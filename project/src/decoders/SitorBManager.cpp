#include "SitorBManager.h"

#include <algorithm>
#include <cmath>

namespace masdr {

SitorBManager::SitorBManager(QObject* parent)
    : QObject(parent)
{
    aplicarParams();
}

SitorBManager::~SitorBManager()
{
    stop();
}

void SitorBManager::setParams(const Params& p)
{
    params_ = p;
    aplicarParams();
}

void SitorBManager::aplicarParams()
{
    SitorBCore::Params cp;
    // O nucleo trabalha a 8 kHz: 80 amostras por bit a 100 baud, folgado para
    // separar mark e space, que estao a apenas 170 Hz um do outro.
    cp.sampleRate = 8000.0;
    cp.baudRate   = params_.baudRate   > 0 ? double(params_.baudRate)   : 100.0;
    cp.shift      = params_.shift      > 0 ? double(params_.shift)      : 170.0;
    cp.centerFreq = params_.centerFreq > 0 ? double(params_.centerFreq) : 1700.0;
    cp.invert     = params_.invert;
    core_.setParams(cp);
}

QString SitorBManager::stateString() const
{
    switch (state_) {
        case State::Stopped:  return QStringLiteral("stopped");
        case State::Starting: return QStringLiteral("starting");
        case State::Running:  return QStringLiteral("running");
        case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject SitorBManager::statusJson() const
{
    QJsonObject o;
    o["state"]      = stateString();
    o["baudRate"]   = double(params_.baudRate);
    o["shift"]      = double(params_.shift);
    o["centerFreq"] = double(params_.centerFreq);
    o["invert"]     = params_.invert;
    // Metricas uteis para saber se esta realmente decodificando
    o["sync"]       = core_.sincronizado();
    o["chars"]      = core_.totalChars();
    o["valid"]      = core_.validChars();
    o["fecFixes"]   = core_.corrigidos();
    if (!lastError_.isEmpty()) o["error"] = lastError_;
    return o;
}

bool SitorBManager::start()
{
    if (state_ == State::Running) return true;

    core_.reset();
    aplicarParams();
    resamplePos_  = 0.0;
    resampleLast_ = 0.0f;
    temUltimo_    = false;
    ultimaSps_    = 0;
    linhaParcial_.clear();
    lastError_.clear();

    state_ = State::Running;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[SITOR-B] decodificador iniciado - %1 baud, shift %2 Hz, tom central %3 Hz")
                     .arg(double(params_.baudRate)).arg(double(params_.shift)).arg(double(params_.centerFreq)));
    return true;
}

void SitorBManager::stop()
{
    if (state_ == State::Stopped) return;

    // Solta o que ficou pela metade antes de encerrar
    if (!linhaParcial_.trimmed().isEmpty())
        emit logLine(QStringLiteral("[SITOR-B] ") + linhaParcial_.trimmed());
    linhaParcial_.clear();

    state_ = State::Stopped;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[SITOR-B] decodificador parado"));
}

// ---------------------------------------------------------------------------
//  Audio -> nucleo
//  O radio entrega PCM 16 bits na taxa dele (perto de 48 kHz). O nucleo
//  trabalha a 8 kHz, entao reamostramos por interpolacao linear guardando a
//  sobra entre chamadas, para nao perder continuidade de fase.
// ---------------------------------------------------------------------------
void SitorBManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running || !samples || count <= 0 || sps == 0) return;

    const double dstHz = core_.params().sampleRate;
    const double passo = double(sps) / dstHz;
    if (passo <= 0.0) return;

    if (sps != ultimaSps_) {
        ultimaSps_    = sps;
        resamplePos_  = 0.0;
        temUltimo_    = false;
    }

    bloco_.clear();
    bloco_.reserve(size_t(double(count) / passo) + 4);

    // Junta a ultima amostra do bloco anterior para poder interpolar na emenda
    std::vector<float> ent;
    ent.reserve(size_t(count) + 1);
    if (temUltimo_) ent.push_back(resampleLast_);
    for (int i = 0; i < count; ++i) ent.push_back(float(samples[i]) / 32768.0f);
    if (ent.size() < 2) return;

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
    if (!txt.empty()) despejarTexto(txt);
}

// ---------------------------------------------------------------------------
//  Acumula o texto e so emite quando fecha linha. NAVTEX manda mensagens
//  longas; mandar caractere a caractere para a interface picotaria tudo.
// ---------------------------------------------------------------------------
void SitorBManager::despejarTexto(const std::string& txt)
{
    for (char c : txt) {
        if (c == '\n' || c == '\r') {
            const QString l = linhaParcial_.trimmed();
            if (!l.isEmpty()) emit logLine(QStringLiteral("[SITOR-B] ") + l);
            linhaParcial_.clear();
        } else {
            linhaParcial_ += QLatin1Char(c);
            // Linha muito longa sem quebra: corta para nao segurar o texto
            if (linhaParcial_.size() >= 80) {
                emit logLine(QStringLiteral("[SITOR-B] ") + linhaParcial_);
                linhaParcial_.clear();
            }
        }
    }
}

} // namespace masdr
