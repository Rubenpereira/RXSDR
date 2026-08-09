#include "DscCore.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

namespace {

// Sequencia de fase: as posicoes DX levam sempre 125, as RX levam 111, 110,
// 109 ... 104. Sao esses valores que travam o alinhamento no inicio.
constexpr int kFaseDx     = 125;
constexpr int kFaseRxAlto = 111;
constexpr int kFaseRxBaixo = 104;

// Especificadores de formato validos (M.493). Uma mensagem SEMPRE comeca por
// um deles, repetido. E por isso que a mensagem e reconhecida pelo formato e
// nao por "o que vier depois da fase": antes dela vem o padrao de pontos, que
// as vezes fecha a contagem de verificacao por acaso e produz simbolo valido
// mas sem sentido. Comecar no formato descarta esse lixo sozinho.
bool ehFormato(int v)
{
    return v == 102 || v == 112 || v == 114 || v == 116 || v == 120 || v == 123;
}

bool ehFimDeSequencia(int v)
{
    return v == 117 || v == 122 || v == 127;
}

int contarUns(int v)
{
    int n = 0;
    while (v) { n += (v & 1); v >>= 1; }
    return n;
}

// Cinco simbolos de dois digitos formam os 10 digitos do endereco; o MMSI sao
// os 9 primeiros, o decimo e enchimento.
std::string mmsiDe(const std::vector<int>& m, size_t i)
{
    if (i + 5 > m.size()) return {};
    std::string d;
    for (size_t k = i; k < i + 5; ++k) {
        const int v = m[k];
        if (v < 0 || v > 99) return {};
        d += char('0' + v / 10);
        d += char('0' + v % 10);
    }
    return d.substr(0, 9);
}

} // namespace

// ---------------------------------------------------------------------------
//  Codificacao de simbolo (ITU-R M.493)
//  7 bits de informacao com o menos significativo na frente, seguidos de 3
//  bits com a contagem de zeros entre eles, com o mais significativo na
//  frente. Um simbolo em que a contagem nao bate foi corrompido no ar.
// ---------------------------------------------------------------------------
void DscCore::codificar(int valor, int bits[10])
{
    const int v = valor & 0x7F;
    for (int i = 0; i < 7; ++i) bits[i] = (v >> i) & 1;
    const int zeros = 7 - contarUns(v);
    bits[7] = (zeros >> 2) & 1;
    bits[8] = (zeros >> 1) & 1;
    bits[9] = zeros & 1;
}

int DscCore::decodificar(const int bits[10])
{
    int v = 0;
    for (int i = 0; i < 7; ++i) v |= (bits[i] & 1) << i;
    const int check = ((bits[7] & 1) << 2) | ((bits[8] & 1) << 1) | (bits[9] & 1);
    if (check != 7 - contarUns(v)) return -1;
    return v;
}

std::string DscCore::nomeSimbolo(int v)
{
    switch (v) {
        case 100: return "rotina";
        case 102: return "grupo";
        case 108: return "seguranca";
        case 110: return "urgencia";
        case 112: return "socorro";
        case 114: return "area geografica";
        case 116: return "todos os navios";
        case 120: return "chamada seletiva";
        case 123: return "estacao individual";
        default:  return {};
    }
}

// ---------------------------------------------------------------------------

DscCore::DscCore()               { setParams(Params{}); }
DscCore::DscCore(const Params& p){ setParams(p); }

void DscCore::setParams(const Params& p)
{
    p_ = p;
    amostrasPorBit_ = p_.sampleRate / p_.baudRate;

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

void DscCore::reset()
{
    janela_.assign(janelaLen_, 0.0f);
    janelaPos_ = 0;
    janelaCheia_ = false;
    fase_ = 0.0;
    ultimoD_ = 0.0f;
    bitsNo_ = 0;
    historico_.clear();
    idxRecebidos_ = 0;
    proximoDx_ = -1;
    faseDx_ = 0;
    sincronizado_ = false;
    desdeAvaliacao_ = 0;
    mensagem_.clear();
    coletando_ = false;
    saida_.clear();
    simbolos_ = validos_ = corrigidos_ = mensagens_ = 0;
}

// ---------------------------------------------------------------------------
//  Demodulacao FSK + recuperacao de temporizacao
// ---------------------------------------------------------------------------
std::string DscCore::feed(const float* samples, size_t n)
{
    if (!samples || n == 0) return {};

    for (size_t k = 0; k < n; ++k) {
        janela_[janelaPos_] = samples[k];
        janelaPos_ = (janelaPos_ + 1) % janelaLen_;
        if (janelaPos_ == 0) janelaCheia_ = true;
        if (!janelaCheia_) continue;

        float mI = 0, mQ = 0, sI = 0, sQ = 0;
        for (size_t i = 0; i < janelaLen_; ++i) {
            const float v = janela_[(janelaPos_ + i) % janelaLen_];
            mI += v * refMarkCos_[i];   mQ += v * refMarkSin_[i];
            sI += v * refSpaceCos_[i];  sQ += v * refSpaceSin_[i];
        }
        const float mark  = std::sqrt(mI * mI + mQ * mQ);
        const float space = std::sqrt(sI * sI + sQ * sQ);
        const float d = mark - space;

        // Toda transicao deveria cair na borda do bit; puxamos a fase para la.
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

void DscCore::processarBit(int bit)
{
    if (bitsNo_ < 10) {
        bitsBuf_[bitsNo_++] = bit;
    }
    if (bitsNo_ < 10) return;

    const int v = decodificar(bitsBuf_);

    if (!sincronizado_) {
        // Sem sincronismo deslizamos um bit por vez ate a contagem fechar.
        // Aceitar so simbolo valido evita travar em lixo.
        if (v >= 0) {
            processarSimbolo(v, true);
            bitsNo_ = 0;
        } else {
            for (int i = 0; i < 9; ++i) bitsBuf_[i] = bitsBuf_[i + 1];
            bitsNo_ = 9;
        }
    } else {
        processarSimbolo(v, v >= 0);
        bitsNo_ = 0;
    }
}

// ---------------------------------------------------------------------------
//  Fila de simbolos, sequencia de fase e diversidade DX/RX
// ---------------------------------------------------------------------------
void DscCore::processarSimbolo(int valor, bool valido)
{
    ++simbolos_;
    if (valido) ++validos_;

    historico_.push_back(valido ? valor : -1);
    const long long idx = idxRecebidos_++;

    const size_t maxHist = 256;
    while (historico_.size() > maxHist) historico_.pop_front();

    if (++desdeAvaliacao_ >= 4) {
        desdeAvaliacao_ = 0;
        avaliarFase();
    }
    if (!sincronizado_) return;

    auto base = [&]() { return idxRecebidos_ - (long long)historico_.size(); };
    auto emJanela = [&](long long i) { return i >= base() && i < idxRecebidos_; };
    auto valorEm  = [&](long long i) { return historico_[size_t(i - base())]; };

    // A copia RX de um caractere chega 9 posicoes depois da DX dele.
    while (proximoDx_ >= 0 && proximoDx_ + 9 <= idx
           && emJanela(proximoDx_) && emJanela(proximoDx_ + 9)) {

        const int dx = valorEm(proximoDx_);
        const int rx = valorEm(proximoDx_ + 9);

        int c;
        if (dx >= 0)      { c = dx; }
        else if (rx >= 0) { c = rx; ++corrigidos_; }
        else              { c = -1; }

        // Fora de mensagem, so o especificador de formato abre coleta. Isso
        // pula a sequencia de fase e o padrao de pontos sem precisar
        // adivinhar onde eles terminam.
        if (!coletando_) {
            if (c < 0 || !ehFormato(c)) { proximoDx_ += 2; continue; }
            coletando_ = true;
            mensagem_.clear();
        }
        mensagem_.push_back(c);

        // O formato vai repetido. Se a segunda copia nao confirma, o que
        // abriu a coleta era coincidencia - desiste sem sujar a saida.
        if (mensagem_.size() == 2 && mensagem_[1] != mensagem_[0]) {
            coletando_ = false;
            mensagem_.clear();
            proximoDx_ += 2;
            continue;
        }

        // Fim de sequencia, mais um simbolo de ECC, fecha a mensagem.
        if (mensagem_.size() >= 2) {
            const int penultimo = mensagem_[mensagem_.size() - 2];
            if (ehFimDeSequencia(penultimo)) {
                montarMensagem();
                coletando_ = false;
                mensagem_.clear();
            }
        }
        // Mensagem absurdamente longa: algo se perdeu, recomeca
        if (mensagem_.size() > 64) { coletando_ = false; mensagem_.clear(); }

        proximoDx_ += 2;
    }
}

// A sequencia de fase resolve o alinhamento de uma vez: as posicoes DX levam
// sempre 125. Basta ver de que lado da paridade eles caem.
void DscCore::avaliarFase()
{
    if (historico_.size() < 12) return;

    const long long base = idxRecebidos_ - (long long)historico_.size();
    int pontos[2] = {0, 0};
    for (size_t k = 0; k < historico_.size(); ++k) {
        if (historico_[k] != kFaseDx) continue;
        const long long i = base + (long long)k;
        pontos[int(i % 2)]++;
    }

    const int melhor = (pontos[1] > pontos[0]) ? 1 : 0;
    if (pontos[melhor] < 3) return;      // 125 isolado nao e sequencia de fase

    if (!sincronizado_ || melhor != faseDx_) {
        faseDx_ = melhor;
        sincronizado_ = true;
        coletando_ = false;
        mensagem_.clear();
        proximoDx_ = base;
        while (int(proximoDx_ % 2) != faseDx_) ++proximoDx_;
    }
}

// ---------------------------------------------------------------------------
//  Mensagem M.493
//  Formato (duas copias) | endereco | categoria | identificacao propria |
//  telecomandos | fim de sequencia | ECC
//
//  Nem toda variante segue esse desenho - socorro e area geografica tem campos
//  proprios. Quando o desenho nao encaixa, despejamos os simbolos crus em vez
//  de inventar interpretacao.
// ---------------------------------------------------------------------------
void DscCore::montarMensagem()
{
    const std::vector<int>& m = mensagem_;
    if (m.size() < 4) return;
    ++mensagens_;

    const int ecc = m.back();
    const size_t nEos = m.size() - 2;
    const int eos = m[nEos];

    // ECC = ou-exclusivo de tudo desde o formato ate o fim de sequencia. A
    // norma conta uma copia do formato; como o formato chega duplicado,
    // conferimos das duas maneiras e aceitamos se qualquer uma fechar.
    int x1 = 0, x2 = 0;
    for (size_t i = 0; i <= nEos; ++i) if (m[i] >= 0) x1 ^= m[i];
    for (size_t i = 1; i <= nEos; ++i) if (m[i] >= 0) x2 ^= m[i];
    const bool eccOk = (ecc >= 0) && (ecc == x1 || ecc == x2);

    const int fmt = m[0];
    std::string s = "formato ";
    const std::string nf = nomeSimbolo(fmt);
    s += nf.empty() ? ("desconhecido (" + std::to_string(fmt) + ")") : nf;

    // Pula as copias repetidas do especificador de formato
    size_t i = 0;
    while (i < m.size() && m[i] == fmt) ++i;

    // Formatos com endereco explicito
    if (fmt == 120 || fmt == 123 || fmt == 102) {
        const std::string para = mmsiDe(m, i);
        if (!para.empty()) { s += " | para " + para; i += 5; }
    }

    if (i < nEos) {
        const std::string nc = nomeSimbolo(m[i]);
        if (!nc.empty()) { s += " | " + nc; ++i; }
    }

    const std::string de = mmsiDe(m, i);
    if (!de.empty()) { s += " | de " + de; i += 5; }

    switch (eos) {
        case 117: s += " | pede confirmacao"; break;
        case 122: s += " | confirmacao";      break;
        default:  s += " | fim";              break;
    }
    s += eccOk ? " | ECC ok" : " | ECC FALHOU";

    // Simbolos crus: sempre uteis para conferir no ar, e a unica saida honesta
    // quando o desenho da mensagem nao e um dos previstos.
    s += " | [";
    for (size_t k = 0; k < m.size(); ++k) {
        if (k) s += ' ';
        s += (m[k] < 0) ? "??" : std::to_string(m[k]);
    }
    s += ']';

    emitir(s);
}

void DscCore::emitir(const std::string& linha)
{
    saida_ += linha;
    saida_ += '\n';
}

} // namespace masdr
