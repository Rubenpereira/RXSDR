#include "ProcAudio.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace masdr {

static constexpr double kPi = 3.14159265358979323846;

// ===========================================================================
//  NR ESPECTRAL - traducao do NrEspectral do index.html
// ===========================================================================
class NrEspectral {
public:
    explicit NrEspectral(double rate) : rate_(rate)
    {
        N = 256; while (N < rate * 0.043) N <<= 1;
        H = N >> 2; K = (N >> 1) + 1;
        win.resize(N);
        for (int i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos(2 * kPi * i / N);
        V = std::max(2, int(std::lround(kJanelaPisoS * rate / H / kU)));
        kLo = std::max(1, int(std::floor(150.0 * N / rate)));
        kHi = std::min(K - 1, int(std::ceil(4000.0 * N / rate)));
        const double inf = std::numeric_limits<double>::infinity();
        for (int u = 0; u < kU; ++u) mins[u].assign(K, inf);
        prepararFft();
        anel.assign(N, 0.0); acc.assign(N, 0.0);
        saidaCap = std::max(N * 4, int(rate * 2));
        saida.assign(saidaCap, 0.f);
        re.assign(N, 0.0); im.assign(N, 0.0);
        P.assign(K, 0.0); sub.assign(K, inf);
        Nr.assign(K, 0.0); tmp.assign(K, 0.0);
        Gp.assign(K, 1.0); gp.assign(K, 1.0); pprev.assign(K, 0.0);
        xi.assign(K, 0.0); gam.assign(K, 0.0); v.assign(K, 0.0);
        glsa.assign(K, 0.0); g.assign(K, 0.0);
        setForca(40);
    }

    // Mexer na forca NAO reinicia o filtro (nao perde o chiado ja aprendido).
    void setForca(int f)
    {
        forca_ = std::max(0, std::min(100, f));
        const double t = forca_ / 100.0;
        profundidade = 10 + 35 * t;
        vies = 1.5 + 2.5 * t;
        sppInf = -15 + 10 * t; sppSup = 0 + 10 * t;
        comp = std::pow(10.0, (1 + 5 * t) / 20);
        Gmin = std::pow(10.0, -profundidade / 20);
    }
    int forca() const { return forca_; }

    void processar(float* x, size_t L)
    {
        for (size_t i = 0; i < L; ++i) {
            anel[pos] = x[i]; pos = (pos + 1) % N;       // janela deslizante em anel
            if (++novos >= H) { novos = 0; quadro(); }
        }
        // O que ja saiu do filtro vai para a saida; no comeco (fila ainda
        // vazia) completa com silencio - so nos primeiros ~85 ms.
        const size_t falta = L - std::min(L, size_t(saidaN));
        for (size_t i = 0; i < falta; ++i) x[i] = 0.f;
        for (size_t i = falta; i < L; ++i) {
            double y = saida[saidaIni] * comp;
            saidaIni = (saidaIni + 1) % saidaCap; --saidaN;
            x[i] = float(y > 1 ? 1 : y < -1 ? -1 : y);
        }
    }

private:
    static constexpr double kJanelaPisoS = 1.0;   // memoria do piso de ruido
    static constexpr int kU = 8;                  // sub-janelas da estatistica minima

    double rate_;
    int N, H, K, V, kLo, kHi;
    int forca_ = 40;
    double profundidade = 45, vies = 4.0, sppInf = -5, sppSup = 10, comp = 1.41, Gmin = 0.0056;

    std::vector<double> win;
    std::vector<int> rev;
    std::vector<double> cs, sn;
    std::vector<double> anel, acc, re, im, P, Ps, sub, Nr, tmp, Gp, gp, pprev, xi, gam, v, glsa, g;
    std::vector<double> mins[kU];
    int uAt = 0, cont = 0, pos = 0, novos = 0;
    std::vector<float> saida; int saidaCap = 0, saidaIni = 0, saidaN = 0;

    void prepararFft()
    {
        int bits = 0; while ((1 << bits) < N) ++bits;
        rev.resize(N);
        for (int i = 0; i < N; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
            rev[i] = r;
        }
        cs.resize(N / 2); sn.resize(N / 2);
        for (int i = 0; i < N / 2; ++i) { cs[i] = std::cos(2 * kPi * i / N); sn[i] = -std::sin(2 * kPi * i / N); }
    }

    void fft(bool inversa)
    {
        for (int i = 0; i < N; ++i) {
            const int j = rev[i];
            if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        const double sg = inversa ? -1 : 1;
        for (int tam = 2; tam <= N; tam <<= 1) {
            const int meio = tam >> 1, passo = N / tam;
            for (int i = 0; i < N; i += tam) {
                for (int j = 0, k = 0; j < meio; ++j, k += passo) {
                    const double wr = cs[k], wi = sg * sn[k];
                    const int a = i + j, b = a + meio;
                    const double xr = re[b] * wr - im[b] * wi, xim = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xim;
                    re[a] += xr;        im[a] += xim;
                }
            }
        }
        if (inversa) for (int i = 0; i < N; ++i) { re[i] /= N; im[i] /= N; }
    }

    // E1(x), integral exponencial (Abramowitz-Stegun 5.1.53 e 5.1.56)
    static double e1(double x)
    {
        if (x <= 1) {
            return -std::log(x) - 0.57721566 + x * (0.99999193 + x * (-0.24991055
                   + x * (0.05519968 + x * (-0.00976004 + x * 0.00107857))));
        }
        return std::exp(-x) / x * (x * x + 2.334733 * x + 0.250621) / (x * x + 3.330657 * x + 1.681534);
    }

    void mediaMovel(const std::vector<double>& src, std::vector<double>& dst, int largura)
    {
        const int r = largura >> 1;
        for (int k = 0; k < K; ++k) {
            double s = 0; int n = 0;
            for (int j = k - r; j <= k + r; ++j) if (j >= 0 && j < K) { s += src[j]; ++n; }
            dst[k] = s / n;
        }
    }

    void filaPoe(const double* x, int n)
    {
        for (int i = 0; i < n; ++i) {
            if (saidaN == saidaCap) { saidaIni = (saidaIni + 1) % saidaCap; --saidaN; }
            saida[(saidaIni + saidaN) % saidaCap] = float(x[i]); ++saidaN;
        }
    }

    void quadro()
    {
        // monta o quadro com as N amostras mais recentes, da mais antiga para a mais nova
        for (int i = 0; i < N; ++i) { re[i] = anel[(pos + i) % N] * win[i]; im[i] = 0; }
        fft(false);
        for (int k = 0; k < K; ++k) P[k] = re[k] * re[k] + im[k] * im[k];

        // 1) piso de ruido: minimo da PSD suavizada, em U sub-janelas de V quadros
        if (Ps.empty()) Ps = P;
        else for (int k = 0; k < K; ++k) Ps[k] = 0.8 * Ps[k] + 0.2 * P[k];
        for (int k = 0; k < K; ++k) if (Ps[k] < sub[k]) sub[k] = Ps[k];
        if (++cont >= V) {
            mins[uAt] = sub; uAt = (uAt + 1) % kU;
            std::fill(sub.begin(), sub.end(), std::numeric_limits<double>::infinity());
            cont = 0;
        }
        for (int k = 0; k < K; ++k) {
            double m = sub[k];
            for (int u = 0; u < kU; ++u) if (mins[u][k] < m) m = mins[u][k];
            tmp[k] = m * vies;
        }
        mediaMovel(tmp, Nr, 5);

        // 2) ganho log-MMSE com decisao dirigida
        double somaXi = 0;
        for (int k = 0; k < K; ++k) {
            const double nk = Nr[k] > 1e-20 ? Nr[k] : 1e-20;
            double ga = P[k] / nk; if (ga > 1e4) ga = 1e4;
            double x = 0.98 * Gp[k] * Gp[k] * gp[k] + 0.02 * (ga > 1 ? ga - 1 : 0);
            if (x < 1e-3) x = 1e-3;
            const double vv = x * ga / (1 + x);
            double gl = x / (1 + x) * std::exp(0.5 * e1(vv > 1e-8 ? vv : 1e-8));
            if (gl > 1) gl = 1;
            xi[k] = x; gam[k] = ga; v[k] = vv; glsa[k] = gl;
            if (k >= kLo && k <= kHi) somaXi += x;
        }
        // 3) presenca de voz: na faixa (media de 9 faixas) e no quadro inteiro
        mediaMovel(xi, tmp, 9);
        double fv = (10 * std::log10(somaXi / (kHi - kLo + 1) + 1e-12) + 2) / 8;
        fv = fv < 0 ? 0 : fv > 1 ? 1 : fv;
        for (int k = 0; k < K; ++k) {
            double pl = (10 * std::log10(tmp[k] + 1e-12) - sppInf) / (sppSup - sppInf);
            pl = pl < 0 ? 0 : pl > 1 ? 1 : pl;
            const double q = 1 - pl * fv;
            double p = 1 / (1 + q / (1 - q + 1e-6) * (1 + xi[k]) * std::exp(-v[k]));
            if (!(p >= 0)) p = 0;
            if (p > 1) p = 1;
            if (p < 0.6 * pprev[k]) p = 0.6 * pprev[k];     // a voz nao some de um quadro para o outro
            pprev[k] = p;
            g[k] = std::pow(glsa[k], p) * std::pow(Gmin, 1 - p);
        }
        mediaMovel(g, tmp, 3);
        for (int k = 0; k < K; ++k) {
            const double gk = tmp[k] > Gmin ? tmp[k] : Gmin;
            re[k] *= gk; im[k] *= gk;
            if (k > 0 && k < N / 2) { re[N - k] *= gk; im[N - k] *= gk; }
            Gp[k] = glsa[k]; gp[k] = gam[k];
        }

        // 4) volta ao tempo (fase original) e soma com os quadros vizinhos
        fft(true);
        const double esc = 1 / 1.5;                          // soma de hann^2 com passo N/4
        for (int i = 0; i < N; ++i) acc[i] += re[i] * win[i] * esc;
        filaPoe(acc.data(), H);
        std::copy(acc.begin() + H, acc.end(), acc.begin());
        std::fill(acc.end() - H, acc.end(), 0.0);
    }
};

// ===========================================================================
//  ProcAudio
// ===========================================================================
ProcAudio::ProcAudio() = default;
ProcAudio::~ProcAudio() = default;

void ProcAudio::ajustar(const AjusteAudio& a)
{
    std::lock_guard<std::mutex> lk(m_);
    aj_ = a;
}

AjusteAudio ProcAudio::ajuste() const
{
    std::lock_guard<std::mutex> lk(m_);
    return aj_;
}

// Prateleiras iguais as BiquadFilter 'lowshelf'/'highshelf' do navegador
// (receitas de R. Bristow-Johnson, inclinacao S = 1).
static void prateleira(double f0, double dB, bool aguda, double fs,
                       double& b0, double& b1, double& b2, double& a1, double& a2)
{
    const double A = std::pow(10.0, dB / 40.0);
    const double w0 = 2 * kPi * f0 / fs, c = std::cos(w0);
    const double alfa = std::sin(w0) / 2 * std::sqrt(2.0);
    const double r = 2 * std::sqrt(A) * alfa;
    double B0, B1, B2, A0, A1, A2;
    if (!aguda) {
        B0 = A * ((A + 1) - (A - 1) * c + r);
        B1 = 2 * A * ((A - 1) - (A + 1) * c);
        B2 = A * ((A + 1) - (A - 1) * c - r);
        A0 = (A + 1) + (A - 1) * c + r;
        A1 = -2 * ((A - 1) + (A + 1) * c);
        A2 = (A + 1) + (A - 1) * c - r;
    } else {
        B0 = A * ((A + 1) + (A - 1) * c + r);
        B1 = -2 * A * ((A - 1) + (A + 1) * c);
        B2 = A * ((A + 1) + (A - 1) * c - r);
        A0 = (A + 1) - (A - 1) * c + r;
        A1 = 2 * ((A - 1) - (A + 1) * c);
        A2 = (A + 1) - (A - 1) * c - r;
    }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
}

// Mesmo mapa do applyTone() da pagina.
void ProcAudio::calcularTom(int tom)
{
    tomAtual_ = tom;
    double gGrave = 0, gAgudo = 0;
    if (tom < 50) {
        const double d = (50 - tom) / 50.0;
        gGrave = d * 8.0;  gAgudo = -d * 15.0;
    } else if (tom > 50) {
        const double d = (tom - 50) / 50.0;
        gGrave = -d * 10.0; gAgudo = d * 8.0;
    }
    prateleira(350, gGrave, false, kTaxa, grave_.b0, grave_.b1, grave_.b2, grave_.a1, grave_.a2);
    prateleira(2500, gAgudo, true, kTaxa, agudo_.b0, agudo_.b1, agudo_.b2, agudo_.a1, agudo_.a2);
}

// Redutor de Ruido (slider) - traducao do _applyNR da pagina.
void ProcAudio::aplicarNR(float* x, size_t N, double level)
{
    if (level <= 0) { nrPiso_ = 0.0; nrUltimoGanho_ = 1.0; return; }
    const size_t BLOCK = 64;
    if (nrPiso_ < 1e-9) {
        double s = 0;
        for (size_t i = 0; i < N; ++i) s += double(x[i]) * x[i];
        nrPiso_ = std::sqrt(s / double(N)) * 0.10;
    }
    const double overSub = level * 2.0;
    size_t pos = 0;
    while (pos < N) {
        const size_t fim = std::min(pos + BLOCK, N), len = fim - pos;
        double sq = 0;
        for (size_t i = pos; i < fim; ++i) sq += double(x[i]) * x[i];
        const double rms = std::sqrt(sq / double(len));
        const bool ruido = rms < nrPiso_ * 2.0;
        const double alfa = ruido ? 0.06 : 0.002;
        nrPiso_ = nrPiso_ * (1 - alfa) + rms * alfa;
        const double ratio = rms > 1e-9 ? std::min(1.0, nrPiso_ / rms) : 1.0;
        const double alvo = std::max(0.0, 1.0 - ratio * ratio * overSub);
        const double g0 = nrUltimoGanho_;
        for (size_t i = 0; i < len; ++i) {
            const double t = double(i) / double(len);
            x[pos + i] = float(x[pos + i] * (g0 + (alvo - g0) * t));
        }
        nrUltimoGanho_ = alvo;
        pos = fim;
    }
}

void ProcAudio::processar(float* x, size_t n)
{
    if (!x || n == 0) return;
    const AjusteAudio a = ajuste();

    // 1) silenciar em rampa (~8 ms), como a pagina: quem cala, cala em rampa
    {
        const float alvo = a.mudo ? 0.f : 1.f;
        const float passo = 1.f - std::exp(-1.f / float(kTaxa * 0.008));
        for (size_t i = 0; i < n; ++i) {
            rampa_ += (alvo - rampa_) * passo;
            float s = x[i] * rampa_;
            x[i] = s > 1 ? 1 : s < -1 ? -1 : s;
        }
    }

    // 2) Noise Blanker
    if (a.nb > 0) {
        const float limiar = 5.0f - a.nb * 3.5f;
        for (size_t i = 0; i < n; ++i) {
            const float ab = std::fabs(x[i]);
            nbEnv_ = nbEnv_ * 0.995f + ab * 0.005f;
            if (ab > nbEnv_ * limiar && nbEnv_ > 0.001f) x[i] = 0.f;
            else nbEnv_ = nbEnv_ * 0.999f + ab * 0.001f;
        }
    }

    // 3) NR ESPECTRAL - ligar de novo recomeca aprendendo o chiado (igual a pagina)
    if (a.nrEsp) {
        if (!espLigadoAntes_ || !esp_) esp_ = std::make_unique<NrEspectral>(kTaxa);
        if (esp_->forca() != a.nrEspForca) esp_->setForca(a.nrEspForca);
        esp_->processar(x, n);
    }
    espLigadoAntes_ = a.nrEsp;

    // 4) Redutor de Ruido (slider)
    aplicarNR(x, n, a.nr);

    // 5) Tom (grave/agudo). Plano (50) nao passa pelos filtros.
    if (a.tom != 50) {
        if (a.tom != tomAtual_) calcularTom(a.tom);
        for (size_t i = 0; i < n; ++i) x[i] = agudo_.passa(grave_.passa(x[i]));
    } else if (tomAtual_ != 50) {
        tomAtual_ = 50;
        grave_ = Biquad(); agudo_ = Biquad();
    }
}

} // namespace masdr
