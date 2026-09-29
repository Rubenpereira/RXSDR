#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  SitorBCore - decodificador SITOR-B / NAVTEX nativo
//
//  Substitui o runner em Python que existia antes. Aquele exigia Python
//  instalado (quebrava na maquina dos amigos), validava o CCIR 476 contra o
//  numero errado de bits e - o mais grave - nao implementava a diversidade
//  temporal, que e justamente o que da robustez ao SITOR-B.
//
//  Cadeia, na mesma ideia da do OpenWebRX+:
//    audio USB -> correlatores mark/space -> decisao de bit ->
//    recuperacao de temporizacao -> caracteres de 7 bits ->
//    FEC (DX/RX) -> CCIR 476 -> texto
//
//  SITOR-B manda cada caractere DUAS vezes, separadas no tempo: a copia DX e,
//  mais adiante, a copia RX. Se a DX chega corrompida, usa-se a RX. O
//  alinhamento entre as duas depende da convencao do transmissor, entao ele e
//  descoberto sozinho: testamos os atrasos candidatos e ficamos com o que
//  produz mais caracteres validos.
// ---------------------------------------------------------------------------
class SitorBCore {
public:
    struct Params {
        double sampleRate = 8000.0;   // taxa de trabalho apos decimacao
        double baudRate   = 100.0;    // SITOR-B/NAVTEX
        double centerFreq = 1700.0;   // tom central tipico do audio USB
        double shift      = 170.0;    // separacao mark/space
        bool   invert     = false;
        bool   autoTom    = true;     // mede o tom central sozinho
    };

    SitorBCore();                              // usa os valores padrao
    explicit SitorBCore(const Params& p);

    void reset();
    void setParams(const Params& p);
    const Params& params() const { return p_; }

    // Entrega audio (float normalizado -1..1). Devolve o texto decodificado
    // desde a chamada anterior; string vazia quando nada fechou.
    std::string feed(const float* samples, size_t n);

    // Estatisticas para a interface
    int  totalChars()  const { return totalChars_; }
    int  validChars()  const { return validChars_; }
    int  corrigidos()  const { return corrigidos_; }   // salvos pela copia RX
    int  vaoFec()      const { return vaoFec_; }
    bool sincronizado() const { return sincronizado_; }

    // --- partes expostas para teste -------------------------------------
    // Tabela CCIR 476: codigo de 7 bits -> indice ITA2 (5 bits). So os codigos
    // com exatamente 4 bits em '1' existem.
    static bool ccir476Valido(uint8_t code);
    static int  ccir476ParaIta2(uint8_t code);   // -1 se invalido

private:
    void processarBloco();
    void processarCaractere(uint8_t code);
    void emitirIta2(int ita2);
    void processarServico(uint8_t code);   // SIA, SIB, RPT

    Params p_;

    // correlatores
    std::vector<float> refMarkCos_, refMarkSin_, refSpaceCos_, refSpaceSin_;
    std::vector<float> janela_;
    size_t janelaPos_ = 0;
    size_t janelaLen_ = 0;
    bool   janelaCheia_ = false;

    // Temporizacao por BUSCA DIRETA de fase.
    //
    // Antes havia uma malha perseguindo transicoes. Ela nunca escolhia a fase
    // certa: nos 13 min de trafego real de 12.579 dava 38% de codigos validos,
    // enquanto escolher a melhor fase por trecho dava 99%. A informacao estava
    // toda no sinal - era a malha que decidia mal, e ajustar as constantes
    // dela so levou de 337 para 384 caracteres. Trocar o metodo resolve; afinar
    // o metodo errado, nao.
    //
    // Agora o discriminador e acumulado num bloco de ~2 s e, quando ele enche,
    // testamos TODAS as fases e os 7 alinhamentos de caractere, ficando com a
    // combinacao que produz mais codigos de peso 4 - a regra do CCIR 476, que
    // e o unico criterio que nao depende de suposicao nossa.
    double amostrasPorBit_ = 80.0;
    std::vector<float> blocoD_;
    int    bitsPorBloco_ = 200;

    // montagem de caracteres
    uint32_t shiftReg_ = 0;
    int      bitsNoReg_ = 0;

    // FEC: o fluxo alterna uma copia DX e uma copia RX. A RX de um caractere
    // aparece um numero fixo de posicoes depois da DX dele. Guardamos a fila
    // crua e so emitimos quando as duas copias ja chegaram.
    std::deque<uint8_t> historico_;
    long long idxRecebidos_ = 0;   // indice absoluto do proximo codigo
    long long proximoDx_    = -1;  // indice DX ainda por emitir
    int  faseDx_        = 0;       // paridade das posicoes DX
    int  vaoFec_        = 11;      // distancia em posicoes entre DX e RX
    bool sincronizado_  = false;
    int  desdeAvaliacao_ = 0;
    int  ruimSeguidos_  = 0;   // caracteres invalidos em sequencia

    // medicao automatica do tom central
    std::vector<float> tomBuf_;
    bool   tomPronto_ = false;
    double tomMedido_ = 0.0;
    bool   estimarTom(const std::vector<float>& buf, double& centro) const;
    double refinarTom(const std::vector<float>& buf, double centroInicial) const;
    void   aplicarTom(double centro);

    // Folga minima entre o melhor codigo e o segundo colocado para acreditar
    // na decisao. Abaixo disso o caractere vira apagamento e quem decide e a
    // copia DX/RX. O valor foi escolhido medindo em sinal real degradado.
    static constexpr double kMargemMinima = 0.12;

    // estado ITA2
    bool figuras_ = false;

    // saida e contadores
    std::string saida_;
    int totalChars_ = 0;
    int validChars_ = 0;
    int corrigidos_ = 0;
};

} // namespace masdr
