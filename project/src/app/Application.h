#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <QThread>
#include <memory>
#include <mutex>
#include <deque>
#include <atomic>
#include <chrono>
#include "../dsp/Filters.h"

class QJsonObject;

namespace masdr {

class HttpServer;
class WsServer;
class RestApi;
class TrayController;
class ISdrDevice;
class FftProcessor;
class Demodulator;
class AisCatcherManager;
class AcarsDecoManager;
class DsdManager;
class AprsManager;
class AprsIsClient;
class SitorBManager;
class PactorManager;
class DscManager;
class AnaliseManager;
class CwManager;
class SelcalManager;
class TetraManager;

class Application : public QObject {
    Q_OBJECT
public:
    Application();
    ~Application() override;

    bool start();
    void stop();

    QString frontendUrl() const;

    // Modo headless (Raspberry Pi): desabilita auto-quit e tray icon
    void setHeadless(bool h) { headless_ = h; }
    bool isHeadless() const { return headless_; }

private:
    std::unique_ptr<HttpServer> http_;
    std::unique_ptr<WsServer>   ws_;
    std::unique_ptr<RestApi>    rest_;
#ifndef RXSDR_HEADLESS
    std::unique_ptr<TrayController> tray_;
#endif
    std::shared_ptr<ISdrDevice> device_;
    std::unique_ptr<FftProcessor> fft_;
    std::unique_ptr<Demodulator> demod_;
    std::unique_ptr<AisCatcherManager> aisCatcher_;
    std::unique_ptr<AcarsDecoManager> acarsDeco_;
    std::unique_ptr<DsdManager> dsdDeco_;
    std::unique_ptr<AprsManager> aprsDeco_;
    std::unique_ptr<AprsIsClient> aprsIs_;
    std::unique_ptr<SitorBManager> sitorBDeco_;
    std::unique_ptr<CwManager>     cwDeco_;
    std::unique_ptr<PactorManager> pactorDeco_;
    std::unique_ptr<DscManager> dscDeco_;
    std::unique_ptr<AnaliseManager> analiseDeco_;
    std::unique_ptr<SelcalManager> selcalDeco_;
    std::unique_ptr<TetraManager> tetraDeco_;
    IirDcBlock dcBlock_;

    // Leva o audio a 48000 Hz exatos antes de mandar para o navegador.
    // Fica so no caminho do navegador: os decodificadores continuam
    // recebendo a taxa original, sem pagar uma interpolacao a mais.
    Resampler48k audioResampler_;
    std::vector<int16_t> audioSaida48k_;   // reaproveitado a cada bloco

    // ---- saida de audio RITMADA -------------------------------------------
    // O audio nascia junto com o IQ e era despachado na mesma hora. Como a
    // leitura do TCP, a demodulacao, a FFT e o envio acontecem todos em
    // sequencia na mesma thread, o socket ficava sem ser lido durante o
    // processamento e os dados empilhavam: a leitura seguinte trazia um bloco
    // grande e o audio saia aos trancos. Medido em 15 minutos: 449 intervalos
    // acima de 150 ms e o buffer do navegador oscilando de 28 a 1259 ms.
    //
    // Agora o audio pronto entra numa fila e um temporizador o entrega em
    // ritmo constante. Nada se perde - so para de sair em rajada.
    // O temporizador do audio vive numa THREAD PROPRIA.
    //
    // Ritmar a saida na thread principal ja tinha derrubado as faltas de 18
    // para 4 em 15 minutos. O que sobrou tem causa clara: o proprio
    // temporizador morava na thread que engasga, entao quando a leitura do
    // TCP, a demodulacao e a FFT ocupavam o processador ele tambem parava - e
    // ao voltar entregava de uma vez o que ficou represado. O metronomo estava
    // preso ao lugar que perde o compasso.
    //
    // Aqui so o RELOGIO muda de lugar. Ele continua tirando audio da mesma
    // fila, com o mesmo mutex, e chamando o mesmo envio (que ja e seguro entre
    // threads). O dispositivo, os decodificadores e a interface nao se mexem,
    // entao nao ha estado novo compartilhado nem corrida para inventar.
    QThread*                audioThread_ = nullptr;
    QTimer*                 audioPaceTimer_ = nullptr;
    std::mutex              audioFilaMutex_;
    std::deque<int16_t>     audioFila48k_;
    // Atomico porque e escrito na thread do temporizador de audio e zerado
    // pela thread que atende o liga/desliga.
    std::atomic<int64_t>    audioPaceUltimo_{0};

    // Contabilidade da PRODUCAO de audio. O navegador ja foi inocentado -
    // worklet ligado, zero faltas, zero mutes, zero descartes - e mesmo assim
    // ha picote na abertura. Entao a pergunta passa a ser: este lado aqui
    // entrega 48000 amostras por segundo de relogio, ou entrega menos?
    std::atomic<int64_t>  audioAmostras_{0};   // total ja enviado
    std::atomic<int64_t>  audioEsperadoMs_{0}; // tempo COM audio fluindo
    std::atomic<int64_t>  audioT0_{0};         // instante do primeiro envio
    std::atomic<int64_t>  audioUltimo_{0};     // instante do envio anterior
    std::atomic<int>      audioMaiorVaoMs_{0};
    std::atomic<int>      audioVaosGrandes_{0}; // vaos acima de 150 ms

    // Quantas vezes o dongle foi REPROGRAMADO. Cada applyConfigToDevice mexe
    // em taxa, bias, quadratura, ppm e ganho - cinco operacoes de hardware, e
    // o sinal da um pulo real a cada uma. Se este contador subir umas vinte
    // vezes na abertura, achamos o picote.
    std::atomic<int>      reconfigs_{0};

    quint16 port_ = 8070;
    QString mode_ = "USB";
    // freqA_ é lido na thread do callback librtlsdr E escrito na thread HTTP
    // (onTune). Deve ser atômico para evitar data race.
    std::atomic<uint64_t> freqA_{14250000ULL};
    // Instante a partir do qual o audio pode sair. Serve para engolir o
    // comeco do fluxo do dongle, que chega irregular - ver aquecimentoMs().
    std::atomic<int64_t> audioLiberadoEm_{0};

    // Ultima linha do dsd-fme repassada ao navegador, para estrangular o fluxo.
    qint64 dsdUltimaLinhaMs_ = 0;

    // Zera o ritmo e joga fora o audio da sessao anterior. Sem isto, o primeiro
    // tique do temporizador depois de ligar calcula um intervalo enorme (o
    // tempo em que o radio ficou desligado), corta em 500 ms e despeja meio
    // segundo de audio VELHO de uma vez - no exato instante em que tudo esta
    // comecando.
    void reiniciarRitmoAudio();

    std::atomic<uint32_t> bwHz_{3000};
    QString deviceType_ = "none";
    QString deviceSerial_;
    std::atomic<float> peakDb_{-120.f};
    std::atomic<qint64> lastIqMs_{0};
    bool powerOn_ = false;
    bool headless_ = false;
    bool hadWsClient_ = false;
    std::unique_ptr<QTimer> noClientQuitTimer_;

    double phaseAcc_ = 0.0;

    // Protege demod_ contra acesso simultâneo entre:
    //   • thread HTTP  → onTune reassigna demod_
    //   • thread librtlsdr → callback chama demod_->process()
    // Sem este mutex ocorre use-after-free: o onTune destrói o objeto
    // enquanto process() ainda está executando → SEGFAULT → processo morre.
    mutable std::mutex demodMutex_;
    mutable std::mutex fftMutex_;

    std::vector<int16_t> audioBuffer_;
    std::mutex audioBufferMutex_;
    void handleAudioCallback(const std::vector<int16_t>& pcm, uint32_t sps);
    void enviarAudioAoNavegador(const std::vector<int16_t>& pcm, uint32_t sps);
    void entregarAudioRitmado();

    std::chrono::steady_clock::time_point lastFftTime_ = std::chrono::steady_clock::now();

    void wireDeviceCallback();
    void applyConfigToDevice();
    bool applyConfigJson(const QJsonObject& j);
};

} // namespace masdr
