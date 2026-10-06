#include "Decoders.h"

#include "../decoders/AleCore.h"
#include "../decoders/AnaliseCore.h"
#include "../decoders/CwCore.h"
#include "../decoders/DscCore.h"
#include "../decoders/PactorCore.h"
#include "../decoders/RttyCore.h"
#include "../decoders/SitorBCore.h"

#include <windows.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#include <fstream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>

namespace masdr {

namespace {
constexpr double kTaxa = 8000.0;               // taxa de trabalho de todos os nucleos
constexpr size_t kFilaMax = 48000 * 3;         // mais de 3 s atrasado: joga o velho fora

std::string horaUtc()
{
    SYSTEMTIME st; GetSystemTime(&st);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return b;
}
}

const char* Decoders::nome(Tipo t)
{
    switch (t) {
    case CW: return "CW / Morse";
    case RTTY: return "RTTY";
    case SITORB: return "SITOR-B / NAVTEX";
    case DSC: return "DSC";
    case ALE: return "ALE 2G";
    case DMR: return "DMR / P25 / NXDN (dsd-fme)";
    case TETRA: return "TETRA";
    case HFDL: return "HFDL (ACARS em HF)";
    case AIS: return "AIS (navios)";
    case APRS: return "APRS";
    case ACARS: return "ACARS (131,550 / 131,825)";
    case VDL2: return "VDL2 (136,975)";
    case ANALISE: return "Analisar sinal";
    case DRM: return "DRM (rádio digital)";
    case SSTV: return "SSTV (imagens)";
    case WEFAX: return "WEFAX (fax meteorológico)";
    case PACTOR: return "PACTOR-I (Marinha / FEC)";
    default: return "Nenhum";
    }
}

Decoders::Decoders()
{
    th_ = std::thread([this] { laco(); });
}

Decoders::~Decoders()
{
    dsd_.parar();
    tetra_.parar();
    hfdl_.parar(); ais_.parar(); aprs_.parar(); acars_.parar(); vdl2_.parar(); drm_.parar();
    {
        std::lock_guard<std::mutex> lk(filaMutex_);
        sair_ = true;
    }
    filaCv_.notify_all();
    if (th_.joinable()) th_.join();
}

void Decoders::iniciar(Tipo t, const Ajustes& a)
{
    std::lock_guard<std::mutex> lk(coreMutex_);
    aj_ = a;
    cw_.reset(); rtty_.reset(); sitor_.reset(); dsc_.reset(); ale_.reset(); analise_.reset(); pactor_.reset();
    analiseFeita_ = false;
    if (t != DMR) dsd_.parar();
    if (t != TETRA) tetra_.parar();
    if (t != HFDL) hfdl_.parar();
    if (t != AIS) ais_.parar();
    if (t != APRS) aprs_.parar();
    if (t != ACARS) acars_.parar();
    if (t != VDL2) vdl2_.parar();
    if (t != DRM) drm_.parar();
    switch (t) {
    case CW: {
        CwCore::Params p;
        p.sampleRate = kTaxa;
        p.tomHz = a.cwTom > 0 ? a.cwTom : 0.0;
        p.autoTom = a.cwTom <= 0;
        cw_ = std::make_unique<CwCore>(p);
        escrever("[CW] iniciado - o tom e a velocidade sao medidos sozinhos\n");
        break;
    }
    case RTTY: {
        RttyCore::Params p;
        p.sampleRate = kTaxa;
        p.baudRate = a.rttyBaud;
        p.shift = a.rttyShift;
        p.invert = a.rttyInverter;
        rtty_ = std::make_unique<RttyCore>(p);
        char b[128];
        std::snprintf(b, sizeof b, "[RTTY] iniciado - %.2f baud, shift %.0f Hz%s\n", a.rttyBaud, a.rttyShift,
                      a.rttyInverter ? ", invertido" : "");
        escrever(b);
        break;
    }
    case SITORB: {
        SitorBCore::Params p;
        p.sampleRate = kTaxa;
        p.shift = a.sitorShift;
        p.invert = a.sitorInverter;
        sitor_ = std::make_unique<SitorBCore>(p);
        escrever("[SITOR-B] iniciado - 100 baud, recepcao em USB\n");
        break;
    }
    case DSC: {
        DscCore::Params p;
        p.sampleRate = kTaxa;
        p.invert = a.dscInverter;
        dsc_ = std::make_unique<DscCore>(p);
        escrever("[DSC] iniciado - 100 baud, shift 170 Hz, recepcao em USB\n");
        break;
    }
    case PACTOR:
        pactor_ = std::make_unique<PactorCore>(kTaxa);
        pactorUltimo_ = -1e9; pactorRelogio_ = 0;
        escrever("[PACTOR-I] iniciado - procurando pacotes (100/200 baud, shift 200 Hz, USB)\n");
        break;
    case ALE:
        ale_ = std::make_unique<AleCore>();
        escrever("[ALE] iniciado - 2G ALE (MIL-STD-188-141), 8-FSK 125 baud, USB com BW de 3 kHz\n");
        break;
    case DMR: {
        dsd_.aoTexto = [this](const std::string& s) { escrever(s); };
        dsd_.setTaxaVoz(a.dmrTaxaVoz);
        std::string erro;
        if (!dsd_.iniciar(a.dmrModo, a.dmrInverter, erro)) escrever("[DMR] " + erro + "\n");
        break;
    }
    case TETRA: {
        tetra_.aoTexto = [this](const std::string& s) { escrever(s); };
        std::string erro;
        if (!tetra_.iniciar(a.tetraInverter, erro)) escrever("[TETRA] " + erro + "\n");
        break;
    }
    case HFDL: {
        hfdl_.aoTexto = [this](const std::string& s) { escrever(s); };
        std::string erro;
        if (!hfdl_.iniciar(a.hfdlCanais, a.hfdlCentroHz, a.hfdlTaxa, erro)) escrever("[HFDL] " + erro + "\n");
        break;
    }
    case AIS: {
        ais_.aoTexto = [this](const std::string& s) { escrever(s); };
        std::string erro;
        if (!ais_.iniciar(erro)) escrever("[AIS] " + erro + "\n");
        break;
    }
    case APRS: {
        aprs_.aoTexto = [this](const std::string& s) { escrever(s); };
        std::string erro;
        if (!aprs_.iniciar(a.aprsBaud, erro)) escrever("[APRS] " + erro + "\n");
        break;
    }
    case ACARS: {
        acars_.aoTexto = [this](const std::string& s) { escrever(s); };
        std::string erro;
        if (!acars_.iniciar(a.acarsCanais, a.acarsCentroHz, a.acarsTaxa, erro)) escrever("[ACARS] " + erro + "\n");
        break;
    }
    case VDL2: {
        vdl2_.aoTexto = [this](const std::string& s) { escrever(s); };
        std::string erro;
        if (!vdl2_.iniciar(a.vdl2Canais, a.vdl2CentroHz, a.vdl2Taxa, erro)) escrever("[VDL2] " + erro + "\n");
        break;
    }
    case DRM: {
        drm_.aoTexto = [this](const std::string& s) { escrever(s); };
        drm_.inverter = a.drmInverter;
        std::string erro;
        if (!drm_.iniciar(erro)) escrever("[DRM] " + erro + "\n");
        break;
    }
    case SSTV:
        sstv_.parar();      // Reiniciar: a imagem pela metade (1/4 ou mais) ainda e salva
        colherSstv();
        sstv_.limpar();
        escrever("[SSTV] esperando o VIS (cabecalho) - Robot, Martin, Scottie, SC2 e PD sao reconhecidos sozinhos\n");
        break;
    case WEFAX:
        wefax_.parar();     // Reiniciar: a imagem pela metade ainda e salva
        colherWefax();
        wefax_.limpar();
        escrever("[WEFAX] esperando o tom de inicio (300 Hz) - a fase acerta a margem e as linhas por minuto sozinha\n");
        break;
    case ANALISE:
        analise_ = std::make_unique<AnaliseCore>(kTaxa);
        escrever("[ANALISE] juntando 12 s de audio do sinal sintonizado...\n");
        break;
    default:
        break;
    }
    tipo_ = t;
    // recomeca a reducao de taxa
    std::lock_guard<std::mutex> lf(filaMutex_);
    fila_.clear(); filaAmostras_ = 0;
}

void Decoders::parar()
{
    dsd_.parar();
    tetra_.parar();
    hfdl_.parar(); ais_.parar(); aprs_.parar(); acars_.parar(); vdl2_.parar(); drm_.parar();
    if (tipo_.load() == SSTV) { sstv_.parar(); colherSstv(); }   // a imagem pela metade fica salva como esta
    if (tipo_.load() == WEFAX) { wefax_.parar(); colherWefax(); }
    std::lock_guard<std::mutex> lk(coreMutex_);
    tipo_ = NENHUM;
    cw_.reset(); rtty_.reset(); sitor_.reset(); dsc_.reset(); ale_.reset(); analise_.reset(); pactor_.reset();
}

void Decoders::empurrar(const int16_t* pcm, size_t n, uint32_t sps)
{
    const Tipo tp = tipo_.load();
    if (tp == NENHUM || tp == TETRA || tp == HFDL || tp == AIS || tp == ACARS || tp == VDL2 || tp == DRM || !pcm || !n || !sps) return;
    {
        std::lock_guard<std::mutex> lk(filaMutex_);
        if (sps != filaSps_) { fila_.clear(); filaAmostras_ = 0; filaSps_ = sps; }
        fila_.emplace_back(pcm, pcm + n);
        filaAmostras_ += n;
        while (filaAmostras_ > kFilaMax && !fila_.empty()) {
            filaAmostras_ -= fila_.front().size();
            fila_.pop_front();
        }
    }
    filaCv_.notify_one();
}

// Filtro passa-baixas (sinc com janela de Blackman) para descer de 48 kHz a
// 8 kHz sem trazer para dentro o que esta acima de 4 kHz.
static std::vector<float> projetarFir(double fs, double corte, int n)
{
    std::vector<float> h(n);
    const double fc = corte / fs, pi = 3.14159265358979;
    double soma = 0;
    for (int i = 0; i < n; ++i) {
        const double m = i - (n - 1) / 2.0;
        const double s = m == 0 ? 2 * fc : std::sin(2 * pi * fc * m) / (pi * m);
        const double w = 0.42 - 0.5 * std::cos(2 * pi * i / (n - 1)) + 0.08 * std::cos(4 * pi * i / (n - 1));
        h[i] = (float)(s * w);
        soma += h[i];
    }
    for (auto& v : h) v = (float)(v / soma);
    return h;
}

void Decoders::laco()
{
    std::vector<std::vector<int16_t>> pegos;
    for (;;) {
        uint32_t sps;
        {
            std::unique_lock<std::mutex> lk(filaMutex_);
            filaCv_.wait(lk, [this] { return sair_ || !fila_.empty(); });
            if (sair_) return;
            pegos.assign(std::make_move_iterator(fila_.begin()), std::make_move_iterator(fila_.end()));
            fila_.clear(); filaAmostras_ = 0;
            sps = filaSps_;
        }
        if (sps != spsAtual_) {
            spsAtual_ = sps;
            fator_ = (sps % 8000 == 0) ? (int)(sps / 8000) : 0;
            // O NFM/SSB sai em 51,2 kHz com o dongle em 1,024 Msps (nao 48k):
            // tambem ai o passa-baixas vem antes de reduzir para 8 kHz.
            fir_ = fator_ != 1 ? projetarFir(sps, 3600.0, fator_ > 1 ? 16 * fator_ + 1 : 97) : std::vector<float>();
            dmrPos_ = 0; dmrTemUlt_ = false;
            hist_.assign(fir_.size() * 2, 0.f); histPos_ = 0;
            fase_ = 0; posLin_ = 0; temUlt_ = false;
        }
        if (tipo_.load() == SSTV) {
            // o SstvCore reduz sozinho para ~12 kS/s (precisa de 1100-2300 Hz
            // com folga; os 8 kHz dos outros nucleos ficariam no limite)
            for (const auto& b : pegos) sstv_.alimentar(b.data(), b.size(), sps);
            pegos.clear();
            colherSstv();
            continue;
        }
        if (tipo_.load() == WEFAX) {
            for (const auto& b : pegos) wefax_.alimentar(b.data(), b.size(), sps);
            pegos.clear();
            colherWefax();
            continue;
        }
        const bool ehDmr = tipo_.load() == DMR;
        if (ehDmr || tipo_.load() == APRS) {   // dsd-fme e direwolf querem 48 kHz EXATOS
            // Tira o "DC" do discriminador: um erro de sintonia/PPM de poucos kHz
            // desloca os quatro niveis do 4FSK e o dsd-fme nao sincroniza.
            // Medido na Rodovia (395,775 MHz): 2,7 kHz fora -> nenhuma linha;
            // com o DC tirado -> Color Code 03, IDLE/DATA/R12U decodificados.
            auto semDc = [this](std::vector<int16_t>& v) {
                const float a = 1.f / (48000.f * 0.2f);
                for (auto& s : v) {
                    dmrDc_ += a * ((float)s - dmrDc_);
                    s = (int16_t)std::clamp((float)s - dmrDc_, -32767.f, 32767.f);
                }
            };
            for (const auto& b : pegos) {
                if (sps == 48000) {
                    std::vector<int16_t> c(b);
                    if (ehDmr) { semDc(c); dsd_.escrever(c.data(), c.size()); } else aprs_.escrever48k(c.data(), c.size());
                    continue;
                }
                // Reamostra (o demodulador entrega 51,2 kHz): sem isso o dsd-fme
                // le os simbolos 6,7% rapidos demais e nao sincroniza.
                const double passo = double(sps) / 48000.0;
                std::vector<float> ent;
                ent.reserve(b.size() + 1);
                if (dmrTemUlt_) ent.push_back(dmrUlt_);
                for (int16_t v : b) ent.push_back((float)v);
                std::vector<int16_t> out;
                out.reserve((size_t)(b.size() / passo) + 2);
                double pos = dmrPos_;
                while (pos + 1.0 < double(ent.size())) {
                    const size_t i0 = (size_t)pos;
                    const float fr = (float)(pos - i0);
                    const float v = ent[i0] + (ent[i0 + 1] - ent[i0]) * fr;
                    out.push_back((int16_t)std::clamp(v, -32768.f, 32767.f));
                    pos += passo;
                }
                if (!ent.empty()) { dmrUlt_ = ent.back(); dmrTemUlt_ = true; dmrPos_ = std::max(0.0, pos - double(ent.size() - 1)); }
                if (ehDmr) { semDc(out); if (!out.empty()) dsd_.escrever(out.data(), out.size()); }
                else if (!out.empty()) aprs_.escrever48k(out.data(), out.size());
            }
            pegos.clear();
            continue;
        }
        for (const auto& b : pegos) {
            bloco8k_.clear();
            if (fator_ == 1) {
                for (int16_t v : b) bloco8k_.push_back(v / 32768.f);
            } else if (fator_ > 1) {
                // FIR + decimacao inteira (48k -> 8k: fator 6)
                // historico circular duplicado: a janela e sempre contigua
                const size_t L = fir_.size();
                for (int16_t v : b) {
                    const float x = v / 32768.f;
                    hist_[histPos_] = x; hist_[histPos_ + L] = x;
                    if (++histPos_ >= L) histPos_ = 0;
                    if (++fase_ >= (size_t)fator_) {
                        fase_ = 0;
                        const float* h = hist_.data() + histPos_;
                        float acc = 0;
                        for (size_t k = 0; k < L; ++k) acc += h[k] * fir_[k];
                        bloco8k_.push_back(acc);
                    }
                }
            } else {
                // taxa nao inteira (51,2 kHz): passa-baixas e depois interpolacao linear
                const double passo = double(sps) / kTaxa;
                const size_t L = fir_.size();
                std::vector<float> ent;
                ent.reserve(b.size() + 1);
                if (temUlt_) ent.push_back(ultLin_);
                for (int16_t v : b) {
                    const float x = v / 32768.f;
                    hist_[histPos_] = x; hist_[histPos_ + L] = x;
                    if (++histPos_ >= L) histPos_ = 0;
                    const float* h = hist_.data() + histPos_;
                    float acc = 0;
                    for (size_t k = 0; k < L; ++k) acc += h[k] * fir_[k];
                    ent.push_back(acc);
                }
                double pos = posLin_;
                while (pos + 1.0 < double(ent.size())) {
                    const size_t i0 = (size_t)pos;
                    const float fr = (float)(pos - i0);
                    bloco8k_.push_back(ent[i0] + (ent[i0 + 1] - ent[i0]) * fr);
                    pos += passo;
                }
                if (!ent.empty()) { ultLin_ = ent.back(); temUlt_ = true; posLin_ = std::max(0.0, pos - double(ent.size() - 1)); }
            }
            if (!bloco8k_.empty()) alimentar(bloco8k_.data(), bloco8k_.size());
        }
        pegos.clear();
    }
}

void Decoders::alimentar(const float* x, size_t n)
{
    std::string saida;
    {
        std::lock_guard<std::mutex> lk(coreMutex_);
        if (cw_) saida = cw_->feed(x, n);
        else if (rtty_) {
            saida = rtty_->feed(x, n);
            if (rtty_->tomNovo()) {
                char b[80];
                std::snprintf(b, sizeof b, "\n[RTTY] tom central medido: %.0f Hz\n", rtty_->tomMedido());
                saida = b + saida;
            }
        }
        else if (sitor_) saida = sitor_->feed(x, n);
        else if (pactor_) {
            saida = pactor_->feed(x, n);
            pactorRelogio_ += double(n) / kTaxa;
            // texto depois de 1 minuto calado: linha com a hora (e o indicativo)
            if (!saida.empty()) {
                if (pactorRelogio_ - pactorUltimo_ > 60)
                    saida = "\n[PACTOR-I] " + horaUtc() + " UTC" + (pactor_->indicativo().empty() ? "" : "  " + pactor_->indicativo()) + "\n" + saida;
                pactorUltimo_ = pactorRelogio_;
            }
        }
        else if (dsc_) saida = dsc_->feed(x, n);
        else if (ale_) {
            for (const auto& c : ale_->feed(x, n)) saida += "[ALE] " + horaUtc() + " UTC  " + c + "\n";
        }
        else if (analise_ && !analiseFeita_) {
            if (analise_->alimentar(x, n)) {
                analiseFeita_ = true;
                const auto r = analise_->analisar();
                saida += "\n[ANALISE] " + horaUtc() + " UTC\n";
                for (const auto& l : r.linhas) saida += "  " + l + "\n";
                saida += "\n";
            }
        }
    }
    if (!saida.empty()) escrever(saida);
}

// Imagem SSTV terminada: PNG na pasta SSTV ao lado do RXSDR.exe + linha no texto
void Decoders::colherSstv()
{
    ImagemSstv im;
    while (sstv_.pegarTerminada(im.argb, im.w, im.h, im.modo, im.porVis)) {
        SYSTEMTIME st; GetLocalTime(&st);
        const std::string nomeModo = SstvCore::nomeModo(im.modo);
        std::string tag;
        for (char c : nomeModo) if (c != ' ') tag += c;
        const uint64_t hz = sstvVfoHz.load();
        char rot[128];
        std::snprintf(rot, sizeof rot, "%s  %s UTC  %.3f MHz", nomeModo.c_str(), horaUtc().substr(0, 5).c_str(), hz / 1e6);
        im.rotulo = rot;
        std::string linha = std::string("[SSTV] ") + rot + (im.porVis ? "" : "  (sem VIS)");
        if (sstvSalvar.load()) {
            char exe[MAX_PATH]{};
            GetModuleFileNameA(nullptr, exe, MAX_PATH);
            std::string pasta(exe);
            const auto p = pasta.find_last_of("\\/");
            pasta = (p == std::string::npos ? std::string(".") : pasta.substr(0, p)) + "\\SSTV";
            CreateDirectoryA(pasta.c_str(), nullptr);
            char nome[96];
            std::snprintf(nome, sizeof nome, "\\RXSDR_SSTV_%04d%02d%02d_%02d%02d%02d_%s.png", st.wYear, st.wMonth,
                          st.wDay, st.wHour, st.wMinute, st.wSecond, tag.c_str());
            std::vector<uint8_t> rgb(size_t(im.w) * im.h * 3);
            for (size_t i = 0; i < size_t(im.w) * im.h; ++i) {
                const uint32_t v = im.argb[i];
                rgb[3 * i] = uint8_t(v >> 16); rgb[3 * i + 1] = uint8_t(v >> 8); rgb[3 * i + 2] = uint8_t(v);
            }
            std::string png;
            stbi_write_png_to_func([](void* ctx, void* d, int n) { ((std::string*)ctx)->append((const char*)d, size_t(n)); },
                                   &png, im.w, im.h, 3, rgb.data(), im.w * 3);
            const std::string cam = pasta + nome;
            std::ofstream f(cam, std::ios::binary);
            f.write(png.data(), (std::streamsize)png.size());
            if (!png.empty() && f) { im.arquivo = cam; linha += std::string("  salva em SSTV") + nome; }
            else linha += "  (nao consegui salvar em " + pasta + ")";
        }
        escrever(linha + "\n");
        std::lock_guard<std::mutex> lk(sstvMutex_);
        sstvProntas_.push_back(std::move(im));
        while (sstvProntas_.size() > 12) sstvProntas_.pop_front();
        im = ImagemSstv();
    }
}

// Fax terminado: PNG em tons de cinza na pasta WEFAX ao lado do RXSDR.exe
void Decoders::colherWefax()
{
    WefaxCore::Terminada t;
    while (wefax_.pegarTerminada(t)) {
        ImagemWefax im;
        im.cinza = std::move(t.cinza); im.w = t.w; im.h = t.h; im.lpm = t.lpm; im.ioc = t.ioc;
        SYSTEMTIME st; GetLocalTime(&st);
        const uint64_t hz = sstvVfoHz.load();
        char rot[128];
        std::snprintf(rot, sizeof rot, "%s UTC  %.1f kHz  %d LPM / IOC %d  %dx%d", horaUtc().substr(0, 5).c_str(), hz / 1e3,
                      im.lpm, im.ioc, im.w, im.h);
        im.rotulo = rot;
        std::string linha = std::string("[WEFAX] ") + rot;
        if (wefaxSalvar.load()) {
            char exe[MAX_PATH]{};
            GetModuleFileNameA(nullptr, exe, MAX_PATH);
            std::string pasta(exe);
            const auto p = pasta.find_last_of("\\/");
            pasta = (p == std::string::npos ? std::string(".") : pasta.substr(0, p)) + "\\WEFAX";
            CreateDirectoryA(pasta.c_str(), nullptr);
            char nome[96];
            std::snprintf(nome, sizeof nome, "\\RXSDR_WEFAX_%04d%02d%02d_%02d%02d%02d_%.0fkHz.png", st.wYear, st.wMonth,
                          st.wDay, st.wHour, st.wMinute, st.wSecond, hz / 1e3);
            std::string png;
            stbi_write_png_to_func([](void* ctx, void* d, int n) { ((std::string*)ctx)->append((const char*)d, size_t(n)); },
                                   &png, im.w, im.h, 1, im.cinza.data(), im.w);
            const std::string cam = pasta + nome;
            std::ofstream f(cam, std::ios::binary);
            f.write(png.data(), (std::streamsize)png.size());
            if (!png.empty() && f) { im.arquivo = cam; linha += std::string("  salva em WEFAX") + nome; }
            else linha += "  (nao consegui salvar em " + pasta + ")";
        }
        escrever(linha + "\n");
        std::lock_guard<std::mutex> lk(sstvMutex_);
        wefaxProntas_.push_back(std::move(im));
        while (wefaxProntas_.size() > 6) wefaxProntas_.pop_front();
    }
}

bool Decoders::pegarImagemWefax(ImagemWefax& im)
{
    std::lock_guard<std::mutex> lk(sstvMutex_);
    if (wefaxProntas_.empty()) return false;
    im = std::move(wefaxProntas_.front());
    wefaxProntas_.pop_front();
    return true;
}

bool Decoders::pegarImagemSstv(ImagemSstv& im)
{
    std::lock_guard<std::mutex> lk(sstvMutex_);
    if (sstvProntas_.empty()) return false;
    im = std::move(sstvProntas_.front());
    sstvProntas_.pop_front();
    return true;
}

void Decoders::escrever(const std::string& s)
{
    std::lock_guard<std::mutex> lk(textoMutex_);
    texto_ += s;
    if (texto_.size() > 200000) texto_.erase(0, texto_.size() - 100000);
}

std::string Decoders::pegarTexto()
{
    std::lock_guard<std::mutex> lk(textoMutex_);
    std::string s;
    s.swap(texto_);
    return s;
}

std::string Decoders::estado()
{
    std::lock_guard<std::mutex> lk(coreMutex_);
    char b[200] = "Parado";
    if (cw_)
        std::snprintf(b, sizeof b, "tom %.0f Hz  |  %.0f palavras/min  |  %d letras",
                      cw_->tomMedido(), cw_->ppm(), cw_->letras());
    else if (rtty_)
        std::snprintf(b, sizeof b, "%s  |  tom %.0f Hz  |  %d caracteres  |  %d erros",
                      rtty_->sincronizado() ? "SINCRONIZADO" : "procurando", rtty_->tomMedido(),
                      rtty_->totalChars(), rtty_->erros());
    else if (pactor_)
        std::snprintf(b, sizeof b, "%s  |  %s  |  tom %.0f Hz  |  %d pacotes (%d somados)  |  %s%s%s",
                      pactor_->travado() ? "RECEBENDO" : "procurando",
                      pactor_->baud() ? (pactor_->baud() == 200 ? "200 baud" : "100 baud") : "- baud",
                      pactor_->tomCentral(), pactor_->pacotes(), pactor_->somados(), pactor_->formato(),
                      pactor_->indicativo().empty() ? "" : "  |  ", pactor_->indicativo().c_str());
    else if (sitor_)
        std::snprintf(b, sizeof b, "%s  |  %d/%d validos  |  %d salvos pela copia RX",
                      sitor_->sincronizado() ? "SINCRONIZADO" : "procurando", sitor_->validChars(),
                      sitor_->totalChars(), sitor_->corrigidos());
    else if (dsc_)
        std::snprintf(b, sizeof b, "%s  |  %d mensagens  |  %d/%d simbolos validos",
                      dsc_->sincronizado() ? "SINCRONIZADO" : "procurando", dsc_->mensagens(),
                      dsc_->validos(), dsc_->simbolos());
    else if (ale_)
        std::snprintf(b, sizeof b, "%s  |  %d palavras  |  %d chamadas  |  desvio %+.0f Hz",
                      ale_->sincronizado() ? "TRAVADO" : "procurando", ale_->palavras(), ale_->chamadas(),
                      ale_->desvioHz());
    else if (tipo_.load() == DMR) {
        const EstadoDmr e = dsd_.estado();
        if (!e.rodando) std::snprintf(b, sizeof b, "%s", e.erro.empty() ? "Parado" : e.erro.c_str());
        else std::snprintf(b, sizeof b, "dsd-fme rodando  |  %s  |  CC %s  |  %d linhas",
                           e.protocolo.empty() ? "sem sincronismo" : e.protocolo.c_str(),
                           e.cc >= 0 ? std::to_string(e.cc).c_str() : "-", e.linhas);
    }
    else if (tipo_.load() == TETRA) {
        const EstadoTetra e = tetra_.estado();
        if (!e.rodando) std::snprintf(b, sizeof b, "%s", e.erro.empty() ? "Parado" : e.erro.c_str());
        else if (e.adquirindo) std::snprintf(b, sizeof b, "procurando a portadora...");
        else std::snprintf(b, sizeof b, "%s  |  AFC %+.0f Hz  |  %d bursts  |  voz %d",
                           e.travado ? "SINCRONIZADO" : "procurando", e.afc, e.bursts, e.voz);
    }
    else if (tipo_.load() == HFDL) {
        const EstadoExterno e = hfdl_.estado();
        if (!e.rodando) std::snprintf(b, sizeof b, "%s", e.erro.empty() ? "dumphfdl parado" : e.erro.c_str());
        else if (hfdl_.foraDaBanda()) std::snprintf(b, sizeof b, "o radio saiu da banda - clique Reiniciar  |  %d mensagens", e.mensagens);
        else std::snprintf(b, sizeof b, "dumphfdl ouvindo  |  %d mensagens", e.mensagens);
    }
    else if (tipo_.load() == ACARS || tipo_.load() == VDL2) {
        const bool ac = tipo_.load() == ACARS;
        const EstadoExterno e = ac ? acars_.estado() : vdl2_.estado();
        const bool fora = ac ? acars_.foraDaBanda() : vdl2_.foraDaBanda();
        const char* prog = ac ? "acarsdec" : "dumpvdl2";
        if (!e.rodando) std::snprintf(b, sizeof b, "%s", e.erro.empty() ? (ac ? "acarsdec parado" : "dumpvdl2 parado") : e.erro.c_str());
        else if (fora) std::snprintf(b, sizeof b, "o radio saiu do centro combinado - clique Reiniciar  |  %d mensagens", e.mensagens);
        else std::snprintf(b, sizeof b, "%s ouvindo  |  %d mensagens", prog, e.mensagens);
    }
    else if (tipo_.load() == AIS) {
        const EstadoExterno e = ais_.estado();
        if (!e.rodando) std::snprintf(b, sizeof b, "%s", e.erro.empty() ? "AIS-catcher parado" : e.erro.c_str());
        else std::snprintf(b, sizeof b, "AIS-catcher ouvindo  |  %d mensagens  |  %s", e.mensagens, e.info.c_str());
    }
    else if (tipo_.load() == APRS) {
        const EstadoExterno e = aprs_.estado();
        if (!e.rodando) std::snprintf(b, sizeof b, "%s", e.erro.empty() ? "direwolf parado" : e.erro.c_str());
        else std::snprintf(b, sizeof b, "direwolf ouvindo  |  %d pacotes  |  %s", e.mensagens, e.info.c_str());
    }
    else if (tipo_.load() == DRM) {
        const EstadoDrm e = drm_.estado();
        static const char kRob[] = "ABCDE";
        static const char* kQam[] = {"4-QAM", "16-QAM", "64-QAM", "64-QAM", "64-QAM"};
        if (!e.rodando) std::snprintf(b, sizeof b, "%s", e.erro.empty() ? "dream parado" : e.erro.c_str());
        else if (!e.comStatus) std::snprintf(b, sizeof b, "abrindo o dream...");
        else if (e.tempo != 0) std::snprintf(b, sizeof b, "procurando o sinal DRM  |  sintonize o centro do canal");
        else if (e.fac != 0) std::snprintf(b, sizeof b, "sinal achado, sincronizando (FAC)  |  SNR %.1f dB", e.snr);
        else std::snprintf(b, sizeof b, "DRM %s  |  SNR %.1f dB  |  modo %c  %.0f kHz  %s  |  %s %.1f kbps%s",
                           e.estacao.empty() ? "(lendo SDC)" : e.estacao.c_str(), e.snr,
                           e.robustez >= 0 && e.robustez < 5 ? kRob[e.robustez] : '?', e.larguraKHz,
                           e.mscQam >= 0 && e.mscQam < 5 ? kQam[e.mscQam] : "?",
                           e.codec.empty() ? "?" : e.codec.c_str(), e.kbps,
                           e.msc == 0 ? "" : "  |  áudio com erros");
    }
    else if (tipo_.load() == SSTV) {
        const SstvCore::Status s = sstv_.status();
        if (s.estado == SstvCore::RECEBENDO)
            std::snprintf(b, sizeof b, "recebendo %s%s  |  linha %d de %d  |  sintonia %+.0f Hz  |  inclinacao %+.0f ppm",
                          SstvCore::nomeModo(s.modo), s.porVis ? " (VIS)" : "", s.linha, s.linhas, s.desvioHz,
                          s.inclinacaoPpm);
        else
            std::snprintf(b, sizeof b, "esperando o VIS  |  sinal %.0f dB  |  %d imagens recebidas", s.nivelDb, s.imagens);
    }
    else if (tipo_.load() == WEFAX) {
        const WefaxCore::Status s = wefax_.status();
        static const char* kEst[] = {"esperando o tom de inicio", "tom de inicio - IOC %d", "fase: medindo a margem e as linhas por minuto",
                                     "recebendo", "pronta"};
        if (s.estado == WefaxCore::RECEBENDO)
            std::snprintf(b, sizeof b, "recebendo  |  %d LPM, IOC %d  |  %d linhas  |  margem %s", s.lpm, s.ioc, s.linhas,
                          s.alinhadoPelaFase ? "pela fase" : "manual");
        else if (s.estado == WefaxCore::INICIO) std::snprintf(b, sizeof b, kEst[1], s.ioc);
        else std::snprintf(b, sizeof b, "%s  |  sinal %.0f dB  |  %d imagens recebidas", kEst[s.estado], s.nivelDb, s.imagens);
    }
    else if (analise_)
        std::snprintf(b, sizeof b, analiseFeita_ ? "analise pronta - clique Reiniciar para medir de novo"
                                                 : "juntando audio: %.0f de %.0f s",
                      analise_->segundosJuntados(), analise_->segundosNecessarios());
    return b;
}

bool Decoders::travado()
{
    std::lock_guard<std::mutex> lk(coreMutex_);
    if (rtty_) return rtty_->sincronizado();
    if (sitor_) return sitor_->sincronizado();
    if (pactor_) return pactor_->travado();
    if (dsc_) return dsc_->sincronizado();
    if (ale_) return ale_->sincronizado();
    if (cw_) return cw_->ppm() > 0;
    if (tipo_.load() == TETRA) return tetra_.estado().travado;
    if (tipo_.load() == SSTV) return sstv_.status().estado == SstvCore::RECEBENDO;
    if (tipo_.load() == WEFAX) return wefax_.status().estado == WefaxCore::RECEBENDO;
    if (tipo_.load() == DRM) { const EstadoDrm e = drm_.estado(); return e.tempo == 0 && e.fac == 0; }
    if (tipo_.load() == DMR) {
        const EstadoDmr e = dsd_.estado();
        using namespace std::chrono;
        const double t = duration<double>(steady_clock::now().time_since_epoch()).count();
        return t - std::max(e.ts[0].ultAtivo, e.ts[1].ultAtivo) < 2.0;
    }
    return false;
}

float Decoders::progressoAnalise()
{
    std::lock_guard<std::mutex> lk(coreMutex_);
    if (!analise_) return 0;
    if (analiseFeita_) return 1;
    return (float)std::min(1.0, analise_->segundosJuntados() / analise_->segundosNecessarios());
}

} // namespace masdr
