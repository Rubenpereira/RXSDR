#pragma once
#include <vector>
#include <cstdint>

namespace masdr {

class AudioOutput {
public:
    bool start(uint32_t sampleRate, uint8_t channels);
    void stop();
    void push(const std::vector<int16_t>& pcm);
};

} // namespace masdr
