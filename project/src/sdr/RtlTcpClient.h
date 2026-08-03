#pragma once
#include "ISdrDevice.h"

#include <QTcpSocket>
#include <QByteArray>
#include <QString>
#include <memory>
#include <cstdint>

namespace masdr {

class RtlTcpClient : public ISdrDevice {
public:
    RtlTcpClient();
    ~RtlTcpClient() override;

    QString name() const override   { return "RTL-TCP"; }
    QString serial() const override { return endpoint_; }

    bool open(const QString& serial = QString()) override;
    void close() override;
    void start() override;
    void stop() override;

    void setCenterFreq(uint64_t hz) override;
    void setSampleRate(uint32_t sps) override;
    void setGain(int tenthsDb) override;
    void setQuadrature(bool on) override;
    void setPpm(int ppm) override;

    uint64_t centerFreq() const override { return freq_; }
    uint32_t sampleRate() const override { return sps_; }
    int gain() const override { return gainTenths_; }

    void setCallback(SamplesCallback cb) override { cb_ = std::move(cb); }

    QString lastError() const override { return lastError_; }

private:
    void onReadyRead();
    void sendCommand(uint8_t cmd, uint32_t value);

    std::unique_ptr<QTcpSocket> socket_;
    QByteArray rxBytes_;
    QString endpoint_ = "127.0.0.1:1234";
    uint64_t freq_ = 100000000ULL;
    uint32_t sps_ = 2048000U;
    int gainTenths_ = 280;
    bool quadrature_ = false;
    int ppm_ = 0;
    bool gotServerHeader_ = false;
    bool running_ = false;
    QString lastError_;
    SamplesCallback cb_;
};

} // namespace masdr
