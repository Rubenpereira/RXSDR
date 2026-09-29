#pragma once
// ---------------------------------------------------------------------------
//  Dsd - DMR / P25 / NXDN / dPMR / YSF / D-STAR... pelo dsd-fme
//
//  Mesmo arranjo do DsdManager do RXSDR principal, sem Qt:
//    audio FM (48 kHz, 16 bits) -> stdin do dsd-fme
//    voz decodificada <- UDP 127.0.0.1 (taxa ~16150 Hz, reamostrada a 48 kHz)
//    texto do dsd-fme (stdout+stderr) -> quadro dos slots e registro
//
//  O dsd-fme.exe e as DLLs dele ficam na pasta "decoders" ao lado do
//  RXSDR.exe. Ele e um programa Cygwin 3.6: roda no Windows 8.1, 10 e 11
//  (o Cygwin deixou o Windows 7 para tras).
// ---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace masdr {

struct SlotDmr {
    std::string tipo = "-", src = "-", tgt = "-";
    int voz = 0, dados = 0, fec = 0;
    double ultVoz = 0, ultAtivo = 0;
};

struct EstadoDmr {
    SlotDmr ts[2];
    int cc = -1;
    std::string protocolo;
    bool rodando = false;
    std::string erro;
    int linhas = 0;
};

class Dsd {
public:
    static const char* nomeModo(int i);
    static int nModos();

    Dsd();
    ~Dsd();

    bool iniciar(int modo, bool inverter, std::string& erro);
    void parar();
    bool rodando() const { return vivo_.load(); }

    void escrever(const int16_t* pcm, size_t n);        // audio FM 48 kHz
    void puxarVoz(int16_t* out, size_t n);              // voz decodificada 48 kHz
    void setTaxaVoz(int hz) { taxaVoz_ = std::clamp(hz, 14000, 18000); }
    void setTaxaSaida(uint32_t sps) { if (sps >= 8000) taxaSaida_ = sps; }   // taxa do som do radio

    EstadoDmr estado();
    void limparContadores();

    // texto para a janela (mensagens e, se pedido, as linhas cruas)
    std::function<void(const std::string&)> aoTexto;
    std::atomic<bool> linhasCruas{false};

private:
    void lerTexto();
    void lerUdp();
    void tratarLinha(std::string linha);
    void tratarLinhaSegura(std::string linha);
    void guardarVoz(const float* x, size_t n);

    std::atomic<bool> vivo_{false};
    void* proc_ = nullptr;          // HANDLE
    void* stdinW_ = nullptr;        // HANDLE
    void* saidaR_ = nullptr;        // HANDLE
    uintptr_t sock_ = ~(uintptr_t)0;
    std::thread thTexto_, thUdp_;
    std::mutex escritaMutex_;
    void* dump_ = nullptr;          // FILE* do diagnostico
    bool dumpTestado_ = false;

    // voz
    std::mutex vozMutex_;
    std::deque<int16_t> voz_;
    bool tocando_ = false;
    std::vector<float> caudaVoz_;
    double posVoz_ = 0;
    int taxaVoz_ = 16150;
    std::atomic<uint32_t> taxaSaida_{48000};

    // estado dos slots
    std::mutex estMutex_;
    EstadoDmr est_;
    int ultimoSlot_ = 0;
    std::string ultimaMsg_, ultimaCrua_;
};

} // namespace masdr
