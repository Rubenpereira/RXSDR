#include "RtlSdrDevice.h"
#include "../util/Logger.h"
#include <vector>
#include <cmath>
#include <string>

#if RTLSDR_AVAILABLE
  #include <rtl-sdr.h>
#endif

namespace {
inline void applyAgcMode(void* dev, int& cached, int desired) {
#if RTLSDR_AVAILABLE
    if (!dev || cached == desired) return;
    cached = desired;
    rtlsdr_set_agc_mode(static_cast<rtlsdr_dev_t*>(dev), desired);
#else
    (void)dev; (void)cached; (void)desired;
#endif
}
}

namespace masdr {

RtlSdrDevice::RtlSdrDevice()  = default;
RtlSdrDevice::~RtlSdrDevice() { close(); }

std::vector<DeviceInfo> RtlSdrDevice::enumerate() {
    std::vector<DeviceInfo> out;
#if RTLSDR_AVAILABLE
    uint32_t n = rtlsdr_get_device_count();
    for (uint32_t i = 0; i < n; ++i) {
        char manu[256]{}, prod[256]{}, ser[256]{};
        rtlsdr_get_device_usb_strings(i, manu, prod, ser);
        DeviceInfo d;
        d.serial = ser;
        d.name   = prod;
        // trim trailing spaces
        while (!d.serial.empty() && d.serial.back() == ' ') d.serial.pop_back();
        while (!d.name.empty()   && d.name.back()   == ' ') d.name.pop_back();
        if (d.serial.empty() && d.name.empty()) continue;
        out.push_back(d);
    }
#endif
    return out;
}

bool RtlSdrDevice::open(const std::string& serial) {
#if RTLSDR_AVAILABLE
    lastError_.clear();
    deviceOk_ = true;

    uint32_t count = rtlsdr_get_device_count();
    if (count == 0) {
        lastError_ = "Nenhum dispositivo RTL-SDR encontrado via USB.";
        Logger::error(lastError_);
        deviceOk_ = false;
        return false;
    }

    Logger::info("RTL-SDR: " + std::to_string(count) + " dispositivo(s) encontrado(s)");

    uint32_t idx = 0;
    if (!serial.empty()) {
        int found = rtlsdr_get_index_by_serial(serial.c_str());
        if (found >= 0) {
            idx = (uint32_t)found;
        } else {
            Logger::warn("RTL-SDR: serial '" + serial + "' nao encontrado. Usando indice 0.");
            idx = 0;
        }
    }

    if (rtlsdr_open((rtlsdr_dev_t**)&dev_, idx) < 0) {
        lastError_ = "rtlsdr_open falhou para indice " + std::to_string(idx);
        Logger::error(lastError_);
        deviceOk_ = false;
        return false;
    }

    char realSerial[256]{};
    rtlsdr_get_usb_strings((rtlsdr_dev_t*)dev_, nullptr, nullptr, realSerial);
    serial_ = realSerial;

    if (rtlsdr_set_sample_rate((rtlsdr_dev_t*)dev_, sps_) < 0)
        Logger::warn("RTL-SDR: falha ao definir sample rate");
    if (rtlsdr_set_freq_correction((rtlsdr_dev_t*)dev_, ppm_) < 0)
        Logger::warn("RTL-SDR: falha ao definir PPM");
    if (rtlsdr_set_direct_sampling((rtlsdr_dev_t*)dev_, quadrature_ ? 2 : 0) < 0)
        Logger::warn("RTL-SDR: falha ao definir direct sampling");

    uint32_t targetHz = (uint32_t)freq_;
    if (!quadrature_ && targetHz < 24000000) targetHz = 24000000;
    if (rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, targetHz) < 0)
        Logger::warn("RTL-SDR: falha ao definir frequencia inicial");

    if (quadrature_) {
        agcMode_ = -1;
        applyAgcMode(dev_, agcMode_, 1);
    } else {
        rtlsdr_set_tuner_gain_mode((rtlsdr_dev_t*)dev_, gainTenths_ < 0 ? 0 : 1);
        if (gainTenths_ >= 0)
            rtlsdr_set_tuner_gain((rtlsdr_dev_t*)dev_, gainTenths_);
        agcMode_ = -1;
        applyAgcMode(dev_, agcMode_, gainTenths_ < 0 ? 1 : 0);
    }
    if (bias_) rtlsdr_set_bias_tee((rtlsdr_dev_t*)dev_, 1);

    Logger::info("RTL-SDR: aberto (serial='" + serial_ + "', idx=" + std::to_string(idx) + ")");
    return true;
#else
    lastError_ = "Suporte RTL-SDR nao compilado (RTLSDR_AVAILABLE=0).";
    Logger::warn(lastError_);
    return false;
#endif
}

void RtlSdrDevice::close() {
    stop();
#if RTLSDR_AVAILABLE
    if (dev_) { rtlsdr_close((rtlsdr_dev_t*)dev_); dev_ = nullptr; }
#endif
    deviceOk_ = false;
}

void RtlSdrDevice::setCenterFreq(uint64_t hz) {
    freq_ = hz;
#if RTLSDR_AVAILABLE
    if (dev_) {
        uint32_t t = (uint32_t)hz;
        if (!quadrature_ && t < 24000000) t = 24000000;
        if (rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, t) < 0)
            Logger::warn("RTL-SDR: falha ao definir frequencia");
    }
#endif
}

void RtlSdrDevice::setSampleRate(uint32_t sps) {
    sps_ = sps;
#if RTLSDR_AVAILABLE
    if (dev_ && rtlsdr_set_sample_rate((rtlsdr_dev_t*)dev_, sps) < 0)
        Logger::warn("RTL-SDR: falha ao definir sample rate");
#endif
}

void RtlSdrDevice::setGain(int tenthsDb) {
    gainTenths_ = tenthsDb;
#if RTLSDR_AVAILABLE
    if (!dev_) return;
    if (quadrature_) { applyAgcMode(dev_, agcMode_, 1); return; }
    if (tenthsDb < 0) {
        rtlsdr_set_tuner_gain_mode((rtlsdr_dev_t*)dev_, 0);
        applyAgcMode(dev_, agcMode_, 1);
    } else {
        rtlsdr_set_tuner_gain_mode((rtlsdr_dev_t*)dev_, 1);
        rtlsdr_set_tuner_gain((rtlsdr_dev_t*)dev_, tenthsDb);
        applyAgcMode(dev_, agcMode_, 0);
    }
#endif
}

void RtlSdrDevice::setBias(bool on) {
    bias_ = on;
#if RTLSDR_AVAILABLE
    if (dev_) rtlsdr_set_bias_tee((rtlsdr_dev_t*)dev_, on ? 1 : 0);
#endif
}

void RtlSdrDevice::setQuadrature(bool on) {
    if (on == quadrature_) return;
    quadrature_ = on;
#if RTLSDR_AVAILABLE
    if (dev_) {
        if (on) {
            rtlsdr_set_direct_sampling((rtlsdr_dev_t*)dev_, 2);
            uint32_t t = (uint32_t)freq_;
            rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, t);
        } else {
            uint32_t t = (uint32_t)freq_;
            if (t < 24000000) t = 24000000;
            rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, t);
            rtlsdr_set_direct_sampling((rtlsdr_dev_t*)dev_, 0);
            rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, t);
        }
        applyAgcMode(dev_, agcMode_, on ? 1 : (gainTenths_ < 0 ? 1 : 0));
    }
#endif
}

void RtlSdrDevice::setPpm(int ppm) {
    ppm_ = ppm;
#if RTLSDR_AVAILABLE
    if (dev_ && rtlsdr_set_freq_correction((rtlsdr_dev_t*)dev_, ppm) < 0)
        Logger::warn("RTL-SDR: falha ao definir PPM");
#endif
}

void RtlSdrDevice::start() {
#if RTLSDR_AVAILABLE
    if (!dev_ || !deviceOk_ || running_) return;
    Logger::info("RTL-SDR: iniciando stream...");
    rtlsdr_reset_buffer((rtlsdr_dev_t*)dev_);
    running_ = true;
    thread_ = std::thread([this]{ readLoop(); });
    Logger::info("RTL-SDR: thread lancada");
#endif
}

void RtlSdrDevice::stop() {
    if (!running_) return;
    Logger::info("RTL-SDR: parando stream...");
    running_ = false;
#if RTLSDR_AVAILABLE
    if (dev_) rtlsdr_cancel_async((rtlsdr_dev_t*)dev_);
#endif
    if (thread_.joinable()) thread_.join();
    Logger::info("RTL-SDR: stream parado");
}

#if RTLSDR_AVAILABLE
static void rtlsdr_callback(unsigned char* buf, uint32_t len, void* ctx) {
    auto* self = static_cast<RtlSdrDevice*>(ctx);
    if (!self) return;
    static thread_local std::vector<std::complex<float>> iq;
    size_t n = len / 2;
    if (iq.size() < n) iq.resize(n);
    for (size_t i = 0; i < n; ++i) {
        float I = ((float)buf[2*i]   - 127.5f) / 127.5f;
        float Q = ((float)buf[2*i+1] - 127.5f) / 127.5f;
        iq[i] = { I, Q };
    }
    auto cb = self->callback();
    if (cb) cb(iq.data(), n);
}
#endif

void RtlSdrDevice::readLoop() {
#if RTLSDR_AVAILABLE
    Logger::info("RTL-SDR: read_async iniciado");
    rtlsdr_read_async((rtlsdr_dev_t*)dev_, &rtlsdr_callback, this, 0, 16384*2);
    Logger::info("RTL-SDR: read_async encerrou");
#endif
}

} // namespace masdr
