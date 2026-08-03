#pragma once
#include "ISdrDevice.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace masdr {

class RtlTcpClient : public ISdrDevice {
public:
    RtlTcpClient();
    ~RtlTcpClient() override;

    std::string name()   const override { return "RTL-TCP"; }
    std::string serial() const override { return endpoint_; }

    bool open(const std::string& serial = std::string()) override;
    void close() override;
    void start() override;
    void stop() override;

    void setCenterFreq(uint64_t hz) override;
    void setSampleRate(uint32_t sps) override;
    void setGain(int tenthsDb) override;
    void setQuadrature(bool on) override;
    void setPpm(int ppm) override;

    uint64_t centerFreq()  const override { return freq_; }
    uint32_t sampleRate()  const override { return sps_; }
    int      gain()        const override { return gainTenths_; }

    void setCallback(SamplesCallback cb) override { cb_ = std::move(cb); }
    std::string lastError() const override { return lastError_; }

private:
    void recvLoop();
    void sendCommand(uint8_t cmd, uint32_t value);

    uintptr_t    sock_     = (uintptr_t)(~0ULL);
    std::string  endpoint_ = "127.0.0.1:1234";
    uint64_t     freq_         = 100000000ULL;
    uint32_t     sps_          = 2048000U;
    int          gainTenths_   = 280;
    bool         quadrature_   = false;
    int          ppm_          = 0;
    bool         gotHeader_    = false;
    std::atomic<bool> running_{false};
    std::string  lastError_;
    SamplesCallback cb_;
    std::thread  recvThread_;
    std::mutex   sendMutex_;
};

} // namespace masdr
