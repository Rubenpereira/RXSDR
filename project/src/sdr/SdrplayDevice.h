#pragma once
#include "ISdrDevice.h"

#include <QThread>
#include <QString>
#include <atomic>
#include <vector>
#include <complex>

// SDRPLAY_AVAILABLE é definido pelo CMakeLists.txt (1 se o header for encontrado)
#ifndef SDRPLAY_AVAILABLE
#define SDRPLAY_AVAILABLE 0
#endif

namespace masdr {

struct SdrplayInfo {
    QString serial;
    QString name;
};

class SdrplayDevice : public ISdrDevice {
public:
    SdrplayDevice();
    ~SdrplayDevice() override;

    // Enumera dispositivos SDRplay conectados
    static std::vector<SdrplayInfo> enumerate();

    // ISdrDevice interface
    QString  name()   const override { return name_; }
    QString  serial() const override { return serial_; }

    bool open(const QString& serial = QString()) override;
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

    // Usado internamente pelo callback de stream (acesso controlado)
    void deliverSamples(const std::complex<float>* data, size_t n) {
        if (cb_) cb_(data, n);
    }
    bool isRunning() const { return running_.load(); }

    // Getters para uso interno do callback estático
    int      ifMode()      const { return sdrplayIfMode_; }
    uint32_t streamRate()  const { return sps_; }  // taxa real do stream (= adcFs / decimFactor)

    // Converte amostras brutas ADC → complex<float> com negação de Q (todos os modos).
    // O SDRplay entrega espectro padrão (I+jQ); o DemodSSB foi escrito para o espectro
    // invertido do RTL-SDR. Negar Q iguala as convenções → LSB/USB corretos.
    void processBatchSamples(const short* xi, const short* xq,
                             unsigned numSamples,
                             std::vector<std::complex<float>>& buf);

private:
    QString  serial_;
    QString  name_ = "SDRplay";
    uint64_t freq_       = 100'000'000ULL;  // 100 MHz padrão
    uint32_t sps_        = 2'000'000U;
    int      gainTenths_ = 500;              // 50 dB (em décimos)
    int      sdrplayIfMode_ = 0;
    int      sdrplayLnaState_ = 5;
    int      sdrplayIfGain_ = 40;
    bool     sdrplayIfAgc_ = false;
    int      sdrplayBw_ = -1;
    int      ppm_ = 0;

    std::atomic<bool> running_{ false };
    QThread*          thread_  = nullptr;
    SamplesCallback   cb_;

    // Estado interno do dispositivo SDRplay (opaco para não vazar sdrplay_api.h)
    void* devHandle_   = nullptr;  // sdrplay_api_DeviceT::dev (HANDLE)
    void* devStruct_   = nullptr;  // cópia de sdrplay_api_DeviceT (alocado em open())
    void* devParams_   = nullptr;  // sdrplay_api_DeviceParamsT* (ponteiro da API)
};

} // namespace masdr
