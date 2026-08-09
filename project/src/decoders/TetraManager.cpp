#include "TetraManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostAddress>
#include <QProcessEnvironment>
#include <cmath>
#include <algorithm>

#include <QStandardPaths>

namespace masdr {

TetraManager::TetraManager(QObject* parent)
    : QObject(parent)
{}

TetraManager::~TetraManager()
{
    stop();
}

void TetraManager::setBinaryPath(const QString& path) { binaryPath_ = path; }

QString TetraManager::defaultBinaryPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString batPath = dir + QStringLiteral("/decoders/tetra.bat");
    if (QFileInfo::exists(batPath)) return batPath;
    return dir + QStringLiteral("/decoders/tetra.exe");
#else
    const QString shPath = dir + QStringLiteral("/decoders/tetra");
    if (QFileInfo::exists(shPath)) return shPath;
    return dir + QStringLiteral("/decoders/tetra");
#endif
}

QString TetraManager::resolvedBinaryPath() const
{
    if (!binaryPath_.isEmpty()) return binaryPath_;
    
    const QString defaultPath = defaultBinaryPath();
    if (QFileInfo::exists(defaultPath)) return defaultPath;

    // Tenta encontrar no PATH do sistema como fallback
#ifdef Q_OS_WIN
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("tetra.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("tetra.bat"));
#else
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("tetra"));
#endif
    if (!sysBin.isEmpty()) return sysBin;

    return defaultPath;
}

QString TetraManager::osmoTetraPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    for (const QString& name : { QStringLiteral("tetra-rx.exe"),
                                  QStringLiteral("osmo-tetra-rx.exe") }) {
        const QString p = dir + QStringLiteral("/decoders/") + name;
        if (QFileInfo::exists(p)) return p;
    }
#else
    for (const QString& name : { QStringLiteral("tetra-rx"),
                                  QStringLiteral("osmo-tetra-rx") }) {
        const QString p = dir + QStringLiteral("/decoders/") + name;
        if (QFileInfo::exists(p)) return p;
    }
    // Fallback: tenta buscar no PATH do sistema
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("tetra-rx"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("osmo-tetra-rx"));
    if (!sysBin.isEmpty()) return sysBin;
#endif
    return QString();
}

QString TetraManager::acelpDecoderPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    for (const QString& name : { QStringLiteral("cdecoder.exe"),
                                  QStringLiteral("tetra-decoder.exe"),
                                  QStringLiteral("acelp-decoder.exe") }) {
        const QString p = dir + QStringLiteral("/decoders/") + name;
        if (QFileInfo::exists(p)) return p;
    }
#else
    for (const QString& name : { QStringLiteral("cdecoder"),
                                  QStringLiteral("tetra-decoder"),
                                  QStringLiteral("acelp-decoder") }) {
        const QString p = dir + QStringLiteral("/decoders/") + name;
        if (QFileInfo::exists(p)) return p;
    }
    // Fallback: tenta buscar no PATH do sistema
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("cdecoder"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("tetra-decoder"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("acelp-decoder"));
    if (!sysBin.isEmpty()) return sysBin;
#endif
    return QString();
}

bool TetraManager::binaryExists() const
{
    return QFileInfo::exists(resolvedBinaryPath());
}

bool TetraManager::osmoTetraExists() const
{
    return !osmoTetraPath().isEmpty();
}

bool TetraManager::acelpDecoderExists() const
{
    return !acelpDecoderPath().isEmpty();
}

QString TetraManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject TetraManager::statusJson() const
{
    QJsonObject o;
    o["state"]            = stateString();
    o["binaryPresent"]    = binaryExists();
    o["osmoTetraPresent"] = osmoTetraExists();
    o["acelpPresent"]     = acelpDecoderExists();
    o["symbolRate"]       = params_.symbolRate;
    o["sdrppPort"]        = params_.sdrppPort;
    o["invertIQ"]         = params_.invertIQ;
    o["udpAudioPort"]     = int(m_udpListenPort);
    o["voiceFramesRx"]    = voiceFramesRx_;
    o["mode"]             = (osmoTetraExists() && acelpDecoderExists())
                              ? QStringLiteral("wrapper")
                              : QStringLiteral("detector");
    if (!lastError_.isEmpty())
        o["error"] = lastError_;
    return o;
}

bool TetraManager::start()
{
    if (state_ == State::Running || state_ == State::Starting) return true;

    const QString bin = resolvedBinaryPath();
    if (!QFileInfo::exists(bin)) {
#ifdef Q_OS_WIN
        lastError_ = QStringLiteral("Runner TETRA nao encontrado em ./decoders/ (procurou tetra.bat, tetra.exe).");
#else
        lastError_ = QStringLiteral("Runner TETRA nao encontrado em ./decoders/ (procurou tetra).");
#endif
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    // Abre socket UDP para receber PCM decodificado do runner
    closeUdpAudioListener();
    m_udpSock = new QUdpSocket(this);
    if (!m_udpSock->bind(QHostAddress::LocalHost, 0, QAbstractSocket::ReuseAddressHint)) {
        lastError_ = QStringLiteral("Nao foi possivel reservar porta UDP para audio TETRA: %1")
                         .arg(m_udpSock->errorString());
        emit error(lastError_);
        closeUdpAudioListener();
        return false;
    }
    m_udpListenPort = m_udpSock->localPort();
    connect(m_udpSock, &QUdpSocket::readyRead, this, &TetraManager::pullUdpPackets);

    voiceFramesRx_ = 0;

    // Reset decimation + anti-aliasing state
    m_iqDecimTail.clear();
    m_iqDecimPos = 0.0;
    m_iqStdinPending.clear();
    m_aaRing.clear();
    m_aaSum = {0.0, 0.0};
    m_aaIdx = 0;
    m_aaLen = 0;
    m_aaSps = 0;

    process_ = std::make_unique<QProcess>(this);
    connect(process_.get(), &QProcess::started,
            this, &TetraManager::onProcessStarted);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &TetraManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &TetraManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &TetraManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &TetraManager::onReadyReadStderr);

    process_->setWorkingDirectory(QFileInfo(bin).absolutePath());

    // Configura env do runner
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("TETRA_AUDIO_UDP_PORT", QString::number(m_udpListenPort));
    env.insert("TETRA_SDRPP_PORT",     QString::number(params_.sdrppPort));
    env.insert("TETRA_INVERT_IQ",      params_.invertIQ ? QStringLiteral("1") : QStringLiteral("0"));
    if (osmoTetraExists())     env.insert("TETRA_OSMO_BIN",  osmoTetraPath());
    if (acelpDecoderExists())  env.insert("TETRA_ACELP_BIN", acelpDecoderPath());
    process_->setProcessEnvironment(env);

    // tetra_decoder.py nao recebe argumentos; toda a configuracao vai por env.
    QStringList args;

#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif

    state_ = State::Starting;
    emit stateChanged(state_);

    process_->start(bin, args);
    return true;
}

void TetraManager::stop()
{
    closeUdpAudioListener();
    if (!process_) {
        state_ = State::Stopped;
        emit stateChanged(state_);
        return;
    }

    process_->disconnect();
    process_->terminate();
    if (!process_->waitForFinished(2000)) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    process_.reset();

    state_ = State::Stopped;
    emit stateChanged(state_);
}

void TetraManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    // No fluxo TETRA voz nativo (wrapper Python local) o que vai pro runner
    // e IQ via feedIQ(), nao PCM. Mantido como no-op.
    Q_UNUSED(samples);
    Q_UNUSED(count);
    Q_UNUSED(sps);
}

void TetraManager::feedIQ(const std::complex<float>* iq, size_t count, uint32_t sps)
{
    if (!process_ || state_ != State::Running || !iq || count == 0) return;
    if (sps == 0) return;

    // ── Anti-aliasing boxcar (moving-average) filter ──────────────────────
    // Decimação de sps (~2 MSPS) para 36 kS/s é uma razão de ~57:1.
    // Sem filtro, todo o ruído dos 2 MHz dobra para dentro dos 36 kHz,
    // destruindo o SNR e impedindo o FLL de travar.
    //
    // Solução: média móvel de comprimento M = round(sps / 36000).
    // Resposta em freq = sinc(f·M/fs) → zero em ~36 kHz (taxa de saída),
    // eliminando o alias dominante. Custo: ~2 somas complexas por amostra.
    const size_t M = std::max(size_t(1),
                              size_t(std::round(double(sps) / double(kTetraIqRate))));
    if (sps != m_aaSps || M != m_aaLen) {
        m_aaLen = M;
        m_aaRing.assign(M, {0.0f, 0.0f});
        m_aaSum = {0.0, 0.0};
        m_aaIdx = 0;
        m_aaSps = sps;
        m_lpFilter.setCutoff(14000.0f, float(sps));
        m_lpFilter.reset();
    }

    // Aplica LPF IIR + boxcar e armazena resultado filtrado
    const double invM = 1.0 / double(M);
    std::vector<std::complex<float>> filtered(count);
    for (size_t i = 0; i < count; ++i) {
        // Filtra passa-baixas para evitar aliasing
        std::complex<float> val = m_lpFilter.process(iq[i]);
        
        // Soma corrida: acumula nova amostra, remove a mais antiga do ring
        m_aaSum += std::complex<double>(val.real(), val.imag())
                 - std::complex<double>(m_aaRing[m_aaIdx].real(),
                                        m_aaRing[m_aaIdx].imag());
        m_aaRing[m_aaIdx] = val;
        m_aaIdx = (m_aaIdx + 1) % M;
        filtered[i] = std::complex<float>(
            static_cast<float>(m_aaSum.real() * invM),
            static_cast<float>(m_aaSum.imag() * invM)
        );
    }

    // ── Decimação para kTetraIqRate (36000) via interpolação linear ───────
    const double srcHz = double(sps);
    const double dstHz = double(kTetraIqRate);
    const double inPerOut = srcHz / dstHz;
    if (inPerOut <= 0.0) return;

    m_iqDecimTail.insert(m_iqDecimTail.end(), filtered.begin(), filtered.end());

    std::vector<std::complex<float>> out;
    out.reserve(size_t(count / inPerOut) + 4);

    while (true) {
        const int i0 = int(std::floor(m_iqDecimPos));
        if (i0 + 1 >= int(m_iqDecimTail.size())) break;
        const float frac = float(m_iqDecimPos - double(i0));
        const std::complex<float>& a = m_iqDecimTail[i0];
        const std::complex<float>& b = m_iqDecimTail[i0 + 1];
        out.emplace_back(a + (b - a) * frac);
        m_iqDecimPos += inPerOut;
    }

    const int consumed = std::min(int(std::floor(m_iqDecimPos)),
                                   int(m_iqDecimTail.size()) - 1);
    if (consumed > 0) {
        m_iqDecimTail.erase(m_iqDecimTail.begin(),
                            m_iqDecimTail.begin() + consumed);
        m_iqDecimPos -= double(consumed);
    }

    if (out.empty()) return;

    // Append PCM (complex float32) to stdin backlog
    const int bytes = int(out.size() * sizeof(std::complex<float>));
    m_iqStdinPending.append(reinterpret_cast<const char*>(out.data()), bytes);

    // Cap backlog (~16 MB de IQ)
    static constexpr int kMaxIqBacklog = 16 * 1024 * 1024;
    if (m_iqStdinPending.size() > kMaxIqBacklog) {
        lastError_ = QStringLiteral("Backlog IQ TETRA muito grande");
        emit error(lastError_);
        m_iqStdinPending.clear();
        return;
    }

    // Escrita NAO bloqueante: nunca trava a thread de audio, mesmo que o runner
    // do TETRA pare de ler (evita CONGELAR o radio). O QProcess bufferiza e o
    // event loop principal faz o flush; se o runner nao consome, o backlog e
    // limitado acima e descartado. (NAO usar waitForBytesWritten aqui: bloquearia
    // a thread de audio quando o runner morre.)
    const qint64 w = process_->write(m_iqStdinPending.constData(),
                                      m_iqStdinPending.size());
    if (w < 0) {
        lastError_ = QStringLiteral("Escrita IQ stdin TETRA falhou: %1")
                          .arg(process_->errorString());
        emit error(lastError_);
        m_iqStdinPending.clear();
        return;
    }
    if (w > 0) m_iqStdinPending.remove(0, int(w));
}

void TetraManager::closeUdpAudioListener()
{
    m_udpListenPort = 0;
    m_udpResampleTail.clear();
    m_udpResampleInPos = 0.0;
    if (!m_udpSock) return;
    m_udpSock->disconnect();
    m_udpSock->close();
    delete m_udpSock;
    m_udpSock = nullptr;
}

void TetraManager::pullUdpPackets()
{
    if (!m_udpSock) return;
    while (m_udpSock->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_udpSock->receiveDatagram();
        QByteArray data = dg.data();
        if (data.isEmpty()) continue;

        const int nbytes = data.size();
        if (nbytes < 2 || (nbytes % 2) != 0) continue;
        const int ns = nbytes / 2;

        QVector<float> mono(ns);
        const auto* ps = reinterpret_cast<const qint16*>(data.constData());
        for (int i = 0; i < ns; ++i) {
            mono[i] = float(ps[i]) * (1.0f / 32768.0f);
        }

        voiceFramesRx_++;
        appendUdpVoiceResampleTo48k(std::move(mono), m_udpVoicePcmHz);
    }
}

void TetraManager::appendUdpVoiceResampleTo48k(QVector<float> monoChunk, int srcHz)
{
    if (monoChunk.isEmpty()) return;
    m_udpResampleTail.append(std::move(monoChunk));

    const double inHz  = double(std::clamp(srcHz, 4000, 96000));
    constexpr double dstHz = 48000.0;
    const double inPerOut = inHz / dstHz;

    std::vector<int16_t> outBlock;

    while (int(m_udpResampleTail.size()) >= 2) {
        const int i0 = int(std::floor(m_udpResampleInPos));
        if (i0 + 1 >= int(m_udpResampleTail.size())) break;
        float frac = float(m_udpResampleInPos - double(i0));
        float y0 = m_udpResampleTail[i0];
        float y1 = m_udpResampleTail[i0 + 1];
        float sample = y0 + frac * (y1 - y0);

        const float c = std::clamp(sample, -1.0f, 1.0f);
        outBlock.push_back(static_cast<int16_t>(std::lround(double(c) * 32767.0)));

        m_udpResampleInPos += inPerOut;

        while (std::floor(m_udpResampleInPos) >= 1.0) {
            const int dn = std::min(static_cast<int>(std::floor(m_udpResampleInPos)),
                                    static_cast<int>(m_udpResampleTail.size()) - 1);
            if (dn < 1) break;
            m_udpResampleTail.remove(0, dn);
            m_udpResampleInPos -= double(dn);
        }
    }

    if (!outBlock.empty()) {
        emit decodedAudioReady(outBlock, 48000);
    }
}

void TetraManager::onProcessStarted()
{
    state_ = State::Running;
    lastError_.clear();
    emit stateChanged(state_);
    const QString mode = (osmoTetraExists() && acelpDecoderExists())
                          ? QStringLiteral("WRAPPER osmo-tetra+ACELP")
                          : QStringLiteral("DETECTOR (heuristico)");
    emit logLine(QStringLiteral("[TETRA] runner iniciado | modo=%1 | audio UDP=%2 | SDR++ UDP=%3")
                 .arg(mode).arg(m_udpListenPort).arg(params_.sdrppPort));
}

void TetraManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (state_ != State::Stopped) {
        lastError_ = QStringLiteral("TETRA runner encerrou inesperadamente (codigo %1)").arg(exitCode);
        state_ = State::Error;
        emit error(lastError_);
    }
    emit stateChanged(state_);
}

void TetraManager::onProcessError(QProcess::ProcessError err)
{
    Q_UNUSED(err)
    lastError_ = QStringLiteral("Erro do runner TETRA: %1").arg(process_ ? process_->errorString() : QString());
    state_ = State::Error;
    emit error(lastError_);
    emit stateChanged(state_);
}

void TetraManager::onReadyReadStdout()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardOutput();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (!t.isEmpty()) emit logLine(t);
    }
}

void TetraManager::onReadyReadStderr()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardError();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (!t.isEmpty()) emit logLine(QStringLiteral("[TETRA-LOG] ") + t);
    }
}

} // namespace masdr
