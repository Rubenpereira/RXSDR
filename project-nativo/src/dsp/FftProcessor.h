#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace masdr {

class FftProcessor {
public:
    explicit FftProcessor(size_t bins);

    // Espectro instantâneo (um único FFT) — mantido para compatibilidade.
    std::vector<int8_t> computeDbfs(const std::complex<float>* iq, size_t n);

    // ---- Média de espectros, como o LogAveragePower do OpenWebRX+ ----------
    // accumulate() soma a POTÊNCIA (linear) de todos os segmentos do bloco;
    // takeAverageDbfs() devolve a média em dBFS e zera o acumulador.
    // Sem isto cada linha da cachoeira vem de um único espectro instantâneo
    // e o piso de ruído treme; o OpenWebRX+ faz média de dezenas por linha.
    void accumulate(const std::complex<float>* iq, size_t n);
    bool hasAccumulated() const { return accumCount_ > 0; }
    std::vector<int8_t> takeAverageDbfs();

    // Teto de espectros somados por janela (limita a CPU nos TV box).
    void setMaxAverages(int n) { maxAvgPerWindow_ = (n < 1 ? 1 : n); }
    int  averagesInWindow() const { return accumCount_; }

private:
    void addPower(const std::vector<std::complex<float>>& spec);

    size_t             bins_ = 1024;
    std::vector<float> hann_;         // janela Hann pré-calculada
    std::vector<std::complex<float>> history_; // histórico de amostras IQ para manter bins_ elementos

    std::vector<double> accum_;       // soma de potência linear por bin
    int                 accumCount_ = 0;
    int                 maxAvgPerWindow_ = 16;
    int                 maxSegmentsPerCall_ = 2;
};

} // namespace masdr
