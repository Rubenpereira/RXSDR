#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  AleCore - receptor de ALE 2G (MIL-STD-188-141A / 141B apendice A /
//  FED-STD-1045 - e o mesmo sinal nas tres normas), so recepcao.
//
//  Sinal: 8-FSK, tons de 750 a 2500 Hz de 250 em 250 Hz, 125 simbolos/s
//  (8 ms), 3 bits por tom em codigo Gray. Cada palavra ALE tem 24 bits:
//  3 de preambulo (TO, TIS, TWAS, DATA, REP, CMD, THRU, FROM) + 3 caracteres
//  de 7 bits. A palavra vira dois codigos Golay(24,12) - o segundo com os
//  12 bits de paridade invertidos -, os 48 bits sao entrelacados (+1 bit de
//  enchimento = 49) e mandados TRES vezes seguidas: 147 bits = 49 tons =
//  392 ms por palavra.
//
//  Recepcao:
//   - energia numa janela de 1 simbolo (64 amostras a 8 kHz), a cada amostra,
//     numa grade de 62,5 Hz de 250 a 3000 Hz (45 frequencias). A grade fina
//     e o que deixa achar o sinal fora do ponto: quem sintoniza 11.491 num
//     canal publicado como 11.490,5 poe os tons 500 Hz abaixo do esperado;
//   - procura: a cada 2 amostras, e para cada deslocamento de -500 a +500 Hz
//     (passos de 62,5), tenta ler uma palavra inteira terminando ali. Um
//     filtro rapido (quantos dos 48 bits tem as 3 copias iguais, com a
//     decisao de tom ja guardada) descarta quase tudo; o que passa vai para o
//     voto "suave" (pesa a forca dos tons, nao so 0/1) e para o Golay;
//   - trava quando a palavra passa no Golay com ate 3 bits corrigidos, tem
//     >= 30 votos unanimes e caracteres validos. O deslocamento de tom
//     achado vale para a chamada inteira. Travado, le palavra por palavra no
//     compasso de 392 ms, reajustando alguns microssegundos de deriva, ate
//     uma falhar: ai a chamada terminou.
//
//  Referencias: MIL-STD-188-141 apendice A; mapeamento de tons, ordem do
//  entrelacamento e base do Golay conferidos com o openALE (licenca MIT,
//  github.com/dl3hc/openALE). Validado contra a saida do Sorcerer numa
//  gravacao real em 14.109 USB (as tres chamadas batem letra por letra) e
//  contra o MultiPSK numa gravacao da Marinha em 11.490,5 USB.
// ---------------------------------------------------------------------------
class AleCore {
public:
    AleCore();
    void reset();

    // Audio float (-1..1) a 8 kHz. Devolve as chamadas que terminaram neste
    // trecho, ja em texto: "[TO][KT0G][TIS][W6HIQ]".
    std::vector<std::string> feed(const float* s, size_t n);

    bool   sincronizado() const { return travado_; }
    int    palavras()     const { return totalPalavras_; }
    int    chamadas()     const { return totalChamadas_; }
    // Deslocamento de tom da ultima chamada lida (Hz; + = tons acima do certo)
    double desvioHz()     const { return ultimoDesvio_ * kGrade; }

    // Exposto para teste
    static bool golayDecodifica(uint32_t cw, uint16_t& info, int& erros);
    static uint32_t golayCodifica(uint16_t info);

    static constexpr int kFs  = 8000;
    static constexpr int kSps = 64;              // amostras por simbolo
    static constexpr int kW   = 49 * kSps;       // amostras por palavra (3136)

    // grade de frequencias: 62,5 Hz (meio "bin" de uma janela de 64 amostras)
    static constexpr int    kSub   = 2;                  // subdivisoes de 125 Hz
    static constexpr double kGrade = 125.0 / kSub;       // 62,5 Hz
    static constexpr int    kMin   = 250;                // Hz, bin 0
    static constexpr int    kNb    = (3000 - kMin) * kSub / 125 + 1;   // 45 bins
    static constexpr int    kDesvioMax = 4 * kSub;       // +-500 Hz em passos da grade
    static constexpr int    kNd    = 2 * kDesvioMax + 1; // 17 deslocamentos

private:
    struct Palavra {
        bool ok = false;
        int  tipo = -1;       // 0 DATA 1 THRU 2 TO 3 TWAS 4 FROM 5 TIS 6 CMD 7 REP
        char c[3] = {0, 0, 0};
        int  erros = 99;      // bits corrigidos pelo Golay (as duas metades)
        int  unanimes = 0;    // dos 48 bits, quantos tiveram as 3 copias iguais
        float energia = -1e30f; // soma do log do tom mais forte nos 49 simbolos
    };
    static bool melhorQue(const Palavra& a, const Palavra& b);
    static bool ehEndereco(int tipo) { return tipo >= 1 && tipo <= 5; }

    void    amostra(float x);
    static int binDoTom(int rank, int desvio) { return 4 * kSub + 2 * kSub * rank + desvio; }
    Palavra avaliar(long long t0, int desvio, bool soQuandoUnanime) const;
    bool    caracteresValidos(const Palavra& p) const;
    Palavra melhorPerto(long long t, int raio, int desvio, int raioDesvio,
                        long long& tMelhor, int& desvioMelhor) const;
    void    fecharChamada(std::vector<std::string>& saida);
    static std::string textoDaChamada(const std::vector<Palavra>& ps);

    // historico, por janela de 1 simbolo (indexado pelo inicio da janela):
    //   logE_: log da energia em cada um dos kNb bins
    //   dura_: para cada deslocamento, o simbolo (0..7) do tom mais forte
    static constexpr int kHist = 7 * kW;   // da para voltar 5 palavras ao travar
    std::vector<float>   logE_;   // kHist * kNb
    std::vector<uint8_t> dura_;   // kHist * kNd
    long long n_ = 0;             // amostras recebidas
    long long ultimaJanela_ = -1; // inicio da janela mais nova ja calculada

    // correlatores (soma movel de 64 amostras)
    std::vector<float>  bufI_, bufQ_;   // kNb * kSps
    std::vector<double> somaI_, somaQ_; // kNb
    std::vector<int>    fase_;          // kNb, indice na tabela de fases
    int pos_ = 0;

    // maquina
    bool      travado_ = false;
    int       desvio_ = 0;        // deslocamento da chamada em curso (passos da grade)
    int       ultimoDesvio_ = 0;  // da ultima chamada mostrada
    int       desvioChamada_ = 0; // da chamada em curso, na trava
    long long proxBusca_ = 0;     // proxima posicao de palavra a testar (procura)
    long long candidato_ = -1;    // achou algo: espera o refinamento
    int       candDesvio_ = 0;
    long long tPalavra_ = 0;      // inicio da ultima palavra aceita (travado)
    std::vector<Palavra> chamada_;

    int totalPalavras_ = 0;
    int totalChamadas_ = 0;
};

} // namespace masdr
