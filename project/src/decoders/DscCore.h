#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  DscCore - decodificador DSC (Digital Selective Calling) nativo
//  ITU-R M.493 / M.541 - chamada seletiva digital maritima em HF.
//
//  Substitui o DscManager antigo, que lancava um runner em Python (exigia
//  Python na maquina do usuario final, o que quebrava a distribuicao) e ainda
//  vinha com parametros errados: 200 baud / 200 Hz de shift, que sao numeros
//  de PACTOR-I, nao de DSC. DSC em HF e 100 baud com shift de 170 Hz.
//
//  Cadeia:
//    audio USB -> correlatores mark/space -> decisao de bit ->
//    recuperacao de temporizacao -> simbolos de 10 bits ->
//    sequencia de fase -> diversidade DX/RX -> mensagem M.493 -> texto
//
//  SIMBOLO. Cada simbolo tem 10 bits: os 7 primeiros sao informacao,
//  transmitidos com o bit menos significativo na frente; os 3 ultimos sao a
//  contagem de elementos B (zeros) entre os 7 de informacao, com o mais
//  significativo na frente. E essa contagem que permite descartar simbolo
//  corrompido sem precisar da copia repetida.
//
//  DIVERSIDADE. Como no SITOR-B, cada caractere vai duas vezes. As posicoes
//  pares levam a copia DX e as impares a copia RX do caractere transmitido
//  quatro posicoes antes - ou seja, a RX de um caractere aparece 9 posicoes
//  depois da DX dele.
//
//  FASE. Diferente do SITOR-B, aqui nao e preciso adivinhar o alinhamento:
//  toda transmissao comeca com uma sequencia de fase em que as posicoes DX
//  levam sempre 125 e as RX levam 111, 110, 109 ... 104, em ordem
//  decrescente. Achar esse par ja trava fase e vao de uma vez.
// ---------------------------------------------------------------------------
class DscCore {
public:
    struct Params {
        double sampleRate = 8000.0;   // taxa de trabalho apos decimacao
        double baudRate   = 100.0;    // DSC em HF
        double centerFreq = 1700.0;   // tom central tipico do audio USB
        double shift      = 170.0;    // separacao mark/space
        bool   invert     = false;
    };

    DscCore();
    explicit DscCore(const Params& p);

    void reset();
    void setParams(const Params& p);
    const Params& params() const { return p_; }

    // Entrega audio (float normalizado -1..1). Devolve as linhas de texto
    // prontas desde a chamada anterior; vazio quando nada fechou.
    std::string feed(const float* samples, size_t n);

    // Estatisticas para a interface
    int  simbolos()     const { return simbolos_; }
    int  validos()      const { return validos_; }
    int  corrigidos()   const { return corrigidos_; }   // salvos pela copia RX
    int  mensagens()    const { return mensagens_; }
    bool sincronizado() const { return sincronizado_; }

    // --- partes expostas para teste --------------------------------------
    // Monta os 10 bits de um simbolo, na ordem em que vao para o ar.
    static void codificar(int valor, int bits[10]);
    // Devolve o valor 0..127, ou -1 se a contagem de verificacao nao fecha.
    static int  decodificar(const int bits[10]);
    // Nome legivel de um simbolo de comando/formato, ou string vazia.
    static std::string nomeSimbolo(int v);

private:
    void processarBit(int bit);
    void processarSimbolo(int valor, bool valido);
    void avaliarFase();
    void montarMensagem();
    void emitir(const std::string& linha);

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

    // montagem de simbolos: 10 bits crus, na ordem de chegada
    int  bitsBuf_[10] = {0};
    int  bitsNo_      = 0;

    // fila crua de simbolos (valor ou -1 quando a verificacao falhou)
    std::deque<int> historico_;
    long long idxRecebidos_ = 0;
    long long proximoDx_    = -1;
    int  faseDx_       = 0;      // paridade das posicoes DX
    bool sincronizado_ = false;
    int  desdeAvaliacao_ = 0;

    // caracteres ja combinados DX/RX, aguardando fechar mensagem
    std::vector<int> mensagem_;
    bool  coletando_ = false;

    // saida e contadores
    std::string saida_;
    int simbolos_   = 0;
    int validos_    = 0;
    int corrigidos_ = 0;
    int mensagens_  = 0;
};

} // namespace masdr
