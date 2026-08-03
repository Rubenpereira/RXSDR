#include "FftProcessor.h"

#include <algorithm>
#include <cmath>
#include <complex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

// ---------------------------------------------------------------------------
// Cooley-Tukey radix-2 FFT — in-place, tamanho deve ser potência de 2
// ---------------------------------------------------------------------------
static void fft_inplace(std::vector<std::complex<float>>& x)
{
    const size_t n = x.size();
    if (n <= 1) return;

    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Butterfly stages
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = x[i + j];
                const auto v = x[i + j + len / 2] * w;
                x[i + j]           = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Maior potência de 2 <= n
static size_t prevPow2(size_t n)
{
    size_t p = 1;
    while (p * 2 <= n) p <<= 1;
    return p;
}

FftProcessor::FftProcessor(size_t bins)
{
    bins_ = prevPow2(std::max<size_t>(64, bins));
    // Pré-calcular janela Hann para o tamanho fixo bins_
    hann_.resize(bins_);
    for (size_t i = 0; i < bins_; ++i)
        hann_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI)
                                            * static_cast<float>(i)
                                            / static_cast<float>(bins_ - 1)));
}

std::vector<int8_t> FftProcessor::computeDbfs(const std::complex<float>* iq, size_t n)
{
    std::vector<int8_t> out(bins_, -120);
    if (!iq || n == 0) return out;

    if (history_.size() != bins_) {
        history_.assign(bins_, {0.0f, 0.0f});
    }

    if (n >= bins_) {
        std::copy(iq + n - bins_, iq + n, history_.begin());
    } else {
        std::move(history_.begin() + n, history_.end(), history_.begin());
        std::copy(iq, iq + n, history_.end() - n);
    }

    std::vector<std::complex<float>> buf(bins_);
    for (size_t i = 0; i < bins_; ++i) {
        buf[i] = history_[i] * hann_[i];
    }

    fft_inplace(buf);

    const float norm = static_cast<float>(bins_) * static_cast<float>(bins_) * 0.375f;

    for (size_t b = 0; b < bins_; ++b) {
        const size_t shifted = (b + bins_ / 2) % bins_;
        const float re = buf[shifted].real();
        const float im = buf[shifted].imag();
        const float p  = re * re + im * im;
        const float dbfs = 10.0f * std::log10(p / norm + 1e-12f);
        const int   q    = static_cast<int>(std::clamp(dbfs, -120.0f, 0.0f));
        out[b] = static_cast<int8_t>(q);
    }

    return out;
}

// ---------------------------------------------------------------------------
// Média de espectros (equivalente ao LogAveragePower do OpenWebRX+)
// ---------------------------------------------------------------------------
void FftProcessor::addPower(const std::vector<std::complex<float>>& spec)
{
    const double norm = static_cast<double>(bins_) * static_cast<double>(bins_) * 0.375;
    for (size_t b = 0; b < bins_; ++b) {
        const size_t shifted = (b + bins_ / 2) % bins_;
        const double re = spec[shifted].real();
        const double im = spec[shifted].imag();
        accum_[b] += (re * re + im * im) / norm;
    }
    ++accumCount_;
}

void FftProcessor::accumulate(const std::complex<float>* iq, size_t n)
{
    if (!iq || n == 0) return;

    if (accum_.size() != bins_) {
        accum_.assign(bins_, 0.0);
        accumCount_ = 0;
    }
    // Teto de CPU: passou do limite da janela, ignora o resto dos blocos
    if (accumCount_ >= maxAvgPerWindow_) return;

    if (history_.size() != bins_) history_.assign(bins_, {0.0f, 0.0f});

    std::vector<std::complex<float>> buf(bins_);
    const size_t step = bins_ / 2;   // 50% de sobreposição (como fft_voverlap_factor)
    size_t pos  = 0;
    int    segs = 0;

    while (pos + bins_ <= n
           && segs < maxSegmentsPerCall_
           && accumCount_ < maxAvgPerWindow_) {
        for (size_t i = 0; i < bins_; ++i) buf[i] = iq[pos + i] * hann_[i];
        fft_inplace(buf);
        addPower(buf);
        ++segs;
        pos += step;
    }

    // Bloco menor que bins_: cai no histórico deslizante, um segmento só
    if (segs == 0) {
        if (n >= bins_) {
            std::copy(iq + n - bins_, iq + n, history_.begin());
        } else {
            std::move(history_.begin() + n, history_.end(), history_.begin());
            std::copy(iq, iq + n, history_.end() - n);
        }
        for (size_t i = 0; i < bins_; ++i) buf[i] = history_[i] * hann_[i];
        fft_inplace(buf);
        addPower(buf);
    }
}

std::vector<int8_t> FftProcessor::takeAverageDbfs()
{
    std::vector<int8_t> out(bins_, -120);
    if (accumCount_ <= 0 || accum_.size() != bins_) return out;

    const double inv = 1.0 / static_cast<double>(accumCount_);
    for (size_t b = 0; b < bins_; ++b) {
        const double dbfs = 10.0 * std::log10(accum_[b] * inv + 1e-12);
        const long   q    = std::lround(std::clamp(dbfs, -120.0, 0.0));
        out[b] = static_cast<int8_t>(q);
    }

    std::fill(accum_.begin(), accum_.end(), 0.0);
    accumCount_ = 0;
    return out;
}

} // namespace masdr
