#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <memory>
#include <cstdint>
#include <mutex>
#include <vector>
#include <complex>
#include "../dsp/Filters.h"

namespace masdr {

class AisCatcherManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Error
    };
    Q_ENUM(State)

    explicit AisCatcherManager(QObject* parent = nullptr);
    ~AisCatcherManager() override;

    // Configuração
    void setBinaryPath(const QString& path);
    void setWebPort(quint16 port);
    void setDeviceIndex(int index);
    void setSampleRate(double rate);

    // Entrada de áudio PCM (igual ao ACARS/Packet)
    void feedAudio(const int16_t* samples, int count, uint32_t sampleRate);

    // Caminho correto para o AIS-catcher: ele espera IQ COMPLEXO (-r CS16),
    // nao audio do discriminador. Decima o IQ do radio para kAisIqRate e
    // escreve I/Q intercalado em 16 bits no pipe.
    void feedIQ(const std::complex<float>* iq, size_t count, uint32_t sps);

    // Ciclo de vida
    bool start();
    void stop();

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
    void writeToStdin(const QByteArray& data);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError err);
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onWriteToStdin(const QByteArray& data);
    void onNewConnection();
    void onClientDisconnected();

private:
    QString defaultBinaryPath() const;
    QString resolvedBinaryPath() const;
    void rememberProcessOutput(const QString& line);
    QString recentProcessOutput() const;
    void resetResampler();

    QLocalServer* localServer_ = nullptr;
    QLocalSocket* localClient_ = nullptr;

    std::unique_ptr<QProcess> process_;
    QString binaryPath_;
    quint16 webPort_     = 8092;
    int     deviceIndex_ = 0;
    State   state_       = State::Stopped;
    QString lastError_;
    QStringList recentOutput_;

    // Taxa de áudio que o AIS-catcher aceita via stdin (PCM 16-bit mono)
    static constexpr int kAisAudioRate = 48000;

    // Taxa de IQ entregue ao AIS-catcher. Os dois canais AIS (161,975 e
    // 162,025 MHz) estao 50 kHz separados e o VFO fica no meio, em 162,000.
    // O proprio binario diz "sample rate must be between 12k and 192k":
    // usamos o teto, que da a melhor sensibilidade e sobra de banda.
    static constexpr uint32_t kAisIqRate = 192000;

    // Anti-aliasing (mesma receita do TetraManager): LPF IIR + media movel
    // de comprimento M = round(sps / kAisIqRate), com o zero da sinc caindo
    // na taxa de saida. Sem isso o ruido de toda a banda dobra para dentro.
    std::vector<std::complex<float>> aaRing_;
    std::complex<double> aaSum_{0.0, 0.0};
    size_t   aaIdx_ = 0;
    size_t   aaLen_ = 0;
    uint32_t aaSps_ = 0;
    // Tres IirLpf em cascata a 50 kHz. Um so, mais aberto, deixava um sinal
    // forte logo fora da janela (por exemplo um DMR em 162,105 com o VFO em
    // 162,000) dobrar para dentro MAIS FORTE que o proprio AIS. Medido: com
    // 1 filtro a 80 kHz o intruso ficava 16,8 dB acima do AIS; com 3 a
    // 50 kHz o AIS fica 13,3 dB acima dele.
    IirLpf   iqLpf_, iqLpf2_, iqLpf3_;
    std::vector<std::complex<float>> iqTail_;
    double   iqPos_ = 0.0;

    // Resampler simples (igual ao ACARS)
    double  resamplePos_      = 0.0;
    float   resampleLast_     = 0.0f;
    bool    hasResampleLast_  = false;
};

} // namespace masdr
