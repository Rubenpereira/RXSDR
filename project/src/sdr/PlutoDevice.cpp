#include "PlutoDevice.h"
#include "../util/Logger.h"
#include <windows.h>
#include <complex>
#include <vector>

namespace masdr {

// Loader dinâmico da DLL libiio.dll
struct IioApiLoader {
    HMODULE dll = nullptr;

    typedef void* (*iio_create_context_from_uri_t)(const char* uri);
    typedef void* (*iio_create_default_context_t)();
    typedef void (*iio_context_destroy_t)(void* ctx);
    typedef void* (*iio_context_find_device_t)(void* ctx, const char* name);
    typedef void* (*iio_device_find_channel_t)(void* dev, const char* name, bool output);
    typedef int (*iio_channel_enable_t)(void* chn);
    typedef int (*iio_channel_disable_t)(void* chn);
    typedef void* (*iio_device_create_buffer_t)(void* dev, size_t samples_count, bool cyclic);
    typedef void (*iio_buffer_destroy_t)(void* buf);
    typedef void (*iio_buffer_cancel_t)(void* buf);
    typedef ptrdiff_t (*iio_buffer_refill_t)(void* buf);
    typedef void* (*iio_buffer_start_t)(void* buf);
    typedef ptrdiff_t (*iio_buffer_step_t)(void* buf);
    typedef int (*iio_channel_attr_write_longlong_t)(void* chn, const char* attr, long long val);
    typedef int (*iio_channel_attr_write_t)(void* chn, const char* attr, const char* val);
    typedef int (*iio_channel_attr_write_bool_t)(void* chn, const char* attr, bool val);

    iio_create_context_from_uri_t create_context_from_uri = nullptr;
    iio_create_default_context_t create_default_context = nullptr;
    iio_context_destroy_t context_destroy = nullptr;
    iio_context_find_device_t context_find_device = nullptr;
    iio_device_find_channel_t device_find_channel = nullptr;
    iio_channel_enable_t channel_enable = nullptr;
    iio_channel_disable_t channel_disable = nullptr;
    iio_device_create_buffer_t device_create_buffer = nullptr;
    iio_buffer_destroy_t buffer_destroy = nullptr;
    iio_buffer_cancel_t buffer_cancel = nullptr;
    iio_buffer_refill_t buffer_refill = nullptr;
    iio_buffer_start_t buffer_start = nullptr;
    iio_buffer_step_t buffer_step = nullptr;
    iio_channel_attr_write_longlong_t channel_attr_write_longlong = nullptr;
    iio_channel_attr_write_t channel_attr_write = nullptr;
    iio_channel_attr_write_bool_t channel_attr_write_bool = nullptr;

    bool load() {
        if (dll) return true;

        static const char* s_paths[] = {
            "libiio.dll",
            "C:\\RXSDR\\sdrpp_windows_x64\\libiio.dll",
            nullptr
        };

        for (int i = 0; s_paths[i]; ++i) {
            dll = ::LoadLibraryA(s_paths[i]);
            if (dll) {
                Logger::info(QString("PlutoSDR: libiio.dll carregada de %1").arg(s_paths[i]));
                break;
            }
        }

        if (!dll) {
            Logger::warn("PlutoSDR: libiio.dll nao encontrada.");
            return false;
        }

        auto loadSym = [&](const char* name, void** ptr) -> bool {
            *ptr = reinterpret_cast<void*>(::GetProcAddress(dll, name));
            if (!*ptr) {
                Logger::error(QString("PlutoSDR: simbolo ausente na DLL: %1").arg(name));
                unload();
                return false;
            }
            return true;
        };

        if (!loadSym("iio_create_context_from_uri", reinterpret_cast<void**>(&create_context_from_uri))) return false;
        if (!loadSym("iio_create_default_context", reinterpret_cast<void**>(&create_default_context))) return false;
        if (!loadSym("iio_context_destroy", reinterpret_cast<void**>(&context_destroy))) return false;
        if (!loadSym("iio_context_find_device", reinterpret_cast<void**>(&context_find_device))) return false;
        if (!loadSym("iio_device_find_channel", reinterpret_cast<void**>(&device_find_channel))) return false;
        if (!loadSym("iio_channel_enable", reinterpret_cast<void**>(&channel_enable))) return false;
        if (!loadSym("iio_channel_disable", reinterpret_cast<void**>(&channel_disable))) return false;
        if (!loadSym("iio_device_create_buffer", reinterpret_cast<void**>(&device_create_buffer))) return false;
        if (!loadSym("iio_buffer_destroy", reinterpret_cast<void**>(&buffer_destroy))) return false;
        if (!loadSym("iio_buffer_cancel", reinterpret_cast<void**>(&buffer_cancel))) return false;
        if (!loadSym("iio_buffer_refill", reinterpret_cast<void**>(&buffer_refill))) return false;
        if (!loadSym("iio_buffer_start", reinterpret_cast<void**>(&buffer_start))) return false;
        if (!loadSym("iio_buffer_step", reinterpret_cast<void**>(&buffer_step))) return false;
        if (!loadSym("iio_channel_attr_write_longlong", reinterpret_cast<void**>(&channel_attr_write_longlong))) return false;
        if (!loadSym("iio_channel_attr_write", reinterpret_cast<void**>(&channel_attr_write))) return false;
        if (!loadSym("iio_channel_attr_write_bool", reinterpret_cast<void**>(&channel_attr_write_bool))) return false;

        return true;
    }

    void unload() {
        if (dll) {
            ::FreeLibrary(dll);
            dll = nullptr;
        }
        create_context_from_uri = nullptr;
        create_default_context = nullptr;
        context_destroy = nullptr;
        context_find_device = nullptr;
        device_find_channel = nullptr;
        channel_enable = nullptr;
        channel_disable = nullptr;
        device_create_buffer = nullptr;
        buffer_destroy = nullptr;
        buffer_cancel = nullptr;
        buffer_refill = nullptr;
        buffer_start = nullptr;
        buffer_step = nullptr;
        channel_attr_write_longlong = nullptr;
        channel_attr_write = nullptr;
        channel_attr_write_bool = nullptr;
    }

    bool isLoaded() const { return dll != nullptr; }
};

static IioApiLoader s_api;

PlutoDevice::PlutoDevice() = default;

PlutoDevice::~PlutoDevice() {
    close();
}

std::vector<PlutoInfo> PlutoDevice::enumerate() {
    std::vector<PlutoInfo> out;
    if (!s_api.load()) return out;

    // Tentamos fazer um scan rápido de PlutoSDR na rede ou local
    // Caso padrão: se o libiio estiver disponível, listamos o endpoint padrão.
    // Isso evita blocos de timeouts excessivos no scan inicial.
    PlutoInfo info;
    info.serial = "ip:192.168.2.1";
    info.name = "PlutoSDR (USB IP)";
    out.push_back(info);
    return out;
}

bool PlutoDevice::open(const QString& serial) {
    if (!s_api.load()) return false;

    QString uri = serial;
    if (uri.isEmpty()) {
        uri = "ip:192.168.2.1";
    }

    ctx_ = s_api.create_context_from_uri(uri.toLatin1().constData());
    if (!ctx_) {
        ctx_ = s_api.create_default_context();
    }

    if (!ctx_) {
        Logger::error(QString("PlutoSDR: Nao foi possivel criar contexto para %1").arg(uri));
        return false;
    }

    phyDev_ = s_api.context_find_device(ctx_, "ad9361-phy");
    rxDev_  = s_api.context_find_device(ctx_, "cf-ad9361-lpc");

    if (!phyDev_ || !rxDev_) {
        Logger::error("PlutoSDR: Dispositivo PHY ad9361-phy ou DMA cf-ad9361-lpc nao encontrados.");
        s_api.context_destroy(ctx_);
        ctx_ = nullptr;
        phyDev_ = nullptr;
        rxDev_ = nullptr;
        return false;
    }

    // Liga o RX local oscillator (powerdown = false)
    void* loChn = s_api.device_find_channel(phyDev_, "altvoltage0", true);
    if (loChn) {
        s_api.channel_attr_write_bool(loChn, "powerdown", false);
    }

    // Habilita canais I/Q para o buffer
    void* chn_i = s_api.device_find_channel(rxDev_, "voltage0", false);
    void* chn_q = s_api.device_find_channel(rxDev_, "voltage1", false);
    if (!chn_i || !chn_q) {
        Logger::error("PlutoSDR: Canais de entrada voltage0 ou voltage1 nao encontrados.");
        s_api.context_destroy(ctx_);
        ctx_ = nullptr;
        phyDev_ = nullptr;
        rxDev_ = nullptr;
        return false;
    }
    s_api.channel_enable(chn_i);
    s_api.channel_enable(chn_q);

    serial_ = uri;

    // Configuração inicial
    setCenterFreq(freq_);
    setSampleRate(sps_);
    setGain(gainTenths_);

    Logger::info(QString("PlutoSDR: Aberto com sucesso em %1").arg(serial_));
    return true;
}

void PlutoDevice::close() {
    stop();
    if (ctx_) {
        s_api.context_destroy(ctx_);
        ctx_ = nullptr;
        phyDev_ = nullptr;
        rxDev_ = nullptr;
        Logger::info("PlutoSDR: Fechado.");
    }
}

void PlutoDevice::start() {
    if (!ctx_ || running_.load()) return;

    // Cria o buffer IIO (16k amostras IQ é o padrão recomendado)
    rxBuf_ = s_api.device_create_buffer(rxDev_, 16384, false);
    if (!rxBuf_) {
        Logger::error("PlutoSDR: Nao foi possivel criar buffer IIO para streaming.");
        return;
    }

    running_.store(true);
    thread_ = new StreamThread(this);
    thread_->start(QThread::HighPriority);
    Logger::info("PlutoSDR: Streaming iniciado.");
}

void PlutoDevice::stop() {
    if (running_.load()) {
        running_.store(false);
        if (rxBuf_) {
            s_api.buffer_cancel(rxBuf_);
        }
        if (thread_) {
            thread_->wait();
            delete thread_;
            thread_ = nullptr;
        }
        if (rxBuf_) {
            s_api.buffer_destroy(rxBuf_);
            rxBuf_ = nullptr;
        }
        Logger::info("PlutoSDR: Streaming parado.");
    }
}

void PlutoDevice::setCenterFreq(uint64_t hz) {
    freq_ = hz;
    if (phyDev_) {
        void* loChn = s_api.device_find_channel(phyDev_, "altvoltage0", true);
        if (loChn) {
            s_api.channel_attr_write_longlong(loChn, "frequency", static_cast<long long>(hz));
        }
    }
}

void PlutoDevice::setSampleRate(uint32_t sps) {
    sps_ = sps;
    if (phyDev_) {
        void* rxChn = s_api.device_find_channel(phyDev_, "voltage0", false);
        if (rxChn) {
            s_api.channel_attr_write_longlong(rxChn, "sampling_frequency", static_cast<long long>(sps));
        }
    }
}

void PlutoDevice::setGain(int tenthsDb) {
    gainTenths_ = tenthsDb;
    if (phyDev_) {
        void* rxChn = s_api.device_find_channel(phyDev_, "voltage0", false);
        if (rxChn) {
            if (tenthsDb < 0) {
                s_api.channel_attr_write(rxChn, "gain_control_mode", "slow_attack");
            } else {
                s_api.channel_attr_write(rxChn, "gain_control_mode", "manual");
                double db = static_cast<double>(tenthsDb) / 10.0;
                QString dbStr = QString::number(db, 'f', 1);
                s_api.channel_attr_write(rxChn, "hardwaregain", dbStr.toLatin1().constData());
            }
        }
    }
}

void PlutoDevice::runLoop() {
    // Aloca buffers temporários para conversão IQ
    std::vector<std::complex<float>> outBuf;
    outBuf.reserve(16384);

    while (running_.load()) {
        if (!rxBuf_) {
            QThread::msleep(5);
            continue;
        }

        ptrdiff_t bytes = s_api.buffer_refill(rxBuf_);
        if (bytes < 0) {
            if (running_.load()) {
                QThread::msleep(1);
            }
            continue;
        }

        ptrdiff_t step = s_api.buffer_step(rxBuf_);
        if (step <= 0) {
            QThread::msleep(1);
            continue;
        }

        char* start = static_cast<char*>(s_api.buffer_start(rxBuf_));
        int numSamples = static_cast<int>(bytes / step);
        if (numSamples <= 0) {
            QThread::msleep(1);
            continue;
        }

        outBuf.resize(numSamples);
        const int16_t* src = reinterpret_cast<const int16_t*>(start);

        constexpr float kScale = 1.0f / 2048.0f;
        for (int i = 0; i < numSamples; ++i) {
            float fi = static_cast<float>(src[2 * i]) * kScale;
            float fq = static_cast<float>(src[2 * i + 1]) * kScale;
            outBuf[i] = { fi, fq };
        }

        deliverSamples(outBuf.data(), numSamples);
    }
}

} // namespace masdr
