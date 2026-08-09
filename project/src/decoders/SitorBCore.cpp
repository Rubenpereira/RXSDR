#include "SitorBCore.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

// ---------------------------------------------------------------------------
//  CCIR 476
//  Cada caractere tem exatamente 4 bits em '1' e 3 em '0' (codigo de peso
//  constante 4/7). E essa propriedade que permite detectar erro sem FEC.
//  A implementacao anterior testava contra 3 bits, aceitando o conjunto errado.
//  Tabela: codigo de 7 bits -> indice ITA2 de 5 bits.
// ---------------------------------------------------------------------------
namespace {

struct Ccir476Entrada { uint8_t code; int ita2; };

// Ordem dos bits conforme a recomendacao: B1..B7, sendo o codigo montado
// como (B1<<6)|...|B7.
const Ccir476Entrada kTabela[] = {
    {0x0F,  0}, {0x71,  1}, {0x1C,  2}, {0x53,  3}, {0x65,  4},
    {0x1B,  5}, {0x27,  6}, {0x2D,  7}, {0x4B,  8}, {0x72,  9},
    {0x39, 10}, {0x36, 11}, {0x2E, 12}, {0x5A, 13}, {0x74, 14},
    {0x69, 15}, {0x3C, 16}, {0x4D, 17}, {0x56, 18}, {0x47, 19},
    {0x63, 20}, {0x5C, 21}, {0x2B, 22}, {0x35, 23}, {0x6C, 24},
    {0x59, 25}, {0x1E, 26}, {0x33, 27}, {0x4E, 28}, {0x66, 29},
    {0x6A, 30}, {0x78, 31},
};

// Alfabeto ITA2 (Baudot). Indice 0..31.
const char kLetras[32] = {
    '\0','E','\n','A',' ','S','I','U','\r','D','R','J','N','F','C','K',
    'T','Z','L','W','H','Y','P','Q','O','B','G','\0','M','X','V','\0'
};
const char kFiguras[32] = {
    '\0','3','\n','-',' ','\'','8','7','\r','\x05','4','\a',',','!',':','(',
    '5','+',')','2','#','6','0','1','9','?','&','\0','.','/',';','\0'
};

constexpr int kIta2Letras  = 31;   // troca para letras
constexpr int kIta2Figuras = 27;   // troca para figuras

int contarUns(uint8_t v)
{
    int n = 0;
    while (v) { n += (v & 1); v >>= 1; }
    return n;
}

} // namespace

bool SitorBCore::ccir476Valido(uint8_t code)
{
    // Peso constante: exatamente 4 bits em '1' nos 7 bits uteis.
    return (code & 0x80) == 0 && contarUns(code) == 4;
}

int SitorBCore::ccir476ParaIta2(uint8_t code)
{
    if (!ccir476Valido(code)) return -1;
    for (const auto& e : kTabela)
        if (e.code == code) return e.ita2;
    return -1;
}

// ---------------------------------------------------------------------------

SitorBCore::SitorBCore()
{
    setParams(Params{});
}

SitorBCore::SitorBCore(const Params& p)
{
    setParams(p);
}

void SitorBCore::setParams(const Params& p)
{
    p_ = p;
    amostrasPorBit_ = p_.sampleRate / p_.baudRate;

    // Janela dos correlatores: um periodo de bit. Menos que isso perde
    // seletividade entre mark e space, que estao a apenas 170 Hz.
    janelaLen_ = static_cast<size_t>(std::lround(amostrasPorBit_));
    if (janelaLen_ < 8) janelaLen_ = 8;

    const double markHz  = p_.centerFreq + p_.shift / 2.0;
    const double spaceHz = p_.centerFreq - p_.shift / 2.0;

    refMarkCos_.resize(janelaLen_);  refMarkSin_.resize(janelaLen_);
    refSpaceCos_.resize(janelaLen_); refSpaceSin_.resize(janelaLen_);
    for (size_t i = 0; i < janelaLen_; ++i) {
        const double t = double(i) / p_.sampleRate;
        refMarkCos_[i]  = float(std::cos(2.0 * M_PI * markHz  * t));
        refMarkSin_[i]  = float(std::sin(2.0 * M_PI * markHz  * t));
        refSpaceCos_[i] = float(std::cos(2.0 * M_PI * spaceHz * t));
        refSpaceSin_[i] = float(std::sin(2.0 * M_PI * spaceHz * t));
    }
    reset();
}

void SitorBCore::reset()
{
    janela_.assign(janelaLen_, 0.0f);
    janelaPos_ = 0;
    janelaCheia_ = false;
    fase_ = 0.0;
    ultimoD_ = 0.0f;
    shiftReg_ = 0;
    bitsNoReg_ = 0;
    historico_.clear();
    idxRecebidos_ = 0;
    proximoDx_    = -1;
    faseDx_       = 0;
    vaoFec_       = 11;
    sincronizado_ = false;
    desdeAvaliacao_ = 0;
    figuras_ = false;
    saida_.clear();
    totalChars_ = validChars_ = corrigidos_ = 0;
}

// ---------------------------------------------------------------------------
//  Demodulacao FSK + recuperacao de temporizacao
// ---------------------------------------------------------------------------
std::string SitorBCore::feed(const float* samples, size_t n)
{
    if (!samples || n == 0) return {};

    for (size_t k = 0; k < n; ++k) {
        janela_[janelaPos_] = samples[k];
        janelaPos_ = (janelaPos_ + 1) % janelaLen_;
        if (janelaPos_ == 0) janelaCheia_ = true;
        if (!janelaCheia_) continue;

        // Energia em cada tom sobre a janela de 1 bit
        float mI = 0, mQ = 0, sI = 0, sQ = 0;
        for (size_t i = 0; i < janelaLen_; ++i) {
            const float v = janela_[(janelaPos_ + i) % janelaLen_];
            mI += v * refMarkCos_[i];   mQ += v * refMarkSin_[i];
            sI += v * refSpaceCos_[i];  sQ += v * refSpaceSin_[i];
        }
        const float mark  = std::sqrt(mI * mI + mQ * mQ);
        const float space = std::sqrt(sI * sI + sQ * sQ);
        const float d = mark - space;

        // Sincronismo de bit por transicao: toda vez que o sinal cruza zero,
        // a transicao deveria cair na BORDA do bit. Puxamos a fase para la.
        if ((d >= 0.0f) != (ultimoD_ >= 0.0f)) {
            if (fase_ < amostrasPorBit_ / 2.0) fase_ += amostrasPorBit_ * 0.05;
            else                               fase_ -= amostrasPorBit_ * 0.05;
        }
        ultimoD_ = d;

        fase_ += 1.0;
        if (fase_ >= amostrasPorBit_) {
            fase_ -= amostrasPorBit_;
            int bit = (d >= 0.0f) ? 1 : 0;
            if (p_.invert) bit = 1 - bit;
            processarBit(bit);
        }
    }

    std::string r;
    r.swap(saida_);
    return r;
}

void SitorBCore::processarBit(int bit)
{
    shiftReg_ = ((shiftReg_ << 1) | uint32_t(bit)) & 0x7F;
    if (++bitsNoReg_ < 7) return;

    // Alinhamento de caractere: enquanto nao ha sincronismo, deslizamos um bit
    // por vez ate aparecer um codigo valido; com sincronismo, avancamos de 7
    // em 7. Sem isso o fluxo nunca casa com a fronteira do caractere.
    const uint8_t code = uint8_t(shiftReg_);
    if (!sincronizado_) {
        if (ccir476Valido(code)) {
            processarCaractere(code);
            bitsNoReg_ = 0;
        } else {
            bitsNoReg_ = 6;   // desliza um bit
        }
    } else {
        processarCaractere(code);
        bitsNoReg_ = 0;
    }
}

// ---------------------------------------------------------------------------
//  FEC do SITOR-B: diversidade temporal DX/RX
//
//  O fluxo alterna uma copia DX e uma copia RX; a RX de um caractere aparece
//  um numero fixo de posicoes depois da DX correspondente. Emitir tudo que
//  chega duplica o texto - era o erro da primeira versao. Aqui esperamos as
//  DUAS copias e emitimos UMA vez: vale a DX se ela fechar o CCIR 476, senao
//  a RX. Fase e vao sao descobertos sozinhos, porque a convencao varia.
// ---------------------------------------------------------------------------
void SitorBCore::processarCaractere(uint8_t code)
{
    ++totalChars_;
    historico_.push_back(code);
    const long long idx = idxRecebidos_++;

    // Mantem so o necessario para olhar para tras
    const size_t maxHist = 128;
    while (historico_.size() > maxHist) historico_.pop_front();

    auto emJanela = [&](long long i) -> bool {
        const long long base = idxRecebidos_ - (long long)historico_.size();
        return i >= base && i < idxRecebidos_;
    };
    auto codigoEm = [&](long long i) -> uint8_t {
        const long long base = idxRecebidos_ - (long long)historico_.size();
        return historico_[size_t(i - base)];
    };

    // ---- descoberta de fase e vao -------------------------------------
    // Avalia cedo e com frequencia: quanto antes travar a fase, antes o
    // texto comeca a sair. 32 caracteres ja dao pares DX/RX suficientes.
    if (++desdeAvaliacao_ >= 8 && historico_.size() >= 32) {
        desdeAvaliacao_ = 0;
        int melhorPontos = 0, melhorFase = faseDx_, melhorVao = vaoFec_;
        const long long base = idxRecebidos_ - (long long)historico_.size();
        for (int vao : {9, 11}) {
            for (int fase = 0; fase < 2; ++fase) {
                int pontos = 0;
                for (long long i = base + vao; i < idxRecebidos_; ++i) {
                    if (int(i % 2) == fase) continue;            // so posicoes RX
                    const uint8_t rx = codigoEm(i);
                    const uint8_t dx = codigoEm(i - vao);
                    if (rx == dx && ccir476Valido(rx)) ++pontos;
                }
                if (pontos > melhorPontos) {
                    melhorPontos = pontos; melhorFase = fase; melhorVao = vao;
                }
            }
        }
        if (melhorPontos > 0) {
            faseDx_ = melhorFase;
            vaoFec_ = melhorVao;
            sincronizado_ = true;
            if (proximoDx_ < 0) {
                // primeira DX pendente dentro da janela
                const long long base2 = idxRecebidos_ - (long long)historico_.size();
                proximoDx_ = base2;
                while (int(proximoDx_ % 2) != faseDx_) ++proximoDx_;
            }
        }
    }

    if (!sincronizado_) return;   // ainda procurando o alinhamento

    // ---- emissao: so quando a copia RX ja chegou -----------------------
    while (proximoDx_ >= 0
           && proximoDx_ + vaoFec_ <= idx
           && emJanela(proximoDx_)
           && emJanela(proximoDx_ + vaoFec_)) {

        const uint8_t dx = codigoEm(proximoDx_);
        const uint8_t rx = codigoEm(proximoDx_ + vaoFec_);

        if (ccir476Valido(dx)) {
            ++validChars_;
            emitirIta2(ccir476ParaIta2(dx));
        } else if (ccir476Valido(rx)) {
            ++validChars_;
            ++corrigidos_;                    // salvo pela copia repetida
            emitirIta2(ccir476ParaIta2(rx));
        } else {
            saida_ += '_';                    // as duas falharam
        }
        proximoDx_ += 2;                      // proxima posicao DX
    }
}

void SitorBCore::emitirIta2(int ita2)
{
    if (ita2 < 0 || ita2 > 31) return;
    if (ita2 == kIta2Letras)  { figuras_ = false; return; }
    if (ita2 == kIta2Figuras) { figuras_ = true;  return; }

    const char c = figuras_ ? kFiguras[ita2] : kLetras[ita2];
    if (c != '\0') saida_ += c;
}

} // namespace masdr
