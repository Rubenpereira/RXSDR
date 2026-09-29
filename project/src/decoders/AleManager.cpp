#include "AleManager.h"

#include <QDateTime>

namespace masdr {

AleManager::AleManager(QObject* parent)
    : QObject(parent)
{
}

AleManager::~AleManager()
{
    stop();
}

QString AleManager::stateString() const
{
    return state_ == State::Running ? QStringLiteral("running") : QStringLiteral("stopped");
}

QJsonObject AleManager::statusJson() const
{
    QJsonObject o;
    o["state"]    = stateString();
    o["modo"]     = modo_;
    o["sync"]     = core_.sincronizado();
    o["palavras"] = core_.palavras();
    o["chamadas"] = core_.chamadas();
    o["desvio"]   = core_.desvioHz();   // tom da ultima chamada, em Hz (+ = acima)
    return o;
}

bool AleManager::start()
{
    if (state_ == State::Running) return true;
    core_.reset();
    resamplePos_  = 0.0;
    resampleLast_ = 0.0f;
    temUltimo_    = false;
    ultimaSps_    = 0;
    state_ = State::Running;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[ALE] decodificador iniciado - 2G ALE (MIL-STD-188-141A/B, FED-STD-1045), 8-FSK 125 baud, procura o tom em +-500 Hz"));
    return true;
}

void AleManager::stop()
{
    if (state_ == State::Stopped) return;
    state_ = State::Stopped;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[ALE] decodificador parado"));
}

// Audio do radio (perto de 48 kHz) -> 8 kHz por interpolacao linear, igual ao
// RttyManager. Os tons do ALE vao ate 2500 Hz: nada a dobrar acima de 4 kHz.
void AleManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running || !samples || count <= 0 || sps == 0) return;

    const double passo = double(sps) / double(AleCore::kFs);
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

    const std::vector<std::string> chamadas = core_.feed(bloco_.data(), bloco_.size());
    for (const std::string& c : chamadas) {
        const QString hora = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss"));
        emit logLine(QStringLiteral("[ALE] %1 UTC  %2").arg(hora, QString::fromLatin1(c.data(), int(c.size()))));
    }
}

} // namespace masdr
