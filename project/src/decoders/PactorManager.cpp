#include "PactorManager.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>

namespace masdr {

namespace {
constexpr double kTaxa = 8000.0;   // o PactorCore trabalha a 8 kHz
}

PactorManager::PactorManager(QObject* parent)
    : QObject(parent)
    , fila_([this](const std::vector<int16_t>& pcm, uint32_t sps) { processar(pcm, sps); })
{}

PactorManager::~PactorManager()
{
    fila_.parar();
}

QString PactorManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject PactorManager::statusJson() const
{
    QJsonObject o;
    o["state"]         = stateString();
    o["binaryPresent"] = true;
    {
        std::lock_guard<std::mutex> lk(coreMutex_);
        if (core_) {
            o["locked"]      = core_->travado();
            o["baud"]        = core_->baud();
            o["tom"]         = std::round(core_->tomCentral());
            o["pacotes"]     = core_->pacotes();
            o["somados"]     = core_->somados();
            o["descartados"] = core_->descartados();
            o["formato"]     = QString::fromLatin1(core_->formato());
            o["indicativo"]  = QString::fromStdString(core_->indicativo());
        }
    }
    if (!lastError_.isEmpty()) o["error"] = lastError_;
    return o;
}

bool PactorManager::start()
{
    if (state_ == State::Running) return true;

    fila_.parar();
    {
        std::lock_guard<std::mutex> lk(coreMutex_);
        core_ = std::make_unique<PactorCore>(kTaxa);
    }
    resamplePos_  = 0.0;
    resampleLast_ = 0.0f;
    temUltimo_    = false;
    ultimaSps_    = 0;
    relogio_      = 0.0;
    ultimoTexto_  = -1e9;
    lastError_.clear();
    fila_.ligar();

    state_ = State::Running;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[PACTOR-I] iniciado - procurando pacotes (100/200 baud, shift 200 Hz, USB)"));
    return true;
}

void PactorManager::stop()
{
    fila_.parar();
    if (state_ == State::Stopped) return;
    state_ = State::Stopped;
    emit stateChanged(state_);
    emit logLine(QStringLiteral("[PACTOR-I] parado"));
}

void PactorManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running) return;
    fila_.empurrar(samples, count, sps);
}

// ---------------------------------------------------------------------------
//  Na thread do decodificador: reamostra para 8 kHz (interpolacao linear,
//  guardando a sobra entre blocos) e passa ao nucleo.
// ---------------------------------------------------------------------------
void PactorManager::processar(const std::vector<int16_t>& pcm, uint32_t sps)
{
    if (pcm.empty() || sps == 0) return;
    const double passo = double(sps) / kTaxa;

    if (sps != ultimaSps_) {
        ultimaSps_   = sps;
        resamplePos_ = 0.0;
        temUltimo_   = false;
    }

    ent_.clear();
    ent_.reserve(pcm.size() + 1);
    if (temUltimo_) ent_.push_back(resampleLast_);
    for (int16_t v : pcm) ent_.push_back(float(v) / 32768.0f);
    if (ent_.size() < 2) return;

    bloco_.clear();
    bloco_.reserve(size_t(double(pcm.size()) / passo) + 4);
    double pos = resamplePos_;
    while (pos + 1.0 < double(ent_.size())) {
        const size_t i0 = size_t(pos);
        const float frac = float(pos - double(i0));
        bloco_.push_back(ent_[i0] + (ent_[i0 + 1] - ent_[i0]) * frac);
        pos += passo;
    }
    resampleLast_ = ent_.back();
    temUltimo_    = true;
    resamplePos_  = std::max(0.0, pos - double(ent_.size() - 1));
    if (bloco_.empty()) return;

    std::string saida, indicativo;
    {
        std::lock_guard<std::mutex> lk(coreMutex_);
        if (!core_) return;
        saida = core_->feed(bloco_.data(), bloco_.size());
        indicativo = core_->indicativo();
    }
    relogio_ += double(bloco_.size()) / kTaxa;
    if (saida.empty()) return;

    // Texto depois de 1 minuto calado: uma linha com a hora (e o indicativo)
    if (relogio_ - ultimoTexto_ > 60.0) {
        QString cab = QStringLiteral("\n[PACTOR-I] ")
                    + QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss"))
                    + QStringLiteral(" UTC");
        if (!indicativo.empty()) cab += QStringLiteral("  ") + QString::fromStdString(indicativo);
        emit textoFluxo(cab + QLatin1Char('\n'));
    }
    ultimoTexto_ = relogio_;
    emit textoFluxo(QString::fromLatin1(saida.data(), int(saida.size())));
}

} // namespace masdr
