#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <vector>

#include "AleCore.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  AleManager - ALE 2G (MIL-STD-188-141A/B, FED-STD-1045), so recepcao
//
//  Mesmo desenho do RttyManager: audio do radio reamostrado para 8 kHz e
//  entregue ao AleCore. Cada chamada que termina vira uma linha de texto,
//  com a hora UTC na frente, emitida por logLine().
//
//  "modo" fica guardado para quando entrarem outras variantes no menu do
//  painel; hoje so existe "2g".
// ---------------------------------------------------------------------------
class AleManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running };
    Q_ENUM(State)

    explicit AleManager(QObject* parent = nullptr);
    ~AleManager() override;

    void setModo(const QString& modo) { modo_ = modo; }
    QString modo() const { return modo_; }

    bool start();
    void stop();

    State   state() const { return state_; }
    QString stateString() const;
    QJsonObject statusJson() const;

    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State novo);
    void logLine(const QString& linha);

private:
    State   state_ = State::Stopped;
    QString modo_  = QStringLiteral("2g");
    AleCore core_;

    std::vector<float> bloco_;
    double   resamplePos_  = 0.0;
    float    resampleLast_ = 0.0f;
    bool     temUltimo_    = false;
    uint32_t ultimaSps_    = 0;
};

} // namespace masdr
