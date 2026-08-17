#include "Application.h"
#ifndef RXSDR_HEADLESS
#include "TrayController.h"
#endif
#include "Config.h"
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
#include "../decoders/AisCatcherManager.h"
#include "../decoders/AcarsDecoManager.h"
#include "../decoders/DsdManager.h"
#include "../decoders/AprsManager.h"
#include "../decoders/AprsIsClient.h"
#include "../decoders/SitorBManager.h"
#include "../decoders/CwManager.h"
#include "../decoders/PactorManager.h"
#include "../decoders/DscManager.h"
#include "../decoders/AnaliseManager.h"
#include "../decoders/SelcalManager.h"
#include "../decoders/TetraManager.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QCoreApplication>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdlib>   // std::llabs, usado na deteccao de VFO sobre o DC
#include <complex>

namespace masdr {
namespace {
std::unique_ptr<Demodulator> createDemodForMode(const QString& mode)
{
    const QString m = mode.toUpper();
    if (m == "AM") return std::make_unique<DemodAM>();
    if (m == "FM" || m == "NFM" || m == "WFM") {
        auto d = std::make_unique<DemodFM>();
        d->setMode(m);   // mode_ é usado internamente para gain (NFM ±5 kHz, WFM ±75 kHz)
        return d;
    }
    if (m == "CW") return std::make_unique<DemodCW>();
    auto d = std::make_unique<DemodSSB>();
    d->setMode(m);
    return d;
}

QJsonObject buildConfigJson()
{
    const auto& c = Config::instance();
    QJsonObject o;
    o["deviceType"] = c.lastDevice();
    o["deviceSerial"] = c.lastSerial();
    o["rtltcpHost"] = c.rtltcpHost();
    o["rtltcpPort"] = c.rtltcpPort();
    o["sampleRate"] = static_cast<int>(c.sampleRate());
    o["fftSize"] = c.fftSize();
    o["gainTenths"] = c.gainTenths();
    o["agc"] = c.agc();
    o["biasT"] = c.biasT();
    o["quadrature"] = c.quadrature();
    o["qmode"] = c.qMode();
    o["ppm"] = c.ppm();
    o["iqCorrection"] = c.iqCorrection();
    o["sdrplayIfMode"] = c.sdrplayIfMode();
    o["sdrplayLnaState"] = c.sdrplayLnaState();
    o["sdrplayIfGain"] = c.sdrplayIfGain();
    o["sdrplayIfAgc"] = c.sdrplayIfAgc();
    o["sdrplayBw"] = c.sdrplayBw();
    QJsonObject sm;
    sm["hfOffset"] = c.smeterHfOffset();
    sm["vhfOffset"] = c.smeterVhfOffset();
    sm["s9Hf"] = c.smeterS9Hf();
    sm["s9Vhf"] = c.smeterS9Vhf();
    sm["hfEmpty"] = c.smeterHfEmpty();
    sm["vhfEmpty"] = c.smeterVhfEmpty();
    sm["rmsAligned"] = c.smeterRmsAligned();
    o["smeter"] = sm;
    return o;
}

QString resolveDeviceEndpoint(const QString& type, const QString& serial)
{
    const auto& cfg = Config::instance();
    if (type != QStringLiteral("rtltcp")) return serial.trimmed();
    const QString t = serial.trimmed();
    if (t.contains(':')) return t;
    const QString host = t.isEmpty() ? cfg.rtltcpHost() : t;
    return QStringLiteral("%1:%2").arg(host).arg(cfg.rtltcpPort());
}
}

Application::Application() = default;
Application::~Application()
{
    stop();
}

bool Application::start()
{
    Logger::info("RXSDR starting...");
    dcBlock_.reset();
    const auto& cfg = Config::instance();
    port_ = cfg.httpPort();
    freqA_.store(cfg.vfoA());
    mode_ = cfg.mode();

    // Pipeline DSP básico para já disponibilizar espectro/áudio.
    fft_ = std::make_unique<FftProcessor>(Config::instance().fftSize());
    demod_ = createDemodForMode(mode_);
    demod_->setBandwidth(bwHz_.load());
    demod_->setAudioCallback([this](const std::vector<int16_t>& pcm, uint32_t sps) {
        handleAudioCallback(pcm, sps);
    });
    {
        std::lock_guard<std::mutex> lkAudio(audioBufferMutex_);
        audioBuffer_.clear();
    }

    ws_ = std::make_unique<WsServer>();
    if (!ws_->listen(cfg.wsPort())) {
        Logger::warn("WS em porta padrão falhou, tentando porta dinâmica");
        if (!ws_->listen(0)) {
            Logger::error("Falha ao iniciar WebSocket");
            return false;
        }
    }
    // O timer de auto-quit é desabilitado no modo headless (Pi roda 24/7)
    if (!headless_) {
        noClientQuitTimer_ = std::make_unique<QTimer>(this);
        noClientQuitTimer_->setSingleShot(true);
        noClientQuitTimer_->setInterval(10000);
        connect(noClientQuitTimer_.get(), &QTimer::timeout, this, [this]() {
            if (!ws_ || ws_->clientCount() > 0 || !hadWsClient_) return;
            Logger::info("Sem clientes Web por 10 segundos, encerrando RXSDR automaticamente.");
            QCoreApplication::quit();
        });
    }
    // Entrega o audio em ritmo constante, numa thread que ninguem interrompe.
    audioThread_ = new QThread(this);
    audioThread_->setObjectName(QStringLiteral("audio-pace"));
    audioPaceTimer_ = new QTimer();              // sem pai: vai mudar de thread
    audioPaceTimer_->setInterval(20);
    audioPaceTimer_->setTimerType(Qt::PreciseTimer);
    audioPaceTimer_->moveToThread(audioThread_);

    // O timer so pode ser iniciado de dentro da propria thread.
    connect(audioThread_, &QThread::started, audioPaceTimer_,
            QOverload<>::of(&QTimer::start));
    // DirectConnection: a entrega roda NA thread do audio. Se fosse a
    // conexao automatica, o trabalho voltaria para a fila da thread principal
    // e o problema continuaria exatamente igual.
    connect(audioPaceTimer_, &QTimer::timeout, this,
            &Application::entregarAudioRitmado, Qt::DirectConnection);
    connect(audioThread_, &QThread::finished, audioPaceTimer_, &QObject::deleteLater);
    audioThread_->start();

    connect(ws_.get(), &WsServer::clientsChanged, this, [this](int count) {
        if (count > 0) {
            hadWsClient_ = true;
            if (noClientQuitTimer_) noClientQuitTimer_->stop();
            return;
        }
        if (!headless_ && hadWsClient_ && noClientQuitTimer_) noClientQuitTimer_->start();
    });

    rest_ = std::make_unique<RestApi>();
    rest_->onListDevices = [this]() {
        QJsonArray arr = DeviceFactory::scanAll(deviceType_, deviceSerial_);
        if (device_ && !deviceSerial_.isEmpty()) {
            bool found = false;
            for (const auto& val : arr) {
                QJsonObject o = val.toObject();
                if (o["type"].toString() == deviceType_ && o["serial"].toString() == deviceSerial_) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                QJsonObject o;
                o["type"]   = deviceType_;
                o["serial"] = deviceSerial_;
                // Usa o serial real do dongle no display
                o["name"]   = deviceType_ == QStringLiteral("rtlsdr") ? QStringLiteral("RTL-SDR") : deviceType_;
                arr.prepend(o);
            }
        }
        return arr;
    };
    rest_->onSelectDevice = [this](const QString& type, const QString& serial) -> QJsonObject {
        QJsonObject out;
        auto dev = DeviceFactory::create(type);
        if (!dev) {
            out.insert("ok", false);
            out.insert("error", QStringLiteral("Tipo de dispositivo inválido: %1").arg(type));
            return out;
        }
        const QString endpoint = resolveDeviceEndpoint(type, serial);
        const QString openArg = (type == QStringLiteral("rtltcp")) ? endpoint : serial;
        if (type == QStringLiteral("rtltcp") && openArg.isEmpty()) {
            out.insert("ok", false);
            out.insert("error", QStringLiteral("Indique o IP e a porta do servidor rtl_tcp."));
            return out;
        }

        // ── IMPORTANTE: fecha o dispositivo atual ANTES de abrir o novo ──────
        // Se não fecharmos aqui, a libusb não consegue abrir o mesmo dongle
        // pois já há um handle ativo (ex: auto-select do boot).
        if (device_) {
            Logger::info(QString("Fechando dispositivo anterior: %1").arg(deviceType_));
            device_->stop();
            device_->close();
            device_.reset();
            dcBlock_.reset();
            // Dá um tempo de folga para o Windows e a libusb liberarem o descritor USB
            QThread::msleep(300);
        }

        if (type == QStringLiteral("rtlsdr")) {
            auto& cfg = Config::instance();
            dev->setSampleRate(cfg.sampleRate());
            
            const uint64_t currentFreq = freqA_.load();
            dev->setQuadrature(cfg.quadratureEm(currentFreq));
            dev->setPpm(cfg.ppm());
            dev->setBias(cfg.biasT());
            dev->setCenterFreq(currentFreq);
            dev->setGain(cfg.agc() ? -1 : cfg.gainTenths());
        }

        // Tenta abrir o dispositivo com retentativas para evitar falhas de concorrência USB do Windows
        bool openOk = false;
        for (int rTry = 0; rTry < 3; ++rTry) {
            if (dev->open(openArg)) {
                openOk = true;
                break;
            }
            Logger::warn(QString("RTL-SDR: tentativa %1 de abrir falhou. Retentando em 250ms...").arg(rTry + 1));
            QThread::msleep(250);
        }

        if (!openOk) {
            out.insert("ok", false);
            QString err = dev->lastError();
            if (err.isEmpty())
                err = QStringLiteral("Falha ao abrir %1 (%2)").arg(type, openArg);
            out.insert("error", err);
            return out;
        }
        device_ = dev;
        deviceType_ = type;
        deviceSerial_ = dev->serial().isEmpty() ? openArg : dev->serial();
        Config::instance().setLastDevice(type);
        Config::instance().setLastSerial(deviceSerial_);
        if (type == QStringLiteral("rtltcp")) {
            const int colon = deviceSerial_.lastIndexOf(':');
            if (colon > 0) {
                Config::instance().setRtltcpHost(deviceSerial_.left(colon));
                Config::instance().setRtltcpPort(deviceSerial_.mid(colon + 1).toInt());
            }
        }
        applyConfigToDevice();
        device_->setCenterFreq(freqA_.load());
        // Reaplica o ganho APÓS setCenterFreq porque o driver RTL-SDR
        // reseta o ganho internamente ao mudar a frequência central.
        if (deviceType_ != QStringLiteral("sdrplay")) {
            device_->setGain(Config::instance().agc() ? -1 : Config::instance().gainTenths());
        }
        wireDeviceCallback();
        if (powerOn_) device_->start();
        Logger::info(QString("Dispositivo selecionado: %1 (%2)").arg(type, deviceSerial_));
        out.insert("ok", true);
        out.insert("serial", deviceSerial_);
        return out;
    };
    rest_->onTune = [this](const QString& /*vfo*/, quint64 freq, const QString& mode, int bwHz) {
        freqA_.store(freq);                          // atômico: lido na thread callback
        QString newMode = mode.toUpper();
        bool modeChanged = (newMode != mode_);
        mode_ = newMode;
        bwHz_.store(bwHz);
        Config::instance().setVfoA(freq);
        Config::instance().setMode(mode_);
        if (device_) {
            const uint32_t sr = device_->sampleRate();
            const int64_t currentCenter = device_->centerFreq();
            const int64_t diff = std::abs(static_cast<int64_t>(freq) - currentCenter);
            // Se estiver dentro de 48% da largura de banda visível, sintoniza apenas via Software VFO (sem mover a cachoeira)
            if (diff > static_cast<int64_t>(sr * 0.48)) {
                // Atualiza o modo direct sampling conforme a frequência ANTES de sintonizar.
                // Isso evita que o sintonizador (tuner) tente sintonizar em HF e trave o dongle.
                device_->setQuadrature(Config::instance().quadratureEm(freq));

                device_->setCenterFreq(freq);
                // Re-aplica o ganho: o driver RTL-SDR reseta o ganho de hardware
                // internamente ao mudar a frequência central — sem isso o ganho
                // manual some a cada mudança de frequência.
                if (deviceType_ == QStringLiteral("sdrplay")) {
                    auto* sdrplay = dynamic_cast<SdrplayDevice*>(device_.get());
                    if (sdrplay) {
                        const auto& cfg = Config::instance();
                        sdrplay->setSdrplayParams(
                            cfg.sdrplayIfMode(),
                            cfg.sdrplayLnaState(),
                            cfg.sdrplayIfGain(),
                            cfg.sdrplayIfAgc(),
                            cfg.sdrplayBw()
                        );
                    }
                } else {
                    device_->setGain(Config::instance().agc() ? -1 : Config::instance().gainTenths());
                }
            }
        }
        // CRÍTICO: demod_ é lido na thread do callback (librtlsdr). Trocar o
        // demodulador sem mutex causa use-after-free: o destrutor do objeto antigo
        // corre enquanto process() ainda está sendo executado → SEGFAULT.
        {
            std::lock_guard<std::mutex> lk(demodMutex_);
            if (modeChanged || !demod_) {
                demod_ = createDemodForMode(mode_);
                demod_->setAudioCallback([this](const std::vector<int16_t>& pcm, uint32_t sps) {
                    handleAudioCallback(pcm, sps);
                });
                {
                    std::lock_guard<std::mutex> lkAudio(audioBufferMutex_);
                    audioBuffer_.clear();
                }
            }
            if (demod_) {
                demod_->setBandwidth(bwHz);
            }
        }
        return true;
    };
    rest_->onSetCenter = [this](quint64 freq) {
        if (device_) {
            // Atualiza o modo direct sampling conforme a frequência ANTES de sintonizar.
            // Isso evita que o sintonizador (tuner) tente sintonizar em HF e trave o dongle.
            device_->setQuadrature(Config::instance().quadratureEm(freq));

            device_->setCenterFreq(freq);
            if (deviceType_ == QStringLiteral("sdrplay")) {
                auto* sdrplay = dynamic_cast<SdrplayDevice*>(device_.get());
                if (sdrplay) {
                    const auto& cfg = Config::instance();
                    sdrplay->setSdrplayParams(
                        cfg.sdrplayIfMode(),
                        cfg.sdrplayLnaState(),
                        cfg.sdrplayIfGain(),
                        cfg.sdrplayIfAgc(),
                        cfg.sdrplayBw()
                    );
                }
            } else {
                device_->setGain(Config::instance().agc() ? -1 : Config::instance().gainTenths());
            }
            return true;
        }
        return false;
    };
    rest_->onSetGain = [this](int gainTenths) {
        Config::instance().setGain(gainTenths);
        if (device_ && deviceType_ != QStringLiteral("sdrplay")) {
            const int g = Config::instance().agc() ? -1 : gainTenths;
            device_->setGain(g);
        }
        return true;
    };
    rest_->onPower = [this](bool on) {
        powerOn_ = on;
        Logger::info(QString("Power %1 | device: %2").arg(on ? "ON" : "OFF",
                     device_ ? deviceType_ : "nenhum"));
        
        if (powerOn_ && !device_) {
            const auto& cfg = Config::instance();
            const QString lastType = cfg.lastDevice();
            if (!lastType.isEmpty() && rest_->onSelectDevice) {
                const QString lastSerial = cfg.lastSerial();
                Logger::info(QString("Tentando conectar dispositivo ao ligar: %1 (%2)").arg(lastType, lastSerial));
                rest_->onSelectDevice(lastType, lastSerial);
            }
        }

        if (!device_) {
            if (powerOn_) {
                Logger::error("Nao foi possivel ligar o radio: nenhum dispositivo conectado.");
                powerOn_ = false;
                return false;
            }
            return true;
        }

        if (powerOn_) {
            {
                std::lock_guard<std::mutex> lkAudio(audioBufferMutex_);
                audioBuffer_.clear();
            }
            applyConfigToDevice();
            device_->setCenterFreq(freqA_.load());
            // Reaplica o ganho APÓS setCenterFreq porque o driver RTL-SDR
            // reseta o ganho internamente ao mudar a frequência central.
            const auto& cfg = Config::instance();
            if (deviceType_ == QStringLiteral("sdrplay")) {
                auto* sdrplay = dynamic_cast<SdrplayDevice*>(device_.get());
                if (sdrplay) {
                    sdrplay->setSdrplayParams(
                        cfg.sdrplayIfMode(),
                        cfg.sdrplayLnaState(),
                        cfg.sdrplayIfGain(),
                        cfg.sdrplayIfAgc(),
                        cfg.sdrplayBw()
                    );
                }
            } else {
                device_->setGain(cfg.agc() ? -1 : cfg.gainTenths());
            }
            device_->start();
        } else {
            device_->stop();
            {
                std::lock_guard<std::mutex> lkAudio(audioBufferMutex_);
                audioBuffer_.clear();
            }
        }
        return true;
    };
    rest_->onGetConfig = []() { return buildConfigJson(); };
    rest_->onSetConfig = [this](const QJsonObject& j) {
        return applyConfigJson(j);
    };
    // ── AIS-catcher — AIS Decoder ──────────────────────────────────────────────
    aisCatcher_ = std::make_unique<AisCatcherManager>(this);

    connect(aisCatcher_.get(), &AisCatcherManager::logLine, [this](const QString& line) {
        Logger::info(line);
        ws_->broadcastJson(QJsonObject{
            {"t", "dec_line"},
            {"decoder", "AIS"},
            {"text", line}
        });
    });
    connect(aisCatcher_.get(), &AisCatcherManager::error, [](const QString& msg) {
        Logger::error(msg);
    });

    rest_->onAisStatus = [this]() {
        QJsonObject o = aisCatcher_->statusJson();
        o["binaryPresent"] = aisCatcher_->binaryExists();
        return o;
    };

    rest_->onAisStart = [this](const QJsonObject& j) -> QJsonObject {
        QJsonObject r;
        if (j.contains("deviceIndex")) aisCatcher_->setDeviceIndex(j.value("deviceIndex").toInt(0));
        if (j.contains("webPort"))     aisCatcher_->setWebPort(static_cast<quint16>(j.value("webPort").toInt(8092)));

        if (!aisCatcher_->start()) {
            r["ok"]    = false;
            r["error"] = aisCatcher_->lastError();
            return r;
        }

        r["ok"]     = true;
        r["webUrl"] = aisCatcher_->webUrl();
        r["state"]  = aisCatcher_->stateString();
        return r;
    };

    rest_->onAisStop = [this]() -> QJsonObject {
        aisCatcher_->stop();

        QJsonObject r;
        r["ok"]    = true;
        r["state"] = aisCatcher_->stateString();
        return r;
    };

    // ── acarsdeco2 — ACARS Decoder ─────────────────────────────────────────────
    acarsDeco_ = std::make_unique<AcarsDecoManager>(this);

    connect(acarsDeco_.get(), &AcarsDecoManager::logLine, [this](const QString& line) {
        Logger::info(line);
        ws_->broadcastJson(QJsonObject{
            {"t", "dec_line"},
            {"decoder", "ACARS"},
            {"text", line}
        });
    });
    connect(acarsDeco_.get(), &AcarsDecoManager::error, [](const QString& msg) {
        Logger::error(msg);
    });
    rest_->onAcarsStatus = [this]() {
        QJsonObject o = acarsDeco_->statusJson();
        o["binaryPresent"] = acarsDeco_->binaryExists();
        return o;
    };

    rest_->onAcarsStart = [this](const QJsonObject& j) -> QJsonObject {
        QJsonObject r;
        if (j.contains("deviceIndex")) acarsDeco_->setDeviceIndex(j.value("deviceIndex").toInt(0));
        if (j.contains("webPort"))     acarsDeco_->setWebPort(static_cast<quint16>(j.value("webPort").toInt(8093)));
        if (j.contains("frequencies")) acarsDeco_->setFrequencies(j.value("frequencies").toString());

        acarsDeco_->setExclusiveMode(false);

        if (!acarsDeco_->start()) {
            r["ok"]    = false;
            r["error"] = acarsDeco_->lastError();
            return r;
        }

        r["ok"]     = true;
        r["webUrl"] = acarsDeco_->webUrl();
        r["state"]  = acarsDeco_->stateString();
        return r;
    };

    rest_->onAcarsStop = [this]() -> QJsonObject {
        acarsDeco_->stop();

        QJsonObject r;
        r["ok"]    = true;
        r["state"] = acarsDeco_->stateString();
        return r;
    };

    // ── DSD Decoder (DSDPlus/dsd) ───────────────────────────────────────────────
    dsdDeco_ = std::make_unique<DsdManager>(this);

    // LOG DO DSD DESATIVADO de proposito: NAO retransmitir cada linha do dsd-fme
    // pelo WebSocket. Durante a voz o dsd-fme emite varias linhas por segundo, e
    // esse fluxo competia com o audio na mesma conexao WebSocket, causando falhas
    // no som (especialmente em box mais fraco / cartao SD). Sem isto o audio fica limpo.
    connect(dsdDeco_.get(), &DsdManager::logLine, [](const QString& /*line*/) {
        /* desativado */
    });
    connect(dsdDeco_.get(), &DsdManager::error, [](const QString& msg) {
        Logger::error(msg);
    });
    connect(dsdDeco_.get(), &DsdManager::decodedAudioReady, this, [this](const std::vector<int16_t>& pcm, uint32_t sps) {
        if (ws_) {
            enviarAudioAoNavegador(pcm, sps);
        }
    });

    rest_->onDsdStatus = [this]() {
        QJsonObject o = dsdDeco_->statusJson();
        o["binaryPresent"] = dsdDeco_->binaryExists();
        return o;
    };

    rest_->onDsdStart = [this](const QJsonObject& j) -> QJsonObject {
        if (j.contains("inputDevice")) {
            dsdDeco_->setInputDevice(j.value("inputDevice").toInt(1));
        }
        QJsonObject r;
        if (!dsdDeco_->start()) {
            r["ok"]    = false;
            r["error"] = dsdDeco_->lastError();
            return r;
        }
        r["ok"]    = true;
        r["state"] = dsdDeco_->stateString();
        return r;
    };

    rest_->onDsdStop = [this]() -> QJsonObject {
        dsdDeco_->stop();
        QJsonObject r;
        r["ok"]    = true;
        r["state"] = dsdDeco_->stateString();
        return r;
    };

    rest_->onDsdTogglePolarity = [this]() -> QJsonObject {
        dsdDeco_->togglePolarity();
        return dsdDeco_->statusJson();
    };

    rest_->onDsdSetPcmHz = [this](int hz) -> QJsonObject {
        dsdDeco_->setUdpVoicePcmHz(hz);
        QJsonObject r;
        r["ok"]  = true;
        r["hz"]  = dsdDeco_->getUdpVoicePcmHz();
        return r;
    };



    // ── APRS Decoder (Direwolf) ────────────────────────────────────────────────
    aprsDeco_ = std::make_unique<AprsManager>(this);

    connect(aprsDeco_.get(), &AprsManager::logLine, [this](const QString& line) {
        // Grava tambem no run.log. Sem isto, quando o APRS parava de decodificar
        // nao havia rastro nenhum: nem o "[Direwolf] iniciado" aparecia, e era
        // impossivel saber se o processo tinha subido ou nem chegou a tentar.
        Logger::info(line);
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "APRS"},
            {"text",    line}
        });
    });
    connect(aprsDeco_.get(), &AprsManager::error, [](const QString& msg) {
        Logger::error(msg);
    });

    rest_->onAprsStatus = [this]() {
        QJsonObject o = aprsDeco_->statusJson();
        return o;
    };

    rest_->onAprsStart = [this](const QJsonObject& j) -> QJsonObject {
        Q_UNUSED(j)
        aprsDeco_->start();
        QJsonObject r = aprsDeco_->statusJson();
        r["ok"] = (aprsDeco_->state() == AprsManager::State::Running);
        return r;
    };

    // ── Envio de mensagem APRS pela internet (APRS-IS) ────────────────────
    // O RTL-SDR nao transmite; o caminho e injetar na rede APRS-IS. Se houver
    // um IGate transmissor perto do destinatario, chega no radio dele.
    aprsIs_ = std::make_unique<AprsIsClient>(this);
    connect(aprsIs_.get(), &AprsIsClient::logLine, [this](const QString& line) {
        Logger::info(line);
        ws_->broadcastJson(QJsonObject{
            {"t", "dec_line"}, {"decoder", "APRS"}, {"text", line}
        });
    });

    rest_->onAprsSend = [this](const QJsonObject& j) -> QJsonObject {
        const QString de    = j.value("from").toString();
        const QString para  = j.value("to").toString();
        const QString texto = j.value("text").toString();
        if (aprsIs_->ocupado()) {
            return QJsonObject{{"ok", false}, {"error", "Envio anterior ainda em andamento."}};
        }
        aprsIs_->enviar(de, para, texto);
        // O envio e assincrono: o resultado aparece no terminal do painel.
        return QJsonObject{{"ok", true}, {"info", "Enviando..."}};
    };

    rest_->onAprsStop = [this]() -> QJsonObject {
        aprsDeco_->stop();
        QJsonObject r = aprsDeco_->statusJson();
        r["ok"] = true;
        return r;
    };

    // ── SITOR-B Decoder (Transmissões Marinhas) ───────────────────────────
    sitorBDeco_ = std::make_unique<SitorBManager>(this);

    connect(sitorBDeco_.get(), &SitorBManager::logLine, [this](const QString& line) {
        Logger::info(line);
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "SITOR-B"},
            {"text",    line}
        });
    });
    connect(sitorBDeco_.get(), &SitorBManager::error, [](const QString& msg) {
        Logger::error(msg);
    });

    rest_->onSitorBStatus = [this]() {
        QJsonObject o = sitorBDeco_->statusJson();
        return o;
    };

    rest_->onSitorBStart = [this](const QJsonObject& j) -> QJsonObject {
        SitorBManager::Params p;
        p.baudRate   = static_cast<float>(j.value("baudRate").toDouble(100.0));
        p.shift      = static_cast<float>(j.value("shift").toDouble(170.0));
        p.centerFreq = static_cast<float>(j.value("centerFreq").toDouble(1700.0));
        p.invert     = j.value("invert").toBool(false);
        sitorBDeco_->setParams(p);
        sitorBDeco_->start();
        QJsonObject r = sitorBDeco_->statusJson();
        r["ok"] = (sitorBDeco_->state() == SitorBManager::State::Running);
        return r;
    };

    rest_->onSitorBStop = [this]() -> QJsonObject {
        sitorBDeco_->stop();
        QJsonObject r = sitorBDeco_->statusJson();
        r["ok"] = true;
        return r;
    };

    // ── CW / Morse ─────────────────────────────────────────────────────────
    cwDeco_ = std::make_unique<CwManager>(this);

    connect(cwDeco_.get(), &CwManager::logLine, [this](const QString& line) {
        Logger::info(line);
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "CW"},
            {"text",    line}
        });
    });

    // O texto decodificado vai por um caminho proprio, marcado com "cont".
    // A tela emenda esses pedacos na linha que ja esta la, em vez de comecar
    // uma linha nova a cada um - e assim a letra aparece no momento em que e
    // lida, como num terminal de telegrafia de verdade.
    connect(cwDeco_.get(), &CwManager::textoFluxo, [this](const QString& pedaco) {
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "CW"},
            {"text",    pedaco},
            {"cont",    true}
        });
    });

    rest_->onCwStatus = [this]() { return cwDeco_->statusJson(); };

    rest_->onCwStart = [this](const QJsonObject& j) -> QJsonObject {
        CwManager::Params p;
        // Tom 0 significa "meca sozinho". Em CW nao existe tom padrao: ele
        // depende de onde o operador sintonizou.
        p.tomHz   = static_cast<float>(j.value("tom").toDouble(0.0));
        p.autoTom = (p.tomHz <= 0.0f) || j.value("autoTom").toBool(true);
        cwDeco_->setParams(p);
        cwDeco_->start();
        QJsonObject r = cwDeco_->statusJson();
        r["ok"] = (cwDeco_->state() == CwManager::State::Running);
        return r;
    };

    rest_->onCwStop = [this]() -> QJsonObject {
        cwDeco_->stop();
        QJsonObject r = cwDeco_->statusJson();
        r["ok"] = true;
        return r;
    };

    // ── PACTOR Decoder (Pactor-I FSK) ──────────────────────────────────────
    pactorDeco_ = std::make_unique<PactorManager>(this);

    connect(pactorDeco_.get(), &PactorManager::logLine, [this](const QString& line) {
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "PACTOR"},
            {"text",    line}
        });
    });
    connect(pactorDeco_.get(), &PactorManager::error, [](const QString& msg) {
        Logger::error(msg);
    });

    rest_->onPactorStatus = [this]() {
        QJsonObject o = pactorDeco_->statusJson();
        return o;
    };

    rest_->onPactorStart = [this](const QJsonObject& j) -> QJsonObject {
        PactorManager::Params p;
        p.baudRate   = static_cast<float>(j.value("baudRate").toDouble(200.0));
        p.shift      = static_cast<float>(j.value("shift").toDouble(200.0));
        p.center     = static_cast<float>(j.value("center").toDouble(1500.0));
        p.invert     = j.value("invert").toBool(false);
        p.autoDetect = j.value("autoDetect").toBool(true);
        pactorDeco_->setParams(p);
        pactorDeco_->start();
        QJsonObject r = pactorDeco_->statusJson();
        r["ok"] = (pactorDeco_->state() == PactorManager::State::Running);
        return r;
    };

    rest_->onPactorStop = [this]() -> QJsonObject {
        pactorDeco_->stop();
        QJsonObject r = pactorDeco_->statusJson();
        r["ok"] = true;
        return r;
    };

    // ── DSC Decoder (ITU-R M.493) ──────────────────────────────────────────
    dscDeco_ = std::make_unique<DscManager>(this);

    connect(dscDeco_.get(), &DscManager::logLine, [this](const QString& line) {
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "DSC"},
            {"text",    line}
        });
    });
    connect(dscDeco_.get(), &DscManager::error, [](const QString& msg) {
        Logger::error(msg);
    });

    rest_->onDscStatus = [this]() {
        QJsonObject o = dscDeco_->statusJson();
        return o;
    };

    rest_->onDscStart = [this](const QJsonObject& j) -> QJsonObject {
        DscManager::Params p;
        p.baudRate   = static_cast<float>(j.value("baudRate").toDouble(100.0));
        p.shift      = static_cast<float>(j.value("shift").toDouble(170.0));
        p.centerFreq = static_cast<float>(j.value("centerFreq").toDouble(
                           j.value("center").toDouble(1700.0)));
        p.invert     = j.value("invert").toBool(false);
        dscDeco_->setParams(p);
        dscDeco_->start();
        QJsonObject r = dscDeco_->statusJson();
        r["ok"] = (dscDeco_->state() == DscManager::State::Running);
        return r;
    };

    // ── Audio de arquivo ───────────────────────────────────────────────────
    // O navegador ja sabe abrir mp3, wav e ogg, entao ele decodifica e manda
    // as amostras prontas. Evita trazer uma biblioteca de audio so para isso,
    // e de quebra aceita qualquer formato que o navegador aceite.
    rest_->onAudioArquivo = [this](const QJsonObject& j) -> QJsonObject {
        const QString alvo = j.value("decoder").toString().toUpper();
        const uint32_t sps = uint32_t(j.value("sampleRate").toInt(8000));
        const QByteArray bruto = QByteArray::fromBase64(
            j.value("pcm").toString().toLatin1());

        const int n = int(bruto.size() / 2);
        if (n <= 0) return QJsonObject{{"ok",false},{"error","sem amostras"}};
        const int16_t* pcm = reinterpret_cast<const int16_t*>(bruto.constData());

        if (alvo == QLatin1String("SITORB") && sitorBDeco_)
            sitorBDeco_->feedAudio(pcm, n, sps);
        else if (alvo == QLatin1String("DSC") && dscDeco_)
            dscDeco_->feedAudio(pcm, n, sps);
        else if (alvo == QLatin1String("ANALISE") && analiseDeco_)
            analiseDeco_->feedAudio(pcm, n, sps);
        else if (alvo == QLatin1String("CW") && cwDeco_)
            cwDeco_->feedAudio(pcm, n, sps);
        else
            return QJsonObject{{"ok",false},{"error","decoder desconhecido: " + alvo}};

        return QJsonObject{{"ok",true},{"samples",n}};
    };

    // ── Analisador de sinal desconhecido ───────────────────────────────────
    analiseDeco_ = std::make_unique<AnaliseManager>(this);
    connect(analiseDeco_.get(), &AnaliseManager::logLine, [this](const QString& line) {
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "ANALISE"},
            {"text",    line}
        });
        Logger::info(line);
    });
    rest_->onAnaliseStatus = [this]() { return analiseDeco_->statusJson(); };
    rest_->onAnaliseStart  = [this]() -> QJsonObject {
        analiseDeco_->start();
        QJsonObject r = analiseDeco_->statusJson();
        r["ok"] = true;
        return r;
    };
    rest_->onAnaliseStop = [this]() -> QJsonObject {
        analiseDeco_->stop();
        QJsonObject r = analiseDeco_->statusJson();
        r["ok"] = true;
        return r;
    };

    rest_->onDscStop = [this]() -> QJsonObject {
        dscDeco_->stop();
        QJsonObject r = dscDeco_->statusJson();
        r["ok"] = true;
        return r;
    };




    // ── SELCAL Decoder ─────────────────────────────────────────────────────────
    selcalDeco_ = std::make_unique<SelcalManager>(this);

    connect(selcalDeco_.get(), &SelcalManager::logLine, [this](const QString& line) {
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "SELCAL"},
            {"text",    line}
        });
    });
    connect(selcalDeco_.get(), &SelcalManager::error, [](const QString& msg) {
        Logger::error(msg);
    });

    rest_->onSelcalStatus = [this]() {
        return selcalDeco_->statusJson();
    };

    rest_->onSelcalStart = [this](const QJsonObject& j) -> QJsonObject {
        Q_UNUSED(j);
        SelcalManager::Params p;
        selcalDeco_->setParams(p);
        selcalDeco_->start();
        QJsonObject r = selcalDeco_->statusJson();
        r["ok"] = (selcalDeco_->state() == SelcalManager::State::Running);
        return r;
    };

    rest_->onSelcalStop = [this]() -> QJsonObject {
        selcalDeco_->stop();
        QJsonObject r = selcalDeco_->statusJson();
        r["ok"] = true;
        return r;
    };

    // ── TETRA Decoder ──────────────────────────────────────────────────────────
    tetraDeco_ = std::make_unique<TetraManager>(this);

    connect(tetraDeco_.get(), &TetraManager::logLine, [this](const QString& line) {
        ws_->broadcastJson(QJsonObject{
            {"t",       "dec_line"},
            {"decoder", "TETRA"},
            {"text",    line}
        });
    });
    connect(tetraDeco_.get(), &TetraManager::error, [](const QString& msg) {
        Logger::error(msg);
    });
    connect(tetraDeco_.get(), &TetraManager::decodedAudioReady, this,
            [this](const std::vector<int16_t>& pcm, uint32_t sps) {
        // Reproduz a voz TETRA decodificada (ACELP -> PCM 48 kHz) no navegador
        // pelo mesmo caminho do audio do radio.
        enviarAudioAoNavegador(pcm, sps);
    });

    rest_->onTetraStatus = [this]() {
        return tetraDeco_->statusJson();
    };

    rest_->onTetraStart = [this](const QJsonObject& j) -> QJsonObject {
        TetraManager::Params p;
        p.symbolRate = j.value("symbolRate").toInt(18000);
        p.invertIQ = j.value("invertIQ").toBool(false);
        tetraDeco_->setParams(p);
        tetraDeco_->start();
        QJsonObject r = tetraDeco_->statusJson();
        r["ok"] = (tetraDeco_->state() == TetraManager::State::Running);
        return r;
    };

    rest_->onTetraStop = [this]() -> QJsonObject {
        tetraDeco_->stop();
        QJsonObject r = tetraDeco_->statusJson();
        r["ok"] = true;
        return r;
    };

    rest_->onStatus = [this]() {
        const auto& cfg = Config::instance();
        const bool live = device_ && (QDateTime::currentMSecsSinceEpoch() - lastIqMs_.load()) < 2000;
        QJsonObject o;
        o["ok"] = true;
        o["deviceType"] = deviceType_;
        o["serial"] = deviceSerial_;
        o["freqA"] = static_cast<double>(freqA_.load());
        o["mode"] = mode_;
        o["wsPort"] = static_cast<int>(ws_ ? ws_->port() : 0);
        o["httpPort"] = static_cast<int>(port_);
        o["powerOn"] = powerOn_;
        o["streaming"] = live;
        o["rfMode"] = live ? QStringLiteral("live") : QStringLiteral("idle");
        o["peakDb"] = static_cast<double>(peakDb_.load());
        o["gainTenths"] = cfg.gainTenths();
        o["agc"] = cfg.agc();
        o["biasT"] = cfg.biasT();
        o["quadrature"] = cfg.quadrature();
        o["qmode"] = cfg.qMode();
        o["sampleRate"] = static_cast<int>(cfg.sampleRate());

        // ---- saude da producao de audio -------------------------------------
        // 'audioFalta' e a diferenca, em milissegundos, entre o audio que
        // DEVERIA ter sido produzido pelo relogio de parede e o que realmente
        // saiu. Se crescer, o backend esta perdendo audio - e audio perdido e
        // descontinuidade, ou seja, picote, mesmo sem o navegador falhar.
        const int64_t t0 = audioT0_.load();
        if (t0 > 0) {
            const int64_t esperado = audioEsperadoMs_.load() * 48;   // 48 amostras por ms
            o["audioFalta"]  = double(esperado - audioAmostras_.load()) / 48.0;
            o["audioMaiorVao"] = audioMaiorVaoMs_.load();
            o["audioVaos"]     = audioVaosGrandes_.load();
        }
        o["reconfigs"] = reconfigs_.load();
        return o;
    };

    http_ = std::make_unique<HttpServer>();
    http_->setRestApi(rest_.get());
    if (!http_->listen(port_)) {
        // tenta portas alternativas
        for (quint16 p : {8081, 8082, 8083, 8084, 0}) {
            if (http_->listen(p)) { port_ = p; break; }
        }
    }
    if (http_->port() == 0) {
        Logger::error("Falha ao iniciar HTTP");
        return false;
    }
    port_ = http_->port();

    // Auto-select com último dispositivo só se existir hardware.
    const QString lastType = cfg.lastDevice();
    if (!lastType.isEmpty()) {
        const QString lastSerial = cfg.lastSerial();
        rest_->onSelectDevice(lastType, lastSerial);
    }

    // Tray icon (somente modo GUI / Windows)
#ifndef RXSDR_HEADLESS
    if (!headless_) {
        tray_ = std::make_unique<TrayController>(frontendUrl());
        tray_->show();
    }
#endif

    Logger::info(QString("Backend up at %1").arg(frontendUrl()));
    return true;
}

void Application::stop()
{
    // Para o relogio do audio ANTES de tudo. Se ele continuasse batendo
    // enquanto o WebSocket e os decodificadores sao desmontados, ele acabaria
    // usando algo ja destruido - e o programa fecharia com falha em vez de
    // fechar limpo, que foi trabalho que ja tivemos aqui.
    if (audioThread_) {
        audioThread_->quit();
        audioThread_->wait(1000);
        audioPaceTimer_ = nullptr;   // apagado pelo sinal finished
    }

    dcBlock_.reset();
    if (aisCatcher_) aisCatcher_->stop();
    if (acarsDeco_) acarsDeco_->stop();
    if (dsdDeco_) dsdDeco_->stop();
    if (aprsDeco_) aprsDeco_->stop();
    if (sitorBDeco_) sitorBDeco_->stop();
    if (pactorDeco_) pactorDeco_->stop();
    if (dscDeco_) dscDeco_->stop();
    if (analiseDeco_) analiseDeco_->stop();
    if (selcalDeco_) selcalDeco_->stop();
    if (tetraDeco_) tetraDeco_->stop();
    if (device_) {
        device_->stop();
        device_->close();
        device_.reset();
    }
    if (ws_) ws_->stop();
    if (http_) http_->stop();
    {
        std::lock_guard<std::mutex> lkAudio(audioBufferMutex_);
        audioBuffer_.clear();
    }
}

QString Application::frontendUrl() const
{
    return QString("http://localhost:%1").arg(port_);
}

void Application::applyConfigToDevice()
{
    if (!device_) return;
    reconfigs_.fetch_add(1);
    auto& cfg = Config::instance();
    // O modo escolhido pelo usuario decide: "off" nunca, "on" sempre, "auto"
    // liga abaixo de 24 MHz e desliga acima. A escolha nunca e sobrescrita no
    // arquivo de configuracao - so o que vai para o dispositivo muda.
    const uint64_t currentFreq = freqA_.load();
    const bool quadratureOn = cfg.quadratureEm(currentFreq);
    if (cfg.qMode() == QLatin1String("auto")) {
        Logger::info(QString("Modo Q automatico: %1 em %2 Hz")
                         .arg(quadratureOn ? "amostragem direta ON" : "tuner normal (Q OFF)")
                         .arg(currentFreq));
    }
    device_->setSampleRate(cfg.sampleRate());
    device_->setBias(cfg.biasT());
    device_->setQuadrature(quadratureOn);
    device_->setPpm(cfg.ppm());

    if (deviceType_ == QStringLiteral("sdrplay")) {
        auto* sdrplay = dynamic_cast<SdrplayDevice*>(device_.get());
        if (sdrplay) {
            sdrplay->setSdrplayParams(
                cfg.sdrplayIfMode(),
                cfg.sdrplayLnaState(),
                cfg.sdrplayIfGain(),
                cfg.sdrplayIfAgc(),
                cfg.sdrplayBw()
            );
        }
    }

    // Ganho definido POR ÚLTIMO para garantir que não seja resetado pelo
    // retune interno do setQuadrature ou por qualquer ajuste de frequência anterior.
    if (deviceType_ != QStringLiteral("sdrplay")) {
        device_->setGain(cfg.agc() ? -1 : cfg.gainTenths());
    }

}

bool Application::applyConfigJson(const QJsonObject& j)
{
    auto& cfg = Config::instance();

    // Captura os valores de dispositivo atuais antes de atualizar o Config
    const QString oldType = deviceType_;
    const QString oldSerial = deviceSerial_;
    // Estado EFETIVO antes da mudanca, e nao a chave crua: e o que decide se
    // o dispositivo precisa mesmo ser reaberto.
    const bool oldQuadrature = cfg.quadratureEm(freqA_.load());
    const int oldSampleRate = cfg.sampleRate();
    const bool oldBiasT = cfg.biasT();
    const int oldFftSize = cfg.fftSize();

    QString newType = j.contains("deviceType") ? j.value("deviceType").toString() : oldType;
    QString newSerial = j.contains("deviceSerial") ? j.value("deviceSerial").toString() : oldSerial;

    // Se for rtltcp, a serial/endpoint pode precisar ser construída a partir de host/port
    if (newType == QStringLiteral("rtltcp")) {
        const QString host = j.contains("rtltcpHost") ? j.value("rtltcpHost").toString() : cfg.rtltcpHost();
        const int port = j.contains("rtltcpPort") ? j.value("rtltcpPort").toInt() : cfg.rtltcpPort();
        if (!host.isEmpty()) {
            newSerial = QStringLiteral("%1:%2").arg(host).arg(port);
        }
    }

    if (j.contains("deviceType")) cfg.setLastDevice(j.value("deviceType").toString());
    if (j.contains("deviceSerial")) cfg.setLastSerial(j.value("deviceSerial").toString());
    if (j.contains("rtltcpHost")) cfg.setRtltcpHost(j.value("rtltcpHost").toString());
    if (j.contains("rtltcpPort")) cfg.setRtltcpPort(j.value("rtltcpPort").toInt(1234));
    if (j.value("deviceType").toString() == QStringLiteral("rtltcp")) {
        const QString host = j.contains("rtltcpHost") ? j.value("rtltcpHost").toString() : cfg.rtltcpHost();
        const int port = j.contains("rtltcpPort") ? j.value("rtltcpPort").toInt() : cfg.rtltcpPort();
        if (!host.isEmpty())
            cfg.setLastSerial(QStringLiteral("%1:%2").arg(host).arg(port));
    }
    if (j.contains("sampleRate")) cfg.setSampleRate(j.value("sampleRate").toInt(2048000));
    if (j.contains("gainTenths")) cfg.setGain(j.value("gainTenths").toInt(280));
    if (j.contains("agc")) cfg.setAgc(j.value("agc").toBool());
    if (j.contains("biasT")) cfg.setBiasT(j.value("biasT").toBool());
    if (j.contains("qmode")) cfg.setQMode(j.value("qmode").toString());
    else if (j.contains("quadrature")) cfg.setQuadrature(j.value("quadrature").toBool());
    if (j.contains("ppm")) cfg.setPpm(j.value("ppm").toInt());
    if (j.contains("iqCorrection")) cfg.setIqCorrection(j.value("iqCorrection").toBool());
    if (j.contains("sdrplayIfMode")) cfg.setSdrplayIfMode(j.value("sdrplayIfMode").toInt(0));
    if (j.contains("sdrplayLnaState")) cfg.setSdrplayLnaState(j.value("sdrplayLnaState").toInt(9));
    if (j.contains("sdrplayIfGain")) cfg.setSdrplayIfGain(j.value("sdrplayIfGain").toInt(59));
    if (j.contains("sdrplayIfAgc")) cfg.setSdrplayIfAgc(j.value("sdrplayIfAgc").toBool(false));
    if (j.contains("sdrplayBw")) cfg.setSdrplayBw(j.value("sdrplayBw").toInt(-1));
    if (j.contains("fftSize")) cfg.setFftSize(j.value("fftSize").toInt(8192));

    const bool fftChanged = j.contains("fftSize") && (j.value("fftSize").toInt() != oldFftSize);
    if (fftChanged) {
        std::lock_guard<std::mutex> lk(fftMutex_);
        fft_ = std::make_unique<FftProcessor>(cfg.fftSize());
    }
    if (j.contains("smeter")) {
        const auto sm = j.value("smeter").toObject();
        if (sm.contains("hfOffset")) cfg.setSmeterHfOffset(sm.value("hfOffset").toDouble());
        if (sm.contains("vhfOffset")) cfg.setSmeterVhfOffset(sm.value("vhfOffset").toDouble());
        if (sm.contains("s9Hf")) cfg.setSmeterS9Hf(sm.value("s9Hf").toInt());
        if (sm.contains("s9Vhf")) cfg.setSmeterS9Vhf(sm.value("s9Vhf").toInt());
        if (sm.contains("hfEmpty")) cfg.setSmeterHfEmpty(sm.value("hfEmpty").toInt());
        if (sm.contains("vhfEmpty")) cfg.setSmeterVhfEmpty(sm.value("vhfEmpty").toInt());
        if (sm.contains("rmsAligned")) cfg.setSmeterRmsAligned(sm.value("rmsAligned").toBool());
    }

    // Se o novo serial veio vazio E o tipo de dispositivo é o mesmo E há um device aberto,
    // trata como "mesmo dispositivo" — evita fechar/reabrir desnecessariamente.
    // Isso é comum ao salvar o Setup quando o campo Serial fica em branco
    // (o form não exibe o serial se o device já está conectado).
    if (newSerial.isEmpty() && newType == oldType && device_) {
        newSerial = oldSerial;
    }

    // Se o dispositivo físico (tipo ou serial) mudou, ou se parâmetros estruturais críticos mudaram,
    // ou se não há dispositivo aberto, força o fechamento e a reabertura completa do hardware
    // com delay de segurança para garantir estabilidade USB e reconfiguração correta de endpoints do driver.
    // Trocar de modo so exige reabrir o hardware se o que vai para o
    // dispositivo AGORA mudou. Passar de "on" para "auto" estando em HF, por
    // exemplo, nao muda nada na pratica e nao precisa derrubar o USB.
    const bool qChanged = (Config::instance().quadratureEm(freqA_.load()) != oldQuadrature);
    const bool srChanged = j.contains("sampleRate") && (j.value("sampleRate").toInt() != oldSampleRate);
    const bool biasChanged = j.contains("biasT") && (j.value("biasT").toBool() != oldBiasT);

    bool deviceChanged = (newType != oldType || newSerial != oldSerial || qChanged || srChanged || biasChanged || !device_);
    if (deviceChanged && !newType.isEmpty() && rest_->onSelectDevice) {
        rest_->onSelectDevice(newType, newSerial);
    } else {
        // Se caímos aqui, os parâmetros alterados não foram estruturais (como ganho manual, AGC e PPM)
        // e podem ser aplicados em tempo real na mesma transmissão sem interromper o stream.
        applyConfigToDevice();
    }

    Config::instance().sync();
    return true;
}

void Application::wireDeviceCallback()
{
    if (!device_) return;
    std::weak_ptr<ISdrDevice> weakDev = device_;
    device_->setCallback([this, weakDev](const std::complex<float>* iq, size_t n) {
        auto dev = weakDev.lock();
        if (!dev || !iq || n == 0) return;
        lastIqMs_.store(QDateTime::currentMSecsSinceEpoch());

        // Captura freqA_ atomicamente UMA VEZ no início do bloco.
        // freqA_ pode ser escrito pela thread HTTP (onTune) a qualquer momento;
        // usar o valor capturado localmente garante consistência dentro deste bloco.
        const uint64_t vfoHz = freqA_.load(std::memory_order_relaxed);

        // Ganho digital de RX (controlável pelo slider Gain da UI).
        // FÓRMULA RESTAURADA + AJUSTES FINAIS DO USUÁRIO.
        //
        // === Ganho de slider (manual) ===
        // Referência: 280 tenths (28,0 dB) → 0 dB de ganho digital adicional.
        // Cada 10 tenths (1,0 dB) acima/abaixo de 280 → +1/−1 dB digital, clampeado [-24, +36 dB].
        // Em 49,6 dB (gainTenths=496) → +21,6 dB digital → uiGain ≈ 12x.
        //
        // === Boost adicional em AGC ON ===
        // Em AGC ON o tuner/RTL2832U faz seu próprio AGC e estabiliza por volta
        // de 29,8 dB equivalentes. O usuário prefere um nível de 32,6 dB → aplicamos
        // +2,8 dB digitais fixos quando AGC está ligado (RTL-SDR / rtl_tcp).
        //
        // === Boost adicional em HF (Q-on / direct sampling) ===
        // Em direct sampling o tuner R820T2 é bypassado e o sinal entra direto
        // no ADC do RTL2832U, que tem ganho menor que o caminho com tuner ativo.
        // Compensa-se com +18 dB digital fixo em HF para alinhar o nível ao do
        // VHF/UHF (aplicado por cima do uiGain, vale em manual e em AGC).
        //
        // Exceções:
        //   • SDRplay: tem AGC/gRdB próprios na API — não somar ganho digital extra.
        const auto& cfgNow = Config::instance();
        const int gainTenths = cfgNow.gainTenths();
        const float relDb = std::clamp((static_cast<float>(gainTenths) - 280.0f) / 10.0f, -24.0f, 36.0f);
        float uiGain = std::pow(10.0f, relDb / 20.0f);
        const bool isDirectSampling = cfgNow.quadratureEm(vfoHz);
        const bool isRtlsdrFamily = (deviceType_ == QStringLiteral("rtlsdr")
                                     || deviceType_ == QStringLiteral("rtltcp"));
        const bool noUiGain = (deviceType_ == QStringLiteral("sdrplay"));
        if (cfgNow.agc() || noUiGain) {
            uiGain = 1.0f;
        }
        // Boost fixo de +2,8 dB quando AGC ON (sobe nível médio de 29,8 → 32,6 dB).
        if (cfgNow.agc() && isRtlsdrFamily) {
            uiGain *= 1.380f; // +2,8 dB
        }
        // Boost fixo de +18 dB em HF (Q-on) para compensar bypass do tuner.
        if (isDirectSampling && isRtlsdrFamily) {
            uiGain *= 7.943f; // +18 dB
        }
        // Boost fixo de +1 dB SOMENTE no modo Q Off (quadrature desligado = VHF/UHF).
        // Vale em manual e em AGC (aplicado por cima do uiGain, apos o reset de AGC).
        // O modo Q On (quadrature ligado / HF / direct sampling) NAO e alterado.
        if (!isDirectSampling && isRtlsdrFamily) {
            uiGain *= 1.259f; // +2 dB (1 dB inicial + 1 dB extra), somente no modo Q Off
        }

        std::vector<std::complex<float>> work(iq, iq + n);

        // Bloqueia o DC Offset com filtro IIR para eliminar o LO Leakage / center spike.
        // EXCEÇÃO — SDRplay: o hardware já faz correção de DC (dcOffset.DCenable = 1).
        // Aplicar o bloqueador de DC em software no SDRplay remove a portadora AM
        // (que está em DC no modo Zero-IF), causando ausência total de áudio AM ou
        // distorção severa (envelope sem portadora → detector produz frequência dobrada).
        // Em modo Low-IF o mesmo ocorre após o DDC interno da API. Por isso o bloqueador
        // de DC em software é desativado para o SDRplay.
        const bool usaBloqueadorDc = (deviceType_ != QStringLiteral("sdrplay"));

        // ---------------------------------------------------------------------
        //  CAMINHOS SEPARADOS PARA A TELA E PARA O ÁUDIO
        //
        //  O bloqueador acima existe por causa da TELA: sem ele aparece o risco
        //  do LO no meio da cachoeira. Só que o mesmo buffer alimentava também o
        //  demodulador, e aí a observação do comentário do SDRplay vale para
        //  QUALQUER rádio — inclusive o RTL-SDR: se o sinal sintonizado cair em
        //  cima do centro, a portadora dele está em DC e o filtro a apaga.
        //
        //  Medido no próprio IirDcBlock, a 1,024 Msps: uma portadora AM exatamente
        //  no centro perde 35 dB. A 1 kHz do centro perde 0,1 dB. Por isso o botão
        //  ">.<", que põe o VFO exatamente no centro, fazia a estação sumir — e um
        //  arrasto de nada na cachoeira a trazia de volta.
        //
        //  A saída não é escolher entre risco na tela e áudio: é usar cada coisa
        //  onde ela serve. A tela continua com o bloqueador SEMPRE. O demodulador
        //  recebe o sinal sem filtrar apenas quando o VFO está perto do centro,
        //  que é o único caso em que o filtro atrapalha. Fora dessa faixa nada
        //  muda em relação ao que já funcionava.
        //
        //  O limite é generoso de propósito: o corte do filtro fica em
        //  fs/6283 (163 Hz a 1,024 Msps), e fs/500 é umas doze vezes isso.
        // ---------------------------------------------------------------------
        const int64_t desvioVfo = static_cast<int64_t>(vfoHz)
                                - static_cast<int64_t>(dev->centerFreq());
        const bool vfoSobreODc = usaBloqueadorDc
            && std::llabs(desvioVfo) < static_cast<int64_t>(dev->sampleRate() / 500.0);

        // Só existe quando o demodulador vai precisar dela - fora disso não se
        // paga a cópia.
        std::vector<std::complex<float>> semBloqueioDc;
        if (vfoSobreODc) semBloqueioDc.assign(iq, iq + n);

        if (usaBloqueadorDc) {
            for (size_t i = 0; i < n; ++i) {
                work[i] = dcBlock_.process(work[i]);
            }
        }

        // Aplica ganho digital fixo (estável, sem AGC automático que causaria
        // variações de brilho na cachoeira ao sintonizar/arrastar)
        if (uiGain != 1.0f) {
            for (size_t i = 0; i < n; ++i) {
                work[i] *= uiGain;
            }
            for (size_t i = 0; i < semBloqueioDc.size(); ++i) {
                semBloqueioDc[i] *= uiGain;
            }
        }

        const std::complex<float>* src = work.data();
        const std::complex<float>* srcDemod = vfoSobreODc ? semBloqueioDc.data()
                                                          : work.data();


        // Encontra o pico de potência máxima linearmente (otimização de CPU crítica)
        float maxPower = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float p = src[i].real() * src[i].real() + src[i].imag() * src[i].imag();
            if (p > maxPower) {
                maxPower = p;
            }
        }
        const float peak = 10.f * std::log10(maxPower + 1e-12f);
        peakDb_.store(peak);

        // Acumula a potência de TODOS os blocos que chegam, como o
        // LogAveragePower do OpenWebRX+. Antes só um bloco a cada ~6 era
        // usado (o resto era descartado) e cada linha da cachoeira vinha de
        // um único espectro instantâneo — daí o piso de ruído tremer tanto.
        {
            std::lock_guard<std::mutex> lk(fftMutex_);
            if (fft_) {
                fft_->accumulate(src, n);
            }
        }

        // Limita a transmissão WebSocket para ~25 Hz (cerca de 40ms):
        // envia a MÉDIA dos espectros somados durante a janela.
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFftTime_).count() >= 40) {
            lastFftTime_ = now;
            std::vector<int8_t> bins;
            {
                std::lock_guard<std::mutex> lk(fftMutex_);
                if (fft_) {
                    bins = fft_->hasAccumulated() ? fft_->takeAverageDbfs()
                                                  : fft_->computeDbfs(src, n);
                }
            }
            if (!bins.empty() && ws_) {
                ws_->broadcastFftThreadSafe(bins, dev->centerFreq(), dev->sampleRate());
            }
        }

        // Para o demodulador, aplica o deslocamento complexo de frequência (Software VFO / DDC offset tuning)
        std::vector<std::complex<float>> demodWork;
        const std::complex<float>* demodSrc = srcDemod;

        {
            // Usa vfoHz capturado atomicamente no inicio do bloco (nao freqA_ diretamente)
            const int64_t fOffset = static_cast<int64_t>(vfoHz) - static_cast<int64_t>(dev->centerFreq());
            if (fOffset != 0) {
                demodWork.resize(n);
                const double fs = dev->sampleRate();
                const double phase_step = -2.0 * M_PI * static_cast<double>(fOffset) / fs;
                
                const float cosS = std::cos(static_cast<float>(phase_step));
                const float sinS = std::sin(static_cast<float>(phase_step));
                const std::complex<float> step(cosS, sinS);
                
                float cosA = std::cos(static_cast<float>(phaseAcc_));
                float sinA = std::sin(static_cast<float>(phaseAcc_));
                std::complex<float> nco(cosA, sinA);

                for (size_t i = 0; i < n; ++i) {
                    // srcDemod, nao src: quando o VFO esta a poucas centenas de
                    // hertz do centro este ramo roda (fOffset != 0) e precisa do
                    // sinal SEM o bloqueador de DC, senao a correcao valeria so
                    // no centro exato e falharia justamente na borda do notch.
                    demodWork[i] = srcDemod[i] * nco;
                    nco *= step;
                    
                    // Renormaliza a cada 1024 amostras para evitar deriva numerica
                    if ((i & 1023) == 0) {
                        float lenSq = nco.real() * nco.real() + nco.imag() * nco.imag();
                        if (std::abs(lenSq - 1.0f) > 1e-4f) {
                            float len = std::sqrt(lenSq);
                            nco /= len;
                        }
                    }
                }
                
                // Atualiza o acumulador de fase para a proxima iteracao/bloco
                phaseAcc_ += phase_step * n;
                // Envolve phaseAcc_ dentro de [-M_PI, M_PI]
                phaseAcc_ = std::fmod(phaseAcc_ + M_PI, 2.0 * M_PI);
                if (phaseAcc_ < 0.0) phaseAcc_ += 2.0 * M_PI;
                phaseAcc_ -= M_PI;
                
                demodSrc = demodWork.data();
            }
        }

        // AIS agora usa feedAudio no handleAudioCallback (igual ao ACARS/Packet)

        // TETRA: alimenta IQ ja centralizado no VFO (antes do demod FM/SSB/AM).
        // O TetraManager decima para 36 kS/s e envia para o runner Python.
        if (tetraDeco_ && tetraDeco_->state() == TetraManager::State::Running) {
            tetraDeco_->feedIQ(demodSrc, n, dev->sampleRate());
        }

        // AIS-catcher tambem quer IQ, nao audio (ver feedIQ no manager).
        if (aisCatcher_ && aisCatcher_->state() == AisCatcherManager::State::Running) {
            aisCatcher_->feedIQ(demodSrc, n, dev->sampleRate());
        }

        // CRITICO: lock obrigatorio - onTune (thread HTTP) pode trocar demod_
        {
            std::lock_guard<std::mutex> lk(demodMutex_);
            if (demod_) {
                bool digitalActive = (dsdDeco_ && dsdDeco_->state() == DsdManager::State::Running)
                                  || (aisCatcher_ && aisCatcher_->state() == AisCatcherManager::State::Running);
                demod_->setIsDigital(digitalActive);
                demod_->process(demodSrc, n, dev->sampleRate());
            }
        }
    });
}

// ---------------------------------------------------------------------------
//  enviarAudioAoNavegador - unico portao de saida do audio para a tela
//
//  Converte para 48000 Hz exatos. Antes cada bloco chegava ao navegador em
//  51200 (ou 50000) Hz e ERA O NAVEGADOR quem reamostrava - um bloco de cada
//  vez, sem memoria entre eles. Media: 49 emendas quebradas e 2 ms de atraso
//  acumulado a cada 2 segundos de audio.
//
//  Os decodificadores nao passam por aqui de proposito: eles ja reamostram
//  para a taxa que precisam, e uma interpolacao extra so somaria erro.
// ---------------------------------------------------------------------------
void Application::enviarAudioAoNavegador(const std::vector<int16_t>& pcm, uint32_t sps)
{
    if (!ws_ || pcm.empty() || sps == 0) return;

    // ---- contabilidade: este lado produz em tempo real? -------------------
    // O tempo esperado NAO pode ser o relogio desde o inicio.
    //
    // A primeira versao fazia isso, e por isso acusava perda onde nao havia:
    // com o radio DESLIGADO o relogio corre e o audio nao e produzido, entao
    // cada pausa do usuario entrava na conta como se fosse audio perdido.
    // Testando com varios liga-desliga, a "perda" chegou a 5 segundos sem que
    // uma unica amostra tivesse sido descartada.
    //
    // Agora somamos apenas os intervalos em que o audio estava FLUINDO: vaos
    // acima de 2 s significam radio parado, nao perda, e ficam de fora.
    const int64_t agora = QDateTime::currentMSecsSinceEpoch();
    if (audioT0_.load() == 0) { audioT0_.store(agora); audioUltimo_.store(agora); }
    else {
        const int vao = int(agora - audioUltimo_.exchange(agora));
        if (vao > audioMaiorVaoMs_.load()) audioMaiorVaoMs_.store(vao);
        if (vao > 150) audioVaosGrandes_.fetch_add(1);
        if (vao > 0 && vao < 2000) audioEsperadoMs_.fetch_add(vao);
    }

    // Nao envia: ENFILEIRA. Quem entrega e o temporizador, em ritmo constante.
    std::lock_guard<std::mutex> lk(audioFilaMutex_);

    if (sps == 48000) {                      // ja esta certo: passa direto
        audioAmostras_.fetch_add(int64_t(pcm.size()));
        audioFila48k_.insert(audioFila48k_.end(), pcm.begin(), pcm.end());
    } else {
        audioSaida48k_.clear();
        audioResampler_.processa(pcm.data(), pcm.size(), sps, audioSaida48k_);
        if (!audioSaida48k_.empty()) {
            audioAmostras_.fetch_add(int64_t(audioSaida48k_.size()));
            audioFila48k_.insert(audioFila48k_.end(),
                                 audioSaida48k_.begin(), audioSaida48k_.end());
        }
    }

    // Teto de 3 s. So chega aqui se o temporizador tiver parado de rodar - e
    // nesse caso guardar mais nao ajuda ninguem, so atrasa.
    while (audioFila48k_.size() > 48000 * 3) audioFila48k_.pop_front();
}

// ---------------------------------------------------------------------------
//  entregarAudioRitmado - chamado pelo temporizador, a cada 20 ms
//
//  Manda so o que couber no tempo que passou desde a ultima vez. Assim uma
//  rajada de IQ que produza 200 ms de audio de uma vez sai espalhada por 200
//  ms de relogio, e o navegador recebe um fluxo em vez de solavancos.
// ---------------------------------------------------------------------------
void Application::entregarAudioRitmado()
{
    if (!ws_) return;
    const int64_t agora = QDateTime::currentMSecsSinceEpoch();
    if (audioPaceUltimo_ == 0) { audioPaceUltimo_ = agora; return; }

    int64_t dt = agora - audioPaceUltimo_;
    audioPaceUltimo_ = agora;
    if (dt <= 0) return;
    // Se o programa ficou parado (janela minimizada, travada longa), nao
    // despeja tudo de uma vez - seria trocar uma rajada por outra.
    if (dt > 500) dt = 500;

    std::vector<int16_t> saida;
    {
        std::lock_guard<std::mutex> lk(audioFilaMutex_);
        if (audioFila48k_.empty()) return;

        size_t querem = size_t(dt * 48);          // 48 amostras por ms
        // Fila crescendo: anda 50% mais rapido para devolver o atraso. Meio
        // por cento de diferenca de ritmo e inaudivel; o atraso acumulado nao.
        if (audioFila48k_.size() > 48000) querem += querem / 2;
        if (querem > audioFila48k_.size()) querem = audioFila48k_.size();
        if (querem == 0) return;

        saida.assign(audioFila48k_.begin(), audioFila48k_.begin() + long(querem));
        audioFila48k_.erase(audioFila48k_.begin(), audioFila48k_.begin() + long(querem));
    }
    ws_->broadcastAudioThreadSafe(saida, 48000);
}

void Application::handleAudioCallback(const std::vector<int16_t>& pcm, uint32_t sps)
{
    if (pcm.empty()) return;

    std::lock_guard<std::mutex> lk(audioBufferMutex_);
    audioBuffer_.insert(audioBuffer_.end(), pcm.begin(), pcm.end());

    while (audioBuffer_.size() >= 2048) {
        std::vector<int16_t> chunk(audioBuffer_.begin(), audioBuffer_.begin() + 2048);
        audioBuffer_.erase(audioBuffer_.begin(), audioBuffer_.begin() + 2048);
        
        bool dsdActive = dsdDeco_ && (dsdDeco_->state() == DsdManager::State::Running);
        // Muta o audio bruto sempre que o painel TETRA esta aberto (estado !=
        // Stopped): nao sai chiado mesmo se o runner ainda nao subiu ou falhou.
        // Ao fechar o painel (stop -> Stopped) o audio analogico volta.
        bool tetraActive = tetraDeco_ && (tetraDeco_->state() != TetraManager::State::Stopped);
        if (dsdActive) {
            dsdDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        } else if (tetraActive) {
            // Muta o audio bruto de VFO (estatica NFM) para nao incomodar,
            // ja que o audio decodificado entra via decodedAudioReady
        } else {
            if (ws_) {
                enviarAudioAoNavegador(chunk, sps);
            }
        }

        if (acarsDeco_ && acarsDeco_->state() == AcarsDecoManager::State::Running) {
            acarsDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        if (aprsDeco_ && aprsDeco_->state() == AprsManager::State::Running) {
            aprsDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        if (sitorBDeco_ && sitorBDeco_->state() == SitorBManager::State::Running) {
            sitorBDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        if (pactorDeco_ && pactorDeco_->state() == PactorManager::State::Running) {
            pactorDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        if (dscDeco_ && dscDeco_->state() == DscManager::State::Running) {
            dscDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        if (analiseDeco_ && analiseDeco_->state() == AnaliseManager::State::Running) {
            analiseDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        if (cwDeco_ && cwDeco_->state() == CwManager::State::Running) {
            cwDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        if (selcalDeco_ && selcalDeco_->state() == SelcalManager::State::Running) {
            selcalDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
        // TETRA usa feedIQ (alimentado direto na thread do device callback);
        // mantemos feedAudio aqui como no-op por compatibilidade da chamada
        // generica feita sobre todos os decoders.
        if (tetraDeco_ && tetraDeco_->state() == TetraManager::State::Running) {
            tetraDeco_->feedAudio(chunk.data(), static_cast<int>(chunk.size()), sps);
        }
    }
}

} // namespace masdr
