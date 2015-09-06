// Copyright 2014 BVLC and contributors.

#include <cublas_v2.h>

#include <vector>

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/filler.hpp"
#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {
  
template <typename Dtype>
__global__ void make_nonneg(const int n, Dtype* in) {
  CUDA_KERNEL_LOOP(index, n) {
    in[index] = max(0., in[index]);      
  }
}

template <typename Dtype>
Dtype InnerProductOrthLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
    vector<Blob<Dtype>*>* top) {
  const Dtype* bottom_data = bottom[0]->gpu_data();
  Dtype* top_data = (*top)[0]->mutable_gpu_data();
  Dtype* weight = this->blobs_[0]->mutable_gpu_data();
  
  // Orthogonalizing
  if (Caffe::phase() == Caffe::TRAIN && orth_step_ > 0) {
    if (!(iter_ % orth_step_) && (orth_before_iter_ == 0 || iter_ < orth_before_iter_)) {
//       LOG(INFO) << "Orthogonalizing, iter=" << iter_;
      switch (orth_method_) {
        case OrthParameter_OrthMethod_ESMAEILI:
        {  
//           LOG(INFO) << "ESMAEILI";
          Dtype* gram = this->gram_.mutable_gpu_data();
          Dtype* kk = this->kk_.mutable_gpu_data();
          Dtype* ak = this->ak_.mutable_gpu_data();
          const Dtype* id = this->id_.gpu_data();
          Dtype error;

          caffe_gpu_scal(N_*K_, Dtype(1) / col_norm_, weight);
          for (int ni=0; ni<max_num_iter_; ni++) {
            caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasTrans, N_, N_, K_, (Dtype)1.,
                weight, weight, (Dtype)0., gram);
            caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_, N_, N_, esmaeili_coeff_,
                gram, gram, (Dtype)0., kk);
            caffe_gpu_axpy<Dtype>(N_*N_, -(1. + esmaeili_coeff_), gram, kk);
            caffe_gpu_axpy<Dtype>(N_*N_, Dtype(2), id, kk);
            caffe_gpu_axpy<Dtype>(N_*N_, Dtype(-1), id, gram);
            error = caffe_gpu_norm2(N_*N_, gram);
            //LOG(INFO) << "Iter " << ni+1 << "  ||Gram - id||=" << error;
            if (error < min_error_ + eps_)
              ni = max_num_iter_;
            else {
              LOG(INFO) << "Iter " << iter_ << "." << ni <<"  ||Gram - id||=" << error;
              caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_, K_, N_, (Dtype)1.,
                kk, weight, (Dtype)0., ak);
              caffe_gpu_copy(N_*K_, ak, weight);
            }
          }
          caffe_gpu_scal(N_*K_, col_norm_, weight);
          break;
        }
        case OrthParameter_OrthMethod_NORM_L2:
        {
          normalize_weights(min_norm_, max_norm_, target_norm_);
          break;
        }
        case OrthParameter_OrthMethod_NORM_L1:
        {
          normalize_weights_l1(min_norm_, max_norm_, target_norm_);
          break;
        }
        case OrthParameter_OrthMethod_NORM_L1_NONNEG:
        {
//           LOG(INFO) << "Make nonneg";
          make_nonneg<Dtype><<<CAFFE_GET_BLOCKS(this->blobs_[0]->count()), CAFFE_CUDA_NUM_THREADS>>>(
              this->blobs_[0]->count(), weight);
//           LOG(INFO) << "Normalize";
          normalize_weights_l1(min_norm_, max_norm_, target_norm_);
          break;
        }
        case OrthParameter_OrthMethod_NONE:
//           LOG(INFO) << "NONE";
          break;
        default:
          LOG(FATAL) << "Unknown orthogonalization method";
          break;  
      }
    }
    iter_++;
  }
  
  caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasTrans, M_, N_, K_, (Dtype)1.,
      bottom_data, weight, (Dtype)0., top_data);
  if (bias_term_) {
    caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, M_, N_, 1, (Dtype)1.,
        reinterpret_cast<const Dtype*>(bias_multiplier_->gpu_data()),
        this->blobs_[1]->gpu_data(), (Dtype)1., top_data);
  }  
  
  return Dtype(0);
}

template <typename Dtype>
void InnerProductOrthLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const bool propagate_down,
    vector<Blob<Dtype>*>* bottom) {
  const Dtype* top_diff = top[0]->gpu_diff();
  const Dtype* bottom_data = (*bottom)[0]->gpu_data();
  // Gradient with respect to weight
  caffe_gpu_gemm<Dtype>(CblasTrans, CblasNoTrans, N_, K_, M_, (Dtype)1.,
      top_diff, bottom_data, (Dtype)0., this->blobs_[0]->mutable_gpu_diff());
  if (bias_term_) {
    // Gradient with respect to bias
    caffe_gpu_gemv<Dtype>(CblasTrans, M_, N_, (Dtype)1., top_diff,
        reinterpret_cast<const Dtype*>(bias_multiplier_->gpu_data()),
        (Dtype)0., this->blobs_[1]->mutable_gpu_diff());
  }
  if (propagate_down) {
    // Gradient with respect to bottom data
    caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, M_, K_, N_, (Dtype)1.,
        top_diff, this->blobs_[0]->gpu_data(), (Dtype)0.,
        (*bottom)[0]->mutable_gpu_diff());
  }
}

INSTANTIATE_CLASS(InnerProductOrthLayer);

}  // namespace caffe
