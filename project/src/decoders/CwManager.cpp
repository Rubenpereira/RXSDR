#include "CwManager.h"

#include <algorithm>

namespace masdr {

namespace {
constexpr double kCoreRate = 8000.0;
}

CwManager::CwManager(QObject* parent) : QObject(parent) {}
CwManager::~CwManager() { stop(); }

QString CwManager::stateString() const
{
    switch (state_) {
        case State::Stopped: return QStringLiteral("stopped");
        case State::Running: return QStringLiteral("running");
        case State::Error:   return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

void CwManager::setParams(const Params& p)
{
    params_ = p;
    aplicarParams();
}

void CwManager::aplicarParams()
{
    CwCore::Params cp;
    cp.sampleRate = kCoreRate;
    cp.tomHz      = params_.tomHz > 0.0f ? double(params_.tomHz) : 0.0;
    cp.autoTom    = params_.autoTom;
    core_.setParams(cp);
}

bool CwManager::start()
{
    core_.reset();
    aplicarParams();
    resamplePos_  = 0.0;
    resampleLast_ = 0.0f;
    temUltimo_    = false;
    ultimaSps_    = 0;
    state_ = State::Running;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[CW] decodificador iniciado"));
    emit logLine(QStringLiteral("[CW] o tom e a velocidade sao medidos sozinhos"));
    return true;
}

void CwManager::stop()
{
    if (state_ == State::Stopped) return;
    state_ = State::Stopped;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[CW] decodificador parado"));
}

void CwManager::restart()
{
    if (state_ != State::Running) { start(); return; }
    core_.reset();
    aplicarParams();
    resamplePos_ = 0.0;
    temUltimo_   = false;
    ultimaSps_   = 0;
}

QJsonObject CwManager::statusJson() const
{
    QJsonObject o;
    o["state"]     = stateString();
    o["tom"]       = core_.tomMedido();
    o["ppm"]       = core_.ppm();
    o["letras"]    = core_.letras();
    o["naoLidos"]  = core_.naoLidos();
    return o;
}

// ---------------------------------------------------------------------------

void CwManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running || !samples || count <= 0 || sps == 0) return;

    const double passo = double(sps) / kCoreRate;
    if (passo <= 0.0) return;

    if (sps != ultimaSps_) {
        ultimaSps_   = sps;
        resamplePos_ = 0.0;
        temUltimo_   = false;
    }

    // Junta a ultima amostra do bloco anterior para interpolar na emenda
    const size_t nEnt = size_t(count) + (temUltimo_ ? 1 : 0);
    ent_.resize(nEnt);
    size_t w = 0;
    if (temUltimo_) { ent_[0] = resampleLast_; w = 1; }
    for (int i = 0; i < count; ++i) ent_[w + size_t(i)] = float(samples[i]) / 32768.0f;
    if (nEnt < 2) return;

    bloco_.clear();
    bloco_.reserve(size_t(double(nEnt) / passo) + 4);
    double pos = resamplePos_;
    while (pos + 1.0 < double(nEnt)) {
        const size_t i0 = size_t(pos);
        const float frac = float(pos - double(i0));
        bloco_.push_back(ent_[i0] + (ent_[i0 + 1] - ent_[i0]) * frac);
        pos += passo;
    }
    resampleLast_ = ent_[nEnt - 1];
    temUltimo_    = true;
    resamplePos_  = pos - double(nEnt - 1);
    if (resamplePos_ < 0.0) resamplePos_ = 0.0;

    if (bloco_.empty()) return;
    const std::string txt = core_.feed(bloco_.data(), bloco_.size());
    if (!txt.empty()) emit textoFluxo(QString::fromStdString(txt));
}

} // namespace masdr
