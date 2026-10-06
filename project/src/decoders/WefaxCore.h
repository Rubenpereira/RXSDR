#pragma once
// ---------------------------------------------------------------------------
//  WefaxCore - fac-simile meteorologico em HF (WEFAX / HF-FAX), dentro do RXSDR
//
//  O sinal e FM no audio USB: 1500 Hz = preto, 2300 Hz = branco (centro 1900).
//  Uma transmissao tem:
//    * tom de inicio : preto/branco alternando a 300 Hz (IOC 576) ou 675 Hz
//                      (IOC 288) por ~5 s
//    * fase          : ~30 s de linhas pretas com um pulso branco curto (5%)
//                      no comeco de cada linha - diz ONDE a linha comeca
//    * imagem        : LPM linhas por minuto (120 quase sempre), cada linha
//                      com IOC*pi pixels (1809 no IOC 576)
//    * tom de fim    : 450 Hz por ~5 s, depois preto
//  Aqui: audio -> ~12 kS/s -> mistura em 1900 Hz + passa-baixas complexo ->
//  d[n] = z[n]*conj(z[n-1]); o angulo da SOMA numa janela e a frequencia media
//  (pesada pelo nivel - aguenta estalo de ruido). Os tons de inicio/fim saem
//  de um Goertzel no "brilho" instantaneo. Na fase, a "dobra" do sinal no
//  periodo de cada LPM candidato mostra o pulso: o LPM com o pulso mais
//  nitido e o certo, e a posicao do pulso e o comeco da linha.
//  Inclinacao (relogio fora) e alinhamento sao so desenho: cada linha e
//  girada de (desloc + k * inclinacao) pixels - da para acertar depois.
// ---------------------------------------------------------------------------
#include <atomic>
#include <complex>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace masdr {

class WefaxCore {
public:
    enum Estado { ESPERANDO = 0, INICIO, FASE, RECEBENDO, PRONTA };
    struct Status {
        Estado estado = ESPERANDO;
        int lpm = 120, ioc = 576, largura = 1809, linhas = 0;
        double nivelDb = -99;
        double inclinacao = 0;        // pixels por linha
        int deslocamento = 0;         // pixels
        bool alinhadoPelaFase = false;
        int imagens = 0;
    };
    struct Terminada { std::vector<uint8_t> cinza; int w = 0, h = 0, lpm = 120, ioc = 576; };

    WefaxCore();

    void alimentar(const int16_t* pcm, size_t n, uint32_t sps);

    void configurar(int lpm, int ioc);    // valores do comecar manual (e do padrao sem fase)
    void comecarAgora();                  // sem tom de inicio: comeca ja, alinhar depois
    void parar();                         // termina a imagem atual (vai para a fila de prontas)
    void limpar();

    std::atomic<bool> automatico{true};   // tom de inicio/fim comandam
    std::atomic<bool> inverter{false};    // preto <-> branco
    std::atomic<bool> autoInclinacao{true};   // endireita sozinho ao terminar

    void definirInclinacao(double pxPorLinha);
    void alinharEm(int coluna);           // essa coluna (da imagem mostrada) vira a margem esquerda
    bool endireitar();                    // mede a inclinacao pelas linhas verticais do mapa

    Status status();
    // imagem em meia resolucao (para a tela), com inclinacao/deslocamento aplicados
    bool imagemMeia(std::vector<uint32_t>& argb, int& w, int& h, uint64_t& versao);
    bool pegarTerminada(Terminada& t);

    // --- exposto para teste ---
    std::vector<uint8_t> imagemFinal(int& w, int& h);

private:
    void reconfigurar(uint32_t sps);
    void processar(std::complex<float> d, float p);
    void blocoTons();
    void analisarFase(bool ultimaChance);
    void iniciarRecepcao(double inicioAbs, int lpm, int ioc, bool pelaFase);
    void avancarLinhas();
    void terminar(int cortarLinhas);
    bool endireitarSemTrava();
    double mediaV(double a, double b) const;      // brilho medio 0..1 entre amostras absolutas
    std::vector<uint8_t> montar(int& w, int& h) const;   // aplica giro de cada linha

    std::mutex m_;

    // entrada -> ~12 kS/s
    uint32_t sps_ = 0;
    int D_ = 1, faseD_ = 0;
    std::vector<float> h0_, hist0_;
    size_t pos0_ = 0;
    double fs_ = 12000;
    std::vector<float> h1_;
    std::vector<std::complex<float>> hist1_;
    size_t pos1_ = 0;
    double faseNco_ = 0;
    std::complex<float> zAnt_{0, 0};
    double nivel_ = 0;

    // historico de d[n]
    std::vector<std::complex<float>> buf_;
    uint64_t base_ = 0, total_ = 0;

    // tons (Goertzel no brilho instantaneo, blocos de 0,25 s)
    std::vector<float> blocoV_;
    int seg300_ = 0, seg675_ = 0, seg450_ = 0;
    int iocInicio_ = 576;

    // estado
    Estado estado_ = ESPERANDO;
    int lpmCfg_ = 120, iocCfg_ = 576;
    uint64_t faseIni_ = 0;
    int lpm_ = 120, ioc_ = 576, W_ = 1809;
    double periodo_ = 6000;       // amostras por linha
    double inicio_ = 0;           // amostra absoluta do comeco da linha 0
    int proxLinha_ = 0;
    bool pulandoFase_ = false;
    int pulados_ = 0;
    bool pelaFase_ = false;
    std::vector<uint8_t> linhas_;  // W_ * H
    int H_ = 0;
    double incl_ = 0;
    double desloc_ = 0;
    uint64_t versao_ = 1;
    int imagens_ = 0;
    std::deque<Terminada> prontas_;
};

} // namespace masdr
