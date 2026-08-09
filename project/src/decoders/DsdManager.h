#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QJsonObject>
#include <QVector>
#include <memory>
#include <algorithm>
#include <vector>

class QUdpSocket;

namespace masdr {

class DsdManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Error
    };
    Q_ENUM(State)

    explicit DsdManager(QObject* parent = nullptr);
    ~DsdManager() override;

    // Configuração
    void setBinaryPath(const QString& path);
    void setInputDevice(int dev);
    int  inputDevice() const { return inputDevice_; }

    // Ciclo de vida
    bool start();
    void stop();

    // Polaridade do sinal (reinicia com flag -P para dsd-fme)
    void     togglePolarity();
    bool     invertPolarity() const { return invertPolarity_; }

    // Estado
    State   state() const { return state_; }
    QString stateString() const;
    QString lastError() const { return lastError_; }
    QJsonObject statusJson() const;

    // Utilitário
    bool binaryExists() const;

    // Alimentação de áudio bruto com resampling interno para 48000 Hz
    void feedAudio(const int16_t* samples, int count, uint32_t sps);

    // Taxa de amostragem do áudio decodificado recebido via UDP do DSD-FME (fixada em 16000 Hz)
    void setUdpVoicePcmHz(int hz) { if (hz < 14000) hz = 14000; if (hz > 18000) hz = 18000; m_udpVoicePcmHz = hz; }
    int  getUdpVoicePcmHz() const { return m_udpVoicePcmHz; }

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);
    void error(const QString& message);
    void decodedAudioReady(const std::vector<int16_t>& pcm, uint32_t sps);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError err);
    void onReadyReadStdout();
    void onReadyReadStderr();
    void pullUdpPackets();

private:
    QString defaultBinaryPath() const;
    QString resolvedBinaryPath() const;

    void closeUdpAudioListener();
    void flushUdpResampleState();
    void appendUdpVoiceResampleTo48k(QVector<float> monoChunk);
    void dumpFeed(const QByteArray& pcm);

    std::unique_ptr<QProcess> process_;
    QString binaryPath_;
    State   state_          = State::Stopped;
    QString lastError_;
    bool    invertPolarity_ = false;
    int     inputDevice_    = 1;

    QUdpSocket* m_udpSock = nullptr;
    quint16 m_udpListenPort = 0;
    QByteArray m_stdinPending;
    void* m_dumpFile = nullptr; // diagnostico: dump do audio enviado ao dsd-fme
    int m_udpVoicePcmHz = 16150; // DSD-FME UDP — taxa do audio decodificado (ajuste fino no painel)
    QVector<float> m_udpResampleTail;
    double m_udpResampleInPos = 0.0;

    std::vector<int16_t> m_feedResampleTail;
    double m_feedResamplePos = 0.0;
};

} // namespace masdr
