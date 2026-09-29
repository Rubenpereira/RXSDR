#include "SomNativo.h"
#include "../util/Logger.h"

#include <algorithm>
#include <cmath>

namespace masdr {

// Saturacao suave igual a da pagina: linear ate 0,6, depois arredonda os
// picos em vez de corta-los. Devolve a amostra de 16 bits.
static inline int16_t paraPlaca(float x)
{
    const float ax = std::fabs(x);
    if (ax > 0.6f) x = std::copysign(0.6f + 0.38f * std::tanh((ax - 0.6f) / 0.38f), x);
    return int16_t(std::lround(std::max(-1.f, std::min(1.f, x)) * 32767.f));
}

std::vector<std::string> SomNativo::listarSaidas()
{
    std::vector<std::string> v;
    const UINT n = waveOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        WAVEOUTCAPSW c{};
        if (waveOutGetDevCapsW(i, &c, sizeof c) != MMSYSERR_NOERROR) { v.push_back("?"); continue; }
        char b[256]{};
        WideCharToMultiByte(CP_UTF8, 0, c.szPname, -1, b, sizeof b, nullptr, nullptr);
        v.push_back(b);
    }
    return v;
}

bool SomNativo::gravar(const std::string& arquivo)
{
    std::lock_guard<std::mutex> lk(gravMutex_);
    if (grav_) return true;
    grav_ = std::fopen(arquivo.c_str(), "wb");
    if (!grav_) return false;
    // cabecalho com tamanho zero; acertado em pararGravacao()
    const unsigned char h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
                                 0x80,0xBB,0,0, 0x00,0x77,0x01,0, 2,0,16,0,'d','a','t','a',0,0,0,0};
    std::fwrite(h, 1, 44, grav_);
    gravadas_ = 0;
    gravando_ = true;
    return true;
}

void SomNativo::pararGravacao()
{
    std::lock_guard<std::mutex> lk(gravMutex_);
    if (!grav_) return;
    const uint32_t dados = uint32_t(gravadas_.load() * 2);
    const uint32_t riff = 36 + dados;
    std::fseek(grav_, 4, SEEK_SET);  std::fwrite(&riff, 4, 1, grav_);
    std::fseek(grav_, 40, SEEK_SET); std::fwrite(&dados, 4, 1, grav_);
    std::fclose(grav_);
    grav_ = nullptr;
    gravando_ = false;
}

bool SomNativo::iniciar(int dispositivo)
{
    if (aberto_) return true;
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 1;
    fmt.nSamplesPerSec  = kTaxa;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = 2;
    fmt.nAvgBytesPerSec = kTaxa * 2;

    evento_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    const MMRESULT r = waveOutOpen(&h_, dispositivo < 0 ? WAVE_MAPPER : (UINT)dispositivo, &fmt, (DWORD_PTR)evento_, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        erro_ = "waveOutOpen falhou (" + std::to_string((int)r) + ")";
        Logger::error("SomNativo: " + erro_);
        CloseHandle(evento_); evento_ = nullptr; h_ = nullptr;
        return false;
    }
    for (int i = 0; i < kNBlocos; ++i) {
        dados_[i].assign(kBloco, 0);
        hdr_[i] = {};
        hdr_[i].lpData = reinterpret_cast<LPSTR>(dados_[i].data());
        hdr_[i].dwBufferLength = kBloco * 2;
        waveOutPrepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
    }
    aberto_ = true;
    rodando_ = true;
    t_ = std::thread([this] { laco(); });
    Logger::info("SomNativo: placa aberta, 48 kHz mono");
    return true;
}

void SomNativo::parar()
{
    if (!aberto_) return;
    rodando_ = false;
    if (evento_) SetEvent(evento_);
    if (t_.joinable()) t_.join();
    waveOutReset(h_);
    for (int i = 0; i < kNBlocos; ++i) waveOutUnprepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
    waveOutClose(h_);
    h_ = nullptr;
    CloseHandle(evento_); evento_ = nullptr;
    aberto_ = false;
    std::lock_guard<std::mutex> lk(filaMutex_);
    fila_.clear();
}

void SomNativo::ajustar(const AjusteAudio& a)
{
    proc_.ajustar(a);
    const float v = a.volume;
    ganhoAlvo_.store(v < 0 ? 0.f : (v > 8.f ? 8.f : v));
}

// Entrada: reamostra para 48 kHz (linear, com estado entre chamadas), passa
// pelos redutores de ruido e poe na fila.
void SomNativo::empurrar(const int16_t* pcm, size_t n, uint32_t sps)
{
    if (!aberto_ || !pcm || n == 0 || sps == 0) return;
    std::lock_guard<std::mutex> lkE(entradaMutex_);
    if (sps != spsEntrada_) { spsEntrada_ = sps; posEntrada_ = 0.0; temUltima_ = false; }
    const double passo = double(sps) / kTaxa;

    // Mesmo desenho do RttyManager: a ultima amostra da chamada anterior vai
    // na frente, e a posicao fracionaria continua de onde parou.
    std::vector<float> ent;
    ent.reserve(n + 1);
    if (temUltima_) ent.push_back(ultimaEntrada_);
    for (size_t i = 0; i < n; ++i) ent.push_back(pcm[i] / 32768.f);
    if (ent.size() < 2) return;
    bloco_.clear();
    double pos = posEntrada_;
    while (pos + 1.0 < double(ent.size())) {
        const size_t i0 = size_t(pos);
        const float f = float(pos - double(i0));
        bloco_.push_back(ent[i0] + (ent[i0 + 1] - ent[i0]) * f);
        pos += passo;
    }
    ultimaEntrada_ = ent.back();
    temUltima_ = true;
    posEntrada_ = pos - double(ent.size() - 1);
    if (posEntrada_ < 0.0) posEntrada_ = 0.0;
    if (bloco_.empty()) return;

    proc_.processar(bloco_.data(), bloco_.size());
    if (gravando_) {
        std::lock_guard<std::mutex> lg(gravMutex_);
        if (grav_) {
            std::vector<int16_t> pcm16(bloco_.size());
            for (size_t i = 0; i < bloco_.size(); ++i)
                pcm16[i] = int16_t(std::lround(std::max(-1.f, std::min(1.f, bloco_[i])) * 32767.f));
            std::fwrite(pcm16.data(), 2, pcm16.size(), grav_);
            gravadas_ += (long long)pcm16.size();
        }
    }
    {
        double soma = 0;
        for (float x : bloco_) soma += double(x) * x;
        nivel_.store(float(std::min(1.0, std::sqrt(soma / double(bloco_.size())) / 0.22)));
    }

    std::lock_guard<std::mutex> lk(filaMutex_);
    if (faltaPendente_) {
        faltaPendente_ = false;
        if (std::chrono::steady_clock::now() - faltaEm_ < std::chrono::seconds(2)) {
            faltas_.fetch_add(1);
            alvo_ = std::min(kAlvoMax, alvo_ * 3 / 2);   // travou: cresce a reserva
        }
    }
    fila_.insert(fila_.end(), bloco_.begin(), bloco_.end());
    totalEntrada_ += (long long)bloco_.size();
    // teto: bem acima do alvo, guardar mais nao e folga, e atraso
    const size_t teto = size_t(std::max(kTeto, alvo_ * 2));
    if (fila_.size() > teto) fila_.erase(fila_.begin(), fila_.begin() + long(fila_.size() - teto));
}

void SomNativo::laco()
{
    // primeira volta: todos os blocos vazios vao para a placa
    for (int i = 0; i < kNBlocos; ++i) encher(i);
    while (rodando_) {
        WaitForSingleObject(evento_, 50);
        if (!rodando_) break;
        for (int i = 0; i < kNBlocos; ++i)
            if (hdr_[i].dwFlags & WHDR_DONE) encher(i);
    }
}

// Enche um bloco de 20 ms e manda para a placa.
void SomNativo::encher(int i)
{
    int16_t* out = dados_[i].data();
    {
        std::lock_guard<std::mutex> lk(filaMutex_);
        const int tam = int(fila_.size());
        filaMs_.store(tam * 1000 / kTaxa);

        // Espera juntar o alvo antes de tocar (inicio, ou depois de uma falta):
        // tocar com a fila quase vazia so trocaria uma falta por varias.
        if (enchendo_ && tam >= alvo_) {
            enchendo_ = false;
            filaMedia_ = tam;     // a media recomeca daqui (senao o 1o segundo sairia lento)
        }

        // Ritmo em duas partes:
        //  1) base = razao entre o que chega e o que a placa consome, medida
        //     em ate 20 s. E o "relogio da placa" (no PC do amigo ~1,088).
        //     Como conta amostras, uma rajada nao muda a conta: so o tempo.
        //  2) uma correcao pequena pela fila media, so para trazer a fila de
        //     volta ao alvo (sem ela a fila ficaria onde a rajada a deixou).
        //  A medida (1) so conta enquanto toca: radio desligado ou pausa na
        //  entrada nao sao "placa lenta".
        if (!enchendo_) filaMedia_ += (tam - filaMedia_) * 0.01;   // ~2 s de media
        const double erro = std::max(-1.0, std::min(2.0, (filaMedia_ - alvo_) / alvo_));
        const double passo = std::max(0.85, std::min(1.2, base_ * (1.0 + 0.03 * erro)));
        passoAtual_.store(passo);

        const int precisa = int(std::ceil(kBloco * passo)) + 2;
        if (enchendo_ || tam < precisa) {
            std::fill(out, out + kBloco, 0);
            if (!enchendo_) {
                // Acabou agora. Pode ser a maquina que travou mais do que a
                // reserva aguenta, ou so o radio que desligou / reconfigurou.
                // Quem decide e o empurrar(): se o audio voltar em menos de
                // 2 s foi travada (conta a falta e cresce a reserva).
                enchendo_ = true;
                faltaPendente_ = true;
                faltaEm_ = std::chrono::steady_clock::now();
                marcas_.clear(); contaBlocos_ = 0;
                // o que sobrou sai com o volume caindo ate zero: sem estalo
                const int m = std::min(tam, kBloco);
                for (int k = 0; k < m; ++k)
                    out[k] = paraPlaca(fila_[size_t(k)] * ganhoAtual_ * (1.f - float(k) / float(m)));
                fila_.erase(fila_.begin(), fila_.begin() + m);
            }
            ganhoAtual_ = 0.f;           // volta em rampa, sem estalo
        } else {
            const float alvoG = ganhoAlvo_.load();
            // rampa de ~10 ms no ganho: mudo e squelch nao estalam
            const float passoG = 1.f - std::exp(-1.f / (kTaxa * 0.010f));
            double f = frac_;
            size_t lidas = 0;
            for (int k = 0; k < kBloco; ++k) {
                const float a = fila_[lidas], b = fila_[lidas + 1];
                float x = a + (b - a) * float(f);
                ganhoAtual_ += (alvoG - ganhoAtual_) * passoG;
                out[k] = paraPlaca(x * ganhoAtual_);
                f += passo;
                while (f >= 1.0) { f -= 1.0; ++lidas; }
            }
            frac_ = f;
            fila_.erase(fila_.begin(), fila_.begin() + long(lidas));

            totalSaida_ += kBloco;
            if (++contaBlocos_ >= kTaxa / kBloco) {          // a cada 1 s
                contaBlocos_ = 0;
                marcas_.emplace_back(totalEntrada_, totalSaida_);
                if (marcas_.size() > 21) marcas_.pop_front();
                if (marcas_.size() >= 4) {                      // 3 s de medida
                    const double dEnt = double(marcas_.back().first - marcas_.front().first);
                    const double dSai = double(marcas_.back().second - marcas_.front().second);
                    if (dSai > 0) {
                        const double r = std::max(0.85, std::min(1.18, dEnt / dSai));
                        base_ += (r - base_) * (marcas_.size() < 11 ? 0.5 : 0.2);
                    }
                }
            }
        }
    }
    waveOutWrite(h_, &hdr_[i], sizeof(WAVEHDR));
}

} // namespace masdr
