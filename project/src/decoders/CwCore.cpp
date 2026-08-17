#include "CwCore.h"

#include "AnaliseCore.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace masdr {

namespace {

// ---------------------------------------------------------------------------
//  Tabela Morse internacional (ITU-R M.1677-1).
//  Escrita a partir da recomendacao, e conferida por um teste no proprio
//  programa: nenhum codigo pode se repetir, e todo codigo tem de ser composto
//  so de pontos e tracos. Foi assim que a tabela do CCIR 476 deveria ter sido
//  tratada desde o inicio - a versao montada de memoria custou dias.
// ---------------------------------------------------------------------------
struct Linha { const char* codigo; const char* texto; };

const Linha kTabela[] = {
    {".-",    "A"}, {"-...",  "B"}, {"-.-.",  "C"}, {"-..",   "D"},
    {".",     "E"}, {"..-.",  "F"}, {"--.",   "G"}, {"....",  "H"},
    {"..",    "I"}, {".---",  "J"}, {"-.-",   "K"}, {".-..",  "L"},
    {"--",    "M"}, {"-.",    "N"}, {"---",   "O"}, {".--.",  "P"},
    {"--.-",  "Q"}, {".-.",   "R"}, {"...",   "S"}, {"-",     "T"},
    {"..-",   "U"}, {"...-",  "V"}, {".--",   "W"}, {"-..-",  "X"},
    {"-.--",  "Y"}, {"--..",  "Z"},

    {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"},
    {"....-", "4"}, {".....", "5"}, {"-....", "6"}, {"--...", "7"},
    {"---..", "8"}, {"----.", "9"},

    {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"}, {".----.", "'"},
    {"-.-.--", "!"}, {"-..-.",  "/"}, {"-.--.",  "("}, {"-.--.-", ")"},
    {".-...",  "&"}, {"---...", ":"}, {"-.-.-.", ";"}, {"-...-",  "="},
    {".-.-.",  "+"}, {"-....-", "-"}, {"..--.-", "_"}, {".-..-.", "\""},
    {"...-..-","$"}, {".--.-.", "@"},

    // Abreviaturas de servico que o radioamador usa o tempo todo. Sem elas o
    // texto sai com letras coladas justamente nos pontos mais importantes da
    // conversa: o inicio, a passagem e o fim.
    {"-.-.-",  "<KA>"},   // inicio de mensagem
    {".-.-",   "<AA>"},
    {"...-.-", "<SK>"},   // fim de contato
    {"...-.",  "<SN>"},
    {"........","<HH>"},  // erro
    {"-...-.-","<BK>"},
};

int contarUns(const std::string& s)
{
    int n = 0;
    for (char c : s) if (c == '.' || c == '-') ++n;
    return n;
}

} // namespace

const char* CwCore::morseParaTexto(const std::string& codigo)
{
    if (codigo.empty()) return nullptr;
    for (const Linha& l : kTabela)
        if (codigo == l.codigo) return l.texto;
    return nullptr;
}

// ---------------------------------------------------------------------------

CwCore::CwCore()                 { setParams(Params{}); }
CwCore::CwCore(const Params& p)  { setParams(p); }

void CwCore::setParams(const Params& p)
{
    p_ = p;

    const double tom = (p_.tomHz > 0.0) ? p_.tomHz : 700.0;
    const double w = 2.0 * M_PI * tom / p_.sampleRate;
    passoCos_ = std::cos(w);
    passoSen_ = std::sin(w);

    // Janela da media movel: 10 ms. Precisa ser bem menor que o ponto mais
    // curto que queremos ler - a 40 PPM o ponto tem 30 ms -, e larga o
    // bastante para alisar o tom de 700 Hz.
    mediaLen_ = size_t(p_.sampleRate * 0.010);
    if (mediaLen_ < 8) mediaLen_ = 8;

    reset();
}

void CwCore::reset()
{
    oscI_ = 1.0; oscQ_ = 0.0;
    mediaI_.assign(mediaLen_, 0.0);
    mediaQ_.assign(mediaLen_, 0.0);
    mediaPos_ = 0; somaI_ = 0.0; somaQ_ = 0.0;

    piso_ = 0.0; pico_ = 0.0;
    ligado_ = false;
    amostrasNoEstado_ = 0.0;
    bruto_ = false;
    persistenciaMs_ = 0.0;

    unidadeMs_ = 60.0;
    marcasRecentes_.clear();
    ppm_ = 0.0;

    codigo_.clear();
    saida_.clear();
    letras_ = 0; naoLidos_ = 0;
    espacoPendente_ = false;
    // tomBuf_ e tomPronto_ ficam de fora: setParams chama reset, e apagar a
    // medicao aqui faria o decodificador medir em circulo.
}

// ---------------------------------------------------------------------------

std::string CwCore::feed(const float* samples, size_t n)
{
    if (!samples || n == 0) return {};

    // ---- tom automatico ---------------------------------------------------
    // Em CW o tom depende de onde o operador sintonizou - nao ha valor
    // "certo". Medimos nos primeiros 3 s, que a 20 PPM ja trazem varias
    // letras.
    if (p_.autoTom && !tomPronto_) {
        tomBuf_.insert(tomBuf_.end(), samples, samples + n);
        if (tomBuf_.size() < size_t(3.0 * p_.sampleRate)) return {};

        std::vector<float> guardado;
        guardado.swap(tomBuf_);
        tomBuf_.shrink_to_fit();

        double tom = 0.0;
        if (estimarTom(guardado, tom)) {
            tomMedido_ = tom;
            Params np = p_;
            np.tomHz = tom;
            setParams(np);          // recalcula o oscilador e zera o estado
            tomPronto_ = true;
            char msg[96];
            std::snprintf(msg, sizeof(msg), "\n[tom medido: %.0f Hz]\n", tom);
            const std::string atrasado = feed(guardado.data(), guardado.size());
            saida_ += msg;
            saida_ += atrasado;
        } else {
            tomPronto_ = true;
            const std::string atrasado = feed(guardado.data(), guardado.size());
            saida_ += "\n[nao consegui medir o tom - usando o valor da tela]\n";
            saida_ += atrasado;
        }
        std::string r; r.swap(saida_); return r;
    }

    for (size_t k = 0; k < n; ++k) {
        // oscilador local por recorrencia, renormalizado de vez em quando
        const double ni = oscI_ * passoCos_ - oscQ_ * passoSen_;
        const double nq = oscI_ * passoSen_ + oscQ_ * passoCos_;
        oscI_ = ni; oscQ_ = nq;
        if ((k & 1023) == 0) {
            const double m = std::sqrt(oscI_ * oscI_ + oscQ_ * oscQ_);
            if (m > 1e-12) { oscI_ /= m; oscQ_ /= m; }
        }

        const double v = double(samples[k]);
        const double vi = v * oscI_;
        const double vq = -v * oscQ_;

        somaI_ += vi - mediaI_[mediaPos_];  mediaI_[mediaPos_] = vi;
        somaQ_ += vq - mediaQ_[mediaPos_];  mediaQ_[mediaPos_] = vq;
        mediaPos_ = (mediaPos_ + 1) % mediaLen_;

        const double env = std::sqrt(somaI_ * somaI_ + somaQ_ * somaQ_) / double(mediaLen_);
        processarAmostra(float(env));
    }

    std::string r; r.swap(saida_); return r;
}

// ---------------------------------------------------------------------------
//  Limiar com HISTERESE
//
//  Um limiar unico faz a chave "tremer" perto do ponto de corte: um mesmo
//  traco vira varios pontos porque a envoltoria cruza o limite para cima e
//  para baixo. A histerese resolve com dois limites - liga em 60% e desliga
//  em 40% da distancia entre o piso e o pico.
//
//  Piso e pico sao seguidos devagar, para acompanhar desvanecimento sem
//  correr atras de cada elemento.
// ---------------------------------------------------------------------------
void CwCore::processarAmostra(float envF)
{
    const double env = double(envF);
    const double dt = 1000.0 / p_.sampleRate;    // ms por amostra

    if (env > pico_) pico_ = env;
    else             pico_ += (env - pico_) * 0.00002;
    if (pico_ < 1e-9) pico_ = 1e-9;

    if (env < piso_ || piso_ == 0.0) piso_ = env;
    else                             piso_ += (env - piso_) * 0.00002;

    const double faixa = pico_ - piso_;
    const double limLiga  = piso_ + faixa * 0.60;
    const double limDesl  = piso_ + faixa * 0.40;

    amostrasNoEstado_ += dt;

    // ---- AMORTECIMENTO (deglitch) ------------------------------------------
    // A histerese cuida do tremor em volta do limiar, mas nao de fragmento:
    // um traco com ondulacao de amplitude mergulha abaixo do limite de
    // desligar por alguns milissegundos e se parte em dois, e um estalo no
    // silencio sobe acima do de ligar e vira marca. Medido nas gravacoes de
    // 40 m: apareciam marcas de 8, 9 e 14 ms num sinal cujo ponto tem 40.
    //
    // Essas marcas curtas nao atrapalhavam a leitura direto - elas envenenavam
    // a MEDIDA. Entravam no grupo dos pontos e puxavam a media de 40 para 24
    // ms, o que levava a razao traco/ponto a 4,83 e fazia a unidade parar de
    // ser atualizada.
    //
    // A regra: so aceitamos a mudanca depois que ela se sustentar por um
    // pedaco do ponto. Abaixo disso e ondulacao, nao manipulacao. Os 4 ms de
    // piso seguram o caso da unidade ainda nao ter sido medida.
    // A histerese e relativa ao estado CRU, nao ao confirmado. Escrever
    // "ligado_" aqui - como eu fiz na primeira versao - faz o sinal ter de
    // ficar acima do limite de SUBIDA durante toda a espera de confirmacao:
    // qualquer ondulacao zera o contador e a marca nunca entra. O resultado
    // foi o decodificador emudecer em sinal fraco.
    const bool cru = bruto_ ? (env >= limDesl) : (env > limLiga && faixa > 1e-7);
    if (cru != bruto_) { bruto_ = cru; persistenciaMs_ = 0.0; }
    else               { persistenciaMs_ += dt; }

    const double minimoMs = std::max(4.0, unidadeMs_ * 0.35);

    if (bruto_ != ligado_ && persistenciaMs_ >= minimoMs) {
        // A mudanca comecou ha persistenciaMs_; o estado que termina durou o
        // resto. Sem esse desconto cada elemento sairia mais longo do que foi,
        // e o proprio amortecimento estragaria a medida que veio consertar.
        const double duracao = amostrasNoEstado_ - persistenciaMs_;
        if (ligado_) fecharMarca(duracao);
        else         fecharEspaco(duracao);
        ligado_ = bruto_;
        amostrasNoEstado_ = persistenciaMs_;
    } else if (!ligado_ && amostrasNoEstado_ > unidadeMs_ * 8.0) {
        // Silencio longo: fecha a letra e a palavra, e nao deixa o contador
        // crescer sem limite.
        //
        // Eram 20 unidades. So que e ESTE ramo que solta a ultima letra de
        // uma transmissao - a letra anterior sai quando o proximo espaco
        // aparece, mas a ultima nao tem "proximo". A 20 PPM, 20 unidades sao
        // 1,2 s de tela parada no fim de cada chamada. O espaco entre
        // palavras vale 7 unidades, entao qualquer coisa acima de 8 ja e fim
        // de transmissao com folga.
        fecharEspaco(amostrasNoEstado_);
        amostrasNoEstado_ = 0.0;
    }
}

// ---------------------------------------------------------------------------
//  MEDIDA DA UNIDADE (o ponto)
//
//  Tudo em Morse e multiplo do ponto: traco = 3, espaco entre elementos = 1,
//  entre letras = 3, entre palavras = 7. Entao basta saber o ponto.
//
//  Guardamos as marcas recentes e tomamos como ponto o menor grupo. Usar a
//  media de todas seria errado: ela cai entre o ponto e o traco e nao serve
//  para separar os dois. O percentil 25 aproxima bem o grupo dos pontos sem
//  precisar de agrupamento de verdade.
// ---------------------------------------------------------------------------
void CwCore::atualizarUnidade(double msMarca)
{
    if (msMarca < 8.0 || msMarca > 2000.0) return;   // ruido ou portadora
    marcasRecentes_.push_back(msMarca);
    while (marcasRecentes_.size() > 40) marcasRecentes_.pop_front();
    if (marcasRecentes_.size() < 8) return;

    std::vector<double> v(marcasRecentes_.begin(), marcasRecentes_.end());
    std::sort(v.begin(), v.end());

    // SEPARACAO EM DOIS GRUPOS
    //
    // Duas tentativas ja falharam aqui. O percentil 25 devolvia um traco como
    // se fosse ponto quando o operador mandava so tracos. O MAIOR SALTO na
    // lista ordenada parecia resolver - "nao existe elemento de duracao
    // intermediaria no Morse" -, mas isso so vale para manipulador
    // eletronico. Num manipulador manual o ponto varia 30% ou mais, os dois
    // grupos se encostam, e o maior salto cai DENTRO do grupo dos pontos.
    // Medido na gravacao de 7.0282: ele separou 86 pontos e UM traco, e a
    // unidade saiu 90 ms no lugar de 40. Como o corte ponto/traco fica em
    // duas unidades - 180 ms -, todo traco de 118 ms virava ponto.
    //
    // Agora usamos o metodo de Otsu: varremos todos os cortes possiveis e
    // ficamos com o que minimiza a variancia DENTRO dos dois grupos. Ele nao
    // precisa de vao entre os grupos, so que eles sejam compactos. Na mesma
    // gravacao devolveu 28 pontos de 40 ms e 59 tracos de 118 - razao 2,94,
    // contra os 3,00 exatos da norma.
    //
    // Sao no maximo 40 marcas, entao a varredura direta custa 1600 contas:
    // barato demais para justificar somas acumuladas.
    size_t corte = 0;
    double melhorDispersao = -1.0;
    for (size_t i = 1; i < v.size(); ++i) {
        double sa = 0.0, sb = 0.0;
        for (size_t k = 0; k < i; ++k)         sa += v[k];
        for (size_t k = i; k < v.size(); ++k)  sb += v[k];
        const double ma = sa / double(i);
        const double mb = sb / double(v.size() - i);
        double da = 0.0, db = 0.0;
        for (size_t k = 0; k < i; ++k)         da += (v[k] - ma) * (v[k] - ma);
        for (size_t k = i; k < v.size(); ++k)  db += (v[k] - mb) * (v[k] - mb);
        const double dispersao = da + db;
        if (melhorDispersao < 0.0 || dispersao < melhorDispersao) {
            melhorDispersao = dispersao;
            corte = i;
        }
    }

    double nova = unidadeMs_;
    const double medioBaixo = [&]{
        double s = 0; for (size_t i = 0; i < corte; ++i) s += v[i];
        return corte ? s / double(corte) : 0.0;
    }();
    const double medioAlto = [&]{
        double s = 0; for (size_t i = corte; i < v.size(); ++i) s += v[i];
        return (v.size() > corte) ? s / double(v.size() - corte) : 0.0;
    }();

    // A razao traco/ponto tem de dar perto de 3. Se o corte tivesse caido no
    // lugar errado ela sairia longe disso, e e mais seguro nao mexer na
    // unidade do que aceitar uma separacao que a propria norma desmente.
    const double razao = (medioBaixo > 0.0) ? (medioAlto / medioBaixo) : 0.0;
    if (corte >= 2 && medioBaixo > 0 && razao > 2.0 && razao < 4.5) {
        // Ha os dois grupos. O ponto e a media do grupo de baixo, mas o traco
        // tambem informa: ele deveria valer 3 pontos. Combinamos os dois, o
        // que reduz o erro quando um dos grupos tem poucas amostras.
        nova = 0.5 * medioBaixo + 0.5 * (medioAlto / 3.0);
    } else {
        // Um grupo so. Nao da para saber se sao todos pontos ou todos tracos,
        // entao NAO mexemos na unidade - chutar aqui e o que quebrava tudo.
        return;
    }

    unidadeMs_ += (nova - unidadeMs_) * 0.30;
    if (unidadeMs_ < 15.0)  unidadeMs_ = 15.0;    // 80 PPM, limite pratico
    if (unidadeMs_ > 400.0) unidadeMs_ = 400.0;   // 3 PPM
    ppm_ = 1200.0 / unidadeMs_;
}

void CwCore::fecharMarca(double ms)
{
    if (ms < 5.0) return;                 // estalo, nao e elemento
    atualizarUnidade(ms);
    // Ponto ou traco: o corte natural fica em 2 unidades, entre 1 e 3.
    codigo_ += (ms < unidadeMs_ * 2.0) ? '.' : '-';
    if (codigo_.size() > 8) {             // nada valido passa de 8 elementos
        naoLidos_++;
        codigo_.clear();
    }
}

void CwCore::fecharEspaco(double ms)
{
    if (ms < 5.0) return;
    // Entre elementos = 1 unidade; entre letras = 3; entre palavras = 7.
    // Os cortes ficam no meio: 2 e 5.
    if (ms < unidadeMs_ * 2.0) return;              // dentro da mesma letra
    emitirLetra();
    if (ms > unidadeMs_ * 5.0) espacoPendente_ = true;
}

void CwCore::emitirLetra()
{
    if (codigo_.empty()) return;
    if (espacoPendente_) { saida_ += ' '; espacoPendente_ = false; }

    const char* t = morseParaTexto(codigo_);
    if (t) { saida_ += t; ++letras_; }
    else {
        // Sequencia que nao existe na tabela. Mostrar o codigo cru ajuda a
        // conferir no ar se o problema e o sinal ou o decodificador - some
        // com o texto se eu simplesmente engolisse.
        saida_ += '<'; saida_ += codigo_; saida_ += '>';
        ++naoLidos_;
    }
    codigo_.clear();
}

// ---------------------------------------------------------------------------

bool CwCore::estimarTom(const std::vector<float>& buf, double& tom) const
{
    const int N = 4096;
    const size_t NN = size_t(N);
    if (buf.size() < NN * 2) return false;

    std::vector<double> jan(NN, 0.0);
    for (int i = 0; i < N; ++i)
        jan[size_t(i)] = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(N - 1));

    // CW e sinal de RAJADA: a chave passa metade do tempo aberta. Mediar tudo
    // dilui o tom no silencio, entao so entram as janelas com energia.
    std::vector<double> ener;
    for (size_t o = 0; o + NN <= buf.size(); o += NN / 2) {
        double e = 0;
        for (int i = 0; i < N; ++i) { const double v = buf[o + size_t(i)]; e += v * v; }
        ener.push_back(e);
    }
    if (ener.size() < 2) return false;
    std::vector<double> ord = ener;
    std::sort(ord.begin(), ord.end());
    const double corte = ord[ord.size() / 2];

    std::vector<double> P(NN / 2 + 1, 0.0);
    int usadas = 0; size_t idx = 0;
    for (size_t o = 0; o + NN <= buf.size(); o += NN / 2, ++idx) {
        if (idx < ener.size() && ener[idx] < corte) continue;
        std::vector<std::complex<double>> a(NN, std::complex<double>(0.0, 0.0));
        for (int i = 0; i < N; ++i)
            a[size_t(i)] = std::complex<double>(double(buf[o + size_t(i)]) * jan[size_t(i)], 0.0);
        AnaliseCore::fft(a, false);
        for (size_t k = 0; k < P.size(); ++k) P[k] += std::norm(a[k]);
        ++usadas;
    }
    if (usadas < 2) return false;

    const double hzRaia = p_.sampleRate / double(N);
    const size_t kMin = size_t(200.0 / hzRaia);
    const size_t kMax = std::min(P.size() - 2, size_t(2500.0 / hzRaia));
    if (kMax <= kMin + 4) return false;

    std::vector<double> c(P.begin() + long(kMin), P.begin() + long(kMax));
    std::nth_element(c.begin(), c.begin() + long(c.size() / 2), c.end());
    const double mediana = c[c.size() / 2];

    size_t melhor = 0; double vmax = 0;
    for (size_t k = kMin; k < kMax; ++k) if (P[k] > vmax) { vmax = P[k]; melhor = k; }
    if (melhor == 0 || vmax < mediana * 12.0) return false;

    const double y0 = P[melhor - 1], y1 = P[melhor], y2 = P[melhor + 1];
    const double den = y0 - 2.0 * y1 + y2;
    const double d = (std::abs(den) < 1e-20) ? 0.0 : 0.5 * (y0 - y2) / den;
    tom = (double(melhor) + d) * hzRaia;
    return tom > 200.0 && tom < 2500.0;
}

} // namespace masdr
