#pragma once
#include "Demodulator.h"
#include "Filters.h"

namespace masdr {

class DemodAM : public Demodulator {
public:
    void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) override;

private:
    IirLpf     lpf_;             // pre-filtro IQ
    float      lpfFs_  = 0.0f;
    float      lpfFc_  = 0.0f;   // frequencia de corte atual do pre-filtro
    float      envDc_  = 0.0f;   // remove DC da portadora
    float      agc_    = 0.05f;  // AGC com ataque/liberacao assimetricos
    IirLpfMono audioLpf_;         // pos-filtro audio
    IirHpf     audioHpf_;         // pos-filtro audio
    float      audioFs_ = 0.0f;
};

} // namespace masdr
