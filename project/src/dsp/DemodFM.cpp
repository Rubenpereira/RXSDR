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

    // --- Pré-filtro IIR LPF (RF) e Pós-filtros de Áudio ------------------
    // A frequência de corte do pré-filtro de RF é metade da largura de banda (BW) selecionada na UI.
    // Isso permite ao usuário estreitar o filtro dinamicamente para limpar ruídos adjacentes em NFM.
    float wantFc = 6250.0f;
    if (isDigital_) {
        // Para sinal digital (DMR), abrimos a frequência de corte do pré-filtro para 12.5 kHz
        // para minimizar a distorção de fase (group delay) dentro da banda do sinal 4FSK.
        wantFc = 12500.0f;
    } else if (isWfm) {
        wantFc = (bwHz_ > 0) ? static_cast<float>(bwHz_) / 2.0f : 100000.0f;
        wantFc = std::clamp(wantFc, 50000.0f, 150000.0f); // limites de segurança para WFM
    } else {
        wantFc = (bwHz_ > 0) ? static_cast<float>(bwHz_) / 2.0f : 6250.0f;
        wantFc = std::clamp(wantFc, 3000.0f, 12500.0f);   // limites de segurança para NFM
    }

    const float audioFs = static_cast<float>(actualAudioRate);
    if (lpfFc_ != wantFc || lpfFs_ != rate || audioFs_ != audioFs) {
        lpf_.setCutoff(wantFc, static_cast<float>(rate));
        lpf_.reset();
        
        // Pós-filtros de áudio: NFM corta em 3.0 kHz para eliminar chuvisco restante nas pausas de fala
        const float audioLpfFc = isWfm ? 15000.0f : 3000.0f;
        const float audioHpfFc = isWfm ? 30.0f : 100.0f;
        audioLpf_.setCutoff(audioLpfFc, audioFs);
        audioLpf_.reset();
        audioHpf_.setCutoff(audioHpfFc, audioFs);
        audioHpf_.reset();

        lpfFc_ = wantFc;
        lpfFs_ = rate;
        audioFs_ = audioFs;
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

        // Proteção de auto-recuperação contra nan/inf causados por transientes de sintonia ou travamento
        if (std::isnan(a) || std::isinf(a)) {
            lpf_.reset();
            audioLpf_.reset();
            audioHpf_.reset();
            deEmph_ = 0.0f;
            return;
        }

        // Em modo digital (DMR/DSD), ignoramos todos os pós-filtros de áudio, de-ênfase e compressão
        // para obter o sinal discriminador bruto (linear e de banda larga) para o decodificador
        if (isDigital_) {
            a = std::clamp(a, -1.0f, 1.0f) * 30000.0f;
        } else {
            // De-enfase para WFM e NFM (remove chiado de alta frequência, melhorando drasticamente clareza e fidelidade)
            deEmph_ += deEmphAlpha * (a - deEmph_);
            a = deEmph_;

            // Aplica pós-filtro de áudio (LPF remove chuvisco HF; HPF remove subtons CTCSS/rumble)
            a = audioHpf_.process(audioLpf_.process(a));

            // Compressão muito mais suave para manter a linearidade da voz sem espremer o áudio
            a = std::tanh(a * 1.05f) * 30000.0f;
        }
        pcm.push_back(static_cast<int16_t>(a));
    }

    if (!pcm.empty()) onAudio_(pcm, actualAudioRate);
}

} // namespace masdr
