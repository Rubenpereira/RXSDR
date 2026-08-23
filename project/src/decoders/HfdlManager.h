#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QMutex>
#include <QJsonObject>
#include <QByteArray>
#include <vector>
#include <complex>
#include <memory>

namespace masdr {

// ---------------------------------------------------------------------------
//  HfdlManager - HFDL (High Frequency Data Link), o ACARS das ondas curtas
//
//  Quem decodifica e o dumphfdl, do mesmo autor do dumpvdl2. Ele recebe IQ
//  pela entrada padrao e escreve as mensagens ja interpretadas na saida.
//
//  DUAS COISAS O SEPARAM DOS OUTROS DECODIFICADORES DAQUI:
//
//  1. Ele quer o espectro INTEIRO, nao o canal sintonizado. O HFDL tem varias
//     frequencias por banda e as estacoes se revezam entre elas; o dumphfdl
//     acompanha todas de uma vez. Por isso alimentamos o IQ do jeito que sai
//     do radio - centrado no centro do DONGLE - e nao o do VFO, que ja veio
//     deslocado para uma frequencia so.
//
//  2. Nao ha reamostragem aqui. O dumphfdl decima por conta propria a partir
//     do que receber, entao basta dizer a taxa e o centro. Menos codigo nosso
//     no caminho, menos lugar para errar.
//
//  O formato CF32 nao e escolha a toa: e exatamente o complex<float> que o
//  RXSDR ja usa internamente, entao os bytes vao para o cano sem conversao
//  nenhuma.
// ---------------------------------------------------------------------------
class HfdlManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Error };
    Q_ENUM(State)

    struct Params {
        // Canais HFDL a acompanhar, em kHz. O dumphfdl aceita varios.
        QList<double> canaisKHz;
        // Centro do IQ que vamos entregar, em kHz.
        double centroKHz = 0.0;
        // Taxa do IQ entregue, em amostras por segundo.
        uint32_t sampleRate = 0;
    };

    explicit HfdlManager(QObject* parent = nullptr);
    ~HfdlManager() override;

    void   setParams(const Params& p) { params_ = p; }
    Params params() const { return params_; }

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QString lastError() const { return lastError_; }
    QJsonObject statusJson() const;

    bool binaryExists() const;
    QString binaryPath() const;

    // IQ cru, como sai do radio, centrado no centro do dongle.
    //
    // Chamado pela thread de leitura do dispositivo. Aqui dentro so se COPIA
    // para uma fila protegida por mutex; quem escreve no processo e o
    // drenarStdin(), que corre na thread dona do QProcess.
    //
    // Escrever no QProcess direto daqui nao funciona - apenas enfileira bytes
    // que o laco de eventos daquela thread nunca despacha. Foi o defeito que
    // ja apareceu no APRS, ACARS, DSD, Pactor, SELCAL e TETRA.
    void feedIQ(const std::complex<float>* iq, size_t count, uint32_t sps);

signals:
    void stateChanged(State novoEstado);
    void logLine(const QString& linha);
    void error(const QString& mensagem);
    // Uma mensagem HFDL completa, ja montada a partir das linhas soltas.
    void mensagem(const QString& texto);

private slots:
    void drenarStdin();
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError err);
    void onReadyReadStdout();
    void onReadyReadStderr();

private:
    QString pastaDecoders() const;
    QString systablePath() const;
    void    mudarEstado(State novo);
    void    processarLinha(const QString& linha);

    std::unique_ptr<QProcess> process_;
    Params  params_;
    State   state_ = State::Stopped;
    QString lastError_;
    int     mensagensRecebidas_ = 0;

    // Fila de IQ esperando para ir ao processo.
    QByteArray pendente_;
    QMutex     pendenteMutex_;

    // O dumphfdl escreve uma mensagem em VARIAS linhas, separadas por uma
    // linha em branco. Juntamos aqui ate a mensagem fechar.
    QString  parcial_;
    QString  restoStdout_;
    QString  restoStderr_;

    // Teto da fila de IQ.
    //
    // Se o dumphfdl travar ou nao acompanhar, a fila cresceria sem limite ate
    // a memoria acabar - o radio morreria por causa de um decodificador. Com
    // teto, o que passa disso e descartado: perde-se mensagem, nao o programa.
    static constexpr int kMaxPendenteBytes = 16 * 1024 * 1024;
};

} // namespace masdr
