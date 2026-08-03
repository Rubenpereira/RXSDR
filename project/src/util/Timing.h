#pragma once
#include <chrono>

namespace masdr {

class Stopwatch {
public:
    Stopwatch() : t_(std::chrono::steady_clock::now()) {}
    double seconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - t_).count();
    }
    void reset() { t_ = std::chrono::steady_clock::now(); }
private:
    std::chrono::steady_clock::time_point t_;
};

} // namespace masdr
