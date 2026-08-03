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
};

} // namespace masdr
