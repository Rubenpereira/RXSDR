#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <vector>

#include "CwCore.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  CwManager - telegrafia (CW / Morse)
//
//  Liga o audio demodulado ao CwCore, reamostrando da taxa do radio para os
//  8 kHz do nucleo.
//
//  Diferenca em relacao ao SITOR-B e ao DSC: aqui nao ha "baud" nem "shift"
//  para configurar. A velocidade e do operador humano, muda dentro da propria
//  transmissao, e por isso e medida continuamente pelo nucleo. O unico ajuste
//  que faz sentido expor e o tom - e mesmo esse e medido sozinho.
// ---------------------------------------------------------------------------
class CwManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running, Error };
    Q_ENUM(State)

    struct Params {
        float tomHz   = 0.0f;    // 0 = medir sozinho
        bool  autoTom = true;
    };

    explicit CwManager(QObject* parent = nullptr);
    ~CwManager() override;

    void setParams(const Params& p);
    Params params() const { return params_; }

    bool start();
    void stop();
    void restart();

    State   state() const { return state_; }
    QString stateString() const;
    QJsonObject statusJson() const;

    void feedAudio(const int16_t* samples, int count, uint32_t sps);

signals:
    void stateChanged(State novo);

    // Mensagens de servico - iniciado, parado, tom medido. Cada uma e uma
    // linha inteira e independente.
    void logLine(const QString& linha);

    // O TEXTO DECODIFICADO, em fluxo.
    //
    // Antes ele saia por logLine, e por isso precisava ser cortado em linhas
    // de 72 caracteres. So que em CW 72 caracteres levam mais de 40 segundos
    // a 20 PPM: o operador ficava vendo o sinal passar sem nada na tela, e o
    // texto chegava todo de uma vez. Agora cada pedaco sai assim que o nucleo
    // o entrega, e quem junta as linhas e a tela.
    void textoFluxo(const QString& pedaco);

private:
    void aplicarParams();

    State  state_ = State::Stopped;
    Params params_;
    CwCore core_;

    // reamostragem para 8 kHz
    std::vector<float> bloco_;
    std::vector<float> ent_;
    double   resamplePos_  = 0.0;
    float    resampleLast_ = 0.0f;
    bool     temUltimo_    = false;
    uint32_t ultimaSps_    = 0;

    // A quebra de linha agora e feita na tela, que sabe a largura disponivel.
    // Aqui so passamos o texto adiante.
};

} // namespace masdr
