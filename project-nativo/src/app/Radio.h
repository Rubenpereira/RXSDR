#pragma once
// ---------------------------------------------------------------------------
//  Radio - o "motor" do RXSDR Nativo, sem tela e sem servidor
//
//  E o mesmo caminho do RXSDR de navegador (Application.cpp do projeto
//  principal), sem HTTP nem WebSocket:
//
//    dongle -> ganho digital -> bloqueador de DC -> FFT (media, 25 quadros/s)
//                             \-> NCO (VFO) -> demodulador -> SomNativo
//
//  A tela (Ui) pede quadros de FFT com pegarFft() e manda as ordens
//  (sintonizar, modo, ligar...) por metodos daqui. Tudo que a thread do
//  dongle le e protegido por atomics ou mutex.
// ---------------------------------------------------------------------------
#include <atomic>
#include <complex>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../dsp/Filters.h"
#include "SomNativo.h"
#include "Decoders.h"

namespace masdr {

class ISdrDevice;
class FftProcessor;
class Demodulator;

struct DispositivoInfo {
    std::string tipo;     // rtlsdr | rtltcp | sdrplay
    std::string serial;
    std::string nome;
};

struct QuadroFft {
    std::vector<int8_t> bins;   // dBFS por bin, do mais baixo ao mais alto
    uint64_t centro = 0;
    uint32_t taxa = 0;
    uint64_t seq = 0;
};

class Radio {
public:
    Radio();
    ~Radio();

    bool iniciar();                       // abre o som; nao liga o dongle
    void encerrar();

    // sdrplay=false: nao carrega a API da SDRplay (so quando o tipo SDRplay
    // e escolhido; com ela carregada o fechamento do programa ficava instavel)
    static std::vector<DispositivoInfo> listar(const std::string& tipoAberto = "", const std::string& serialAberto = "", bool sdrplay = false);
    bool selecionar(const std::string& tipo, const std::string& serial, std::string& erro);
    bool ligar(bool on, std::string& erro);
    bool ligado() const { return ligado_.load(); }
    bool temDispositivo() const { return (bool)dev_; }
    std::string tipo() const { return tipo_; }
    std::string serial() const { return serial_; }

    // Sintonia. sintonizar() muda so o VFO; se sair da janela do dongle,
    // recentraliza sozinho. Devolve true se o centro mudou.
    bool sintonizar(uint64_t hz);
    void centralizar(uint64_t hz);        // muda o centro do dongle
    uint64_t vfo() const { return vfo_.load(); }
    uint64_t centro() const;
    uint32_t taxa() const;
    void setModo(const std::string& m);
    std::string modo() const;
    void setBanda(int hz);
    int banda() const { return bw_.load(); }
    void setGanho(int tenths);            // 0..496 (dB x 10); ignora com AGC
    void aplicarConfig();                 // relê taxa, ppm, bias, Q, SDRplay, FFT
    bool qAtivo() const;                  // amostragem direta valendo agora

    bool pegarFft(QuadroFft& out);        // true se chegou quadro novo
    bool aoVivo() const;                  // chegou IQ nos ultimos 2 s
    float picoDb() const { return pico_.load(); }

    SomNativo& som() { return som_; }
    Decoders& decoders() { return dec_; }
    // Troca a placa de som (nome como aparece no Windows; vazio = padrao)
    bool trocarSaida(const std::string& nome);
    static int indiceSaida(const std::string& nome);

private:
    void ligarCallback();
    void novoDemod();
    void aoAudio(const std::vector<int16_t>& pcm, uint32_t sps);

    std::shared_ptr<ISdrDevice> dev_;
    std::string tipo_, serial_;
    std::atomic<bool> ligado_{false};
    bool qAtual_ = false;
    int  ppmAplicado_ = 0;
    size_t fftN_ = 0;

    std::atomic<uint64_t> vfo_{7100000};
    std::atomic<int>      bw_{2400};
    std::string           modo_ = "LSB";
    mutable std::mutex    modoMutex_;

    std::unique_ptr<FftProcessor> fft_;
    std::mutex fftMutex_;
    QuadroFft ultimo_;
    std::mutex quadroMutex_;
    uint64_t seqLido_ = 0;
    int64_t  ultimoFftMs_ = 0;

    std::unique_ptr<Demodulator> demod_;
    std::mutex demodMutex_;

    IirDcBlock dcBlock_;
    double fase_ = 0.0;
    std::atomic<int64_t> ultimoIqMs_{0};
    std::atomic<float>   pico_{-120.f};

    SomNativo som_;
    Decoders dec_;                    // decodificadores digitais (thread propria)
    mutable std::mutex devMutex_;     // selecionar/ligar/sintonizar vem da tela
};

} // namespace masdr
