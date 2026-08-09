#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <mutex>
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
    std::unique_ptr<PactorManager> pactorDeco_;
    std::unique_ptr<DscManager> dscDeco_;
    std::unique_ptr<AnaliseManager> analiseDeco_;
    std::unique_ptr<SelcalManager> selcalDeco_;
    std::unique_ptr<TetraManager> tetraDeco_;
    IirDcBlock dcBlock_;

    quint16 port_ = 8070;
    QString mode_ = "USB";
    // freqA_ é lido na thread do callback librtlsdr E escrito na thread HTTP
    // (onTune). Deve ser atômico para evitar data race.
    std::atomic<uint64_t> freqA_{14250000ULL};
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

    std::chrono::steady_clock::time_point lastFftTime_ = std::chrono::steady_clock::now();

    void wireDeviceCallback();
    void applyConfigToDevice();
    bool applyConfigJson(const QJsonObject& j);
};

} // namespace masdr
