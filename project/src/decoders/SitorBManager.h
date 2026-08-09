#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <vector>
#include <memory>

#include "SitorBCore.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  SitorBManager - SITOR-B / NAVTEX
//
//  Antes isto lancava um processo Python (decoders/sitorb.bat chamando o
//  sitorb_runner.py). Exigia Python instalado na maquina do usuario final, o
//  que quebrava a distribuicao, e o decodificador de la nao fazia a
//  diversidade temporal DX/RX nem validava o CCIR 476 com o numero certo de
//  bits.
//
//  Agora a decodificacao e nativa (SitorBCore): sem processo externo, sem
//  dependencia nenhuma, e com o FEC que da robustez ao SITOR-B.
// ---------------------------------------------------------------------------
class SitorBManager : public QObject {
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
        float baudRate   = 100.0f;
        float shift      = 170.0f;
        float centerFreq = 1700.0f;   // tom central no audio USB
        bool  invert     = false;
    };

    explicit SitorBManager(QObject* parent = nullptr);
    ~SitorBManager() override;

    void setParams(const Params& p);
    Params params() const { return params_; }

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QString lastError() const { return lastError_; }
    QJsonObject statusJson() const;

    // Nao ha mais binario externo. Fica sempre verdadeiro para a interface
    // nao precisar de tratamento especial.
    bool binaryExists() const { return true; }

    // Recebe audio PCM demodulado (USB/SSB)
    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);
    void error(const QString& message);

private:
    void aplicarParams();
    void despejarTexto(const std::string& txt);

    State   state_ = State::Stopped;
    QString lastError_;
    Params  params_;

    SitorBCore core_;

    // Reamostragem do audio do radio para a taxa de trabalho do nucleo
    std::vector<float> bloco_;
    double   resamplePos_  = 0.0;
    float    resampleLast_ = 0.0f;
    bool     temUltimo_    = false;
    uint32_t ultimaSps_    = 0;

    // Junta o texto ate fechar linha, para nao mandar fragmento a fragmento
    QString linhaParcial_;
};

} // namespace masdr
