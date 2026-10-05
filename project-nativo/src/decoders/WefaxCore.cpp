#include "WefaxCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace masdr {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kAlvo = 12000.0;        // taxa de trabalho (aproximada)
constexpr int kMaxLinhas = 5000;         // ~40 min a 120 LPM: mais que isso, fecha a imagem
const int kLpms[] = {120, 60, 90, 100, 180, 240};

std::vector<float> passaBaixas(double fc, double fs, int L)
{
    std::vector<float> h((size_t)L);
    double soma = 0;
    const double f = fc / fs;
    for (int i = 0; i < L; ++i) {
        const double t = i - (L - 1) / 2.0;
        const double s = t == 0 ? 2 * f : std::sin(2 * kPi * f * t) / (kPi * t);
        const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (L - 1)) + 0.08 * std::cos(4 * kPi * i / (L - 1));
        h[size_t(i)] = float(s * w);
        soma += s * w;
    }
    for (auto& v : h) v = float(v / soma);
    return h;
}

// fracao da variancia do bloco que esta na frequencia f (0..1)
double fracaoTom(const std::vector<float>& x, double f, double fs, double media, double var)
{
    if (var <= 1e-9 || x.size() < 16) return 0;
    const double w = 2 * kPi * f / fs, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (float v : x) { const double s0 = (v - media) + c * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;          // |X|^2
    const double n = double(x.size());
    return std::min(1.0, p / (n * n * var / 2.0));
}
} // namespace

WefaxCore::WefaxCore() {}

void WefaxCore::reconfigurar(uint32_t sps)
{
    if (estado_ == RECEBENDO) terminar(0);
    sps_ = sps;
    D_ = std::max(1, (int)std::lround(sps / kAlvo));
    fs_ = double(sps) / D_;
    faseD_ = 0; pos0_ = 0;
    if (D_ > 1) {
        int L = (int)std::ceil(5.5 * sps / std::max(2000.0, fs_ - 7200.0)) | 1;
        L = std::clamp(L, 15, 301);
        h0_ = passaBaixas(3400.0, sps, L);
        hist0_.assign(size_t(2 * L), 0.f);
    } else {
        h0_.clear(); hist0_.clear();
    }
    // complexo em volta de 1900 Hz: 1500..2300 mais as bandas laterais dos pixels
    int L1 = (int)std::ceil(5.5 * fs_ / 600.0) | 1;
    h1_ = passaBaixas(1250.0, fs_, std::clamp(L1, 31, 255));
    hist1_.assign(h1_.size() * 2, {0, 0});
    pos1_ = 0; faseNco_ = 0; zAnt_ = {0, 0};
    buf_.clear(); base_ = total_;
    blocoV_.clear();
    seg300_ = seg675_ = seg450_ = 0;
    if (estado_ == INICIO || estado_ == FASE) estado_ = ESPERANDO;
}

void WefaxCore::alimentar(const int16_t* pcm, size_t n, uint32_t sps)
{
    if (!pcm || !n || sps < 8000) return;
    std::lock_guard<std::mutex> lk(m_);
    if (sps != sps_) reconfigurar(sps);
    const double passo = 2 * kPi * 1900.0 / fs_;
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
        const std::complex<float> mix((float)std::cos(faseNco_), (float)-std::sin(faseNco_));
        faseNco_ += passo;
        if (faseNco_ > 2 * kPi) faseNco_ -= 2 * kPi;
        hist1_[pos1_] = hist1_[pos1_ + L1] = x * mix;
        pos1_ = (pos1_ + 1) % L1;
        const std::complex<float>* zz = &hist1_[pos1_];
        float re = 0, im = 0;
        for (size_t k = 0; k < L1; ++k) { re += h1_[k] * zz[k].real(); im += h1_[k] * zz[k].imag(); }
        const std::complex<float> z(re, im);
        const std::complex<float> d = z * std::conj(zAnt_);
        zAnt_ = z;
        processar(d, std::norm(z));
    }
}

void WefaxCore::processar(std::complex<float> d, float p)
{
    buf_.push_back(d);
    ++total_;
    nivel_ += (double(p) - nivel_) * 0.0005;
    // brilho instantaneo para os tons
    const double f = 1900.0 + std::atan2(d.imag(), d.real()) * fs_ / (2 * kPi);
    blocoV_.push_back(float(std::clamp((f - 1500.0) / 800.0, -0.25, 1.25)));
    if (blocoV_.size() >= size_t(fs_ * 0.25)) { blocoTons(); blocoV_.clear(); }

    if (estado_ == RECEBENDO) avancarLinhas();

    // guarda so o necessario
    uint64_t manter;
    if (estado_ == RECEBENDO) manter = (uint64_t)std::max(0.0, inicio_ + proxLinha_ * periodo_ - fs_);
    else if (estado_ == FASE) manter = faseIni_;
    else manter = total_ > uint64_t(fs_ * 2) ? total_ - uint64_t(fs_ * 2) : 0;
    if (manter > base_ + uint64_t(fs_ * 4)) {
        const size_t corta = size_t(std::min<uint64_t>(manter - base_, buf_.size()));
        buf_.erase(buf_.begin(), buf_.begin() + long(corta));
        base_ += corta;
    }
}

// ---------------------------------------------------------------------------
//  Tons de inicio (300 / 675 Hz) e de fim (450 Hz), em blocos de 0,25 s
// ---------------------------------------------------------------------------
void WefaxCore::blocoTons()
{
    double media = 0;
    for (float v : blocoV_) media += v;
    media /= double(blocoV_.size());
    double var = 0;
    for (float v : blocoV_) var += (v - media) * (v - media);
    var /= double(blocoV_.size());
    // precisa de contraste (preto e branco de verdade), nao de chiado
    const bool forte = var > 0.03;
    const double r300 = forte ? fracaoTom(blocoV_, 300, fs_, media, var) : 0;
    const double r675 = forte ? fracaoTom(blocoV_, 675, fs_, media, var) : 0;
    const double r450 = forte ? fracaoTom(blocoV_, 450, fs_, media, var) : 0;
    // dentro de uma imagem um hachurado podia imitar o tom: ali o criterio e mais duro
    const bool dentro = estado_ == RECEBENDO;
    const double lim = dentro ? 0.55 : 0.40;
    seg300_ = r300 > lim ? seg300_ + 1 : 0;
    seg675_ = r675 > lim ? seg675_ + 1 : 0;
    seg450_ = r450 > 0.35 ? seg450_ + 1 : 0;
    if (!automatico) return;
    const int precisa = dentro ? 8 : 6;
    if ((seg300_ >= precisa || seg675_ >= precisa) && estado_ != INICIO && estado_ != FASE) {
        if (estado_ == RECEBENDO) terminar(int(std::ceil((precisa * 0.25 + 0.5) * lpm_ / 60.0)));
        iocInicio_ = seg300_ >= precisa ? 576 : 288;
        estado_ = INICIO;
        faseIni_ = total_;
        ++versao_;
        return;
    }
    if (estado_ == INICIO) {
        // o tom de inicio acabou: comeca a fase
        if (seg300_ == 0 && seg675_ == 0) { estado_ = FASE; faseIni_ = total_; }
        else if (double(total_ - faseIni_) > fs_ * 15) { estado_ = FASE; faseIni_ = total_; }
        return;
    }
    if (estado_ == FASE) {
        const double seg = double(total_ - faseIni_) / fs_;
        if (seg >= 8.0 && std::fmod(seg, 2.0) < 0.26) analisarFase(seg >= 26.0);
        return;
    }
    if (estado_ == RECEBENDO && seg450_ >= 8) {
        // tom de fim: as linhas dele (e um pouco antes) saem da imagem
        terminar(int(std::ceil((seg450_ * 0.25 + 0.25) * lpm_ / 60.0)));
        seg450_ = 0;
    }
}

// Dobra o sinal da fase no periodo de cada LPM: o certo mostra o pulso nitido
void WefaxCore::analisarFase(bool ultimaChance)
{
    const size_t ini = size_t(faseIni_ - base_);
    if (ini >= buf_.size()) return;
    const size_t n = buf_.size() - ini;
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& d = buf_[ini + i];
        const double f = 1900.0 + std::atan2(d.imag(), d.real()) * fs_ / (2 * kPi);
        v[i] = float(std::clamp((f - 1500.0) / 800.0, 0.0, 1.0));
    }
    constexpr int B = 200, J = 10;            // 200 fatias por linha; pulso = 5% = 10 fatias
    double melhor = -1, melhorPos = 0; int melhorLpm = lpmCfg_;
    for (int lpm : kLpms) {
        const double P = fs_ * 60.0 / lpm;
        if (double(n) < 4 * P) continue;
        std::vector<double> s(B, 0.0), c(B, 0.0);
        const size_t usar = size_t(std::floor(double(n) / P) * P);
        for (size_t i = 0; i < usar; ++i) {
            const int b = std::min(B - 1, int(std::fmod(double(i), P) / P * B));
            s[size_t(b)] += v[i]; c[size_t(b)] += 1;
        }
        double tot = 0;
        for (int b = 0; b < B; ++b) { s[size_t(b)] = c[size_t(b)] ? s[size_t(b)] / c[size_t(b)] : 0; tot += s[size_t(b)]; }
        for (int b = 0; b < B; ++b) {
            double w = 0;
            for (int j = 0; j < J; ++j) w += s[size_t((b + j) % B)];
            const double mw = w / J, mr = (tot - w) / (B - J);
            const double ctr = std::fabs(mw - mr);
            if (ctr > melhor) { melhor = ctr; melhorPos = b + J / 2.0; melhorLpm = lpm; }
        }
    }
    if (melhor < 0) return;
    if (melhor >= 0.35 || (ultimaChance && melhor >= 0.15)) {
        const double P = fs_ * 60.0 / melhorLpm;
        iniciarRecepcao(double(faseIni_) + melhorPos / B * P, melhorLpm, iocInicio_, true);
    } else if (ultimaChance) {
        iniciarRecepcao(double(total_), lpmCfg_, iocInicio_, false);
    }
}

void WefaxCore::iniciarRecepcao(double inicioAbs, int lpm, int ioc, bool pelaFase)
{
    lpm_ = lpm; ioc_ = ioc;
    W_ = (int)std::lround(ioc * kPi);
    periodo_ = fs_ * 60.0 / lpm;
    inicio_ = inicioAbs;
    proxLinha_ = 0;
    linhas_.clear(); H_ = 0;
    desloc_ = 0;
    pulandoFase_ = pelaFase;
    pulados_ = 0;
    pelaFase_ = pelaFase;
    seg450_ = 0;
    estado_ = RECEBENDO;
    ++versao_;
}

double WefaxCore::mediaV(double a, double b) const
{
    double la = a - double(base_), lb = b - double(base_);
    if (la < 0) la = 0;
    const double nmax = double(buf_.size());
    if (lb > nmax) lb = nmax;
    if (lb - la < 1.0) { const double c = 0.5 * (la + lb); la = std::max(0.0, c - 0.5); lb = std::min(nmax, c + 0.5); }
    if (lb <= la) return 0;
    std::complex<double> soma(0, 0);
    long i = (long)std::floor(la);
    const long fim = (long)std::ceil(lb);
    for (; i < fim; ++i) {
        const double x0 = std::max(la, double(i)), x1 = std::min(lb, double(i + 1));
        if (x1 > x0) soma += std::complex<double>(buf_[size_t(i)]) * (x1 - x0);
    }
    const double f = 1900.0 + std::atan2(soma.imag(), soma.real()) * fs_ / (2 * kPi);
    return std::clamp((f - 1500.0) / 800.0, 0.0, 1.0);
}

void WefaxCore::avancarLinhas()
{
    while (estado_ == RECEBENDO) {
        const double s = inicio_ + proxLinha_ * periodo_;
        if (double(total_) < s + periodo_ + 2) return;
        std::vector<uint8_t> lin((size_t)W_);
        const bool inv = inverter;
        double soma = 0, soma2 = 0, borda = 0; int nb = 0, nm = 0;
        for (int x = 0; x < W_; ++x) {
            double v = mediaV(s + periodo_ * x / W_, s + periodo_ * (x + 1) / W_);
            if (inv) v = 1.0 - v;
            lin[size_t(x)] = (uint8_t)std::lround(v * 255.0);
            if (x < W_ / 40 || x >= W_ - W_ / 40) { borda += v; ++nb; }
            else if (x >= W_ / 10 && x < W_ - W_ / 10) { soma += v; soma2 += v * v; ++nm; }
        }
        ++proxLinha_;
        if (pulandoFase_) {
            // linha de fase (pulso nas pontas, o resto liso): nao entra na imagem
            const double mm = soma / nm, dp = std::sqrt(std::max(0.0, soma2 / nm - mm * mm));
            if (std::fabs(borda / nb - mm) > 0.35 && dp < 0.18 && pulados_ < 80) { ++pulados_; continue; }
            pulandoFase_ = false;
        }
        linhas_.insert(linhas_.end(), lin.begin(), lin.end());
        ++H_;
        ++versao_;
        if (H_ >= kMaxLinhas) terminar(0);
    }
}

void WefaxCore::terminar(int cortar)
{
    if (estado_ != RECEBENDO) return;
    cortar = std::clamp(cortar, 0, H_);
    H_ -= cortar;
    linhas_.resize(size_t(H_) * size_t(W_));
    estado_ = PRONTA;
    if (autoInclinacao && H_ >= 150) endireitarSemTrava();   // so muda se a melhora for clara
    if (H_ >= 50) {
        ++imagens_;
        Terminada t;
        t.cinza = montar(t.w, t.h);
        t.lpm = lpm_; t.ioc = ioc_;
        prontas_.push_back(std::move(t));
        while (prontas_.size() > 4) prontas_.pop_front();
    }
    ++versao_;
}

std::vector<uint8_t> WefaxCore::montar(int& w, int& h) const
{
    w = W_; h = H_;
    std::vector<uint8_t> out(size_t(w) * size_t(h));
    for (int k = 0; k < h; ++k) {
        long sh = std::lround(desloc_ + k * incl_) % w;
        if (sh < 0) sh += w;
        const uint8_t* src = &linhas_[size_t(k) * size_t(w)];
        uint8_t* dst = &out[size_t(k) * size_t(w)];
        std::memcpy(dst, src + sh, size_t(w - sh));
        std::memcpy(dst + (w - sh), src, size_t(sh));
    }
    return out;
}

// ---------------------------------------------------------------------------
void WefaxCore::configurar(int lpm, int ioc)
{
    std::lock_guard<std::mutex> lk(m_);
    lpmCfg_ = lpm > 0 ? lpm : 120;
    iocCfg_ = ioc == 288 ? 288 : 576;
}

void WefaxCore::comecarAgora()
{
    std::lock_guard<std::mutex> lk(m_);
    if (estado_ == RECEBENDO) terminar(0);
    iniciarRecepcao(double(total_), lpmCfg_, iocCfg_, false);
}

void WefaxCore::parar()
{
    std::lock_guard<std::mutex> lk(m_);
    if (estado_ == RECEBENDO) terminar(0);
    else if (estado_ == INICIO || estado_ == FASE) { estado_ = ESPERANDO; ++versao_; }
}

void WefaxCore::limpar()
{
    std::lock_guard<std::mutex> lk(m_);
    if (estado_ != RECEBENDO) estado_ = ESPERANDO;
    linhas_.clear(); H_ = 0;
    desloc_ = 0;
    ++versao_;
}

void WefaxCore::definirInclinacao(double pxPorLinha)
{
    std::lock_guard<std::mutex> lk(m_);
    incl_ = std::clamp(pxPorLinha, -20.0, 20.0);
    ++versao_;
}

void WefaxCore::alinharEm(int coluna)
{
    std::lock_guard<std::mutex> lk(m_);
    if (W_ <= 0) return;
    desloc_ = std::fmod(desloc_ + coluna, double(W_));
    ++versao_;
}

// Inclinacao pela "nitidez" das colunas: com a inclinacao certa as linhas
// verticais do mapa (bordas, meridianos) se empilham e a media de cada coluna
// fica mais contrastada (variancia maior).
bool WefaxCore::endireitar()
{
    std::lock_guard<std::mutex> lk(m_);
    return endireitarSemTrava();
}

bool WefaxCore::endireitarSemTrava()
{
    if (H_ < 80 || W_ <= 0) return false;
    const int R = 4;                               // colunas de 4 em 4
    const int w4 = W_ / R;
    const int passoL = std::max(1, H_ / 800);
    std::vector<int> ks;
    for (int k = 0; k < H_; k += passoL) ks.push_back(k);
    // cada linha reduzida a w4 colunas (media de 4)
    std::vector<float> red(ks.size() * size_t(w4));
    for (size_t i = 0; i < ks.size(); ++i) {
        const uint8_t* src = &linhas_[size_t(ks[i]) * size_t(W_)];
        for (int c = 0; c < w4; ++c) {
            int s = 0;
            for (int j = 0; j < R; ++j) s += src[c * R + j];
            red[i * size_t(w4) + size_t(c)] = s / float(R);
        }
    }
    auto nota = [&](double incl) {
        std::vector<double> col(size_t(w4), 0.0);
        for (size_t i = 0; i < ks.size(); ++i) {
            long sh = std::lround((desloc_ + ks[i] * incl) / R) % w4;
            if (sh < 0) sh += w4;
            const float* r = &red[i * size_t(w4)];
            for (int c = 0; c < w4; ++c) col[size_t(c)] += r[(c + sh) % w4];
        }
        double m = 0, v = 0;
        for (double x : col) m += x;
        m /= w4;
        for (double x : col) v += (x - m) * (x - m);
        return v;
    };
    const double atual = nota(incl_);
    double melhor = atual, bi = incl_;
    for (double s = incl_ - 2.0; s <= incl_ + 2.0001; s += 0.05) { const double n = nota(s); if (n > melhor) { melhor = n; bi = s; } }
    const double c0 = bi;
    for (double s = c0 - 0.05; s <= c0 + 0.05001; s += 0.005) { const double n = nota(s); if (n > melhor) { melhor = n; bi = s; } }
    if (melhor > atual * 1.15) { incl_ = bi; ++versao_; return true; }
    return false;
}

WefaxCore::Status WefaxCore::status()
{
    std::lock_guard<std::mutex> lk(m_);
    Status s;
    s.estado = estado_;
    s.lpm = estado_ == RECEBENDO || estado_ == PRONTA ? lpm_ : lpmCfg_;
    s.ioc = estado_ == RECEBENDO || estado_ == PRONTA ? ioc_ : (estado_ == INICIO || estado_ == FASE ? iocInicio_ : iocCfg_);
    s.largura = (int)std::lround(s.ioc * kPi);
    s.linhas = H_;
    s.nivelDb = 10.0 * std::log10(nivel_ + 1e-12);
    s.inclinacao = incl_;
    s.deslocamento = (int)std::lround(desloc_);
    s.alinhadoPelaFase = pelaFase_;
    s.imagens = imagens_;
    return s;
}

bool WefaxCore::imagemMeia(std::vector<uint32_t>& argb, int& w, int& h, uint64_t& versao)
{
    std::lock_guard<std::mutex> lk(m_);
    if (versao == versao_) return false;
    versao = versao_;
    if (H_ < 2) { argb.clear(); w = h = 0; return true; }
    int W, H;
    const std::vector<uint8_t> g = montar(W, H);
    w = W / 2; h = H / 2;
    argb.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y) {
        const uint8_t* a = &g[size_t(2 * y) * size_t(W)];
        const uint8_t* b = a + W;
        for (int x = 0; x < w; ++x) {
            const uint32_t v = (uint32_t(a[2 * x]) + a[2 * x + 1] + b[2 * x] + b[2 * x + 1] + 2) / 4;
            argb[size_t(y) * size_t(w) + size_t(x)] = 0xFF000000u | (v << 16) | (v << 8) | v;
        }
    }
    return true;
}

bool WefaxCore::pegarTerminada(Terminada& t)
{
    std::lock_guard<std::mutex> lk(m_);
    if (prontas_.empty()) return false;
    t = std::move(prontas_.front());
    prontas_.pop_front();
    return true;
}

std::vector<uint8_t> WefaxCore::imagemFinal(int& w, int& h)
{
    std::lock_guard<std::mutex> lk(m_);
    return montar(w, h);
}

} // namespace masdr
