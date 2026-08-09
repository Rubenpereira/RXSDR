#include "DsdManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostAddress>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdio>

#include <QStandardPaths>

namespace masdr {

DsdManager::DsdManager(QObject* parent)
    : QObject(parent)
{}

DsdManager::~DsdManager()
{
    stop();
}

void DsdManager::setBinaryPath(const QString& path) { binaryPath_ = path; }
void DsdManager::setInputDevice(int dev) { inputDevice_ = dev; }

QString DsdManager::defaultBinaryPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    return dir + QStringLiteral("/decoders/dsd-fme.exe");
#else
    return dir + QStringLiteral("/decoders/dsd-fme");
#endif
}

QString DsdManager::resolvedBinaryPath() const
{
    if (!binaryPath_.isEmpty()) return binaryPath_;

    const QString dir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    // Prioridade: dsd-fme.exe > dsd.exe > DSDPlus.exe
    for (const QString& name : { QStringLiteral("dsd-fme.exe"),
                                  QStringLiteral("dsd.exe"),
                                  QStringLiteral("DSDPlus.exe") }) {
        const QString p = dir + QStringLiteral("/decoders/") + name;
        if (QFileInfo::exists(p)) return p;
    }
#else
    // Prioridade: dsd-fme > dsd > DSDPlus
    for (const QString& name : { QStringLiteral("dsd-fme"),
                                  QStringLiteral("dsd"),
                                  QStringLiteral("DSDPlus") }) {
        const QString p = dir + QStringLiteral("/decoders/") + name;
        if (QFileInfo::exists(p)) return p;
    }
#endif

    // Tenta encontrar no PATH do sistema como fallback
#ifdef Q_OS_WIN
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("dsd-fme.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("dsd.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("DSDPlus.exe"));
#else
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("dsd-fme"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("dsd"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("DSDPlus"));
#endif
    if (!sysBin.isEmpty()) return sysBin;

    return defaultBinaryPath();
}

bool DsdManager::binaryExists() const
{
    return QFileInfo::exists(resolvedBinaryPath());
}

QString DsdManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject DsdManager::statusJson() const
{
    QJsonObject o;
    o["state"]          = stateString();
    o["binaryPresent"]  = binaryExists();
    o["inverted"]       = invertPolarity_;
    o["pcmHz"]          = m_udpVoicePcmHz;
    if (!lastError_.isEmpty())
        o["error"] = lastError_;
    return o;
}

bool DsdManager::start()
{
    if (state_ == State::Running || state_ == State::Starting) return true;

    const QString bin = resolvedBinaryPath();
    if (!QFileInfo::exists(bin)) {
#ifdef Q_OS_WIN
        lastError_ = QStringLiteral("Nenhum decodificador DSD encontrado em ./decoders/ (dsd-fme.exe, dsd.exe ou DSDPlus.exe).");
#else
        lastError_ = QStringLiteral("Nenhum decodificador DSD encontrado em ./decoders/ (dsd-fme, dsd ou DSDPlus).");
#endif
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    closeUdpAudioListener();

    m_udpSock = new QUdpSocket(this);
    if (!m_udpSock->bind(QHostAddress::LocalHost, 0, QAbstractSocket::ReuseAddressHint)) {
        lastError_ = QStringLiteral("Não foi possível reservar porta UDP local para áudio dsd-fme: %1")
                          .arg(m_udpSock->errorString());
        emit error(lastError_);
        closeUdpAudioListener();
        return false;
    }
    m_udpListenPort = m_udpSock->localPort();
    connect(m_udpSock, &QUdpSocket::readyRead, this, &DsdManager::pullUdpPackets);

    process_ = std::make_unique<QProcess>(this);
    connect(process_.get(), &QProcess::started,
            this, &DsdManager::onProcessStarted);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &DsdManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &DsdManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &DsdManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &DsdManager::onReadyReadStderr);

    // Diretório de trabalho = pasta do binário
    process_->setWorkingDirectory(QFileInfo(bin).absolutePath());

    // Monta argumentos
    QStringList args;
    const QString binName = QFileInfo(bin).fileName().toLower();
    const bool isDsdFme   = binName.contains(QStringLiteral("dsd-fme")) || binName == QStringLiteral("dsd.exe");

    if (isDsdFme) {
        const QString udpOut = QStringLiteral("udp:127.0.0.1:%1").arg(m_udpListenPort);
        // -fs: DMR BS/MS
        // -l: desliga filtragem de entrada DMR
        // -mg: otimizações GFSK
        // -V 3: TDMA voz sintetizada em TS1 e TS2
        // -i -: entrada via stdin
        // -o udp:127.0.0.1:<porta>: saída de áudio via UDP
        args << QStringLiteral("-fs") << QStringLiteral("-l") << QStringLiteral("-mg")
             << QStringLiteral("-V") << QStringLiteral("3") << QStringLiteral("-i")
             << QStringLiteral("-") << QStringLiteral("-o") << udpOut;

        if (invertPolarity_) {
            args << QStringLiteral("-P");   // inverter polaridade
        }
    } else {
        // Fallback para DSDPlus tradicional (requer VAC)
        if (inputDevice_ > 0) {
            args << QStringLiteral("-i%1").arg(inputDevice_);
        }
    }

#ifdef Q_OS_WIN
    // ── Suprime janelas de console pretas ──────────────────────────────────────
    process_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif

    state_ = State::Starting;
    emit stateChanged(state_);

    process_->start(bin, args);
    return true;
}

void DsdManager::stop()
{
    closeUdpAudioListener();
    m_stdinPending.clear();
    if (m_dumpFile) { std::fclose(static_cast<std::FILE*>(m_dumpFile)); m_dumpFile = nullptr; }
    if (!process_) return;

    process_->disconnect();
    process_->terminate();
    if (!process_->waitForFinished(4000)) {
        process_->kill();
        process_->waitForFinished(2000);
    }
    process_.reset();

    state_ = State::Stopped;
    emit stateChanged(state_);
}

void DsdManager::togglePolarity()
{
    invertPolarity_ = !invertPolarity_;
    if (state_ == State::Running) {
        stop();
        start();   // reinicia com polaridade invertida
    }
}

void DsdManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (!process_ || state_ != State::Running || count <= 0) return;

    if (sps == 48000) {
        QByteArray pcm;
        pcm.resize(count * 2);
        memcpy(pcm.data(), samples, count * 2);
        m_stdinPending.append(pcm);
        dumpFeed(pcm);
    } else {
        m_feedResampleTail.insert(m_feedResampleTail.end(), samples, samples + count);

        const double srcHz = double(sps);
        constexpr double dstHz = 48000.0;
        const double inPerOutput = srcHz / dstHz;

        std::vector<int16_t> resampledBlock;

        while (true) {
            const int i0 = int(std::floor(m_feedResamplePos));
            if (i0 + 1 >= int(m_feedResampleTail.size()))
                break;
            double frac = m_feedResamplePos - double(i0);
            double y0 = m_feedResampleTail[i0];
            double y1 = m_feedResampleTail[i0 + 1];
            double sample = y0 + frac * (y1 - y0);

            sample = std::clamp(sample, -32768.0, 32767.0);
            resampledBlock.push_back(static_cast<int16_t>(std::lround(sample)));

            m_feedResamplePos += inPerOutput;
        }

        const int next_i0 = int(std::floor(m_feedResamplePos));
        const int numConsumed = std::min(next_i0, int(m_feedResampleTail.size()) - 1);
        if (numConsumed > 0) {
            m_feedResampleTail.erase(m_feedResampleTail.begin(), m_feedResampleTail.begin() + numConsumed);
            m_feedResamplePos -= double(numConsumed);
        }

        if (!resampledBlock.empty()) {
            QByteArray pcm;
            pcm.resize(static_cast<int>(resampledBlock.size() * 2));
            memcpy(pcm.data(), resampledBlock.data(), resampledBlock.size() * 2);
            m_stdinPending.append(pcm);
        dumpFeed(pcm);
        }
    }

    // Evita backlog infinito
    static constexpr int kMaxStdinBacklog = 8 * 1024 * 1024;
    if (m_stdinPending.size() > kMaxStdinBacklog) {
        lastError_ = QStringLiteral("Backlog stdin dsd-fme > 8 MB — o processo provavelmente não está a ler (-i -).");
        emit error(lastError_);
        m_stdinPending.clear();
        stop();
        return;
    }

    int stall = 0;
    while (!m_stdinPending.isEmpty()) {
        const qint64 w = process_->write(m_stdinPending.constData(), m_stdinPending.size());
        if (w < 0) {
            lastError_ = QStringLiteral("Escrita stdin dsd-fme falhou: %1").arg(process_->errorString());
            emit error(lastError_);
            m_stdinPending.clear();
            stop();
            return;
        }
        if (w > 0) {
            m_stdinPending.remove(0, int(w));
            stall = 0;
        }
        // IMPORTANTE: forca o envio sincrono ao pipe do dsd-fme. O QProcess foi
        // criado na thread principal mas e alimentado pela thread de audio; sem
        // este flush o buffer NAO e drenado para o pipe e o dsd-fme fica sem
        // audio (so recebe o burst inicial). waitForBytesWritten escreve
        // imediatamente no descritor (equivale ao flush() do RKSDR em Python).
        if (!process_->waitForBytesWritten(100)) {
            if (++stall >= 3)
                break;
        }
    }
}

void DsdManager::closeUdpAudioListener()
{
    m_udpListenPort = 0;
    flushUdpResampleState();
    m_feedResampleTail.clear();
    m_feedResamplePos = 0.0;
    if (!m_udpSock) return;
    m_udpSock->disconnect();
    m_udpSock->close();
    delete m_udpSock;
    m_udpSock = nullptr;
}

void DsdManager::flushUdpResampleState()
{
    m_udpResampleTail.clear();
    m_udpResampleInPos = 0.0;
}

void DsdManager::pullUdpPackets()
{
    if (!m_udpSock) return;
    while (m_udpSock->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_udpSock->receiveDatagram();
        QByteArray data = dg.data();
        if (data.isEmpty()) continue;

        const int nbytes = data.size();
        if (nbytes < 2 || (nbytes % 2) != 0) continue;
        const int ns = nbytes / 2;
        QVector<float> out(ns);
        const auto* ps = reinterpret_cast<const qint16*>(data.constData());
        for (int i = 0; i < ns; ++i) {
            out[i] = float(ps[i]) * (1.0f / 32768.0f);
        }
        appendUdpVoiceResampleTo48k(std::move(out));
    }
}

void DsdManager::appendUdpVoiceResampleTo48k(QVector<float> monoChunk)
{
    if (monoChunk.isEmpty()) return;
    m_udpResampleTail.append(std::move(monoChunk));

    const double srcHz = double(std::clamp(m_udpVoicePcmHz, 4000, 96000));
    constexpr double dstHz = 48000.0;
    const double inPerOutput = srcHz / dstHz;

    std::vector<int16_t> outBlock;

    while (int(m_udpResampleTail.size()) >= 2) {
        const int i0 = int(std::floor(m_udpResampleInPos));
        if (i0 + 1 >= int(m_udpResampleTail.size()))
            break;
        float frac = float(m_udpResampleInPos - double(i0));
        float y0 = m_udpResampleTail[i0];
        float y1 = m_udpResampleTail[i0 + 1];
        float sample = y0 + frac * (y1 - y0);

        // Ganho de volume do DSD (a voz decodificada saia um pouco baixa).
        sample *= 2.5f;

        // Converte para int16_t (o clamp evita estouro/distorcao no pico)
        const float c = std::clamp(sample, -1.0f, 1.0f);
        outBlock.push_back(static_cast<int16_t>(std::lround(double(c) * 32767.0)));

        m_udpResampleInPos += inPerOutput;

        while (std::floor(m_udpResampleInPos) >= 1.0) {
            const int dn = std::min(static_cast<int>(std::floor(m_udpResampleInPos)),
                                   static_cast<int>(m_udpResampleTail.size()) - 1);
            if (dn < 1)
                break;
            m_udpResampleTail.remove(0, dn);
            m_udpResampleInPos -= double(dn);
        }
    }

    if (!outBlock.empty()) {
        emit decodedAudioReady(outBlock, 48000);
    }
}

void DsdManager::onProcessStarted()
{
    state_ = State::Running;
    lastError_.clear();
    emit stateChanged(state_);

    const QString name = QFileInfo(resolvedBinaryPath()).fileName();
    emit logLine(QStringLiteral("[%1] iniciado%2 — monitorando via pipeline interno e UDP")
                 .arg(name)
                 .arg(invertPolarity_ ? QStringLiteral(" (polaridade invertida)") : QString()));
}

void DsdManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (state_ != State::Stopped) {
        const QString name = QFileInfo(resolvedBinaryPath()).fileName();
        lastError_ = QStringLiteral("%1 encerrou inesperadamente (código %2)").arg(name).arg(exitCode);
        state_ = State::Error;
        emit error(lastError_);
    }
    emit stateChanged(state_);
}

void DsdManager::onProcessError(QProcess::ProcessError err)
{
    Q_UNUSED(err)
    const QString name = QFileInfo(resolvedBinaryPath()).fileName();
    lastError_ = QStringLiteral("Erro de processo do %1: %2").arg(name).arg(process_ ? process_->errorString() : QString());
    state_ = State::Error;
    emit error(lastError_);
    emit stateChanged(state_);
}

void DsdManager::onReadyReadStdout()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardOutput();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines)
        emit logLine(QStringLiteral("[DSD] ") + line.trimmed());
}

void DsdManager::onReadyReadStderr()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardError();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines)
        emit logLine(QStringLiteral("[DSD-ERR] ") + line.trimmed());
}

void DsdManager::dumpFeed(const QByteArray& pcm)
{
    // Diagnostico: se /tmp/dsd_dump existir, grava o PCM 48k/mono/16-bit
    // EXATAMENTE como enviado ao dsd-fme em /tmp/dsd_feed.s16 (raw int16 LE).
    if (!m_dumpFile) {
        if (!QFileInfo::exists(QStringLiteral("/tmp/dsd_dump"))) return;
        m_dumpFile = std::fopen("/tmp/dsd_feed.s16", "wb");
        if (!m_dumpFile) return;
    }
    std::fwrite(pcm.constData(), 1, static_cast<size_t>(pcm.size()),
                static_cast<std::FILE*>(m_dumpFile));
    std::fflush(static_cast<std::FILE*>(m_dumpFile));
}

} // namespace masdr
