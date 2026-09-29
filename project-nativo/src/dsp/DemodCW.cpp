#include "DemodCW.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace masdr {

void DemodCW::process(const std::complex<float>* iq, size_t n, uint32_t sampleRate)
{
    if (!iq || n == 0 || !onAudio_) return;

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
    const uint32_t audioRate = rate / step;
    if (audioRate == 0) return;

    // ---- filtros, refeitos so quando algo muda ----------------------------
    const float fsPre   = static_cast<float>(rate);
    const float fsAudio = static_cast<float>(audioRate);

    // Largura util: metade da banda escolhida de cada lado da portadora. Um
    // filtro de 500 Hz corresponde a +-250 Hz em banda base.
    const uint32_t bw = (bwHz_ >= 100 && bwHz_ <= 4000) ? bwHz_ : 500;

    if (preFs_ != fsPre || audioFs_ != fsAudio || ultimaBw_ != bw) {
        pre_.setCutoff(std::max(80.0f, float(bw) * 0.5f), fsPre);
        pre_.reset();

        // O tom fica no MEIO da banda de audio util. Deixamos passar do tom
        // menos meia banda ate o tom mais meia banda, com folga - assim o
        // filtro nao corta a propria batida que queremos ouvir. Este foi o
        // detalhe que me fez NAO reaproveitar o DemodSSB direto: la o
        // pos-filtro e calculado para voz e cortaria em 600 Hz num filtro
        // de 500, justamente em cima do tom de 700.
        const float alto  = std::min(fsAudio * 0.45f, tomHz_ + float(bw) * 0.6f + 100.0f);
        const float baixo = std::max(80.0f, tomHz_ - float(bw) * 0.6f - 100.0f);
        audioLpf_.setCutoff(alto, fsAudio);
        audioLpf_.reset();
        audioHpf_.setCutoff(baixo, fsAudio);
        audioHpf_.reset();

        preFs_ = fsPre; audioFs_ = fsAudio; ultimaBw_ = bw;
    }

    // Passo do oscilador que desloca a banda base para o tom escolhido.
    const float w = 6.283185307f * tomHz_ / fsAudio;
    const float passoCos = std::cos(w);
    const float passoSen = std::sin(w);

    std::vector<int16_t> pcm;
    pcm.reserve(pN / step + 1);

    size_t conta = 0;
    for (size_t i = 0; i + step <= pN; i += step, ++conta) {
        // 1) Seleciona a estacao em banda base, ainda no complexo. Aqui e que
        //    a seletividade acontece: tudo que nao for a estacao sintonizada
        //    fica de fora, inclusive a vizinha a 300 Hz.
        const std::complex<float> z = pre_.process(pIq[i]);

        // 2) Desloca para o tom. Como o sinal ja esta limpo em torno de zero,
        //    a parte real depois do deslocamento e a batida - sem imagem, sem
        //    tom sintetico.
        const float ni = oscI_ * passoCos - oscQ_ * passoSen;
        const float nq = oscI_ * passoSen + oscQ_ * passoCos;
        oscI_ = ni; oscQ_ = nq;
        if ((conta & 1023) == 0) {
            const float m = std::sqrt(oscI_ * oscI_ + oscQ_ * oscQ_);
            if (m > 1e-9f) { oscI_ /= m; oscQ_ /= m; }
        }
        float a = z.real() * oscI_ - z.imag() * oscQ_;

        // 3) Limpa o que sobrou fora da banda util.
        a = audioHpf_.process(audioLpf_.process(a));

        // 4) AGC com ataque rapido e retorno lento, igual ao do SSB: CW tem
        //    silencio entre os elementos, e um AGC simetrico levantaria o
        //    ruido nesses vaos ate ficar tao alto quanto o sinal.
        const float mag = std::fabs(a);
        if (mag > agc_) agc_ += (mag - agc_) * 0.05f;
        else            agc_ += (mag - agc_) * 0.0008f;
        if (agc_ < 1e-4f) agc_ = 1e-4f;

        const float saida = std::clamp(a / (agc_ * 3.0f), -1.0f, 1.0f);
        pcm.push_back(static_cast<int16_t>(saida * 22000.0f));
    }

    if (!pcm.empty()) onAudio_(pcm, audioRate);
}

} // namespace masdr
