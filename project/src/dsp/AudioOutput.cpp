#include "AudioOutput.h"

namespace masdr {

bool AudioOutput::start(uint32_t /*sampleRate*/, uint8_t /*channels*/)
{
    return true;
}

void AudioOutput::stop() {}

void AudioOutput::push(const std::vector<int16_t>& /*pcm*/) {}

} // namespace masdr
