#include "HttpServer.h"
#include "RestApi.h"
#include "../util/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace masdr {

// ─── MIME types ──────────────────────────────────────────────────────────────
static std::string mimeFor(const std::string& path) {
    auto ext = path.rfind('.');
    if (ext == std::string::npos) return "application/octet-stream";
    std::string e = path.substr(ext);
    for (char& c : e) c = (char)tolower((unsigned char)c);
    if (e==".html"||e==".htm") return "text/html; charset=utf-8";
    if (e==".js"||e==".mjs")   return "application/javascript; charset=utf-8";
    if (e==".css")              return "text/css; charset=utf-8";
    if (e==".json")             return "application/json";
    if (e==".png")              return "image/png";
    if (e==".jpg"||e==".jpeg") return "image/jpeg";
    if (e==".gif")              return "image/gif";
    if (e==".svg")              return "image/svg+xml";
    if (e==".ico")              return "image/x-icon";
    if (e==".woff")             return "font/woff";
    if (e==".woff2")            return "font/woff2";
    if (e==".ttf")              return "font/ttf";
    if (e==".txt")              return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static std::string statusText(int code) {
    switch(code) {
        case 200: return "OK";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

static void sendResponse(HttpSocket sock, const HttpResponse& res) {
    std::string hdr =
        "HTTP/1.1 " + std::to_string(res.status) + " " + statusText(res.status) + "\r\n"
        "Content-Type: " + res.contentType + "\r\n"
        "Content-Length: " + std::to_string(res.body.size()) + "\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    send((SOCKET)sock, hdr.c_str(), (int)hdr.size(), 0);
    if (!res.body.empty())
        send((SOCKET)sock, res.body.c_str(), (int)res.body.size(), 0);
}

// ─── HttpServer ──────────────────────────────────────────────────────────────
HttpServer::HttpServer() {
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir(exePath);
    auto pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    webRoot_ = dir + "\\web";
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::listen(uint16_t port) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) { closesocket(s); return false; }
    if (::listen(s, 32) != 0) { closesocket(s); return false; }

    struct sockaddr_in bound{};
    int blen = sizeof(bound);
    getsockname(s, (struct sockaddr*)&bound, &blen);
    port_       = ntohs(bound.sin_port);
    serverSock_ = (HttpSocket)s;
    running_    = true;

    acceptThread_ = std::thread([this]{ acceptLoop(); });
    Logger::info("HTTP escutando em http://localhost:" + std::to_string(port_));
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (serverSock_ != (HttpSocket)(~0ULL)) {
        closesocket((SOCKET)serverSock_);
        serverSock_ = (HttpSocket)(~0ULL);
    }
    if (acceptThread_.joinable()) acceptThread_.join();
}

void HttpServer::acceptLoop() {
    while (running_) {
        SOCKET c = accept((SOCKET)serverSock_, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        // Timeout de recepção de 5s
        DWORD to = 5000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        std::thread([this, c]{ handleClient((HttpSocket)c); }).detach();
    }
}

void HttpServer::handleClient(HttpSocket sock) {
    // Lê a requisição completa
    std::string raw;
    char buf[8192];
    while (raw.find("\r\n\r\n") == std::string::npos) {
        int n = recv((SOCKET)sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) { closesocket((SOCKET)sock); return; }
        buf[n] = '\0';
        raw += buf;
        if (raw.size() > 65536) break;
    }

    // Parse da linha de request
    HttpRequest req;
    std::istringstream ss(raw);
    ss >> req.method >> req.path;

    // Parse dos headers
    std::string line;
    std::getline(ss, line); // consome o resto da primeira linha
    while (std::getline(ss, line)) {
        if (line == "\r" || line.empty()) break;
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = trim(line.substr(0, colon));
            std::string val = trim(line.substr(colon + 1));
            for (char& c : key) c = (char)tolower((unsigned char)c);
            req.headers[key] = val;
        }
    }

    // Lê o body se houver Content-Length
    {
        auto it = req.headers.find("content-length");
        if (it != req.headers.end()) {
            size_t bodyLen = 0;
            try { bodyLen = (size_t)std::stoul(it->second); } catch(...) {}
            // Calcula quantos bytes do body já vieram no raw
            auto bodyStart = raw.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                bodyStart += 4;
                req.body = raw.substr(bodyStart);
            }
            while (req.body.size() < bodyLen) {
                int n = recv((SOCKET)sock, buf, (int)std::min(sizeof(buf)-1, bodyLen - req.body.size()), 0);
                if (n <= 0) break;
                buf[n] = '\0';
                req.body.append(buf, n);
            }
        }
    }

    HttpResponse res = dispatchRequest(req);
    sendResponse(sock, res);
    closesocket((SOCKET)sock);
}

HttpResponse HttpServer::dispatchRequest(const HttpRequest& req) {
    // Previne path traversal
    if (req.path.find("..") != std::string::npos) {
        return {404, "text/plain", "Not Found"};
    }

    // Rota para API REST
    if (req.path.size() >= 4 && req.path.substr(0, 4) == "/api") {
        if (api_) return api_->handle(req);
        return {500, "application/json", "{\"error\":\"No API\"}"};
    }

    // Arquivo estático
    std::string filePath = req.path;
    if (filePath == "/" || filePath.empty()) filePath = "/index.html";
    return serveFile(filePath);
}

HttpResponse HttpServer::serveFile(const std::string& urlPath) {
    std::string fullPath = webRoot_ + urlPath;
    // Normaliza separadores
    for (char& c : fullPath) if (c == '/') c = '\\';

    std::ifstream f(fullPath, std::ios::binary);
    if (!f.is_open()) return {404, "text/plain", "Not Found"};

    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return {200, mimeFor(urlPath), body};
}

} // namespace masdr
