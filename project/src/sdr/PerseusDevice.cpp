#include "PerseusDevice.h"
#include "../util/Logger.h"
#include <windows.h>
#include <complex>
#include <vector>

namespace masdr {

// Loader dinâmico da DLL perseus-sdr.dll
struct PerseusApiLoader {
    HMODULE dll = nullptr;

    typedef int (*perseus_init_t)();
    typedef void (*perseus_exit_t)();
    typedef void* (*perseus_open_t)(int idx);
    typedef int (*perseus_close_t)(void* descr);
    typedef int (*perseus_firmware_download_t)(void* descr, const char* fname);
    typedef int (*perseus_set_ddc_center_freq_t)(void* descr, double hz, int enablePresel);
    typedef int (*perseus_set_sampling_rate_t)(void* descr, int rate);
    typedef int (*perseus_set_attenuator_in_db_t)(void* descr, int att);
    typedef int (*perseus_start_async_input_t)(void* descr, uint32_t bufsize, int (*cb)(void*, int, void*), void* extra);
    typedef int (*perseus_stop_async_input_t)(void* descr);
    typedef const char* (*perseus_errorstr_t)(int errCode);

    perseus_init_t init = nullptr;
    perseus_exit_t exit_fn = nullptr;
    perseus_open_t open_fn = nullptr;
    perseus_close_t close_fn = nullptr;
    perseus_firmware_download_t firmware_download = nullptr;
    perseus_set_ddc_center_freq_t set_ddc_center_freq = nullptr;
    perseus_set_sampling_rate_t set_sampling_rate = nullptr;
    perseus_set_attenuator_in_db_t set_attenuator_in_db = nullptr;
    perseus_start_async_input_t start_async_input = nullptr;
    perseus_stop_async_input_t stop_async_input = nullptr;
    perseus_errorstr_t errorstr = nullptr;

    bool load() {
        if (dll) return true;

        static const char* s_paths[] = {
            "perseus-sdr.dll",
            "C:\\RXSDR\\sdrpp_windows_x64\\perseus-sdr.dll",
            nullptr
        };

        for (int i = 0; s_paths[i]; ++i) {
            dll = ::LoadLibraryA(s_paths[i]);
            if (dll) {
                Logger::info(QString("Perseus: DLL carregada de %1").arg(s_paths[i]));
                break;
            }
        }

        if (!dll) {
            Logger::warn("Perseus: perseus-sdr.dll nao encontrada.");
            return false;
        }

        auto loadSym = [&](const char* name, void** ptr) -> bool {
            *ptr = reinterpret_cast<void*>(::GetProcAddress(dll, name));
            if (!*ptr) {
                Logger::error(QString("Perseus: simbolo ausente na DLL: %1").arg(name));
                unload();
                return false;
            }
            return true;
        };

        if (!loadSym("perseus_init", reinterpret_cast<void**>(&init))) return false;
        if (!loadSym("perseus_exit", reinterpret_cast<void**>(&exit_fn))) return false;
        if (!loadSym("perseus_open", reinterpret_cast<void**>(&open_fn))) return false;
        if (!loadSym("perseus_close", reinterpret_cast<void**>(&close_fn))) return false;
        if (!loadSym("perseus_firmware_download", reinterpret_cast<void**>(&firmware_download))) return false;
        if (!loadSym("perseus_set_ddc_center_freq", reinterpret_cast<void**>(&set_ddc_center_freq))) return false;
        if (!loadSym("perseus_set_sampling_rate", reinterpret_cast<void**>(&set_sampling_rate))) return false;
        if (!loadSym("perseus_set_attenuator_in_db", reinterpret_cast<void**>(&set_attenuator_in_db))) return false;
        if (!loadSym("perseus_start_async_input", reinterpret_cast<void**>(&start_async_input))) return false;
        if (!loadSym("perseus_stop_async_input", reinterpret_cast<void**>(&stop_async_input))) return false;
        loadSym("perseus_errorstr", reinterpret_cast<void**>(&errorstr));

        if (init) {
            init();
        }

        return true;
    }

    void unload() {
        if (exit_fn && dll) {
            exit_fn();
        }
        if (dll) {
            ::FreeLibrary(dll);
            dll = nullptr;
        }
        init = nullptr;
        exit_fn = nullptr;
        open_fn = nullptr;
        close_fn = nullptr;
        firmware_download = nullptr;
        set_ddc_center_freq = nullptr;
        set_sampling_rate = nullptr;
        set_attenuator_in_db = nullptr;
        start_async_input = nullptr;
        stop_async_input = nullptr;
        errorstr = nullptr;
    }

    bool isLoaded() const { return dll != nullptr; }
};

static PerseusApiLoader s_api;

static int PERSEUS_streamCb(void* buf, int buf_size, void* extra) {
    auto* dev = static_cast<masdr::PerseusDevice*>(extra);
    if (!dev || !dev->isRunning() || buf_size <= 0) return 0;

    const uint8_t* p = static_cast<const uint8_t*>(buf);
    int numSamples = buf_size / 6; // 6 bytes por amostra IQ (3 bytes I, 3 bytes Q)
    if (numSamples <= 0) return 0;

    std::vector<std::complex<float>> out(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        // Amostra I (24-bit signed)
        int32_t raw_i = p[0] | (p[1] << 8) | (p[2] << 16);
        if (raw_i & 0x800000) {
            raw_i |= 0xFF000000;
        }
        // Amostra Q (24-bit signed)
        int32_t raw_q = p[3] | (p[4] << 8) | (p[5] << 16);
        if (raw_q & 0x800000) {
            raw_q |= 0xFF000000;
        }

        // Converter para float normalizado [-1.0f, 1.0f]
        float fi = static_cast<float>(raw_i) / 8388607.0f;
        float fq = static_cast<float>(raw_q) / 8388607.0f;
        out[i] = { fi, fq };

        p += 6;
    }

    dev->deliverSamples(out.data(), out.size());
    return 0;
}

PerseusDevice::PerseusDevice() = default;

PerseusDevice::~PerseusDevice() {
    close();
}

std::vector<PerseusInfo> PerseusDevice::enumerate() {
    std::vector<PerseusInfo> out;
    if (!s_api.load()) return out;

    // Tenta abrir o índice 0 para verificar se há algum receptor conectado
    void* tempDev = s_api.open_fn(0);
    if (tempDev) {
        PerseusInfo info;
        info.serial = "0000000001";
        info.name = "Perseus SDR";
        out.push_back(info);
        s_api.close_fn(tempDev);
    }
    return out;
}

bool PerseusDevice::open(const QString& serial) {
    if (!s_api.load()) return false;

    dev_ = s_api.open_fn(0);
    if (!dev_) {
        Logger::error("Perseus: Nao foi possivel abrir o dispositivo. Verifique a conexao USB.");
        return false;
    }

    // Baixa o firmware padrão
    int err = s_api.firmware_download(dev_, nullptr);
    if (err < 0) {
        Logger::error(QString("Perseus: Falha no download de firmware. Codigo: %1").arg(err));
        s_api.close_fn(dev_);
        dev_ = nullptr;
        return false;
    }

    serial_ = serial.isEmpty() ? "0000000001" : serial;

    // Aplica configurações iniciais
    setCenterFreq(freq_);
    setSampleRate(sps_);
    setGain(gainTenths_);

    Logger::info(QString("Perseus: Dispositivo aberto com sucesso (s/n: %1)").arg(serial_));
    return true;
}

void PerseusDevice::close() {
    stop();
    if (dev_) {
        s_api.close_fn(dev_);
        dev_ = nullptr;
        Logger::info("Perseus: Dispositivo fechado.");
    }
}

void PerseusDevice::start() {
    if (!dev_ || running_.load()) return;

    running_.store(true);
    // Buffer padrão de 64 KB (aproximadamente 10k amostras IQ)
    int err = s_api.start_async_input(dev_, 65536, PERSEUS_streamCb, this);
    if (err < 0) {
        Logger::error(QString("Perseus: Falha ao iniciar streaming asincrono (codigo %1)").arg(err));
        running_.store(false);
    } else {
        Logger::info("Perseus: Streaming iniciado.");
    }
}

void PerseusDevice::stop() {
    if (running_.load()) {
        running_.store(false);
        if (dev_) {
            s_api.stop_async_input(dev_);
        }
        Logger::info("Perseus: Streaming parado.");
    }
}

void PerseusDevice::setCenterFreq(uint64_t hz) {
    freq_ = hz;
    if (dev_) {
        // Presel = 1 (filtro pré-seletor analógico ativado)
        s_api.set_ddc_center_freq(dev_, static_cast<double>(hz), 1);
    }
}

void PerseusDevice::setSampleRate(uint32_t sps) {
    // Escolhe a taxa mais próxima das suportadas
    uint32_t rate = 1000000;
    if (sps <= 125000) rate = 125000;
    else if (sps <= 250000) rate = 250000;
    else if (sps <= 500000) rate = 500000;
    else if (sps <= 1000000) rate = 1000000;
    else rate = 2000000;

    sps_ = rate;

    if (dev_) {
        s_api.set_sampling_rate(dev_, static_cast<int>(rate));
    }
}

void PerseusDevice::setGain(int tenthsDb) {
    gainTenths_ = tenthsDb;
    if (dev_) {
        // Mapeia gainTenths para atenuação do Perseus (0, 10, 20, 30 dB)
        int db = tenthsDb / 10;
        int att = 0;
        if (db < 10) att = 30;      // Baixo ganho (atenuação máxima)
        else if (db < 20) att = 20;
        else if (db < 30) att = 10;
        else att = 0;               // Alto ganho (atenuação zero)

        s_api.set_attenuator_in_db(dev_, att);
        Logger::info(QString("Perseus: ganho ajustado. Atenuacao setada em %1 dB").arg(att));
    }
}

} // namespace masdr
