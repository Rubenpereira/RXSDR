#pragma once
#include <string>
#include <vector>
#include <complex>
#include <cstdint>
#include <functional>

namespace masdr {

class ISdrDevice {
public:
    virtual ~ISdrDevice() = default;

    virtual std::string name()   const = 0;
    virtual std::string serial() const = 0;

    virtual bool open(const std::string& serial = std::string()) = 0;
    virtual void close() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void setCenterFreq(uint64_t hz) = 0;
    virtual void setSampleRate(uint32_t sps) = 0;
    virtual void setGain(int tenthsDb) = 0;
    virtual void setBias(bool) {}
    virtual void setQuadrature(bool) {}
    virtual void setPpm(int) {}
    virtual void setAntenna(const std::string&) {}

    virtual std::string lastError() const { return {}; }
    virtual std::vector<std::string> listAntennas() const { return {}; }
    virtual uint64_t centerFreq()  const = 0;
    virtual uint32_t sampleRate()  const = 0;
    virtual int      gain()        const = 0;

    using SamplesCallback = std::function<void(const std::complex<float>* iq, size_t n)>;
    virtual void setCallback(SamplesCallback cb) = 0;
};

} // namespace masdr
