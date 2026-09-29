#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  CwCore - decodificador de telegrafia (CW / Morse)
//
//  Diferenca fundamental para o SITOR-B e o DSC: ali a velocidade e fixa por
//  norma - 100 baud, sempre. Aqui NAO existe velocidade padrao. Cada operador
//  manda no ritmo que quer, entre 5 e 40 palavras por minuto, e o mesmo
//  operador acelera e desacelera dentro de uma mesma transmissao.
//
//  Por isso o decodificador nao pode ter uma velocidade configurada: ele
//  precisa MEDIR o ponto continuamente e se adaptar. Todo o resto - separar
//  ponto de traco, letra de palavra - sai dessa unica medida.
//
//  Cadeia: audio -> tom medido sozinho -> envoltoria -> limiar com histerese
//  -> duracoes de marca e espaco -> classificacao em pontos e tracos ->
//  tabela Morse -> texto.
// ---------------------------------------------------------------------------
class CwCore {
public:
    struct Params {
        double sampleRate = 8000.0;
        double tomHz      = 0.0;    // 0 = medir sozinho
        bool   autoTom    = true;
    };

    CwCore();
    explicit CwCore(const Params& p);

    void setParams(const Params& p);
    void reset();

    // Recebe audio (float -1..1) e devolve o texto decodificado desde a
    // ultima chamada.
    std::string feed(const float* samples, size_t n);

    // --- leitura de estado, para a tela ---------------------------------
    double tomMedido() const { return tomMedido_; }
    double ppm()       const { return ppm_; }        // palavras por minuto
    int    letras()    const { return letras_; }
    int    naoLidos()  const { return naoLidos_; }   // sequencias sem letra

    static const char* morseParaTexto(const std::string& codigo);

private:
    void  processarAmostra(float env);
    void  fecharMarca(double ms);
    void  fecharEspaco(double ms);
    void  emitirLetra();
    void  atualizarUnidade(double msMarca);
    bool  estimarTom(const std::vector<float>& buf, double& tom) const;

    Params p_;

    // deteccao do tom e envoltoria
    double faseI_ = 0.0, faseQ_ = 0.0;
    double passoCos_ = 1.0, passoSen_ = 0.0;
    double oscI_ = 1.0, oscQ_ = 0.0;
    std::vector<double> mediaI_, mediaQ_;
    size_t mediaPos_ = 0, mediaLen_ = 160;
    double somaI_ = 0.0, somaQ_ = 0.0;

    // limiar adaptativo
    double piso_ = 0.0, pico_ = 0.0;
    bool   ligado_ = false;
    double amostrasNoEstado_ = 0.0;

    // Estado CRU do comparador, antes do amortecimento. Ele oscila; o
    // ligado_ acima so acompanha depois que a mudanca se sustenta.
    bool   bruto_ = false;
    double persistenciaMs_ = 0.0;

    // ritmo
    double unidadeMs_ = 60.0;         // duracao do ponto, em milissegundos
    std::deque<double> marcasRecentes_;
    double ppm_ = 0.0;

    std::string codigo_;              // pontos e tracos da letra em montagem
    std::string saida_;
    int letras_ = 0;
    int naoLidos_ = 0;
    bool espacoPendente_ = false;

    // medicao automatica do tom
    std::vector<float> tomBuf_;
    bool   tomPronto_ = false;
    double tomMedido_ = 0.0;
};

} // namespace masdr
