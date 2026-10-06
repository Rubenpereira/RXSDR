#include "WefaxManager.h"
#include "ImagemPng.h"

#include <QDateTime>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace masdr {

WefaxManager::WefaxManager(QObject* parent)
    : QObject(parent)
    , fila_([this](const std::vector<int16_t>& pcm, uint32_t sps) {
          core_.alimentar(pcm.data(), pcm.size(), sps);
          colher();
      })
{}

WefaxManager::~WefaxManager()
{
    fila_.parar();
}

QString WefaxManager::pasta()
{
    return pastaImagens(QStringLiteral("RXSDR_WEFAX"));
}

void WefaxManager::opcoes(const QJsonObject& j)
{
    if (j.contains("automatico"))     core_.automatico = j.value("automatico").toBool(true);
    if (j.contains("inverter"))       core_.inverter = j.value("inverter").toBool(false);
    if (j.contains("endireitarAuto")) core_.autoInclinacao = j.value("endireitarAuto").toBool(true);
    if (j.contains("salvar"))         salvar_ = j.value("salvar").toBool(true);
    if (j.contains("lpm") || j.contains("ioc")) {
        lpm_ = std::clamp(j.value("lpm").toInt(lpm_), 30, 300);
        ioc_ = j.value("ioc").toInt(ioc_) == 288 ? 288 : 576;
        core_.configurar(lpm_, ioc_);
    }
}

bool WefaxManager::start(const QJsonObject& j)
{
    opcoes(j);
    if (state_ == State::Running) return true;
    core_.limpar();
    fila_.ligar();
    state_ = State::Running;
    emit logLine(QStringLiteral("[WEFAX] esperando o tom de inicio (300 Hz) - a fase acerta a margem e as linhas por minuto sozinha"));
    return true;
}

void WefaxManager::stop()
{
    fila_.parar();
    if (state_ == State::Stopped) return;
    state_ = State::Stopped;
    core_.parar();      // a imagem pela metade ainda e salva
    colher();
}

void WefaxManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (state_ != State::Running) return;
    fila_.empurrar(samples, count, sps);
}

QJsonObject WefaxManager::comando(const QString& acao, const QJsonObject& j)
{
    opcoes(j);
    if (acao == QLatin1String("comecar")) {
        if (state_ != State::Running) start(QJsonObject());
        core_.configurar(lpm_, ioc_);
        core_.comecarAgora();
    } else if (acao == QLatin1String("terminar")) {
        core_.parar();
        colher();
    } else if (acao == QLatin1String("limpar")) {
        core_.limpar();
    } else if (acao == QLatin1String("recomecar")) {
        // canal novo da lista: o que estava pela metade fica salvo e volta a
        // esperar o tom de inicio
        core_.parar();
        colher();
        core_.limpar();
    } else if (acao == QLatin1String("endireitar")) {
        core_.endireitar();
    } else if (acao == QLatin1String("inclinacao")) {
        core_.definirInclinacao(j.value("valor").toDouble(0.0));
    } else if (acao == QLatin1String("alinhar")) {
        // a pagina mostra a imagem em meia resolucao
        core_.alinharEm(int(std::lround(j.value("coluna").toDouble(0.0) * 2.0)));
    } else if (acao == QLatin1String("abrirpasta")) {
        abrirPasta(pasta());
    }
    QJsonObject r = statusJson();
    r["ok"] = true;
    return r;
}

// Fax terminado: PNG inteiro (tons de cinza) na pasta RXSDR_WEFAX + historico
void WefaxManager::colher()
{
    WefaxCore::Terminada t;
    while (core_.pegarTerminada(t)) {
        const uint64_t hz = freqHz ? freqHz() : 0;
        Feita f;
        f.w = t.w; f.h = t.h;
        f.rotulo = QStringLiteral("%1 UTC  %2 kHz  %3 LPM / IOC %4  %5x%6")
                       .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm")))
                       .arg(double(hz) / 1e3, 0, 'f', 1)
                       .arg(t.lpm).arg(t.ioc).arg(t.w).arg(t.h);
        f.png = pngCinza(t.cinza.data(), t.w, t.h);
        QString linha = QStringLiteral("[WEFAX] ") + f.rotulo;
        if (salvar_ && !f.png.isEmpty()) {
            const QString nome = QStringLiteral("RXSDR_WEFAX_%1_%2kHz.png")
                                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")))
                                     .arg(double(hz) / 1e3, 0, 'f', 0);
            const QString cam = gravarArquivo(pasta(), nome, f.png);
            if (!cam.isEmpty()) { f.arquivo = cam; linha += QStringLiteral("  salva em ") + cam; }
            else linha += QStringLiteral("  (nao consegui salvar em ") + pasta() + QLatin1Char(')');
        }
        emit logLine(linha);
        std::lock_guard<std::mutex> lk(m_);
        f.id = proxId_++;
        hist_.push_front(std::move(f));
        while (hist_.size() > 6) hist_.pop_back();
        t = WefaxCore::Terminada();
    }
}

void WefaxManager::atualizarCache()
{
    std::lock_guard<std::mutex> lk(m_);
    int w = 0, h = 0;
    uint64_t v = verCache_;
    if (core_.imagemMeia(buf_, w, h, v)) {
        verCache_ = v; wCache_ = w; hCache_ = h;
        if (w > 0 && h > 0 && buf_.size() >= size_t(w) * size_t(h)) {
            cinza_.resize(size_t(w) * size_t(h));
            for (size_t i = 0; i < cinza_.size(); ++i) cinza_[i] = uint8_t(buf_[i] & 0xFF);
            pngAtual_ = pngCinza(cinza_.data(), w, h);
        } else {
            pngAtual_.clear();
        }
    }
}

QJsonObject WefaxManager::statusJson()
{
    const WefaxCore::Status s = core_.status();
    QJsonObject o;
    o["state"] = state_ == State::Running ? QStringLiteral("running") : QStringLiteral("stopped");
    static const char* kEst[] = {"esperando", "inicio", "fase", "recebendo", "pronta"};
    o["estado"] = QString::fromLatin1(kEst[std::clamp(int(s.estado), 0, 4)]);
    o["lpm"] = s.lpm;
    o["ioc"] = s.ioc;
    o["largura"] = s.largura;
    o["linhas"] = s.linhas;
    o["nivelDb"] = std::round(s.nivelDb);
    o["inclinacao"] = std::round(s.inclinacao * 100.0) / 100.0;
    o["deslocamento"] = s.deslocamento;
    o["pelaFase"] = s.alinhadoPelaFase;
    o["imagens"] = s.imagens;
    o["automatico"] = core_.automatico.load();
    o["inverter"] = core_.inverter.load();
    o["endireitarAuto"] = core_.autoInclinacao.load();
    o["salvar"] = salvar_;
    o["lpmCfg"] = lpm_;
    o["iocCfg"] = ioc_;
    o["pasta"] = pasta();
    atualizarCache();
    std::lock_guard<std::mutex> lk(m_);
    o["versao"] = double(verCache_);
    o["w"] = wCache_;
    o["h"] = hCache_;
    QJsonArray hs;
    for (const Feita& f : hist_)
        hs.append(QJsonObject{{"id", f.id}, {"rotulo", f.rotulo}, {"arquivo", f.arquivo}, {"w", f.w}, {"h", f.h}});
    o["historico"] = hs;
    return o;
}

QByteArray WefaxManager::png(const QString& qual)
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
