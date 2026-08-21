#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QMutex>
#include <QJsonObject>
#include <QVector>
#include <vector>
#include <memory>
#include <complex>
#include "../dsp/Filters.h"

class QUdpSocket;

namespace masdr {

// TetraManager — modo wrapper "osmo-tetra":
//   • Spawna decoders/tetra.bat → tetra_runner.py
//   • Runner gerencia tetra-rx.exe (osmo-tetra) ouvindo sinbolos UDP 8355
//     vindos do plugin TETRA Demodulator do SDR++.
//   • Runner pipe-eia ACELP frames para cdecoder.exe -> PCM 8 kHz mono
//   • Runner envia PCM via UDP de volta ao TetraManager
//   • TetraManager faz resample 8k → 48k e emite decodedAudioReady → audio
//     vai para o navegador via WsServer::broadcastAudioThreadSafe()
//
// Em "modo fallback" (sem tetra-rx.exe), o runner roda apenas detector
// heuristico sobre o PCM enviado via feedAudio (compativel com a regra
// "feedAudio nao alterar").
class TetraManager : public QObject {
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
        int symbolRate = 18000;  // π/4-DQPSK 18 ksym/s = 36 kbps TETRA
        // Porta UDP que o plugin TETRA Demodulator do SDR++ envia simbolos
        int sdrppPort  = 8355;
        bool invertIQ  = false;
    };

    explicit TetraManager(QObject* parent = nullptr);
    ~TetraManager() override;

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
    bool osmoTetraExists() const;   // tetra-rx.exe presente?
    bool acelpDecoderExists() const; // cdecoder.exe presente?

    // Em modo wrapper o PCM nao e usado (SDR++ ja faz a demod); ainda assim
    // mantemos o metodo para compatibilidade com a chamada generica do
    // Application sobre todos os decoders.
    void feedAudio(const int16_t* samples, int count, uint32_t sps);

    // Alimenta IQ complex float32 (ja centralizado no VFO). Decimamos para
    // 36 kS/s aqui dentro e enviamos via stdin para o runner Python.
    // Chamado por Application::wireDeviceCallback depois do shift de VFO.
    void feedIQ(const std::complex<float>* iq, size_t count, uint32_t sps);

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);
    void error(const QString& message);
    // PCM mono 48 kHz pronto para a saida de audio do navegador
    void decodedAudioReady(const std::vector<int16_t>& pcm, uint32_t sps);

private slots:
    // Envia ao runner o IQ ja acumulado, a partir da thread dona do QProcess.
    //
    // O feedIQ() e chamado pela thread de leitura do dongle. Escrever no
    // QProcess de la apenas ENFILEIRA bytes que o laco de eventos da thread
    // principal nunca despacha: o demodulador nao recebia nada e a cadeia
    // TETRA se desfazia poucos segundos depois de abrir o painel.
    //
    // Mesmo defeito ja corrigido no APRS, ACARS, DSD, Pactor e SELCAL. Este
    // escapou porque usa feedIQ, e a varredura de 20/08/2026 procurou so por
    // feedAudio - o criterio certo e "qualquer feed* que escreva num processo".
    void drenarStdinIq();

    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError err);
    void onReadyReadStdout();
    void onReadyReadStderr();
    void pullUdpPackets();

private:
    QString defaultBinaryPath() const;
    QString resolvedBinaryPath() const;
    QString osmoTetraPath() const;
    QString acelpDecoderPath() const;

    void closeUdpAudioListener();
    void appendUdpVoiceResampleTo48k(QVector<float> monoChunk, int srcHz);

    std::unique_ptr<QProcess> process_;
    QString binaryPath_;
    Params  params_;
    State   state_          = State::Stopped;
    QString lastError_;
    int     voiceFramesRx_  = 0;

    QUdpSocket* m_udpSock = nullptr;
    quint16     m_udpListenPort = 0;
    QVector<float> m_udpResampleTail;
    double         m_udpResampleInPos = 0.0;
    int            m_udpVoicePcmHz = 8000; // PCM do cdecoder ACELP TETRA

    // IQ tap → stdin do runner Python (complex float32 a 36 kS/s)
    static constexpr uint32_t kTetraIqRate = 36000;
    std::vector<std::complex<float>> m_iqDecimTail;
    double  m_iqDecimPos    = 0.0;
    QByteArray m_iqStdinPending;
    QMutex     m_iqStdinMutex;

    // Anti-aliasing boxcar (moving-average) filter applied before decimation.
    // Length M ≈ round(sps / kTetraIqRate). Provides sinc(f) response with
    // a null near the output sample rate, eliminating the dominant alias.
    std::vector<std::complex<float>> m_aaRing;
    std::complex<double> m_aaSum{0.0, 0.0};
    size_t m_aaIdx  = 0;
    size_t m_aaLen  = 0;
    uint32_t m_aaSps = 0;
    IirLpf m_lpFilter;
};

} // namespace masdr
