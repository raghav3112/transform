// Copyright 2014 BVLC and contributors.

#include <algorithm>
#include <cfloat>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {
  
template <typename Dtype>
Dtype AccumLayer<Dtype>::Forward_gpu(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  const Dtype* data = bottom[0]->gpu_data();
  Dtype* top_data = (*top)[0]->mutable_gpu_data();
  const Dtype* ones_data = ones_.gpu_data();
  Dtype* avg_data = avg_blob_.mutable_gpu_data();
  
  alpha_ = alpha_ * discount_coeff_ + Dtype(1);
  caffe_gpu_gemv<Dtype>(CblasTrans, num_, dim_, Dtype(1) / alpha_, data, ones_data, (alpha_ - Dtype(1)) / alpha_, avg_data);
  caffe_gpu_copy<Dtype>(dim_, avg_data, top_data);
  
  return Dtype(0);
}

template <typename Dtype>
void AccumLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const bool propagate_down,
    vector<Blob<Dtype>*>* bottom) {
  // Compute the diff
  Dtype* bottom_diff = (*bottom)[0]->mutable_gpu_diff();
  const Dtype* top_diff = top[0]->gpu_diff();
  
  for (int i=0; i<num_; i++) {
    caffe_gpu_copy<Dtype>(dim_, top_diff, bottom_diff);
    bottom_diff += dim_;
  }
  bottom_diff = (*bottom)[0]->mutable_gpu_diff();
  caffe_gpu_scal<Dtype>(num_ * dim_, Dtype(1) / alpha_ / num_, bottom_diff);
}

INSTANTIATE_CLASS(AccumLayer);


}  // namespace caffe
