#pragma once
#include "Demodulator.h"
#include "Filters.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  DemodCW - telegrafia, recebida como BANDA LATERAL ESTREITA.
//
//  A versao anterior SINTETIZAVA um tom de 700 Hz e o modulava com a
//  envoltoria do sinal. Aquilo exigia um gate, com limiar, histerese e
//  conformacao de batida - e cada uma dessas pecas era uma fonte de aspereza.
//  Foram tres rodadas de correcao, cada uma melhorando um sintoma e piorando
//  outro: o "arranhado", o tom continuo com filtro estreito, e os cliques de
//  manipulacao. Medido no ar, a energia espalhada em volta do tom chegava a
//  29 dB acima do fundo, contra 10 dB nos trechos em USB da MESMA gravacao.
//
//  Todo receptor de CW faz diferente: o tom que se ouve e a BATIDA entre a
//  portadora e o oscilador, nao algo gerado. Sem gate, sem limiar, sem
//  clique. E por isso que os trechos em USB soavam limpos.
//
//  Agora e isso: filtra o sinal em banda estreita, desloca para o tom
//  escolhido e entrega. Tres passos, nenhuma peca que possa "chiar".
// ---------------------------------------------------------------------------
class DemodCW : public Demodulator {
public:
    void setMode(const QString& mode) override {
        Demodulator::setMode(mode);
        oscI_ = 1.0f; oscQ_ = 0.0f;
        agc_  = 0.05f;
        preFs_ = 0.0f; audioFs_ = 0.0f; ultimaBw_ = 0;
    }

    void process(const std::complex<float>* iq, size_t n, uint32_t sampleRate) override;

    // Altura do tom, em Hz. E so onde a estacao aparece no audio - nao muda
    // a sintonia, so o timbre. 700 Hz e o valor que a maioria dos operadores
    // usa; 600 e 800 tambem sao comuns.
    void setTomHz(float hz) { tomHz_ = (hz > 200.0f && hz < 1500.0f) ? hz : 700.0f; }
    float tomHz() const { return tomHz_; }

private:
    float tomHz_ = 700.0f;

    IirLpf     pre_;              // filtro complexo em banda base: seleciona a estacao
    float      preFs_ = 0.0f;
    IirLpfMono audioLpf_;         // tira o que ficou acima da banda util
    IirHpf     audioHpf_;         // tira zumbido e sopro abaixo do tom
    float      audioFs_ = 0.0f;
    uint32_t   ultimaBw_ = 0;

    float oscI_ = 1.0f, oscQ_ = 0.0f;   // oscilador do deslocamento
    float agc_  = 0.05f;
};

} // namespace masdr
