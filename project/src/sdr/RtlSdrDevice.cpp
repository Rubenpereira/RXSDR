#include "RtlSdrDevice.h"
#include "../util/Logger.h"
#include <QThread>
#include <vector>
#include <cmath>

#if RTLSDR_AVAILABLE
  #include <rtl-sdr.h>
#endif

namespace {
// Aplica rtlsdr_set_agc_mode apenas se o valor mudar. Cada chamada USB
// ao dongle durante streaming é cara — evitar chamadas desnecessárias
// previne erros de barramento e desconexão do dispositivo.
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

RtlSdrDevice::RtlSdrDevice() {}
RtlSdrDevice::~RtlSdrDevice() { close(); }

std::vector<DeviceInfo> RtlSdrDevice::enumerate() {
    std::vector<DeviceInfo> out;
#if RTLSDR_AVAILABLE
    uint32_t n = rtlsdr_get_device_count();
    for (uint32_t i = 0; i < n; ++i) {
        char manu[256]{}, prod[256]{}, ser[256]{};
        rtlsdr_get_device_usb_strings(i, manu, prod, ser);
        DeviceInfo d;
        d.serial = QString::fromLatin1(ser).trimmed();
        d.name   = QString::fromLatin1(prod).trimmed();
        if (d.serial.isEmpty() && d.name.isEmpty()) {
            continue;
        }
        out.push_back(d);
    }
#endif
    return out;
}

bool RtlSdrDevice::open(const QString& serial) {
#if RTLSDR_AVAILABLE
    lastError_.clear();
    deviceOk_ = true;

    // Verifica se há algum dispositivo RTL-SDR conectado
    uint32_t count = rtlsdr_get_device_count();
    if (count == 0) {
        lastError_ = "Nenhum dispositivo RTL-SDR encontrado via USB. "
                     "Verifique se o dongle está conectado e se o driver WinUSB "
                     "foi instalado corretamente pelo Zadig.";
        Logger::error(lastError_);
        deviceOk_ = false;
        return false;
    }

    // Loga os seriais reais de todos os dongles para facilitar diagnóstico
    Logger::info(QString("RTL-SDR: %1 dispositivo(s) USB encontrado(s):").arg(count));
    for (uint32_t i = 0; i < count; ++i) {
        char manu[256]{}, prod[256]{}, ser[256]{};
        rtlsdr_get_device_usb_strings(i, manu, prod, ser);
        Logger::info(QString("  [%1] %2 | %3 | serial='%4'")
                     .arg(i).arg(QString::fromLatin1(prod))
                     .arg(QString::fromLatin1(manu))
                     .arg(QString::fromLatin1(ser)));
    }

    // Tenta encontrar pelo serial, se fornecido
    uint32_t idx = 0;
    if (!serial.isEmpty()) {
        // Tenta match exato pelo serial
        int found = rtlsdr_get_index_by_serial(serial.toLatin1().constData());
        if (found >= 0) {
            idx = (uint32_t)found;
            Logger::info(QString("RTL-SDR: serial '%1' encontrado no índice %2").arg(serial).arg(idx));
        } else {
            // Serial não encontrado — usa índice 0 (único dongle na maioria dos casos)
            // Código -3 = serial não encontrado; -2 = nenhum device; -1 = null
            Logger::warn(QString("RTL-SDR: serial '%1' não encontrado (código %2). "
                                 "Abrindo índice 0 (primeiro disponível).").arg(serial).arg(found));
            idx = 0;
        }
    }

    if (rtlsdr_open((rtlsdr_dev_t**)&dev_, idx) < 0) {
        lastError_ = QString("rtlsdr_open falhou para índice %1.\n"
                             "Possíveis causas:\n"
                             "• O dongle já está sendo usado por outro processo "
                             "(feche SDR#, HDSDR ou qualquer outro software SDR)\n"
                             "• O driver WinUSB não está ativo — rode o Zadig novamente\n"
                             "• Desconecte e reconecte o dongle USB").arg(idx);
        Logger::error(lastError_);
        deviceOk_ = false;
        return false;
    }

    // Lê o serial REAL do dongle (pode diferir do serial configurado)
    char realSerial[256]{};
    rtlsdr_get_usb_strings((rtlsdr_dev_t*)dev_, nullptr, nullptr, realSerial);
    serial_ = QString::fromLatin1(realSerial);
    if (!serial_.isEmpty() && serial_ != serial) {
        Logger::info(QString("RTL-SDR: serial real do dongle: '%1'").arg(serial_));
    }

    // Configura o dongle
    if (rtlsdr_set_sample_rate((rtlsdr_dev_t*)dev_, sps_) < 0) {
        lastError_ = "RTL-SDR: falha ao definir taxa de amostragem (sample rate)";
        Logger::error(lastError_);
        deviceOk_ = false;
        close();
        return false;
    }
    if (rtlsdr_set_freq_correction((rtlsdr_dev_t*)dev_, ppm_) < 0) {
        Logger::warn("RTL-SDR: falha ao definir correção PPM (não crítico)");
    }

    // Configura a amostragem direta ANTES de definir a frequência central e o ganho
    // para evitar que o sintonizador (tuner) receba frequências de HF e trave.
    if (rtlsdr_set_direct_sampling((rtlsdr_dev_t*)dev_, quadrature_ ? 2 : 0) < 0) {
        Logger::warn("RTL-SDR: falha ao definir direct sampling (não crítico)");
    }

    // Ajusta a frequência central com clamping de segurança se o modo Q estiver desligado
    uint32_t targetHz = (uint32_t)freq_;
    if (!quadrature_ && targetHz < 24000000) {
        targetHz = 24000000;
        Logger::warn(QString("RTL-SDR open: Frequência %1 Hz abaixo do limite para Q-Off. Clamping para 24 MHz.").arg(freq_));
    }
    if (rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, targetHz) < 0) {
        Logger::warn("RTL-SDR: falha ao definir frequência central inicial (não crítico)");
    }

    // Configura o ganho de forma segura dependendo do modo de amostragem direta
    if (quadrature_) {
        // Q-on (direct sampling): tuner R820T2 bypassado. NÃO mexer no
        // tuner_gain_mode aqui — forçar manual com ganho 0 atenua o sinal
        // residual em algumas placas RTL-SDR (perda de ganho em HF).
        // Comportamento idêntico à versão estável: só ativa o AGC interno
        // do RTL2832U, que faz o ganho do ADC em direct sampling.
        agcMode_ = -1; // força aplicação inicial
        applyAgcMode(dev_, agcMode_, 1);
    } else {
        if (rtlsdr_set_tuner_gain_mode((rtlsdr_dev_t*)dev_, gainTenths_ < 0 ? 0 : 1) < 0) {
            Logger::warn("RTL-SDR: falha ao definir modo de ganho do tuner (não crítico)");
        }
        if (gainTenths_ >= 0) {
            if (rtlsdr_set_tuner_gain((rtlsdr_dev_t*)dev_, gainTenths_) < 0) {
                Logger::warn("RTL-SDR: falha ao definir ganho do tuner (não crítico)");
            }
        }
        agcMode_ = -1; // força aplicação inicial
        applyAgcMode(dev_, agcMode_, gainTenths_ < 0 ? 1 : 0);
    }
    if (bias_) {
        rtlsdr_set_bias_tee((rtlsdr_dev_t*)dev_, 1);
    }
    Logger::info(QString("RTL-SDR: dispositivo aberto com sucesso (serial='%1', idx=%2)").arg(serial_).arg(idx));
    return true;
#else
    lastError_ = "Suporte RTL-SDR USB não foi compilado neste build "
                 "(RTLSDR_AVAILABLE=0). Verifique se o SDK librtlsdr está em "
                 "third_party/librtlsdr e recompile o projeto.";
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
        uint32_t targetHz = (uint32_t)hz;
        if (!quadrature_ && targetHz < 24000000) {
            targetHz = 24000000;
            Logger::warn(QString("RTL-SDR: Frequência %1 Hz abaixo do limite para Q-Off. Clamping para 24 MHz.").arg(hz));
        }
        if (rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, targetHz) < 0) {
            lastError_ = "RTL-SDR: falha ao definir frequência central";
            Logger::warn(lastError_);
        }
    }
#endif
}
void RtlSdrDevice::setSampleRate(uint32_t sps) {
    sps_ = sps;
#if RTLSDR_AVAILABLE
    if (dev_) {
        if (rtlsdr_set_sample_rate((rtlsdr_dev_t*)dev_, sps) < 0) {
            lastError_ = "RTL-SDR: falha ao definir taxa de amostragem";
            Logger::warn(lastError_);
        }
    }
#endif
}
void RtlSdrDevice::setGain(int tenthsDb) {
    gainTenths_ = tenthsDb;
#if RTLSDR_AVAILABLE
    if (!dev_) return;

    if (quadrature_) {
        // Q-on (direct sampling): tuner bypassado. NÃO mexer no tuner_gain_mode —
        // só ativar AGC interno do RTL2832U para preservar o ganho em HF.
        applyAgcMode(dev_, agcMode_, 1);
        return;
    }

    if (tenthsDb < 0) {
        if (rtlsdr_set_tuner_gain_mode((rtlsdr_dev_t*)dev_, 0) < 0) {
            lastError_ = "RTL-SDR: falha ao desativar modo manual de ganho";
            Logger::warn(lastError_);
        }
        applyAgcMode(dev_, agcMode_, 1);
    } else {
        if (rtlsdr_set_tuner_gain_mode((rtlsdr_dev_t*)dev_, 1) < 0) {
            lastError_ = "RTL-SDR: falha ao ativar modo manual de ganho";
            Logger::warn(lastError_);
        }
        if (rtlsdr_set_tuner_gain((rtlsdr_dev_t*)dev_, tenthsDb) < 0) {
            lastError_ = "RTL-SDR: falha ao definir ganho manual";
            Logger::warn(lastError_);
        }
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
            // Ativando Q-on (Direct Sampling)
            // Primeiro ativamos a amostragem direta ANTES de sintonizar frequências de HF
            Logger::info("RTL-SDR: Ativando amostragem direta (Q-on)...");

            // IMPORTANTE: NÃO chamar rtlsdr_set_tuner_gain_mode aqui — forçar o
            // tuner em modo manual quando o tuner já está bypassado pelo direct
            // sampling derruba o ganho residual do sinal em HF em algumas placas.
            // Comportamento idêntico à versão estável (project-win7).

            int ret = rtlsdr_set_direct_sampling((rtlsdr_dev_t*)dev_, 2);
            if (ret < 0) {
                lastError_ = QString("RTL-SDR: rtlsdr_set_direct_sampling(2) falhou com código %1").arg(ret);
                Logger::warn(lastError_);
            }

            uint32_t targetHz = (uint32_t)freq_;
            Logger::info(QString("RTL-SDR: Sintonizando frequência final em Q-on: %1 Hz").arg(targetHz));
            if (rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, targetHz) < 0) {
                lastError_ = "RTL-SDR: falha ao definir frequência central em Q-on";
                Logger::warn(lastError_);
            }
        } else {
            // Desativando Q-on (voltando para Q-off)
            // Primeiro sintonizamos para uma frequência segura (>= 24 MHz) ANTES de desativar a amostragem direta.
            // Isso evita que o sintonizador (tuner) tente sintonizar frequências baixas de HF que causam travamento/desconexão.
            uint32_t targetHz = (uint32_t)freq_;
            if (targetHz < 24000000) {
                targetHz = 24000000;
            }
            Logger::info(QString("RTL-SDR: Sintonizando frequência segura em Q-off: %1 Hz").arg(targetHz));
            if (rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, targetHz) < 0) {
                Logger::warn("RTL-SDR: falha ao definir frequência de segurança antes de desativar Q-on (não crítico)");
            }

            Logger::info("RTL-SDR: Desativando amostragem direta (Q-off)...");
            int ret = rtlsdr_set_direct_sampling((rtlsdr_dev_t*)dev_, 0);
            if (ret < 0) {
                lastError_ = QString("RTL-SDR: rtlsdr_set_direct_sampling(0) falhou com código %1").arg(ret);
                Logger::warn(lastError_);
            }

            // Restaura o modo de ganho do tuner ao voltar para Q-off
            if (rtlsdr_set_tuner_gain_mode((rtlsdr_dev_t*)dev_, gainTenths_ < 0 ? 0 : 1) < 0) {
                Logger::warn("RTL-SDR: falha ao restaurar modo de ganho do tuner ao voltar para Q-off (não crítico)");
            }
            if (gainTenths_ >= 0) {
                if (rtlsdr_set_tuner_gain((rtlsdr_dev_t*)dev_, gainTenths_) < 0) {
                    Logger::warn("RTL-SDR: falha ao restaurar ganho manual do tuner ao voltar para Q-off (não crítico)");
                }
            }

            // Repete o tune final apenas para garantir consistência
            if (rtlsdr_set_center_freq((rtlsdr_dev_t*)dev_, targetHz) < 0) {
                lastError_ = "RTL-SDR: falha ao reconfigurar frequência central após desativar Q-on";
                Logger::warn(lastError_);
            }
        }

        // Em amostragem direta (HF), o tuner R820T2 é bypassado: ativa o AGC
        // interno do RTL2832U para compensar. Ao voltar para VHF/UHF, respeita
        // o modo configurado (manual=0, AGC=-1→1).
        applyAgcMode(dev_, agcMode_, on ? 1 : (gainTenths_ < 0 ? 1 : 0));
    }
#endif
}

void RtlSdrDevice::setPpm(int ppm) {
    ppm_ = ppm;
#if RTLSDR_AVAILABLE
    if (dev_) {
        if (rtlsdr_set_freq_correction((rtlsdr_dev_t*)dev_, ppm) < 0) {
            lastError_ = "RTL-SDR: falha ao definir correção PPM";
            Logger::warn(lastError_);
        }
    }
#endif
}

void RtlSdrDevice::start() {
#if RTLSDR_AVAILABLE
    if (!dev_) {
        Logger::error("RTL-SDR start() falhou: dev_ é null (dispositivo não aberto)");
        return;
    }
    if (!deviceOk_) {
        Logger::error("RTL-SDR start() ignorado: dispositivo em estado de erro.");
        return;
    }
    if (running_) {
        Logger::warn("RTL-SDR start() ignorado: já em execução");
        return;
    }
    Logger::info("RTL-SDR: iniciando stream async...");
    int ret = rtlsdr_reset_buffer((rtlsdr_dev_t*)dev_);
    if (ret < 0) {
        Logger::warn(QString("RTL-SDR: reset_buffer retornou %1 (não crítico)").arg(ret));
    }
    running_ = true;
    thread_ = QThread::create([this]{ readLoop(); });
    thread_->start();
    Logger::info("RTL-SDR: thread de leitura lançada");
#endif
}

void RtlSdrDevice::stop() {
    if (!running_) return;
    Logger::info("RTL-SDR: parando stream...");
    running_ = false;
#if RTLSDR_AVAILABLE
    if (dev_) rtlsdr_cancel_async((rtlsdr_dev_t*)dev_);
#endif
    if (thread_) {
        thread_->wait();
        delete thread_;
        thread_ = nullptr;
    }
    // Libera tempo para a libusb/driver concluir callbacks e limpar transferências em background
    QThread::msleep(200);
    Logger::info("RTL-SDR: stream parado");
}

#if RTLSDR_AVAILABLE
static void rtlsdr_callback(unsigned char* buf, uint32_t len, void* ctx) {
    auto* self = static_cast<RtlSdrDevice*>(ctx);
    if (!self) return;
    // Conversão uint8 → complex<float> com offset 127.5
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
    Logger::info("RTL-SDR: read_async iniciado na thread de leitura");
    int ret = rtlsdr_read_async((rtlsdr_dev_t*)dev_, &rtlsdr_callback, this, 0, 16384*2);
    Logger::info(QString("RTL-SDR: read_async encerrou com código %1").arg(ret));
#endif
}

} // namespace masdr
