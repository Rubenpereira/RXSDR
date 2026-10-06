#pragma once
// ---------------------------------------------------------------------------
//  PactorCore - PACTOR-I (modo FEC / "unproto" e escuta de ARQ), dentro do RXSDR
//
//  FSK de 2 tons, 200 Hz de shift, 100 ou 200 baud. Cada pacote (0,96 s):
//    cabecalho 0x55 | dados (8 bytes a 100 Bd, 20 a 200 Bd) | status | CRC-16
//  com os bits de cada byte saindo do menos significativo para o mais.
//  O texto vem em ASCII de 8 bits ou comprimido em Huffman (2 a 15 bits por
//  letra). O CRC (polinomio CCITT x16+x12+x5+1) valida cada pacote - e o que
//  separa texto certo de lixo.
//
//  Como o pacote e curto e o relogio dos dois lados e de cristal, nao ha
//  recuperacao de relogio: o discriminador e amostrado em 8 fases por bit, nas
//  duas polaridades e nas duas velocidades, e cada sequencia procura o
//  cabecalho + CRC a cada bit novo. As variantes do CRC (ordem dos bits, valor
//  inicial, inversao) sao todas testadas ate uma se repetir - ai ela trava.
//  Pacote repetido (o FEC manda cada um mais de uma vez) entra uma vez so; e
//  copias que falham no CRC sao SOMADAS (memoria-ARQ) e testadas de novo.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <complex>
#include <deque>
#include <string>
#include <vector>

namespace masdr {

class PactorCore {
public:
    explicit PactorCore(double fs = 8000.0);

    std::string feed(const float* x, size_t n);

    bool   travado() const;          // pacote valido nos ultimos 10 s
    int    baud() const { return baudTravado_; }
    double tomCentral() const { return f0_; }
    int    pacotes() const { return okPacotes_; }
    int    somados() const { return okSomados_; }
    int    descartados() const { return ruins_; }
    const char* formato() const { return formato_; }
    std::string indicativo() const { return indicativo_; }
    double segundos() const { return double(t_) / fs_; }

    // --- exposto para teste ---
    static bool decodificarHuffman(const std::vector<int>& bits, std::string& txt, bool* fechou = nullptr);

private:
    struct Seq {                     // uma fase de amostragem, uma velocidade
        int baud = 100, fase = 0;
        double prox = 0;             // amostra do proximo bit
        std::deque<float> soft;
    };
    struct Falho { uint64_t t; int baud; std::vector<float> soft; };

    void estimarCentro();
    void novoBit(Seq& s, float v);
    bool tentar(const std::vector<float>& soft, int baud, bool somado, uint64_t t);
    bool conferirCrc(const std::vector<uint8_t>& corpo, uint16_t recebido, int& variante) const;
    void aceitar(const std::vector<uint8_t>& dados, uint8_t status, int baud, bool somado, uint64_t t);

    double fs_;
    uint64_t t_ = 0;
    // mistura + passa-baixas complexo
    double f0_ = 1500.0, faseNco_ = 0;
    std::vector<float> h_;
    std::vector<std::complex<float>> hist_;
    size_t pos_ = 0;
    std::complex<float> zAnt_{0, 0};
    // discriminador acumulado (para medias de qualquer janela)
    std::vector<double> acum_;       // soma corrida, anel
    size_t acumN_ = 0;
    double soma_ = 0;
    // centro automatico
    std::vector<float> bufEsp_;
    std::vector<double> mediaEsp_;
    uint64_t proxEst_ = 0;
    double potSinal_ = 0, potRuido_ = 1e-9;
    bool centroOk_ = false;

    std::vector<Seq> seqs_;
    std::deque<Falho> falhos_;

    // CRC: variante travada (-1 = ainda testando todas)
    int variante_ = -1;
    struct Pendente { int var = -1; std::vector<uint8_t> dados; uint8_t status = 0; int baud = 100; uint64_t t = 0; } pendente_;
    int votoH_ = 0;
    uint64_t ultimoOk_ = 0;
    bool algumOk_ = false;
    int baudTravado_ = 0;
    std::vector<uint8_t> ultDados_; uint8_t ultStatus_ = 0xFF; uint64_t ultT_ = 0;
    int okPacotes_ = 0, okSomados_ = 0, ruins_ = 0;
    const char* formato_ = "-";
    std::string indicativo_, recente_;
    std::string saida_;
};

} // namespace masdr
