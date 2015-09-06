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
__global__ void neg_log(const int n, const Dtype* in, Dtype* out) {
  CUDA_KERNEL_LOOP(index, n) {
    out[index] = -log(max(in[index], Dtype(kLOG_THRESHOLD)));
  }
}

template <typename Dtype>
Dtype SoftmaxMultilabelLossLayer<Dtype>::Forward_gpu(
    const vector<Blob<Dtype>*>& bottom, vector<Blob<Dtype>*>* top) {
  // The forward pass computes the softmax prob values.
  softmax_bottom_vec_[0] = bottom[0];
  softmax_layer_->Forward(softmax_bottom_vec_, &softmax_top_vec_);
  const Dtype* prob_data = prob_.gpu_data();
  Dtype* log_prob_data = log_prob_.mutable_gpu_data();
  Dtype* log_label_data = log_label_.mutable_gpu_data();
  const Dtype* label = bottom[1]->gpu_data();
  int num = prob_.num();
  int count = prob_.count();
  Dtype loss;
//   Dtype loss1;
  
  neg_log<Dtype><<<CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>(
      count, prob_data, log_prob_data);
  neg_log<Dtype><<<CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>(
      count, label, log_label_data);
  
  caffe_gpu_dot<Dtype>(count, label, log_prob_data, &loss);
//   caffe_gpu_dot<Dtype>(count, label, log_label_data, &loss1);     // comment out to make gradient tests work
//   loss -= loss1;                                                  // comment out to make gradient tests work   
  return loss / num;
}

template <typename Dtype>
void SoftmaxMultilabelLossLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const bool propagate_down,
    vector<Blob<Dtype>*>* bottom) {
  // Compute the diff
  Dtype* bottom_diff_net = (*bottom)[0]->mutable_gpu_diff();
  Dtype* bottom_diff_lbl = (*bottom)[1]->mutable_gpu_diff();
  const Dtype* prob_data = prob_.gpu_data();
  const Dtype* log_prob_data = log_prob_.gpu_data();
  int num = prob_.num();
  int count = prob_.count(); 
  
  CUDA_CHECK(cudaMemcpy(bottom_diff_net, prob_data, sizeof(Dtype) * count, cudaMemcpyDeviceToDevice));
//   memcpy(bottom_diff_net, prob_data, sizeof(Dtype) * count);
  const Dtype* label = (*bottom)[1]->gpu_data();  
  caffe_gpu_axpy<Dtype>(count, Dtype(-1), label, bottom_diff_net);
  // Scale down gradient
  caffe_gpu_scal(count, Dtype(1) / num, bottom_diff_net);
  
  CUDA_CHECK(cudaMemcpy(bottom_diff_lbl, log_prob_data, sizeof(Dtype) * count, cudaMemcpyDeviceToDevice));
//   memcpy(bottom_diff_lbl, log_prob_data, sizeof(Dtype) * count);
  caffe_gpu_scal(count, Dtype(1) / num, bottom_diff_lbl);
}

INSTANTIATE_CLASS(SoftmaxMultilabelLossLayer);


}  // namespace caffe
