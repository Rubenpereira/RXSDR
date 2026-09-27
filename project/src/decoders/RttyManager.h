#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <vector>

#include "RttyCore.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  RttyManager - RTTY (Baudot/ITA2) nativo
//
//  Mesmo desenho do SitorBManager (audio do radio reamostrado para 8 kHz e
//  entregue ao nucleo) e do CwManager no texto: as letras saem por
//  textoFluxo() assim que sao lidas - a 45 baud sao ~6 por segundo, e esperar
//  fechar linha deixaria a tela parada por 10 s ou mais.
// ---------------------------------------------------------------------------
class RttyManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running };
    Q_ENUM(State)

    struct Params {
        float baudRate   = 45.45f;
        float shift      = 170.0f;
        float centerFreq = 1500.0f;   // tom central no audio USB
        bool  invert     = false;
        bool  autoTom    = true;
        bool  usos       = true;      // espaco volta para letras
    };

    explicit RttyManager(QObject* parent = nullptr);
    ~RttyManager() override;

    void setParams(const Params& p);
    Params params() const { return params_; }

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QJsonObject statusJson() const;

    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State novo);
    void logLine(const QString& linha);      // servico: iniciado, tom medido...
    void textoFluxo(const QString& pedaco);  // texto decodificado, sem esperar linha

private:
    void aplicarParams();

    State  state_ = State::Stopped;
    Params params_;
    RttyCore core_;

    std::vector<float> bloco_;
    double   resamplePos_  = 0.0;
    float    resampleLast_ = 0.0f;
    bool     temUltimo_    = false;
    uint32_t ultimaSps_    = 0;
};

} // namespace masdr
