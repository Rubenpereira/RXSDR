#pragma once
#include <QString>
#include <cstdint>
#include <functional>
#include <complex>
#include <vector>

namespace masdr {

class Demodulator {
public:
    virtual ~Demodulator() = default;

    virtual void setMode(const QString& mode) {
        QString newMode = mode.toUpper();
        if (newMode != mode_) {
            mode_ = newMode;
            carryOver_.clear();
        }
    }
    virtual QString mode() const { return mode_; }
    virtual void setBandwidth(uint32_t bwHz) { bwHz_ = bwHz; }
    virtual void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) = 0;

    void setAudioCallback(std::function<void(const std::vector<int16_t>&, uint32_t)> cb) {
        onAudio_ = std::move(cb);
    }

    virtual void setIsDigital(bool isDigital) { isDigital_ = isDigital; }
    virtual bool isDigital() const { return isDigital_; }

protected:
    static constexpr uint32_t kAudioRate = 48000;
    static size_t audioDecimStep(uint32_t sampleRate) {
        return sampleRate > kAudioRate ? sampleRate / kAudioRate : 1;
    }

    // Retorna a nova taxa de amostragem
    uint32_t preDecimate(const std::complex<float>* src, size_t n, uint32_t inRate,
                         std::vector<std::complex<float>>& dest) const
    {
        uint32_t factor = 1;
        if (inRate >= 2000000) factor = 8;
        else if (inRate >= 1000000) factor = 4;
        else if (inRate >= 500000) factor = 2;

        if (factor == 1) {
            dest.assign(src, src + n);
            return inRate;
        }

        const size_t outN = n / factor;
        dest.resize(outN);
        const float invFactor = 1.0f / static_cast<float>(factor);

        for (size_t i = 0; i < outN; ++i) {
            std::complex<float> sum = 0.0f;
            const size_t start = i * factor;
            for (size_t j = 0; j < factor; ++j) {
                sum += src[start + j];
            }
            dest[i] = sum * invFactor;
        }

        return inRate / factor;
    }

    // Processa a entrada de forma contínua, mantendo o carryover
    uint32_t processInput(const std::complex<float>* iq, size_t n, uint32_t sampleRate,
                          uint32_t factor, size_t step,
                          std::vector<std::complex<float>>& decimatedOutput)
    {
        if (lastSampleRate_ != sampleRate) {
            carryOver_.clear();
            lastSampleRate_ = sampleRate;
        }

        // Mescla o carryover anterior com os novos amostras
        std::vector<std::complex<float>> merged;
        merged.reserve(carryOver_.size() + n);
        merged.insert(merged.end(), carryOver_.begin(), carryOver_.end());
        merged.insert(merged.end(), iq, iq + n);

        const size_t totalStep = factor * step;
        const size_t processableInputSamples = (merged.size() / totalStep) * totalStep;

        // Salva a sobra no carryOver_
        if (processableInputSamples < merged.size()) {
            carryOver_.assign(merged.begin() + processableInputSamples, merged.end());
        } else {
            carryOver_.clear();
        }

        // Pre-decima apenas a parte processável
        const size_t decimatedSize = processableInputSamples / factor;
        decimatedOutput.resize(decimatedSize);

        if (factor <= 1) {
            if (decimatedSize > 0) {
                std::copy(merged.begin(), merged.begin() + processableInputSamples, decimatedOutput.begin());
            }
        } else {
            const float invFactor = 1.0f / static_cast<float>(factor);
            for (size_t i = 0; i < decimatedSize; ++i) {
                std::complex<float> sum = 0.0f;
                const size_t start = i * factor;
                for (size_t j = 0; j < factor; ++j) {
                    sum += merged[start + j];
                }
                decimatedOutput[i] = sum * invFactor;
            }
        }

        return sampleRate / factor;
    }

    QString mode_ = "USB";
    uint32_t bwHz_ = 3000;
    std::function<void(const std::vector<int16_t>&, uint32_t)> onAudio_;
    std::vector<std::complex<float>> carryOver_;
    uint32_t lastSampleRate_ = 0;
    bool isDigital_ = false;
};

} // namespace masdr
