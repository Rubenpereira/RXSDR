#include "WsServer.h"
#include "../util/Logger.h"

#include <QWebSocket>
#include <QJsonDocument>
#include <cstring>

namespace masdr {

WsServer::WsServer(QObject* parent) : QObject(parent) {
    server_ = std::make_unique<QWebSocketServer>(
        QStringLiteral("RXSDR"), QWebSocketServer::NonSecureMode);
    connect(server_.get(), &QWebSocketServer::newConnection,
            this, &WsServer::onNewConnection);

    // Conecta os sinais internos com QueuedConnection para garantir
    // que o envio ocorra sempre na thread do event loop (thread principal),
    // mesmo quando o sinal é emitido de uma thread de background (RTL-SDR async).
    connect(this, &WsServer::_fftReady, this, [this](QByteArray data){
        for (auto* c : clients_) c->sendBinaryMessage(data);
    }, Qt::QueuedConnection);

    connect(this, &WsServer::_audioReady, this, [this](QByteArray data){
        for (auto* c : clients_) c->sendBinaryMessage(data);
    }, Qt::QueuedConnection);

    connect(this, &WsServer::_jsonReady, this, [this](QByteArray data){
        for (auto* c : clients_) c->sendTextMessage(QString::fromUtf8(data));
    }, Qt::QueuedConnection);
}


WsServer::~WsServer() { stop(); }

bool WsServer::listen(quint16 port) {
    if (!server_->listen(QHostAddress::Any, port)) {
        Logger::error(QString("WS: falha ao escutar porta %1").arg(port));
        return false;
    }
    port_ = server_->serverPort();
    Logger::info(QString("WS escutando em ws://localhost:%1").arg(port_));
    return true;
}

void WsServer::stop() {
    if (!server_) return;
    for (auto* c : clients_) c->close();
    server_->close();
}

void WsServer::onNewConnection() {
    auto* s = server_->nextPendingConnection();
    clients_.append(s);
    connect(s, &QWebSocket::disconnected, this, &WsServer::onDisconnected);
    emit clientsChanged(clients_.size());
    Logger::info(QString("WS cliente conectado (%1 total)").arg(clients_.size()));
}

void WsServer::onDisconnected() {
    auto* s = qobject_cast<QWebSocket*>(sender());
    if (!s) return;
    clients_.removeAll(s);
    s->deleteLater();
    emit clientsChanged(clients_.size());
    Logger::info(QString("WS cliente saiu (%1 restantes)").arg(clients_.size()));
}

void WsServer::broadcastFft(const std::vector<int8_t>& bins,
                            uint64_t centerHz, uint32_t sps)
{
    if (clients_.isEmpty()) return;
    // Header binário compacto:
    //   byte 0:  0x01 (tipo FFT)
    //   byte 1-8:  centerHz (uint64 LE)
    //   byte 9-12: sps     (uint32 LE)
    //   byte 13-14: nBins  (uint16 LE)
    //   bytes 15..: int8 dBfs
    QByteArray buf;
    buf.resize(15 + bins.size());
    char* p = buf.data();
    p[0] = 0x01;
    std::memcpy(p+1,  &centerHz, 8);
    std::memcpy(p+9,  &sps,      4);
    uint16_t n = (uint16_t)bins.size();
    std::memcpy(p+13, &n, 2);
    std::memcpy(p+15, bins.data(), bins.size());
    for (auto* c : clients_) c->sendBinaryMessage(buf);
}

void WsServer::broadcastAudio(const std::vector<int16_t>& pcm, uint32_t sps)
{
    if (clients_.isEmpty()) return;
    QByteArray buf;
    buf.resize(7 + pcm.size()*2);
    char* p = buf.data();
    p[0] = 0x02;
    std::memcpy(p+1, &sps, 4);
    uint16_t n = (uint16_t)pcm.size();
    std::memcpy(p+5, &n, 2);
    std::memcpy(p+7, pcm.data(), pcm.size()*2);
    for (auto* c : clients_) c->sendBinaryMessage(buf);
}

void WsServer::broadcastJson(const QJsonObject& obj) {
    if (clients_.isEmpty()) return;
    QByteArray j = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (auto* c : clients_) c->sendTextMessage(QString::fromUtf8(j));
}

// ── Versões Thread-Safe ──────────────────────────────────────────────────────
// Podem ser chamadas de qualquer thread (ex: thread do rtlsdr_read_async).
// Montam o buffer e emitem um sinal com Qt::AutoConnection — se chamado de
// outra thread, a entrega é automaticamente enfileirada (QueuedConnection),
// garantindo que sendBinaryMessage() rode na thread principal onde os
// objetos QWebSocket foram criados.

void WsServer::broadcastFftThreadSafe(const std::vector<int8_t>& bins,
                                      uint64_t centerHz, uint32_t sps)
{
    if (clients_.isEmpty()) return;
    QByteArray buf;
    buf.resize(15 + (int)bins.size());
    char* p = buf.data();
    p[0] = 0x01;
    std::memcpy(p+1,  &centerHz, 8);
    std::memcpy(p+9,  &sps,      4);
    uint16_t n = (uint16_t)bins.size();
    std::memcpy(p+13, &n, 2);
    std::memcpy(p+15, bins.data(), bins.size());
    emit _fftReady(buf);  // AutoConnection → QueuedConnection quando cross-thread
}

void WsServer::broadcastAudioThreadSafe(const std::vector<int16_t>& pcm, uint32_t sps)
{
    if (clients_.isEmpty()) return;
    QByteArray buf;
    buf.resize(7 + (int)pcm.size()*2);
    char* p = buf.data();
    p[0] = 0x02;
    std::memcpy(p+1, &sps, 4);
    uint16_t n = (uint16_t)pcm.size();
    std::memcpy(p+5, &n, 2);
    std::memcpy(p+7, pcm.data(), pcm.size()*2);
    emit _audioReady(buf);  // AutoConnection → QueuedConnection quando cross-thread
}

void WsServer::broadcastJsonThreadSafe(const QJsonObject& obj)
{
    if (clients_.isEmpty()) return;
    QByteArray j = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    emit _jsonReady(j);
}

} // namespace masdr
