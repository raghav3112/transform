// Copyright 2014 BVLC and contributors.

#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {

template <typename Dtype>
void ReshapeLayer<Dtype>::SetUp(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  Layer<Dtype>::SetUp(bottom, top);
  int num = bottom[0]->num();
  int channels_out = this->layer_param_.reshape_param().channels();
  int height_out = this->layer_param_.reshape_param().height();
  int width_out = this->layer_param_.reshape_param().width();
  (*top)[0]->Reshape(num, channels_out, height_out, width_out);
  CHECK_EQ(bottom[0]->count(), (*top)[0]->count());
}

template <typename Dtype>
Dtype ReshapeLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  (*top)[0]->ShareData(*bottom[0]);
  return Dtype(0.);
}

template <typename Dtype>
void ReshapeLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const bool propagate_down, vector<Blob<Dtype>*>* bottom) {
  (*bottom)[0]->ShareDiff(*top[0]);
}

INSTANTIATE_CLASS(ReshapeLayer);

}  // namespace caffe
