#include "HttpServer.h"
#include "RestApi.h"
#include "../util/Logger.h"

#include <QHttpServerResponse>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QMimeDatabase>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6,7,0)
#include <QHttpHeaders>
#endif

namespace {
// Compatibilidade de versao do Qt6:
//  - Qt < 6.7 (ex.: Debian Bookworm): QHttpServerResponse::setHeader(name,value)
//  - Qt >= 6.7 (ex.: Debian Trixie): setHeader removido -> QHttpHeaders + setHeaders()
inline void rxsdrAddHeader(QHttpServerResponse& resp, const char* name, const char* value) {
#if QT_VERSION >= QT_VERSION_CHECK(6,7,0)
    QHttpHeaders h = resp.headers();
    h.append(QByteArray(name), QByteArray(value));
    resp.setHeaders(std::move(h));
#else
    resp.setHeader(name, value);
#endif
}
} // namespace

namespace masdr {

HttpServer::HttpServer(QObject* parent) : QObject(parent) {
    server_ = std::make_unique<QHttpServer>();
}
HttpServer::~HttpServer() { stop(); }

bool HttpServer::listen(quint16 port) {
    if (!server_) {
        server_ = std::make_unique<QHttpServer>();
    }
    tcp_ = std::make_unique<QTcpServer>();
    if (!tcp_->listen(QHostAddress::Any, port)) {
        Logger::warn(QString("HTTP: porta %1 indisponível").arg(port));
        return false;
    }
    port_ = tcp_->serverPort();
    registerRoutes();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (!server_->bind(tcp_.get())) {
        Logger::error("Falha ao bindar QHttpServer no QTcpServer");
        return false;
    }
#else
    server_->bind(tcp_.get());
#endif
    Logger::info(QString("HTTP escutando em http://localhost:%1").arg(port_));
    return true;
}

void HttpServer::stop() {
    if (tcp_) { tcp_->close(); tcp_.reset(); }
    server_.reset();
}

void HttpServer::registerRoutes()
{
    // API REST primeiro para evitar colisão com rotas estáticas genéricas.
    if (api_) api_->install(server_.get());

    // Rota raiz → index.html
    server_->route("/", [this]() {
        QByteArray mime;
        auto body = readWebFile("/index.html", mime);
        QHttpServerResponse resp(mime, body);
        rxsdrAddHeader(resp, "Cache-Control", "no-cache, no-store, must-revalidate");
        rxsdrAddHeader(resp, "Pragma", "no-cache");
        rxsdrAddHeader(resp, "Expires", "0");
        return resp;
    });

    // Arquivos estáticos: /css, /js, /assets, /favicon.ico, etc.
    server_->route("/<arg>", [this](const QString& path) {
        QByteArray mime;
        auto body = readWebFile("/" + path, mime);
        if (body.isEmpty())
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
        return QHttpServerResponse(mime, body);
    });

    server_->route("/<arg>/<arg>", [this](const QString& a, const QString& b) {
        QByteArray mime;
        auto body = readWebFile("/" + a + "/" + b, mime);
        if (body.isEmpty())
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
        return QHttpServerResponse(mime, body);
    });

    server_->route("/<arg>/<arg>/<arg>", [this](const QString& a, const QString& b, const QString& c){
        QByteArray mime;
        auto body = readWebFile("/" + a + "/" + b + "/" + c, mime);
        if (body.isEmpty())
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
        return QHttpServerResponse(mime, body);
    });
    server_->route("/<arg>/<arg>/<arg>/<arg>",
                   [this](const QString& a, const QString& b, const QString& c, const QString& d){
        QByteArray mime;
        auto body = readWebFile("/" + a + "/" + b + "/" + c + "/" + d, mime);
        if (body.isEmpty())
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
        return QHttpServerResponse(mime, body);
    });
}

QByteArray HttpServer::readWebFile(const QString& path, QByteArray& mime) {
    QString base = QCoreApplication::applicationDirPath() + "/web";
    QFileInfo fi(base + path);
    if (!fi.exists() || !fi.isFile()) return {};
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    QMimeDatabase db;
    mime = db.mimeTypeForFile(fi).name().toUtf8();
    if (mime.startsWith("text/")) {
        mime += "; charset=utf-8";
    }
    return f.readAll();
}

} // namespace masdr
