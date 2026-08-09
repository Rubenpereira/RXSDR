#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <vector>

#include "AnaliseCore.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  AnaliseManager - identificador de sinal desconhecido
//
//  Nao decodifica nada: junta alguns segundos de audio, mede e responde. Serve
//  para quando aparece um sinal digital na cachoeira e voce nao sabe o que e.
//  Diz quantos tons, a separacao entre eles, a velocidade e se o conteudo tem
//  cara de texto ou de cifra.
// ---------------------------------------------------------------------------
class AnaliseManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running, Error };
    Q_ENUM(State)

    explicit AnaliseManager(QObject* parent = nullptr);
    ~AnaliseManager() override;

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QJsonObject statusJson() const;

    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State newState);
    void logLine(const QString& line);

private:
    void concluir();

    State  state_ = State::Stopped;
    AnaliseCore core_{8000.0};
    bool   concluido_ = false;

    // Reamostragem do audio do radio para os 8 kHz do nucleo
    std::vector<float> bloco_;
    double   resamplePos_  = 0.0;
    float    resampleLast_ = 0.0f;
    bool     temUltimo_    = false;
    uint32_t ultimaSps_    = 0;
};

} // namespace masdr
