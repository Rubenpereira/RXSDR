#pragma once
#include "Demodulator.h"
#include "Filters.h"

namespace masdr {

class DemodAM : public Demodulator {
public:
    void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) override;

private:
    IirLpf     lpf_;             // pre-filtro IQ: 6 kHz
    float      lpfFs_  = 0.0f;
    float      envDc_  = 0.0f;   // remove DC da portadora
    float      agc_    = 0.05f;  // AGC com ataque/liberacao assimetricos
    IirLpfMono audioLpf_;         // pos-filtro audio: LPF 5 kHz
    IirHpf     audioHpf_;         // pos-filtro audio: HPF 80 Hz
    float      audioFs_ = 0.0f;
};

} // namespace masdr
