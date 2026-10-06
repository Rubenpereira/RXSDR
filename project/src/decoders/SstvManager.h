#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "SstvCore.h"
#include "FilaAudio.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  SstvManager - imagens SSTV (o mesmo SstvCore do RXSDR Nativo)
//
//  O audio do radio entra numa fila e e decodificado numa thread propria. A
//  pagina pergunta o estado (/api/sstv/status) e, quando a versao da imagem
//  muda, busca o PNG (/api/sstv/png/atual). Cada imagem terminada vira PNG
//  na pasta RXSDR_SSTV da Area de Trabalho e fica no historico (12 ultimas).
// ---------------------------------------------------------------------------
class SstvManager : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running };

    explicit SstvManager(QObject* parent = nullptr);
    ~SstvManager() override;

    // Frequencia do VFO, para o rotulo e o nome do arquivo
    std::function<uint64_t()> freqHz;

    bool start(const QJsonObject& opcoes);
    void stop();
    State state() const { return state_; }

    // comecar (modo), terminar, limpar, opcoes, abrirpasta
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
    SstvCore core_;
    bool salvar_ = true;

    std::mutex m_;                 // historico e cache do PNG atual
    std::deque<Feita> hist_;
    int proxId_ = 1;
    std::vector<uint32_t> buf_;
    uint64_t verCache_ = 0;
    int wCache_ = 0, hCache_ = 0;
    QByteArray pngAtual_;

    FilaAudio fila_;               // por ultimo: para a thread antes do resto sumir
};

} // namespace masdr
