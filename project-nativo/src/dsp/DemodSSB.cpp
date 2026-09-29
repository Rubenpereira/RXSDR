#include "DemodSSB.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace masdr {

void DemodSSB::process(const std::complex<float>* iq, size_t n, uint32_t sampleRate)
{
    if (!iq || n == 0 || !onAudio_) return;

    // Pre-decimação para reduzir taxas de amostragem altas para ~250 kHz
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

    // Inicializa e atualiza BPF de voz, Weaver LPF e pre-filtro caso a taxa de amostragem ou a largura de banda mude
    const float wantFs = static_cast<float>(rate);
    const float audioFs = static_cast<float>(actualAudioRate);
    if (lpfFs_ != wantFs || audioFs_ != audioFs || lastBwHz_ != bwHz_) {
        float fLow = 250.0f;
        float fHigh = std::max(500.0f, static_cast<float>(bwHz_));

        float fc = (fHigh - fLow) / 2.0f;

        lpf_.setCutoff(fHigh, wantFs);
        lpf_.reset();

        // Garante estabilidade numérica do filtro Weaver de 8ª ordem (Butterworth)
        // limitando seu corte a um mínimo seguro de 900 Hz. Taxas fc/fs extremamente baixas
        // causam instabilidade no IIR de 8ª ordem em float de precisão simples (nan).
        // O pós-filtro de áudio audioLpf_ (2 polos) fará a atenuação precisa do BW menor.
        const float weaverFc = std::max(900.0f, fc);
        weaverLpf_.setCutoff(weaverFc, audioFs);
        weaverLpf_.reset();

        // Configura o pós-filtro com folga de 20% acima de fHigh para evitar atenuação dupla
        // e dar mais brilho e presença à voz em SSB (USB/LSB).
        const float audioLpfFc = std::min(6000.0f, fHigh * 1.20f);
        audioLpf_.setCutoff(audioLpfFc, audioFs);
        audioLpf_.reset();

        audioHpf_.setCutoff(fLow, audioFs);
        audioHpf_.reset();

        lpfFs_ = wantFs;
        audioFs_ = audioFs;
        lastBwHz_ = bwHz_;
    }

    const bool lsb = (mode_ == "USB");

    std::vector<int16_t> pcm;
    pcm.reserve(pN / step + 1);

    // Oscilador de Weaver centrado no meio da faixa de voz calculada dinamicamente
    float fLow = 250.0f;
    float fHigh = std::max(500.0f, static_cast<float>(bwHz_));
    float f0 = (fLow + fHigh) / 2.0f;
    const float phaseStep = 2.0f * static_cast<float>(M_PI) * f0 / audioFs;

    for (size_t i = 0; i + step <= pN; i += step) {
        std::complex<float> sum = 0.0f;
        for (size_t j = 0; j < step; ++j) {
            sum += lpf_.process(pIq[i + j]);
        }
        std::complex<float> s = sum / static_cast<float>(step);

        // Atualiza a fase do oscilador local complexo de 1650 Hz
        phase_ += phaseStep;
        if (phase_ > 2.0f * static_cast<float>(M_PI)) {
            phase_ -= 2.0f * static_cast<float>(M_PI);
        }

        const float cosPhase = std::cos(phase_);
        const float sinPhase = std::sin(phase_);

        // 1. Mistura Weaver (primeiro produto complexo)
        // Invertido para alinhar com a orientacao de espectro do receptor SDR
        const float mixSin = lsb ? -sinPhase : sinPhase;
        const std::complex<float> mixed = s * std::complex<float>(cosPhase, mixSin);

        // 2. Filtragem pelo LPF Weaver Butterworth de 8a ordem (corte acentuado em 1.4 kHz)
        const std::complex<float> filtered = weaverLpf_.process(mixed);

        // 3. Segunda mistura Weaver (traz de volta a +1650 Hz ou -1650 Hz) e extrai a parte real
        // LSB: desloca para cima (+1650 Hz) -> Re{ filtered * e^(j * phase_) } = I*cos - Q*sin
        // USB: desloca para baixo (-1650 Hz) -> Re{ filtered * e^(-j * phase_) } = I*cos + Q*sin
        float a = 0.0f;
        if (lsb) {
            a = filtered.real() * cosPhase - filtered.imag() * sinPhase;
        } else {
            a = filtered.real() * cosPhase + filtered.imag() * sinPhase;
        }

        // Proteção de auto-recuperação: se o filtro Weaver explodir devido a transiente de sintonia
        // e gerar nan/inf, limpa o estado de todos os filtros e do AGC para retomar no bloco seguinte.
        if (std::isnan(a) || std::isinf(a)) {
            weaverLpf_.reset();
            lpf_.reset();
            audioLpf_.reset();
            audioHpf_.reset();
            agc_ = 0.05f;
            phase_ = 0.0f;
            return;
        }

        // AGC com ataque rapido / liberacao lenta:
        //   Ataque  ~7 ms : alpha 0.003 -> responde a picos de vogal
        //   Liberacao ~1 s: alpha 0.00002 -> nao "bomba" entre silabas
        const float absA = std::abs(a);
        if (absA > agc_) {
            agc_ += 0.003f   * (absA - agc_);
        } else {
            agc_ += 0.00002f * (absA - agc_);
        }
        a *= 0.55f / std::max(0.01f, agc_);

        // BPF de voz: LPF 3.0 kHz remove chiado HF; HPF 250 Hz remove rumble e DC.
        a = audioHpf_.process(audioLpf_.process(a));

        // Limitador de compressão suave (soft clipping) para evitar estalos harmônicos nos picos
        a = std::tanh(a * 1.10f);
        a = std::clamp(a, -1.0f, 1.0f);
        pcm.push_back(static_cast<int16_t>(a * 30000.0f));
    }

    if (!pcm.empty()) onAudio_(pcm, actualAudioRate);
}

} // namespace masdr
