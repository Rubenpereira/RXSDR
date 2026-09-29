#include "AleCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace masdr {

namespace {

// Tons em ordem de frequencia (rank 0 = 750 Hz) -> valor do simbolo (Gray),
// MIL-STD-188-141 A.5.1.2.
constexpr int kRankParaSimbolo[8] = {0, 1, 3, 2, 6, 7, 5, 4};

// Golay (24,12) do ALE: palavra = info<<12 | paridade(info), e a paridade e
// o XOR das colunas abaixo para cada bit de info ligado (bit i -> kBase[i]).
constexpr uint16_t kBase[12] = {
    0x5C7, 0xB8D, 0x2DE, 0x5BC, 0xB78, 0x337,
    0x66D, 0xCD9, 0xC76, 0xD2B, 0xF92, 0xAE3
};

uint16_t paridade(uint16_t info)
{
    uint16_t p = 0;
    for (int i = 0; i < 12; ++i)
        if (info & (1u << i)) p ^= kBase[i];
    return p;
}

int bits(uint32_t v)
{
    int c = 0;
    while (v) { v &= v - 1; ++c; }
    return c;
}

// Sindrome (12 bits) -> padrao de erro de peso <= 3. O Golay estendido tem
// distancia 8: ate 3 erros o padrao e unico; 4 so e detectado.
struct TabelaGolay {
    int32_t erro[4096];
    TabelaGolay()
    {
        std::fill(std::begin(erro), std::end(erro), -1);
        auto sind = [](uint32_t e) {
            return uint16_t(paridade(uint16_t(e >> 12)) ^ (e & 0xFFF));
        };
        erro[0] = 0;
        for (int a = 0; a < 24; ++a) {
            const uint32_t e1 = 1u << a;
            if (erro[sind(e1)] < 0) erro[sind(e1)] = int32_t(e1);
        }
        for (int a = 0; a < 24; ++a)
            for (int b = a + 1; b < 24; ++b) {
                const uint32_t e2 = (1u << a) | (1u << b);
                if (erro[sind(e2)] < 0) erro[sind(e2)] = int32_t(e2);
            }
        for (int a = 0; a < 24; ++a)
            for (int b = a + 1; b < 24; ++b)
                for (int c = b + 1; c < 24; ++c) {
                    const uint32_t e3 = (1u << a) | (1u << b) | (1u << c);
                    if (erro[sind(e3)] < 0) erro[sind(e3)] = int32_t(e3);
                }
    }
};
const TabelaGolay& tabela() { static const TabelaGolay t; return t; }

// cos/sen de 2*pi*k/(64*kSub): o bin k da grade (250 + 62,5*k Hz) anda
// (4+k)/(64*kSub) de volta por amostra a 8 kHz - sempre um inteiro da tabela.
struct TabelaFase {
    static constexpr int N = 64 * AleCore::kSub;
    float c[N], s[N];
    TabelaFase()
    {
        for (int k = 0; k < N; ++k) {
            c[k] = float(std::cos(2.0 * 3.14159265358979323846 * k / N));
            s[k] = float(std::sin(2.0 * 3.14159265358979323846 * k / N));
        }
    }
};
const TabelaFase& fases() { static const TabelaFase t; return t; }

bool basico38(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '@' || c == '?';
}
bool expandido64(char c) { return c >= 0x20 && c <= 0x5F; }

const char* nomeTipo(int t)
{
    static const char* n[8] = {"DATA", "THRU", "TO", "TWAS", "FROM", "TIS", "CMD", "REP"};
    return (t >= 0 && t < 8) ? n[t] : "?";
}

} // namespace

uint32_t AleCore::golayCodifica(uint16_t info)
{
    return (uint32_t(info & 0xFFF) << 12) | paridade(info & 0xFFF);
}

bool AleCore::golayDecodifica(uint32_t cw, uint16_t& info, int& erros)
{
    const uint16_t s = uint16_t(paridade(uint16_t((cw >> 12) & 0xFFF)) ^ (cw & 0xFFF));
    const int32_t e = tabela().erro[s];
    if (e < 0) { erros = 4; return false; }
    const uint32_t certo = (cw ^ uint32_t(e)) & 0xFFFFFF;
    info  = uint16_t(certo >> 12);
    erros = bits(uint32_t(e));
    return true;
}

AleCore::AleCore()
{
    (void)tabela();
    (void)fases();
    reset();
}

void AleCore::reset()
{
    logE_.assign(size_t(kHist) * kNb, -30.0f);
    dura_.assign(size_t(kHist) * kNd, 0);
    n_ = 0;
    ultimaJanela_ = -1;
    bufI_.assign(size_t(kNb) * kSps, 0.0f);
    bufQ_.assign(size_t(kNb) * kSps, 0.0f);
    somaI_.assign(kNb, 0.0);
    somaQ_.assign(kNb, 0.0);
    fase_.assign(kNb, 0);
    pos_ = 0;
    travado_ = false;
    desvio_ = 0;
    ultimoDesvio_ = 0;
    desvioChamada_ = 0;
    proxBusca_ = 0;
    candidato_ = -1;
    candDesvio_ = 0;
    tPalavra_ = 0;
    chamada_.clear();
    totalPalavras_ = 0;
    totalChamadas_ = 0;
}

// Uma amostra nova: atualiza os correlatores da grade e grava, para a janela
// de 64 amostras que termina aqui (comeca em n-63), o log da energia de cada
// bin e - para cada deslocamento de tom - qual dos 8 tons ficou mais forte.
void AleCore::amostra(float x)
{
    const TabelaFase& f = fases();
    constexpr int N = TabelaFase::N;
    for (int k = 0; k < kNb; ++k) {
        const int ph = fase_[k];
        const float i = x * f.c[ph];
        const float q = -x * f.s[ph];
        fase_[k] = (ph + 4 + k) % N;
        const size_t o = size_t(k) * kSps + size_t(pos_);
        somaI_[k] += double(i) - double(bufI_[o]);
        somaQ_[k] += double(q) - double(bufQ_[o]);
        bufI_[o] = i;
        bufQ_[o] = q;
    }
    pos_ = (pos_ + 1) % kSps;
    ++n_;
    // Refaz as somas do zero de vez em quando: somar e subtrair float por
    // horas acumula erro de arredondamento.
    if ((n_ & 0xFFFF) == 0) {
        for (int k = 0; k < kNb; ++k) {
            double si = 0, sq = 0;
            for (int j = 0; j < kSps; ++j) {
                si += bufI_[size_t(k) * kSps + j];
                sq += bufQ_[size_t(k) * kSps + j];
            }
            somaI_[k] = si; somaQ_[k] = sq;
        }
    }
    if (n_ < kSps) return;
    const long long w = n_ - kSps;          // inicio da janela que acabou de fechar
    float* L = &logE_[size_t(w % kHist) * kNb];
    for (int k = 0; k < kNb; ++k)
        L[k] = std::log(float(somaI_[k] * somaI_[k] + somaQ_[k] * somaQ_[k]) + 1e-9f);
    uint8_t* D = &dura_[size_t(w % kHist) * kNd];
    for (int d = 0; d < kNd; ++d) {
        const int desvio = d - kDesvioMax;
        int melhor = 0;
        float vm = L[binDoTom(0, desvio)];
        for (int r = 1; r < 8; ++r) {
            const float v = L[binDoTom(r, desvio)];
            if (v > vm) { vm = v; melhor = r; }
        }
        D[d] = uint8_t(kRankParaSimbolo[melhor]);
    }
    ultimaJanela_ = w;
}

// Le a palavra cujos 49 simbolos comecam em t0, com os tons deslocados de
// "desvio" passos da grade. Com soQuandoUnanime, faz antes o teste barato
// (decisao dura ja guardada) e desiste cedo se nao parecer ALE.
AleCore::Palavra AleCore::avaliar(long long t0, int desvio, bool soQuandoUnanime) const
{
    Palavra p;
    const long long tFim = t0 + 48LL * kSps;
    if (t0 < 0 || tFim > ultimaJanela_ || t0 <= ultimaJanela_ - kHist) return p;
    if (desvio < -kDesvioMax || desvio > kDesvioMax) return p;
    const int d = desvio + kDesvioMax;

    int dura[49];
    for (int k = 0; k < 49; ++k)
        dura[k] = dura_[size_t((t0 + k * kSps) % kHist) * kNd + d];
    int unanimes = 0;
    for (int b = 0; b < 48; ++b) {
        int uns = 0;
        for (int c = 0; c < 3; ++c) {
            const int pb = b + c * 49;
            uns += (dura[pb / 3] >> (2 - pb % 3)) & 1;
        }
        if (uns == 0 || uns == 3) ++unanimes;
    }
    p.unanimes = unanimes;
    if (soQuandoUnanime && unanimes < 24) return p;

    // voto "suave": para cada bit de cada tom, o quanto o melhor tom com o
    // bit em 1 e mais forte que o melhor com o bit em 0 (em log)
    int binSimbolo[8];
    for (int r = 0; r < 8; ++r) binSimbolo[kRankParaSimbolo[r]] = binDoTom(r, desvio);
    float llr[49][3];
    float energia = 0;
    for (int k = 0; k < 49; ++k) {
        const float* L = &logE_[size_t((t0 + k * kSps) % kHist) * kNb];
        float v[8];
        for (int s = 0; s < 8; ++s) v[s] = L[binSimbolo[s]];
        energia += *std::max_element(v, v + 8);
        for (int lane = 0; lane < 3; ++lane) {
            float m1 = -1e30f, m0 = -1e30f;
            for (int s = 0; s < 8; ++s) {
                if ((s >> lane) & 1) m1 = std::max(m1, v[s]);
                else                 m0 = std::max(m0, v[s]);
            }
            llr[k][lane] = m1 - m0;
        }
    }
    uint64_t tx = 0;
    for (int b = 0; b < 48; ++b) {
        float soma = 0;
        for (int c = 0; c < 3; ++c) {
            const int pb = b + c * 49;
            soma += llr[pb / 3][2 - pb % 3];
        }
        if (soma > 0) tx |= (1ULL << b);
    }
    // desentrelaca: bits pares -> codigo A, impares -> codigo B (MSB primeiro)
    uint32_t a = 0, bb = 0;
    for (int k = 0; k < 24; ++k) {
        if ((tx >> (2 * k)) & 1ULL)     a  |= 1u << (23 - k);
        if ((tx >> (2 * k + 1)) & 1ULL) bb |= 1u << (23 - k);
    }
    bb = (bb & 0xFFF000u) | (~bb & 0xFFFu);   // o codigo B vai com a paridade invertida
    uint16_t ia = 0, ib = 0;
    int ea = 0, eb = 0;
    if (!golayDecodifica(a, ia, ea) || !golayDecodifica(bb, ib, eb)) return p;
    const uint32_t w24 = (uint32_t(ia) << 12) | ib;
    p.ok    = true;
    p.energia = energia;
    p.erros = ea + eb;
    p.tipo  = int((w24 >> 21) & 7);
    p.c[0]  = char((w24 >> 14) & 0x7F);
    p.c[1]  = char((w24 >> 7) & 0x7F);
    p.c[2]  = char(w24 & 0x7F);
    return p;
}

bool AleCore::caracteresValidos(const Palavra& p) const
{
    switch (p.tipo) {
    case 1: case 2: case 3: case 4: case 5:          // enderecos: 38 caracteres
        return basico38(p.c[0]) && basico38(p.c[1]) && basico38(p.c[2]);
    case 0: case 7: {                                 // DATA / REP
        // Depois de um CMD o conteudo pode ser binario (LQA, comandos).
        if (travado_ && !chamada_.empty()) {
            for (auto it = chamada_.rbegin(); it != chamada_.rend(); ++it)
                if (it->tipo != 0 && it->tipo != 7) {
                    if (it->tipo == 6) return true;
                    break;
                }
        }
        return expandido64(p.c[0]) && expandido64(p.c[1]) && expandido64(p.c[2]);
    }
    case 6:                                           // CMD
        return p.c[0] >= 0x20;
    }
    return false;
}

// Qual leitura e melhor: menos bits corrigidos; empate, mais votos unanimes;
// empate de novo, mais energia nos tons. A energia desempata o deslocamento
// de tom: meio passo (62,5 Hz) ao lado do certo ainda le os mesmos simbolos,
// so que mais fraco - e as vezes le lixo que passa no Golay.
bool AleCore::melhorQue(const Palavra& a, const Palavra& b)
{
    if (!b.ok) return a.ok;
    if (!a.ok) return false;
    if (a.erros != b.erros) return a.erros < b.erros;
    if (a.unanimes != b.unanimes) return a.unanimes > b.unanimes;
    return a.energia > b.energia;
}

AleCore::Palavra AleCore::melhorPerto(long long t, int raio, int desvio, int raioDesvio,
                                      long long& tMelhor, int& desvioMelhor) const
{
    Palavra melhor;
    tMelhor = t;
    desvioMelhor = desvio;
    for (int dv = desvio - raioDesvio; dv <= desvio + raioDesvio; ++dv)
        for (int d = -raio; d <= raio; ++d) {
            const Palavra p = avaliar(t + d, dv, true);
            if (!p.ok || !caracteresValidos(p)) continue;
            if (melhorQue(p, melhor)) {
                melhor = p;
                tMelhor = t + d;
                desvioMelhor = dv;
            }
        }
    return melhor;
}

std::vector<std::string> AleCore::feed(const float* s, size_t n)
{
    std::vector<std::string> saida;
    for (size_t i = 0; i < n; ++i) {
        amostra(s[i]);
        if (ultimaJanela_ < 0) continue;
        const long long w = ultimaJanela_;

        if (travado_) {
            const long long tProx = tPalavra_ + kW;
            if (tProx + 4 + 48LL * kSps > w) continue;
            long long tb; int db;
            // o tom da chamada ja e conhecido; deixa so 62,5 Hz de folga
            Palavra p = melhorPerto(tProx, 4, desvio_, 0, tb, db);
            {
                long long tb2; int db2;
                const Palavra q = melhorPerto(tProx, 4, desvio_, 1, tb2, db2);
                if (q.ok && (!p.ok || q.erros < p.erros)) { p = q; tb = tb2; db = db2; }
            }
            if (p.ok && p.unanimes >= 24) {
                chamada_.push_back(p);
                ++totalPalavras_;
                tPalavra_ = tb;
                desvio_ = db;
            } else {
                fecharChamada(saida);
                travado_   = false;
                // Recomeca a procura logo depois da ultima palavra aceita, e nao
                // no fim do compasso: se aquela leitura era falsa, a chamada de
                // verdade pode estar comecando no meio do caminho.
                proxBusca_ = tPalavra_ + kSps;
            }
            continue;
        }

        if (candidato_ >= 0) {
            if (candidato_ + 16 + 48LL * kSps > w) continue;
            long long tb; int db;
            // Compara TODOS os deslocamentos em volta: com sinal forte, ler um
            // tom ao lado (250 Hz) so embaralha os simbolos e as vezes forma
            // palavra valida. No deslocamento certo a leitura e sempre melhor.
            const Palavra p = melhorPerto(candidato_, 16, 0, kDesvioMax, tb, db);
            if (p.ok && p.erros <= 3 && p.unanimes >= 30 && ehEndereco(p.tipo)) {
                travado_  = true;
                tPalavra_ = tb;
                desvio_   = db;
                desvioChamada_ = db;
                chamada_.assign(1, p);
                ++totalPalavras_;
                // A procura so trava num endereco. Se a recepcao pegou a
                // chamada pelo meio, as palavras logo antes (o texto de um
                // AMD, um CMD, o resto de um endereco) ainda estao no
                // historico: volta ate 5 palavras no mesmo tom e recupera as
                // que passarem.
                for (int k = 1; k <= 5; ++k) {
                    long long tv; int dv2;
                    const Palavra a = melhorPerto(tb - k * kW, 4, db, 0, tv, dv2);
                    if (!a.ok || a.unanimes < 24 || a.erros > 3) break;
                    chamada_.insert(chamada_.begin(), a);
                    ++totalPalavras_;
                }
            } else {
                proxBusca_ = candidato_ + 2;
            }
            candidato_ = -1;
            continue;
        }

        // procura, em todos os deslocamentos de tom (+-500 Hz)
        if (proxBusca_ < w - kHist + kW) proxBusca_ = w - kHist + kW;
        // Uma chamada so comeca por endereco (TO, TIS, TWAS, THRU, FROM): no
        // chiado, com 17 deslocamentos testados, uma palavra solta de CMD ou
        // DATA passava no Golay de vez em quando.
        while (proxBusca_ + 48LL * kSps <= w && candidato_ < 0) {
            Palavra melhor;
            int dMelhor = 0;
            for (int dv = -kDesvioMax; dv <= kDesvioMax; ++dv) {
                const Palavra p = avaliar(proxBusca_, dv, true);
                if (p.ok && p.erros <= 3 && p.unanimes >= 30 && ehEndereco(p.tipo)
                    && caracteresValidos(p) && melhorQue(p, melhor)) {
                    melhor = p;
                    dMelhor = dv;
                }
            }
            if (melhor.ok) { candidato_ = proxBusca_; candDesvio_ = dMelhor; }
            else proxBusca_ += 2;
        }
    }
    return saida;
}

void AleCore::fecharChamada(std::vector<std::string>& saida)
{
    if (chamada_.empty()) return;
    bool temEndereco = false;
    for (const Palavra& p : chamada_) temEndereco |= ehEndereco(p.tipo);
    // Chamada real tem varias palavras (TO... TIS..., sondagem TIS repetido).
    // Uma palavra sozinha so vale se veio perfeita.
    const bool sozinhaDuvidosa = chamada_.size() == 1
        && (chamada_[0].erros > 0 || chamada_[0].unanimes < 44);
    if (!temEndereco || sozinhaDuvidosa) {
        totalPalavras_ -= int(chamada_.size());
        chamada_.clear();
        return;
    }
    ++totalChamadas_;
    ultimoDesvio_ = desvioChamada_;     // so conta o tom de chamada que saiu
    saida.push_back(textoDaChamada(chamada_));
    chamada_.clear();
}

// Junta as palavras em campos, como os decodificadores de mesa mostram:
// DATA/REP continuam o campo anterior (enderecos longos, texto do AMD).
std::string AleCore::textoDaChamada(const std::vector<Palavra>& ps)
{
    struct Campo { std::string rotulo, texto; };
    std::vector<Campo> campos;
    auto limpo = [](char c) { return (c >= 0x20 && c < 0x7F) ? c : '.'; };

    for (const Palavra& p : ps) {
        if ((p.tipo == 0 || p.tipo == 7) && !campos.empty()) {
            if (campos.back().rotulo.rfind("LQA", 0) == 0) continue;   // LQA ja fechado
            for (char c : p.c) campos.back().texto += limpo(c);
            continue;
        }
        Campo c;
        if (p.tipo == 6 && p.c[0] == 'a') {
            // LQA (tabela A-XIV): 'a' + KA1, MP(3), SINAD(5), BER(5)
            const uint32_t v = (uint32_t(uint8_t(p.c[1])) << 7) | uint8_t(p.c[2]);
            char b[48];
            std::snprintf(b, sizeof b, "LQA BER %u SINAD %u", unsigned(v & 0x1F),
                          unsigned((v >> 5) & 0x1F));
            c.rotulo = b;
        } else if (p.tipo == 6) {
            const bool amd = expandido64(p.c[0]) && expandido64(p.c[1]) && expandido64(p.c[2]);
            if (amd) c.rotulo = "CMD AMD";
            else {
                char b[32];
                std::snprintf(b, sizeof b, "CMD %c(0x%02X)", limpo(p.c[0]), unsigned(uint8_t(p.c[0])));
                c.rotulo = b;
            }
            if (amd) for (char ch : p.c) c.texto += limpo(ch);
            else     for (int k = 1; k < 3; ++k) c.texto += limpo(p.c[k]);
        } else {
            c.rotulo = nomeTipo(p.tipo);
            for (char ch : p.c) c.texto += limpo(ch);
        }
        campos.push_back(c);
    }

    // enderecos: o '@' no fim e so enchimento (mas "@@?" e o proprio endereco)
    std::vector<std::string> blocos;
    for (Campo& c : campos) {
        std::string t = c.texto;
        if (c.rotulo.rfind("CMD", 0) != 0 && t.find_first_not_of("@?") != std::string::npos)
            while (!t.empty() && t.back() == '@') t.pop_back();
        while (!t.empty() && t.back() == ' ') t.pop_back();
        blocos.push_back("[" + c.rotulo + "]" + (t.empty() ? "" : "[" + t + "]"));
    }

    // "[TO][@@?]" 40 vezes numa chamada de varredura vira "[TO][@@?] x40"
    std::string out;
    for (size_t i = 0; i < blocos.size();) {
        size_t j = i + 1;
        while (j < blocos.size() && blocos[j] == blocos[i]) ++j;
        const size_t rep = j - i;
        if (rep > 3) out += blocos[i] + " x" + std::to_string(rep);
        else for (size_t k = 0; k < rep; ++k) out += blocos[i];
        if (j < blocos.size() && rep > 3) out += " ";
        i = j;
    }
    return out;
}

} // namespace masdr
