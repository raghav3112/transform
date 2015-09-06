// Copyright 2014 BVLC and contributors.

#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {

template <typename Dtype>
Dtype InvertGradientLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  (*top)[0]->ShareData(*bottom[0]);
  return Dtype(0.);
}

template <typename Dtype>
void InvertGradientLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
      const bool propagate_down, vector<Blob<Dtype>*>* bottom) {
  const Dtype* top_diff = top[0]->gpu_diff();
  Dtype* bottom_diff = (*bottom)[0]->mutable_gpu_diff();
  int count = top[0]->count();
  caffe_gpu_copy(count, top_diff, bottom_diff);
  caffe_gpu_scal(count, -coeff_, bottom_diff);
  
  iter_++;
  coeff_ = initial_coeff_ + (final_coeff_ - initial_coeff_) * (Dtype(2) / (Dtype(1) + exp(-gamma_ * iter_)) - Dtype(1));
}

INSTANTIATE_CLASS(InvertGradientLayer);

}  // namespace caffe
