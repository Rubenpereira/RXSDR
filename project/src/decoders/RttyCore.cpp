#include "RttyCore.h"

#include <algorithm>
#include <cmath>
#include <complex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

// ---------------------------------------------------------------------------
//  ITA2 (Baudot), indice = valor de 5 bits com o PRIMEIRO bit recebido como
//  bit 0. Letras e algarismos conferidos com a tabela do SitorBCore (que veio
//  do ARRL Handbook). Onde o ITA2 nao define algarismo (D, F, G, H), usamos o
//  do teletipo americano ($ ! & #), que e o que o radioamador manda.
// ---------------------------------------------------------------------------
namespace {
const char kLetras[32] = {
    '\0','E','\n','A',' ','S','I','U','\r','D','R','J','N','F','C','K',
    'T','Z','L','W','H','Y','P','Q','O','B','G','\0','M','X','V','\0'
};
const char kFiguras[32] = {
    '\0','3','\n','-',' ','\'','8','7','\r','$','4','\a',',','!',':','(',
    '5','+',')','2','#','6','0','1','9','?','&','\0','.','/','=','\0'
};
constexpr int kCodFigs = 27;
constexpr int kCodLtrs = 31;
constexpr int kCodEspaco = 4;

// FFT radix-2 simples, so para a medicao do tom (roda a cada 2 s)
void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace

char RttyCore::ita2(int code, bool figuras)
{
    if (code < 0 || code > 31) return '\0';
    return figuras ? kFiguras[code] : kLetras[code];
}

RttyCore::RttyCore() { setParams(Params{}); }
RttyCore::RttyCore(const Params& p) { setParams(p); }

void RttyCore::setParams(const Params& p)
{
    p_ = p;
    if (p_.sampleRate <= 0) p_.sampleRate = 8000.0;
    if (p_.baudRate <= 0)   p_.baudRate = 45.45;
    if (p_.shift <= 0)      p_.shift = 170.0;
    spb_  = p_.sampleRate / p_.baudRate;
    nBit_ = std::max(8, int(std::lround(spb_)));
    nCurto_ = std::max(4, int(std::lround(spb_ / 4.0)));
    reset();
}

void RttyCore::configurarTons(double centro)
{
    double m = centro + p_.shift / 2.0, s = centro - p_.shift / 2.0;
    if (p_.invert) std::swap(m, s);
    wMark_  = 2.0 * M_PI * m / p_.sampleRate;
    wSpace_ = 2.0 * M_PI * s / p_.sampleRate;
    p_.centerFreq = centro;
}

void RttyCore::reset()
{
    configurarTons(p_.centerFreq);
    n_ = 0;
    bmI_.assign(size_t(nBit_), 0.0); bmQ_ = bmI_; bsI_ = bmI_; bsQ_ = bmI_;
    cmI_.assign(size_t(nCurto_), 0.0); cmQ_ = cmI_; csI_ = cmI_; csQ_ = cmI_;
    smI_ = smQ_ = ssI_ = ssQ_ = 0;
    cmIs_ = cmQs_ = csIs_ = csQs_ = 0;
    pos_ = posC_ = 0;
    pkMark_ = pkSpace_ = 1e-6;
    vlMark_ = vlSpace_ = 0.0;
    dHist_.assign(size_t(nBit_) * 12, 0.0f);
    dPos_ = 0;
    est_ = Est::Repouso;
    tBorda_ = 0;
    marcaSeguida_ = 0;
    ultCurto_ = 0;
    figuras_ = false;
    sync_ = false;
    bonsSeguidos_ = errosSeguidos_ = 0;
    pendente_.clear();
    tomBuf_.clear();
    tomNovo_ = false;
    ultimaMedida_ = 0;
    saida_.clear();
    totalChars_ = erros_ = 0;
}

// ---------------------------------------------------------------------------
//  Medicao do tom central: espectro medio de ~2 s e procura o PAR de picos
//  separados pelo shift. O criterio e o menor dos dois (min(mark, space)):
//  assim um apito isolado nao engana - precisa haver energia nos DOIS tons.
// ---------------------------------------------------------------------------
void RttyCore::medirTom()
{
    const size_t N = 4096;
    if (tomBuf_.size() < N * 2) return;
    std::vector<double> P(N / 2 + 1, 0.0);
    std::vector<std::complex<double>> a(N);
    for (size_t o = 0; o + N <= tomBuf_.size(); o += N / 2) {
        for (size_t i = 0; i < N; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(N - 1));
            a[i] = std::complex<double>(double(tomBuf_[o + i]) * w, 0.0);
        }
        fft(a);
        for (size_t k = 0; k < P.size(); ++k) P[k] += std::norm(a[k]);
    }
    const double hzBin = p_.sampleRate / double(N);
    const int meio = int(std::lround(p_.shift / 2.0 / hzBin));
    auto pico = [&](int k) {           // maior valor a +-2 faixas (tom nao cai exato)
        double m = 0;
        for (int j = k - 2; j <= k + 2; ++j)
            if (j >= 0 && j < int(P.size())) m = std::max(m, P[size_t(j)]);
        return m;
    };
    std::vector<double> ord(P.begin() + int(200 / hzBin), P.begin() + int(3400 / hzBin));
    std::nth_element(ord.begin(), ord.begin() + long(ord.size() / 2), ord.end());
    const double piso = ord[ord.size() / 2] + 1e-18;

    double melhor = 0; int kMelhor = -1;
    for (int k = int(300 / hzBin) + meio; k < int(3200 / hzBin) - meio; ++k) {
        const double s = std::min(pico(k - meio), pico(k + meio));
        if (s > melhor) { melhor = s; kMelhor = k; }
    }
    // Os dois tons tem de estar bem acima do chiado (10 dB); senao nao ha
    // sinal. Com 6 dB, nas PAUSAS de uma estacao (so chiado) a medicao achava
    // "tons" no ruido e levava o decodificador para la - e quando a estacao
    // voltava ele perdia o comeco da chamada. Visto em 21.095, CQ WW RTTY:
    // o tom pulava 535, 770, 480, 659... a cada 2 s.
    if (kMelhor < 0 || melhor < piso * 10.0) return;

    // Fica na estacao atual enquanto ela estiver ali com forca parecida
    // (ate ~5 dB abaixo do melhor par). Banda de concurso tem varias
    // estacoes na mesma faixa de 2,4 kHz: sem isto ele pulava entre elas.
    const int kAtual = int(std::lround(p_.centerFreq / hzBin));
    if (kAtual - meio > 2 && kAtual + meio < int(P.size()) - 2) {
        const double sAtual = std::min(pico(kAtual - meio), pico(kAtual + meio));
        if (sAtual >= melhor * 0.3 && sAtual >= piso * 10.0) return;
    }
    // Refina com o centroide de cada tom (a faixa da FFT tem ~2 Hz, mas o pico
    // escolhido pelo 'max' pode cair a varias faixas do centro verdadeiro).
    auto centroide = [&](int k) {
        double sw = 0, swk = 0;
        for (int j = k - 6; j <= k + 6; ++j)
            if (j >= 0 && j < int(P.size())) { sw += P[size_t(j)]; swk += P[size_t(j)] * j; }
        return sw > 0 ? swk / sw : double(k);
    };
    const double centro = 0.5 * (centroide(kMelhor - meio) + centroide(kMelhor + meio)) * hzBin;
    if (std::fabs(centro - p_.centerFreq) > 4.0) {
        configurarTons(centro);
        tomMedido_ = centro;
        tomNovo_ = true;
    }
}

// ---------------------------------------------------------------------------

void RttyCore::fecharCaractere(int code, double confianca)
{
    ++totalChars_;
    char c = '\0';
    if (code == kCodLtrs) { figuras_ = false; }
    else if (code == kCodFigs) { figuras_ = true; }
    else {
        c = ita2(code, figuras_);
        if (code == kCodEspaco && p_.usos) figuras_ = false;
    }
    (void)confianca;

    bonsSeguidos_++;
    errosSeguidos_ = 0;
    std::string txt;
    if (c == '\n' || c == '\r') txt = "\n";
    else if (c >= 32 && c < 127) txt = std::string(1, c);

    if (sync_) {
        saida_ += txt;
    } else {
        pendente_ += txt;
        if (pendente_.size() > 8) pendente_.erase(0, pendente_.size() - 8);
        // Quatro caracteres bem enquadrados em sequencia: e sinal, nao ruido.
        // (Com tres, 10 min de chiado puro imprimiam 7 caracteres de lixo.)
        if (bonsSeguidos_ >= 4) {
            sync_ = true;
            saida_ += pendente_;
            pendente_.clear();
        }
    }
}

void RttyCore::processarAmostra(float xf)
{
    const double x = double(xf);
    const double am = wMark_ * double(n_), as = wSpace_ * double(n_);
    const double mI = x * std::cos(am), mQ = -x * std::sin(am);
    const double sI = x * std::cos(as), sQ = -x * std::sin(as);

    // somas moveis de um bit (filtro casado)
    smI_ += mI - bmI_[pos_]; bmI_[pos_] = mI;
    smQ_ += mQ - bmQ_[pos_]; bmQ_[pos_] = mQ;
    ssI_ += sI - bsI_[pos_]; bsI_[pos_] = sI;
    ssQ_ += sQ - bsQ_[pos_]; bsQ_[pos_] = sQ;
    pos_ = (pos_ + 1) % size_t(nBit_);
    // somas moveis de 1/4 de bit (detector de borda)
    cmIs_ += mI - cmI_[posC_]; cmI_[posC_] = mI;
    cmQs_ += mQ - cmQ_[posC_]; cmQ_[posC_] = mQ;
    csIs_ += sI - csI_[posC_]; csI_[posC_] = sI;
    csQs_ += sQ - csQ_[posC_]; csQ_[posC_] = sQ;
    posC_ = (posC_ + 1) % size_t(nCurto_);

    const double em = std::hypot(smI_, smQ_) / nBit_, es = std::hypot(ssI_, ssQ_) / nBit_;
    const double emc = std::hypot(cmIs_, cmQs_) / nCurto_, esc = std::hypot(csIs_, csQs_) / nCurto_;

    // ATC "otimo": cada tom e comparado com o MEIO entre o proprio pico e o
    // proprio vale (acompanhados em ~8 bits), e as duas diferencas sao
    // somadas em unidades de amplitude. Quando um tom desvanece, a diferenca
    // dele encolhe junto e quem decide e o outro tom. A 1a versao dividia cada
    // tom pelo proprio pico: com um tom sumido, esse quociente virava puro
    // chiado e decidia errado - medido: 2 de 20 frases com desvanecimento
    // seletivo.
    const double a = 1.0 - std::exp(-1.0 / (spb_ * 8.0));
    auto seguir = [&](double e, double& pk, double& vl) {
        pk = (e > pk) ? e : pk - (pk - e) * a;
        vl = (e < vl) ? e : vl + (e - vl) * a;
    };
    seguir(em, pkMark_, vlMark_);
    seguir(es, pkSpace_, vlSpace_);
    const double faixa = 0.5 * ((pkMark_ - vlMark_) + (pkSpace_ - vlSpace_)) + 1e-9;
    const double midM = 0.5 * (pkMark_ + vlMark_), midS = 0.5 * (pkSpace_ + vlSpace_);
    const double d  = ((em  - midM) - (es  - midS)) / faixa;   // + = MARK
    const double dc = ((emc - midM) - (esc - midS)) / faixa;

    dHist_[dPos_] = float(d);
    dPos_ = (dPos_ + 1) % dHist_.size();

    // As somas acumulam erro de arredondamento; a cada 2^16 amostras refaz.
    if ((n_ & 0xFFFF) == 0xFFFF) {
        smI_ = smQ_ = ssI_ = ssQ_ = 0;
        for (int i = 0; i < nBit_; ++i) { smI_ += bmI_[size_t(i)]; smQ_ += bmQ_[size_t(i)]; ssI_ += bsI_[size_t(i)]; ssQ_ += bsQ_[size_t(i)]; }
        cmIs_ = cmQs_ = csIs_ = csQs_ = 0;
        for (int i = 0; i < nCurto_; ++i) { cmIs_ += cmI_[size_t(i)]; cmQs_ += cmQ_[size_t(i)]; csIs_ += csI_[size_t(i)]; csQs_ += csQ_[size_t(i)]; }
    }

    const double agora = double(n_);
    if (est_ == Est::Repouso) {
        if (d > 0) marcaSeguida_++;
        // Borda MARK -> SPACE depois de pelo menos meio bit em MARK: e o bit de
        // partida. A soma de um bit cruza zero quando metade dela ja esta em
        // SPACE, ou seja, meio bit depois da borda verdadeira. (Uma janela mais
        // curta daria a borda mais cedo, mas a 75 baud ela nao separa dois tons
        // a 170 Hz - medido: 39% de erro com sinal limpo.)
        if (ultCurto_ > 0 && d <= 0 && marcaSeguida_ >= int(spb_ * 0.5)) {
            tBorda_ = agora - spb_ / 2.0;
            est_ = Est::Quadro;
        }
        if (d <= 0) marcaSeguida_ = 0;
    } else {
        // Ja passou tudo: partida + 5 dados + parada = 7 bits.
        if (agora >= tBorda_ + 7.0 * spb_) {
            // A soma de um bit, lida no FIM de cada bit, e a media dele inteiro.
            auto bitK = [&](int k) {
                const double t = tBorda_ + double(k + 1) * spb_;
                const long long atras = (long long)std::lround(agora - t);
                const long long L = (long long)dHist_.size();
                long long idx = (long long)dPos_ - 1 - atras;
                idx %= L; if (idx < 0) idx += L;
                return double(dHist_[size_t(idx)]);
            };
            const double partida = bitK(0), parada = bitK(6);
            int code = 0; double conf = 1.0;
            for (int b = 0; b < 5; ++b) {
                const double v = bitK(b + 1);
                if (v > 0) code |= (1 << b);
                conf = std::min(conf, std::fabs(v));
            }
            if (partida < -0.15 && parada > 0.15 && conf > 0.08) {
                fecharCaractere(code, conf);
                // A parada ja foi vista em MARK: libera a proxima borda na hora.
                marcaSeguida_ = int(spb_);
            } else if (partida >= 0) {
                // Falso inicio (um pico de ruido): nao e erro de quadro.
                marcaSeguida_ = 0;
            } else {
                ++erros_;
                bonsSeguidos_ = 0;
                if (++errosSeguidos_ >= 4) { sync_ = false; pendente_.clear(); }
                marcaSeguida_ = 0;
            }
            est_ = Est::Repouso;
        }
    }
    ultCurto_ = d;
    (void)dc;
    ++n_;
}

std::string RttyCore::feed(const float* s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        processarAmostra(s[i]);
        if (p_.autoTom && !sync_) {
            tomBuf_.push_back(s[i]);
            if (tomBuf_.size() >= size_t(p_.sampleRate * 2.0)) {
                medirTom();
                tomBuf_.clear();
            }
        }
    }
    std::string r;
    r.swap(saida_);
    return r;
}

} // namespace masdr
