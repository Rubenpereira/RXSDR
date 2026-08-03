#pragma once
#include <QString>
#include <QStringList>
#include <complex>
#include <cstdint>
#include <functional>

namespace masdr {

class ISdrDevice {
public:
    virtual ~ISdrDevice() = default;

    // Identificação
    virtual QString name() const = 0;
    virtual QString serial() const = 0;

    // Ciclo de vida
    virtual bool open(const QString& serial = QString()) = 0;
    virtual void close() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    // Sintonia / ganho
    virtual void setCenterFreq(uint64_t hz) = 0;
    virtual void setSampleRate(uint32_t sps) = 0;
    virtual void setGain(int tenthsDb) = 0;     // -10..496 (-1 = auto AGC)
    virtual void setBias(bool /*on*/) {}        // bias-T (RTL-SDR v3)
    virtual void setQuadrature(bool /*on*/) {}  // direct sampling I (HF)
    virtual void setPpm(int /*ppm*/) {}         // correção de frequência
    virtual void setAntenna(const QString&) {}  // SDRplay RSP2/RSPdx/RSPduo

    virtual QString lastError() const { return {}; }

    // Introspecção
    virtual QStringList listAntennas() const { return {}; }
    virtual uint64_t centerFreq() const = 0;
    virtual uint32_t sampleRate() const = 0;
    virtual int gain() const = 0;

    // Stream IQ: callback recebe blocos de amostras complexas
    using SamplesCallback = std::function<void(const std::complex<float>* iq, size_t n)>;
    virtual void setCallback(SamplesCallback cb) = 0;
};

} // namespace masdr
