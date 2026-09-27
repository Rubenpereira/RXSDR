#include "RttyManager.h"

#include <cmath>

namespace masdr {

RttyManager::RttyManager(QObject* parent)
    : QObject(parent)
{
    aplicarParams();
}

RttyManager::~RttyManager()
{
    stop();
}

void RttyManager::setParams(const Params& p)
{
    params_ = p;
    aplicarParams();
}

void RttyManager::aplicarParams()
{
    RttyCore::Params cp;
    // 8 kHz: 176 amostras por bit a 45,45 baud - folga de sobra para separar
    // dois tons a 170 Hz, e o audio USB nao passa de 3 kHz.
    cp.sampleRate = 8000.0;
    cp.baudRate   = params_.baudRate   > 0 ? double(params_.baudRate)   : 45.45;
    cp.shift      = params_.shift      > 0 ? double(params_.shift)      : 170.0;
    cp.centerFreq = params_.centerFreq > 0 ? double(params_.centerFreq) : 1500.0;
    cp.invert     = params_.invert;
    cp.autoTom    = params_.autoTom;
    cp.usos       = params_.usos;
    core_.setParams(cp);
}

QString RttyManager::stateString() const
{
    return state_ == State::Running ? QStringLiteral("running") : QStringLiteral("stopped");
}

QJsonObject RttyManager::statusJson() const
{
    QJsonObject o;
    o["state"]      = stateString();
    o["baudRate"]   = double(params_.baudRate);
    o["shift"]      = double(params_.shift);
    o["centerFreq"] = core_.params().centerFreq;   // o que o nucleo esta usando agora
    o["invert"]     = params_.invert;
    o["autoTom"]    = params_.autoTom;
    o["sync"]       = core_.sincronizado();
    o["chars"]      = core_.totalChars();
    o["errors"]     = core_.erros();
    return o;
}

bool RttyManager::start()
{
    if (state_ == State::Running) return true;
    aplicarParams();
    resamplePos_  = 0.0;
    resampleLast_ = 0.0f;
    temUltimo_    = false;
    ultimaSps_    = 0;
    state_ = State::Running;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[RTTY] decodificador iniciado - %1 baud, shift %2 Hz%3")
                     .arg(double(params_.baudRate)).arg(double(params_.shift))
                     .arg(params_.autoTom ? QStringLiteral(", tom medido sozinho")
                                          : QStringLiteral(", tom %1 Hz").arg(double(params_.centerFreq))));
    return true;
}

void RttyManager::stop()
{
    if (state_ == State::Stopped) return;
    state_ = State::Stopped;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[RTTY] decodificador parado"));
}

// Audio do radio (perto de 48 kHz) -> 8 kHz por interpolacao linear, guardando
// a sobra entre chamadas para nao quebrar a fase. O audio USB ja vem filtrado
// abaixo de 3 kHz, entao nao ha o que dobrar acima de 4 kHz.
void RttyManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running || !samples || count <= 0 || sps == 0) return;

    const double passo = double(sps) / core_.params().sampleRate;
    if (passo <= 0.0) return;
    if (sps != ultimaSps_) { ultimaSps_ = sps; resamplePos_ = 0.0; temUltimo_ = false; }

    std::vector<float> ent;
    ent.reserve(size_t(count) + 1);
    if (temUltimo_) ent.push_back(resampleLast_);
    for (int i = 0; i < count; ++i) ent.push_back(float(samples[i]) / 32768.0f);
    if (ent.size() < 2) return;

    bloco_.clear();
    double pos = resamplePos_;
    while (pos + 1.0 < double(ent.size())) {
        const size_t i0 = size_t(pos);
        const float frac = float(pos - double(i0));
        bloco_.push_back(ent[i0] + (ent[i0 + 1] - ent[i0]) * frac);
        pos += passo;
    }
    resampleLast_ = ent.back();
    temUltimo_    = true;
    resamplePos_  = pos - double(ent.size() - 1);
    if (resamplePos_ < 0.0) resamplePos_ = 0.0;
    if (bloco_.empty()) return;

    const std::string txt = core_.feed(bloco_.data(), bloco_.size());
    if (core_.tomNovo())
        emit logLine(QStringLiteral("[RTTY] tom central medido: %1 Hz")
                         .arg(int(std::lround(core_.tomMedido()))));
    if (!txt.empty()) emit textoFluxo(QString::fromLatin1(txt.data(), int(txt.size())));
}

} // namespace masdr
