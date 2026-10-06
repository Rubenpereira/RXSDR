#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  FilaAudio - leva o trabalho pesado de um decodificador para uma thread sua
//
//  O audio do radio chega na mesma thread que le o dongle, demodula e faz a
//  FFT (ver o comentario do audio ritmado no Application.h). SSTV, WEFAX e
//  PACTOR fazem conta de verdade a cada bloco - a fase do WEFAX dobra 30 s
//  de sinal em seis velocidades de uma vez - e isso ali engasgaria o som e a
//  cachoeira. Aqui o bloco so entra numa fila; a thread do decodificador
//  pega tudo o que juntou e processa no tempo dela.
// ---------------------------------------------------------------------------
class FilaAudio {
public:
    using Trabalho = std::function<void(const std::vector<int16_t>& pcm, uint32_t sps)>;

    explicit FilaAudio(Trabalho t) : trabalho_(std::move(t)) {}
    ~FilaAudio() { parar(); }

    void ligar()
    {
        parar();
        {
            std::lock_guard<std::mutex> lk(m_);
            fila_.clear(); amostras_ = 0; sair_ = false;
        }
        thread_ = std::thread([this] { laco(); });
        ativo_ = true;
    }

    void parar()
    {
        ativo_ = false;
        {
            std::lock_guard<std::mutex> lk(m_);
            sair_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> lk(m_);
        fila_.clear(); amostras_ = 0;
    }

    bool ligada() const { return ativo_.load(); }

    void empurrar(const int16_t* pcm, int n, uint32_t sps)
    {
        if (!pcm || n <= 0 || sps == 0) return;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (sair_ || !ativo_.load()) return;
            // Nao deixa a fila crescer sem fim se a thread ficar para tras
            // (PC muito fraco): mais de 20 s de audio esperando, larga o velho.
            while (!fila_.empty() && amostras_ > size_t(sps) * 20) {
                amostras_ -= fila_.front().pcm.size();
                fila_.pop_front();
            }
            fila_.push_back(Bloco{std::vector<int16_t>(pcm, pcm + n), sps});
            amostras_ += size_t(n);
        }
        cv_.notify_one();
    }

private:
    struct Bloco { std::vector<int16_t> pcm; uint32_t sps; };

    void laco()
    {
        std::deque<Bloco> pegos;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return sair_ || !fila_.empty(); });
                if (sair_) return;
                pegos.swap(fila_);
                amostras_ = 0;
            }
            for (const Bloco& b : pegos) trabalho_(b.pcm, b.sps);
            pegos.clear();
        }
    }

    Trabalho trabalho_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Bloco> fila_;
    size_t amostras_ = 0;
    bool sair_ = false;
    std::atomic<bool> ativo_{false};
    std::thread thread_;
};

} // namespace masdr
