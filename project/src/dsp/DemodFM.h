#pragma once
#include "Demodulator.h"
#include "Filters.h"

namespace masdr {

class DemodFM : public Demodulator {
public:
    void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) override;

private:
    std::complex<float> prev_{1.0f, 0.0f};
    float     deEmph_ = 0.0f;
    IirLpf    lpf_;           // pre-filtro IQ (NFM: 12.5 kHz, WFM: 100 kHz)
    float     lpfFc_  = 0.0f;
    uint32_t  lpfFs_  = 0;

    IirLpfMono audioLpf_;      // pos-filtro de audio LPF (NFM: 3.4 kHz, WFM: 15 kHz)
    IirHpf     audioHpf_;      // pos-filtro de audio HPF (NFM: 100 Hz, WFM: 30 Hz)
    float      audioFs_ = 0.0f;
};

} // namespace masdr
