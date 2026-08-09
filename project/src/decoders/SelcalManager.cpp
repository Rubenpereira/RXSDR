#include "SelcalManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cmath>
#include <algorithm>

#include <QStandardPaths>

namespace masdr {

SelcalManager::SelcalManager(QObject* parent)
    : QObject(parent)
{}

SelcalManager::~SelcalManager()
{
    stop();
}

void SelcalManager::setBinaryPath(const QString& path) { binaryPath_ = path; }

QString SelcalManager::defaultBinaryPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString batPath = dir + QStringLiteral("/decoders/selcal.bat");
    if (QFileInfo::exists(batPath)) {
        return batPath;
    }
    return dir + QStringLiteral("/decoders/selcal.exe");
#else
    const QString shPath = dir + QStringLiteral("/decoders/selcal");
    if (QFileInfo::exists(shPath)) {
        return shPath;
    }
    return dir + QStringLiteral("/decoders/selcal");
#endif
}

QString SelcalManager::resolvedBinaryPath() const
{
    if (!binaryPath_.isEmpty()) return binaryPath_;
    
    const QString defaultPath = defaultBinaryPath();
    if (QFileInfo::exists(defaultPath)) return defaultPath;

    // Tenta encontrar no PATH do sistema como fallback
#ifdef Q_OS_WIN
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("selcal.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("selcal.bat"));
#else
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("selcal"));
#endif
    if (!sysBin.isEmpty()) return sysBin;

    return defaultPath;
}

bool SelcalManager::binaryExists() const
{
    return QFileInfo::exists(resolvedBinaryPath());
}

QString SelcalManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject SelcalManager::statusJson() const
{
    QJsonObject o;
    o["state"]         = stateString();
    o["binaryPresent"] = binaryExists();
    if (!lastError_.isEmpty())
        o["error"] = lastError_;
    return o;
}

bool SelcalManager::start()
{
    if (state_ == State::Running || state_ == State::Starting) return true;

    const QString bin = resolvedBinaryPath();
    if (!QFileInfo::exists(bin)) {
#ifdef Q_OS_WIN
        lastError_ = QStringLiteral("O decodificador SELCAL nao foi encontrado em ./decoders/ (procurou: selcal.bat, selcal.exe)");
#else
        lastError_ = QStringLiteral("O decodificador SELCAL nao foi encontrado em ./decoders/ (procurou: selcal)");
#endif
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    process_ = std::make_unique<QProcess>(this);
    connect(process_.get(), &QProcess::started,
            this, &SelcalManager::onProcessStarted);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &SelcalManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &SelcalManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &SelcalManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &SelcalManager::onReadyReadStderr);

    QStringList args;
    args << QStringLiteral("-r") << QStringLiteral("48000")
         << QStringLiteral("-");

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

void SelcalManager::stop()
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

void SelcalManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
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
        lastError_ = QStringLiteral("Backlog stdin SELCAL muito grande.");
        emit error(lastError_);
        m_stdinPending.clear();
        stop();
        return;
    }

    int stall = 0;
    while (!m_stdinPending.isEmpty()) {
        const qint64 w = process_->write(m_stdinPending.constData(), m_stdinPending.size());
        if (w < 0) {
            lastError_ = QStringLiteral("Escrita stdin SELCAL falhou: %1").arg(process_->errorString());
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

void SelcalManager::onProcessStarted()
{
    state_ = State::Running;
    lastError_.clear();
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[SELCAL] iniciado — monitorando 16 tons CCITT/ARINC 596"));
}

void SelcalManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (state_ != State::Stopped) {
        lastError_ = QStringLiteral("SELCAL encerrou inesperadamente (codigo %1)").arg(exitCode);
        state_ = State::Error;
        emit error(lastError_);
    }
    emit stateChanged(state_);
}

void SelcalManager::onProcessError(QProcess::ProcessError err)
{
    Q_UNUSED(err)
    lastError_ = QStringLiteral("Erro do SELCAL: %1").arg(process_ ? process_->errorString() : QString());
    state_ = State::Error;
    emit error(lastError_);
    emit stateChanged(state_);
}

void SelcalManager::onReadyReadStdout()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardOutput();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (!t.isEmpty()) {
            emit logLine(t);
        }
    }
}

void SelcalManager::onReadyReadStderr()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardError();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (!t.isEmpty()) {
            emit logLine(QStringLiteral("[SELCAL-LOG] ") + t);
        }
    }
}

} // namespace masdr
