#include "AnaliseManager.h"

#include <algorithm>

namespace masdr {

AnaliseManager::AnaliseManager(QObject* parent) : QObject(parent) {}
AnaliseManager::~AnaliseManager() { stop(); }

QString AnaliseManager::stateString() const
{
    switch (state_) {
        case State::Stopped: return QStringLiteral("stopped");
        case State::Running: return QStringLiteral("running");
        case State::Error:   return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject AnaliseManager::statusJson() const
{
    QJsonObject o;
    o["state"]     = stateString();
    o["seconds"]   = core_.segundosJuntados();
    o["needed"]    = core_.segundosNecessarios();
    o["done"]      = concluido_;
    return o;
}

bool AnaliseManager::start()
{
    core_.limpar();
    concluido_    = false;
    resamplePos_  = 0.0;
    resampleLast_ = 0.0f;
    temUltimo_    = false;
    ultimaSps_    = 0;

    state_ = State::Running;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[ANALISE] juntando audio - deixe o sinal presente por %1 segundos")
                     .arg(core_.segundosNecessarios(), 0, 'f', 0));
    return true;
}

void AnaliseManager::stop()
{
    if (state_ == State::Stopped) return;
    state_ = State::Stopped;
    emit stateChanged(state_);
}

void AnaliseManager::concluir()
{
    if (concluido_) return;
    concluido_ = true;

    const AnaliseCore::Resultado r = core_.analisar();
    emit logLine(QStringLiteral("[ANALISE] ----- resultado -----"));
    for (const std::string& l : r.linhas)
        emit logLine(QStringLiteral("[ANALISE] ") + QString::fromStdString(l));
    emit logLine(QStringLiteral("[ANALISE] ---------------------"));
    emit logLine(QStringLiteral("[ANALISE] Para medir outro sinal, clique em Analisar de novo."));
}

void AnaliseManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running || concluido_ || !samples || count <= 0 || sps == 0) return;

    const double dstHz = 8000.0;
    const double passo = double(sps) / dstHz;
    if (passo <= 0.0) return;

    if (sps != ultimaSps_) {
        ultimaSps_   = sps;
        resamplePos_ = 0.0;
        temUltimo_   = false;
    }

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

    if (core_.alimentar(bloco_.data(), bloco_.size()))
        concluir();
}

} // namespace masdr
