// Copyright 2014 BVLC and contributors.

#include <cstring>
#include <vector>

#include "cuda_runtime.h"
#include "gtest/gtest.h"
#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/filler.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/test/test_gradient_check_util.hpp"

#include "caffe/test/test_caffe_main.hpp"

namespace caffe {

extern cudaDeviceProp CAFFE_TEST_CUDA_PROP;

template <typename Dtype>
class LabelToOnehotLayerTest : public ::testing::Test {
 protected:
  LabelToOnehotLayerTest()
      : blob_bottom_(new Blob<Dtype>(5, 1, 1, 1)),
        blob_top_(new Blob<Dtype>()) {
    // fill the values
    int num = blob_bottom_->num();
    for (int i = 0; i < num; ++i) {
      blob_bottom_->mutable_cpu_data()[i] = Dtype(i % 3);
    }
    blob_bottom_vec_.push_back(blob_bottom_);
    blob_top_vec_.push_back(blob_top_);
  }
  virtual ~LabelToOnehotLayerTest() { delete blob_bottom_; delete blob_top_; }
  Blob<Dtype>* const blob_bottom_;
  Blob<Dtype>* const blob_top_;
  vector<Blob<Dtype>*> blob_bottom_vec_;
  vector<Blob<Dtype>*> blob_top_vec_;
};

typedef ::testing::Types<float, double> Dtypes;
TYPED_TEST_CASE(LabelToOnehotLayerTest, Dtypes);

TYPED_TEST(LabelToOnehotLayerTest, TestSetUp) {
  LayerParameter layer_param;
  InnerProductParameter* inner_product_param =
      layer_param.mutable_inner_product_param();
  inner_product_param->set_num_output(4);
  shared_ptr<LabelToOnehotLayer<TypeParam> > layer(
      new LabelToOnehotLayer<TypeParam>(layer_param));
  layer->SetUp(this->blob_bottom_vec_, &(this->blob_top_vec_));
  EXPECT_EQ(this->blob_top_->num(), 5);
  EXPECT_EQ(this->blob_top_->channels(), 4);
  EXPECT_EQ(this->blob_top_->height(), 1);
  EXPECT_EQ(this->blob_top_->width(), 1);
}

TYPED_TEST(LabelToOnehotLayerTest, TestCPU) {
  LayerParameter layer_param;
  InnerProductParameter* inner_product_param =
      layer_param.mutable_inner_product_param();
  inner_product_param->set_num_output(4);
  Caffe::set_mode(Caffe::CPU);
  shared_ptr<LabelToOnehotLayer<TypeParam> > layer(
      new LabelToOnehotLayer<TypeParam>(layer_param));
  layer->SetUp(this->blob_bottom_vec_, &(this->blob_top_vec_));
  layer->Forward(this->blob_bottom_vec_, &(this->blob_top_vec_));
  const TypeParam* data = this->blob_top_->cpu_data();
  const int num = this->blob_top_->num();
  const int channels = this->blob_top_->channels();
  for (int i = 0; i < num; ++i) {
    for (int j = 0; j < channels; ++j) {
      if (j == i % 3)
        EXPECT_EQ(data[i*channels + j], 1.);
      else
        EXPECT_EQ(data[i*channels + j], 0.);
    }
  }
}

TYPED_TEST(LabelToOnehotLayerTest, TestGPU) {
  if (sizeof(TypeParam) == 4 || CAFFE_TEST_CUDA_PROP.major >= 2) {
    LayerParameter layer_param;
    InnerProductParameter* inner_product_param =
        layer_param.mutable_inner_product_param();
    inner_product_param->set_num_output(4);
    Caffe::set_mode(Caffe::GPU);
    shared_ptr<LabelToOnehotLayer<TypeParam> > layer(
        new LabelToOnehotLayer<TypeParam>(layer_param));
    layer->SetUp(this->blob_bottom_vec_, &(this->blob_top_vec_));
    layer->Forward(this->blob_bottom_vec_, &(this->blob_top_vec_));
    const TypeParam* data = this->blob_top_->cpu_data();
    const int num = this->blob_top_->num();
    const int channels = this->blob_top_->channels();
    for (int i = 0; i < num; ++i) {
      for (int j = 0; j < channels; ++j) {
        if (j == i % 3)
          EXPECT_EQ(data[i*channels + j], 1.);
        else
          EXPECT_EQ(data[i*channels + j], 0.);
      }
    }
  } else {
    LOG(ERROR) << "Skipping test due to old architecture.";
  }
}

}  // namespace caffe
