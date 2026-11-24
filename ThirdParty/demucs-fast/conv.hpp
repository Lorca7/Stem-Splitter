// Faster drop-in replacement for demucs.cpp's src/conv.hpp (https://github.com/sevagh/demucs.cpp, MIT).
//
// Same function templates and the same results as upstream, but much less work per call:
//  - weights are used in place (the upstream code re-shuffled every weight tensor on every call)
//  - im2col gathers contiguous channel runs, and 1x1 convolutions skip im2col entirely
//  - the GEMM writes straight into the output tensor (no strided element-by-element copy)
//  - transposed convolutions multiply first and scatter-add afterwards ("col2im"), instead of
//    multiplying a matrix that is mostly zeros (4x fewer FLOPs for the stride-4 decoders)
//  - conv1d wrappers avoid tensor shuffles when they're pure reshapes
//
// Copied over the upstream header at configure time by cmake/Demucs.cmake.

#ifndef CONV_HPP
#define CONV_HPP

#include "model.hpp"
#include "tensor.hpp"
#include <Eigen/Dense>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unsupported/Eigen/CXX11/Tensor>

namespace demucscpp
{

namespace fastconv
{
using RowMap = Eigen::Map<Eigen::MatrixXf>;
using ConstMap = Eigen::Map<const Eigen::MatrixXf>;

inline float gelu(float v) { return 0.5f * v * (1.0f + std::erf(v / std::sqrt(2.0f))); }

inline void gelu_inplace(float *data, Eigen::Index n)
{
    for (Eigen::Index i = 0; i < n; ++i)
        data[i] = gelu(data[i]);
}

// Output size formulas exactly as upstream (including its use of ceil).
inline int out_size(int in, int pad, int kernel, int stride)
{
    return static_cast<int>(std::ceil((float)(in + 2 * pad - (kernel - 1) - 1) / stride)) + 1;
}

inline int col_size(int in, int pad, int kernel, int stride, int dilation)
{
    return static_cast<int>(std::ceil((in + 2 * pad - dilation * (kernel - 1) - 1) / float(stride)) + 1);
}

// Forward convolution of x (Cin, H, W) with weights w laid out (Cout, Cin, KH, KW), column-major.
// The result is written into y (Cout, outH, outW), which must already be allocated.
template <int Cin, int Cout, int KH, int KW, int SH, int SW, int PH, int PW, int DH, int DW, bool Gelu>
void conv2d_into(const float *x, int H, int W, const float *w, const float *b, Eigen::Tensor3dXf &y)
{
    const int outH = (int)y.dimension(1), outW = (int)y.dimension(2);
    const int colH = col_size(H, PH, KH, SH, DH), colW = col_size(W, PW, KW, SW, DW);
    const int P = colH * colW;
    constexpr int K = Cin * KH * KW;

    // Weights: column k = c + Cin * (kh + KH * kw) of the (Cout x K) matrix is exactly the memory
    // layout of a column-major (Cout, Cin, KH, KW) tensor, so no copy is needed.
    ConstMap Wm (w, Cout, K);

    Eigen::MatrixXf Y (Cout, P);

    if (KH == 1 && KW == 1 && SH == 1 && SW == 1 && PH == 0 && PW == 0)
    {
        // 1x1 convolution: the input tensor already is the (Cin x H*W) matrix.
        Y.noalias() = Wm * ConstMap (x, Cin, (Eigen::Index)H * W);
    }
    else
    {
        // im2col: column p = h + colH * w; within it, row k = c + Cin * (kh + KH * kw).
        Eigen::MatrixXf col (K, P);
        float *colData = col.data();

        for (int wc = 0; wc < colW; ++wc)
        {
            for (int hc = 0; hc < colH; ++hc)
            {
                float *dst = colData + (Eigen::Index)K * (hc + (Eigen::Index)colH * wc);

                for (int kw = 0; kw < KW; ++kw)
                {
                    const int wi = wc * SW + kw * DW - PW;

                    for (int kh = 0; kh < KH; ++kh)
                    {
                        const int hi = hc * SH + kh * DH - PH;
                        float *d = dst + Cin * (kh + KH * kw);

                        if (hi >= 0 && hi < H && wi >= 0 && wi < W)
                            std::memcpy (d, x + (Eigen::Index)Cin * (hi + (Eigen::Index)H * wi), sizeof (float) * Cin);
                        else
                            std::memset (d, 0, sizeof (float) * Cin);
                    }
                }
            }
        }

        Y.noalias() = Wm * col;
    }

    Y.colwise() += Eigen::Map<const Eigen::VectorXf> (b, Cout);

    if (Gelu)
        gelu_inplace (Y.data(), Y.size());

    float *out = y.data();

    if (outH == colH && outW == colW)
    {
        std::memcpy (out, Y.data(), sizeof (float) * Y.size());
        return;
    }

    // Upstream maps GEMM row r = h * outW + w to output (h, w) when its output size formula disagrees
    // with its im2col size formula (dilated convolutions). Reproduce that exactly.
    std::memset (out, 0, sizeof (float) * y.size());

    for (int wo = 0; wo < outW; ++wo)
    {
        for (int ho = 0; ho < outH; ++ho)
        {
            const Eigen::Index r = (Eigen::Index)ho * outW + wo;

            if (r >= P)
                continue;

            const int hc = (int)(r / colW), wc = (int)(r % colW);
            std::memcpy (out + (Eigen::Index)Cout * (ho + (Eigen::Index)outH * wo),
                         Y.data() + (Eigen::Index)Cout * (hc + (Eigen::Index)colH * wc), sizeof (float) * Cout);
        }
    }
}

// Transposed convolution of x (Cin, H, W) with weights w laid out (Cin, Cout, KH, KW), column-major.
template <int Cin, int Cout, int KH, int KW, int SH, int SW, int PH, int PW, int DH, int DW, bool Gelu>
Eigen::Tensor3dXf conv2d_tr_impl(const float *x, int H, int W, const float *w, const float *b)
{
    const int effKH = KH + (KH - 1) * (DH - 1), effKW = KW + (KW - 1) * (DW - 1);
    const int expH = (H - 1) * SH + effKH, expW = (W - 1) * SW + effKW;
    const int outH = expH - 2 * PH, outW = expW - 2 * PW;

    // Y (Cout*KH*KW x H*W) = W2^T * X, with W2 = the (Cin x Cout*KH*KW) view of the weights.
    ConstMap W2 (w, Cin, (Eigen::Index)Cout * KH * KW);
    ConstMap X (x, Cin, (Eigen::Index)H * W);
    Eigen::MatrixXf Y (Cout * KH * KW, (Eigen::Index)H * W);
    Y.noalias() = W2.transpose() * X;

    // Scatter-add into the expanded output (row-major positions, as upstream).
    // Upstream reads GEMM row h * outW + w for output (h, w); with no padding that's the expanded grid.
    Eigen::Tensor3dXf y_out (Cout, outH, outW);
    float *out = y_out.data();

    if (PH == 0 && PW == 0)
    {
        std::memset (out, 0, sizeof (float) * y_out.size());

        for (int wi = 0; wi < W; ++wi)
        {
            for (int hi = 0; hi < H; ++hi)
            {
                const float *src = Y.data() + (Eigen::Index)Y.rows() * (hi + (Eigen::Index)H * wi);

                for (int kw = 0; kw < KW; ++kw)
                {
                    const int wo = wi * SW + kw * DW;

                    for (int kh = 0; kh < KH; ++kh)
                    {
                        const int ho = hi * SH + kh * DH;
                        float *dst = out + (Eigen::Index)Cout * (ho + (Eigen::Index)outH * wo);
                        const float *s = src + Cout * (kh + KH * kw);

                        for (int c = 0; c < Cout; ++c)
                            dst[c] += s[c];
                    }
                }
            }
        }

        Eigen::Map<Eigen::MatrixXf> Ym (out, Cout, (Eigen::Index)outH * outW);
        Ym.colwise() += Eigen::Map<const Eigen::VectorXf> (b, Cout);

        if (Gelu)
            gelu_inplace (out, y_out.size());

        return y_out;
    }

    // Padded transposed convolution (unused by Demucs): reproduce upstream's indexing literally.
    Eigen::MatrixXf expanded = Eigen::MatrixXf::Zero (Cout, (Eigen::Index)expH * expW);

    for (int wi = 0; wi < W; ++wi)
        for (int hi = 0; hi < H; ++hi)
            for (int kw = 0; kw < KW; ++kw)
                for (int kh = 0; kh < KH; ++kh)
                {
                    const int eh = hi * SH + kh * DH - PH, ew = wi * SW + kw * DW - PW;

                    if (eh < 0 || eh >= expH || ew < 0 || ew >= expW)
                        continue;

                    const Eigen::Index row = (Eigen::Index)eh * expW + ew;
                    expanded.col (row) += Y.block (Cout * (kh + KH * kw), (Eigen::Index)hi + (Eigen::Index)H * wi, Cout, 1);
                }

    for (int wo = 0; wo < outW; ++wo)
        for (int ho = 0; ho < outH; ++ho)
            for (int c = 0; c < Cout; ++c)
            {
                const float v = expanded (c, (Eigen::Index)ho * outW + wo) + b[c];
                y_out (c, ho, wo) = Gelu ? gelu (v) : v;
            }

    return y_out;
}

// (d0, d1, d2) -> (d1, d2, d0) is a pure reshape when d0 == 1 (column-major layout unchanged).
inline Eigen::Tensor3dXf to_cin_h_w(const Eigen::Tensor3dXf &x)
{
    if (x.dimension(0) == 1)
        return x.reshape (Eigen::array<Eigen::Index, 3>{ x.dimension(1), x.dimension(2), 1 });

    return x.shuffle (Eigen::array<int, 3>({ 1, 2, 0 }));
}

// (d0, d1, 1) -> (1, d0, d1)
inline Eigen::Tensor3dXf from_cout_h_w(const Eigen::Tensor3dXf &y)
{
    if (y.dimension(2) == 1)
        return y.reshape (Eigen::array<Eigen::Index, 3>{ 1, y.dimension(0), y.dimension(1) });

    return y.shuffle (Eigen::array<int, 3>({ 2, 0, 1 }));
}
} // namespace fastconv

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d(const Eigen::Tensor3dXf &x, const Eigen::Tensor4dXf &w,
                         const Eigen::Tensor1dXf &b)
{
    const int H = (int)x.dimension(1), W = (int)x.dimension(2);
    Eigen::Tensor3dXf y (out_channels, fastconv::out_size (H, pad_height, kernel_height, stride_height),
                         fastconv::out_size (W, pad_width, kernel_width, stride_width));
    fastconv::conv2d_into<in_channels, out_channels, kernel_height, kernel_width, stride_height, stride_width,
                          pad_height, pad_width, dilation_height, dilation_width, false> (x.data(), H, W, w.data(), b.data(), y);
    return y;
}

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d_fused_gelu(const Eigen::Tensor3dXf &x,
                                    const Eigen::Tensor4dXf &w,
                                    const Eigen::Tensor1dXf &b)
{
    const int H = (int)x.dimension(1), W = (int)x.dimension(2);
    Eigen::Tensor3dXf y (out_channels, fastconv::out_size (H, pad_height, kernel_height, stride_height),
                         fastconv::out_size (W, pad_width, kernel_width, stride_width));
    fastconv::conv2d_into<in_channels, out_channels, kernel_height, kernel_width, stride_height, stride_width,
                          pad_height, pad_width, dilation_height, dilation_width, true> (x.data(), H, W, w.data(), b.data(), y);
    return y;
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d(const Eigen::Tensor3dXf &x, const Eigen::Tensor3dXf &w,
                         const Eigen::Tensor1dXf &b)
{
    // w (Cout, Cin, K) has the same memory layout as (Cout, Cin, K, 1)
    const Eigen::Tensor3dXf xs = fastconv::to_cin_h_w (x);
    const int H = (int)xs.dimension(1), W = (int)xs.dimension(2);
    Eigen::Tensor3dXf y (out_channels, fastconv::out_size (H, pad, kernel_size, stride), fastconv::out_size (W, 0, 1, 1));
    fastconv::conv2d_into<in_channels, out_channels, kernel_size, 1, stride, 1, pad, 0, dilation, 1, false> (
        xs.data(), H, W, w.data(), b.data(), y);
    return fastconv::from_cout_h_w (y);
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d_fused_gelu(const Eigen::Tensor3dXf &x,
                                    const Eigen::Tensor3dXf &w,
                                    const Eigen::Tensor1dXf &b)
{
    const Eigen::Tensor3dXf xs = fastconv::to_cin_h_w (x);
    const int H = (int)xs.dimension(1), W = (int)xs.dimension(2);
    Eigen::Tensor3dXf y (out_channels, fastconv::out_size (H, pad, kernel_size, stride), fastconv::out_size (W, 0, 1, 1));
    fastconv::conv2d_into<in_channels, out_channels, kernel_size, 1, stride, 1, pad, 0, dilation, 1, true> (
        xs.data(), H, W, w.data(), b.data(), y);
    return fastconv::from_cout_h_w (y);
}

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d_tr(const Eigen::Tensor3dXf &x,
                            const Eigen::Tensor4dXf &w,
                            const Eigen::Tensor1dXf &b)
{
    return fastconv::conv2d_tr_impl<in_channels, out_channels, kernel_height, kernel_width, stride_height, stride_width,
                                    pad_height, pad_width, dilation_height, dilation_width, false> (
        x.data(), (int)x.dimension(1), (int)x.dimension(2), w.data(), b.data());
}

template <int in_channels, int out_channels, int kernel_height,
          int kernel_width, int stride_height, int stride_width, int pad_height,
          int pad_width, int dilation_height, int dilation_width>
Eigen::Tensor3dXf conv2d_tr_fused_gelu(const Eigen::Tensor3dXf &x,
                                       const Eigen::Tensor4dXf &w,
                                       const Eigen::Tensor1dXf &b)
{
    return fastconv::conv2d_tr_impl<in_channels, out_channels, kernel_height, kernel_width, stride_height, stride_width,
                                    pad_height, pad_width, dilation_height, dilation_width, true> (
        x.data(), (int)x.dimension(1), (int)x.dimension(2), w.data(), b.data());
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d_tr(const Eigen::Tensor3dXf &x,
                            const Eigen::Tensor3dXf &w,
                            const Eigen::Tensor1dXf &b)
{
    const Eigen::Tensor3dXf xs = fastconv::to_cin_h_w (x);
    return fastconv::from_cout_h_w (
        fastconv::conv2d_tr_impl<in_channels, out_channels, kernel_size, 1, stride, 1, pad, 0, dilation, 1, false> (
            xs.data(), (int)xs.dimension(1), (int)xs.dimension(2), w.data(), b.data()));
}

template <int in_channels, int out_channels, int kernel_size, int stride,
          int pad, int dilation>
Eigen::Tensor3dXf conv1d_tr_fused_gelu(const Eigen::Tensor3dXf &x,
                                       const Eigen::Tensor3dXf &w,
                                       const Eigen::Tensor1dXf &b)
{
    const Eigen::Tensor3dXf xs = fastconv::to_cin_h_w (x);
    return fastconv::from_cout_h_w (
        fastconv::conv2d_tr_impl<in_channels, out_channels, kernel_size, 1, stride, 1, pad, 0, dilation, 1, true> (
            xs.data(), (int)xs.dimension(1), (int)xs.dimension(2), w.data(), b.data()));
}

} // namespace demucscpp

#endif // CONV_HPP
