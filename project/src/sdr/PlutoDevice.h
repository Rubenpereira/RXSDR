#pragma once
#include "ISdrDevice.h"
#include <QThread>
#include <QString>
#include <atomic>
#include <complex>
#include <vector>

namespace masdr {

struct PlutoInfo {
    QString serial;
    QString name;
};

class PlutoDevice : public ISdrDevice {
public:
    PlutoDevice();
    ~PlutoDevice() override;

    static std::vector<PlutoInfo> enumerate();

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
    void runLoop();

    class StreamThread : public QThread {
    public:
        StreamThread(PlutoDevice* dev) : dev_(dev) {}
        void run() override { dev_->runLoop(); }
    private:
        PlutoDevice* dev_;
    };

    QString serial_;
    QString name_ = "PlutoSDR";
    uint64_t freq_ = 100000000ULL; // 100 MHz padrão
    uint32_t sps_ = 2048000U;      // 2.048 MSPS padrão
    int gainTenths_ = 280;         // 28.0 dB padrão

    std::atomic<bool> running_{false};
    StreamThread* thread_ = nullptr;
    SamplesCallback cb_;

    void* ctx_ = nullptr;          // iio_context*
    void* rxDev_ = nullptr;        // iio_device* (cf-ad9361-lpc)
    void* phyDev_ = nullptr;       // iio_device* (ad9361-phy)
    void* rxBuf_ = nullptr;        // iio_buffer*
};

} // namespace masdr
