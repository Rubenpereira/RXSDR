#pragma once
// ---------------------------------------------------------------------------
//  SstvCore - recepcao de SSTV (imagem por radio), tudo dentro do RXSDR
//
//  Cadeia: audio do radio (USB/LSB/FM) -> ~12 kS/s -> mistura em 1900 Hz e
//  passa-baixas complexo -> frequencia instantanea (amostra a amostra).
//  Com a frequencia na mao:
//    * VIS: o cabecalho de 300+300 ms de 1900 Hz e o codigo de 8 bits dizem o
//      modo e, de quebra, o desvio de sintonia (AFC).
//    * Linhas: cada linha tem um pulso de 1200 Hz. Ele e procurado perto de
//      onde deveria estar e uma reta (minimos quadrados) por todos os pulsos
//      achados da o inicio e o periodo REAL das linhas - e a correcao da
//      inclinacao (relogio da placa de som do transmissor fora do nominal).
//    * Pixels: media da frequencia na janela de cada pixel; 1500 Hz = preto,
//      2300 Hz = branco. RGB (Martin, Scottie, SC2) ou Y/R-Y/B-Y (Robot, PD).
//  Ao fim a imagem e redesenhada inteira com a reta final.
// ---------------------------------------------------------------------------
#include <atomic>
#include <complex>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace masdr {

class SstvCore {
public:
    enum Modo { ROBOT36 = 0, ROBOT72, MARTIN1, MARTIN2, SCOTTIE1, SCOTTIE2, SCOTTIEDX, SC2_180,
                PD50, PD90, PD120, PD160, PD180, PD240, PD290, N_MODOS };
    enum Estado { ESPERANDO = 0, RECEBENDO, PRONTA };

    struct Status {
        Estado estado = ESPERANDO;
        int modo = -1;
        int linha = 0, linhas = 0;        // linhas da imagem ja recebidas / total
        double desvioHz = 0;              // erro de sintonia medido (AFC)
        double contraste = 1.0;           // correcao do achatamento pelo ruido
        double inclinacaoPpm = 0;         // relogio do transmissor
        double nivelDb = -99;             // sinal na faixa 1100-2300 Hz
        bool porVis = false;
        int sincronismos = 0, faltas = 0;
        int imagens = 0;                  // imagens terminadas desde o inicio
    };

    static int         nModos() { return N_MODOS; }
    static const char* nomeModo(int m);
    static int         larguraModo(int m);
    static int         alturaModo(int m);
    static int         visModo(int m);
    static double      duracaoModo(int m);   // segundos

    SstvCore();

    // Audio demodulado (16 bits) na taxa do radio; reduz sozinho para ~12 kS/s.
    void alimentar(const int16_t* pcm, size_t n, uint32_t sps);

    void comecarAgora(int modo);          // sem VIS (pegou a transmissao no meio)
    void parar();                         // termina a imagem atual como esta
    void limpar();                        // apaga a tela, volta a esperar o VIS

    std::atomic<bool> aceitarVis{true};
    std::atomic<bool> autoInclinacao{true};

    Status status();
    // copia a imagem se mudou desde 'versao' (que volta atualizada)
    bool imagem(std::vector<uint32_t>& argb, int& w, int& h, uint64_t& versao);
    // espectro do audio (dB), de 0 ate ~3000 Hz
    void espectro(std::vector<float>& db, double& hzPorBin);
    // imagem terminada (para salvar e por no historico); devolve false se nao ha
    bool pegarTerminada(std::vector<uint32_t>& argb, int& w, int& h, int& modo, bool& porVis);

private:
    struct Canal { double ini, dur; char tipo; };   // ms a partir do inicio da linha
    struct ModoDef {
        const char* nome; int vis; int w, h;
        double periodo, syncIni, syncLen, atrasoInicio;   // ms
        int linhasPorPeriodo;                             // 2 nos PD
        bool yuv;
        std::vector<Canal> canais;
    };
    static const std::vector<ModoDef>& modos();

    void reconfigurar(uint32_t sps);
    void processarFreq(std::complex<float> d, float p);
    void detectarVis();
    void iniciarImagem(int modo, double inicioAbs, bool porVis);
    void avancarLinhas();
    bool acharSync(double previsto, double janela, double& achado, double& qualidade);
    void ajustarReta();
    void desenharPeriodo(int k);
    void redesenharTudo();
    void terminarImagem();
    double mediaFreq(double a, double b) const;    // posicoes absolutas (amostras)
    double paraHz(std::complex<double> s) const;
    double posLocal(double abs) const { return abs - double(base_); }

    std::mutex m_;

    // entrada -> ~12 kS/s
    uint32_t sps_ = 0;
    int D_ = 1, faseD_ = 0;
    std::vector<float> h0_, hist0_;
    size_t pos0_ = 0;
    double fs_ = 12000;

    // FM: mistura + passa-baixas complexo + frequencia instantanea
    std::vector<float> h1_;
    std::vector<std::complex<float>> hist1_;
    size_t pos1_ = 0;
    double faseNco_ = 0;
    std::complex<float> zAnt_{0, 0};
    double nivel_ = 0;

    // z[n]*conj(z[n-1]) amostra a amostra: o angulo e a frequencia; somar
    // varios e tirar o angulo depois da a frequencia media PESADA pelo nivel
    // (os estalos de ruido, com nivel baixo, quase nao contam)
    std::vector<std::complex<float>> fr_;
    uint64_t base_ = 0;           // indice absoluto de fr_[0]
    uint64_t total_ = 0;          // amostras ja produzidas

    // VIS: medias de 10 ms a cada 5 ms
    std::deque<float> hops_;
    uint64_t proxHop_ = 0;
    int esperaVis_ = 0;

    // espectro
    std::vector<float> aneisEsp_;
    size_t posEsp_ = 0;

    // imagem atual
    Estado estado_ = ESPERANDO;
    int modo_ = -1;
    bool porVis_ = false;
    double desvio_ = 0;           // Hz
    double ganho_ = 1.0;          // contraste: o ruido puxa tudo para 1900 Hz
    double inicio_ = 0;           // amostra absoluta prevista do 1o sync
    int proxPeriodo_ = 0;
    std::vector<std::pair<int, double>> syncs_;   // (periodo, posicao absoluta)
    double a_ = 0, b_ = 0;        // reta: sync(k) = a + b k
    double bDesenhado_ = 0;
    int faltasSeguidas_ = 0, faltas_ = 0;
    std::vector<float> Y_, U_, V_;               // planos (YUV) ou R,G,B
    std::vector<uint8_t> temU_, temV_;           // a linha ja tem B-Y / R-Y
    bool travado_ = false;
    std::vector<uint32_t> img_;
    int w_ = 0, h_ = 0;
    uint64_t versao_ = 1;
    int imagens_ = 0;

    // terminadas
    struct Terminada { std::vector<uint32_t> argb; int w, h, modo; bool porVis; };
    std::deque<Terminada> prontas_;
};

} // namespace masdr
