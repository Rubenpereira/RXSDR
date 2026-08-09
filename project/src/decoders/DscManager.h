#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <vector>

#include "DscCore.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  DscManager - DSC (chamada seletiva digital, ITU-R M.493 / M.541)
//
//  Antes isto lancava um processo Python (decoders/dsc.bat). Exigia Python na
//  maquina do usuario final, o que quebrava a distribuicao. Pior: vinha
//  configurado com 200 baud e shift de 200 Hz, que sao numeros de PACTOR-I e
//  nao de DSC - em HF o DSC e 100 baud com shift de 170 Hz. Com os parametros
//  errados nunca teve chance de decodificar nada.
//
//  Agora e nativo (DscCore): sem processo externo e sem dependencia.
// ---------------------------------------------------------------------------
class DscManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Error };
    Q_ENUM(State)

    struct Params {
        float baudRate   = 100.0f;    // DSC em HF
        float shift      = 170.0f;
        float centerFreq = 1700.0f;   // tom central no audio USB
        bool  invert     = false;
    };

    explicit DscManager(QObject* parent = nullptr);
    ~DscManager() override;

    void setParams(const Params& p);
    Params params() const { return params_; }

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QString lastError() const { return lastError_; }
    QJsonObject statusJson() const;

    // Nao ha mais binario externo.
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

    DscCore core_;

    // Reamostragem do audio do radio para a taxa de trabalho do nucleo
    std::vector<float> bloco_;
    double   resamplePos_  = 0.0;
    float    resampleLast_ = 0.0f;
    bool     temUltimo_    = false;
    uint32_t ultimaSps_    = 0;

    QString linhaParcial_;
};

} // namespace masdr
