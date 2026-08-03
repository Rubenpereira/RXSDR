#pragma once
#include "ISdrDevice.h"
#include <string>
#include <atomic>
#include <vector>
#include <complex>

#ifndef SDRPLAY_AVAILABLE
#define SDRPLAY_AVAILABLE 0
#endif

namespace masdr {

struct SdrplayInfo {
    std::string serial;
    std::string name;
};

class SdrplayDevice : public ISdrDevice {
public:
    SdrplayDevice();
    ~SdrplayDevice() override;

    static std::vector<SdrplayInfo> enumerate();

    std::string name()   const override { return name_; }
    std::string serial() const override { return serial_; }

    bool open(const std::string& serial = std::string()) override;
    void close() override;
    void start() override;
    void stop() override;

    void setCenterFreq(uint64_t hz) override;
    void setSampleRate(uint32_t sps) override;
    void setGain(int tenthsDb) override;
    void setPpm(int ppm) override;
    void setSdrplayParams(int ifMode, int lnaState, int ifGain, bool ifAgc, int bw);

    uint64_t centerFreq()  const override { return freq_; }
    uint32_t sampleRate()  const override { return sps_; }
    int      gain()        const override { return gainTenths_; }

    void setCallback(SamplesCallback cb) override { cb_ = std::move(cb); }

    void deliverSamples(const std::complex<float>* data, size_t n) {
        if (cb_) cb_(data, n);
    }
    bool isRunning() const { return running_.load(); }

    int      ifMode()     const { return sdrplayIfMode_; }
    uint32_t streamRate() const { return sps_; }

    void processBatchSamples(const short* xi, const short* xq,
                             unsigned numSamples,
                             std::vector<std::complex<float>>& buf);

private:
    std::string  serial_;
    std::string  name_ = "SDRplay";
    uint64_t freq_       = 100000000ULL;
    uint32_t sps_        = 2000000U;
    int      gainTenths_ = 500;
    int      sdrplayIfMode_   = 0;
    int      sdrplayLnaState_ = 5;
    int      sdrplayIfGain_   = 40;
    bool     sdrplayIfAgc_    = false;
    int      sdrplayBw_       = -1;
    int      ppm_             = 0;

    std::atomic<bool> running_{false};
    SamplesCallback   cb_;

    void* devHandle_  = nullptr;
    void* devStruct_  = nullptr;
    void* devParams_  = nullptr;
};

} // namespace masdr
