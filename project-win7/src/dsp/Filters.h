#pragma once
#include <vector>
#include <complex>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

class Filters {
public:
    static void dcBlock(std::vector<std::complex<float>>& iq);
};

// IirDcBlock: Filtro passa-alta IIR de 1a ordem para remover componente DC de sinais complexos (IQ) sem descontinuidade entre blocos.
struct IirDcBlock {
    float prevRealX = 0.0f, prevRealY = 0.0f;
    float prevImagX = 0.0f, prevImagY = 0.0f;
    float alpha = 0.999f;

    inline std::complex<float> process(std::complex<float> x) noexcept {
        float ry = alpha * (prevRealY + x.real() - prevRealX);
        prevRealX = x.real();
        prevRealY = ry;

        float iy = alpha * (prevImagY + x.imag() - prevImagX);
        prevImagX = x.imag();
        prevImagY = iy;

        return { ry, iy };
    }

    void reset() {
        prevRealX = prevRealY = 0.0f;
        prevImagX = prevImagY = 0.0f;
    }
};

// IirLpf: filtro passa-baixa IIR de 2a ordem (dois polos em cascata)
// aplicado em I e Q simultaneamente (mesma constante de tempo).
// -40 dB/dec apos fc.
// Uso: setCutoff(fc, fs) uma vez; process() por amostra.
struct IirLpf {
    float l1r = 0, l1i = 0;   // 1o polo - canal real e imag
    float l2r = 0, l2i = 0;   // 2o polo
    float alpha = 0.1f;

    void setCutoff(float fc, float fs) {
        alpha = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * fc / fs);
    }

    inline std::complex<float> process(std::complex<float> x) noexcept {
        l1r += alpha * (x.real() - l1r);
        l1i += alpha * (x.imag() - l1i);
        l2r += alpha * (l1r - l2r);
        l2i += alpha * (l1i - l2i);
        return { l2r, l2i };
    }

    void reset() { l1r = l1i = l2r = l2i = 0.0f; }
};

// IirLpfMono: filtro passa-baixa IIR de 2a ordem para audio mono (float).
// Mesmo algoritmo do IirLpf mas para um unico canal (pos-demodulacao de audio).
struct IirLpfMono {
    float l1 = 0, l2 = 0;
    float alpha = 0.1f;

    void setCutoff(float fc, float fs) {
        alpha = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * fc / fs);
    }

    inline float process(float x) noexcept {
        l1 += alpha * (x - l1);
        l2 += alpha * (l1 - l2);
        return l2;
    }

    void reset() { l1 = l2 = 0.0f; }
};

// IirHpf: filtro passa-alta de 1a ordem.
// Remove DC, ruido infra-audio e ronco de portadora residual.
// Uso: setCutoff(fc, fs) uma vez; process() por amostra de audio.
struct IirHpf {
    float prevX = 0, prevY = 0;
    float alpha = 0.995f;

    void setCutoff(float fc, float fs) {
        const float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * fc);
        const float dt = 1.0f / fs;
        alpha = rc / (rc + dt);
    }

    inline float process(float x) noexcept {
        const float y = alpha * (prevY + x - prevX);
        prevX = x;
        prevY = y;
        return y;
    }

    void reset() { prevX = prevY = 0.0f; }
};

} // namespace masdr
