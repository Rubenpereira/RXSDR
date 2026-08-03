#pragma once
#include <atomic>
#include <vector>
#include <cstring>

// Ring buffer SPSC lock-free para amostras IQ (templated em T = std::complex<float>).
// 1 produtor (driver SDR) — N consumidores (FFT, demod, decoders) leem sob mutex
// separado se múltiplos consumidores. Aqui simples SPSC.

namespace masdr {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buf_(capacity), cap_(capacity), w_(0), r_(0) {}

    size_t capacity() const { return cap_; }
    size_t size() const {
        size_t w = w_.load(std::memory_order_acquire);
        size_t r = r_.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (cap_ - r + w);
    }

    // Escreve n itens. Retorna quanto foi de fato escrito.
    size_t write(const T* data, size_t n) {
        size_t w = w_.load(std::memory_order_relaxed);
        size_t r = r_.load(std::memory_order_acquire);
        size_t free = (r > w) ? (r - w - 1) : (cap_ - w + r - 1);
        size_t toWrite = std::min(n, free);
        for (size_t i = 0; i < toWrite; ++i) {
            buf_[(w + i) % cap_] = data[i];
        }
        w_.store((w + toWrite) % cap_, std::memory_order_release);
        return toWrite;
    }

    // Lê até n itens.
    size_t read(T* out, size_t n) {
        size_t r = r_.load(std::memory_order_relaxed);
        size_t w = w_.load(std::memory_order_acquire);
        size_t avail = (w >= r) ? (w - r) : (cap_ - r + w);
        size_t toRead = std::min(n, avail);
        for (size_t i = 0; i < toRead; ++i) {
            out[i] = buf_[(r + i) % cap_];
        }
        r_.store((r + toRead) % cap_, std::memory_order_release);
        return toRead;
    }

private:
    std::vector<T> buf_;
    size_t cap_;
    std::atomic<size_t> w_, r_;
};

} // namespace masdr
