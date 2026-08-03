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

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        lastError_ = "RTL-TCP: falha ao criar socket";
        Logger::error(lastError_);
        return false;
    }

    // Timeout de 3s na conexão
    DWORD timeout = 3000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    Logger::info("RTL-TCP: conectando a " + host + ":" + std::to_string(port));
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        lastError_ = "RTL-TCP: connect falhou para " + endpoint_;
        Logger::error(lastError_);
        closesocket(s);
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
        uint8_t tmp[4096];
        int n = recv((SOCKET)sock_, (char*)tmp, sizeof(tmp), 0);
        if (n <= 0) break;

        rxBuf.insert(rxBuf.end(), tmp, tmp + n);

        // Pula header de 12 bytes do servidor rtl_tcp
        if (!gotHeader_) {
            if (rxBuf.size() < 12) continue;
            rxBuf.erase(rxBuf.begin(), rxBuf.begin() + 12);
            gotHeader_ = true;
        }

        while (rxBuf.size() >= 16384 * 2) {
            const size_t take = 16384 * 2;
            const size_t samples = take / 2;
            if (iq.size() < samples) iq.resize(samples);
            for (size_t i = 0; i < samples; ++i) {
                float I = ((float)rxBuf[2*i]   - 127.5f) / 127.5f;
                float Q = ((float)rxBuf[2*i+1] - 127.5f) / 127.5f;
                iq[i] = { I, Q };
            }
            rxBuf.erase(rxBuf.begin(), rxBuf.begin() + take);
            if (running_ && cb_) cb_(iq.data(), samples);
        }
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
