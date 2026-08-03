#pragma once
#include "ISdrDevice.h"
#include <QThread>
#include <QString>
#include <atomic>
#include <vector>

// Wrapper sobre librtlsdr (rtl-sdr.h).
// Quando compilar sem a lib, deixe RTLSDR_AVAILABLE como 0.
// Em release definimos -DRTLSDR_AVAILABLE=1 e linkamos librtlsdr.

#ifndef RTLSDR_AVAILABLE
#define RTLSDR_AVAILABLE 0
#endif

namespace masdr {

struct DeviceInfo {
    QString serial;
    QString name;
};

class RtlSdrDevice : public ISdrDevice {
public:
    RtlSdrDevice();
    ~RtlSdrDevice() override;

    static std::vector<DeviceInfo> enumerate();

    QString name() const override   { return name_; }
    QString serial() const override { return serial_; }

    bool open(const QString& serial = QString()) override;
    void close() override;
    void start() override;
    void stop() override;

    void setCenterFreq(uint64_t hz) override;
    void setSampleRate(uint32_t sps) override;
    void setGain(int tenthsDb) override;
    void setBias(bool on) override;
    void setQuadrature(bool on) override;
    void setPpm(int ppm) override;

    uint64_t centerFreq() const override { return freq_; }
    uint32_t sampleRate() const override { return sps_; }
    int gain() const override { return gainTenths_; }

    void setCallback(SamplesCallback cb) override { cb_ = std::move(cb); }
    SamplesCallback callback() const { return cb_; }

    QString lastError() const override { return lastError_; }

private:
    void readLoop();

    void* dev_ = nullptr;        // rtlsdr_dev_t*
    QString lastError_;
    QString serial_;
    QString name_ = "RTL-SDR";
    uint64_t freq_ = 100000000;
    uint32_t sps_  = 2048000;
    int gainTenths_ = 280;
    bool bias_ = false;
    bool quadrature_ = false;
    int ppm_ = 0;
    int agcMode_ = -1;  // -1 = não inicializado; evita chamadas USB redundantes a rtlsdr_set_agc_mode
    bool deviceOk_ = false;

    std::atomic<bool> running_{false};
    QThread* thread_ = nullptr;
    SamplesCallback cb_;
};

} // namespace masdr
