#pragma once
#include "Demodulator.h"

namespace masdr {

class DemodCW : public Demodulator {
public:
    void setMode(const QString& mode) override {
        Demodulator::setMode(mode);
        phase_        = 0.0f;
        envelope_     = 0.0f;
        // Inicia ALTO: garante que o gate comece FECHADO e va aprendendo
        // o piso de ruido real por baixo (min-tracker). Sem isso, qualquer
        // amostra com magnitude > 0 abria o gate logo no primeiro sample.
        noiseFloor_   = 1.0f;
        gateSmoothed_ = 0.0f;
        gateOpen_     = false;
    }

    void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) override;

private:
    float phase_        = 0.0f;
    // Squelch / noise gate adaptativo do sidetone 700 Hz
    float envelope_     = 0.0f;  // envolvente rapida da magnitude do IQ
    float noiseFloor_   = 1.0f;  // piso de ruido estimado (init alto -> gate fechado)
    float gateSmoothed_ = 0.0f;  // ganho do gate suavizado (0..1)
    bool  gateOpen_     = false; // estado do gate com histerese
};

} // namespace masdr
