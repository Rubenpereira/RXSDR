#include "WsServer.h"
#include "../util/Logger.h"
#include "../../third_party/sha1.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace masdr {

WsServer::WsServer()  = default;
WsServer::~WsServer() { stop(); }

bool WsServer::listen(uint16_t port) {
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
    if (::listen(s, 16) != 0) { closesocket(s); return false; }

    // Descobre a porta real (se port==0)
    struct sockaddr_in bound{};
    int blen = sizeof(bound);
    getsockname(s, (struct sockaddr*)&bound, &blen);
    port_ = ntohs(bound.sin_port);

    serverSock_ = (WsSocket)s;
    running_    = true;
    acceptThread_ = std::thread([this]{ acceptLoop(); });

    Logger::info("WS escutando na porta " + std::to_string(port_));
    return true;
}

void WsServer::stop() {
    running_ = false;
    if (serverSock_ != (WsSocket)(~0ULL)) {
        closesocket((SOCKET)serverSock_);
        serverSock_ = (WsSocket)(~0ULL);
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    std::lock_guard<std::mutex> lk(clientsMutex_);
    for (auto c : clients_) closesocket((SOCKET)c);
    clients_.clear();
}

int WsServer::clientCount() const {
    std::lock_guard<std::mutex> lk(clientsMutex_);
    return (int)clients_.size();
}

void WsServer::acceptLoop() {
    while (running_) {
        struct sockaddr_in cli{};
        int len = sizeof(cli);
        SOCKET c = accept((SOCKET)serverSock_, (struct sockaddr*)&cli, &len);
        if (c == INVALID_SOCKET) break;
        std::thread([this, c]{ clientLoop((WsSocket)c); }).detach();
    }
}

void WsServer::clientLoop(WsSocket sock) {
    std::string path;
    if (!doHandshake(sock, path)) { closesocket((SOCKET)sock); return; }

    {
        std::lock_guard<std::mutex> lk(clientsMutex_);
        clients_.push_back(sock);
        int cnt = (int)clients_.size();
        if (onClientsChanged) onClientsChanged(cnt);
    }
    Logger::info("WS: cliente conectado (total=" + std::to_string(clientCount()) + ")");

    // Loop de leitura - apenas detecta desconexao
    char buf[256];
    while (running_) {
        int n = recv((SOCKET)sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        // opcode 0x88 = close frame
        if (n >= 2 && (buf[0] & 0x0F) == 0x08) break;
    }

    removeClient(sock);
    closesocket((SOCKET)sock);
    Logger::info("WS: cliente desconectado (total=" + std::to_string(clientCount()) + ")");
}

bool WsServer::doHandshake(WsSocket sock, std::string& out_path) {
    // Le o request HTTP de upgrade
    std::string request;
    char buf[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        int n = recv((SOCKET)sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) return false;
        buf[n] = '\0';
        request += buf;
        if (request.size() > 8192) return false;
    }

    // Extrai Sec-WebSocket-Key
    std::string key;
    {
        const std::string header = "Sec-WebSocket-Key:";
        auto pos = request.find(header);
        if (pos == std::string::npos) return false;
        pos += header.size();
        while (pos < request.size() && request[pos] == ' ') ++pos;
        auto end = request.find("\r\n", pos);
        if (end == std::string::npos) return false;
        key = request.substr(pos, end - pos);
    }

    // Extrai path (primeira linha)
    {
        auto sp1 = request.find(' ');
        auto sp2 = request.find(' ', sp1 + 1);
        if (sp1 != std::string::npos && sp2 != std::string::npos)
            out_path = request.substr(sp1 + 1, sp2 - sp1 - 1);
    }

    std::string accept = masdr::sha1Base64(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

    int sent = send((SOCKET)sock, resp.c_str(), (int)resp.size(), 0);
    return sent == (int)resp.size();
}

void WsServer::removeClient(WsSocket sock) {
    std::lock_guard<std::mutex> lk(clientsMutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), sock), clients_.end());
    if (onClientsChanged) onClientsChanged((int)clients_.size());
}

bool WsServer::sendFrame(WsSocket sock, uint8_t opcode, const uint8_t* payload, size_t payloadLen) {
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode); // FIN + opcode

    if (payloadLen <= 125) {
        frame.push_back((uint8_t)payloadLen);
    } else if (payloadLen <= 65535) {
        frame.push_back(126);
        frame.push_back((uint8_t)(payloadLen >> 8));
        frame.push_back((uint8_t)(payloadLen & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((uint8_t)((payloadLen >> (i * 8)) & 0xFF));
    }
    frame.insert(frame.end(), payload, payload + payloadLen);

    int sent = send((SOCKET)sock, (const char*)frame.data(), (int)frame.size(), 0);
    return sent == (int)frame.size();
}

void WsServer::broadcastBinary(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lk(clientsMutex_);
    std::vector<WsSocket> dead;
    for (auto c : clients_)
        if (!sendFrame(c, 0x02, data, len)) dead.push_back(c);
    for (auto d : dead) {
        closesocket((SOCKET)d);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), d), clients_.end());
    }
    if (!dead.empty() && onClientsChanged) onClientsChanged((int)clients_.size());
}

// Envia como frame de TEXTO WebSocket (opcode 0x01) para que
// o JavaScript receba ev.data como string, nao como ArrayBuffer
void WsServer::broadcastText(const std::string& msg) {
    std::lock_guard<std::mutex> lk(clientsMutex_);
    std::vector<WsSocket> dead;
    for (auto c : clients_)
        if (!sendFrame(c, 0x01, (const uint8_t*)msg.data(), msg.size()))
            dead.push_back(c);
    for (auto d : dead) {
        closesocket((SOCKET)d);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), d), clients_.end());
    }
    if (!dead.empty() && onClientsChanged) onClientsChanged((int)clients_.size());
}

// ---------------------------------------------------------------------------
// Formato binario FFT -- identico ao Qt6 WsServer:
//   byte  0    : 0x01 (tipo FFT)
//   bytes 1-8  : centerHz  uint64 little-endian
//   bytes 9-12 : sps       uint32 little-endian
//   bytes 13-14: nBins     uint16 little-endian  <-- campo de contagem!
//   bytes 15+  : int8 dBfs (um por bin)
// ---------------------------------------------------------------------------
void WsServer::broadcastFftThreadSafe(const std::vector<int8_t>& bins, uint64_t centerHz, uint32_t sps) {
    const uint16_t n = (uint16_t)bins.size();
    std::vector<uint8_t> payload(15 + bins.size());
    payload[0] = 0x01;
    std::memcpy(&payload[1],  &centerHz, 8); // x86 = little-endian
    std::memcpy(&payload[9],  &sps,      4); // x86 = little-endian
    std::memcpy(&payload[13], &n,        2); // x86 = little-endian
    std::memcpy(&payload[15], bins.data(), bins.size());
    broadcastBinary(payload.data(), payload.size());
}

// ---------------------------------------------------------------------------
// Formato binario audio -- identico ao Qt6 WsServer:
//   byte  0    : 0x02 (tipo audio)
//   bytes 1-4  : sps       uint32 little-endian
//   bytes 5-6  : n         uint16 little-endian  <-- campo de contagem!
//   bytes 7+   : int16 PCM little-endian (nativo x86)
// ---------------------------------------------------------------------------
void WsServer::broadcastAudioThreadSafe(const std::vector<int16_t>& pcm, uint32_t sps) {
    const uint16_t n = (uint16_t)pcm.size();
    std::vector<uint8_t> payload(7 + pcm.size() * 2);
    payload[0] = 0x02;
    std::memcpy(&payload[1], &sps, 4); // x86 = little-endian
    std::memcpy(&payload[5], &n,   2); // x86 = little-endian
    std::memcpy(&payload[7], pcm.data(), pcm.size() * 2); // int16 nativo x86
    broadcastBinary(payload.data(), payload.size());
}

// JSON enviado como frame de TEXTO WebSocket (opcode 0x01)
void WsServer::broadcastJsonThreadSafe(const std::string& json) {
    std::lock_guard<std::mutex> lk(clientsMutex_);
    std::vector<WsSocket> dead;
    for (auto c : clients_)
        if (!sendFrame(c, 0x01, (const uint8_t*)json.data(), json.size()))
            dead.push_back(c);
    for (auto d : dead) {
        closesocket((SOCKET)d);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), d), clients_.end());
    }
    if (!dead.empty() && onClientsChanged) onClientsChanged((int)clients_.size());
}

} // namespace masdr
