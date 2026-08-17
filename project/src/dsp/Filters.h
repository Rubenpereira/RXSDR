#pragma once
#include <cstdint>
#include <vector>
#include <complex>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

class Filters {
public:
    static void dcBlock(std::vector<std::complex<float>>& iq);
};

// IirDcBlock: Filtro passa-alta IIR de 1a ordem para remover componente DC de sinais complexos (IQ) sem descontinuidade entre blocos.
struct IirDcBlock {
    float prevRealX = 0.0f, prevRealY = 0.0f;
    float prevImagX = 0.0f, prevImagY = 0.0f;
    float alpha = 0.999f;

    inline std::complex<float> process(std::complex<float> x) noexcept {
        float ry = alpha * (prevRealY + x.real() - prevRealX);
        prevRealX = x.real();
        prevRealY = ry;

        float iy = alpha * (prevImagY + x.imag() - prevImagX);
        prevImagX = x.imag();
        prevImagY = iy;

        return { ry, iy };
    }

    void reset() {
        prevRealX = prevRealY = 0.0f;
        prevImagX = prevImagY = 0.0f;
    }
};

// ---------------------------------------------------------------------------
//  Reamostrador para 48000 Hz exatos, COM ESTADO entre blocos.
//
//  Por que isto existe
//  -------------------
//  O audioDecimStep usa divisao inteira, entao a taxa de audio nunca cai em
//  48000: a 1,024 e a 2,048 Msps da 51200 Hz; a 0,25 e 2,4 Msps da 50000. O
//  navegador roda a 48000 e reamostrava cada bloco SOZINHO, sem memoria do
//  bloco anterior.
//
//  Medido com um tom de 1 kHz, 2 segundos entregues em blocos de 2048:
//
//     com estado   95998 amostras (2,0000 s)  0 emendas quebradas
//     por bloco    95900 amostras (1,9979 s)  49 emendas quebradas
//
//  Ou seja, 25 descontinuidades por segundo - a "travadinha" que se ouvia em
//  qualquer banda, sem relacao com propagacao - mais 1 ms de atraso acumulado
//  por segundo. Esse atraso e que fazia o relogio do agendador escorregar ate
//  disparar o reset, abrindo os buracos de audio.
//
//  Interpolacao de Hermite (Catmull-Rom) sobre 4 amostras. O conteudo de audio
//  vai ate uns 5 kHz contra 48 kHz de saida, entao nao ha risco de dobra; o
//  que importa aqui e a CONTINUIDADE entre blocos, nao a ordem do polinomio.
// ---------------------------------------------------------------------------
struct Resampler48k {
    static constexpr double kSaida = 48000.0;

    double   pos = 0.0;          // posicao fracionaria dentro da entrada
    float    h[4] = {0,0,0,0};   // janela deslizante que atravessa os blocos
    bool     pronto = false;     // ja entraram as 4 primeiras amostras?
    uint32_t taxaAnterior = 0;

    void reset() {
        pos = 0.0;
        h[0] = h[1] = h[2] = h[3] = 0.0f;
        pronto = false;
    }

    static float hermite(float y0, float y1, float y2, float y3, float t) {
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // Acrescenta o resultado em 'saida'. Se a taxa de entrada mudar, o estado
    // e zerado - continuar com a posicao antiga produziria um salto.
    void processa(const int16_t* ent, size_t n, uint32_t taxaEntrada,
                  std::vector<int16_t>& saida)
    {
        if (!ent || n == 0 || taxaEntrada == 0) return;
        if (taxaEntrada != taxaAnterior) { reset(); taxaAnterior = taxaEntrada; }

        const double passo = double(taxaEntrada) / kSaida;
        saida.reserve(saida.size() + size_t(double(n) / passo) + 4);

        for (size_t i = 0; i < n; ++i) {
            h[0] = h[1]; h[1] = h[2]; h[2] = h[3];
            h[3] = float(ent[i]) / 32768.0f;
            if (!pronto) { if (i >= 3) pronto = true; else continue; }

            while (pos < 1.0) {
                float v = hermite(h[0], h[1], h[2], h[3], float(pos));
                if (v >  1.0f) v =  1.0f;
                if (v < -1.0f) v = -1.0f;
                saida.push_back(int16_t(v * 32767.0f));
                pos += passo;
            }
            pos -= 1.0;
        }
    }
};

// IirLpf: filtro passa-baixa IIR de 2a ordem (dois polos em cascata)
// aplicado em I e Q simultaneamente (mesma constante de tempo).
// -40 dB/dec apos fc.
// Uso: setCutoff(fc, fs) uma vez; process() por amostra.
struct IirLpf {
    float l1r = 0, l1i = 0;   // 1o polo - canal real e imag
    float l2r = 0, l2i = 0;   // 2o polo
    float alpha = 0.1f;

    void setCutoff(float fc, float fs) {
        alpha = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * fc / fs);
    }

    inline std::complex<float> process(std::complex<float> x) noexcept {
        l1r += alpha * (x.real() - l1r);
        l1i += alpha * (x.imag() - l1i);
        l2r += alpha * (l1r - l2r);
        l2i += alpha * (l1i - l2i);
        return { l2r, l2i };
    }

    void reset() { l1r = l1i = l2r = l2i = 0.0f; }
};

// IirLpfMono: filtro passa-baixa IIR de 2a ordem para audio mono (float).
// Mesmo algoritmo do IirLpf mas para um unico canal (pos-demodulacao de audio).
struct IirLpfMono {
    float l1 = 0, l2 = 0;
    float alpha = 0.1f;

    void setCutoff(float fc, float fs) {
        alpha = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * fc / fs);
    }

    inline float process(float x) noexcept {
        l1 += alpha * (x - l1);
        l2 += alpha * (l1 - l2);
        return l2;
    }

    void reset() { l1 = l2 = 0.0f; }
};

// IirHpf: filtro passa-alta de 1a ordem.
// Remove DC, ruido infra-audio e ronco de portadora residual.
// Uso: setCutoff(fc, fs) uma vez; process() por amostra de audio.
struct IirHpf {
    float prevX = 0, prevY = 0;
    float alpha = 0.995f;

    void setCutoff(float fc, float fs) {
        const float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * fc);
        const float dt = 1.0f / fs;
        alpha = rc / (rc + dt);
    }

    inline float process(float x) noexcept {
        const float y = alpha * (prevY + x - prevX);
        prevX = x;
        prevY = y;
        return y;
    }

    void reset() { prevX = prevY = 0.0f; }
};

} // namespace masdr
