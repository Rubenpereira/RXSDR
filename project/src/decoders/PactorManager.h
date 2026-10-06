#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <memory>
#include <mutex>
#include <vector>

#include "PactorCore.h"
#include "FilaAudio.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  PactorManager - PACTOR-I (FEC / escuta de ARQ), o modo da Marinha do Brasil
//
//  Antes isto lancava um processo Python (decoders/pactor.bat chamando o
//  pactor_runner.py), que exigia Python na maquina e nao conferia o CRC.
//  Agora a decodificacao e nativa (PactorCore, o mesmo do RXSDR Nativo):
//  acha sozinho o tom, 100 ou 200 baud e a polaridade, aceita Huffman e
//  ASCII, e so mostra pacote que fechou o CRC. Roda numa thread propria.
// ---------------------------------------------------------------------------
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

    // Mantidos por compatibilidade com o /api/pactor/start. O nucleo mede
    // tudo sozinho: tom central, velocidade e polaridade.
    struct Params {
        float baudRate = 100.0f;
        float shift    = 200.0f;
        float center   = 1500.0f;
        bool  invert   = false;
        bool  autoDetect = true;
    };

    explicit PactorManager(QObject* parent = nullptr);
    ~PactorManager() override;

    void setParams(const Params& p) { params_ = p; }
    Params params() const { return params_; }

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QString lastError() const { return lastError_; }
    QJsonObject statusJson() const;

    bool binaryExists() const { return true; }

    // Recebe audio PCM demodulado (USB) - so enfileira, a conta e na thread
    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);
    // Texto decodificado em pedacos, emendado na tela como num terminal
    void textoFluxo(const QString& pedaco);
    void error(const QString& message);

private:
    void processar(const std::vector<int16_t>& pcm, uint32_t sps);

    State   state_ = State::Stopped;
    QString lastError_;
    Params  params_;

    mutable std::mutex coreMutex_;
    std::unique_ptr<PactorCore> core_;

    // Reamostragem do audio do radio para os 8 kHz do nucleo
    std::vector<float> bloco_, ent_;
    double   resamplePos_  = 0.0;
    float    resampleLast_ = 0.0f;
    bool     temUltimo_    = false;
    uint32_t ultimaSps_    = 0;

    double relogio_ = 0.0;        // segundos de audio processados
    double ultimoTexto_ = -1e9;   // quando saiu o ultimo texto

    // Por ultimo: e destruida primeiro, e a thread para antes do resto sumir
    FilaAudio fila_;
};

} // namespace masdr
