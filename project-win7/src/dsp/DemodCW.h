#pragma once
#include "Demodulator.h"

namespace masdr {

class DemodCW : public Demodulator {
public:
    void setMode(const std::string& mode) override {
        Demodulator::setMode(mode);
        phase_        = 0.0f;
        envelope_     = 0.0f;
        noiseFloor_   = 1.0f;
        gateSmoothed_ = 0.0f;
        gateOpen_     = false;
    }

    void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) override;

private:
    float phase_        = 0.0f;
    float envelope_     = 0.0f;
    float noiseFloor_   = 1.0f;
    float gateSmoothed_ = 0.0f;
    bool  gateOpen_     = false;
};

} // namespace masdr
