#pragma once
// ---------------------------------------------------------------------------
//  SomNativo - o RXSDR (versao Windows 7) toca o audio DIRETO na placa de som
//
//  Por que existe: no PC de um amigo do autor (AMD E-350 1,6 GHz, Win7) o som
//  pelo navegador "acelerava e desacelerava". O diagnostico mostrou duas
//  coisas: o Chrome usava um nucleo inteiro so para desenhar, e a placa de som
//  consumia 8% mais devagar do que dizia ao Chrome ("placa x0.919" = 44100 /
//  48000). SDR# e SDR Console tocam direto na placa e la funcionam - entao a
//  versao Windows 7 passou a fazer o mesmo. O navegador continua sendo a tela
//  (cachoeira, botoes, VFO); o volume, o MUTE, o squelch e os redutores de
//  ruido da tela chegam aqui por POST /api/audio (ver ProcAudio).
//
//  waveOut: a interface de audio mais simples e compativel do Windows (existe
//  desde o 95), 48 kHz, 16 bits, mono. Uma thread entrega blocos de 20 ms.
//
//  Ritmo: mede-se quantas amostras chegam para cada uma que a placa consome
//  (janela de ate 20 s - rajadas nao atrapalham a conta) e o passo de leitura
//  segue essa razao, com interpolacao; uma correcao pequena pela fila media
//  mantem a fila no alvo. O alvo comeca em 200 ms e cresce 50% a cada falta,
//  ate 800 ms: num PC que trava, a reserva se ajusta sozinha.
// ---------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include "ProcAudio.h"

#include <atomic>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace masdr {

class SomNativo {
public:
    SomNativo() = default;
    ~SomNativo() { parar(); }

    // Abre a placa. dispositivo = -1: a padrao do Windows; senao o numero da
    // lista de listarSaidas().
    bool iniciar(int dispositivo = -1);
    static std::vector<std::string> listarSaidas();

    // Gravacao do que se ouve (depois dos redutores e do tom, antes do volume
    // - igual ao gravador da pagina). WAV 48 kHz mono 16 bits.
    bool gravar(const std::string& arquivo);
    void pararGravacao();
    bool gravando() const { return gravando_.load(); }
    double segundosGravados() const { return gravadas_.load() / 48000.0; }
    void parar();
    bool ativo() const { return aberto_; }
    std::string erro() const { return erro_; }

    // Audio demodulado, na taxa em que ele vier (ex.: 51200 no WFM).
    void empurrar(const int16_t* pcm, size_t n, uint32_t sps);
    // Ajustes vindos da tela (POST /api/audio): mudo/squelch, volume, NB,
    // NR ESPECTRAL, Redutor de Ruido, tom.
    void ajustar(const AjusteAudio& a);
    AjusteAudio ajuste() const { return proc_.ajuste(); }

    // Para o rodape / diagnostico
    double passo() const { return passoAtual_.load(); }
    int    filaMs() const { return filaMs_.load(); }
    int    faltas() const { return faltas_.load(); }
    // Nivel do audio que entra (0..1), para o olho magico - mesma conta da pagina
    float  nivel() const { return nivel_.load(); }

private:
    static constexpr int kTaxa   = 48000;
    static constexpr int kBloco  = 960;     // 20 ms
    static constexpr int kNBlocos = 6;      // 120 ms dentro do Windows
    static constexpr int kAlvo   = kTaxa / 5;   // fila alvo inicial: 200 ms
    static constexpr int kAlvoMax = kTaxa * 4 / 5; // cresce ate 800 ms se faltar
    static constexpr int kTeto   = kTaxa;       // 1 s: acima disso descarta

    void laco();
    void encher(int i);

    HWAVEOUT h_ = nullptr;
    HANDLE   evento_ = nullptr;
    WAVEHDR  hdr_[kNBlocos] = {};
    std::vector<int16_t> dados_[kNBlocos];
    std::thread t_;
    std::atomic<bool> rodando_{false};
    bool aberto_ = false;
    std::string erro_;

    // gravacao
    std::mutex gravMutex_;
    FILE* grav_ = nullptr;
    std::atomic<bool> gravando_{false};
    std::atomic<long long> gravadas_{0};

    // fila a 48 kHz (float -1..1)
    std::deque<float> fila_;
    std::mutex filaMutex_;
    bool enchendo_ = true;       // esperando a fila chegar ao alvo (inicio / depois de falta)

    // entrada: reamostragem + redutores de ruido (fora do filaMutex_, para
    // a thread da placa nunca esperar pelas FFTs do NR)
    std::mutex entradaMutex_;
    ProcAudio  proc_;
    std::vector<float> bloco_;
    // reamostragem de entrada (taxa do demodulador -> 48 kHz)
    uint32_t spsEntrada_ = 0;
    double   posEntrada_ = 0.0;
    float    ultimaEntrada_ = 0.f;
    bool     temUltima_ = false;

    // leitura com ritmo ajustado
    double frac_ = 0.0;
    // relogio: quantas amostras chegam (a 48 kHz) para cada amostra que a
    // placa consome, medido em janelas longas - as rajadas nao atrapalham
    long long totalEntrada_ = 0;          // amostras postas na fila
    long long totalSaida_ = 0;            // amostras entregues a placa
    std::deque<std::pair<long long, long long>> marcas_;   // (entrada, saida) a cada 1 s
    int    contaBlocos_ = 0;
    double base_ = 1.0;                   // razao medida (suavizada)
    double filaMedia_ = kAlvo;
    int    alvo_ = kAlvo;        // cresce 50% a cada falta, ate kAlvoMax
    bool   faltaPendente_ = false;   // esvaziou; falta de verdade se voltar em < 2 s
    std::chrono::steady_clock::time_point faltaEm_{};
    std::atomic<double> passoAtual_{1.0};
    std::atomic<int>    filaMs_{0};
    std::atomic<int>    faltas_{0};
    std::atomic<float>  nivel_{0.f};

    // volume (0..8, igual a pagina; a saturacao suave segura os picos)
    std::atomic<float> ganhoAlvo_{0.f};
    float ganhoAtual_ = 0.f;
};

} // namespace masdr
