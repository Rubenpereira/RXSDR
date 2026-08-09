#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>
#include <vector>
#include <memory>

namespace masdr {

class AprsManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Error
    };
    Q_ENUM(State)

    explicit AprsManager(QObject* parent = nullptr);
    ~AprsManager() override;

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

    // --- Porta AGW do Direwolf (127.0.0.1:8000) --------------------------
    // O stdout do Direwolf sai por um pipe, e nesse caso o runtime C troca
    // buffer por linha por buffer cheio de ~4 KB: os pacotes ficavam presos
    // ate encher, chegando em blocos com minutos de atraso. A porta AGW
    // entrega cada quadro no instante em que e decodificado.
    void tentarConectarAgw();
    void onAgwConectado();
    void onAgwDados();
    void onAgwDesconectado();

private:
    QString defaultBinaryPath() const;
    QString resolvedBinaryPath() const;
    bool createConfigFile(const QString& configPath);

    std::unique_ptr<QProcess> process_;
    QString binaryPath_;
    State   state_          = State::Stopped;
    QString lastError_;

    QByteArray m_stdinPending;
    std::vector<int16_t> m_feedResampleTail;
    double m_feedResamplePos = 0.0;

    // Cliente AGW
    QTcpSocket* agw_          = nullptr;
    QTimer*     agwRetry_     = nullptr;
    QByteArray  agwBuf_;
    bool        agwPronto_    = false;   // ja recebeu quadro: stdout vira so log
    int         agwTentativas_ = 0;
    static constexpr quint16 kAgwPort = 8000;
};

} // namespace masdr
