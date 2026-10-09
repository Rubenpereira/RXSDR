#include "PactorCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "AnaliseCore.h"

namespace masdr {

namespace {
constexpr double kPi = 3.14159265358979323846;
// Huffman do PACTOR-I (ASCII 0..127), bits na ordem em que sao transmitidos
const char* const kHuffman[128] = {
    "111100111000110", "111100111000101", "111100111000100", "111100111000011",
    "111100111000010", "111100111000001", "111100111000000", "111100001111111",
    "111100001111110", "111100001111101", "001101", "111100001111100",
    "111100001111011", "001100", "111100001111010", "111100001111001",
    "111100001110111", "111100001110110", "111100001110101", "111100001110100",
    "111100001110011", "111100001110010", "111100001110001", "111100001110000",
    "111100001101111", "111100001101110", "111100111000111", "111100001101101",
    "1100011001", "111100001101100", "111100001111000", "110001101111111",
    "10", "11110011101", "110001101100", "0010100011011",
    "0001010111001", "110001101101", "111100111001", "110001101110",
    "110011011", "110011100", "001010001100", "111100111110",
    "1100101", "00010101111", "1100100", "11110011110",
    "11000111", "001010000", "0001011010", "0001011011",
    "0001011100", "0001010101", "0001011101", "0001011110",
    "0001011111", "0001010010", "00101000111", "11110000110100",
    "0001010111010", "1111000010", "111100111111", "1100110101",
    "0001010111000", "00101001", "11001111", "11110001",
    "0001101", "11000000", "11001100", "00010100111",
    "0010100010", "11110010", "1100000110", "1100110100",
    "110011101", "111101010", "111100000", "000101000",
    "000101100", "1111010111", "110000010", "1111011",
    "11110100", "1100000111", "1100011000", "0001010100",
    "0001010111011", "00101000110101", "111100110", "001010001101000",
    "11110000110101", "001010001101001", "110001101111110", "111100001100",
    "110001101111101", "01000", "0000110", "010011",
    "00111", "011", "0000111", "000111",
    "000100", "1101", "00010100110", "0010101",
    "000010", "001011", "0101", "010010",
    "11000010", "1111010110", "1110", "00100",
    "00000", "11111", "11000011", "0001100",
    "1100011010", "0001010110", "1100010", "110001101111100",
    "110001101111011", "110001101111010", "110001101111001", "110001101111000",
};

struct NoHuff { int filho[2] = {-1, -1}; int ch = -1; };
const std::vector<NoHuff>& arvoreHuffman()
{
    static const std::vector<NoHuff> a = [] {
        std::vector<NoHuff> v(1);
        for (int c = 0; c < 128; ++c) {
            int n = 0;
            for (const char* p = kHuffman[c]; *p; ++p) {
                const int b = *p == '1';
                if (v[size_t(n)].filho[b] < 0) { v[size_t(n)].filho[b] = (int)v.size(); v.emplace_back(); }
                n = v[size_t(n)].filho[b];
            }
            v[size_t(n)].ch = c;
        }
        return v;
    }();
    return a;
}

uint16_t crcRefletido(const std::vector<uint8_t>& b, uint16_t ini)
{
    uint16_t c = ini;
    for (uint8_t x : b) { c ^= x; for (int i = 0; i < 8; ++i) c = (c & 1) ? uint16_t((c >> 1) ^ 0x8408) : uint16_t(c >> 1); }
    return c;
}
uint16_t crcNormal(const std::vector<uint8_t>& b, uint16_t ini)
{
    uint16_t c = ini;
    for (uint8_t x : b) { c ^= uint16_t(x) << 8; for (int i = 0; i < 8; ++i) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1); }
    return c;
}
uint16_t inverte16(uint16_t v) { uint16_t r = 0; for (int i = 0; i < 16; ++i) if (v & (1 << i)) r |= uint16_t(1 << (15 - i)); return r; }

bool imprimivel(int c) { return (c >= 32 && c < 127) || c == 10 || c == 13; }
// Texto em ASCII pode vir em UTF-8: a Marinha manda o espaco como C2 A0
// (espaco que nao quebra) e os acentos como C3 xx. So C2/C3 e o byte de
// continuacao (80..BF) contam como texto: aceitar qualquer byte alto deixava
// passar pacote de lixo (e saiam letras cirilicas no meio do boletim).
bool textoAscii(int c) { return imprimivel(c) || c == 0xC2 || c == 0xC3 || (c >= 0x80 && c <= 0xBF); }
} // namespace

PactorCore::PactorCore(double fs) : fs_(fs)
{
    // passa-baixas complexo: tons a +-100 Hz e a modulacao de ate 200 Bd
    const int L = (int)std::ceil(5.5 * fs / 250.0) | 1;
    h_.resize(size_t(L));
    double soma = 0;
    const double fc = 330.0 / fs;
    for (int i = 0; i < L; ++i) {
        const double t = i - (L - 1) / 2.0;
        const double s = t == 0 ? 2 * fc : std::sin(2 * kPi * fc * t) / (kPi * t);
        const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (L - 1)) + 0.08 * std::cos(4 * kPi * i / (L - 1));
        h_[size_t(i)] = float(s * w); soma += s * w;
    }
    for (auto& v : h_) v = float(v / soma);
    hist_.assign(h_.size() * 2, {0, 0});
    acumN_ = 8192;
    acum_.assign(acumN_, 0.0);
    for (int baud : {100, 200}) {
        const double N = fs_ / baud;
        for (int k = 0; k < 8; ++k) { Seq s; s.baud = baud; s.fase = k; s.prox = N + k * N / 8.0; seqs_.push_back(s); }
    }
}

bool PactorCore::travado() const { return algumOk_ && double(t_ - ultimoOk_) < fs_ * 10; }

// Centro do sinal: dois tons a 200 Hz um do outro (media de varias janelas)
void PactorCore::estimarCentro()
{
    const size_t N = 4096;
    std::vector<std::complex<double>> a(N);
    for (size_t i = 0; i < N; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / (N - 1));
        a[i] = std::complex<double>(bufEsp_[i] * w, 0);
    }
    AnaliseCore::fft(a, false);
    std::vector<double>& media = mediaEsp_;
    if (media.size() != N / 2) media.assign(N / 2, 0.0);
    const double hz = fs_ / N;
    for (size_t k = 0; k < N / 2; ++k) media[k] += (std::norm(a[k]) - media[k]) * 0.3;
    auto pot = [&](double f) {
        const int c = (int)std::lround(f / hz); double s = 0;
        for (int j = c - 3; j <= c + 3; ++j) if (j > 0 && j < int(N / 2)) s += media[size_t(j)];
        return s;
    };
    double melhor = 0, fm = f0_, somaTudo = 0; int n = 0;
    for (double c = 400; c <= 2700; c += hz) {
        const double s = std::min(pot(c - 100), pot(c + 100));   // os DOIS tons precisam estar la
        somaTudo += s; ++n;
        if (s > melhor) { melhor = s; fm = c; }
    }
    const double med = somaTudo / std::max(1, n);
    potSinal_ = melhor; potRuido_ = med;
    // so muda o centro com sinal claro; com pacotes validos o AFC fino manda
    if (melhor > 8 * med && (!travado() || std::fabs(fm - f0_) > 60)) { f0_ = fm; centroOk_ = true; }
}

std::string PactorCore::feed(const float* x, size_t n)
{
    const size_t L = h_.size();
    for (size_t i = 0; i < n; ++i) {
        bufEsp_.push_back(x[i]);
        if (bufEsp_.size() >= 4096) { estimarCentro(); bufEsp_.erase(bufEsp_.begin(), bufEsp_.begin() + 2048); }

        const std::complex<float> mix((float)std::cos(faseNco_), (float)-std::sin(faseNco_));
        faseNco_ += 2 * kPi * f0_ / fs_;
        if (faseNco_ > 2 * kPi) faseNco_ -= 2 * kPi;
        hist_[pos_] = hist_[pos_ + L] = x[i] * mix;
        pos_ = (pos_ + 1) % L;
        const std::complex<float>* zz = &hist_[pos_];
        float re = 0, im = 0;
        for (size_t k = 0; k < L; ++k) { re += h_[k] * zz[k].real(); im += h_[k] * zz[k].imag(); }
        const std::complex<float> z(re, im);
        const std::complex<float> d = z * std::conj(zAnt_);
        zAnt_ = z;
        double f = std::atan2(d.imag(), d.real()) * fs_ / (2 * kPi);
        f = std::clamp(f, -400.0, 400.0);
        soma_ += f;
        acum_[t_ % acumN_] = soma_;
        ++t_;
        for (auto& s : seqs_) {
            if (double(t_) < s.prox) continue;
            const double N = fs_ / s.baud;
            // media do discriminador no miolo do bit (80%): menos interferencia do vizinho
            const uint64_t b = uint64_t(s.prox - 0.1 * N), a = uint64_t(s.prox - 0.9 * N);
            float v = 0;
            if (a >= 1 && t_ - a < acumN_ - 2 && b > a)
                v = float((acum_[(b - 1) % acumN_] - acum_[(a - 1) % acumN_]) / double(b - a));
            s.prox += N;
            novoBit(s, v);
        }
    }
    std::string r;
    r.swap(saida_);
    return r;
}

void PactorCore::novoBit(Seq& s, float v)
{
    const size_t L = s.baud == 100 ? 96 : 192;
    s.soft.push_back(v);
    if (s.soft.size() > L) s.soft.pop_front();
    if (s.soft.size() < L) return;
    // Cabecalho: os pacotes ALTERNAM 0x55 e 0xAA (visto na Marinha do Brasil,
    // 6450 kHz, 06/10/2026: "NO REST" com 0x55, "ANTE DA" com 0xAA, ...) - e a
    // copia repetida do FEC vem com a polaridade trocada. 0xAA numa polaridade
    // e 0x55 na outra, entao basta ver se os 8 bits alternam e testar o CRC
    // nas duas polaridades (o CRC diz qual e a certa).
    bool alterna = true;
    for (int i = 1; i < 8 && alterna; ++i) alterna = (s.soft[size_t(i)] > 0) != (s.soft[size_t(i - 1)] > 0);
    if (!alterna) return;
    for (int p : {1, -1}) {
        std::vector<float> soft(s.soft.begin(), s.soft.end());
        if (p < 0) for (auto& x : soft) x = -x;
        tentar(soft, s.baud, false, t_);
    }
}

bool PactorCore::conferirCrc(const std::vector<uint8_t>& corpo, uint16_t rec, int& variante) const
{
    // variantes: algoritmo (refletido/normal) x inicio (FFFF/0) x inversao final x ordem dos bytes x bits ao contrario
    const uint16_t base[4] = {crcRefletido(corpo, 0xFFFF), crcRefletido(corpo, 0), crcNormal(corpo, 0xFFFF), crcNormal(corpo, 0)};
    const uint16_t trocado = uint16_t((rec >> 8) | (rec << 8));
    const uint16_t cand[4] = {rec, trocado, inverte16(rec), inverte16(trocado)};
    for (int v = 0; v < 32; ++v) {
        if (variante_ >= 0 && v != variante_) continue;
        const uint16_t c = base[v & 3] ^ ((v & 4) ? 0xFFFF : 0);
        if (c == cand[(v >> 3) & 3]) { variante = v; return true; }
    }
    return false;
}

bool PactorCore::tentar(const std::vector<float>& soft, int baud, bool somado, uint64_t t)
{
    const int nd = baud == 100 ? 8 : 20;
    const int nb = int(soft.size()) / 8;
    std::vector<uint8_t> by(size_t(nb), 0);
    for (int k = 0; k < nb; ++k)
        for (int i = 0; i < 8; ++i)
            if (soft[size_t(k * 8 + i)] > 0) by[size_t(k)] |= uint8_t(1 << i);
    std::vector<uint8_t> corpo(by.begin() + 1, by.begin() + 1 + nd + 1);   // dados + status
    const uint16_t rec = uint16_t(by[size_t(nd + 2)] | (by[size_t(nd + 3)] << 8));
    int var = -1;
    // sinal de verdade: os bits de FSK ficam perto de +-100 Hz; no chiado espalham
    int firmes = 0;
    for (float x : soft) if (std::fabs(x) > 45 && std::fabs(x) < 170) ++firmes;
    const bool sinal = firmes > int(soft.size() * 0.6);
    if (!somado && variante_ < 0 && !sinal) return false;
    if (conferirCrc(corpo, rec, var)) {
        const std::vector<uint8_t> dados(corpo.begin(), corpo.begin() + nd);
        if (variante_ < 0) {
            // antes de travar a variante do CRC, o primeiro pacote espera um
            // segundo com a mesma (em 60 s): so entao os dois saem
            // (pacotes "ociosos" - so 0x1E - tem os dados iguais, mas o contador
            // no status muda de um para o outro: tambem vale)
            if (pendente_.var == var && double(t - pendente_.t) < fs_ * 60 &&
                (pendente_.dados != dados || pendente_.status != corpo[size_t(nd)])) {
                variante_ = var;
                aceitar(pendente_.dados, pendente_.status, pendente_.baud, false, pendente_.t);
            } else {
                pendente_ = {var, dados, corpo[size_t(nd)], baud, t};
                return true;
            }
        }
        // AFC fino pelo cabecalho (0x55: quatro 1 e quatro 0 - a media e o desvio)
        double m = 0, mag = 0;
        for (int i = 0; i < 8; ++i) { m += soft[size_t(i)]; mag += std::fabs(soft[size_t(i)]); }
        m /= 8; mag /= 8;
        if (!somado && mag > 30) f0_ += std::clamp(m * 0.3, -20.0, 20.0);
        aceitar(dados, corpo[size_t(nd)], baud, somado, t);
        return true;
    }
    if (somado || variante_ < 0 || !sinal) return false;   // memoria-ARQ so depois de travar
    // memoria-ARQ: soma com copias anteriores que tambem falharam
    for (const auto& f : falhos_) {
        if (f.baud != baud || t - f.t < uint64_t(fs_ * 0.5)) continue;
        // so a copia do mesmo pacote: o FEC repete de pacote em pacote
        // (0,97 s); somar com qualquer coisa fazia o CRC passar por acaso
        const double per = fs_ * 0.97, dt = double(t - f.t);
        const double k = std::round(dt / per);
        if (k < 1 || k > 4 || std::fabs(dt - k * per) > 3.0 * fs_ / baud) continue;
        std::vector<float> s2(soft.size());
        for (size_t i = 0; i < soft.size(); ++i) s2[i] = soft[i] + f.soft[i];
        if (tentar(s2, baud, true, t)) return true;
    }
    falhos_.push_back({t, baud, soft});
    while (!falhos_.empty() && (falhos_.size() > 20 || t - falhos_.front().t > uint64_t(fs_ * 8))) falhos_.pop_front();
    ++ruins_;
    return false;
}

bool PactorCore::decodificarHuffman(const std::vector<int>& bits, std::string& txt, bool* fechou)
{
    // O fim do campo pode ser enchimento com zeros: letra que comeca dentro
    // da sequencia final de zeros nao entra (o resto incompleto tambem nao).
    const auto& a = arvoreHuffman();
    size_t z = bits.size();
    while (z > 0 && bits[z - 1] == 0) --z;
    int n = 0;
    size_t inicio = 0;
    for (size_t i = 0; i < bits.size(); ++i) {
        n = a[size_t(n)].filho[bits[i]];
        if (n < 0) return false;
        if (a[size_t(n)].ch >= 0) {
            if (inicio < z) txt += char(a[size_t(n)].ch);
            n = 0; inicio = i + 1;
        }
    }
    // sobrou um pedaco de letra que nao e so enchimento? entao nao era Huffman
    if (fechou) *fechou = n == 0 || inicio >= z;
    return true;
}

void PactorCore::aceitar(const std::vector<uint8_t>& dados, uint8_t status, int baud, bool somado, uint64_t t)
{
    // o FEC repete o pacote: a copia (igual, com o mesmo status) entra uma vez so
    if (dados == ultDados_ && status == ultStatus_ && double(t - ultT_) < fs_ * 8) { ultT_ = t; ultimoOk_ = t; return; }
    ultDados_ = dados; ultStatus_ = status; ultT_ = t;
    // Pacote ocioso: a estacao esta no ar, mas sem mensagem (so enchimento
    // 0x1E). Visto em 8582 kHz em 06/10/2026: o mesmo pacote de 0x1E com o
    // contador do status girando 0-1-2-3. Mostra que esta recebendo, sem texto.
    bool ocioso = true;
    for (uint8_t c : dados) if (c != 0x1E && c != 0) { ocioso = false; break; }
    if (ocioso) {
        ultimoOk_ = t; algumOk_ = true; baudTravado_ = baud;
        if (somado) ++okSomados_; else ++okPacotes_;
        formato_ = "ocioso (sem texto)";
        return;
    }
    // ASCII ou Huffman? O que der texto mais "limpo"
    std::string asc;
    int boaA = 0;
    for (uint8_t c : dados) {
        if (c == 0x1E || c == 0) continue;               // enchimento
        asc += char(c);
        if (textoAscii(c)) ++boaA;
    }
    std::vector<int> bits;
    for (uint8_t c : dados) for (int i = 0; i < 8; ++i) bits.push_back((c >> i) & 1);
    std::string huf;
    int boaH = 0;
    bool fechou = false;
    const bool hOk = decodificarHuffman(bits, huf, &fechou);
    for (char c : huf) if (imprimivel((unsigned char)c)) ++boaH;
    const bool podeH = hOk && fechou && !huf.empty() && double(boaH) / huf.size() > 0.8;
    const bool podeA = !asc.empty() && double(boaA) / asc.size() > 0.8;
    // os dois servem: decide o voto dos pacotes anteriores (o formato nao muda a toda hora)
    bool usaH;
    if (podeH && !podeA) usaH = true;
    else if (podeA && !podeH) usaH = false;
    else if (podeA && podeH) usaH = votoH_ > 0;
    else { ++ruins_; return; }
    // Formato ja firme (varios pacotes seguidos num so formato): um pacote que
    // so "serve" no outro e quase sempre lixo que passou no CRC por acaso -
    // descarta em vez de escrever um pedaco embaralhado no meio do texto.
    if ((usaH && !podeA && votoH_ <= -3) || (!usaH && !podeH && votoH_ >= 3)) {
        if (podeH != podeA) votoH_ = std::clamp(votoH_ + (podeH ? 1 : -1), -6, 6);
        ++ruins_; return;
    }
    if (podeH != podeA) votoH_ = std::clamp(votoH_ + (podeH ? 1 : -1), -6, 6);
    const std::string txt = usaH ? huf : asc;
    formato_ = usaH ? "Huffman" : "ASCII";
    ultimoOk_ = t; algumOk_ = true; baudTravado_ = baud;
    if (somado) ++okSomados_; else ++okPacotes_;
    for (char ch : txt) {
        const unsigned char c = (unsigned char)ch;
        // UTF-8 de 2 bytes (a letra pode vir partida entre dois pacotes):
        // C2 A0 vira espaco; as outras (acentos) saem como estao
        if (!usaH && (c == 0xC2 || c == 0xC3)) { utf8Pend_.assign(1, ch); continue; }
        if (!usaH && c >= 0x80 && c <= 0xBF) {
            if (utf8Pend_.size() == 1) {
                if ((unsigned char)utf8Pend_[0] == 0xC2 && c == 0xA0) saida_ += ' ';
                else { saida_ += utf8Pend_; saida_ += ch; }
            }
            utf8Pend_.clear();
            continue;
        }
        utf8Pend_.clear();
        if (c == '\r') { saida_ += '\n'; continue; }
        if (c == '\n') { if (saida_.empty() || saida_.back() != '\n') saida_ += '\n'; continue; }
        if (imprimivel(c)) saida_ += ch;
    }
    // indicativo: palavra de 4 a 7 com letra e numero (PWZ33), a mais recente
    recente_ += txt;
    if (recente_.size() > 300) recente_.erase(0, recente_.size() - 300);
    std::string w;
    // (a ultima palavra do buffer ainda pode continuar no proximo pacote:
    // "O061" + "902Z" dava o falso indicativo O061 - so conta palavra fechada)
    for (size_t i = 0; i < recente_.size(); ++i) {
        const char c = recente_[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { w += c; continue; }
        // forma de indicativo: 2 ou 3 letras, um numero, depois 1 a 3 letras/numeros (PWZ33, PWR44)
        size_t k = 0;
        while (k < w.size() && w[k] >= 'A' && w[k] <= 'Z') ++k;
        const bool forma = k >= 2 && k <= 3 && k < w.size() && w[k] >= '0' && w[k] <= '9' &&
                           w.size() - k >= 2 && w.size() - k <= 4 && w.size() >= 4;
        if (forma) indicativo_ = w;
        w.clear();
    }
}

} // namespace masdr
