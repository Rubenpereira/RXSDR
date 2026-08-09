#pragma once
#include <QString>
#include <QJsonObject>
#include <complex>
#include <cstdint>
#include <functional>

namespace masdr {

// Interface base para todos os decoders digitais:
// ADS-B, ACARS, AIS, DMR, RTTY, SITOR-B, CW, PACTOR, Packet (AX.25),
// SELCAL, BPSK, Radiossondas, HFDL.

class IDecoder {
public:
    virtual ~IDecoder() = default;

    virtual QString name() const = 0;
    virtual void setSampleRate(uint32_t sps) = 0;

    // Recebe bloco de IQ pré-decimado da banda do decoder
    virtual void feed(const std::complex<float>* iq, size_t n) = 0;

    using MessageCallback = std::function<void(const QJsonObject&)>;
    void setOnMessage(MessageCallback cb) { onMsg_ = std::move(cb); }

protected:
    MessageCallback onMsg_;
};

} // namespace masdr
