#include "Application.h"
#include "Config.h"
#include "TrayController.h"
#include "../server/HttpServer.h"
#include "../server/WsServer.h"
#include "../server/RestApi.h"
#include "../sdr/DeviceFactory.h"
#include "../sdr/ISdrDevice.h"
#include "../sdr/RtlTcpClient.h"
#include "../sdr/SdrplayDevice.h"
#include "../dsp/FftProcessor.h"
#include "../dsp/DemodSSB.h"
#include "../dsp/DemodAM.h"
#include "../dsp/DemodFM.h"
#include "../dsp/DemodCW.h"
#include "../dsp/Filters.h"
#include "../util/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace masdr {
namespace {

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static std::string toUpper(const std::string& s) {
    std::string r;
    for (char c : s) r += (char)toupper((unsigned char)c);
    return r;
}

std::unique_ptr<Demodulator> createDemodForMode(const std::string& mode) {
    const std::string m = toUpper(mode);
    if (m == "AM") return std::make_unique<DemodAM>();
    if (m == "FM" || m == "NFM" || m == "WFM") {
        auto d = std::make_unique<DemodFM>();
        d->setMode(m);
        return d;
    }
    if (m == "CW") return std::make_unique<DemodCW>();
    auto d = std::make_unique<DemodSSB>();
    d->setMode(m);
    return d;
}

Json buildConfigJson() {
    const auto& c = Config::instance();
    Json o = Json::object();
    o["deviceType"]     = Json(c.lastDevice());
    o["deviceSerial"]   = Json(c.lastSerial());
    o["rtltcpHost"]     = Json(c.rtltcpHost());
    o["rtltcpPort"]     = Json((int)c.rtltcpPort());
    o["sampleRate"]     = Json((int)c.sampleRate());
    o["gainTenths"]     = Json(c.gainTenths());
    o["agc"]            = Json(c.agc());
    o["biasT"]          = Json(c.biasT());
    o["quadrature"]     = Json(c.quadrature());
    o["ppm"]            = Json(c.ppm());
    o["iqCorrection"]   = Json(c.iqCorrection());
    o["sdrplayIfMode"]  = Json(c.sdrplayIfMode());
    o["sdrplayLnaState"]= Json(c.sdrplayLnaState());
    o["sdrplayIfGain"]  = Json(c.sdrplayIfGain());
    o["sdrplayIfAgc"]   = Json(c.sdrplayIfAgc());
    o["sdrplayBw"]      = Json(c.sdrplayBw());
    Json sm = Json::object();
    sm["hfOffset"]   = Json(c.smeterHfOffset());
    sm["vhfOffset"]  = Json(c.smeterVhfOffset());
    sm["s9Hf"]       = Json(c.smeterS9Hf());
    sm["s9Vhf"]      = Json(c.smeterS9Vhf());
    sm["hfEmpty"]    = Json(c.smeterHfEmpty());
    sm["vhfEmpty"]   = Json(c.smeterVhfEmpty());
    sm["rmsAligned"] = Json(c.smeterRmsAligned());
    o["smeter"] = sm;
    return o;
}

std::string resolveDeviceEndpoint(const std::string& type, const std::string& serial) {
    const auto& cfg = Config::instance();
    if (type != "rtltcp") return serial;
    if (!serial.empty() && serial.find(':') != std::string::npos) return serial;
    const std::string host = serial.empty() ? cfg.rtltcpHost() : serial;
    return host + ":" + std::to_string(cfg.rtltcpPort());
}

} // namespace

Application::Application()  = default;
Application::~Application() { stop(); }

bool Application::start() {
    Logger::info("RXSDR starting...");
    dcBlock_.reset();
    const auto& cfg = Config::instance();
    port_  = cfg.httpPort();
    freqA_.store(cfg.vfoA());
    mode_  = cfg.mode();

    fft_   = std::make_unique<FftProcessor>(8192);
    demod_ = createDemodForMode(mode_);
    demod_->setBandwidth(bwHz_.load());
    demod_->setAudioCallback([this](const std::vector<int16_t>& pcm, uint32_t sps) {
        handleAudioCallback(pcm, sps);
    });
    { std::lock_guard<std::mutex> lk(audioBufferMutex_); audioBuffer_.clear(); }

    // WebSocket
    ws_ = std::make_unique<WsServer>();
    if (!ws_->listen(cfg.wsPort())) {
        Logger::warn("WS na porta padrão falhou, tentando dinamica");
        if (!ws_->listen(0)) { Logger::error("Falha ao iniciar WS"); return false; }
    }
    lastClientTime_ = std::chrono::steady_clock::now();

    ws_->onClientsChanged = [this](int count) {
        if (count > 0) {
            hadWsClient_ = true;
            lastClientTime_ = std::chrono::steady_clock::now();
            stopNoClientTimer();
        } else if (hadWsClient_) {
            lastClientTime_ = std::chrono::steady_clock::now();
            startNoClientTimer();
        }
    };

    // REST
    rest_ = std::make_unique<RestApi>();

    rest_->onListDevices = [this]() {
        Json arr = DeviceFactory::scanAll(deviceType_, deviceSerial_);
        if (device_ && !deviceSerial_.empty()) {
            bool found = false;
            for (size_t i = 0; i < arr.size(); ++i) {
                const Json& o = arr[i];
                if (o["type"].getString() == deviceType_ && o["serial"].getString() == deviceSerial_)
                    { found = true; break; }
            }
            if (!found) {
                Json o = Json::object();
                o["type"]   = Json(deviceType_);
                o["serial"] = Json(deviceSerial_);
                o["name"]   = Json(deviceType_ == "rtlsdr" ? "RTL-SDR (Em uso)" : deviceType_);
                // Prepend: criar novo array com o primeiro
                Json newArr = Json::array();
                newArr.push(o);
                for (size_t i = 0; i < arr.size(); ++i) newArr.push(arr[i]);
                arr = newArr;
            }
        }
        return arr;
    };

    rest_->onSelectDevice = [this](const std::string& type, const std::string& serial) -> Json {
        Json out = Json::object();
        auto dev = DeviceFactory::create(type);
        if (!dev) {
            out["ok"]    = Json(false);
            out["error"] = Json("Tipo invalido: " + type);
            return out;
        }
        const std::string endpoint = resolveDeviceEndpoint(type, serial);
        const std::string openArg  = (type == "rtltcp") ? endpoint : serial;
        if (type == "rtltcp" && openArg.empty()) {
            out["ok"]    = Json(false);
            out["error"] = Json("Indique o IP e porta do servidor rtl_tcp.");
            return out;
        }
        if (device_) {
            Logger::info("Fechando dispositivo anterior: " + deviceType_);
            device_->stop(); device_->close(); device_.reset(); dcBlock_.reset();
        }
        if (type == "rtlsdr") {
            auto& c = Config::instance();
            dev->setSampleRate(c.sampleRate());
            bool q = c.quadrature() && freqA_.load() < 24000000ULL;
            dev->setQuadrature(q);
            dev->setPpm(c.ppm());
            dev->setBias(c.biasT());
            dev->setCenterFreq(freqA_.load());
            dev->setGain(c.agc() ? -1 : c.gainTenths());
        }
        if (!dev->open(openArg)) {
            out["ok"]    = Json(false);
            std::string err = dev->lastError();
            if (err.empty()) err = "Falha ao abrir " + type + " (" + openArg + ")";
            out["error"] = Json(err);
            return out;
        }
        device_       = dev;
        deviceType_   = type;
        deviceSerial_ = dev->serial().empty() ? openArg : dev->serial();
        Config::instance().setLastDevice(type);
        Config::instance().setLastSerial(deviceSerial_);
        if (type == "rtltcp") {
            auto colon = deviceSerial_.rfind(':');
            if (colon != std::string::npos) {
                Config::instance().setRtltcpHost(deviceSerial_.substr(0, colon));
                try { Config::instance().setRtltcpPort(std::stoi(deviceSerial_.substr(colon+1))); }
                catch(...) {}
            }
        }
        applyConfigToDevice();
        device_->setCenterFreq(freqA_.load());
        if (deviceType_ != "sdrplay")
            device_->setGain(Config::instance().agc() ? -1 : Config::instance().gainTenths());
        wireDeviceCallback();
        if (powerOn_) device_->start();
        Logger::info("Dispositivo selecionado: " + type + " (" + deviceSerial_ + ")");
        out["ok"]     = Json(true);
        out["serial"] = Json(deviceSerial_);
        return out;
    };

    rest_->onTune = [this](const std::string& /*vfo*/, uint64_t freq,
                           const std::string& mode, int bwHz) {
        freqA_.store(freq);
        std::string newMode = toUpper(mode);
        bool modeChanged = (newMode != mode_);
        mode_ = newMode;
        bwHz_.store(bwHz);
        Config::instance().setVfoA(freq);
        Config::instance().setMode(mode_);
        if (device_) {
            const uint32_t sr = device_->sampleRate();
            const int64_t diff = std::abs((int64_t)freq - (int64_t)device_->centerFreq());
            if (diff > (int64_t)(sr * 0.48)) {
                bool q = Config::instance().quadrature() && freq < 24000000ULL;
                device_->setQuadrature(q);
                device_->setCenterFreq(freq);
                if (deviceType_ == "sdrplay") {
                    auto* s = dynamic_cast<SdrplayDevice*>(device_.get());
                    if (s) {
                        const auto& c = Config::instance();
                        s->setSdrplayParams(c.sdrplayIfMode(), c.sdrplayLnaState(),
                                            c.sdrplayIfGain(), c.sdrplayIfAgc(), c.sdrplayBw());
                    }
                } else {
                    device_->setGain(Config::instance().agc() ? -1 : Config::instance().gainTenths());
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(demodMutex_);
            if (modeChanged || !demod_) {
                demod_ = createDemodForMode(mode_);
                demod_->setAudioCallback([this](const std::vector<int16_t>& pcm, uint32_t sps) {
                    handleAudioCallback(pcm, sps);
                });
                { std::lock_guard<std::mutex> lkA(audioBufferMutex_); audioBuffer_.clear(); }
            }
            if (demod_) demod_->setBandwidth(bwHz);
        }
        return true;
    };

    rest_->onSetCenter = [this](uint64_t freq) {
        if (!device_) return false;
        bool q = Config::instance().quadrature() && freq < 24000000ULL;
        device_->setQuadrature(q);
        device_->setCenterFreq(freq);
        if (deviceType_ == "sdrplay") {
            auto* s = dynamic_cast<SdrplayDevice*>(device_.get());
            if (s) {
                const auto& c = Config::instance();
                s->setSdrplayParams(c.sdrplayIfMode(), c.sdrplayLnaState(),
                                    c.sdrplayIfGain(), c.sdrplayIfAgc(), c.sdrplayBw());
            }
        } else {
            device_->setGain(Config::instance().agc() ? -1 : Config::instance().gainTenths());
        }
        return true;
    };

    rest_->onSetGain = [this](int gainTenths) {
        Config::instance().setGain(gainTenths);
        if (device_ && deviceType_ != "sdrplay")
            device_->setGain(Config::instance().agc() ? -1 : gainTenths);
        return true;
    };

    rest_->onPower = [this](bool on) {
        powerOn_ = on;
        Logger::info(std::string("Power ") + (on?"ON":"OFF") + " | device: " + (device_?deviceType_:"nenhum"));
        if (powerOn_ && !device_) {
            const auto& c = Config::instance();
            const std::string lastType = c.lastDevice();
            if (!lastType.empty() && rest_->onSelectDevice)
                rest_->onSelectDevice(lastType, c.lastSerial());
        }
        if (!device_) {
            if (powerOn_) { Logger::error("Sem dispositivo para ligar."); powerOn_=false; return false; }
            return true;
        }
        if (powerOn_) {
            { std::lock_guard<std::mutex> lk(audioBufferMutex_); audioBuffer_.clear(); }
            applyConfigToDevice();
            device_->setCenterFreq(freqA_.load());
            const auto& c = Config::instance();
            if (deviceType_ == "sdrplay") {
                auto* s = dynamic_cast<SdrplayDevice*>(device_.get());
                if (s) s->setSdrplayParams(c.sdrplayIfMode(),c.sdrplayLnaState(),
                                           c.sdrplayIfGain(),c.sdrplayIfAgc(),c.sdrplayBw());
            } else {
                device_->setGain(c.agc() ? -1 : c.gainTenths());
            }
            device_->start();
        } else {
            device_->stop();
            { std::lock_guard<std::mutex> lk(audioBufferMutex_); audioBuffer_.clear(); }
        }
        return true;
    };

    rest_->onGetConfig = []() { return buildConfigJson(); };
    rest_->onSetConfig = [this](const Json& j) { return applyConfigJson(j); };

    rest_->onStatus = [this]() {
        const auto& c = Config::instance();
        bool live = device_ && (nowMs() - lastIqMs_.load()) < 2000;
        Json o = Json::object();
        o["ok"]         = Json(true);
        o["deviceType"] = Json(deviceType_);
        o["serial"]     = Json(deviceSerial_);
        o["freqA"]      = Json((double)freqA_.load());
        o["mode"]       = Json(mode_);
        o["wsPort"]     = Json((int)(ws_ ? ws_->port() : 0));
        o["httpPort"]   = Json((int)port_);
        o["powerOn"]    = Json(powerOn_);
        o["streaming"]  = Json(live);
        o["rfMode"]     = Json(live ? std::string("live") : std::string("idle"));
        o["peakDb"]     = Json((double)peakDb_.load());
        o["gainTenths"] = Json(c.gainTenths());
        o["agc"]        = Json(c.agc());
        o["biasT"]      = Json(c.biasT());
        o["quadrature"] = Json(c.quadrature());
        o["sampleRate"] = Json((int)c.sampleRate());
        return o;
    };

    // HTTP
    http_ = std::make_unique<HttpServer>();
    http_->setRestApi(rest_.get());
    if (!http_->listen(port_)) {
        for (uint16_t p : {(uint16_t)8081,(uint16_t)8082,(uint16_t)8083,(uint16_t)8084,(uint16_t)0}) {
            if (http_->listen(p)) { port_ = p; break; }
        }
    }
    if (http_->port() == 0) { Logger::error("Falha ao iniciar HTTP"); return false; }
    port_ = http_->port();

    // Auto-select
    const std::string lastType = cfg.lastDevice();
    if (!lastType.empty()) rest_->onSelectDevice(lastType, cfg.lastSerial());

    Logger::info("Backend em " + frontendUrl());
    return true;
}

void Application::stop() {
    stopNoClientTimer();
    dcBlock_.reset();
    if (device_) { device_->stop(); device_->close(); device_.reset(); }
    if (ws_)     ws_->stop();
    if (http_)   http_->stop();
    { std::lock_guard<std::mutex> lk(audioBufferMutex_); audioBuffer_.clear(); }
}

std::string Application::frontendUrl() const {
    // Usa 127.0.0.1 em vez de localhost para evitar resolucao IPv6 no Windows 7
    return "http://127.0.0.1:" + std::to_string(port_);
}

void Application::startNoClientTimer() {
    if (noClientTimerRunning_) return;
    noClientTimerRunning_ = true;
    noClientTimer_ = std::thread([this]{
        while (noClientTimerRunning_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!noClientTimerRunning_) break;
            if (ws_ && ws_->clientCount() > 0) break;
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - lastClientTime_).count();
            if (elapsed >= 300) { // 5 minutos
                Logger::info("Sem clientes por 5 min, encerrando.");
                PostQuitMessage(0);
                break;
            }
        }
        noClientTimerRunning_ = false;
    });
}

void Application::stopNoClientTimer() {
    noClientTimerRunning_ = false;
    if (noClientTimer_.joinable()) noClientTimer_.join();
}

void Application::applyConfigToDevice() {
    if (!device_) return;
    auto& c = Config::instance();
    bool q = c.quadrature() && freqA_.load() < 24000000ULL;
    if (!q && c.quadrature())
        Logger::info("Quadrature ignorado em VHF/UHF. Config preservada.");
    device_->setSampleRate(c.sampleRate());
    device_->setBias(c.biasT());
    device_->setQuadrature(q);
    device_->setPpm(c.ppm());
    if (deviceType_ == "sdrplay") {
        auto* s = dynamic_cast<SdrplayDevice*>(device_.get());
        if (s) s->setSdrplayParams(c.sdrplayIfMode(),c.sdrplayLnaState(),
                                   c.sdrplayIfGain(),c.sdrplayIfAgc(),c.sdrplayBw());
    } else {
        device_->setGain(c.agc() ? -1 : c.gainTenths());
    }
}

bool Application::applyConfigJson(const Json& j) {
    auto& cfg = Config::instance();
    const std::string oldType   = deviceType_;
    const std::string oldSerial = deviceSerial_;
    const bool  oldQ  = cfg.quadrature();
    const int   oldSR = cfg.sampleRate();
    const bool  oldBT = cfg.biasT();

    std::string newType   = j.contains("deviceType")   ? j["deviceType"].getString()   : oldType;
    std::string newSerial = j.contains("deviceSerial") ? j["deviceSerial"].getString() : oldSerial;

    if (newType == "rtltcp") {
        std::string host = j.contains("rtltcpHost") ? j["rtltcpHost"].getString() : cfg.rtltcpHost();
        int port = j.contains("rtltcpPort") ? (int)j["rtltcpPort"].getInt() : cfg.rtltcpPort();
        if (!host.empty()) newSerial = host + ":" + std::to_string(port);
    }

    if (j.contains("deviceType"))   cfg.setLastDevice(j["deviceType"].getString());
    if (j.contains("deviceSerial")) cfg.setLastSerial(j["deviceSerial"].getString());
    if (j.contains("rtltcpHost"))   cfg.setRtltcpHost(j["rtltcpHost"].getString());
    if (j.contains("rtltcpPort"))   cfg.setRtltcpPort((int)j["rtltcpPort"].getInt(1234));
    if (j["deviceType"].getString() == "rtltcp") {
        std::string host = j.contains("rtltcpHost") ? j["rtltcpHost"].getString() : cfg.rtltcpHost();
        int port = j.contains("rtltcpPort") ? (int)j["rtltcpPort"].getInt() : cfg.rtltcpPort();
        if (!host.empty()) cfg.setLastSerial(host + ":" + std::to_string(port));
    }
    if (j.contains("sampleRate"))    cfg.setSampleRate((uint32_t)j["sampleRate"].getInt(2048000));
    if (j.contains("gainTenths"))    cfg.setGain((int)j["gainTenths"].getInt(280));
    if (j.contains("agc"))           cfg.setAgc(j["agc"].getBool());
    if (j.contains("biasT"))         cfg.setBiasT(j["biasT"].getBool());
    if (j.contains("quadrature"))    cfg.setQuadrature(j["quadrature"].getBool());
    if (j.contains("ppm"))           cfg.setPpm((int)j["ppm"].getInt());
    if (j.contains("iqCorrection"))  cfg.setIqCorrection(j["iqCorrection"].getBool());
    if (j.contains("sdrplayIfMode")) cfg.setSdrplayIfMode((int)j["sdrplayIfMode"].getInt(0));
    if (j.contains("sdrplayLnaState")) cfg.setSdrplayLnaState((int)j["sdrplayLnaState"].getInt(9));
    if (j.contains("sdrplayIfGain")) cfg.setSdrplayIfGain((int)j["sdrplayIfGain"].getInt(59));
    if (j.contains("sdrplayIfAgc"))  cfg.setSdrplayIfAgc(j["sdrplayIfAgc"].getBool());
    if (j.contains("sdrplayBw"))     cfg.setSdrplayBw((int)j["sdrplayBw"].getInt(-1));
    if (j.contains("smeter")) {
        const Json& sm = j["smeter"];
        if (sm.contains("hfOffset"))   cfg.setSmeterHfOffset(sm["hfOffset"].getDouble());
        if (sm.contains("vhfOffset"))  cfg.setSmeterVhfOffset(sm["vhfOffset"].getDouble());
        if (sm.contains("s9Hf"))       cfg.setSmeterS9Hf((int)sm["s9Hf"].getInt());
        if (sm.contains("s9Vhf"))      cfg.setSmeterS9Vhf((int)sm["s9Vhf"].getInt());
        if (sm.contains("hfEmpty"))    cfg.setSmeterHfEmpty((int)sm["hfEmpty"].getInt());
        if (sm.contains("vhfEmpty"))   cfg.setSmeterVhfEmpty((int)sm["vhfEmpty"].getInt());
        if (sm.contains("rmsAligned")) cfg.setSmeterRmsAligned(sm["rmsAligned"].getBool());
    }

    if (newSerial.empty() && newType == oldType && device_) newSerial = oldSerial;

    bool qChanged  = j.contains("quadrature") && (j["quadrature"].getBool() != oldQ);
    bool srChanged = j.contains("sampleRate") && ((int)j["sampleRate"].getInt() != oldSR);
    bool btChanged = j.contains("biasT")      && (j["biasT"].getBool() != oldBT);
    bool devChanged = (newType != oldType || newSerial != oldSerial || qChanged || srChanged || btChanged || !device_);

    if (devChanged && !newType.empty() && rest_->onSelectDevice)
        rest_->onSelectDevice(newType, newSerial);
    else
        applyConfigToDevice();

    cfg.sync();
    return true;
}

void Application::wireDeviceCallback() {
    if (!device_) return;
    device_->setCallback([this](const std::complex<float>* iq, size_t n) {
        if (!iq || n == 0 || !fft_) return;

        using namespace std::chrono;
        lastIqMs_.store(duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count());

        const uint64_t vfoHz = freqA_.load(std::memory_order_relaxed);
        const auto& cfgNow = Config::instance();
        const int gainTenths = cfgNow.gainTenths();
        const float relDb = std::clamp((float(gainTenths) - 280.0f) / 10.0f, -24.0f, 36.0f);
        float uiGain = std::pow(10.0f, relDb / 20.0f);
        if (deviceType_ == "sdrplay" || cfgNow.agc()) uiGain = 1.0f;

        std::vector<std::complex<float>> work(iq, iq + n);

        if (deviceType_ != "sdrplay") {
            for (size_t i = 0; i < n; ++i) work[i] = dcBlock_.process(work[i]);
        }
        if (uiGain != 1.0f) {
            for (size_t i = 0; i < n; ++i) work[i] *= uiGain;
        }

        const std::complex<float>* src = work.data();

        float maxPower = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            float p = src[i].real()*src[i].real() + src[i].imag()*src[i].imag();
            if (p > maxPower) maxPower = p;
        }
        peakDb_.store(10.f * std::log10(maxPower + 1e-12f));

        // Acumula a potencia de TODOS os blocos (LogAveragePower do OpenWebRX+)
        // e envia a MEDIA a cada 40 ms, em vez de um espectro instantaneo.
        fft_->accumulate(src, n);

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFftTime_).count() >= 40) {
            lastFftTime_ = now;
            const auto bins = fft_->hasAccumulated() ? fft_->takeAverageDbfs()
                                                     : fft_->computeDbfs(src, n);
            if (ws_) ws_->broadcastFftThreadSafe(bins, device_->centerFreq(), device_->sampleRate());
        }

        // Software VFO / DDC
        std::vector<std::complex<float>> demodWork;
        const std::complex<float>* demodSrc = src;
        if (device_) {
            const int64_t fOffset = (int64_t)vfoHz - (int64_t)device_->centerFreq();
            if (fOffset != 0) {
                demodWork.resize(n);
                const double fs = device_->sampleRate();
                const double phase_step = -2.0 * 3.14159265358979323846 * double(fOffset) / fs;
                for (size_t i = 0; i < n; ++i) {
                    phaseAcc_ += phase_step;
                    if (phaseAcc_ > 3.14159265358979323846) phaseAcc_ -= 2.0*3.14159265358979323846;
                    else if (phaseAcc_ < -3.14159265358979323846) phaseAcc_ += 2.0*3.14159265358979323846;
                    const float cosV = std::cos((float)phaseAcc_);
                    const float sinV = std::sin((float)phaseAcc_);
                    demodWork[i] = {
                        src[i].real()*cosV - src[i].imag()*sinV,
                        src[i].real()*sinV + src[i].imag()*cosV
                    };
                }
                demodSrc = demodWork.data();
            }
        }

        {
            std::lock_guard<std::mutex> lk(demodMutex_);
            if (demod_) demod_->process(demodSrc, n, device_->sampleRate());
        }
    });
}

void Application::handleAudioCallback(const std::vector<int16_t>& pcm, uint32_t sps) {
    if (pcm.empty()) return;
    std::lock_guard<std::mutex> lk(audioBufferMutex_);
    audioBuffer_.insert(audioBuffer_.end(), pcm.begin(), pcm.end());
    while (audioBuffer_.size() >= 2048) {
        std::vector<int16_t> chunk(audioBuffer_.begin(), audioBuffer_.begin() + 2048);
        audioBuffer_.erase(audioBuffer_.begin(), audioBuffer_.begin() + 2048);
        if (ws_) ws_->broadcastAudioThreadSafe(chunk, sps);
    }
}

} // namespace masdr
