// Copyright 2014 BVLC and contributors.
/*
TODO:
- only load parts of the file, in accordance with a prototxt param "max_mem"
*/

#include <stdint.h>
#include <string>
#include <vector>

#include "hdf5.h"
#include "hdf5_hl.h"

#include "caffe/layer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/vision_layers.hpp"

using std::string;

namespace caffe {

template <typename Dtype>
Dtype HDF5DataCouplesLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  HDF5DataCouplesLayer<Dtype>::Forward_cpu(bottom, top);
  return Dtype(0.);
}

template <typename Dtype>
void HDF5DataCouplesLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
      const bool propagate_down, vector<Blob<Dtype>*>* bottom) {
}

INSTANTIATE_CLASS(HDF5DataCouplesLayer);

}  // namespace caffe
