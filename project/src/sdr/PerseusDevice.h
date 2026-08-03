#pragma once
#include "ISdrDevice.h"
#include <QString>
#include <atomic>
#include <complex>
#include <vector>

namespace masdr {

struct PerseusInfo {
    QString serial;
    QString name;
};

class PerseusDevice : public ISdrDevice {
public:
    PerseusDevice();
    ~PerseusDevice() override;

    static std::vector<PerseusInfo> enumerate();

    QString name() const override { return name_; }
    QString serial() const override { return serial_; }

    bool open(const QString& serial = QString()) override;
    void close() override;
    void start() override;
    void stop() override;

    void setCenterFreq(uint64_t hz) override;
    void setSampleRate(uint32_t sps) override;
    void setGain(int tenthsDb) override;

    uint64_t centerFreq() const override { return freq_; }
    uint32_t sampleRate() const override { return sps_; }
    int gain() const override { return gainTenths_; }

    void setCallback(SamplesCallback cb) override { cb_ = std::move(cb); }

    void deliverSamples(const std::complex<float>* data, size_t n) {
        if (cb_) cb_(data, n);
    }
    bool isRunning() const { return running_.load(); }

private:
    QString serial_;
    QString name_ = "Perseus SDR";
    uint64_t freq_ = 7000000ULL; // 7 MHz padrão HF
    uint32_t sps_ = 1000000U;    // 1 MSPS padrão
    int gainTenths_ = 0;         // 0 dB attenuation (max gain)

    std::atomic<bool> running_{false};
    SamplesCallback cb_;

    void* dev_ = nullptr;        // Descriptor perseus_descr*
};

} // namespace masdr
