#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  RttyCore - decodificador RTTY (FSK assincrono, codigo Baudot ITA2) nativo
//
//  Existiu um "RTTY" antes, via minimodem_runner.py - mas aquilo era um
//  SIMULADOR: imprimia frases sorteadas ("CQ CQ DE PY1XTBS...") sem ler o
//  sinal. Foi tirado do menu e os arquivos ficaram orfaos. Este e de verdade.
//
//  Cadeia:
//    audio USB (8 kHz) -> dois detectores de tom (mark e space), cada um
//    misturando o seu tom para zero e integrando UM periodo de bit (filtro
//    casado para pulso retangular) -> discriminador com ATC (cada tom
//    normalizado pela propria envoltoria, o que aguenta desvanecimento
//    seletivo, comum em HF) -> maquina de estados assincrona:
//    repouso em MARK, borda para SPACE = bit de partida, 5 bits de dados
//    (o primeiro que chega e o menos significativo), bit de parada em MARK.
//    -> ITA2 com LTRS/FIGS e "unshift on space".
//
//  Convencao de tons: em USB, MARK e o tom mais ALTO (mark e a frequencia de
//  RF mais alta). Em LSB seria o contrario - para isso existe o "Inverter".
//
//  Nao ha codigo corretor no RTTY. O que separa texto de lixo e o bit de
//  parada: com ruido ele sai errado a toda hora. So se imprime com a trava
//  (sincronizado) ligada, e ela cai depois de varios erros de enquadramento.
// ---------------------------------------------------------------------------
class RttyCore {
public:
    struct Params {
        double sampleRate = 8000.0;
        double baudRate   = 45.45;   // radioamador; DWD usa 50
        double shift      = 170.0;   // radioamador; DWD usa 450
        double centerFreq = 1500.0;  // meio entre os dois tons, no audio
        bool   invert     = false;   // troca mark e space
        bool   autoTom    = true;    // mede o tom central sozinho
        bool   usos       = true;    // espaco volta para LETRAS
    };

    RttyCore();
    explicit RttyCore(const Params& p);

    void setParams(const Params& p);
    const Params& params() const { return p_; }
    void reset();

    // Audio float (-1..1) na taxa de trabalho. Devolve o texto decodificado
    // desde a chamada anterior.
    std::string feed(const float* s, size_t n);

    // Estatisticas
    int  totalChars()   const { return totalChars_; }
    int  erros()        const { return erros_; }
    bool sincronizado() const { return sync_; }
    double tomMedido()  const { return tomMedido_; }
    bool tomNovo()      { bool v = tomNovo_; tomNovo_ = false; return v; }

    // Exposto para teste: ITA2 de 5 bits (bit 0 = primeiro recebido)
    static char ita2(int code, bool figuras);

private:
    void configurarTons(double centro);
    void processarAmostra(float x);
    void fecharCaractere(int code, double confianca);
    void medirTom();

    Params p_;
    double spb_ = 176.0;          // amostras por bit
    int    nBit_ = 176;           // janela do filtro casado

    // osciladores e integradores (soma movel de um bit) dos dois tons
    double wMark_ = 0, wSpace_ = 0, fase_ = 0;
    long long n_ = 0;
    std::vector<double> bmI_, bmQ_, bsI_, bsQ_;   // buffers circulares
    double smI_ = 0, smQ_ = 0, ssI_ = 0, ssQ_ = 0;
    // detector de borda mais curto (1/4 de bit), para achar o inicio do start
    int    nCurto_ = 44;
    std::vector<double> cmI_, cmQ_, csI_, csQ_;
    double cmIs_ = 0, cmQs_ = 0, csIs_ = 0, csQs_ = 0;
    size_t pos_ = 0, posC_ = 0;

    // ATC: envoltorias de pico de cada tom
    double pkMark_ = 1e-6, pkSpace_ = 1e-6;
    double vlMark_ = 0.0, vlSpace_ = 0.0;

    // historico do discriminador (para amostrar no meio de cada bit)
    std::vector<float> dHist_;
    size_t dPos_ = 0;

    // maquina assincrona
    enum class Est { Repouso, Quadro } est_ = Est::Repouso;
    double  tBorda_ = 0;          // amostra (absoluta) estimada da borda do start
    int     marcaSeguida_ = 0;    // amostras seguidas em MARK antes da borda
    double  ultCurto_ = 0;

    // ITA2
    bool figuras_ = false;

    // trava
    bool sync_ = false;
    int  bonsSeguidos_ = 0;
    int  errosSeguidos_ = 0;
    std::string pendente_;        // caracteres esperando a trava confirmar

    // medicao do tom
    std::vector<float> tomBuf_;
    double tomMedido_ = 0;
    bool   tomNovo_ = false;
    long long ultimaMedida_ = 0;

    std::string saida_;
    int totalChars_ = 0;
    int erros_ = 0;
};

} // namespace masdr
