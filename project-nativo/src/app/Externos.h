#pragma once
// ---------------------------------------------------------------------------
//  Externos - HFDL, AIS e APRS pelos mesmos programas do RXSDR principal
//
//    HFDL : dumphfdl.exe   <- IQ CRU do dongle (CF32), a banda inteira
//    AIS  : AIS-catcher.exe <- IQ com o VFO em 162,000 MHz, 192 kS/s (CS16),
//                              por um cano nomeado do Windows
//    APRS : direwolf.exe   <- audio do FM/USB em 48 kHz (16 bits);
//                              os pacotes chegam pela porta AGW (tempo real)
//
//  Os programas ficam na pasta "decoders" ao lado do RXSDR.exe (o AIS-catcher
//  em decoders\ais, sozinho com as DLLs de 64 bits dele).
//
//  Quem chama (thread do dongle / dos decodificadores) nunca espera: o que vai
//  para o programa entra numa fila e uma thread propria escreve no cano.
// ---------------------------------------------------------------------------
#include <atomic>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace masdr {

// Um programa de fora: entrada por stdin (ou cano nomeado), saida por linhas.
class Externo {
public:
    ~Externo() { parar(); }
    // pipeNomeado vazio: escreve no stdin. Senao cria \\.\pipe\<nome> e o
    // programa o abre como arquivo de entrada.
    bool iniciar(const std::wstring& exe, const std::wstring& args, const std::wstring& pasta,
                 const std::wstring& pipeNomeado, std::string& erro);
    void parar();
    bool vivo() const { return vivo_.load(); }
    void escrever(const void* p, size_t n);          // so enfileira
    std::function<void(const std::string&)> aoLinha; // stdout+stderr, linha a linha (sem \n)
    // Se definido ANTES de iniciar(): o stdout vem cru (binario) por aqui e
    // so o stderr vai para aoLinha (DRM: audio no stdout, estado no stderr).
    std::function<void(const char*, size_t)> aoBinario;
    size_t teto = 16u << 20;                          // fila maxima (bytes)
    uint64_t descartados() const { return descartados_.load(); }

private:
    void lerSaida();
    void lacoEscrita();

    std::atomic<bool> vivo_{false};
    void* proc_ = nullptr;
    void* entrada_ = nullptr;       // stdin ou o cano nomeado
    void* saidaR_ = nullptr;
    void* binR_ = nullptr;
    std::thread thBin_;
    std::wstring nomePipe_;
    bool usaPipe_ = false;
    std::thread thLer_, thEsc_;
    std::mutex filaMutex_;
    std::condition_variable filaCv_;
    std::vector<char> fila_;
    bool sair_ = false;
    std::atomic<uint64_t> descartados_{0};
};

// ---------------------------------------------------------------------------
struct EstadoExterno {
    bool rodando = false;
    std::string erro;
    int mensagens = 0;
    std::string info;
};

// Aviao ouvido no HFDL (para a tabela com os links do FlightAware)
struct AeronaveHfdl {
    std::string voo, prefixo, hex;     // voo ja normalizado (TAM0803 -> TAM803), prefixo sem os pontos
    double lat = 91, lon = 181;
    double freqKHz = 0;
    int msgs = 0;
    double visto = 0;
    std::string urlVoo() const;        // mesmo endereco do FaLink da pagina
    std::string urlPrefixo() const;
};

class Hfdl {
public:
    ~Hfdl() { parar(); }
    bool iniciar(const std::vector<double>& canaisKHz, double centroHz, uint32_t taxa, std::string& erro);
    void parar();
    bool rodando() const { return ext_.vivo(); }
    void alimentarIQCru(const std::complex<float>* iq, size_t n, uint32_t sps, uint64_t centro);
    EstadoExterno estado();
    bool foraDaBanda() const { return fora_.load(); }
    std::vector<AeronaveHfdl> aeronaves();
    std::function<void(const std::string&)> aoTexto;
private:
    void linha(const std::string& l);
    void processarBloco();
    std::string bloco_;
    std::map<std::string, AeronaveHfdl> avioes_;
    std::map<std::string, std::string> hexConhecido_;   // "V"+voo / "R"+prefixo -> codigo ICAO
    Externo ext_;
    std::mutex m_;
    EstadoExterno est_;
    double centroHz_ = 0;
    uint32_t taxa_ = 0;
    std::atomic<bool> fora_{false};
    bool emMsg_ = false;
};

struct NavioAis {
    long long mmsi = 0;
    std::string nome, indicativo, destino;
    double lat = 91, lon = 181, vel = -1, rumo = -1;
    int tipo = 0, msgs = 0;
    double visto = 0;
};

class Ais {
public:
    ~Ais() { parar(); }
    bool iniciar(std::string& erro);
    void parar();
    bool rodando() const { return ext_.vivo(); }
    void alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps);   // VFO em 162,000
    EstadoExterno estado();
    std::vector<NavioAis> navios();
    int portaWeb() const { return portaWeb_; }
    std::function<void(const std::string&)> aoTexto;
private:
    void linha(const std::string& l);
    Externo ext_;
    std::mutex m_;
    EstadoExterno est_;
    std::map<long long, NavioAis> navios_;
    int portaWeb_ = 0;
    // reducao para 192 kS/s
    uint32_t sps_ = 0;
    size_t M_ = 1, idx_ = 0;
    std::vector<std::complex<float>> anel_, cauda_;
    std::complex<double> soma_{0, 0};
    std::complex<float> lp1_{0, 0}, lp2_{0, 0};
    float a_ = 0.3f;
    double pos_ = 0;
};

// Estacao APRS ouvida (para a tabela com os links do aprs.fi)
struct EstacaoAprs {
    std::string indicativo, destino, info;
    std::vector<std::string> via;      // digipeaters (sem o '*')
    double lat = 91, lon = 181;
    int pacotes = 0;
    double visto = 0;
};

class Aprs {
public:
    ~Aprs() { parar(); }
    bool iniciar(int baud, std::string& erro);
    void parar();
    bool rodando() const { return ext_.vivo(); }
    void escrever48k(const int16_t* pcm, size_t n);
    EstadoExterno estado();
    std::vector<EstacaoAprs> estacoes();
    static std::string urlAprsFi(const std::string& indicativo);
    std::function<void(const std::string&)> aoTexto;
private:
    void linha(const std::string& l);
    void lacoAgw();
    void registrar(const std::string& dados);
    std::map<std::string, EstacaoAprs> estacoes_;
    Externo ext_;
    std::mutex m_;
    EstadoExterno est_;
    int portaAgw_ = 0;
    std::thread thAgw_;
    std::atomic<bool> agwVivo_{false}, agwPronto_{false};
    std::atomic<uintptr_t> sockAgw_{~(uintptr_t)0};
    std::wstring conf_;
};

// ---------------------------------------------------------------------------
//  ACARS (acarsdec) - VARIOS canais de uma vez a partir do IQ cru do dongle.
//  Cada canal: NCO -> passa-baixas -> 12 kHz -> envelope AM. Os canais vao
//  intercalados (PCM 16 bits, N canais) para o acarsdec, que decodifica cada
//  um como se fosse um receptor separado.
// ---------------------------------------------------------------------------
class Acars {
public:
    ~Acars() { parar(); }
    bool iniciar(const std::vector<double>& canaisHz, double centroHz, uint32_t taxa, std::string& erro);
    void parar();
    bool rodando() const { return ext_.vivo(); }
    void alimentarIQCru(const std::complex<float>* iq, size_t n, uint32_t sps, uint64_t centro);
    EstadoExterno estado();
    bool foraDaBanda() const { return fora_.load(); }
    std::vector<AeronaveHfdl> aeronaves();
    std::function<void(const std::string&)> aoTexto;
private:
    void linha(const std::string& l);
    void processarBloco();
    struct Canal {
        double desloc = 0;
        std::complex<double> nco{1, 0}, passo{1, 0};
        std::complex<float> lp1{0, 0}, lp2{0, 0}, ant{0, 0};
        std::vector<std::complex<float>> anel;
        std::complex<double> soma{0, 0};
        float dc = 0;
    };
    Externo ext_;
    std::mutex m_;
    EstadoExterno est_;
    std::vector<Canal> canais_;
    std::vector<double> freqs_;        // Hz, na ordem dos canais ("[#1" = o primeiro)
    double centroHz_ = 0;
    uint32_t taxa_ = 0;
    size_t M_ = 1, idx_ = 0;
    float a_ = 0.1f;
    double pos_ = 0, passoSaida_ = 1;
    int nco_ = 0;
    std::atomic<bool> fora_{false};
    std::string bloco_;
    std::map<std::string, AeronaveHfdl> avioes_;
};

// ---------------------------------------------------------------------------
//  VDL2 (dumpvdl2) - IQ cru do dongle em 1,05 Msps (10500 simb/s x 10 x 10),
//  S16 intercalado pela entrada padrao; o dumpvdl2 acompanha os canais pedidos.
// ---------------------------------------------------------------------------
class Vdl2 {
public:
    ~Vdl2() { parar(); }
    static constexpr uint32_t kTaxa = 1050000;
    bool iniciar(const std::vector<double>& canaisHz, double centroHz, uint32_t taxa, std::string& erro);
    void parar();
    bool rodando() const { return ext_.vivo(); }
    void alimentarIQCru(const std::complex<float>* iq, size_t n, uint32_t sps, uint64_t centro);
    EstadoExterno estado();
    bool foraDaBanda() const { return fora_.load(); }
    std::vector<AeronaveHfdl> aeronaves();
    std::function<void(const std::string&)> aoTexto;
private:
    void linha(const std::string& l);
    void processarBloco();
    Externo ext_;
    std::mutex m_;
    EstadoExterno est_;
    double centroHz_ = 0;
    uint32_t taxa_ = 0;
    std::atomic<bool> fora_{false};
    bool emMsg_ = false;
    std::string bloco_;
    std::map<std::string, AeronaveHfdl> avioes_;
};

// ---------------------------------------------------------------------------
//  DRM - Digital Radio Mondiale (dream.exe, Dream 2.x de console com xHE-AAC)
//
//  IQ com o VFO no ZERO (o centro do canal DRM) -> reamostrado para 48 kS/s
//  -> S16 I/Q pelo stdin do dream ("-c 6": I/Q, FI em 0 Hz).
//  O audio decodificado volta pelo stdout (S16 estereo 48 kHz) e toca no
//  lugar do audio do radio; o estado (JSON) chega pelo stderr a cada 0,5 s.
// ---------------------------------------------------------------------------
struct EstadoDrm {
    bool rodando = false;
    std::string erro;
    bool comStatus = false;          // ja chegou alguma linha de estado
    int io = -1, tempo = -1, quadro = -1, fac = -1, sdc = -1, msc = -1;   // 0 = ok, 1 = CRC, 2 = dados, -1 = nada
    double snr = 0, nivelDb = 0, desvioHz = 0, larguraKHz = 0, doppler = -1, atrasoMs = -1;
    int robustez = -1, mscQam = -1, sdcQam = -1, intercalador = -1;
    int servicosAudio = 0, servicosDados = 0;
    std::string estacao, idServico, texto, codec, modoAudio, idioma, programa, pais, horaDrm, protecao;
    double kbps = 0;
    double ultStatus = 0, ultAudio = 0;   // relogio (s) da ultima linha / do ultimo audio
    double bufferS = 0;                   // audio guardado para tocar (s)
};

class Drm {
public:
    ~Drm() { parar(); }
    bool iniciar(std::string& erro);
    void parar();
    bool rodando() const { return ext_.vivo(); }
    void alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps);   // VFO no zero
    void puxarAudio(int16_t* out, size_t n, uint32_t sps);
    EstadoDrm estado();
    // Audio DRM bom ha pouco (MSC sem erro nos ultimos 5 s)? So entao o audio
    // decodificado toma o lugar do audio do radio; antes disso ouve-se o radio.
    bool audioBom() const;
    std::function<void(const std::string&)> aoTexto;
    std::atomic<bool> linhasCruas{false};
    bool inverter = false;            // espectro invertido ("-c 7"); vale no proximo iniciar()
private:
    void linha(const std::string& l);
    void binario(const char* p, size_t n);
    void prepararReamostragem(uint32_t sps);
    Externo ext_;
    std::mutex m_;
    EstadoDrm est_;
    std::string ultTexto_, ultEstacao_;
    // IQ -> 48 kS/s: 1) FIR + decimacao inteira  2) interpolador polifasico
    uint32_t sps_ = 0;
    int D1_ = 1, cont1_ = 0;
    std::vector<float> h1_;
    std::vector<std::complex<float>> hist1_;     // dobrado (2 x L1) para leitura continua
    size_t pos1_ = 0;
    double S1_ = 0, passo2_ = 1, pos2_ = 0;
    int L2_ = 0, P2_ = 0;
    std::vector<float> tab2_;                    // P2 fases x L2 coeficientes
    std::vector<std::complex<float>> buf2_;
    double ganho_ = 0, potMedia_ = 0;
    std::vector<int16_t> saida_;
    // audio que volta do dream
    std::mutex audMutex_;
    std::vector<int16_t> aud_;                   // mono 48 kHz (fila)
    size_t audIni_ = 0;
    bool tocando_ = false;
    std::string restoBin_;
    double posAud_ = 0;
    std::atomic<double> ultMscOk_{0};            // relogio (s) do ultimo MSC sem erro
};

} // namespace masdr
