#include "AcarsDecoManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#include <QStandardPaths>

namespace masdr {

namespace {
constexpr int kAcarsAudioRate = 48000;
constexpr int kAcarsRateMult = kAcarsAudioRate / 12000;
}

AcarsDecoManager::AcarsDecoManager(QObject* parent)
    : QObject(parent)
{}

AcarsDecoManager::~AcarsDecoManager()
{
    stop();
}

void AcarsDecoManager::setBinaryPath(const QString& path)  { binaryPath_   = path; }
void AcarsDecoManager::setWebPort(quint16 port)             { webPort_       = port; }
void AcarsDecoManager::setDeviceIndex(int index)            { deviceIndex_   = index; }
void AcarsDecoManager::setFrequencies(const QString& freqs) { freqs_         = freqs; }
void AcarsDecoManager::setExclusiveMode(bool exclusive)     { exclusive_     = exclusive; }

QString AcarsDecoManager::defaultBinaryPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    return dir + QStringLiteral("/decoders/acarsdec.exe");
#else
    return dir + QStringLiteral("/decoders/acarsdec");
#endif
}

QString AcarsDecoManager::resolvedBinaryPath() const
{
    if (!binaryPath_.isEmpty()) return binaryPath_;

    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QStringList candidates = {
        dir + QStringLiteral("/decoders/acarsdec.exe"),
        dir + QStringLiteral("/acarsdec.exe"),
        QDir::cleanPath(dir + QStringLiteral("/../acarsdec.exe")),
        dir + QStringLiteral("/decoders/acarsdeco2.exe")
    };
#else
    const QStringList candidates = {
        dir + QStringLiteral("/decoders/acarsdec"),
        dir + QStringLiteral("/acarsdec"),
        QDir::cleanPath(dir + QStringLiteral("/../acarsdec")),
        dir + QStringLiteral("/decoders/acarsdeco2")
    };
#endif
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }

    // Tenta encontrar no PATH do sistema como fallback
#ifdef Q_OS_WIN
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("acarsdec.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("acarsdeco2.exe"));
#else
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("acarsdec"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("acarsdeco2"));
#endif
    if (!sysBin.isEmpty()) return sysBin;

    return defaultBinaryPath();
}

QString AcarsDecoManager::webUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(webPort_);
}

void AcarsDecoManager::rememberProcessOutput(const QString& line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return;
    recentOutput_.append(trimmed);
    while (recentOutput_.size() > 12) {
        recentOutput_.removeFirst();
    }
}

QString AcarsDecoManager::recentProcessOutput() const
{
    return recentOutput_.join(QStringLiteral(" | "));
}

bool AcarsDecoManager::binaryExists() const
{
    return QFileInfo::exists(resolvedBinaryPath());
}

QString AcarsDecoManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject AcarsDecoManager::statusJson() const
{
    QJsonObject o;
    o["state"]       = stateString();
    o["webUrl"]      = webUrl();
    o["webPort"]     = webPort_;
    o["frequencies"] = freqs_;
    o["exclusive"]   = exclusive_;
    if (!lastError_.isEmpty())
        o["error"] = lastError_;
    return o;
}

bool AcarsDecoManager::start()
{
    if (state_ == State::Running || state_ == State::Starting) return true;

    const QString bin = resolvedBinaryPath();
    if (!QFileInfo::exists(bin)) {
#ifdef Q_OS_WIN
        lastError_ = QStringLiteral("acarsdec.exe nao encontrado em: %1").arg(bin);
#else
        lastError_ = QStringLiteral("acarsdec nao encontrado em: %1").arg(bin);
#endif
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    recentOutput_.clear();
    resetResampler();

    process_ = std::make_unique<QProcess>(this);
    connect(process_.get(), &QProcess::started,
            this, &AcarsDecoManager::onProcessStarted);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &AcarsDecoManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &AcarsDecoManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &AcarsDecoManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &AcarsDecoManager::onReadyReadStderr);

    process_->setWorkingDirectory(QFileInfo(bin).absolutePath());
    process_->setProcessChannelMode(QProcess::SeparateChannels);

    QStringList args;
    const QString fileName = QFileInfo(bin).fileName().toLower();
    if (fileName.contains(QStringLiteral("acarsdec")) && !fileName.contains(QStringLiteral("acarsdeco2"))) {
        QString freqMhz = QStringLiteral("131.550000");
        const QStringList parts = freqs_.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            bool ok = false;
            double f = parts.first().trimmed().toDouble(&ok);
            if (ok) {
                if (f > 1000000.0) f /= 1000000.0;
                freqMhz = QString::number(f, 'f', 6);
            }
        }
        args << QStringLiteral("--sndfile") << QStringLiteral("file=-,subtype=0x0002")
             << QStringLiteral("--output") << QStringLiteral("oneline:file:path=-")
             << QStringLiteral("-m") << QString::number(kAcarsRateMult)
             << freqMhz;
    } else {
        args << QStringLiteral("--device-index") << QString::number(deviceIndex_)
             << QStringLiteral("--http-port") << QString::number(webPort_);

        const QStringList parts = freqs_.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            const QString f = part.trimmed();
            if (!f.isEmpty()) args << QStringLiteral("--freq") << f;
        }
    }

    state_ = State::Starting;
    emit stateChanged(state_);

#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    process_->start(bin, args);
    return true;
}

void AcarsDecoManager::stop()
{
    if (!process_) return;

    state_ = State::Stopped;
    process_->closeWriteChannel();
    process_->terminate();
    if (!process_->waitForFinished(4000)) {
        process_->kill();
        process_->waitForFinished(2000);
    }
    process_.reset();
    resetResampler();

    emit stateChanged(state_);
}

void AcarsDecoManager::feedAudio(const int16_t* samples, int count, uint32_t sampleRate)
{
    if (!samples || count <= 0 || !process_ || state_ != State::Running) return;

    const QString fileName = QFileInfo(resolvedBinaryPath()).fileName().toLower();
    if (!fileName.contains(QStringLiteral("acarsdec")) || fileName.contains(QStringLiteral("acarsdeco2"))) {
        return;
    }

    QByteArray pcm;
    if (sampleRate == kAcarsAudioRate) {
        pcm.resize(count * static_cast<int>(sizeof(qint16)));
        std::copy_n(reinterpret_cast<const char*>(samples), pcm.size(), pcm.data());
    } else {
        std::vector<float> input;
        input.reserve(static_cast<size_t>(count) + 1);
        if (hasResampleLast_) input.push_back(resampleLast_);
        for (int i = 0; i < count; ++i) {
            input.push_back(static_cast<float>(samples[i]) / 32768.0f);
        }
        if (input.size() < 2 || sampleRate == 0) return;

        const double step = static_cast<double>(sampleRate) / static_cast<double>(kAcarsAudioRate);
        std::vector<qint16> out;
        double pos = resamplePos_;
        while (pos + 1.0 < static_cast<double>(input.size())) {
            const int idx = static_cast<int>(pos);
            const double frac = pos - static_cast<double>(idx);
            const float v = input[static_cast<size_t>(idx)] * static_cast<float>(1.0 - frac)
                          + input[static_cast<size_t>(idx + 1)] * static_cast<float>(frac);
            const float c = std::clamp(v, -1.0f, 1.0f);
            out.push_back(static_cast<qint16>(std::lround(c * 32767.0f)));
            pos += step;
        }

        hasResampleLast_ = true;
        resampleLast_ = input.back();
        resamplePos_ = pos - static_cast<double>(input.size() - 1);

        if (out.empty()) return;
        pcm.resize(static_cast<int>(out.size() * sizeof(qint16)));
        std::copy_n(reinterpret_cast<const char*>(out.data()), pcm.size(), pcm.data());
    }

    // Enfileira e pede a drenagem na thread dona do QProcess - ver o comentario
    // do slot no cabecalho.
    {
        QMutexLocker lk(&m_stdinMutex);
        m_stdinPending.append(pcm);
        static constexpr int kMaxBacklog = 8 * 1024 * 1024;
        if (m_stdinPending.size() > kMaxBacklog)
            m_stdinPending.remove(0, m_stdinPending.size() - kMaxBacklog);
    }
    QMetaObject::invokeMethod(this, "drenarStdin", Qt::QueuedConnection);
}

void AcarsDecoManager::drenarStdin()
{
    if (!process_ || state_ != State::Running) return;
    QMutexLocker lk(&m_stdinMutex);

    while (!m_stdinPending.isEmpty()) {
        const qint64 w = process_->write(m_stdinPending.constData(), m_stdinPending.size());
        if (w < 0) {
            lastError_ = QStringLiteral("Escrita stdin acarsdec falhou: %1").arg(process_->errorString());
            emit error(lastError_);
            m_stdinPending.clear();
            QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection);
            return;
        }
        if (w == 0) {
            if (!process_->waitForBytesWritten(10)) return;
            continue;
        }
        m_stdinPending.remove(0, int(w));
    }
}

void AcarsDecoManager::onProcessStarted()
{
    state_ = State::Running;
    lastError_.clear();
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[%1] iniciado com audio PCM via stdin.").arg(QFileInfo(resolvedBinaryPath()).fileName()));
}

void AcarsDecoManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (state_ != State::Stopped) {
        const QString output = recentProcessOutput();
        const QString name = QFileInfo(resolvedBinaryPath()).fileName();
        if (output.contains(QStringLiteral("already in use"), Qt::CaseInsensitive)
            || output.contains(QStringLiteral("SDR Device Not found"), Qt::CaseInsensitive)
            || output.contains(QStringLiteral("permission"), Qt::CaseInsensitive)
            || output.contains(QStringLiteral("libusb_open error"), Qt::CaseInsensitive)) {
            lastError_ = QStringLiteral("acarsdeco2 nao conseguiu abrir o RTL-SDR. O dispositivo ja esta em uso pelo RXSDR ou sem permissao USB.");
        } else if (status == QProcess::NormalExit && exitCode == 0) {
            lastError_ = QStringLiteral("%1 parou sozinho sem erro de sistema.").arg(name);
        } else {
            lastError_ = QStringLiteral("%1 falhou ou foi finalizado pelo sistema (codigo %2).").arg(name).arg(exitCode);
        }
        if (!output.isEmpty()) {
            lastError_ += QStringLiteral(" Ultima saida: %1").arg(output);
        }
        state_ = State::Error;
        emit error(lastError_);
    }
    emit stateChanged(state_);
}

void AcarsDecoManager::onProcessError(QProcess::ProcessError err)
{
    Q_UNUSED(err)
    lastError_ = QStringLiteral("Erro de processo do %1: %2")
                     .arg(QFileInfo(resolvedBinaryPath()).fileName())
                     .arg(process_ ? process_->errorString() : QString());
    state_ = State::Error;
    emit error(lastError_);
    emit stateChanged(state_);
}

void AcarsDecoManager::onReadyReadStdout()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardOutput();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        rememberProcessOutput(line);
        emit logLine(QStringLiteral("[ACARS] ") + line.trimmed());
    }
}

void AcarsDecoManager::onReadyReadStderr()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardError();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        rememberProcessOutput(line);
        emit logLine(QStringLiteral("[ACARS-ERR] ") + line.trimmed());
    }
}

void AcarsDecoManager::resetResampler()
{
    resamplePos_ = 0.0;
    resampleLast_ = 0.0f;
    hasResampleLast_ = false;
}

} // namespace masdr
