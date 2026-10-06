#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "WefaxCore.h"
#include "FilaAudio.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  WefaxManager - fax meteorologico em HF (o mesmo WefaxCore do RXSDR Nativo)
//
//  Mesmo arranjo do SSTV: audio numa fila, thread propria, a pagina pergunta
//  o estado e busca o PNG (meia resolucao, em tons de cinza) quando a versao
//  muda. O fax terminado vai inteiro para a pasta RXSDR_WEFAX da Area de
//  Trabalho e para o historico (6 ultimos).
// ---------------------------------------------------------------------------
class WefaxManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running };

    explicit WefaxManager(QObject* parent = nullptr);
    ~WefaxManager() override;

    // Frequencia do VFO, para o rotulo e o nome do arquivo
    std::function<uint64_t()> freqHz;

    bool start(const QJsonObject& opcoes);
    void stop();
    State state() const { return state_; }

    // comecar, terminar, limpar, endireitar, inclinacao, alinhar, config, opcoes, abrirpasta
    QJsonObject comando(const QString& acao, const QJsonObject& j);
    QJsonObject statusJson();
    // "atual" ou o numero de uma imagem do historico; vazio se nao ha
    QByteArray png(const QString& qual);

    void feedAudio(const int16_t* samples, int count, uint32_t sps);

    static QString pasta();

signals:
    void logLine(const QString& line);

private:
    struct Feita { int id; QString rotulo, arquivo; int w, h; QByteArray png; };

    void opcoes(const QJsonObject& j);
    void colher();
    void atualizarCache();

    State state_ = State::Stopped;
    WefaxCore core_;
    bool salvar_ = true;
    int lpm_ = 120, ioc_ = 576;

    std::mutex m_;                 // historico e cache do PNG atual
    std::deque<Feita> hist_;
    int proxId_ = 1;
    std::vector<uint32_t> buf_;
    std::vector<uint8_t> cinza_;
    uint64_t verCache_ = 0;
    int wCache_ = 0, hCache_ = 0;
    QByteArray pngAtual_;

    FilaAudio fila_;               // por ultimo: para a thread antes do resto sumir
};

} // namespace masdr
