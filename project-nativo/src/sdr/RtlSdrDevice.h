#pragma once
#include "ISdrDevice.h"
#include <thread>
#include <atomic>
#include <vector>
#include <string>

#ifndef RTLSDR_AVAILABLE
#define RTLSDR_AVAILABLE 0
#endif

namespace masdr {

struct DeviceInfo {
    std::string serial;
    std::string name;
};

class RtlSdrDevice : public ISdrDevice {
public:
    RtlSdrDevice();
    ~RtlSdrDevice() override;

    static std::vector<DeviceInfo> enumerate();

    std::string name()   const override { return name_; }
    std::string serial() const override { return serial_; }

    bool open(const std::string& serial = std::string()) override;
    void close() override;
    void start() override;
    void stop() override;

    void setCenterFreq(uint64_t hz) override;
    void setSampleRate(uint32_t sps) override;
    void setGain(int tenthsDb) override;
    void setBias(bool on) override;
    void setQuadrature(bool on) override;
    void setPpm(int ppm) override;

    uint64_t centerFreq()  const override { return freq_; }
    uint32_t sampleRate()  const override { return sps_; }
    int      gain()        const override { return gainTenths_; }

    void setCallback(SamplesCallback cb) override { cb_ = std::move(cb); }
    SamplesCallback callback() const { return cb_; }

    std::string lastError() const override { return lastError_; }

private:
    void readLoop();

    void*       dev_       = nullptr;
    std::string lastError_;
    std::string serial_;
    std::string name_ = "RTL-SDR";
    uint64_t freq_       = 100000000;
    uint32_t sps_        = 2048000;
    int      gainTenths_ = 280;
    bool     bias_       = false;
    bool     quadrature_ = false;
    int      ppm_        = 0;
    int      agcMode_    = -1;
    bool     deviceOk_   = false;

    std::atomic<bool> running_{false};
    std::thread       thread_;
    SamplesCallback   cb_;
};

} // namespace masdr
