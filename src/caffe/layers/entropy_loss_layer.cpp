// Copyright 2014 BVLC and contributors.

#include <algorithm>
#include <cfloat>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/loss_layers.hpp"
#include "caffe/util/math_functions.hpp"

using std::max;

namespace caffe {

template <typename Dtype>
void EntropyLossLayer<Dtype>::SetUp(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  Layer<Dtype>::SetUp(bottom, top);
  if (this->layer_param_.has_loss_param())
    coeff_ = this->layer_param_.loss_param().coeff();
  else
    coeff_ = Dtype(1);
  log_blob_.ReshapeLike(*(bottom[0]));
  if (coeff_ < 0) 
    min_val_ = log(Dtype(bottom[0]->count() / bottom[0]->num())) * coeff_;
  else
    min_val_ = Dtype(0);
}

template <typename Dtype>
Dtype EntropyLossLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  
  const Dtype* data = bottom[0]->cpu_data();
  Dtype* log_data = log_blob_.mutable_cpu_data(); 
  
  int count = bottom[0]->count();
  int num = bottom[0]->num();
  
  for (int i=0; i<count; i++) 
    log_data[i] = log(max(data[i], Dtype(kLOG_THRESHOLD)));
  
  caffe_scal<Dtype>(count, -coeff_, log_data);
  Dtype loss = caffe_cpu_dot<Dtype>(count, log_data, data);
  
  return loss / Dtype(num) - min_val_;
}

template <typename Dtype>
void EntropyLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const bool propagate_down,
    vector<Blob<Dtype>*>* bottom) {
  // Compute the diff
  Dtype* bottom_diff = (*bottom)[0]->mutable_cpu_diff();
  const Dtype* log_data = log_blob_.cpu_data();
  
  int count = (*bottom)[0]->count();
  int num = (*bottom)[0]->num();
  
  caffe_copy<Dtype>(count, log_data, bottom_diff);
  caffe_add_scalar<Dtype>(count, -coeff_, bottom_diff);
  caffe_scal<Dtype>(count, Dtype(1)/Dtype(num), bottom_diff);

}


INSTANTIATE_CLASS(EntropyLossLayer);


}  // namespace caffe
