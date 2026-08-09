#pragma once

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  AnaliseCore - identificador de sinal desconhecido
//
//  Aponte o radio para um sinal digital que voce nao conhece, deixe juntar
//  alguns segundos de audio e ele responde: quantos tons, qual a separacao
//  entre eles, a que velocidade estao mudando e - o mais util - se o conteudo
//  parece texto ou parece cifrado.
//
//  A ultima parte e a que evita perder tempo. Um fluxo de bits com texto tem
//  uma assinatura estatistica clara: sequencias longas de bits iguais
//  aparecem mais do que o acaso permitiria, porque letras se repetem e o
//  alfabeto nao usa todos os codigos. Ja um fluxo cifrado e indistinguivel de
//  moeda jogada para o alto - e justamente esse o objetivo de quem cifra.
//  Medindo a distribuicao dos comprimentos de sequencia da para separar os
//  dois casos sem decodificar nada.
// ---------------------------------------------------------------------------
class AnaliseCore {
public:
    struct Resultado {
        bool   ok           = false;
        double tomBaixoHz   = 0.0;
        double tomAltoHz    = 0.0;
        double deslocamento = 0.0;   // separacao entre os tons
        double baud         = 0.0;
        int    nTons        = 0;
        double proporcaoUns = 0.0;
        double mediaCorridas = 0.0;  // 2,0 = aleatorio puro
        double aleatoriedade = 0.0;  // 0 = estruturado, 1 = indistinguivel de acaso
        int    bitsUsados   = 0;
        std::string veredito;
        std::vector<std::string> linhas;   // relatorio pronto para a tela
    };

    explicit AnaliseCore(double sampleRate = 8000.0);

    void   limpar();
    // Junta audio (float -1..1). Devolve true quando ja ha material suficiente.
    bool   alimentar(const float* amostras, size_t n);
    double segundosJuntados() const { return double(buf_.size()) / sr_; }
    double segundosNecessarios() const { return kSegundos; }

    Resultado analisar() const;

    // --- exposto para teste ---------------------------------------------
    static void fft(std::vector<std::complex<double>>& a, bool inversa);

private:
    std::vector<double> espectroMedio(int n) const;
    std::vector<float>  envoltoria(double freqHz, int janela) const;

    static constexpr double kSegundos = 12.0;

    double sr_;
    std::vector<float> buf_;
};

} // namespace masdr
