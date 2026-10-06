#include "SstvCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "AnaliseCore.h"

namespace masdr {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kAlvo = 12000.0;        // taxa de trabalho (aproximada)

double blackman(double x) { return 0.42 - 0.5 * std::cos(2 * kPi * x) + 0.08 * std::cos(4 * kPi * x); }

std::vector<float> passaBaixas(double fc, double fs, int L)
{
    std::vector<float> h(L);
    double soma = 0;
    const double f = fc / fs;
    for (int i = 0; i < L; ++i) {
        const double t = i - (L - 1) / 2.0;
        const double s = t == 0 ? 2 * f : std::sin(2 * kPi * f * t) / (kPi * t);
        const double w = blackman(double(i) / (L - 1));
        h[i] = float(s * w);
        soma += s * w;
    }
    for (auto& v : h) v = float(v / soma);
    return h;
}

float mediana(std::vector<float> v)
{
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
    return v[v.size() / 2];
}

uint8_t byte(double v) { return (uint8_t)std::clamp(std::lround(v), 0L, 255L); }
} // namespace

// ---------------------------------------------------------------------------
//  Os modos. Tempos em ms, das normas (Martin/Scottie: Martin Emmerson e Eddie
//  Murphy; Robot; PD: Paul Turner) - os mesmos usados pelo MMSSTV e pelo slowrx.
//  Canais: 'R','G','B' direto; 'Y' luminancia (no PD a 1a linha do par),
//  'y' a 2a linha do par (PD), 'V' = R-Y, 'U' = B-Y, 'C' = R-Y ou B-Y
//  alternado (Robot 36: o pulso separador diz qual: 1500 Hz R-Y, 2300 B-Y).
// ---------------------------------------------------------------------------
const std::vector<SstvCore::ModoDef>& SstvCore::modos()
{
    static const std::vector<ModoDef> m = [] {
        std::vector<ModoDef> v;
        v.push_back({"Robot 36", 8, 320, 240, 150.0, 0, 9.0, 0, 1, true, {{12, 88, 'Y'}, {106, 44, 'C'}}});
        v.push_back({"Robot 72", 12, 320, 240, 300.0, 0, 9.0, 0, 1, true, {{12, 138, 'Y'}, {156, 69, 'V'}, {231, 69, 'U'}}});
        v.push_back({"Martin 1", 44, 320, 256, 446.446, 0, 4.862, 0, 1, false,
                     {{5.434, 146.432, 'G'}, {152.438, 146.432, 'B'}, {299.442, 146.432, 'R'}}});
        v.push_back({"Martin 2", 40, 320, 256, 226.798, 0, 4.862, 0, 1, false,
                     {{5.434, 73.216, 'G'}, {79.222, 73.216, 'B'}, {153.010, 73.216, 'R'}}});
        v.push_back({"Scottie 1", 60, 320, 256, 428.22, 279.48, 9.0, 9.0, 1, false,
                     {{1.5, 138.24, 'G'}, {141.24, 138.24, 'B'}, {289.98, 138.24, 'R'}}});
        v.push_back({"Scottie 2", 56, 320, 256, 277.692, 179.128, 9.0, 9.0, 1, false,
                     {{1.5, 88.064, 'G'}, {91.064, 88.064, 'B'}, {189.628, 88.064, 'R'}}});
        v.push_back({"Scottie DX", 76, 320, 256, 1050.3, 694.2, 9.0, 9.0, 1, false,
                     {{1.5, 345.6, 'G'}, {348.6, 345.6, 'B'}, {704.7, 345.6, 'R'}}});
        v.push_back({"SC2-180", 55, 320, 256, 711.0225, 0, 5.5225, 0, 1, false,
                     {{6.0225, 235.0, 'R'}, {241.0225, 235.0, 'G'}, {476.0225, 235.0, 'B'}}});
        auto pd = [&](const char* nome, int vis, int w, int h, double px) {
            const double s = w * px;
            v.push_back({nome, vis, w, h, 22.08 + 4 * s, 0, 20.0, 0, 2, true,
                         {{22.08, s, 'Y'}, {22.08 + s, s, 'V'}, {22.08 + 2 * s, s, 'U'}, {22.08 + 3 * s, s, 'y'}}});
        };
        pd("PD 50", 93, 320, 256, 0.286);
        pd("PD 90", 99, 320, 256, 0.532);
        pd("PD 120", 95, 640, 496, 0.19);
        pd("PD 160", 98, 512, 400, 0.382);
        pd("PD 180", 96, 640, 496, 0.286);
        pd("PD 240", 97, 640, 496, 0.382);
        pd("PD 290", 94, 800, 616, 0.286);
        return v;
    }();
    return m;
}

const char* SstvCore::nomeModo(int m) { return m >= 0 && m < N_MODOS ? modos()[size_t(m)].nome : "?"; }
int SstvCore::larguraModo(int m) { return m >= 0 && m < N_MODOS ? modos()[size_t(m)].w : 0; }
int SstvCore::alturaModo(int m) { return m >= 0 && m < N_MODOS ? modos()[size_t(m)].h : 0; }
int SstvCore::visModo(int m) { return m >= 0 && m < N_MODOS ? modos()[size_t(m)].vis : -1; }
double SstvCore::duracaoModo(int m)
{
    if (m < 0 || m >= N_MODOS) return 0;
    const ModoDef& d = modos()[size_t(m)];
    return d.periodo * (d.h / d.linhasPorPeriodo) / 1000.0;
}

SstvCore::SstvCore() { aneisEsp_.assign(1024, 0.f); }

// ---------------------------------------------------------------------------
void SstvCore::reconfigurar(uint32_t sps)
{
    sps_ = sps;
    D_ = std::max(1, (int)std::lround(sps / kAlvo));
    fs_ = double(sps) / D_;
    faseD_ = 0; pos0_ = 0;
    if (D_ > 1) {
        const double trans = std::max(2000.0, fs_ - 7200.0);
        int L = (int)std::ceil(5.5 * sps / trans) | 1;
        L = std::clamp(L, 15, 301);
        h0_ = passaBaixas(3400.0, sps, L);
        hist0_.assign(size_t(2 * L), 0.f);
    } else {
        h0_.clear(); hist0_.clear();
    }
    // passa-baixas complexo depois da mistura em 1900 Hz: passa de 1100 a
    // 2300 Hz (-800..+400) e corta o resto da faixa de audio
    int L1 = (int)std::ceil(5.5 * fs_ / 700.0) | 1;
    h1_ = passaBaixas(1000.0, fs_, std::clamp(L1, 31, 255));
    hist1_.assign(h1_.size() * 2, {0, 0});
    pos1_ = 0; faseNco_ = 0; zAnt_ = {0, 0};
    // o historico de frequencia recomeca (a taxa mudou)
    fr_.clear(); base_ = total_; hops_.clear(); proxHop_ = total_;
    if (estado_ == RECEBENDO) estado_ = PRONTA;
}

void SstvCore::alimentar(const int16_t* pcm, size_t n, uint32_t sps)
{
    if (!pcm || !n || sps < 8000) return;
    std::lock_guard<std::mutex> lk(m_);
    if (sps != sps_) reconfigurar(sps);
    const double passoNco = 2 * kPi * 1900.0 / fs_;
    const size_t L0 = h0_.size(), L1 = h1_.size();
    for (size_t i = 0; i < n; ++i) {
        float x = pcm[i] / 32768.f;
        if (D_ > 1) {
            hist0_[pos0_] = hist0_[pos0_ + L0] = x;
            pos0_ = (pos0_ + 1) % L0;
            if (++faseD_ < D_) continue;
            faseD_ = 0;
            const float* hh = &hist0_[pos0_];
            float acc = 0;
            for (size_t k = 0; k < L0; ++k) acc += h0_[k] * hh[k];
            x = acc;
        }
        aneisEsp_[posEsp_] = x;
        posEsp_ = (posEsp_ + 1) % aneisEsp_.size();

        const std::complex<float> mix((float)std::cos(faseNco_), (float)-std::sin(faseNco_));
        faseNco_ += passoNco;
        if (faseNco_ > 2 * kPi) faseNco_ -= 2 * kPi;
        hist1_[pos1_] = hist1_[pos1_ + L1] = x * mix;
        pos1_ = (pos1_ + 1) % L1;
        const std::complex<float>* zz = &hist1_[pos1_];
        float re = 0, im = 0;
        for (size_t k = 0; k < L1; ++k) { re += h1_[k] * zz[k].real(); im += h1_[k] * zz[k].imag(); }
        const std::complex<float> z(re, im);
        const std::complex<float> d = z * std::conj(zAnt_);
        zAnt_ = z;
        processarFreq(d, std::norm(z));
    }
}

double SstvCore::paraHz(std::complex<double> s) const
{
    return 1900.0 + std::atan2(s.imag(), s.real()) * fs_ / (2 * kPi);
}

void SstvCore::processarFreq(std::complex<float> d, float p)
{
    fr_.push_back(d);
    ++total_;
    nivel_ += (double(p) - nivel_) * 0.0005;
    const uint64_t H = (uint64_t)std::max(1.0, fs_ * 0.005);
    if (total_ < proxHop_) return;
    proxHop_ = total_ + H;
    // media dos ultimos 10 ms
    const size_t W = (size_t)std::max(2.0, fs_ * 0.010);
    if (fr_.size() >= W) {
        std::complex<double> s(0, 0);
        for (size_t i = fr_.size() - W; i < fr_.size(); ++i) s += std::complex<double>(fr_[i]);
        hops_.push_back(float(paraHz(s)));
        while (hops_.size() > 200) hops_.pop_front();
        detectarVis();
    }
    if (estado_ == RECEBENDO) avancarLinhas();
    // fora de uma imagem, guarda so os ultimos segundos
    if (estado_ != RECEBENDO && fr_.size() > size_t(8 * fs_)) {
        const size_t corta = fr_.size() - size_t(4 * fs_);
        fr_.erase(fr_.begin(), fr_.begin() + long(corta));
        base_ += corta;
    }
}

// ---------------------------------------------------------------------------
//  VIS: 300 ms de 1900 Hz, 10 ms de 1200, 300 ms de 1900, bit de partida
//  (1200, 30 ms), 7 bits do codigo + paridade (30 ms cada: 1100 = 1,
//  1300 = 0, o menos significativo primeiro) e bit de parada (1200, 30 ms).
//  Olhamos em passos de 5 ms; cada bit sao 6 passos e vale a mediana dos 4
//  do meio, o que aguenta o passo nao estar alinhado com o bit.
// ---------------------------------------------------------------------------
void SstvCore::detectarVis()
{
    if (esperaVis_ > 0) { --esperaVis_; return; }
    if (!aceitarVis) return;
    const int n = (int)hops_.size();
    if (n < 120) return;
    auto seg = [&](int ini, int len) {
        std::vector<float> v;
        for (int i = ini + 1; i < ini + len - 1; ++i) v.push_back(hops_[size_t(i)]);
        return mediana(v);
    };
    // cabecalho: os 60 passos (300 ms) antes do bit de partida
    std::vector<float> lid;
    for (int i = n - 120; i < n - 60; ++i) lid.push_back(hops_[size_t(i)]);
    const float med = mediana(lid);
    const float off = med - 1900.f;
    if (std::fabs(off) > 250.f) return;
    int bons = 0;
    for (float v : lid) if (std::fabs(v - med) < 60.f) ++bons;
    if (bons < 45) return;
    // Com ruido tudo e puxado para o meio da faixa (1900 Hz): 1100 vira ~1170,
    // 1200 vira ~1250, 1300 vira ~1340. Por isso o bit e decidido contra os
    // proprios bits de partida e de parada (1200 Hz medidos AQUI), e nao
    // contra valores fixos.
    const float partida = seg(n - 60, 6), parada = seg(n - 6, 6);
    const float ref = 0.5f * (partida + parada);
    if (std::fabs(partida - parada) > 60.f) return;
    if (med - ref < 450.f || med - ref > 800.f) return;
    int codigo = 0, uns = 0;
    for (int b = 0; b < 8; ++b) {
        const float v = seg(n - 54 + b * 6, 6);
        if (std::fabs(v - ref) < 25.f || std::fabs(v - ref) > 260.f) return;
        const int bit = v < ref ? 1 : 0;
        uns += bit;
        if (b < 7) codigo |= bit << b;
    }
    if (uns % 2) return;                      // paridade par
    int modo = -1;
    for (int m = 0; m < N_MODOS; ++m) if (modos()[size_t(m)].vis == codigo) modo = m;
    if (modo < 0) return;
    // se uma imagem estava no meio, ela termina aqui
    if (estado_ == RECEBENDO) terminarImagem();
    desvio_ = off;
    const ModoDef& d = modos()[size_t(modo)];
    const double ms = fs_ / 1000.0;
    iniciarImagem(modo, double(total_) + (d.atrasoInicio + d.syncIni) * ms, true);
    esperaVis_ = 120;
}

void SstvCore::iniciarImagem(int modo, double inicioAbs, bool porVis)
{
    const ModoDef& d = modos()[size_t(modo)];
    modo_ = modo;
    porVis_ = porVis;
    w_ = d.w; h_ = d.h;
    const size_t N = size_t(w_) * size_t(h_);
    Y_.assign(N, 0.f);
    U_.assign(N, d.yuv ? 128.f : 0.f);
    V_.assign(N, d.yuv ? 128.f : 0.f);
    temU_.assign(size_t(h_), 0); temV_.assign(size_t(h_), 0);
    img_.assign(N, 0xFF000000u);
    syncs_.clear();
    ganho_ = 1.0;
    inicio_ = inicioAbs;
    b_ = d.periodo * fs_ / 1000.0;
    a_ = inicio_;
    bDesenhado_ = b_;
    travado_ = porVis;
    proxPeriodo_ = 0;
    faltasSeguidas_ = faltas_ = 0;
    estado_ = RECEBENDO;
    ++versao_;
}

void SstvCore::comecarAgora(int modo)
{
    if (modo < 0 || modo >= N_MODOS) return;
    std::lock_guard<std::mutex> lk(m_);
    if (estado_ == RECEBENDO) terminarImagem();
    desvio_ = 0;
    iniciarImagem(modo, double(total_), false);
}

void SstvCore::parar()
{
    std::lock_guard<std::mutex> lk(m_);
    if (estado_ == RECEBENDO) terminarImagem();
}

void SstvCore::limpar()
{
    std::lock_guard<std::mutex> lk(m_);
    estado_ = ESPERANDO;
    modo_ = -1;
    w_ = h_ = 0;
    img_.clear();
    ++versao_;
}

// ---------------------------------------------------------------------------
//  Linhas
// ---------------------------------------------------------------------------
bool SstvCore::acharSync(double previsto, double janela, double& achado, double& qualidade)
{
    const ModoDef& d = modos()[size_t(modo_)];
    const int L = std::max(2, (int)std::lround(d.syncLen * fs_ / 1000.0));
    const long ini = (long)std::floor(posLocal(previsto - janela));
    const long fim = (long)std::ceil(posLocal(previsto + janela));
    if (ini < 0 || fim + L >= (long)fr_.size()) return false;
    const double lo = 1050.0 + desvio_, hi = 1350.0 + desvio_;
    // frequencia de cada amostra pela soma de ~1 ms em volta (aguenta ruido)
    const long M = std::max(2L, (long)std::lround(fs_ * 0.001));
    const long i0 = std::max(0L, ini - M), i1 = std::min((long)fr_.size(), fim + L + M);
    std::vector<uint8_t> marca(size_t(i1 - i0), 0);
    {
        std::complex<double> acc(0, 0);
        for (long i = i0; i < i1; ++i) {
            acc += std::complex<double>(fr_[size_t(i)]);
            if (i - i0 >= M) acc -= std::complex<double>(fr_[size_t(i - M)]);
            const double f = paraHz(acc);
            const long c = i - M / 2;              // centro da janela
            if (c >= i0) marca[size_t(c - i0)] = (f > lo && f < hi) ? 1 : 0;
        }
    }
    auto ind = [&](long i) { return (i >= i0 && i < i1) ? int(marca[size_t(i - i0)]) : 0; };
    int soma = 0;
    for (long i = ini; i < ini + L; ++i) soma += ind(i);
    int melhor = -1; long pMelhor = ini;
    std::vector<int> pont;
    pont.reserve(size_t(fim - ini + 1));
    for (long s = ini; s <= fim; ++s) {
        pont.push_back(soma);
        if (soma > melhor) { melhor = soma; pMelhor = s; }
        soma += ind(s + L) - ind(s);
    }
    // centro do plato do maximo (o pulso pode ser um pouco mais longo/curto)
    long a = pMelhor, b = pMelhor;
    const int tol = std::max(1, L / 25);
    while (a > ini && pont[size_t(a - 1 - ini)] >= melhor - tol) --a;
    while (b < fim && pont[size_t(b + 1 - ini)] >= melhor - tol) ++b;
    // Plato largo = o 1200 Hz e mais comprido que o pulso: depois do VIS o bit
    // de parada (30 ms de 1200) emenda no primeiro sincronismo. O pulso de
    // verdade e o FIM desse trecho (logo depois vem o patamar de 1500 Hz).
    achado = double(base_) + ((b - a) > L * 3 / 10 ? double(b) : 0.5 * double(a + b));
    qualidade = double(melhor) / L;
    return true;
}

void SstvCore::ajustarReta()
{
    const ModoDef& d = modos()[size_t(modo_)];
    const double Pn = d.periodo * fs_ / 1000.0;
    if (syncs_.empty()) return;
    std::vector<std::pair<int, double>> p = syncs_;
    for (int volta = 0; volta < 2; ++volta) {
        const int kmin = p.front().first, kmax = p.back().first;
        double a, b;
        if (p.size() >= 3 && kmax - kmin >= 3 && autoInclinacao) {
            double sk = 0, ss = 0, skk = 0, sks = 0;
            for (auto& q : p) { sk += q.first; ss += q.second; skk += double(q.first) * q.first; sks += q.first * q.second; }
            const double n = double(p.size());
            b = (n * sks - sk * ss) / (n * skk - sk * sk);
            if (std::fabs(b / Pn - 1.0) > 0.01) b = Pn;         // mais de 1%: nao e inclinacao
            a = (ss - b * sk) / n;
        } else {
            b = Pn;
            double s = 0;
            for (auto& q : p) s += q.second - b * q.first;
            a = s / double(p.size());
        }
        a_ = a; b_ = b;
        // tira os pulsos fora da reta (> 2 ms) e ajusta de novo
        std::vector<std::pair<int, double>> bons;
        for (auto& q : p) if (std::fabs(q.second - (a + b * q.first)) < fs_ * 0.002) bons.push_back(q);
        if (bons.size() == p.size() || bons.size() < 2) break;
        p.swap(bons);
    }
}

double SstvCore::mediaFreq(double a, double b) const
{
    double la = posLocal(a), lb = posLocal(b);
    if (lb - la < 1.0) { const double c = 0.5 * (la + lb); la = c - 0.5; lb = c + 0.5; }
    if (la < 0) la = 0;
    const double nmax = double(fr_.size());
    if (lb > nmax) lb = nmax;
    if (lb <= la) return 0;
    std::complex<double> soma(0, 0);
    long i = (long)std::floor(la);
    const long fim = (long)std::ceil(lb);
    for (; i < fim; ++i) {
        const double x0 = std::max(la, double(i)), x1 = std::min(lb, double(i + 1));
        if (x1 > x0) soma += std::complex<double>(fr_[size_t(i)]) * (x1 - x0);
    }
    return paraHz(soma);
}

void SstvCore::desenharPeriodo(int k)
{
    const ModoDef& d = modos()[size_t(modo_)];
    const double ms = fs_ / 1000.0;
    const double r = b_ / (d.periodo * ms);                  // escala do relogio do transmissor
    const double linha = a_ + b_ * k - d.syncIni * ms * r;
    const int l0 = k * d.linhasPorPeriodo;
    if (l0 >= h_) return;
    const double centro = 1900.0 + desvio_;
    auto valor = [&](double f) {
        const double fc = centro + (f - centro) * ganho_ - desvio_;
        return float(std::clamp((fc - 1500.0) / 800.0 * 255.0, 0.0, 255.0));
    };
    for (const Canal& c : d.canais) {
        const double ini = linha + c.ini * ms * r, dur = c.dur * ms * r;
        float* plano = nullptr;
        int lin = l0;
        bool dupla = false;
        char tipo = c.tipo;
        if (tipo == 'C') {
            // Robot 36: o separador antes da cor diz qual e (1500 R-Y, 2300 B-Y)
            const double sep = mediaFreq(linha + 100.5 * ms * r, linha + 104.0 * ms * r) - desvio_;
            tipo = sep < 1900.0 ? 'V' : 'U';
        }
        switch (tipo) {
        case 'R': case 'Y': plano = Y_.data(); break;
        case 'G': case 'U': plano = U_.data(); dupla = (tipo == 'U' && d.linhasPorPeriodo == 2); break;
        case 'B': case 'V': plano = V_.data(); dupla = (tipo == 'V' && d.linhasPorPeriodo == 2); break;
        case 'y': plano = Y_.data(); lin = l0 + 1; break;
        default: continue;
        }
        if (lin >= h_) continue;
        // Nas pontas do canal a janela do pixel nao entra no tom vizinho (o
        // pulso de 1200 Hz ou o separador): o filtro espalha ~0,6 ms e, sem
        // isso, a ultima coluna saia verde (cor "abaixo do preto") no Robot e no PD.
        const double g = std::min(0.6 * ms, dur / 16.0);
        for (int x = 0; x < w_; ++x) {
            double a = ini + dur * x / w_, b = ini + dur * (x + 1) / w_;
            if (b > ini + dur - g) { a -= b - (ini + dur - g); b = ini + dur - g; }
            if (a < ini + g) { b += (ini + g) - a; a = ini + g; }
            const float v = valor(mediaFreq(a, b));
            plano[size_t(lin) * w_ + x] = v;
            if (dupla && lin + 1 < h_) plano[size_t(lin + 1) * w_ + x] = v;
        }
        if (tipo == 'U') { temU_[size_t(lin)] = 1; if (dupla && lin + 1 < h_) temU_[size_t(lin + 1)] = 1; }
        if (tipo == 'V') { temV_[size_t(lin)] = 1; if (dupla && lin + 1 < h_) temV_[size_t(lin + 1)] = 1; }
    }
    // monta as linhas tocadas (no Robot 36 a cor vem de duas linhas)
    const int la = d.linhasPorPeriodo == 2 ? l0 : (l0 & ~1);
    const int lb = std::min(h_ - 1, d.linhasPorPeriodo == 2 ? l0 + 1 : (l0 | 1));
    for (int l = la; l <= lb; ++l) {
        if (l > l0 + d.linhasPorPeriodo - 1 && l > proxPeriodo_ * d.linhasPorPeriodo) continue;
        for (int x = 0; x < w_; ++x) {
            const size_t i = size_t(l) * w_ + x;
            double R, G, B;
            if (d.yuv) {
                const double Y = Y_[i];
                auto pega = [&](const std::vector<float>& P, const std::vector<uint8_t>& tem) -> double {
                    if (tem[size_t(l)]) return P[i];
                    const int o = l ^ 1;
                    if (o < h_ && tem[size_t(o)]) return P[size_t(o) * w_ + x];
                    return 128.0;
                };
                const double Cb = pega(U_, temU_), Cr = pega(V_, temV_);
                R = (100 * Y + 140 * Cr - 17850) / 100.0;
                G = (100 * Y - 71 * Cr - 33 * Cb + 13260) / 100.0;
                B = (100 * Y + 178 * Cb - 22695) / 100.0;
            } else {
                R = Y_[i]; G = U_[i]; B = V_[i];
            }
            img_[i] = 0xFF000000u | (uint32_t(byte(R)) << 16) | (uint32_t(byte(G)) << 8) | byte(B);
        }
    }
}

void SstvCore::redesenharTudo()
{
    for (int k = 0; k < proxPeriodo_; ++k) desenharPeriodo(k);
    bDesenhado_ = b_;
    ++versao_;
}

void SstvCore::avancarLinhas()
{
    const ModoDef& d = modos()[size_t(modo_)];
    const double ms = fs_ / 1000.0;
    const int periodos = d.h / d.linhasPorPeriodo;
    for (;;) {
        const int k = proxPeriodo_;
        if (k >= periodos) { terminarImagem(); return; }
        const double r = b_ / (d.periodo * ms);
        const double previsto = a_ + b_ * k;
        double centro, janela;
        if (!travado_) { centro = previsto + 0.5 * b_; janela = 0.5 * b_; }      // sem VIS: procura no periodo todo
        else if (syncs_.empty()) { centro = previsto; janela = 20.0 * ms; }     // logo depois do VIS
        else {
            // perdeu alguns pulsos seguidos: procura mais longe (sem isso um
            // erro de previsao vira uma imagem inteira perdida)
            const double base = std::max(d.syncLen * 0.5, 4.0);
            const double mais = faltasSeguidas_ >= 3 ? std::min(0.45 * d.periodo, 10.0 * faltasSeguidas_) : 0.0;
            centro = previsto; janela = (base + mais) * ms;
        }
        const double fimLinha = previsto - d.syncIni * ms * r + b_;
        const double precisa = std::max(fimLinha, centro + janela + d.syncLen * ms) + 2 * ms;
        if (double(total_) < precisa) return;

        double achado = 0, q = 0;
        if (acharSync(centro, janela, achado, q) && q >= 0.6) {
            if (!travado_) { travado_ = true; a_ = achado - b_ * k; }
            syncs_.push_back({k, achado});
            ajustarReta();
            // Contraste: o pulso deveria medir 1200 Hz. Com ruido ele sai mais
            // perto de 1900, e os pixels tambem (a imagem fica acinzentada).
            // A razao entre o esperado (700 Hz abaixo do centro) e o medido
            // devolve o contraste. O desvio de sintonia (AFC) vem do VIS, que
            // e medido no centro (1900 Hz) e nao sofre esse puxao.
            if (q > 0.8) {
                const double f = mediaFreq(achado + 0.15 * d.syncLen * ms, achado + 0.85 * d.syncLen * ms);
                const double dist = 1900.0 + desvio_ - f;
                if (dist > 300.0) {
                    const double g = std::clamp(700.0 / dist, 0.95, 1.8);
                    ganho_ += (g - ganho_) * 0.1;
                }
            }
            faltasSeguidas_ = 0;
        } else {
            ++faltas_;
            if (travado_ && ++faltasSeguidas_ > 40) { terminarImagem(); return; }   // o sinal acabou
        }
        desenharPeriodo(k);
        proxPeriodo_ = k + 1;
        ++versao_;
        if (std::fabs(b_ - bDesenhado_) > 30e-6 * b_) redesenharTudo();
    }
}

void SstvCore::terminarImagem()
{
    if (estado_ != RECEBENDO) return;
    ajustarReta();
    redesenharTudo();
    estado_ = PRONTA;
    const ModoDef& d = modos()[size_t(modo_)];
    // so guarda se tiver pelo menos um quarto da imagem
    if (proxPeriodo_ * d.linhasPorPeriodo >= h_ / 4) {
        ++imagens_;
        prontas_.push_back({img_, w_, h_, modo_, porVis_});
        while (prontas_.size() > 4) prontas_.pop_front();
    }
    ++versao_;
}

// ---------------------------------------------------------------------------
SstvCore::Status SstvCore::status()
{
    std::lock_guard<std::mutex> lk(m_);
    Status s;
    s.estado = estado_;
    s.modo = modo_;
    if (modo_ >= 0) {
        const ModoDef& d = modos()[size_t(modo_)];
        s.linha = std::min(h_, proxPeriodo_ * d.linhasPorPeriodo);
        s.linhas = h_;
        s.inclinacaoPpm = (b_ / (d.periodo * fs_ / 1000.0) - 1.0) * 1e6;
    }
    s.desvioHz = desvio_;
    s.contraste = ganho_;
    s.nivelDb = 10.0 * std::log10(nivel_ + 1e-12);
    s.porVis = porVis_;
    s.sincronismos = (int)syncs_.size();
    s.faltas = faltas_;
    s.imagens = imagens_;
    return s;
}

bool SstvCore::imagem(std::vector<uint32_t>& argb, int& w, int& h, uint64_t& versao)
{
    std::lock_guard<std::mutex> lk(m_);
    if (versao == versao_) return false;
    argb = img_; w = w_; h = h_; versao = versao_;
    return true;
}

bool SstvCore::pegarTerminada(std::vector<uint32_t>& argb, int& w, int& h, int& modo, bool& porVis)
{
    std::lock_guard<std::mutex> lk(m_);
    if (prontas_.empty()) return false;
    Terminada& t = prontas_.front();
    argb.swap(t.argb); w = t.w; h = t.h; modo = t.modo; porVis = t.porVis;
    prontas_.pop_front();
    return true;
}

void SstvCore::espectro(std::vector<float>& db, double& hzPorBin)
{
    std::vector<std::complex<double>> a(1024);
    double fs;
    {
        std::lock_guard<std::mutex> lk(m_);
        for (size_t i = 0; i < 1024; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / 1023.0);
            a[i] = std::complex<double>(aneisEsp_[(posEsp_ + i) % 1024] * w, 0.0);
        }
        fs = fs_;
    }
    AnaliseCore::fft(a, false);
    hzPorBin = fs / 1024.0;
    const size_t n = std::min<size_t>(512, size_t(3000.0 / hzPorBin) + 1);
    db.resize(n);
    for (size_t k = 0; k < n; ++k) db[k] = float(10.0 * std::log10(std::norm(a[k]) + 1e-12));
}

} // namespace masdr
