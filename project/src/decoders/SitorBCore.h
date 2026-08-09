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
    void processarBit(int bit);
    void processarCaractere(uint8_t code);
    void emitirIta2(int ita2);

    Params p_;

    // correlatores
    std::vector<float> refMarkCos_, refMarkSin_, refSpaceCos_, refSpaceSin_;
    std::vector<float> janela_;
    size_t janelaPos_ = 0;
    size_t janelaLen_ = 0;
    bool   janelaCheia_ = false;

    // temporizacao
    double amostrasPorBit_ = 80.0;
    double fase_           = 0.0;
    float  ultimoD_        = 0.0f;

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

    // estado ITA2
    bool figuras_ = false;

    // saida e contadores
    std::string saida_;
    int totalChars_ = 0;
    int validChars_ = 0;
    int corrigidos_ = 0;
};

} // namespace masdr
