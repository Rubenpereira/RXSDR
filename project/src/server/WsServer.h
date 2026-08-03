#pragma once
#include <QObject>
#include <QWebSocketServer>
#include <QList>
#include <QJsonObject>
#include <vector>
#include <memory>

class QWebSocket;

namespace masdr {

// WebSocket server: distribui frames de FFT, áudio PCM e mensagens
// JSON dos decoders para todos os clientes conectados.
class WsServer : public QObject {
    Q_OBJECT
public:
    explicit WsServer(QObject* parent=nullptr);
    ~WsServer() override;

    bool listen(quint16 port);
    void stop();
    quint16 port() const { return port_; }

    // Envia frame binário FFT (int8 dBfs)
    void broadcastFft(const std::vector<int8_t>& bins,
                      uint64_t centerHz, uint32_t sps);
    // Envia frame binário áudio (int16 PCM mono)
    void broadcastAudio(const std::vector<int16_t>& pcm, uint32_t sps);
    // Envia mensagem JSON (decoders, status)
    void broadcastJson(const QJsonObject& obj);
    int clientCount() const { return clients_.size(); }

    // Versões thread-safe: podem ser chamadas de qualquer thread.
    // Enfileiram a mensagem na event loop da thread principal.
    void broadcastFftThreadSafe(const std::vector<int8_t>& bins,
                                uint64_t centerHz, uint32_t sps);
    void broadcastAudioThreadSafe(const std::vector<int16_t>& pcm, uint32_t sps);
    void broadcastJsonThreadSafe(const QJsonObject& obj);

signals:
    void clientsChanged(int count);
    // Sinais usados internamente para cruzar a fronteira de thread (QueuedConnection)
    void _fftReady(QByteArray data);
    void _audioReady(QByteArray data);
    void _jsonReady(QByteArray data);

private slots:
    void onNewConnection();
    void onDisconnected();

private:
    std::unique_ptr<QWebSocketServer> server_;
    QList<QWebSocket*> clients_;
    quint16 port_ = 0;
};

} // namespace masdr
