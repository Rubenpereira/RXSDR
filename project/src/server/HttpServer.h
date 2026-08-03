#pragma once
#include <QObject>
#include <QHttpServer>
#include <QTcpServer>
#include <memory>

namespace masdr {

class RestApi;

// Servidor HTTP do RXSDR:
//  - Serve arquivos estáticos da pasta ./web (UI)
//  - Roteia /api/* para RestApi
class HttpServer : public QObject {
    Q_OBJECT
public:
    explicit HttpServer(QObject* parent=nullptr);
    ~HttpServer() override;

    bool listen(quint16 port);
    void stop();
    quint16 port() const { return port_; }
    void setRestApi(RestApi* api) { api_ = api; }

private:
    std::unique_ptr<QHttpServer> server_;
    std::unique_ptr<QTcpServer>  tcp_;
    RestApi* api_ = nullptr;
    quint16 port_ = 0;

    void registerRoutes();
    QByteArray readWebFile(const QString& path, QByteArray& mime);
};

} // namespace masdr
