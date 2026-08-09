#include "AnaliseCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

namespace {

std::string fmt(const char* f, double a, double b = 0, double c = 0)
{
    char s[256];
    std::snprintf(s, sizeof(s), f, a, b, c);
    return std::string(s);
}

// Interpolacao parabolica: o pico verdadeiro raramente cai exatamente numa
// raia da FFT. Sem isto o erro chega a meia raia, que a 8 kHz com 4096 pontos
// ja e 1 Hz - suficiente para estragar a leitura do deslocamento.
double refinar(const std::vector<double>& p, size_t i, double hzPorRaia)
{
    if (i == 0 || i + 1 >= p.size()) return double(i) * hzPorRaia;
    const double a = p[i - 1], b = p[i], c = p[i + 1];
    const double den = a - 2.0 * b + c;
    const double d = (std::abs(den) < 1e-20) ? 0.0 : 0.5 * (a - c) / den;
    return (double(i) + d) * hzPorRaia;
}

} // namespace

// ---------------------------------------------------------------------------

void AnaliseCore::fft(std::vector<std::complex<double>>& a, bool inversa)
{
    const size_t n = a.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * M_PI / double(len) * (inversa ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inversa) for (auto& x : a) x /= double(n);
}

AnaliseCore::AnaliseCore(double sampleRate) : sr_(sampleRate > 0 ? sampleRate : 8000.0) {}

void AnaliseCore::limpar() { buf_.clear(); }

bool AnaliseCore::alimentar(const float* amostras, size_t n)
{
    if (!amostras || n == 0) return false;
    const size_t limite = size_t(kSegundos * sr_);
    if (buf_.size() >= limite) return true;
    const size_t cabe = std::min(n, limite - buf_.size());
    buf_.insert(buf_.end(), amostras, amostras + cabe);
    return buf_.size() >= limite;
}

// Espectro medio por sobreposicao: uma FFT unica de um sinal com desvanecimento
// mostra picos que vao e vem. A media sobre varias janelas estabiliza.
std::vector<double> AnaliseCore::espectroMedio(int n) const
{
    std::vector<double> acc(size_t(n) / 2 + 1, 0.0);
    if (buf_.size() < size_t(n)) return acc;

    std::vector<double> jan(static_cast<size_t>(n), 0.0);
    for (int i = 0; i < n; ++i)
        jan[size_t(i)] = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(n - 1));

    // Media SO das janelas com sinal. Sinal em rajada - APRS, DSC, packet -
    // passa a maior parte dos 12 segundos calado, e mediar tudo joga a energia
    // dos tons contra o ruido do silencio: num teste em APRS o tom de 2200 Hz
    // sumiu e so o de 1200 sobrou, virando "um unico tom". Aqui medimos a
    // energia de cada janela e ficamos com o terco mais forte.
    std::vector<double> energia;
    for (size_t off = 0; off + size_t(n) <= buf_.size(); off += size_t(n) / 2) {
        double e = 0;
        for (int i = 0; i < n; ++i) { const double v = double(buf_[off + size_t(i)]); e += v * v; }
        energia.push_back(e);
    }
    if (energia.empty()) return acc;

    std::vector<double> ord = energia;
    std::sort(ord.begin(), ord.end());
    // Corte no percentil 67, mas nunca acima de um terco da energia de pico:
    // em sinal continuo isso mantem praticamente todas as janelas.
    double corte = ord[(ord.size() * 2) / 3];
    if (corte > ord.back() * 0.34) corte = ord.back() * 0.34;

    int blocos = 0;
    size_t idx = 0;
    for (size_t off = 0; off + size_t(n) <= buf_.size(); off += size_t(n) / 2, ++idx) {
        if (idx < energia.size() && energia[idx] < corte) continue;
        std::vector<std::complex<double>> a(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            a[size_t(i)] = std::complex<double>(double(buf_[off + size_t(i)]) * jan[size_t(i)], 0.0);
        fft(a, false);
        for (size_t k = 0; k < acc.size(); ++k) acc[k] += std::norm(a[k]);
        ++blocos;
    }
    if (blocos > 0) for (auto& v : acc) v /= double(blocos);
    return acc;
}

// Energia de um tom ao longo do tempo: desloca o tom para zero e faz media
// movel. E o mesmo principio do correlator dos decodificadores.
std::vector<float> AnaliseCore::envoltoria(double freqHz, int janela) const
{
    const size_t n = buf_.size();
    std::vector<float> out(n, 0.0f);
    if (n < size_t(janela) || janela < 2) return out;

    double somaI = 0, somaQ = 0;
    std::vector<double> bufI(static_cast<size_t>(janela), 0.0);
    std::vector<double> bufQ(static_cast<size_t>(janela), 0.0);
    size_t pos = 0;
    const double w = 2.0 * M_PI * freqHz / sr_;
    for (size_t i = 0; i < n; ++i) {
        const double c = std::cos(w * double(i)), s = -std::sin(w * double(i));
        const double vi = double(buf_[i]) * c, vq = double(buf_[i]) * s;
        somaI += vi - bufI[pos];  bufI[pos] = vi;
        somaQ += vq - bufQ[pos];  bufQ[pos] = vq;
        pos = (pos + 1) % size_t(janela);
        out[i] = float(std::sqrt(somaI * somaI + somaQ * somaQ) / double(janela));
    }
    return out;
}

// ---------------------------------------------------------------------------

AnaliseCore::Resultado AnaliseCore::analisar() const
{
    Resultado r;
    if (buf_.size() < size_t(4.0 * sr_)) {
        r.linhas.push_back("Audio insuficiente. Deixe a janela aberta com o sinal presente.");
        return r;
    }

    // ---- 1) tons ---------------------------------------------------------
    // 8192 e nao 4096: com 4096 as raias ficam a 2 Hz e o borrao da propria
    // modulacao cobria os dois tons quando o deslocamento e estreito (170 Hz
    // do SITOR-B e do DSC). Nos testes, so essa troca fez a velocidade de um
    // sinal de 100 baud sair de 43 para 100.
    const int N = 8192;
    const double hzRaia = sr_ / double(N);
    std::vector<double> P = espectroMedio(N);
    if (P.empty()) { r.linhas.push_back("Falha ao calcular o espectro."); return r; }

    const size_t kMin = size_t(150.0 / hzRaia), kMax = std::min(P.size() - 2, size_t(3600.0 / hzRaia));
    // Guarda tambem a altura de cada pico: os dois tons de uma FSK sao os
    // MAIS FORTES, nao os mais afastados. Pegar os extremos - como eu fazia -
    // escolhia lobos laterais e inflava o deslocamento.
    std::vector<std::pair<double,double>> picos;   // (frequencia, potencia)
    std::vector<double> Ptmp = P;
    for (int t = 0; t < 6; ++t) {
        size_t melhor = 0; double vmax = 0;
        for (size_t k = kMin; k < kMax; ++k)
            if (Ptmp[k] > vmax) { vmax = Ptmp[k]; melhor = k; }
        if (melhor == 0) break;

        // Um pico so conta se destacar do fundo; senao e ruido.
        double mediana = 0;
        { std::vector<double> c(Ptmp.begin() + long(kMin), Ptmp.begin() + long(kMax));
          std::nth_element(c.begin(), c.begin() + long(c.size() / 2), c.end());
          mediana = c[c.size() / 2]; }
        if (vmax < mediana * 8.0) break;

        picos.emplace_back(refinar(Ptmp, melhor, hzRaia), vmax);
        // Apaga a vizinhanca para achar o proximo tom de verdade. A largura
        // tem de ser MENOR que metade do menor deslocamento que interessa:
        // SITOR-B e DSC usam 170 Hz, entao 85 Hz e a metade. Com os 80 Hz que
        // eu tinha posto aqui, o segundo tom era apagado junto com o primeiro
        // e no lugar dele sobrava um lobo lateral - o deslocamento saia
        // errado e a velocidade ia junto.
        const long largura = long(35.0 / hzRaia);
        for (long k = long(melhor) - largura; k <= long(melhor) + largura; ++k)
            if (k >= 0 && k < long(Ptmp.size())) Ptmp[size_t(k)] = 0.0;
    }
    // Tons "de verdade": os que chegam perto do mais forte. Lobo lateral de
    // FSK fica bem abaixo disso, entao nao conta como tom.
    r.nTons = 0;
    if (!picos.empty()) {
        const double maior = picos[0].second;
        for (const auto& pk : picos) if (pk.second > maior * 0.25) ++r.nTons;
    }

    // ---- so um tom: analisa a AMPLITUDE em vez da frequencia -------------
    // Nem todo sinal digital e FSK. Portadora unica pode ser modulada em
    // amplitude: RFID em 13,56 MHz, telemetria, CW. Nesses casos a informacao
    // esta no acender e apagar, e a envoltoria conta a historia.
    auto analisarAmplitude = [&](double f0) -> Resultado& {
        r.linhas.push_back(fmt("Portadora unica em %.1f Hz - nao e FSK.", f0));

        const std::vector<float> e = envoltoria(f0, std::max(8, int(sr_ / 500.0)));
        double soma = 0, pico = 0;
        for (float v : e) { soma += v; if (v > pico) pico = v; }
        const double media = e.empty() ? 0 : soma / double(e.size());

        if (pico <= 0) { r.linhas.push_back("Sem energia suficiente para medir."); r.ok = true; return r; }

        size_t acesos = 0;
        for (float v : e) if (double(v) > pico * 0.5) ++acesos;
        const double fracao = e.empty() ? 0 : double(acesos) / double(e.size());
        const double relacao = media / pico;

        r.linhas.push_back(fmt("Envoltoria: media %.3f do pico | ligado em %.0f%% do tempo",
                               relacao, fracao * 100.0));

        // Repeticao das rajadas: espectro da propria envoltoria. Aqui o que
        // interessa e a faixa BEM baixa - poucos hertz -, porque um leitor
        // RFID interroga algumas vezes por segundo.
        const size_t NB2 = 32768;
        double taxa = 0, destaque = 0;
        if (e.size() > NB2) {
            std::vector<double> acc2(NB2 / 2 + 1, 0.0);
            int nb = 0;
            for (size_t off = 0; off + NB2 <= e.size(); off += NB2 / 2) {
                std::vector<std::complex<double>> a(NB2);
                double m = 0;
                for (size_t i = 0; i < NB2; ++i) m += double(e[off + i]);
                m /= double(NB2);
                for (size_t i = 0; i < NB2; ++i) {
                    const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(NB2 - 1));
                    a[i] = std::complex<double>((double(e[off + i]) - m) * w, 0.0);
                }
                fft(a, false);
                for (size_t k = 0; k < acc2.size(); ++k) acc2[k] += std::norm(a[k]);
                ++nb;
            }
            if (nb > 0) {
                const double hz2 = sr_ / double(NB2);
                const size_t j0 = std::max<size_t>(1, size_t(0.5 / hz2));
                const size_t j1 = std::min(acc2.size(), size_t(2000.0 / hz2));
                size_t melhor2 = 0; double vmax2 = 0;
                for (size_t k = j0; k < j1; ++k)
                    if (acc2[k] > vmax2) { vmax2 = acc2[k]; melhor2 = k; }
                std::vector<double> c2(acc2.begin() + long(j0), acc2.begin() + long(j1));
                std::nth_element(c2.begin(), c2.begin() + long(c2.size() / 2), c2.end());
                const double med2 = c2[c2.size() / 2] > 0 ? c2[c2.size() / 2] : 1e-30;
                destaque = vmax2 / med2;
                if (melhor2) taxa = double(melhor2) * hz2;
            }
        }

        if (fracao > 0.85 && relacao > 0.7) {
            r.veredito = "portadora continua";
            r.linhas.push_back("Portadora limpa, sem liga-desliga. Pode ser CW em repouso,");
            r.linhas.push_back("piloto, ou sinal de fase (PSK) - que este analisador nao le.");
        } else if (fracao < 0.20) {
            r.veredito = "portadora pulsada";
            r.linhas.push_back("PULSADA: fica apagada a maior parte do tempo, em rajadas curtas.");
            if (taxa > 0 && destaque > 8.0)
                r.linhas.push_back(fmt("Rajadas repetindo a cada %.2f s (%.1f por segundo).",
                                       1.0 / taxa, taxa));
            r.linhas.push_back("Tipico de leitor RFID, telemetria ou baliza que chama em ciclo.");
        } else {
            r.veredito = "modulada em amplitude";
            r.linhas.push_back("Modulada em AMPLITUDE - a informacao esta no acender e apagar.");
            if (taxa > 0 && destaque > 8.0)
                r.linhas.push_back(fmt("Ritmo principal: %.1f Hz", taxa));
        }
        r.linhas.push_back("Nao ha texto a extrair por aqui: a leitura de amplitude");
        r.linhas.push_back("so mostra o ritmo, nao o conteudo.");
        r.ok = true;
        return r;
    };

    if (picos.size() == 1) return analisarAmplitude(picos[0].first);

    if (picos.size() < 2) {
        r.linhas.push_back("Nao achei dois tons claros.");
        r.linhas.push_back(picos.empty() ? "Nenhum pico se destaca do ruido - o sinal pode estar fraco ou ser de fase (PSK)."
                                         : fmt("Um unico tom em %.1f Hz - portadora continua, CW ou sinal de fase.", picos[0].first));
        return r;
    }

    // Os dois mais fortes, depois ordenados por frequencia
    double fa = picos[0].first, fb = picos[1].first;
    if (fa > fb) std::swap(fa, fb);
    // Dois picos nao provam FSK. Portadora pulsada ou modulada em amplitude
    // cria bandas laterais que parecem um segundo tom - nos testes, uma
    // portadora ligando a 20 Hz virou "FSK com shift de 60 Hz e 520 baud".
    // Duas provas resolvem: FSK de verdade tem deslocamento de pelo menos
    // uma centena de hertz, e seus dois tons ALTERNAM (quando um acende o
    // outro apaga). Banda lateral de amplitude acende JUNTO com a portadora.
    {
        const int janTeste = std::max(8, int(sr_ / 400.0));
        const std::vector<float> t1 = envoltoria(fa, janTeste);
        const std::vector<float> t2 = envoltoria(fb, janTeste);
        double m1 = 0, m2 = 0;
        for (size_t i = 0; i < t1.size(); ++i) { m1 += t1[i]; m2 += t2[i]; }
        m1 /= double(t1.size()); m2 /= double(t2.size());
        double num = 0, d1 = 0, d2 = 0;
        for (size_t i = 0; i < t1.size(); ++i) {
            const double x1 = double(t1[i]) - m1, x2 = double(t2[i]) - m2;
            num += x1 * x2; d1 += x1 * x1; d2 += x2 * x2;
        }
        const double correl = (d1 > 0 && d2 > 0) ? num / std::sqrt(d1 * d2) : 0.0;

        if ((fb - fa) < 100.0 || correl > -0.05) {
            if ((fb - fa) < 100.0)
                r.linhas.push_back(fmt("Os dois picos estao a so %.0f Hz - estreito demais para FSK.", fb - fa));
            else
                r.linhas.push_back(fmt("Os dois picos acendem juntos (correlacao %+.2f), nao alternam.", correl));
            return analisarAmplitude(picos[0].first);
        }
    }

    r.tomBaixoHz   = fa;
    r.tomAltoHz    = fb;
    r.deslocamento = r.tomAltoHz - r.tomBaixoHz;

    // ---- 2) velocidade ---------------------------------------------------
    // A diferenca das duas envoltorias troca de sinal a cada simbolo. Elevando
    // a derivada ao quadrado nasce uma raia justamente na taxa de simbolos.
    const int jan = std::max(8, int(sr_ / 400.0));
    std::vector<float> ea = envoltoria(r.tomBaixoHz, jan);
    std::vector<float> eb = envoltoria(r.tomAltoHz, jan);
    std::vector<double> d(ea.size());
    for (size_t i = 0; i < ea.size(); ++i) d[i] = double(ea[i]) - double(eb[i]);

    // Eu tirava o pico de CADA bloco e depois a mediana dos picos. Errado:
    // a raia do relogio e fraca, e o pico de um bloco isolado cai em ruido -
    // num sinal real de 75 baud os blocos devolveram 120, 75, 27 e 239, e a
    // mediana disso deu 120. O certo e somar os espectros de todos os blocos
    // ANTES de procurar o pico: o ruido se cancela, a raia se soma.
    // DIFERENCA contra SOMA das envoltorias. Essa e a chave para nao cair em
    // linha falsa: quando um simbolo troca, a energia so MUDA DE LADO entre os
    // dois tons - a diferenca oscila e a soma fica quase parada. Ja zumbido da
    // rede (120 Hz aqui, 100 Hz na Europa), desvanecimento e ruido de fonte
    // mexem nos dois tons juntos, e portanto aparecem na SOMA tambem.
    // Dividir um pelo outro deixa so o que e keying de verdade.
    const size_t NB = 16384;
    double baudConfianca = 0.0;
    std::vector<double> soma(ea.size());
    for (size_t i = 0; i < ea.size(); ++i) soma[i] = double(ea[i]) + double(eb[i]);

    auto espectroRelogio = [&](const std::vector<double>& sig) {
        std::vector<double> acc(NB / 2 + 1, 0.0);
        int blocos = 0;
        for (size_t off = 0; off + NB + 1 < sig.size(); off += NB / 2) {
            std::vector<double> g(NB);
            double m = 0;
            for (size_t i = 0; i < NB; ++i) {
                const double dv = sig[off + i + 1] - sig[off + i];
                g[i] = dv * dv; m += g[i];
            }
            m /= double(NB);
            std::vector<std::complex<double>> a(NB);
            for (size_t i = 0; i < NB; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(NB - 1));
                a[i] = std::complex<double>((g[i] - m) * w, 0.0);
            }
            fft(a, false);
            for (size_t k = 0; k < acc.size(); ++k) acc[k] += std::norm(a[k]);
            ++blocos;
        }
        if (blocos > 0) for (auto& v : acc) v /= double(blocos);
        return acc;
    };

    if (d.size() > NB + 2) {
        const std::vector<double> accD = espectroRelogio(d);
        const std::vector<double> accS = espectroRelogio(soma);

        const double hz = sr_ / double(NB);
        const size_t k0 = size_t(20.0 / hz), k1 = std::min(accD.size(), size_t(600.0 / hz));

        std::vector<double> cD(accD.begin() + long(k0), accD.begin() + long(k1));
        std::vector<double> cS(accS.begin() + long(k0), accS.begin() + long(k1));
        std::nth_element(cD.begin(), cD.begin() + long(cD.size() / 2), cD.end());
        std::nth_element(cS.begin(), cS.begin() + long(cS.size() / 2), cS.end());
        const double medD = cD[cD.size() / 2] > 0 ? cD[cD.size() / 2] : 1e-30;
        const double medS = cS[cS.size() / 2] > 0 ? cS[cS.size() / 2] : 1e-30;

        // Dois criterios, nesta ordem: a raia tem de (1) passar no teste da
        // soma, provando que e keying e nao zumbido, e so entao (2) valer pela
        // altura. Usar so a razao escolhia harmonico - num teste de 75 baud
        // ela apontou 149,5, que e o dobro.
        size_t melhor = 0; double melhorDestaque = 0;
        for (size_t k = k0; k < k1; ++k) {
            const double dD = accD[k] / medD;          // destaque na diferenca
            const double dS = accS[k] / medS;          // destaque na soma
            if (dD < 6.0) continue;                    // fraco demais para ser relogio
            if (dD / (dS + 1.0) < 2.0) continue;       // mexe nos dois tons: nao e keying
            if (dD > melhorDestaque) { melhorDestaque = dD; melhor = k; }
        }

        // Se existe candidato aprovado perto da METADE da frequencia escolhida,
        // o que achamos era o segundo harmonico. A fundamental e a resposta.
        if (melhor) {
            const size_t kMeio = melhor / 2;
            if (kMeio >= k0) {
                size_t achado = 0; double melhorAli = 0;
                const size_t tol = std::max<size_t>(2, melhor / 40);
                for (size_t k = (kMeio > tol ? kMeio - tol : k0); k <= kMeio + tol && k < k1; ++k) {
                    const double dD = accD[k] / medD, dS = accS[k] / medS;
                    if (dD < 6.0 || dD / (dS + 1.0) < 2.0) continue;
                    if (dD > melhorAli) { melhorAli = dD; achado = k; }
                }
                if (achado && melhorAli > melhorDestaque * 0.30) {
                    melhor = achado; melhorDestaque = melhorAli;
                }
            }
        }

        baudConfianca = melhorDestaque;
        if (melhor) r.baud = refinar(accD, melhor, hz);
    }

    // ---- 3) bits e teste de aleatoriedade --------------------------------
    if (r.baud > 5.0) {
        const double spb = sr_ / r.baud;
        double melhorEnergia = -1; int melhorFase = 0;
        for (int ph = 0; ph < int(spb); ++ph) {
            double e = 0; int c = 0;
            for (double t = ph; t < double(d.size()); t += spb) { e += std::abs(d[size_t(t)]); ++c; }
            if (c && e / c > melhorEnergia) { melhorEnergia = e / c; melhorFase = ph; }
        }
        std::vector<uint8_t> bits;
        for (double t = melhorFase; t < double(d.size()); t += spb) bits.push_back(d[size_t(t)] > 0 ? 1 : 0);
        r.bitsUsados = int(bits.size());

        if (bits.size() > 200) {
            size_t uns = 0; for (uint8_t b : bits) uns += b;
            r.proporcaoUns = double(uns) / double(bits.size());

            std::vector<int> corridas;
            int c = 1;
            for (size_t i = 1; i < bits.size(); ++i) {
                if (bits[i] == bits[i - 1]) ++c; else { corridas.push_back(c); c = 1; }
            }
            if (!corridas.empty()) {
                double soma = 0; for (int v : corridas) soma += v;
                r.mediaCorridas = soma / double(corridas.size());

                // Num fluxo aleatorio a fracao de corridas de comprimento k e
                // 2^-k. Comparamos as quatro primeiras com o esperado; quanto
                // mais perto, mais indistinguivel de acaso - ou seja, cifrado.
                double erro = 0;
                for (int k = 1; k <= 4; ++k) {
                    size_t q = 0; for (int v : corridas) if (v == k) ++q;
                    const double obs = double(q) / double(corridas.size());
                    const double esp = std::pow(0.5, k);
                    erro += std::abs(obs - esp);
                }
                r.aleatoriedade = std::max(0.0, 1.0 - erro * 2.5);
            }
        }
    }

    // ---- 4) relatorio ----------------------------------------------------
    r.ok = true;
    r.linhas.push_back(fmt("Tons: %.1f Hz e %.1f Hz", r.tomBaixoHz, r.tomAltoHz));
    r.linhas.push_back(fmt("Deslocamento (shift): %.0f Hz", r.deslocamento));
    if (r.nTons > 2)
        r.linhas.push_back(fmt("Atencao: achei %.0f picos - pode ser multitom (MFSK) ou haver outro sinal junto.", double(r.nTons)));
    if (r.baud > 5.0) {
        r.linhas.push_back(fmt("Velocidade: %.1f baud (raia %.0fx acima do fundo)", r.baud, baudConfianca));
        struct { const char* nome; double bd; } conhecidos[] = {
            {"RTTY amador (45,45)", 45.45}, {"50 baud", 50.0}, {"75 baud (naval)", 75.0},
            {"SITOR-B / NAVTEX / DSC (100)", 100.0}, {"110 baud", 110.0},
            {"200 baud", 200.0}, {"300 baud", 300.0}
        };
        for (auto& k : conhecidos)
            if (std::abs(r.baud - k.bd) < k.bd * 0.04)
                r.linhas.push_back(std::string("Compativel com: ") + k.nome);
    } else {
        r.linhas.push_back("Nao consegui medir a velocidade com seguranca.");
        r.linhas.push_back("Sem raia de relogio clara: sinal fraco, intermitente, ou nao e FSK simples.");
    }

    if (r.bitsUsados > 200) {
        r.linhas.push_back(fmt("Bits analisados: %.0f | proporcao de uns: %.3f | media das sequencias: %.2f",
                               double(r.bitsUsados), r.proporcaoUns, r.mediaCorridas));
        if (r.aleatoriedade > 0.80) {
            r.veredito = "conteudo parece CIFRADO";
            r.linhas.push_back("Conteudo: indistinguivel de bits aleatorios.");
            r.linhas.push_back("Isso e a assinatura de transmissao cifrada. Nao ha o que decodificar.");
        } else if (r.aleatoriedade > 0.55) {
            r.veredito = "indefinido";
            r.linhas.push_back("Conteudo: sem estrutura clara. Pode ser cifrado, ou o sinal esta fraco demais.");
        } else {
            r.veredito = "conteudo parece TEXTO";
            r.linhas.push_back("Conteudo: tem estrutura, nao e aleatorio.");
            r.linhas.push_back("Vale tentar um decodificador de texto com esses parametros.");
        }
    }
    return r;
}

} // namespace masdr
