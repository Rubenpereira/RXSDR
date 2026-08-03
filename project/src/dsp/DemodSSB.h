#pragma once
#include "Demodulator.h"
#include "Filters.h"

namespace masdr {

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float w1r = 0.0f, w2r = 0.0f;
    float w1i = 0.0f, w2i = 0.0f;

    void setLowPass(float fc, float fs, float Q) {
        float omega = 2.0f * static_cast<float>(M_PI) * fc / fs;
        float cosw = std::cos(omega);
        float sinw = std::sin(omega);
        float alpha = sinw / (2.0f * Q);

        float a0 = 1.0f + alpha;
        b0 = ((1.0f - cosw) / 2.0f) / a0;
        b1 = (1.0f - cosw) / a0;
        b2 = ((1.0f - cosw) / 2.0f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    inline std::complex<float> process(std::complex<float> x) noexcept {
        float yr = b0 * x.real() + w1r;
        w1r = b1 * x.real() - a1 * yr + w2r;
        w2r = b2 * x.real() - a2 * yr;

        float yi = b0 * x.imag() + w1i;
        w1i = b1 * x.imag() - a1 * yi + w2i;
        w2i = b2 * x.imag() - a2 * yi;

        return { yr, yi };
    }

    void reset() {
        w1r = w2r = w1i = w2i = 0.0f;
    }
};

class Butterworth8Lpf {
public:
    void setCutoff(float fc, float fs) {
        bq1_.setLowPass(fc, fs, 0.5098f);
        bq2_.setLowPass(fc, fs, 0.6013f);
        bq3_.setLowPass(fc, fs, 0.8999f);
        bq4_.setLowPass(fc, fs, 2.5629f);
    }

    inline std::complex<float> process(std::complex<float> x) noexcept {
        return bq4_.process(bq3_.process(bq2_.process(bq1_.process(x))));
    }

    void reset() {
        bq1_.reset();
        bq2_.reset();
        bq3_.reset();
        bq4_.reset();
    }

private:
    Biquad bq1_, bq2_, bq3_, bq4_;
};

class DemodSSB : public Demodulator {
public:
    void setMode(const QString& mode) override {
        Demodulator::setMode(mode);
        lpf_.reset();
        weaverLpf_.reset();
        audioLpf_.reset();
        audioHpf_.reset();
        phase_ = 0.0f;
    }

    void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) override;

private:
    IirLpf           lpf_;             // pre-filtro IQ: 3 kHz
    float            lpfFs_  = 0.0f;
    Butterworth8Lpf  weaverLpf_;       // Filtro LPF Weaver Butterworth de 8a ordem
    float            phase_  = 0.0f;   // Fase do oscilador Weaver
    float            agc_    = 0.05f;  // AGC com ataque/liberacao assimetricos
    IirLpfMono       audioLpf_;         // pos-filtro: LPF 2.8 kHz (banda de voz)
    IirHpf           audioHpf_;         // pos-filtro: HPF 250 Hz (elimina chiado sub-voz)
    float            audioFs_ = 0.0f;
    uint32_t         lastBwHz_ = 0;
};

} // namespace masdr
