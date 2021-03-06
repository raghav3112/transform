// Copyright 2014 BVLC and contributors.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "caffe/common.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/layer.hpp"
#include "caffe/net.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/insert_splits.hpp"
#include "caffe/util/upgrade_proto.hpp"

using std::pair;
using std::map;
using std::set;

namespace caffe {

template <typename Dtype>
Net<Dtype>::Net(const NetParameter& param) {
  Init(param);
}

template <typename Dtype>
Net<Dtype>::Net(const string& param_file) {
  NetParameter param;
  ReadNetParamsFromTextFileOrDie(param_file, &param);
  Init(param);
}

template <typename Dtype>
void Net<Dtype>::Init(const NetParameter& in_param) {
  LOG(INFO) << "Initializing net from parameters: " << std::endl
            << in_param.DebugString();
  // Create a copy of in_param with splits added where necessary.
  NetParameter param;
  InsertSplits(in_param, &param);
  // Basically, build all the layers and set up its connections.
  name_ = param.name();
  map<string, int> blob_name_to_idx;
  set<string> available_blobs;
  int num_layers = param.layers_size();
  CHECK_EQ(param.input_size() * 4, param.input_dim_size())
      << "Incorrect input blob dimension specifications.";
  memory_used_ = 0;
  // set the input blobs
  for (int input_id = 0; input_id < param.input_size(); ++input_id) {
    const int layer_id = -1;  // inputs have fake layer ID -1
    AppendTop(param, layer_id, input_id, &available_blobs, &blob_name_to_idx);
  }
  DLOG(INFO) << "Memory required for data: " << memory_used_ * sizeof(Dtype);
  // For each layer, set up their input and output
  bottom_vecs_.resize(param.layers_size());
  top_vecs_.resize(param.layers_size());
  bottom_id_vecs_.resize(param.layers_size());
  top_id_vecs_.resize(param.layers_size());
  for (int layer_id = 0; layer_id < param.layers_size(); ++layer_id) {
    bool in_place = false;
    const LayerParameter& layer_param = param.layers(layer_id);
    layers_.push_back(shared_ptr<Layer<Dtype> >(GetLayer<Dtype>(layer_param)));
    layer_names_.push_back(layer_param.name());
    LOG(INFO) << "Creating Layer " << layer_param.name();
    bool need_backward = param.force_backward();
    // Figure out this layer's input and output
    for (int bottom_id = 0; bottom_id < layer_param.bottom_size();
         ++bottom_id) {
      const int blob_id = AppendBottom(param, layer_id, bottom_id,
                                       &available_blobs, &blob_name_to_idx);
      // If a blob needs backward, this layer should provide it.
      need_backward |= blob_need_backward_[blob_id];
    }
    for (int top_id = 0; top_id < layer_param.top_size(); ++top_id) {
      AppendTop(param, layer_id, top_id, &available_blobs, &blob_name_to_idx);
    }
    // After this layer is connected, set it up.
    // LOG(INFO) << "Setting up " << layer_names_[layer_id];
    layers_[layer_id]->SetUp(bottom_vecs_[layer_id], &top_vecs_[layer_id]);
    if (layer_param.force_output())
      LOG(INFO) << "This layers outputs its blobs";
    for (int top_id = 0; top_id < top_vecs_[layer_id].size(); ++top_id) {
      LOG(INFO) << "Top shape: " << top_vecs_[layer_id][top_id]->num() << " "
          << top_vecs_[layer_id][top_id]->channels() << " "
          << top_vecs_[layer_id][top_id]->height() << " "
          << top_vecs_[layer_id][top_id]->width() << " ("
          << top_vecs_[layer_id][top_id]->count() << ")";
      if (layer_param.force_output())
        net_extra_output_blobs_.push_back(top_vecs_[layer_id][top_id]);  
    }
    DLOG(INFO) << "Memory required for data: " << memory_used_ * sizeof(Dtype);
    const int blobs_lr_size = layers_[layer_id]->layer_param().blobs_lr_size();
    CHECK(blobs_lr_size == layers_[layer_id]->blobs().size() ||
          blobs_lr_size == 0) << "Incorrect blobs lr size: should be either 0 "
        << "or the same as the number of the layer's parameter blobs.";
    if (blobs_lr_size) {
      // Check if this layer needs backward operation itself
      for (int param_id = 0; param_id < blobs_lr_size; ++param_id) {
        need_backward |=
            (layers_[layer_id]->layer_param().blobs_lr(param_id) > 0);
      }
    } else if (layers_[layer_id]->blobs().size()) {
      // catch: if a layer param does not specify blobs_lr, we should assume the
      // learning rate to be 1. Thus we will need to perform backward.
      need_backward = true;
    }
    // Finally, set the backward flag
    layer_need_backward_.push_back(need_backward);
    if (need_backward) {
      LOG(INFO) << layer_names_[layer_id] << " needs backward computation.";
      for (int top_id = 0; top_id < top_id_vecs_[layer_id].size(); ++top_id) {
        blob_need_backward_[top_id_vecs_[layer_id][top_id]] = true;
      }
    } else {
      LOG(INFO) << layer_names_[layer_id]
                << " does not need backward computation.";
    }
  }
  // In the end, all remaining blobs are considered output blobs.
  for (set<string>::iterator it = available_blobs.begin();
      it != available_blobs.end(); ++it) {
    LOG(INFO) << "This network produces output " << *it;
    net_output_blobs_.push_back(blobs_[blob_name_to_idx[*it]].get());
    net_output_blob_indices_.push_back(blob_name_to_idx[*it]);
  }
  for (size_t blob_id = 0; blob_id < blob_names_.size(); ++blob_id) {
    blob_names_index_[blob_names_[blob_id]] = blob_id;
  }
  for (size_t layer_id = 0; layer_id < layer_names_.size(); ++layer_id) {
    layer_names_index_[layer_names_[layer_id]] = layer_id;
  }
  
  // Share weights
  for (size_t layer_id = 0; layer_id < layer_names_.size(); ++layer_id) {
    shared_ptr<Layer<Dtype> > curr_layer = layers_[layer_id];
    for (int i=0; i < curr_layer->layer_param().share_weights_with_size(); i++) {
      
      std::string other_layer_name = layers_[layer_id]->layer_param().share_weights_with(i);
      CHECK(layer_names_index_.count(other_layer_name)) << "Layer " << layer_names_[layer_id] <<
          " cannot share weights with layer " << other_layer_name << ": layer " << 
          other_layer_name << " not found";
          
      shared_ptr<Layer<Dtype> > other_layer = layers_[layer_names_index_[other_layer_name]];
      CHECK_EQ(curr_layer->blobs().size(), other_layer->blobs().size());
      
      LOG(INFO) << "Layer " << layer_names_[layer_id] << " shares weights with layer " << other_layer_name;
      for (int j=0; j < curr_layer->blobs().size(); j++) {
        shared_ptr<Blob<Dtype> > curr_blob = curr_layer->blobs()[j];
        shared_ptr<Blob<Dtype> > other_blob = other_layer->blobs()[j];
        CHECK_EQ(curr_blob->num(), other_blob->num());
        CHECK_EQ(curr_blob->channels(), other_blob->channels());
        CHECK_EQ(curr_blob->height(), other_blob->height());
        CHECK_EQ(curr_blob->width(), other_blob->width());
        curr_blob->ShareData(*other_blob);
      }
    }
  } 
  
  GetLearningRateAndWeightDecay();
  LOG(INFO) << "Network initialization done.";
  LOG(INFO) << "Memory required for data: " << memory_used_ * sizeof(Dtype);
}

// Helper for Net::Init: add a new input or top blob to the net.  (Inputs have
// layer_id == -1, tops have layer_id >= 0.)
template <typename Dtype>
void Net<Dtype>::AppendTop(const NetParameter& param, const int layer_id,
                           const int top_id, set<string>* available_blobs,
                           map<string, int>* blob_name_to_idx) {
  shared_ptr<LayerParameter> layer_param((layer_id >= 0) ?
    (new LayerParameter(param.layers(layer_id))) : NULL);
  const string& blob_name = layer_param ?
      layer_param->top(top_id) : param.input(top_id);
  // Check if we are doing in-place computation
  if (layer_param && layer_param->bottom_size() > top_id &&
      blob_name == layer_param->bottom(top_id)) {
    // In-place computation
    LOG(INFO) << layer_param->name() << " -> " << blob_name << " (in-place)";
    top_vecs_[layer_id].push_back(blobs_[(*blob_name_to_idx)[blob_name]].get());
    top_id_vecs_[layer_id].push_back((*blob_name_to_idx)[blob_name]);
  } else if (blob_name_to_idx->find(blob_name) != blob_name_to_idx->end()) {
    // If we are not doing in-place computation but have duplicated blobs,
    // raise an error.
    LOG(FATAL) << "Duplicate blobs produced by multiple sources.";
  } else {
    // Normal output.
    if (layer_param) {
      LOG(INFO) << layer_param->name() << " -> " << blob_name;
    } else {
      LOG(INFO) << "Input " << top_id << " -> " << blob_name;
    }
    shared_ptr<Blob<Dtype> > blob_pointer(new Blob<Dtype>());
    const int blob_id = blobs_.size();
    blobs_.push_back(blob_pointer);
    blob_names_.push_back(blob_name);
    blob_need_backward_.push_back(param.force_backward());
    (*blob_name_to_idx)[blob_name] = blob_id;
    if (layer_id == -1) {
      // Set the (explicitly specified) dimensions of the input blob.
      blob_pointer->Reshape(param.input_dim(top_id * 4),
                            param.input_dim(top_id * 4 + 1),
                            param.input_dim(top_id * 4 + 2),
                            param.input_dim(top_id * 4 + 3));
      net_input_blob_indices_.push_back(blob_id);
      net_input_blobs_.push_back(blob_pointer.get());
    } else {
      top_id_vecs_[layer_id].push_back(blob_id);
      top_vecs_[layer_id].push_back(blob_pointer.get());
    }
    memory_used_ += blob_pointer->count();
  }
  available_blobs->insert(blob_name);
}

// Helper for Net::Init: add a new bottom blob to the net.
template <typename Dtype>
int Net<Dtype>::AppendBottom(const NetParameter& param,
    const int layer_id, const int bottom_id,
    set<string>* available_blobs, map<string, int>* blob_name_to_idx) {
  const LayerParameter& layer_param = param.layers(layer_id);
  const string& blob_name = layer_param.bottom(bottom_id);
  if (available_blobs->find(blob_name) == available_blobs->end()) {
    LOG(FATAL) << "Unknown blob input " << blob_name
               << " (at index " << bottom_id << ") to layer " << layer_id;
  }
  const int blob_id = (*blob_name_to_idx)[blob_name];
  LOG(INFO) << layer_names_[layer_id] << " <- " << blob_name;
  bottom_vecs_[layer_id].push_back(blobs_[blob_id].get());
  bottom_id_vecs_[layer_id].push_back(blob_id);
  available_blobs->erase(blob_name);
  bool need_backward = param.force_backward() || blob_need_backward_[blob_id];
  return blob_id;
}

template <typename Dtype>
void Net<Dtype>::GetLearningRateAndWeightDecay() {
  LOG(INFO) << "Collecting Learning Rate and Weight Decay.";
  for (int i = 0; i < layers_.size(); ++i) {
    vector<shared_ptr<Blob<Dtype> > >& layer_blobs = layers_[i]->blobs();
    for (int j = 0; j < layer_blobs.size(); ++j) {
      params_.push_back(layer_blobs[j]);
    }
    // push the learning rate mutlipliers
    if (layers_[i]->layer_param().blobs_lr_size()) {
      CHECK_EQ(layers_[i]->layer_param().blobs_lr_size(), layer_blobs.size());
      for (int j = 0; j < layer_blobs.size(); ++j) {
        float local_lr = layers_[i]->layer_param().blobs_lr(j);
        CHECK_GE(local_lr, 0.);
        params_lr_.push_back(local_lr);
      }
    } else {
      for (int j = 0; j < layer_blobs.size(); ++j) {
        params_lr_.push_back(1.);
      }
    }
    // push the weight decay multipliers
    if (layers_[i]->layer_param().weight_decay_size()) {
      CHECK_EQ(layers_[i]->layer_param().weight_decay_size(),
          layer_blobs.size());
      for (int j = 0; j < layer_blobs.size(); ++j) {
        float local_decay = layers_[i]->layer_param().weight_decay(j);
        CHECK_GE(local_decay, 0.);
        params_weight_decay_.push_back(local_decay);
      }
    } else {
      for (int j = 0; j < layer_blobs.size(); ++j) {
        params_weight_decay_.push_back(1.);
      }
    }
    // push the weight constraints
    params_weight_constraint_.push_back(layers_[i]->layer_param().max_weight_norm());
  }
}

template <typename Dtype>
const vector<Blob<Dtype>*>& Net<Dtype>::ForwardPrefilled(Dtype* loss) {
  losses_.clear();
  if (loss != NULL) {
    *loss = Dtype(0.);
  }
  for (int i = 0; i < layers_.size(); ++i) {
//     LOG(ERROR) << "Forwarding " << layer_names_[i];
    Dtype layer_loss = layers_[i]->Forward(bottom_vecs_[i], &top_vecs_[i]);
    if (loss != NULL) {
      *loss += layer_loss;
      LOG(INFO) << "timnit"  << loss;
      if (fabs(layer_loss) > 0)
        losses_.push_back(std::make_pair(i, layer_loss));
    }
  }
  return net_output_blobs_;
}

template <typename Dtype>
const vector<Blob<Dtype>*> Net<Dtype>::ForwardPrefilled_extra_output(Dtype* loss) {
  losses_.clear();
  if (loss != NULL) {
    *loss = Dtype(0.);
  }
  for (int i = 0; i < layers_.size(); ++i) {
//     LOG(ERROR) << "Forwarding " << layer_names_[i];
    Dtype layer_loss = layers_[i]->Forward(bottom_vecs_[i], &top_vecs_[i]);
    if (loss != NULL) {
      *loss += layer_loss;
      if (fabs(layer_loss) > 0)
        losses_.push_back(std::make_pair(i, layer_loss));
    }
  }
  vector<Blob<Dtype>*> all_output_blobs; 
  all_output_blobs.reserve( net_output_blobs_.size() + net_extra_output_blobs_.size() );
  all_output_blobs.insert( all_output_blobs.end(), net_output_blobs_.begin(), net_output_blobs_.end() );
  all_output_blobs.insert( all_output_blobs.end(), net_extra_output_blobs_.begin(), net_extra_output_blobs_.end() );
  return all_output_blobs;
}

template <typename Dtype>
const vector<Blob<Dtype>*>& Net<Dtype>::Forward(
    const vector<Blob<Dtype>*> & bottom, Dtype* loss) {
  // Copy bottom to internal bottom
  for (int i = 0; i < bottom.size(); ++i) {
    net_input_blobs_[i]->CopyFrom(*bottom[i]);
  }
  return ForwardPrefilled(loss);
}

template <typename Dtype>
string Net<Dtype>::Forward(const string& input_blob_protos, Dtype* loss) {
  BlobProtoVector blob_proto_vec;
  if (net_input_blobs_.size()) {
    blob_proto_vec.ParseFromString(input_blob_protos);
    CHECK_EQ(blob_proto_vec.blobs_size(), net_input_blobs_.size())
        << "Incorrect input size.";
    for (int i = 0; i < blob_proto_vec.blobs_size(); ++i) {
      net_input_blobs_[i]->FromProto(blob_proto_vec.blobs(i));
    }
  }
  ForwardPrefilled(loss);
  blob_proto_vec.Clear();
  for (int i = 0; i < net_output_blobs_.size(); ++i) {
    net_output_blobs_[i]->ToProto(blob_proto_vec.add_blobs());
  }
  string output;
  blob_proto_vec.SerializeToString(&output);
  return output;
}


template <typename Dtype>
void Net<Dtype>::Backward() {
  for (int i = layers_.size() - 1; i >= 0; --i) {
    if (layer_need_backward_[i]) {
      layers_[i]->Backward(top_vecs_[i], true, &bottom_vecs_[i]);
    }
  }
}

template <typename Dtype>
void Net<Dtype>::ShareTrainedLayersWith(Net* other) {
  int num_source_layers = other->layers().size();
  for (int i = 0; i < num_source_layers; ++i) {
    Layer<Dtype>* source_layer = other->layers()[i].get();
    const string& source_layer_name = other->layer_names()[i];
    int target_layer_id = 0;
    while (target_layer_id != layer_names_.size() &&
        layer_names_[target_layer_id] != source_layer_name) {
      ++target_layer_id;
    }
    if (target_layer_id == layer_names_.size()) {
      DLOG(INFO) << "Ignoring source layer " << source_layer_name;
      continue;
    }
    DLOG(INFO) << "Copying source layer " << source_layer_name << " to " << layer_names_[target_layer_id];
    vector<shared_ptr<Blob<Dtype> > >& target_blobs =
        layers_[target_layer_id]->blobs();
    CHECK_EQ(target_blobs.size(), source_layer->blobs().size())
        << "Incompatible number of blobs for layer " << source_layer_name;
    for (int j = 0; j < target_blobs.size(); ++j) {
      Blob<Dtype>* source_blob = source_layer->blobs()[j].get();
      CHECK_EQ(target_blobs[j]->num(), source_blob->num());
      CHECK_EQ(target_blobs[j]->channels(), source_blob->channels());
      CHECK_EQ(target_blobs[j]->height(), source_blob->height());
      CHECK_EQ(target_blobs[j]->width(), source_blob->width());
      target_blobs[j]->ShareData(*source_blob);
    }
  }
}

template <typename Dtype>
void Net<Dtype>::CopyTrainedLayersFrom(const NetParameter& param) {
  int num_source_layers = param.layers_size();
  for (int i = 0; i < num_source_layers; ++i) {
    const LayerParameter& source_layer = param.layers(i);
    const string& source_layer_name = source_layer.name();
    int target_layer_id = 0;
    while (target_layer_id != layer_names_.size() &&
        layer_names_[target_layer_id] != source_layer_name) {
      ++target_layer_id;
    }
    if (target_layer_id == layer_names_.size()) {
      LOG(INFO) << "Ignoring source layer " << source_layer_name;
      continue;
    }
    LOG(INFO) << "Copying source layer " << source_layer_name;
    vector<shared_ptr<Blob<Dtype> > >& target_blobs =
        layers_[target_layer_id]->blobs();
    CHECK_EQ(target_blobs.size(), source_layer.blobs_size())
        << "Incompatible number of blobs for layer " << source_layer_name;    
    for (int j = 0; j < target_blobs.size(); ++j) {
      LOG(INFO) << "blob " << target_blobs[j]->num() << "x" << target_blobs[j]->channels() <<
        "x" << target_blobs[j]->height() << "x" << target_blobs[j]->width();
      CHECK_EQ(target_blobs[j]->num(), source_layer.blobs(j).num());
      CHECK_EQ(target_blobs[j]->channels(), source_layer.blobs(j).channels());
      CHECK_EQ(target_blobs[j]->height(), source_layer.blobs(j).height());
      CHECK_EQ(target_blobs[j]->width(), source_layer.blobs(j).width());
      target_blobs[j]->FromProto_noReshape(source_layer.blobs(j));
    }
  }
}

template <typename Dtype>
void Net<Dtype>::CopyTrainedLayersFrom(const string trained_filename) {
  NetParameter param;
  ReadNetParamsFromBinaryFileOrDie(trained_filename, &param);
  CopyTrainedLayersFrom(param);
}

template <typename Dtype>
void Net<Dtype>::ToProto(NetParameter* param, bool write_diff) {
  param->Clear();
  param->set_name(name_);
  // Add bottom and top
  for (int i = 0; i < net_input_blob_indices_.size(); ++i) {
    param->add_input(blob_names_[net_input_blob_indices_[i]]);
  }
  DLOG(INFO) << "Serializing " << layers_.size() << " layers";
  for (int i = 0; i < layers_.size(); ++i) {
    LayerParameter* layer_param = param->add_layers();
    for (int j = 0; j < bottom_id_vecs_[i].size(); ++j) {
      layer_param->add_bottom(blob_names_[bottom_id_vecs_[i][j]]);
    }
    for (int j = 0; j < top_id_vecs_[i].size(); ++j) {
      layer_param->add_top(blob_names_[top_id_vecs_[i][j]]);
    }
    layers_[i]->ToProto(layer_param, write_diff);
  }
}

template <typename Dtype>
void Net<Dtype>::Update() {
  for (int i = 0; i < params_.size(); ++i) {
    params_[i]->Update();
  }
}

template <typename Dtype>
bool Net<Dtype>::has_blob(const string& blob_name) {
  return blob_names_index_.find(blob_name) != blob_names_index_.end();
}

template <typename Dtype>
const shared_ptr<Blob<Dtype> > Net<Dtype>::blob_by_name(
    const string& blob_name) {
  shared_ptr<Blob<Dtype> > blob_ptr;
  if (has_blob(blob_name)) {
    blob_ptr = blobs_[blob_names_index_[blob_name]];
  } else {
    blob_ptr.reset((Blob<Dtype>*)(NULL));
    LOG(WARNING) << "Unknown blob name " << blob_name;
  }
  return blob_ptr;
}

template <typename Dtype>
bool Net<Dtype>::has_layer(const string& layer_name) {
  return layer_names_index_.find(layer_name) != layer_names_index_.end();
}

template <typename Dtype>
const shared_ptr<Layer<Dtype> > Net<Dtype>::layer_by_name(
    const string& layer_name) {
  shared_ptr<Layer<Dtype> > layer_ptr;
  if (has_layer(layer_name)) {
    layer_ptr = layers_[layer_names_index_[layer_name]];
  } else {
    layer_ptr.reset((Layer<Dtype>*)(NULL));
    LOG(WARNING) << "Unknown layer name " << layer_name;
  }
  return layer_ptr;
}

template <typename Dtype>
void Net<Dtype>::initialize_weights() {
  for (size_t layer_id = 0; layer_id < layer_names_.size(); ++layer_id) {
    shared_ptr<Layer<Dtype> > curr_layer = layers_[layer_id];
    for (int i=0; i < curr_layer->layer_param().initialize_weights_with_size(); i++) {
      
      std::string other_layer_name = layers_[layer_id]->layer_param().initialize_weights_with(i);
      CHECK(layer_names_index_.count(other_layer_name)) << "Layer " << layer_names_[layer_id] <<
          " cannot share weights with layer " << other_layer_name << ": layer " << 
          other_layer_name << " not found";
          
      shared_ptr<Layer<Dtype> > other_layer = layers_[layer_names_index_[other_layer_name]];
      CHECK_EQ(curr_layer->blobs().size(), other_layer->blobs().size());
      
      LOG(INFO) << "Layer " << layer_names_[layer_id] << " initializes weights with layer " << other_layer_name;
      for (int j=0; j < curr_layer->blobs().size(); j++) {
        shared_ptr<Blob<Dtype> > curr_blob = curr_layer->blobs()[j];
        shared_ptr<Blob<Dtype> > other_blob = other_layer->blobs()[j];
        CHECK_EQ(curr_blob->num(), other_blob->num());
        CHECK_EQ(curr_blob->channels(), other_blob->channels());
        CHECK_EQ(curr_blob->height(), other_blob->height());
        CHECK_EQ(curr_blob->width(), other_blob->width());
        curr_blob->CopyFrom(*other_blob, false, false);
      }
    }
  }
}

INSTANTIATE_CLASS(Net);

}  // namespace caffe
