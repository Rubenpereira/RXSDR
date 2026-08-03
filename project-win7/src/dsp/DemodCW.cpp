#include "DemodCW.h"

#include <algorithm>
#include <cmath>

namespace masdr {

void DemodCW::process(const std::complex<float>* iq, size_t n, uint32_t sampleRate)
{
    if (!iq || n == 0 || !onAudio_) return;

    // Pre-decimacao para reduzir taxas de amostragem altas para ~250 kHz
    uint32_t factor = 1;
    if (sampleRate >= 2000000) factor = 8;
    else if (sampleRate >= 1000000) factor = 4;
    else if (sampleRate >= 500000) factor = 2;

    const size_t step = audioDecimStep(sampleRate / factor);

    std::vector<std::complex<float>> decIQ;
    const uint32_t rate = processInput(iq, n, sampleRate, factor, step, decIQ);
    if (decIQ.empty()) return;

    const std::complex<float>* pIq = decIQ.data();
    const size_t pN = decIQ.size();

    const uint32_t actualAudioRate = rate / step;

    std::vector<int16_t> pcm;
    pcm.reserve(pN / step + 1);
    const float beepHz = 700.0f;
    const float w = 6.283185307f * beepHz / static_cast<float>(actualAudioRate);

    // ---------------------------------------------------------------------
    // Squelch suave do sidetone CW.
    // ---------------------------------------------------------------------
    constexpr float kModGain        = 0.7f;      // ganho de modulacao do sidetone
    constexpr float kIdleGain       = 0.0f;      // ganho do gate quando "fechado" (silêncio total no idle)
    constexpr float kEnvAlpha       = 0.25f;     // envolvente rapida
    constexpr float kNoiseUpAlpha   = 0.00005f;  // piso sobe muito devagar
    constexpr float kNoiseDownAlpha = 0.02f;     // piso desce em ~50 ms
    constexpr float kOpenRatio      = 3.5f;      // abre em 3.5x acima do piso
    constexpr float kCloseRatio     = 2.0f;      // fecha em 2x (histerese)
    constexpr float kAbsMinThr      = 0.002f;    // piso absoluto muito mais sensível
    constexpr float kGateAttack     = 0.40f;     // ataque rapido (nao corta dits)
    constexpr float kGateRelease    = 0.05f;     // release suave (cauda natural)

    for (size_t i = 0; i + step <= pN; i += step) {
        const float mag = std::min(1.0f, std::abs(pIq[i]));

        // 1) Envolvente rapida do sinal
        envelope_ += kEnvAlpha * (mag - envelope_);

        // 2) Min-tracker do piso de ruido (sempre ativo)
        if (envelope_ < noiseFloor_) {
            noiseFloor_ += kNoiseDownAlpha * (envelope_ - noiseFloor_);
        } else {
            noiseFloor_ += kNoiseUpAlpha * (envelope_ - noiseFloor_);
        }
        if (noiseFloor_ < 1e-5f) noiseFloor_ = 1e-5f;

        // 3) Limiares com piso absoluto e histerese
        const float openThr  = std::max(noiseFloor_ * kOpenRatio,  kAbsMinThr);
        const float closeThr = std::max(noiseFloor_ * kCloseRatio, kAbsMinThr * 0.6f);
        if (envelope_ >= openThr)       gateOpen_ = true;
        else if (envelope_ <  closeThr) gateOpen_ = false;

        // 4) Suaviza o ganho: target=1.0 aberto, target=kIdleGain (0.0) fechado.
        const float target = gateOpen_ ? 1.0f : kIdleGain;
        const float aSm    = (target > gateSmoothed_) ? kGateAttack
                                                      : kGateRelease;
        gateSmoothed_ += aSm * (target - gateSmoothed_);

        // 5) Gera o tom 700 Hz com ganho de modulacao reduzido e aplica
        //    o ganho do gate.
        phase_ += w;
        if (phase_ > 2.0f * 3.1415926535f) {
            phase_ -= 2.0f * 3.1415926535f;
        }
        const float tone   = std::tanh(std::sin(phase_) * mag * kModGain);
        const float sample = tone * gateSmoothed_;
        pcm.push_back(static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 26000.0f));
    }
    if (!pcm.empty()) onAudio_(pcm, actualAudioRate);
}

} // namespace masdr
