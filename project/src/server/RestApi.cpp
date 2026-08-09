#include "RestApi.h"
#include "../sdr/ISdrDevice.h"

#include <QHttpServerResponse>
#include <QHttpServerRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUrlQuery>
#include <QDateTime>
#include <QRegularExpression>

namespace masdr {

RestApi::RestApi(QObject* parent) : QObject(parent) {}

void RestApi::install(QHttpServer* server)
{
    // GET /api/devices
    server->route("/api/devices", QHttpServerRequest::Method::Get, [this]() {
        QJsonArray arr = onListDevices ? onListDevices() : QJsonArray();
        return QHttpServerResponse("application/json", QJsonDocument(arr).toJson());
    });

    // POST /api/device/select
    server->route("/api/device/select", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r;
            if (onSelectDevice) {
                r = onSelectDevice(j.value("type").toString(), j.value("serial").toString());
            } else {
                r.insert("ok", false);
                r.insert("error", QStringLiteral("Servidor indisponível"));
            }
            if (!r.contains("ok")) r.insert("ok", false);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/tune
    server->route("/api/tune", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            bool ok = onTune ?
                onTune(j.value("vfo").toString("A"),
                       (quint64)j.value("freq").toDouble(),
                       j.value("mode").toString("USB"),
                       j.value("bw").toInt(3000))
                : false;
            QJsonObject r{ {"ok", ok} };
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/center
    server->route("/api/center", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            bool ok = onSetCenter ? onSetCenter((quint64)j.value("freq").toDouble()) : false;
            QJsonObject r{ {"ok", ok} };
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/gain
    server->route("/api/gain", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            bool ok = onSetGain ? onSetGain(j.value("value").toInt(280)) : false;
            QJsonObject r{ {"ok", ok} };
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/power
    server->route("/api/power", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            bool ok = onPower ? onPower(j.value("on").toBool(true)) : false;
            QJsonObject r{ {"ok", ok} };
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // GET /api/status
    server->route("/api/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onStatus ? onStatus() : QJsonObject();
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // GET /api/config
    server->route("/api/config", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onGetConfig ? onGetConfig() : QJsonObject();
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/config
    server->route("/api/config", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            bool ok = onSetConfig ? onSetConfig(j) : false;
            QJsonObject r{ {"ok", ok} };
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // ── AIS Decoder (AIS-catcher) ──────────────────────────────────────────────

    // GET /api/ais/status
    server->route("/api/ais/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onAisStatus ? onAisStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/ais/start
    server->route("/api/ais/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onAisStart ? onAisStart(j)
                                       : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/ais/stop
    server->route("/api/ais/stop", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest&) {
            QJsonObject r = onAisStop ? onAisStop()
                                      : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // ── ACARS Decoder (acarsdeco2) ─────────────────────────────────────────────

    // GET /api/acars/status
    server->route("/api/acars/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onAcarsStatus ? onAcarsStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/acars/start
    server->route("/api/acars/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onAcarsStart ? onAcarsStart(j)
                                         : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/acars/stop
    server->route("/api/acars/stop", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest&) {
            QJsonObject r = onAcarsStop ? onAcarsStop()
                                        : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // ── DSD Decoder (DSDPlus/dsd) ───────────────────────────────────────────────

    // GET /api/dsd/status
    server->route("/api/dsd/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onDsdStatus ? onDsdStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/dsd/start
    server->route("/api/dsd/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onDsdStart ? onDsdStart(j) : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/dsd/stop
    server->route("/api/dsd/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onDsdStop ? onDsdStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });

    // POST /api/dsd/polarity
    server->route("/api/dsd/polarity", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onDsdTogglePolarity ? onDsdTogglePolarity() : QJsonObject{{"ok",false}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });

    // POST /api/dsd/pcmhz  { "hz": 8000 }  — ajuste em tempo real da taxa UDP do DSD-FME (debug)
    server->route("/api/dsd/pcmhz", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            const auto j = QJsonDocument::fromJson(req.body()).object();
            const int hz = j.value("hz").toInt(8000);
            QJsonObject r = onDsdSetPcmHz ? onDsdSetPcmHz(hz)
                                          : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });




    // ── APRS Decoder (Direwolf) ────────────────────────────────────────────────

    // GET /api/aprs/status
    server->route("/api/aprs/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onAprsStatus ? onAprsStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/aprs/start
    server->route("/api/aprs/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onAprsStart ? onAprsStart(j) : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/aprs/stop
    server->route("/api/aprs/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onAprsStop ? onAprsStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });

    // POST /api/aprs/send  — envia mensagem pela internet (APRS-IS)
    server->route("/api/aprs/send", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onAprsSend ? onAprsSend(j)
                                       : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });


    // ── SITOR-B Decoder (Transmissões Marinhas) ───────────────────────────

    // GET /api/sitorb/status
    server->route("/api/sitorb/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onSitorBStatus ? onSitorBStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/sitorb/start
    server->route("/api/sitorb/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onSitorBStart ? onSitorBStart(j) : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/sitorb/stop
    server->route("/api/sitorb/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onSitorBStop ? onSitorBStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });


    // ── PACTOR Decoder (Pactor-I FSK) ─────────────────────────────────────

    // GET /api/pactor/status
    server->route("/api/pactor/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onPactorStatus ? onPactorStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/pactor/start
    server->route("/api/pactor/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onPactorStart ? onPactorStart(j) : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/pactor/stop
    server->route("/api/pactor/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onPactorStop ? onPactorStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });


    // ── DSC Decoder (ITU-R M.493) ─────────────────────────────────────────

    // GET /api/dsc/status
    server->route("/api/dsc/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onDscStatus ? onDscStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/dsc/start
    server->route("/api/dsc/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onDscStart ? onDscStart(j) : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // ── Analisador de sinal ───────────────────────────────────────────────
    server->route("/api/analise/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onAnaliseStatus ? onAnaliseStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse(o);
    });
    server->route("/api/analise/start", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onAnaliseStart ? onAnaliseStart() : QJsonObject{{"ok",false},{"error","indisponível"}};
        return QHttpServerResponse(r);
    });
    server->route("/api/analise/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onAnaliseStop ? onAnaliseStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        return QHttpServerResponse(r);
    });

    // POST /api/dsc/stop
    server->route("/api/dsc/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onDscStop ? onDscStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });






    // ── SELCAL Decoder ───────────────────────────────────────────

    // GET /api/selcal/status
    server->route("/api/selcal/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onSelcalStatus ? onSelcalStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/selcal/start
    server->route("/api/selcal/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onSelcalStart ? onSelcalStart(j) : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/selcal/stop
    server->route("/api/selcal/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onSelcalStop ? onSelcalStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });

    // ── TETRA Decoder ───────────────────────────────────────────────────────────

    // GET /api/tetra/status
    server->route("/api/tetra/status", QHttpServerRequest::Method::Get, [this]() {
        QJsonObject o = onTetraStatus ? onTetraStatus() : QJsonObject{{"state","unavailable"}};
        return QHttpServerResponse("application/json", QJsonDocument(o).toJson());
    });

    // POST /api/tetra/start
    server->route("/api/tetra/start", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            auto j = QJsonDocument::fromJson(req.body()).object();
            QJsonObject r = onTetraStart ? onTetraStart(j) : QJsonObject{{"ok",false},{"error","indisponível"}};
            if (!r.contains("ok")) r.insert("ok", true);
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

    // POST /api/tetra/stop
    server->route("/api/tetra/stop", QHttpServerRequest::Method::Post, [this]() {
        QJsonObject r = onTetraStop ? onTetraStop() : QJsonObject{{"ok",false},{"error","indisponível"}};
        if (!r.contains("ok")) r.insert("ok", true);
        return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
    });


    // Gravacao MP3: recebe o corpo e grava na Area de Trabalho (Win 7/10/11)
    server->route("/api/save_recording", QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            QUrlQuery q(req.url().query());
            QString name = q.queryItemValue("name");
            if (name.isEmpty())
                name = "RXSDR_gravacao_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".mp3";
            name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

            QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
            if (desktop.isEmpty())
                desktop = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
            QDir().mkpath(desktop);
            QString fullPath = QDir(desktop).filePath(name);

            const QByteArray data = req.body();
            QFile out(fullPath);
            bool ok = out.open(QIODevice::WriteOnly);
            if (ok) { ok = (out.write(data) == data.size()); out.close(); }

            QJsonObject r{ {"ok", ok}, {"path", QDir::toNativeSeparators(fullPath)}, {"bytes", (double)data.size()} };
            return QHttpServerResponse("application/json", QJsonDocument(r).toJson());
        });

}

} // namespace masdr
