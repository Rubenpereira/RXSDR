#pragma once
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <cstdint>
#include "../dsp/Filters.h"
#include "../../third_party/masdr_json.h"

namespace masdr {

class HttpServer;
class WsServer;
class RestApi;
class TrayController;
class ISdrDevice;
class FftProcessor;
class Demodulator;
class SdrplayDevice;

class Application {
public:
    Application();
    ~Application();

    bool start();
    void stop();

    std::string frontendUrl() const;

private:
    std::unique_ptr<HttpServer>      http_;
    std::unique_ptr<WsServer>        ws_;
    std::unique_ptr<RestApi>         rest_;
    std::unique_ptr<TrayController>  tray_;
    std::shared_ptr<ISdrDevice>      device_;
    std::unique_ptr<FftProcessor>    fft_;
    std::unique_ptr<Demodulator>     demod_;
    IirDcBlock dcBlock_;

    uint16_t    port_       = 8080;
    std::string mode_       = "USB";
    std::atomic<uint64_t> freqA_{14250000ULL};
    std::atomic<uint32_t> bwHz_{3000};
    std::string deviceType_;
    std::string deviceSerial_;
    std::atomic<float>   peakDb_{-120.f};
    std::atomic<int64_t> lastIqMs_{0};
    bool powerOn_     = false;
    bool hadWsClient_ = false;
    double phaseAcc_  = 0.0;

    // Timer "sem clientes": encerra após 5 min sem nenhum cliente WS
    std::thread          noClientTimer_;
    std::atomic<bool>    noClientTimerRunning_{false};
    std::chrono::steady_clock::time_point lastClientTime_;

    mutable std::mutex demodMutex_;
    std::vector<int16_t> audioBuffer_;
    std::mutex audioBufferMutex_;

    std::chrono::steady_clock::time_point lastFftTime_ = std::chrono::steady_clock::now();

    void wireDeviceCallback();
    void applyConfigToDevice();
    bool applyConfigJson(const Json& j);
    void handleAudioCallback(const std::vector<int16_t>& pcm, uint32_t sps);
    void startNoClientTimer();
    void stopNoClientTimer();
};

} // namespace masdr
