// Copyright 2014 BVLC and contributors.

#include <algorithm>
#include <cfloat>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/util/math_functions.hpp"

using std::max;

namespace caffe {

template <typename Dtype>
void SoftmaxConditionWithLossLayer<Dtype>::SetUp(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  Layer<Dtype>::SetUp(bottom, top);
  softmax_bottom_vec_.clear();
  softmax_bottom_vec_.push_back(bottom[0]);
  softmax_top_vec_.push_back(&prob_);
  softmax_layer_->SetUp(softmax_bottom_vec_, &softmax_top_vec_);
  if (this->layer_param_.has_loss_param())
    coeff_ = this->layer_param_.loss_param().coeff();
  else
    coeff_ = Dtype(1);
  if (this->layer_param_.has_condition_param()) {
    ge_than_ = this->layer_param_.condition_param().ge_than();
    le_than_ = this->layer_param_.condition_param().le_than();
  }
  else {
    ge_than_ = -FLT_MAX;
    le_than_ = FLT_MAX;
  }
}

template <typename Dtype>
Dtype SoftmaxConditionWithLossLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  // The forward pass computes the softmax prob values.
  softmax_bottom_vec_[0] = bottom[0];
  softmax_layer_->Forward(softmax_bottom_vec_, &softmax_top_vec_);
  const Dtype* prob_data = prob_.cpu_data();
  const Dtype* label = bottom[1]->cpu_data();
  int num = prob_.num();
  int dim = prob_.count() / num;
  Dtype loss = 0;
  for (int i = 0; i < num; ++i) {
    if (label[i] >= ge_than_ && label[i] <= le_than_)
      loss += -log(max(prob_data[i * dim + static_cast<int>(label[i])], Dtype(kLOG_THRESHOLD)));
  }
  return loss * coeff_ / num;
}

template <typename Dtype>
void SoftmaxConditionWithLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const bool propagate_down,
    vector<Blob<Dtype>*>* bottom) {
  // Compute the diff
  Dtype* bottom_diff = (*bottom)[0]->mutable_cpu_diff();
  const Dtype* prob_data = prob_.cpu_data();
  caffe_set(prob_.count(), Dtype(0), bottom_diff);
  const Dtype* label = (*bottom)[1]->cpu_data();
  int num = prob_.num();
  int dim = prob_.count() / num;
  for (int i = 0; i < num; ++i) {
    if (label[i] >= ge_than_ && label[i] <= le_than_) {
      caffe_copy(dim, prob_data + i*dim, bottom_diff + i*dim);
      bottom_diff[i * dim + static_cast<int>(label[i])] -= 1;
    }
  }
  // Scale down gradient
  caffe_scal(prob_.count(), coeff_ / num, bottom_diff);
}


INSTANTIATE_CLASS(SoftmaxConditionWithLossLayer);


}  // namespace caffe
