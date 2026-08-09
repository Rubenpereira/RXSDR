#include "PactorManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cmath>
#include <algorithm>

#include <QStandardPaths>

namespace masdr {

PactorManager::PactorManager(QObject* parent)
    : QObject(parent)
{}

PactorManager::~PactorManager()
{
    stop();
}

void PactorManager::setBinaryPath(const QString& path) { binaryPath_ = path; }

QString PactorManager::defaultBinaryPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    // Procura primeiro por pactor.bat (wrapper Python)
    const QString batPath = dir + QStringLiteral("/decoders/pactor.bat");
    if (QFileInfo::exists(batPath)) {
        return batPath;
    }
    // Fallback para pactor.exe
    return dir + QStringLiteral("/decoders/pactor.exe");
#else
    // Procura primeiro por pactor (wrapper shell)
    const QString shPath = dir + QStringLiteral("/decoders/pactor");
    if (QFileInfo::exists(shPath)) {
        return shPath;
    }
    return dir + QStringLiteral("/decoders/pactor");
#endif
}

QString PactorManager::resolvedBinaryPath() const
{
    if (!binaryPath_.isEmpty()) return binaryPath_;
    
    const QString defaultPath = defaultBinaryPath();
    if (QFileInfo::exists(defaultPath)) return defaultPath;

    // Tenta encontrar no PATH do sistema como fallback
#ifdef Q_OS_WIN
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("pactor.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("pactor.bat"));
#else
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("pactor"));
#endif
    if (!sysBin.isEmpty()) return sysBin;

    return defaultPath;
}

bool PactorManager::binaryExists() const
{
    return QFileInfo::exists(resolvedBinaryPath());
}

QString PactorManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject PactorManager::statusJson() const
{
    QJsonObject o;
    o["state"]         = stateString();
    o["binaryPresent"] = binaryExists();
    o["baudRate"]      = static_cast<double>(params_.baudRate);
    o["shift"]         = static_cast<double>(params_.shift);
    o["center"]        = static_cast<double>(params_.center);
    o["invert"]        = params_.invert;
    o["autoDetect"]    = params_.autoDetect;
    if (!lastError_.isEmpty())
        o["error"] = lastError_;
    return o;
}

bool PactorManager::start()
{
    if (state_ == State::Running || state_ == State::Starting) return true;

    const QString bin = resolvedBinaryPath();
    if (!QFileInfo::exists(bin)) {
#ifdef Q_OS_WIN
        lastError_ = QStringLiteral("O decodificador Pactor não foi encontrado em ./decoders/ (procurou: pactor.bat, pactor.exe)");
#else
        lastError_ = QStringLiteral("O decodificador Pactor não foi encontrado em ./decoders/ (procurou: pactor)");
#endif
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    process_ = std::make_unique<QProcess>(this);
    connect(process_.get(), &QProcess::started,
            this, &PactorManager::onProcessStarted);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PactorManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &PactorManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &PactorManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &PactorManager::onReadyReadStderr);

    // Argumentos: -r 48000 --baud <b> --shift <s> --center <c> [--invert] [--no-auto] -
    QStringList args;
    args << QStringLiteral("-r") << QStringLiteral("48000")
         << QStringLiteral("--baud")   << QString::number(static_cast<double>(params_.baudRate), 'f', 2)
         << QStringLiteral("--shift")  << QString::number(static_cast<double>(params_.shift), 'f', 2)
         << QStringLiteral("--center") << QString::number(static_cast<double>(params_.center), 'f', 1);
    if (params_.invert) {
        args << QStringLiteral("--invert");
    }
    if (!params_.autoDetect) {
        args << QStringLiteral("--no-auto");
    }
    args << QStringLiteral("-");

#ifdef Q_OS_WIN
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

void PactorManager::stop()
{
    m_stdinPending.clear();
    m_feedResampleTail.clear();
    m_feedResamplePos = 0.0;

    if (!process_) return;

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

void PactorManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (!process_ || state_ != State::Running || count <= 0) return;

    if (sps == 48000) {
        QByteArray pcm;
        pcm.resize(count * 2);
        memcpy(pcm.data(), samples, count * 2);
        m_stdinPending.append(pcm);
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
        }
    }

    static constexpr int kMaxStdinBacklog = 8 * 1024 * 1024;
    if (m_stdinPending.size() > kMaxStdinBacklog) {
        lastError_ = QStringLiteral("Backlog stdin Pactor muito grande.");
        emit error(lastError_);
        m_stdinPending.clear();
        stop();
        return;
    }

    int stall = 0;
    while (!m_stdinPending.isEmpty()) {
        const qint64 w = process_->write(m_stdinPending.constData(), m_stdinPending.size());
        if (w < 0) {
            lastError_ = QStringLiteral("Escrita stdin Pactor falhou: %1").arg(process_->errorString());
            emit error(lastError_);
            m_stdinPending.clear();
            stop();
            return;
        }
        if (w == 0) {
            if (!process_->waitForBytesWritten(10)) {
                if (++stall >= 3) break;
                continue;
            }
            stall = 0;
            continue;
        }
        stall = 0;
        m_stdinPending.remove(0, int(w));
    }
}

void PactorManager::onProcessStarted()
{
    state_ = State::Running;
    lastError_.clear();
    emit stateChanged(state_);

    emit logLine(QStringLiteral("[PACTOR] iniciado — monitorando Pactor-I (FSK)"));
}

void PactorManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (state_ != State::Stopped) {
        lastError_ = QStringLiteral("Pactor encerrou inesperadamente (código %1)").arg(exitCode);
        state_ = State::Error;
        emit error(lastError_);
    }
    emit stateChanged(state_);
}

void PactorManager::onProcessError(QProcess::ProcessError err)
{
    Q_UNUSED(err)
    lastError_ = QStringLiteral("Erro do Pactor: %1").arg(process_ ? process_->errorString() : QString());
    state_ = State::Error;
    emit error(lastError_);
    emit stateChanged(state_);
}

void PactorManager::onReadyReadStdout()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardOutput();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (t.isEmpty()) continue;

        emit logLine(t);
    }
}

void PactorManager::onReadyReadStderr()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardError();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (!t.isEmpty()) {
            emit logLine(QStringLiteral("[PACTOR-LOG] ") + t);
        }
    }
}

} // namespace masdr
