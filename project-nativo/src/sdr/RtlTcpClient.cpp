#include "RtlTcpClient.h"
#include "../util/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <complex>
#include <vector>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace masdr {

static bool parseEndpoint(const std::string& ep, std::string& host, uint16_t& port) {
    if (ep.empty()) return false;
    auto colon = ep.rfind(':');
    if (colon != std::string::npos && colon > 0) {
        host = ep.substr(0, colon);
        try { int p = std::stoi(ep.substr(colon+1));
              if (p > 0 && p <= 65535) { port = (uint16_t)p; return true; } }
        catch(...) {}
    }
    host = ep;
    return true;
}

// Conecta com prazo (o connect() do Windows sozinho espera ~21 s quando o
// endereco nao responde). Aceita IP ou nome (ex.: pu1xtb.ddns.net).
static SOCKET conectarComPrazo(const std::string& host, uint16_t port, int timeoutMs, std::string& erro)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    addrinfo dica{}; dica.ai_family = AF_INET; dica.ai_socktype = SOCK_STREAM; dica.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &dica, &res) != 0 || !res) {
        erro = "endereço \"" + host + "\" não encontrado";
        return INVALID_SOCKET;
    }
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); erro = "falha ao criar o socket"; return INVALID_SOCKET; }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    int r = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s); erro = "conexão recusada"; return INVALID_SOCKET;
    }
    if (r != 0) {
        fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        r = select(0, nullptr, &w, &e, &tv);
        if (r == 0) { closesocket(s); erro = "sem resposta (caixa desligada ou IP errado)"; return INVALID_SOCKET; }
        if (r < 0 || FD_ISSET(s, &e)) {
            closesocket(s);
            erro = "conexão recusada (o rtl_tcp não está ativo nessa porta)";
            return INVALID_SOCKET;
        }
    }
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
    BOOL um = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&um, sizeof um);   // comandos saem na hora
    int buf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof buf);
    return s;
}

bool RtlTcpClient::testar(const std::string& endpoint, int timeoutMs, std::string& erro, std::string& info)
{
    erro.clear(); info.clear();
    std::string host = "127.0.0.1"; uint16_t port = 1234;
    if (!parseEndpoint(endpoint, host, port)) { erro = "endereço inválido"; return false; }
    SOCKET s = conectarComPrazo(host, port, timeoutMs, erro);
    if (s == INVALID_SOCKET) return false;
    // O rtl_tcp manda 12 bytes logo ao aceitar: "RTL0" + tipo do sintonizador + n. de ganhos.
    // Se nao vier nada, a porta aceitou mas o rtl_tcp esta ocupado com outro cliente.
    DWORD to = 2500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
    uint8_t h[12]; int lidos = 0;
    while (lidos < 12) {
        const int n = recv(s, (char*)h + lidos, 12 - lidos, 0);
        if (n <= 0) break;
        lidos += n;
    }
    closesocket(s);
    if (lidos < 12) { erro = "conectou, mas o rtl_tcp não respondeu (ocupado com outro cliente, ex.: o celular?)"; return false; }
    if (std::memcmp(h, "RTL0", 4) != 0) { erro = "nessa porta não há um rtl_tcp"; return false; }
    const uint32_t tuner = (uint32_t(h[4]) << 24) | (uint32_t(h[5]) << 16) | (uint32_t(h[6]) << 8) | h[7];
    static const char* nomes[] = {"?", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D"};
    info = std::string("sintonizador ") + (tuner < 7 ? nomes[tuner] : "?");
    return true;
}

RtlTcpClient::RtlTcpClient()  = default;
RtlTcpClient::~RtlTcpClient() { close(); }

bool RtlTcpClient::open(const std::string& serial) {
    lastError_.clear();
    close();

    if (!serial.empty()) endpoint_ = serial;

    std::string host = "127.0.0.1";
    uint16_t port = 1234;
    if (!parseEndpoint(endpoint_, host, port)) {
        lastError_ = "Endpoint RTL-TCP invalido: " + endpoint_;
        Logger::error(lastError_);
        return false;
    }

    Logger::info("RTL-TCP: conectando a " + host + ":" + std::to_string(port));
    std::string motivo;
    SOCKET s = conectarComPrazo(host, port, 3000, motivo);
    if (s == INVALID_SOCKET) {
        lastError_ = "RTL-TCP " + host + ":" + std::to_string(port) + ": " + motivo;
        Logger::error(lastError_);
        return false;
    }

    sock_      = (uintptr_t)s;
    gotHeader_ = false;
    endpoint_  = host + ":" + std::to_string(port);

    // Remove timeout para o loop de recepção
    DWORD noTimeout = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&noTimeout, sizeof(noTimeout));

    Logger::info("RTL-TCP: conectado em " + endpoint_);

    setSampleRate(sps_);
    setPpm(ppm_);
    setQuadrature(quadrature_);
    setCenterFreq(freq_);
    setGain(gainTenths_);
    return true;
}

void RtlTcpClient::close() {
    stop();
    if (sock_ != (uintptr_t)(~0ULL)) {
        closesocket((SOCKET)sock_);
        sock_ = (uintptr_t)(~0ULL);
    }
    gotHeader_ = false;
}

void RtlTcpClient::start() {
    if (running_ || sock_ == (uintptr_t)(~0ULL)) return;
    running_ = true;
    recvThread_ = std::thread([this]{ recvLoop(); });
}

void RtlTcpClient::stop() {
    running_ = false;
    if (recvThread_.joinable()) recvThread_.join();
}

void RtlTcpClient::setCenterFreq(uint64_t hz) {
    freq_ = hz;
    sendCommand(0x01, (uint32_t)hz);
}
void RtlTcpClient::setSampleRate(uint32_t sps) {
    sps_ = sps;
    sendCommand(0x02, sps);
}
void RtlTcpClient::setGain(int tenthsDb) {
    gainTenths_ = tenthsDb;
    if (sock_ == (uintptr_t)(~0ULL)) return;

    if (quadrature_) {
        // Q-on (direct sampling): tuner bypassado. NÃO mexer no tuner_gain_mode —
        // só ativar AGC interno do RTL2832U para preservar o ganho em HF.
        sendCommand(0x08, 1); // RTL AGC = on
        return;
    }

    if (tenthsDb < 0) {
        sendCommand(0x03, 0); // Tuner AGC = on
        sendCommand(0x08, 1); // RTL AGC = on (modo auto de ganho do tuner requer RTL AGC)
    } else {
        sendCommand(0x03, 1); // Tuner AGC = off (ganho manual)
        sendCommand(0x04, (uint32_t)tenthsDb); // Ganho manual do tuner
        sendCommand(0x08, 0); // RTL AGC = off
    }
}
void RtlTcpClient::setQuadrature(bool on) {
    quadrature_ = on;
    sendCommand(0x09, on ? 2u : 0u);

    // Alinhado ao comportamento do RtlSdrDevice (dongle USB):
    if (on) {
        // Se ativando Q-on (Direct Sampling)
        sendCommand(0x08, 1); // RTL AGC = on
    } else {
        // Se desativando Q-on (voltando para Q-off)
        // Restaura o modo de ganho do tuner ao voltar para Q-off
        sendCommand(0x03, gainTenths_ < 0 ? 0 : 1);
        if (gainTenths_ >= 0) {
            sendCommand(0x04, (uint32_t)gainTenths_);
        }
        sendCommand(0x08, gainTenths_ < 0 ? 1 : 0);
    }
}
void RtlTcpClient::setPpm(int ppm) {
    ppm_ = ppm;
    sendCommand(0x05, (uint32_t)ppm);
}

void RtlTcpClient::recvLoop() {
    std::vector<uint8_t> rxBuf;
    rxBuf.reserve(16384 * 4);
    static thread_local std::vector<std::complex<float>> iq;

    while (running_ && sock_ != (uintptr_t)(~0ULL)) {
        uint8_t tmp[65536];
        int n = recv((SOCKET)sock_, (char*)tmp, sizeof(tmp), 0);
        if (n <= 0) break;

        rxBuf.insert(rxBuf.end(), tmp, tmp + n);

        // Pula header de 12 bytes do servidor rtl_tcp
        if (!gotHeader_) {
            if (rxBuf.size() < 12) continue;
            rxBuf.erase(rxBuf.begin(), rxBuf.begin() + 12);
            gotHeader_ = true;
        }

        // Blocos de 16384 amostras; o que sobra fica para a proxima volta
        // (um erase so por recv, nao um por bloco).
        const size_t take = 16384 * 2;
        size_t usado = 0;
        while (rxBuf.size() - usado >= take) {
            const size_t samples = take / 2;
            if (iq.size() < samples) iq.resize(samples);
            const uint8_t* b = rxBuf.data() + usado;
            for (size_t i = 0; i < samples; ++i) {
                float I = ((float)b[2*i]   - 127.5f) / 127.5f;
                float Q = ((float)b[2*i+1] - 127.5f) / 127.5f;
                iq[i] = { I, Q };
            }
            usado += take;
            if (running_ && cb_) cb_(iq.data(), samples);
        }
        if (usado) rxBuf.erase(rxBuf.begin(), rxBuf.begin() + usado);
    }
    Logger::info("RTL-TCP: recvLoop encerrado");
}

void RtlTcpClient::sendCommand(uint8_t cmd, uint32_t value) {
    if (sock_ == (uintptr_t)(~0ULL)) return;
    std::lock_guard<std::mutex> lk(sendMutex_);
    char pkt[5];
    pkt[0] = (char)cmd;
    pkt[1] = (char)((value >> 24) & 0xFF);
    pkt[2] = (char)((value >> 16) & 0xFF);
    pkt[3] = (char)((value >>  8) & 0xFF);
    pkt[4] = (char)( value        & 0xFF);
    send((SOCKET)sock_, pkt, 5, 0);
}

} // namespace masdr
