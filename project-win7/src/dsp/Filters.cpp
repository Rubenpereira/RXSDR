#include "Filters.h"

namespace masdr {

void Filters::dcBlock(std::vector<std::complex<float>>& iq)
{
    if (iq.empty()) return;
    std::complex<float> mean{0.0f, 0.0f};
    for (const auto& s : iq) mean += s;
    mean /= static_cast<float>(iq.size());
    for (auto& s : iq) s -= mean;
}

} // namespace masdr
