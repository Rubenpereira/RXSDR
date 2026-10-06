#include "SstvManager.h"
#include "ImagemPng.h"

#include <QDateTime>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace masdr {

SstvManager::SstvManager(QObject* parent)
    : QObject(parent)
    , fila_([this](const std::vector<int16_t>& pcm, uint32_t sps) {
          // o SstvCore reduz sozinho para ~12 kS/s
          core_.alimentar(pcm.data(), pcm.size(), sps);
          colher();
      })
{}

SstvManager::~SstvManager()
{
    fila_.parar();
}

QString SstvManager::pasta()
{
    return pastaImagens(QStringLiteral("RXSDR_SSTV"));
}

void SstvManager::opcoes(const QJsonObject& j)
{
    if (j.contains("vis"))        core_.aceitarVis = j.value("vis").toBool(true);
    if (j.contains("inclinacao")) core_.autoInclinacao = j.value("inclinacao").toBool(true);
    if (j.contains("salvar"))     salvar_ = j.value("salvar").toBool(true);
}

bool SstvManager::start(const QJsonObject& j)
{
    opcoes(j);
    if (state_ == State::Running) return true;
    core_.limpar();
    fila_.ligar();
    state_ = State::Running;
    emit logLine(QStringLiteral("[SSTV] esperando o VIS (cabecalho) - Robot, Martin, Scottie, SC2 e PD sao reconhecidos sozinhos"));
    return true;
}

void SstvManager::stop()
{
    fila_.parar();
    if (state_ == State::Stopped) return;
    state_ = State::Stopped;
    core_.parar();      // a imagem pela metade (1/4 ou mais) ainda e salva
    colher();
}

void SstvManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running) return;
    fila_.empurrar(samples, count, sps);
}

QJsonObject SstvManager::comando(const QString& acao, const QJsonObject& j)
{
    opcoes(j);
    if (acao == QLatin1String("comecar")) {
        const int modo = std::clamp(j.value("modo").toInt(0), 0, SstvCore::nModos() - 1);
        if (state_ != State::Running) start(QJsonObject());
        core_.comecarAgora(modo);
    } else if (acao == QLatin1String("terminar")) {
        core_.parar();
        colher();
    } else if (acao == QLatin1String("limpar")) {
        core_.limpar();
    } else if (acao == QLatin1String("abrirpasta")) {
        abrirPasta(pasta());
    }
    QJsonObject r = statusJson();
    r["ok"] = true;
    return r;
}

// Imagem terminada: PNG na pasta RXSDR_SSTV + linha no texto + historico
void SstvManager::colher()
{
    std::vector<uint32_t> argb;
    int w = 0, h = 0, modo = 0;
    bool porVis = false;
    while (core_.pegarTerminada(argb, w, h, modo, porVis)) {
        const QString nomeModo = QString::fromLatin1(SstvCore::nomeModo(modo));
        const uint64_t hz = freqHz ? freqHz() : 0;
        const QDateTime agora = QDateTime::currentDateTimeUtc();
        Feita f;
        f.w = w; f.h = h;
        f.rotulo = QStringLiteral("%1  %2 UTC  %3 MHz").arg(nomeModo, agora.toString(QStringLiteral("HH:mm")))
                       .arg(double(hz) / 1e6, 0, 'f', 3);
        f.png = pngArgb(argb.data(), w, h);
        QString linha = QStringLiteral("[SSTV] ") + f.rotulo + (porVis ? QString() : QStringLiteral("  (sem VIS)"));
        if (salvar_ && !f.png.isEmpty()) {
            QString tag = nomeModo; tag.remove(QLatin1Char(' '));
            const QString nome = QStringLiteral("RXSDR_SSTV_%1_%2.png")
                                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")), tag);
            const QString cam = gravarArquivo(pasta(), nome, f.png);
            if (!cam.isEmpty()) { f.arquivo = cam; linha += QStringLiteral("  salva em ") + cam; }
            else linha += QStringLiteral("  (nao consegui salvar em ") + pasta() + QLatin1Char(')');
        }
        emit logLine(linha);
        std::lock_guard<std::mutex> lk(m_);
        f.id = proxId_++;
        hist_.push_front(std::move(f));
        while (hist_.size() > 12) hist_.pop_back();
    }
}

QJsonObject SstvManager::statusJson()
{
    const SstvCore::Status s = core_.status();
    QJsonObject o;
    o["state"]   = state_ == State::Running ? QStringLiteral("running") : QStringLiteral("stopped");
    o["estado"]  = s.estado == SstvCore::RECEBENDO ? QStringLiteral("recebendo")
                 : s.estado == SstvCore::PRONTA    ? QStringLiteral("pronta") : QStringLiteral("esperando");
    o["modo"]    = s.modo;
    o["nomeModo"] = s.modo >= 0 ? QString::fromLatin1(SstvCore::nomeModo(s.modo)) : QString();
    o["linha"]   = s.linha;
    o["linhas"]  = s.linhas;
    o["desvioHz"] = std::round(s.desvioHz);
    o["inclinacaoPpm"] = std::round(s.inclinacaoPpm);
    o["nivelDb"] = std::round(s.nivelDb);
    o["porVis"]  = s.porVis;
    o["sincronismos"] = s.sincronismos;
    o["faltas"]  = s.faltas;
    o["imagens"] = s.imagens;
    o["vis"]     = core_.aceitarVis.load();
    o["inclinacao"] = core_.autoInclinacao.load();
    o["salvar"]  = salvar_;
    o["pasta"]   = pasta();

    // espectro do audio de 900 a 2500 Hz, em 80 pontos
    std::vector<float> db;
    double hzBin = 1.0;
    core_.espectro(db, hzBin);
    QJsonArray esp;
    if (!db.empty() && hzBin > 0) {
        for (int i = 0; i < 80; ++i) {
            const double f0 = 900.0 + i * 20.0, f1 = f0 + 20.0;
            size_t a = size_t(f0 / hzBin), b = std::max(a + 1, size_t(f1 / hzBin));
            b = std::min(b, db.size());
            float mx = -200.f;
            for (size_t k = a; k < b; ++k) mx = std::max(mx, db[k]);
            esp.append(std::round(mx * 10.f) / 10.0);
        }
    }
    o["espectro"] = esp;

    {
        atualizarCache();
        std::lock_guard<std::mutex> lk(m_);
        o["versao"] = double(verCache_);
        o["w"] = wCache_;
        o["h"] = hCache_;
        QJsonArray hs;
        for (const Feita& f : hist_)
            hs.append(QJsonObject{{"id", f.id}, {"rotulo", f.rotulo}, {"arquivo", f.arquivo}, {"w", f.w}, {"h", f.h}});
        o["historico"] = hs;
    }
    QJsonArray modos;
    for (int m = 0; m < SstvCore::nModos(); ++m)
        modos.append(QJsonObject{{"nome", QString::fromLatin1(SstvCore::nomeModo(m))}, {"w", SstvCore::larguraModo(m)},
                                 {"h", SstvCore::alturaModo(m)}, {"s", std::round(SstvCore::duracaoModo(m))}});
    o["modos"] = modos;
    return o;
}

void SstvManager::atualizarCache()
{
    std::lock_guard<std::mutex> lk(m_);
    int w = 0, h = 0;
    uint64_t v = verCache_;
    if (core_.imagem(buf_, w, h, v)) {
        verCache_ = v; wCache_ = w; hCache_ = h;
        pngAtual_ = (w > 0 && h > 0) ? pngArgb(buf_.data(), w, h) : QByteArray();
    }
}

QByteArray SstvManager::png(const QString& qual)
{
    if (qual == QLatin1String("atual")) {
        atualizarCache();
        std::lock_guard<std::mutex> lk(m_);
        return pngAtual_;
    }
    const int id = qual.toInt();
    std::lock_guard<std::mutex> lk(m_);
    for (const Feita& f : hist_) if (f.id == id) return f.png;
    return QByteArray();
}

} // namespace masdr
