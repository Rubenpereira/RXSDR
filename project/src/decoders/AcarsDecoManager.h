#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QMutex>
#include <QByteArray>
#include <QJsonObject>
#include <cstdint>
#include <memory>
#include <vector>

namespace masdr {

class AcarsDecoManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Error
    };
    Q_ENUM(State)

    explicit AcarsDecoManager(QObject* parent = nullptr);
    ~AcarsDecoManager() override;

    // Configuração
    void setBinaryPath(const QString& path);
    void setWebPort(quint16 port);
    void setDeviceIndex(int index);
    void setFrequencies(const QString& freqs); // Ex: "131550000,131725000"
    void setExclusiveMode(bool exclusive);

    // Ciclo de vida
    bool start();
    void stop();
    void feedAudio(const int16_t* samples, int count, uint32_t sampleRate);

    // Estado
    State state() const { return state_; }
    QString stateString() const;
    quint16 webPort() const { return webPort_; }
    QString webUrl() const;
    QString lastError() const { return lastError_; }
    QJsonObject statusJson() const;

    // Utilitário
    bool binaryExists() const;

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);
    void error(const QString& message);

private slots:
    // Mesma razao do AprsManager: o feedAudio() vem da thread de leitura do
    // dongle e o QProcess pertence a thread principal. Escrever nele de fora
    // enfileira bytes que o laco de eventos da thread dona nunca envia, e o
    // decodificador fica esperando para sempre. A drenagem e pedida por
    // conexao enfileirada para acontecer na thread certa.
    void drenarStdin();

    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError err);
    void onReadyReadStdout();
    void onReadyReadStderr();

private:
    QString defaultBinaryPath() const;
    QString resolvedBinaryPath() const;
    void rememberProcessOutput(const QString& line);
    QString recentProcessOutput() const;
    void resetResampler();

    std::unique_ptr<QProcess> process_;

    QByteArray m_stdinPending;

    QMutex     m_stdinMutex;
    QString binaryPath_;
    quint16 webPort_    = 8093; // Porta padrão para acarsdeco2
    int     deviceIndex_ = 0;
    QString freqs_       = QStringLiteral("131550000,131725000"); // Freqs padrão
    bool    exclusive_  = false;
    State   state_      = State::Stopped;
    QString lastError_;
    QStringList recentOutput_;
    double resamplePos_ = 0.0;
    float resampleLast_ = 0.0f;
    bool hasResampleLast_ = false;
};

} // namespace masdr
