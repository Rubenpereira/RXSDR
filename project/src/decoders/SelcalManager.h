#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QJsonObject>
#include <vector>
#include <memory>

namespace masdr {

class SelcalManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Error
    };
    Q_ENUM(State)

    // SELCAL é totalmente automático — sem parâmetros configuráveis.
    struct Params {
    };

    explicit SelcalManager(QObject* parent = nullptr);
    ~SelcalManager() override;

    void setParams(const Params& p) { params_ = p; }
    Params params() const { return params_; }

    void setBinaryPath(const QString& path);

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QString lastError() const { return lastError_; }
    QJsonObject statusJson() const;

    bool binaryExists() const;

    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);
    void error(const QString& message);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError err);
    void onReadyReadStdout();
    void onReadyReadStderr();

private:
    QString defaultBinaryPath() const;
    QString resolvedBinaryPath() const;

    std::unique_ptr<QProcess> process_;
    QString binaryPath_;
    Params  params_;
    State   state_          = State::Stopped;
    QString lastError_;

    QByteArray m_stdinPending;
    std::vector<int16_t> m_feedResampleTail;
    double m_feedResamplePos = 0.0;
};

} // namespace masdr
