#pragma once
// ---------------------------------------------------------------------------
//  Ui - a tela do RXSDR Nativo
//
//  Desenhada com Dear ImGui sobre Direct3D 9 (existe em todo Windows desde o
//  XP, inclusive no 7 do amigo e no 11). A aparencia copia a do RXSDR de
//  navegador: mesmas cores, S-meter de ponteiro, olho magico, frequencia em
//  digitos, espectro dourado e a cachoeira com a paleta Eclipse do
//  OpenWebRX+.
// ---------------------------------------------------------------------------
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <d3d9.h>

#include "../app/Radio.h"

struct ImFont;
struct ImDrawList;
struct ImVec2;

namespace masdr {

struct Fontes {
    ImFont* normal = nullptr;
    ImFont* negrito = nullptr;
    ImFont* pequena = nullptr;
    ImFont* pequenaNeg = nullptr;      // rotulos dos gadgets (negrito)
    ImFont* digitos = nullptr;
    ImFont* digitosPeq = nullptr;
    ImFont* mono = nullptr;            // texto dos decodificadores
    ImFont* freqMouse = nullptr;       // frequencia ao lado do mouse (espectro/cachoeira)
};

class Ui {
public:
    Ui(Radio& radio, IDirect3DDevice9* dev, const Fontes& f, float escala);
    ~Ui();

    void quadro();            // monta a tela inteira (uma vez por quadro)
    void salvarEstado();      // grava sliders/sintonia no RXSDR.ini
    void prepararSaida() { devolverModo(); }   // fechando: o radio volta ao modo de antes do decoder
    bool querSair() const { return sair_; }

private:
    // --- partes da tela ---
    void barraTopo();
    void painelControles(float y, float h);
    void painelEsquerdo(float x, float y, float w, float h);
    void smeter(float x, float y, float w, float h);
    void mostradorFreq(float x, float y, float w, float h);
    void olhoMagico(float x, float y, float w, float h);
    void grupoSliders(float x, float y, float w, float h, int qual);
    void linhaModos(float y, float h);
    void espectro(float x, float y, float w, float h);
    void escala(float x, float y, float w, float h);
    void cachoeira(float x, float y, float w, float h);
    void rodape(float y, float h);
    void janelaConfig();
    void gravarAjustes();                                   // campos da janela -> Config
    bool trocarAparelho(const std::string& tipo, const std::string& serial);
    void janelaSobre();
    void janelaDecoders();
    void anexarDecTexto(const std::string& novo);
    void devolverModo();
    bool decModoGuardado_ = false;
    std::string decModoAntes_;
    int decBwAntes_ = 0;
    // HFDL / AIS / APRS mudam a frequencia (e o HFDL a taxa): ao fechar, volta
    uint64_t decFreqAntes_ = 0; unsigned decTaxaAntes_ = 0;
    bool decMudouFreq_ = false, decMudouTaxa_ = false;
    int decHfdlBanda_ = 5, decAprsCanal_ = 0, decDrmSel_ = -1;
    void sintonizarDecoder(int tipo);
    void tabelaAvioes(const char* id, const std::vector<AeronaveHfdl>& avs);
    // squelch automatico (clique no SQL)
    bool sqlMedindo_ = false;
    double sqlIni_ = 0, sqlUlt_ = 0;
    std::vector<float> sqlAmostras_;
    std::string sqlDica_;
    void ifDisplay(float x, float y, float w, float h);
    void criarTexturaIf();
    void recolorirIf();
    void linhaIf();
    void escolherDecoder(int tipo, bool sintonizarModo);

    // --- logica ---
    void novoQuadroFft();
    void atualizarSmeter(float dt);
    void atualizarSom();
    void sintonizar(uint64_t hz, bool arredondar);
    void centrarParaZoom(bool forcar);   // com zoom: estacao no meio, com o LO do dongle fora do canal
    void mudarModo(const std::string& m);
    void ligarDesligar();
    void autoCachoeira();                 // botao AUTO (igual a pagina)
    void seguirAuto();                    // AUTO aceso: acompanha o piso
    double ultSeguirAuto_ = 0;
    double wfAcum_ = 0;                  // linhas da cachoeira por desenhar (Speed)
    void padrao();                        // botao PADRAO: guarda a aparencia atual
    bool medirPiso(float& piso) const;    // percentil 25 do espectro visivel
    void recolorirCachoeira();
    void reguaMemorias(float x, float y, float w, float h);
    void janelaMemorias();
    void carregarMemorias();
    void salvarMemorias();
    void alternarGravacao();
    void sintonizarMemoria(size_t i);
    void criarTexturaCachoeira(int largura);
    void linhaCachoeira();
    void carregarImagens();
    bool interacaoEspectro(float x, float y, float w, float h, const char* id);
    double hzParaX(double hz, float x, float w) const;
    double xParaHz(float px, float x, float w) const;
    std::string fmtFreq(uint64_t hz) const;
    bool botao(const char* rot, float x, float y, float w, float h, bool ativo,
               unsigned corAtiva = 0, bool borda = true);
    bool slider(const char* id, const char* rotulo, const char* valor, float* v,
                float mn, float mx, float x, float y, float w, float passoRoda, unsigned corRotulo = 0,
                unsigned corValor = 0, unsigned corTrilho = 0);

    Radio& r_;
    IDirect3DDevice9* dev_;
    Fontes f_;
    float s_;                       // escala (DPI)
    bool sair_ = false;

    // estado da tela (vai para o .ini)
    float vol_ = 50, sql_ = -90, nb_ = 0, nr_ = 0, tom_ = 50, zoom_ = 0;
    float wfRange_ = 55, wfBrilho_ = 96, wfSpeed_ = 2, ganhoRf_ = 0;
    bool  nrEsp_ = false; float nrEspForca_ = 40;
    bool  mudo_ = false;
    int   passo_ = 1000;
    bool  autoWf_ = true;

    // S-meter
    float smAlvo_ = 0.05f, smAtual_ = 0.05f, smPico_ = 0.05f;
    // S-meter no compasso do som: o alvo vai para uma fila e sai atrasado pelo
    // tempo que o audio leva na fila da placa - o ponteiro mexe junto com o que se ouve
    std::vector<std::pair<double, float>> smHist_;
    float smRetorno_ = 0.9f;        // segundos: quanto o ponteiro demora para voltar (CONFIGURACAO)
    double smPicoT_ = 0;
    float peakDb_ = -120, peakDbSql_ = -120;
    double hfPiso_ = 1e9;
    bool sqlAberto_ = true;
    float olho_ = 0;

    // FFT / espectro
    QuadroFft fft_;
    std::vector<float> suave_, picoHold_;
    double ultimoQuadroT_ = 0, ultimaLinhaT_ = 0;
    uint64_t centroMostrado_ = 0;
    uint32_t taxaMostrada_ = 1024000;

    // cachoeira
    IDirect3DTexture9* wfTex_ = nullptr;
    int wfW_ = 0, wfH_ = 1200, wfPos_ = 0, wfLinhas_ = 0;
    std::vector<uint32_t> lut_;
    float lutRef_ = 1e9, lutRange_ = 1e9;
    std::vector<int8_t> wfDb_;          // dB de cada ponto (para recolorir tudo)
    uint64_t dadosCentro_ = 0; uint32_t dadosTaxa_ = 0;   // centro/taxa do ultimo quadro
    bool recolorir_ = false;
    double ultRecolor_ = 0;
    float wfAcimaPiso_ = 45, wfRangeCal_ = 53;
    std::string padraoMsg_;
    double padraoMsgAte_ = 0;

    // imagens
    IDirect3DTexture9* texSmeter_ = nullptr;
    IDirect3DTexture9* texOlho_ = nullptr;

    // arrastar
    bool arrastando_ = false;
    bool arrastandoBw_ = false;
    int  bordaBw_ = 0;                   // -1 esquerda, +1 direita
    float arrastoX0_ = 0, arrastoW_ = 1;
    uint64_t arrastoCentro0_ = 0, arrastoVfo0_ = 0;
    double centroVisual_ = 0;            // centro do que se desenha (arrasto)
    double alvoCentro_ = 0, alvoAte_ = 0;
    double ultTuneArrasto_ = 0;
    double muteAte_ = 0;

    // janelas
    bool cfgAberta_ = false, sobreAberta_ = false;
    std::string erro_;
    double erroAte_ = 0;
    char freqTxt_[32] = {0};
    bool editandoFreq_ = false;

    // config em edicao
    std::vector<DispositivoInfo> lista_;
    int cfgTipo_ = 0, cfgDisp_ = 0, cfgTaxa_ = 0, cfgQ_ = 2, cfgFft_ = 3;
    char cfgHost_[64] = "127.0.0.1"; int cfgPorta_ = 1234;
    bool cfgAgc_ = false, cfgBias_ = false; int cfgPpm_ = 0; float cfgGanho_ = 49.6f;
    int cfgLna_ = 9, cfgIfGain_ = 40; bool cfgIfAgc_ = false;
    int cfgS9Hf_ = -94, cfgS9Vhf_ = -93;
    // ajustes ao vivo (valem na hora, sem Aplicar)
    std::string cfgAssin_;          // assinatura do que ja foi aplicado
    double cfgUltAplic_ = 0;
    std::string cfgMsg_; bool cfgMsgErro_ = false;   // mensagem dentro da janela
    // teste do rtl_tcp em segundo plano (a tela nao congela)
    struct TesteTcp { std::atomic<int> estado{0}; std::string erro, info; };   // 0 testando, 1 ok, -1 falhou
    std::shared_ptr<TesteTcp> tcpTeste_;
    std::string tcpTesteAlvo_;

    // memorias (bookmarks.json ao lado do exe - mesmo formato do OpenWebRX)
    struct Memoria {
        std::string nome, modo, desc, modOriginal, alvo, pais;
        uint64_t f = 0;
        int bc = -1;                                   // -1 sem marca, 0 nao, 1 broadcast
        std::vector<std::pair<int, int>> hor;          // horarios UTC (hhmm-hhmm)
    };
    int memFiltroTipo_ = 0;         // 0 todas, 1 utilitarias, 2 bc no ar, 3 bc no ar Americas, 4 bc todas
    bool ehBroadcast(const Memoria& m) const;
    bool passaFiltro(const Memoria& m) const;
    void menuMemorias();
    bool espSobre_ = false;         // mouse em cima do espectro (esconde a marca na cachoeira)
    float espFrac_ = 0.32f;         // altura do espectro (fracao) - arrastavel pela regua
    std::vector<Memoria> mem_;
    bool memVisivel_ = true, memAberta_ = false, memSujo_ = false;
    char memFiltro_[64] = {0};
    char memNome_[96] = {0};
    int memSel_ = -1;

    // decodificadores
    bool decAberta_ = false;
    int  decTipo_ = 0;
    Decoders::Ajustes decAj_;
    std::string decTexto_;
    bool decRodando_ = false, decSelTudo_ = false;
    int  decSelPend_ = 0, decSelFeito_ = 0;
    int  decCol_ = 0, decLinhas_ = 0;
    int decCanalRtty_ = -1, decCanalSitor_ = -1, decCanalDsc_ = -1, decCanalAle_ = -1, decCanalPactor_ = -1;

    // SSTV
    void painelSstv();
    void atualizarSstv();             // textura da imagem atual; terminadas -> PNG + historico
    IDirect3DTexture9* sstvTex_ = nullptr;
    int sstvTexW_ = 0, sstvTexH_ = 0, sstvW_ = 0, sstvH_ = 0;
    uint64_t sstvVer_ = 0;
    std::vector<uint32_t> sstvBuf_;
    struct SstvFeita { IDirect3DTexture9* tex = nullptr; int w = 0, h = 0; std::string arquivo, rotulo; };
    std::vector<SstvFeita> sstvHist_;
    int sstvModoManual_ = 2, sstvFreqSel_ = -1;
    bool sstvSalvar_ = true, sstvAumentar_ = false;
    std::vector<float> sstvEsp_;

    // WEFAX
    void painelWefax();
    void atualizarWefax();            // textura da imagem (meia resolucao) + historico
    IDirect3DTexture9* wfxTex_ = nullptr;
    int wfxTexW_ = 0, wfxTexH_ = 0, wfxW_ = 0, wfxH_ = 0;
    uint64_t wfxVer_ = 0;
    std::vector<uint32_t> wfxBuf_;
    std::vector<SstvFeita> wfxHist_;
    int decCanalWefax_ = -1, wfxLpm_ = 120, wfxIoc_ = 576;
    bool wfxAlinhar_ = false, wfxSalvar_ = true;

    // IF DISPLAY
    bool ifOn_ = false;
    int  ifTam_ = 1;                    // 0 estreita, 1 media, 2 larga
    IDirect3DTexture9* ifTex_ = nullptr;
    int  ifW_ = 512, ifH_ = 600, ifPos_ = 0;
    std::vector<int8_t> ifDb_;

    // saidas de som (placas)
    std::vector<std::string> saidas_;
    int cfgSaida_ = 0;

    // CPU
    double cpuT_ = 0; unsigned long long cpuProc_ = 0, cpuWall_ = 0; float cpu_ = 0;

    // ultimo ajuste mandado ao som
    AjusteAudio ajEnviado_;
    bool ajPrimeiro_ = true;
};

} // namespace masdr
