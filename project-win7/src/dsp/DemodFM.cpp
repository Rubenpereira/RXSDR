#include "DemodFM.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

void DemodFM::process(const std::complex<float>* iq, size_t n, uint32_t sampleRate)
{
    if (!iq || n == 0 || !onAudio_) return;

    const bool isWfm = (mode_ == "WFM");

    // Pre-decimação para reduzir taxas de amostragem altas para ~250 kHz (NFM) ou ~500 kHz (WFM)
    uint32_t factor = 1;
    if (sampleRate >= 2000000) factor = isWfm ? 4 : 8;
    else if (sampleRate >= 1000000) factor = isWfm ? 2 : 4;
    else if (sampleRate >= 500000) factor = isWfm ? 1 : 2;

    const size_t step = audioDecimStep(sampleRate / factor);

    std::vector<std::complex<float>> decIQ;
    const uint32_t rate = processInput(iq, n, sampleRate, factor, step, decIQ);
    if (decIQ.empty()) return;

    const std::complex<float>* pIq = decIQ.data();
    const size_t pN = decIQ.size();

    const uint32_t actualAudioRate = rate / step;

    // --- Pre-filtro IIR LPF (2 polos) ----------------------------------
    // Limita a largura de banda antes de discriminar.
    // NFM: 12.5 kHz  → ruido cai ~44 dB vs sem filtro (BW 2 MHz → 12.5 kHz)
    // WFM: 100 kHz   → ruido cai ~26 dB vs sem filtro
    // Reconfigura somente se fs ou modo mudou (evita reset desnecessario).
    const float wantFc = isWfm ? 100000.0f : 12500.0f;
    if (lpfFc_ != wantFc || lpfFs_ != rate) {
        lpf_.setCutoff(wantFc, static_cast<float>(rate));
        lpf_.reset();
        lpfFc_ = wantFc;
        lpfFs_ = rate;
    }

    // Ganho do discriminador FM: normaliza atan2 (rad, diff de 1 amostra)
    // para amplitude unitaria no desvio maximo.
    const float maxDev = isWfm ? 75000.0f : 5000.0f;
    const float gainFm = static_cast<float>(rate)
                         / (2.0f * static_cast<float>(M_PI) * maxDev);

    // De-enfase WFM e NFM 75 us: fc = 1/(2*pi*75e-6) = 2122 Hz (essencial para eliminar ruídos de alta frequência)
    const float deEmphAlpha = 1.0f / (1.0f + static_cast<float>(actualAudioRate)
                                             / (2.0f * static_cast<float>(M_PI) * 2122.0f));

    std::vector<int16_t> pcm;
    pcm.reserve(pN / step + 1);

    for (size_t i = 0; i + step <= pN; i += step) {
        float phSum = 0.0f;
        for (size_t j = 0; j < step; ++j) {
            // Aplica pre-filtro IIR antes de discriminar
            const std::complex<float> s = lpf_.process(pIq[i + j]);
            const std::complex<float> c = s * std::conj(prev_);
            prev_ = s;
            phSum += std::atan2(c.imag(), c.real());
        }
        float a = (phSum / static_cast<float>(step)) * gainFm;

        // De-enfase para WFM e NFM (remove chiado de alta frequência, melhorando drasticamente clareza e fidelidade)
        deEmph_ += deEmphAlpha * (a - deEmph_);
        a = deEmph_;

        // Compressão muito mais suave para manter a linearidade da voz sem espremer o áudio
        a = std::tanh(a * 1.05f);
        pcm.push_back(static_cast<int16_t>(std::clamp(a, -1.0f, 1.0f) * 30000.0f));
    }

    if (!pcm.empty()) onAudio_(pcm, actualAudioRate);
}

} // namespace masdr
