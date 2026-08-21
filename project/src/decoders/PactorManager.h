#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QMutex>
#include <QJsonObject>
#include <vector>
#include <memory>

namespace masdr {

// Gerenciador do decodificador Pactor-I (FSK 200 baud / 200 Hz shift).
// Espelha SitorBManager: spawna decoders/pactor.bat (runner Python),
// envia PCM demodulado (USB) via stdin e captura texto via stdout.
class PactorManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Error
    };
    Q_ENUM(State)

    struct Params {
        float baudRate = 200.0f;   // Pactor-I = 200 baud
        float shift    = 200.0f;   // Pactor-I = 200 Hz shift
        float center   = 1500.0f;  // centro de audio em SSB
        bool  invert   = false;
        bool  autoDetect = true;   // auto-detecta tons/variante
    };

    explicit PactorManager(QObject* parent = nullptr);
    ~PactorManager() override;

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

    // Recebe audio PCM demodulado (USB/SSB)
    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);
    void error(const QString& message);

private slots:
    // Escreve no processo a partir da thread dona - ver o comentario no .cpp
    void drenarStdin();

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
    State   state_          = State::Stopped;
    QString lastError_;
    Params  params_;

    QByteArray m_stdinPending;

    QMutex     m_stdinMutex;
    std::vector<int16_t> m_feedResampleTail;
    double m_feedResamplePos = 0.0;
};

} // namespace masdr
