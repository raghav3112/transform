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
class AccumLayerTest : public ::testing::Test {
 protected:
  AccumLayerTest()
      : blob_bottom_(new Blob<Dtype>(3, 2, 1, 1)),
        blob_top_(new Blob<Dtype>()) {
    // fill the values
    FillerParameter filler_param;
    filler_param.set_min(1.);
    filler_param.set_max(2.);
    UniformFiller<Dtype> filler(filler_param);
    filler.Fill(this->blob_bottom_);
    blob_bottom_vec_.push_back(blob_bottom_);
    blob_top_vec_.push_back(blob_top_);
  }
  virtual ~AccumLayerTest() { delete blob_bottom_; delete blob_top_; }
  Blob<Dtype>* const blob_bottom_;
  Blob<Dtype>* const blob_top_;
  vector<Blob<Dtype>*> blob_bottom_vec_;
  vector<Blob<Dtype>*> blob_top_vec_;
};

typedef ::testing::Types<float, double> Dtypes;
TYPED_TEST_CASE(AccumLayerTest, Dtypes);

TYPED_TEST(AccumLayerTest, TestSetUp) {
  LayerParameter layer_param;
  shared_ptr<AccumLayer<TypeParam> > layer(
      new AccumLayer<TypeParam>(layer_param));
  layer->SetUp(this->blob_bottom_vec_, &(this->blob_top_vec_));
  EXPECT_EQ(this->blob_top_->num(), 1);
  EXPECT_EQ(this->blob_top_->channels(), 2);
  EXPECT_EQ(this->blob_top_->height(), 1);
  EXPECT_EQ(this->blob_top_->width(), 1);
}

TYPED_TEST(AccumLayerTest, TestCPU) {
  LayerParameter layer_param;
  AccumParameter* accum_param =
      layer_param.mutable_accum_param();
  Caffe::set_mode(Caffe::CPU);
  accum_param->set_discount_coeff(0.9);
  accum_param->set_init_alpha(0.);
  shared_ptr<AccumLayer<TypeParam> > layer(
      new AccumLayer<TypeParam>(layer_param));
  layer->SetUp(this->blob_bottom_vec_, &(this->blob_top_vec_));
  layer->Forward(this->blob_bottom_vec_, &(this->blob_top_vec_));
  const TypeParam* data = this->blob_top_->cpu_data();
  const int count = this->blob_top_->count();
  for (int i = 0; i < count; ++i) {
    EXPECT_GE(data[i], 1.);
    EXPECT_LE(data[i], 2.);
  }
}

TYPED_TEST(AccumLayerTest, TestGPU) {
  if (sizeof(TypeParam) == 4 || CAFFE_TEST_CUDA_PROP.major >= 2) {
    LayerParameter layer_param;
    AccumParameter* accum_param =
        layer_param.mutable_accum_param();
    Caffe::set_mode(Caffe::GPU);
    accum_param->set_discount_coeff(0.9);
    accum_param->set_init_alpha(0.);
    shared_ptr<AccumLayer<TypeParam> > layer(
        new AccumLayer<TypeParam>(layer_param));
    layer->SetUp(this->blob_bottom_vec_, &(this->blob_top_vec_));
    layer->Forward(this->blob_bottom_vec_, &(this->blob_top_vec_));
    const TypeParam* data = this->blob_top_->cpu_data();
    const int count = this->blob_top_->count();
    for (int i = 0; i < count; ++i) {
      EXPECT_GE(data[i], 1.);
      EXPECT_LE(data[i], 2.);
    }
  } else {
    LOG(ERROR) << "Skipping test due to old architecture.";
  }
}

TYPED_TEST(AccumLayerTest, TestCPUGradient) {
  LayerParameter layer_param;
  AccumParameter* accum_param =
      layer_param.mutable_accum_param();
  Caffe::set_mode(Caffe::CPU);
  // We only test discount_coeff=0, because otherwise during computing finite differences average value changes, and the result turns out to be wrong
  accum_param->set_discount_coeff(0.);
  accum_param->set_init_alpha(10.);
  AccumLayer<TypeParam> layer(layer_param);
  GradientChecker<TypeParam> checker(1e-4, 1e-3);
  checker.CheckGradientExhaustive(&layer, &(this->blob_bottom_vec_),
      &(this->blob_top_vec_));
}

TYPED_TEST(AccumLayerTest, TestGPUGradient) {
  if (sizeof(TypeParam) == 4 || CAFFE_TEST_CUDA_PROP.major >= 2) {
    LayerParameter layer_param;
    AccumParameter* accum_param =
        layer_param.mutable_accum_param();
    Caffe::set_mode(Caffe::GPU);
    // We only test discount_coeff=0, because otherwise during computing finite differences average value changes, and the result turns out to be wrong
    accum_param->set_discount_coeff(0.);
    accum_param->set_init_alpha(10.);
    AccumLayer<TypeParam> layer(layer_param);
    GradientChecker<TypeParam> checker(1e-4, 1e-2);
    checker.CheckGradient(&layer, &(this->blob_bottom_vec_),
        &(this->blob_top_vec_));
  } else {
    LOG(ERROR) << "Skipping test due to old architecture.";
  }
}

}  // namespace caffe
