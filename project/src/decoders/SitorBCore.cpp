#include "SitorBCore.h"

#include "AnaliseCore.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

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

// Tabela oficial: ARRL Handbook cap. 16 (ITA2 e AMTOR), coluna "CCIR 476 Code".
// A tabela anterior foi montada de memoria e estava errada - tinha 31 codigos
// distintos em vez de 32, e faltavam justamente 0x3A e 0x1D, que sozinhos sao
// quase um quarto do trafego real. Por isso o texto saia consistente porem
// ilegivel: os bits estavam certos, a traducao e que nao.
struct Ccir476Linha { uint8_t code; char letra; char figura; };

const Ccir476Linha kTabela[] = {
    {0x47,'A','-' }, {0x72,'B','?' }, {0x1D,'C',':' }, {0x53,'D','\x05'},
    {0x56,'E','3' }, {0x1B,'F','\0'}, {0x35,'G','\0'}, {0x69,'H','\0'},
    {0x4D,'I','8' }, {0x17,'J','\a'}, {0x1E,'K','(' }, {0x65,'L',')' },
    {0x39,'M','.' }, {0x59,'N',',' }, {0x71,'O','9' }, {0x2D,'P','0' },
    {0x2E,'Q','1' }, {0x55,'R','4' }, {0x4B,'S','\''}, {0x74,'T','5' },
    {0x4E,'U','7' }, {0x3C,'V','=' }, {0x27,'W','2' }, {0x3A,'X','/' },
    {0x2B,'Y','6' }, {0x63,'Z','+' },
    {0x78,'\r','\r'},                 // CR
    {0x6C,'\n','\n'},                 // LF
    {0x5A,'\0','\0'},                 // LTRS - troca para letras
    {0x36,'\0','\0'},                 // FIGS - troca para algarismos
    {0x5C,' ',' ' },                  // SP
    {0x6A,'\0','\0'},                 // BLK
};

constexpr int kIdxLtrs = 28;   // 0x5A
constexpr int kIdxFigs = 29;   // 0x36

// Os tres codigos de peso 4 que sobram nao sao caracteres: o SITOR usa como
// repouso, fase e pedido de repeticao. Nunca viram texto.
constexpr uint8_t kSia = 0x0F;
constexpr uint8_t kSib = 0x33;
constexpr uint8_t kRpt = 0x66;

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
    for (size_t i = 0; i < sizeof(kTabela) / sizeof(kTabela[0]); ++i)
        if (kTabela[i].code == code) return int(i);
    return -1;   // inclui SIA, SIB e RPT, que nao sao caracteres
}

// Sinais de servico do SITOR. Eu simplesmente os ignorava, e isso tinha um
// efeito ruim: se a estacao mandava fase no meio da transmissao estando em
// modo ALGARISMOS, o decodificador continuava em algarismos depois dela e o
// texto saia como "7-313-1(?82+". A fase reinicia o estado - por isso o
// tratamento agora e explicito. Mostra-los tambem ajuda a saber de relance se
// a estacao esta em repouso ou mandando mensagem.
void SitorBCore::processarServico(uint8_t code)
{
    switch (code) {
        case kSia:  figuras_ = false; saida_ += '>'; break;   // fase / repouso
        case kRpt:  figuras_ = false; saida_ += '^'; break;   // pedido de repeticao
        case kSib:  saida_ += '<';                    break;  // fase
        default: break;
    }
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
    blocoD_.clear();
    janela_.assign(janelaLen_, 0.0f);
    janelaPos_ = 0;
    janelaCheia_ = false;
    shiftReg_ = 0;
    bitsNoReg_ = 0;
    historico_.clear();
    idxRecebidos_ = 0;
    proximoDx_    = -1;
    faseDx_       = 0;
    vaoFec_       = 11;
    sincronizado_ = false;
    desdeAvaliacao_ = 0;
    ruimSeguidos_ = 0;
    figuras_ = false;
    saida_.clear();
    totalChars_ = validChars_ = corrigidos_ = 0;
}

// ---------------------------------------------------------------------------
//  Demodulacao FSK + recuperacao de temporizacao
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  Medicao automatica do tom central
//
//  O tom nao vem da norma, vem de onde o radio esta sintonizado. No DSC a
//  mesma estacao apareceu em 1080 Hz numa gravacao e em 1470 Hz noutra, e aqui
//  no SITOR-B foi igual: ficamos tentando 730 e 720 na mao ate acertar.
//
//  A diferenca em relacao ao DSC e a pontuacao. La existe o ECC da ITU, que ou
//  fecha ou nao fecha. Aqui nao ha ECC, entao o criterio e o CODIGO DE PESO
//  CONSTANTE: todo caractere CCIR 476 tem exatamente 4 bits em 1. Um tom
//  errado quebra isso depressa.
//
//  Uma ressalva que me custou dias: o peso 4 NAO serve para julgar se o texto
//  esta legivel - a ordem dos bits invertida preserva o peso e a metrica
//  aprovava lixo. Mas para achar frequencia ele serve bem, porque errar o tom
//  nao preserva peso nenhum. Como desempate somamos quantos caracteres saem
//  como letra, algarismo ou espaco, que e o que um NAVTEX tem de sobra.
// ---------------------------------------------------------------------------
bool SitorBCore::estimarTom(const std::vector<float>& buf, double& centro) const
{
    const int N = 4096;
    const size_t NN = size_t(N);
    if (buf.size() < NN * 2) return false;

    // Cuidado: std::vector<double> jan(size_t(N));  seria lido pelo compilador
    // como declaracao de funcao. Por isso o valor inicial.
    std::vector<double> jan(NN, 0.0);
    for (int i = 0; i < N; ++i)
        jan[size_t(i)] = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(N - 1));

    // O SITOR-B e continuo, nao em rajada como o DSC, entao nao precisa do
    // portao de energia - basta descartar janelas claramente mudas.
    std::vector<double> P(NN / 2 + 1, 0.0);
    int usadas = 0;
    for (size_t o = 0; o + NN <= buf.size(); o += NN / 2) {
        double e = 0;
        for (int i = 0; i < N; ++i) { const double v = buf[o + size_t(i)]; e += v * v; }
        if (e < 1e-9) continue;
        std::vector<std::complex<double>> a(NN, std::complex<double>(0.0, 0.0));
        for (int i = 0; i < N; ++i)
            a[size_t(i)] = std::complex<double>(double(buf[o + size_t(i)]) * jan[size_t(i)], 0.0);
        AnaliseCore::fft(a, false);
        for (size_t k = 0; k < P.size(); ++k) P[k] += std::norm(a[k]);
        ++usadas;
    }
    if (usadas < 2) return false;

    const double hzRaia = p_.sampleRate / double(N);
    const size_t kMin = size_t(300.0 / hzRaia);
    const size_t kMax = std::min(P.size() - 2, size_t(3000.0 / hzRaia));
    if (kMax <= kMin + 4) return false;

    std::vector<double> c(P.begin() + long(kMin), P.begin() + long(kMax));
    std::nth_element(c.begin(), c.begin() + long(c.size() / 2), c.end());
    const double mediana = c[c.size() / 2];

    auto refinar = [&](size_t k) {
        const double y0 = P[k - 1], y1 = P[k], y2 = P[k + 1];
        const double den = y0 - 2.0 * y1 + y2;
        const double dd = (std::abs(den) < 1e-20) ? 0.0 : 0.5 * (y0 - y2) / den;
        return (double(k) + dd) * hzRaia;
    };

    size_t k1 = 0; double v1 = 0;
    for (size_t k = kMin; k < kMax; ++k) if (P[k] > v1) { v1 = P[k]; k1 = k; }
    if (k1 == 0 || v1 < mediana * 8.0) return false;
    const double f1 = refinar(k1);

    size_t k2 = 0; double v2 = 0;
    for (size_t k = kMin; k < kMax; ++k) {
        const double f = double(k) * hzRaia;
        if (std::abs(f - f1) < 60.0)  continue;   // mesmo lobo
        if (std::abs(f - f1) > 500.0) continue;   // longe demais para ser o par
        if (P[k] > v2) { v2 = P[k]; k2 = k; }
    }
    if (k2 == 0 || v2 < mediana * 5.0) return false;
    const double f2 = refinar(k2);

    centro = (f1 + f2) / 2.0;
    return centro > 300.0 && centro < 3000.0;
}

double SitorBCore::refinarTom(const std::vector<float>& buf, double centroInicial) const
{
    auto pontuar = [&](double cc) {
        Params p = p_;
        p.centerFreq = cc;
        p.autoTom    = false;          // senao a copia tenta medir de novo
        SitorBCore d(p);
        std::string out;
        for (size_t i = 0; i < buf.size(); i += 1600)
            out += d.feed(&buf[i], std::min<size_t>(1600, buf.size() - i));

        int legiveis = 0;
        for (char ch : out) {
            const unsigned char u = (unsigned char)ch;
            if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == ' ')
                ++legiveis;
        }
        // peso 4 encontra a frequencia; os caracteres legiveis desempatam
        return d.validChars() * 4 + legiveis;
    };

    double melhorC = centroInicial;
    int    melhorP = pontuar(centroInicial);
    for (double d = -48.0; d <= 48.0; d += 6.0) {
        if (d == 0.0) continue;
        const double cc = centroInicial + d;
        if (cc < 300.0 || cc > 3000.0) continue;
        const int pt = pontuar(cc);
        if (pt > melhorP) { melhorP = pt; melhorC = cc; }
    }
    return melhorC;
}

void SitorBCore::aplicarTom(double centro)
{
    Params p = p_;
    p.centerFreq = centro;
    setParams(p);        // recalcula correlatores e zera o estado
    tomPronto_ = true;   // depois do reset, senao ele volta a falso
}

std::string SitorBCore::feed(const float* samples, size_t n)
{
    if (!samples || n == 0) return {};

    // ---- tom central automatico -------------------------------------------
    // 5 s bastam: o SITOR-B transmite sem parar, ao contrario do DSC, que
    // precisou de 12 s para pegar uma rajada inteira.
    if (p_.autoTom && !tomPronto_) {
        tomBuf_.insert(tomBuf_.end(), samples, samples + n);

        // Enquanto mede, NAO decodifica: o trecho guardado sera reprocessado
        // depois, e decodificar agora fazia o mesmo texto sair duas vezes -
        // uma antes da medicao e outra depois. Aparecia bem no log, com a
        // linha repetida em volta do aviso "[tom central medido: ...]".
        if (tomBuf_.size() < size_t(5.0 * p_.sampleRate)) return {};
        if (tomBuf_.size() >= size_t(5.0 * p_.sampleRate)) {
            std::vector<float> guardado;
            guardado.swap(tomBuf_);
            tomBuf_.shrink_to_fit();

            double centro = 0.0;
            if (estimarTom(guardado, centro)) {
                centro = refinarTom(guardado, centro);
                tomMedido_ = centro;
                aplicarTom(centro);            // aqui tomPronto_ vira true

                char msg[128];
                std::snprintf(msg, sizeof(msg),
                    "\n[tom central medido: %.0f Hz]\n", centro);
                // Reprocessa o audio guardado: nada se perde na medicao.
                const std::string atrasado = feed(guardado.data(), guardado.size());
                saida_ += msg;
                saida_ += atrasado;
            } else {
                tomPronto_ = true;
                const std::string atrasado = feed(guardado.data(), guardado.size());
                saida_ += "\n[nao consegui medir o tom - usando o valor da tela]\n";
                saida_ += atrasado;
            }

            // Devolve AGORA. As amostras desta chamada ja entraram no trecho
            // guardado e acabaram de ser reprocessadas - deixar o laco abaixo
            // roda-las de novo era o que fazia a primeira linha do log sair
            // repetida em volta do aviso da medicao.
            std::string rr;
            rr.swap(saida_);
            return rr;
        }
    }

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
        blocoD_.push_back(std::sqrt(mI * mI + mQ * mQ) - std::sqrt(sI * sI + sQ * sQ));

        if (blocoD_.size() >= size_t(double(bitsPorBloco_) * amostrasPorBit_)) {
            // processarBloco consome so o que fechou caractere inteiro e
            // devolve o resto para o proximo bloco. NAO limpar aqui: era o
            // clear() que criava a duplicacao (veja o comentario la dentro).
            processarBloco();
        }
    }

    std::string r;
    r.swap(saida_);
    return r;
}

// ---------------------------------------------------------------------------
//  Escolhe fase e alinhamento de caractere medindo o resultado, em vez de
//  persegui-los. 80 fases x 7 alinhamentos por bloco e barato e nao depende
//  de nenhuma constante ajustada a mao.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  DECISAO SUAVE
//
//  A versao anterior decidia cada bit isoladamente - positivo virava 1,
//  negativo virava 0 - e so depois perguntava se os 7 bits formavam um codigo
//  valido. Um caractere com UM bit marginal era jogado fora inteiro, mesmo com
//  os outros seis firmes. Essa decisao bit a bit joga fora a informacao de
//  QUAO confiante ela era, que e justamente o que salva sinal fraco.
//
//  Agora comparamos o trecho analogico contra os 35 codigos validos do
//  CCIR 476 e ficamos com o mais proximo. E o mesmo principio que faz um
//  receptor de satelite funcionar abaixo do ruido.
//
//  ARMADILHA que este desenho cria: escolher sempre o codigo valido mais
//  proximo faz TODO caractere sair valido, e validChars_ viraria 100% mesmo
//  com o texto ilegivel - exatamente a metrica cega que ja me custou dias
//  nesta investigacao. Por isso existe a MARGEM: se o melhor codigo nao ganhar
//  do segundo colocado por uma folga clara, o caractere e marcado como
//  APAGAMENTO (codigo 0, que nao passa no peso 4) e a diversidade DX/RX usa a
//  outra copia. Assim validChars_ continua contando so o que temos motivo para
//  acreditar, e a metrica continua valendo alguma coisa.
// ---------------------------------------------------------------------------
void SitorBCore::processarBloco()
{
    const double spb = amostrasPorBit_;
    if (blocoD_.size() < size_t(spb * 14.0)) return;

    // Os 35 codigos de peso 4 - montados uma vez so.
    static std::vector<uint8_t> validos;
    if (validos.empty())
        for (int v = 0; v < 128; ++v)
            if (ccir476Valido(uint8_t(v))) validos.push_back(uint8_t(v));

    double melhorMargem = -1.0;
    int melhorPh = -1, melhorOff = 0;
    std::vector<uint8_t> melhores;
    std::vector<float> sv;

    for (int ph = 0; ph < int(spb); ++ph) {
        sv.clear();
        for (double t = ph; t < double(blocoD_.size()); t += spb) {
            float d = blocoD_[size_t(t)];
            if (p_.invert) d = -d;
            sv.push_back(d);          // guarda o valor ANALOGICO, nao o bit
        }
        for (int off = 0; off < 7; ++off) {
            std::vector<uint8_t> cods;
            double somaMargem = 0.0;
            for (size_t i = size_t(off); i + 7 <= sv.size(); i += 7) {
                // escala do trecho: soma dos modulos e o maximo que qualquer
                // codigo poderia pontuar, e serve para normalizar a margem
                double escala = 0.0;
                for (int q = 0; q < 7; ++q) escala += std::abs(double(sv[i + size_t(q)]));
                if (escala < 1e-12) { cods.push_back(0); continue; }

                double melhor = -1e30, segundo = -1e30;
                uint8_t melhorC = 0;
                for (uint8_t c : validos) {
                    double s = 0.0;
                    for (int q = 0; q < 7; ++q)
                        s += ((c >> q) & 1) ? double(sv[i + size_t(q)])
                                            : -double(sv[i + size_t(q)]);
                    if (s > melhor) { segundo = melhor; melhor = s; melhorC = c; }
                    else if (s > segundo) { segundo = s; }
                }

                // ORDEM DOS BITS: o CCIR 476 transmite o bit MENOS
                // significativo primeiro, e e assim que os codigos acima sao
                // comparados. Montar ao contrario foi o erro que mais custou
                // aqui, porque a inversao PRESERVA o peso 4 e a verificacao
                // continuava aprovando texto ilegivel.
                const double margem = (melhor - segundo) / escala;
                somaMargem += margem;
                cods.push_back(margem >= kMargemMinima ? melhorC : uint8_t(0));
            }
            if (somaMargem > melhorMargem) {
                melhorMargem = somaMargem;
                melhorPh = ph; melhorOff = off;
                melhores.swap(cods);
            }
        }
    }

    for (uint8_t c : melhores) processarCaractere(c);

    // ---- A EMENDA ENTRE BLOCOS -------------------------------------------
    // Aqui estava a duplicacao de dois caracteres - APAPAGADA, CONFIABLELE,
    // NWNW, AGUGUAS.
    //
    // O bloco junta 200 bits e antes era DESCARTADO INTEIRO. Mas 200 nao e
    // multiplo de 7, entao cada emenda jogava fora um pedaco de caractere.
    // Perder um numero IMPAR de caracteres inverte a paridade das posicoes, e
    // no SITOR-B as posicoes alternam DX e RX. Com a paridade trocada, o
    // decodificador passa a casar cada DX com o RX errado e emite de novo o
    // que ja tinha saido - dois caracteres repetidos, exatamente o sintoma.
    //
    // Batia ate na frequencia: 200 bits a 100 baud sao 2 segundos, cerca de 28
    // caracteres, e as repeticoes apareciam a cada 20 ou 40 caracteres.
    //
    // A correcao e nao descartar nada. Consumimos so o que fechou caractere
    // inteiro e devolvemos o resto do audio para o proximo bloco, de modo que
    // o fluxo de caracteres fica continuo e a paridade nunca se perde.
    if (melhorPh >= 0 && !melhores.empty()) {
        const double usado = double(melhorPh)
                           + double(melhorOff + 7 * int(melhores.size())) * spb;
        size_t corte = (usado > 0.0) ? size_t(usado) : 0;
        if (corte > blocoD_.size()) corte = blocoD_.size();
        blocoD_.erase(blocoD_.begin(), blocoD_.begin() + long(corte));
    } else {
        blocoD_.clear();          // nada aproveitavel, comeca do zero
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
    // Uma vez TRAVADO, a fase e o vao ficam congelados.
    //
    // Antes eu reavaliava a cada 8 caracteres, mesmo travado. Quando a fase
    // virava, o par DX/RX deslocava uma posicao e um caractere ja emitido como
    // RX saia de novo como DX - e essa era a origem exata do "FAFARO",
    // "CACARTA", "PRPRECAUCION": sempre DOIS caracteres repetidos.
    //
    // So voltamos a procurar se o sinal realmente se perder, medido por uma
    // sequencia longa de caracteres invalidos. Assim o decodificador aguenta
    // desvanecimento sem largar o alinhamento por causa de ruido passageiro.
    if (sincronizado_) {
        if (ccir476Valido(code)) ruimSeguidos_ = 0;
        else if (++ruimSeguidos_ > 48) { sincronizado_ = false; proximoDx_ = -1; }
    }

    if (!sincronizado_ && ++desdeAvaliacao_ >= 8 && historico_.size() >= 32) {
        desdeAvaliacao_ = 0;
        // Eu testava so os vaos 9 e 11, que sao os que a leitura da norma me
        // sugeriu. Medindo o fluxo cru de um sinal real, nenhum dos dois foi o
        // melhor - o que mostra que a minha leitura pode estar errada, ou que
        // a estacao usa outra convencao. Em vez de insistir na teoria, agora
        // procuramos o vao entre 3 e 15 e ficamos com o que a evidencia
        // apontar. Custa alguns microssegundos e nao depende de eu ter
        // entendido a norma direito.
        int melhorPontos = 0, melhorFase = faseDx_, melhorVao = vaoFec_;
        const long long base = idxRecebidos_ - (long long)historico_.size();
        for (int vao = 3; vao <= 15; ++vao) {
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
        // Exige que o melhor vao se destaque de verdade. Num sinal em repouso
        // o padrao de fase se repete e QUALQUER vao acerta uns 45% dos pares -
        // aceitar o maior sem margem faria o decodificador travar em vao
        // errado com toda a confianca.
        const int minimo = int(historico_.size()) / 8;

        // Duas correcoes que causavam a DUPLICACAO no texto ("NARANJNJA",
        // "PRECAUCIOION"):
        //
        // 1) Quando a fase mudava, proximoDx_ continuava na paridade antiga -
        //    o ponteiro de emissao passava a apontar para a posicao errada e o
        //    decodificador emitia as DUAS copias, DX e RX, em vez de combinar.
        //    Agora, se a fase ou o vao mudam, o ponteiro e refeito.
        //
        // 2) O travamento era refeito a cada 8 caracteres e trocava por
        //    qualquer melhora minima, inclusive ruido. Uma vez travado, so
        //    trocamos se o novo candidato for CLARAMENTE melhor (metade a
        //    mais), senao ficamos onde estamos.
        bool aceitar = (melhorPontos > minimo);
        if (aceitar && sincronizado_) {
            const bool mudou = (melhorFase != faseDx_) || (melhorVao != vaoFec_);
            if (mudou) {
                int atual = 0;
                const long long b0 = idxRecebidos_ - (long long)historico_.size();
                for (long long i = b0 + vaoFec_; i < idxRecebidos_; ++i) {
                    if (int(i % 2) == faseDx_) continue;
                    const uint8_t rx = codigoEm(i);
                    const uint8_t dx = codigoEm(i - vaoFec_);
                    if (rx == dx && ccir476Valido(rx)) ++atual;
                }
                if (melhorPontos < atual + atual / 2) aceitar = false;
            }
        }

        if (aceitar) {
            const bool mudou = (melhorFase != faseDx_) || (melhorVao != vaoFec_);
            faseDx_ = melhorFase;
            vaoFec_ = melhorVao;
            sincronizado_ = true;
            if (proximoDx_ < 0) {
                const long long base2 = idxRecebidos_ - (long long)historico_.size();
                proximoDx_ = base2;
                while (int(proximoDx_ % 2) != faseDx_) ++proximoDx_;
            } else if (mudou) {
                // So ACERTA A PARIDADE do ponteiro, sem move-lo para tras.
                // Mandar o ponteiro de volta ao inicio do historico, como eu
                // fiz na primeira tentativa, reemite tudo o que ja saiu - o
                // texto passou a repetir frases inteiras.
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

        const uint8_t bom = ccir476Valido(dx) ? dx : (ccir476Valido(rx) ? rx : 0);
        if (bom) {
            ++validChars_;
            if (bom == rx && bom != dx) ++corrigidos_;   // salvo pela copia repetida
            const int idx476 = ccir476ParaIta2(bom);
            if (idx476 >= 0) emitirIta2(idx476);
            else             processarServico(bom);      // SIA, SIB ou RPT
        } else {
            saida_ += '_';                    // as duas falharam
        }
        proximoDx_ += 2;                      // proxima posicao DX
    }
}

void SitorBCore::emitirIta2(int idx)
{
    if (idx < 0 || idx >= int(sizeof(kTabela) / sizeof(kTabela[0]))) return;
    if (idx == kIdxLtrs) { figuras_ = false; return; }
    if (idx == kIdxFigs) { figuras_ = true;  return; }

    const char c = figuras_ ? kTabela[idx].figura : kTabela[idx].letra;
    if (c != '\0') saida_ += c;
}

} // namespace masdr
