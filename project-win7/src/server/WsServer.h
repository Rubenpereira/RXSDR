#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

using WsSocket = uintptr_t;

namespace masdr {

class WsServer {
public:
    WsServer();
    ~WsServer();

    bool listen(uint16_t port);
    void stop();
    uint16_t port() const { return port_; }

    void broadcastBinary(const uint8_t* data, size_t len);
    void broadcastText(const std::string& msg);
    void broadcastFftThreadSafe(const std::vector<int8_t>& bins, uint64_t centerHz, uint32_t sps);
    void broadcastAudioThreadSafe(const std::vector<int16_t>& pcm, uint32_t sps);
    void broadcastJsonThreadSafe(const std::string& json);

    int clientCount() const;

    std::function<void(int)> onClientsChanged;

private:
    void acceptLoop();
    void clientLoop(WsSocket sock);
    bool doHandshake(WsSocket sock, std::string& out_path);
    void removeClient(WsSocket sock);
    bool sendFrame(WsSocket sock, uint8_t opcode, const uint8_t* payload, size_t payloadLen);

    WsSocket          serverSock_ = (WsSocket)(~0ULL);
    uint16_t          port_       = 0;
    std::atomic<bool> running_{false};
    std::thread       acceptThread_;

    mutable std::mutex    clientsMutex_;
    std::vector<WsSocket> clients_;
};

} // namespace masdr
