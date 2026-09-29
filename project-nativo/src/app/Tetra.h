#pragma once
// ---------------------------------------------------------------------------
//  Tetra - TETRA (voz e informacoes da celula) sem Python
//
//  Mesma cadeia do RXSDR principal, com o orquestrador e o demodulador
//  (tetra_decoder.py / tetra_demod.py, numpy) traduzidos para C++:
//
//    IQ do dongle (VFO no zero) -> 36 kS/s -> demod pi/4-DQPSK
//      (FIR 13 kHz, AGC, aquisicao por FFT, FLL de borda de banda, RRC +
//       Gardner, PLL de portadora, fase diferencial -> 1 float por simbolo)
//    -> tetra-rx.exe -i -r -s -e -        (osmo-tetra)
//         TETMON por UDP -> informacoes da celula, chamadas
//                        -> quadros ACELP -> cdecoder | sdecoder -> voz 8 kHz
//
//  Os programas ficam na pasta "decoders" ao lado do RXSDR.exe:
//  tetra-rx.exe, libosmocore-22.dll, libpseudotalloc-0.dll,
//  libwinpthread-1.dll, cdecoder.exe, sdecoder.exe.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace masdr {

struct EstadoTetra {
    bool rodando = false;
    bool travado = false;         // FLL travado
    float snr = -99;              // dB (so com trava)
    float afc = 0;                // Hz
    int bursts = 0, sb = 0, ndb = 0, voz = 0;
    int tetmon = 0;               // pacotes TETMON recebidos (diagnostico)
    int mcc = -1, mnc = -1, cc = -1;
    std::string cripto = "-";     // none / TEA1 / TEA2 / TEA3
    std::string ts[4] = {"?", "?", "?", "?"};
    std::string ultimaChamada;    // "SSI 1234 -> 5678"
    double ultVoz = 0;            // hora (s) do ultimo quadro de voz
    std::string erro;
    bool adquirindo = true;
    float afcInicial = 0;
};

class Tetra {
public:
    Tetra();
    ~Tetra();

    bool iniciar(bool inverterIQ, std::string& erro);
    void parar();
    bool rodando() const { return vivo_.load(); }

    // IQ do dongle, ja com o VFO no zero (thread do dongle; so copia/decima)
    void alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps);
    void puxarVoz(int16_t* out, size_t n, uint32_t sps);

    EstadoTetra estado();
    std::function<void(const std::string&)> aoTexto;
    std::atomic<bool> linhasCruas{false};   // mostra o que o tetra-rx escreve

private:
    // --- demodulador (thread propria) ---
    void lacoDemod();
    void demodular(const std::complex<float>* x, size_t n, std::vector<float>& soft);
    void adquirir();
    // --- saidas do tetra-rx ---
    void lerTetraRx();
    void lerTetmon();
    bool decodificarAcelp(const uint8_t* q, std::vector<int16_t>& pcm);
    bool abrirCodec();
    void fecharCodec();
    void guardarVoz(const int16_t* p, size_t n);
    void escreverSoft(const std::vector<float>& s);
    void msg(const std::string& s);

    std::atomic<bool> vivo_{false};
    bool inverter_ = false;

    // processos e canos (HANDLE como void*)
    void* procRx_ = nullptr;
    void* rxStdinW_ = nullptr;
    void* rxSaidaR_ = nullptr;
    void* procC_ = nullptr;
    void* procS_ = nullptr;
    void* codecW_ = nullptr;
    void* codecR_ = nullptr;
    uintptr_t sockTetmon_ = ~(uintptr_t)0;
    int portaTetmon_ = 0;
    std::thread thDemod_, thRx_, thTetmon_;
    std::mutex escritaMutex_;

    // decimacao para 36 kS/s (thread do dongle)
    uint32_t decSps_ = 0;
    size_t aaM_ = 1, aaIdx_ = 0;
    std::vector<std::complex<float>> aaAnel_;
    std::complex<double> aaSoma_{0, 0};
    float lpA_ = 0.1f;
    std::complex<float> lp1_{0, 0}, lp2_{0, 0};
    std::vector<std::complex<float>> decCauda_;
    double decPos_ = 0;

    // fila para o demodulador
    std::mutex filaMutex_;
    std::condition_variable filaCv_;
    std::deque<std::complex<float>> fila_;

    // estado do demodulador
    std::vector<std::complex<float>> acq_;
    bool acqFeita_ = false;
    std::vector<float> fir_;                          // 127 taps, 13 kHz
    std::vector<std::complex<float>> firHist_;
    std::vector<float> agcHist_; size_t agcPos_ = 0; double agcSoma_ = 0;
    std::vector<std::complex<float>> hLow_, hHigh_, fllHist_;
    double fllKp_ = 0, fllKi_ = 0, fllInteg_ = 0, fllFreq_ = 0, fllFase_ = 0;
    double errVarEma_ = 1, errBiasEma_ = 1;
    bool fllTravado_ = false;
    std::vector<float> rrc_;
    std::vector<std::complex<float>> mfHist_, mfY_;   // saida do filtro casado (continua)
    double mfPos_ = 0, mu_ = 0, rate_ = 0;
    std::complex<float> ultSym_{0, 0};
    double pllFase_ = 0, pllFreq_ = 0;
    std::complex<float> prevSym_{1, 0};
    double snrEma_ = -99; bool snrOk_ = false;
    std::vector<std::complex<float>> bloco_;

    // voz
    std::mutex vozMutex_;
    std::deque<int16_t> voz_;
    bool tocando_ = false;
    std::vector<float> caudaVoz_;
    double posVoz_ = 0;
    std::atomic<uint32_t> taxaSaida_{48000};

    // estado exposto
    std::mutex estMutex_;
    EstadoTetra est_;
    std::string ultMeta_;
    bool tetmonCripto_ = false;
};

} // namespace masdr
