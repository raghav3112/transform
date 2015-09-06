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
__global__ void log_ratio(const int n, const Dtype* in, Dtype* out) {
  CUDA_KERNEL_LOOP(index, n) {
    out[index] = log(max(in[index], Dtype(kLOG_THRESHOLD)));
  }
}

template <typename Dtype>
Dtype SoftmaxEntropyLossLayer<Dtype>::Forward_gpu(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  // The forward pass computes the softmax prob values.
  softmax_bottom_vec_[0] = bottom[0];
  softmax_top_vec_[0] = &prob_;
  softmax_layer_->Forward(softmax_bottom_vec_, &softmax_top_vec_);
  
  const Dtype* prob_data = prob_.gpu_data();
  Dtype* log_data = log_vector_.mutable_gpu_data();
  int num = prob_.num();
  int count = prob_.count();
  
  log_ratio<Dtype><<<CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>(
      count, prob_data, log_data);
  
  caffe_gpu_dot<Dtype>(count, log_data, prob_data, &loss_);
    
  return - min_val_ - loss_ * coeff_ / num;
//   return Forward_cpu(bottom, top);
}

template <typename Dtype>
void SoftmaxEntropyLossLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const bool propagate_down,
    vector<Blob<Dtype>*>* bottom) {
  // Compute the diff
  Dtype* bottom_diff = (*bottom)[0]->mutable_gpu_diff();
  const Dtype* prob_data = prob_.gpu_data();
  const Dtype* log_data = log_vector_.gpu_data();
  Dtype* ones_data = ones_.mutable_gpu_data();
  Dtype* e_persample_data = e_persample_.mutable_gpu_data();
  int num = prob_.num();
  int channels = prob_.channels();
  int count = prob_.count();
  
  caffe_gpu_mul<Dtype>(count, log_data, prob_data, bottom_diff);
  caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, num, channels, channels, Dtype(1),
      bottom_diff, ones_data, Dtype(0), e_persample_data);
  caffe_gpu_mul<Dtype>(count, e_persample_data, prob_data, e_persample_data);
  caffe_gpu_axpy<Dtype>(count, -1, e_persample_data, bottom_diff);  
  caffe_gpu_scal(count, -coeff_ / Dtype(num), bottom_diff); 
}

INSTANTIATE_CLASS(SoftmaxEntropyLossLayer);


}  // namespace caffe
