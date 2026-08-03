#include "RestApi.h"
#include "../../third_party/masdr_json.h"
#include <algorithm>
#include <cctype>

namespace masdr {

static std::string methodOf(const HttpRequest& req) {
    std::string m = req.method;
    for (char& c : m) c = (char)toupper((unsigned char)c);
    return m;
}

HttpResponse RestApi::handle(const HttpRequest& req) {
    const std::string& path = req.path;
    const std::string  meth = methodOf(req);
    HttpResponse res;
    res.contentType = "application/json";

    // ── GET /api/devices ─────────────────────────────────────────────────────
    if (path == "/api/devices" && meth == "GET") {
        Json arr = onListDevices ? onListDevices() : Json::array();
        res.body = arr.serialize();
        return res;
    }

    // ── POST /api/device/select ───────────────────────────────────────────────
    if (path == "/api/device/select" && meth == "POST") {
        Json j = Json::parse(req.body);
        Json r = Json::object();
        if (onSelectDevice) {
            r = onSelectDevice(j["type"].getString(), j["serial"].getString());
        } else {
            r["ok"]    = Json(false);
            r["error"] = Json("Servidor indisponivel");
        }
        if (!r.contains("ok")) r["ok"] = Json(false);
        res.body = r.serialize();
        return res;
    }

    // ── POST /api/tune ────────────────────────────────────────────────────────
    if (path == "/api/tune" && meth == "POST") {
        Json j = Json::parse(req.body);
        uint64_t freq = (uint64_t)j["freq"].getDouble(0.0);
        bool ok = onTune ?
            onTune(j["vfo"].getString("A"), freq,
                   j["mode"].getString("USB"),
                   (int)j["bw"].getInt(3000))
            : false;
        Json r = Json::object(); r["ok"] = Json(ok);
        res.body = r.serialize();
        return res;
    }

    // ── POST /api/center ──────────────────────────────────────────────────────
    if (path == "/api/center" && meth == "POST") {
        Json j = Json::parse(req.body);
        uint64_t freq = (uint64_t)j["freq"].getDouble(0.0);
        bool ok = onSetCenter ? onSetCenter(freq) : false;
        Json r = Json::object(); r["ok"] = Json(ok);
        res.body = r.serialize();
        return res;
    }

    // ── POST /api/gain ────────────────────────────────────────────────────────
    if (path == "/api/gain" && meth == "POST") {
        Json j = Json::parse(req.body);
        bool ok = onSetGain ? onSetGain((int)j["value"].getInt(280)) : false;
        Json r = Json::object(); r["ok"] = Json(ok);
        res.body = r.serialize();
        return res;
    }

    // ── POST /api/power ───────────────────────────────────────────────────────
    if (path == "/api/power" && meth == "POST") {
        Json j = Json::parse(req.body);
        bool ok = onPower ? onPower(j["on"].getBool(true)) : false;
        Json r = Json::object(); r["ok"] = Json(ok);
        res.body = r.serialize();
        return res;
    }

    // ── GET /api/status ───────────────────────────────────────────────────────
    if (path == "/api/status" && meth == "GET") {
        Json o = onStatus ? onStatus() : Json::object();
        res.body = o.serialize();
        return res;
    }

    // ── GET /api/config ───────────────────────────────────────────────────────
    if (path == "/api/config" && meth == "GET") {
        Json o = onGetConfig ? onGetConfig() : Json::object();
        res.body = o.serialize();
        return res;
    }

    // ── POST /api/config ──────────────────────────────────────────────────────
    if (path == "/api/config" && meth == "POST") {
        Json j = Json::parse(req.body);
        bool ok = onSetConfig ? onSetConfig(j) : false;
        Json r = Json::object(); r["ok"] = Json(ok);
        res.body = r.serialize();
        return res;
    }

    // ── OPTIONS (CORS preflight) ──────────────────────────────────────────────
    if (meth == "OPTIONS") {
        res.status = 200; res.body = "{}";
        return res;
    }

    res.status = 404;
    res.body   = "{\"error\":\"Not found\"}";
    return res;
}

} // namespace masdr
