#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QJsonObject>
#include <QByteArray>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <memory>

namespace masdr {

// ---------------------------------------------------------------------------
//  WhisperManager - transcricao da fala recebida, em texto
//
//  SOMENTE WINDOWS, e de proposito. Nas caixas o Amlogic ja faz a conta do
//  radio inteiro; disputar processador com ele traria de volta os picotes no
//  audio que custaram tanto para acabar.
//
//  POR QUE UM SERVIDOR, E NAO UM PROGRAMA POR TRECHO
//
//  O modelo tem 466 MB. Um processo por pedaco de audio pagaria o
//  carregamento a cada vez, e o carregamento demora mais que a transcricao.
//  Medido: com o modelo ja na memoria, 20 s de audio saem em 4 s, e a segunda
//  chamada demora o mesmo. Entao o whisper-server sobe UMA vez e fica
//  esperando; cada trecho vai a ele por HTTP no proprio computador.
//
//  QUEM DECIDE O QUE E FALA NAO E UM LIMIAR
//
//  Em FM o squelch separa fala de silencio de graca. EM SSB NAO HA SQUELCH: o
//  canal fica aberto chiando o tempo todo, e um limiar de energia ali ou nunca
//  dispara ou dispara sempre. Por isso quem responde "aqui tem voz?" e o
//  Silero, um detector treinado para isso, que roda dentro do proprio
//  servidor antes do modelo grande. Sem ele, SSB nao teria como funcionar - e
//  era justamente o caso que o dono do radio lembrou.
//
//  A REGRA DE SEMPRE
//
//  O audio chega pela thread do dispositivo e ELA NAO PODE ESPERAR. Aqui so
//  se reamostra e se empilha; quem conversa com o servidor e uma thread
//  propria.
// ---------------------------------------------------------------------------
class WhisperManager : public QObject {
    Q_OBJECT
public:
    explicit WhisperManager(QObject* parent = nullptr);
    ~WhisperManager() override;

    bool iniciar();
    void parar();
    bool rodando() const { return rodando_.load(); }

    // Audio ja demodulado, na taxa que o demodulador produzir.
    void feedAudio(const int16_t* pcm, size_t n, uint32_t sps);

    void setIdioma(const QString& l) { idioma_ = l; }
    // Quantos nucleos a transcricao pode usar.
    //
    // Um so nao dava conta: com o modelo small, um trecho de 20 s demorava
    // mais de 20 s e a fila crescia ate comecar a descartar. Dois ja sobra.
    // Fica ajustavel porque quem tem maquina modesta pode preferir devolver
    // processador ao radio, e quem tem folga pode querer mais.
    void setNucleos(int n) { nucleos_ = (n < 1) ? 1 : (n > 8 ? 8 : n); }
    int  nucleos() const { return nucleos_; }
    void setModelo(const QString& m) { if (m=="base"||m=="small") modelo_ = m; }
    // Vale na hora, sem reiniciar o servidor: e so um tamanho de janela.
    void setJanelaSeg(int s) { janelaSeg_.store(s < 3 ? 3 : (s > 30 ? 30 : s)); }
    int  janelaSeg() const { return janelaSeg_.load(); }
    QString idioma() const { return idioma_; }

    QJsonObject statusJson() const;

    static QString pastaDecoders();
    static QString caminhoServidor();
    // MEDIDO, e o numero manda na escolha do padrao: com o small em 2
    // nucleos, 20 s de audio levam 34 s. Ele nunca acompanha, a fila cresce e
    // passa a descartar. O base faz os mesmos 20 s em uns 4 s e sobra folga.
    // Quem tem maquina forte pode escolher o small no painel.
    static QString caminhoModelo(const QString& qual);
    static QString caminhoVad();
    static bool    tudoInstalado();

signals:
    // Linha pronta: a pessoa parou de falar (ou o teto chegou).
    void transcricao(const QString& texto);
    // Linha AINDA CRESCENDO - substitui a anterior na tela, nao empilha.
    void transcricaoParcial(const QString& texto);
    // Acabou a fala: o que estiver na tela vira definitivo.
    void fimDeFala();
    void logLine(const QString& linha);

private:
    void   threadEnvio();
    QString pedirTranscricao(const std::vector<int16_t>& amostras);
    static QByteArray montarWav(const std::vector<int16_t>& amostras, uint32_t sps);
    static bool soEtiqueta(const QString& t);
    // Corta o laco de palavra repetida ("que e que e que e...").
    static QString podarRepeticao(const QString& t);
    // Leva a fala sempre ao mesmo volume. Devolve o ganho aplicado.
    static double normalizarNivel(std::vector<int16_t>& amostras);

    std::unique_ptr<QProcess> proc_;
    QString idioma_ = QStringLiteral("pt");
    quint16 porta_ = 8099;
    int     nucleos_ = 4;
    QString modelo_ = QStringLiteral("base");
    // Congelado na partida: a thread de envio le isto a cada trecho, e o
    // idioma so muda com o servidor reiniciado - ler o QString direto daqui
    // seria disputa entre duas threads sem necessidade.
    QByteArray idiomaAtivo_ = QByteArrayLiteral("pt");
    QString ultimaFala_;
    qint64  ultimoAvisoFila_ = 0;

    // ---- reamostragem para 16 kHz ----
    //
    // O whisper so aceita 16 kHz. O demodulador entrega noutra taxa, e ela
    // pode ate mudar no meio se o usuario trocar a amostragem - por isso o
    // passo e recalculado quando a taxa de entrada muda, em vez de fixado.
    uint32_t spsEntrada_ = 0;
    double   passo_ = 0.0;
    double   fase_ = 0.0;
    float    anterior_ = 0.0f;
    float    lp_ = 0.0f;
    // Passa-alta de 150 Hz, antes de tudo.
    //
    // O audio de SSB chega com nivel continuo e ronco de baixa frequencia -
    // resto da demodulacao e da propria banda. Nao se escuta como voz, mas
    // ocupa amplitude: na hora de normalizar, o ronco e que definia o pico e a
    // fala ficava embaixo. Tirando ele, a normalizacao mede so a voz.
    float    hpEnt_ = 0.0f;
    float    hpSai_ = 0.0f;

    // ---- corte por pausa ----
    //
    // E O QUE FAZ O TEXTO APARECER DEPRESSA. Antes a janela fechava pelo
    // relogio: escolhido 12 s, ate um "cambio" de dois segundos esperava os
    // 12 completos. Agora a janela fecha quando a pessoa PARA DE FALAR, e o
    // tempo escolhido vira apenas o teto - quem fala sem parar continua tendo
    // o corte no teto, como antes.
    //
    // Nao ha limiar fixo aqui, e nem poderia haver: em SSB o chiado esta
    // sempre presente e muda de nivel a cada estacao. O que se acompanha e o
    // PISO - ele desce depressa e sobe devagar, entao acaba morando no ruido
    // entre as silabas - e considera-se fala o que passa bem acima dele.
    double   blocoSoma_ = 0.0;
    int      blocoN_ = 0;
    float    fundo_ = 0.0f;
    bool     fundoOk_ = false;
    int      msSilencio_ = 0;
    bool     houveFala_ = false;

    std::vector<int16_t> janela_;      // o que esta sendo juntado
    std::mutex           mutexJanela_;

    std::deque<std::vector<int16_t>> fila_;
    // Um lugar so, sempre com o pedido MAIS NOVO.
    //
    // Parcial nao entra em fila: ele existe para mostrar o que esta sendo dito
    // agora, e um parcial atrasado nao serve para nada - quando o proximo
    // chega, o anterior perdeu a razao de existir e e simplesmente
    // sobrescrito. E tambem o freio automatico: enquanto a maquina esta
    // ocupada nenhum parcial e consumido, entao ela nunca recebe mais trabalho
    // do que aguenta.
    std::vector<int16_t> parcial_;
    size_t               marcaParcial_ = 0;
    std::mutex           mutexFila_;
    std::thread          enviador_;
    std::atomic<bool>    fim_{false};
    std::atomic<bool>    rodando_{false};
    std::atomic<int>     trechos_{0};
    std::atomic<int>     falas_{0};
    // Quanto tempo levou o ultimo trecho definitivo, e de quanto audio ele
    // era. Sem este numero na tela nao ha como saber se a maquina esta
    // acompanhando ou nao - so se ve a fila crescer, ja tarde demais.
    std::atomic<int>     msUltimo_{0};
    std::atomic<int>     msAudioUltimo_{0};
    QString              ultimoErro_;

    static constexpr uint32_t kTaxa = 16000;
    static constexpr size_t   kSobreposicao     = kTaxa * 1;   // 1 s
    // Quando o corte foi na pausa, nao ha palavra partida para emendar - e
    // repetir o fim da frase anterior so produziria linha duplicada.
    static constexpr size_t   kSobreposicaoPausa = kTaxa / 5;  // 0,2 s
    static constexpr size_t   kJanelaMinima      = kTaxa * 8 / 5; // 1,6 s
    static constexpr int      kPausaMs           = 450;
    // De quanto em quanto tempo a linha na tela e reescrita enquanto a pessoa
    // fala. Menos que isto so faria a linha tremer sem acrescentar leitura.
    static constexpr size_t   kPassoParcial      = kTaxa * 6 / 5;  // 1,2 s
    static constexpr size_t   kTetoFila         = 6;

    // O TAMANHO DA JANELA E O ATRASO. Nada aparece na tela antes de ela
    // fechar, entao 20 s de janela sao 20 s de espera - foi o que se viu no
    // video: um carimbo a cada 18 a 22 segundos.
    //
    // Encurtar tem preco: com menos contexto o modelo erra mais e corta frase
    // no meio com mais frequencia. Como o medido mostrou folga de sobra - 20 s
    // de audio saem em 4 s com o modelo rapido -, da para trocar parte dessa
    // folga por resposta mais rapida. Fica escolhivel porque o ponto certo
    // depende do que se escuta: conversa corrida aguenta janela curta, sinal
    // fraco precisa de contexto.
    std::atomic<int> janelaSeg_{5};
};

} // namespace masdr
