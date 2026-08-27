#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QByteArray>

#include <atomic>
#include <complex>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace masdr {

// ---------------------------------------------------------------------------
//  IqRecorder - grava o IQ CRU, antes de qualquer filtro
//
//  POR QUE ISTO EXISTE
//
//  O RXSDR so sabia gravar audio ja demodulado e cortado em 3,4 kHz. Serve
//  para guardar uma conversa; nao serve para investigar um sinal. Um 4FSK
//  precisa de fase e de banda plana, e medir desvio de frequencia num audio
//  filtrado e impossivel. Foi o que travou a analise dos 160.900 MHz.
//
//  ONDE ELE SE LIGA, E ISSO E O QUE IMPORTA
//
//  No callback do dispositivo - o que vem do USB -, ANTES do deslocamento de
//  frequencia, da decimacao e do demodulador. E esse "antes de tudo" que da
//  valor ao arquivo. Nao encosta no caminho do audio: os doze decodificadores
//  dependem da semantica atual dele, e um tap novo nao pode mudar nada la.
//
//  A REGRA QUE MANDA NO DESENHO
//
//  O callback do USB NAO PODE BLOQUEAR. Se a escrita em disco atrasar, o
//  driver descarta amostras e o arquivo sai com buracos silenciosos - pior que
//  nao gravar, porque parece bom. Entao aqui dentro so se CONVERTE e se
//  EMPILHA; quem escreve e uma thread propria. E quando a fila enche, isso e
//  CONTADO e aparece na tela, em vez de virar lixo calado.
// ---------------------------------------------------------------------------
class IqRecorder {
public:

    IqRecorder();
    ~IqRecorder();

    // Parametros do radio no momento. O centro TEM de ser o do sintonizador,
    // nao o VFO da tela: o RXSDR desloca o LO de proposito, e quem for
    // analisar depois erraria o deslocamento.
    void configurar(uint32_t sampleRate, uint64_t centroHz, int ganhoDecimos,
                    int ppm, const QString& tipoDispositivo);

    // Liga a pre-gravacao e/ou o disparo por nivel.
    void armar(bool preGravacao, int preSegundos,
               bool gatilho, double limiarDb, int silencioSegundos);

    bool iniciar();          // comeca agora, aproveitando o pre-buffer
    void parar();

    // Chamado pelo Application a cada bloco, com o nivel ja calibrado.
    void nivel(double peakDb);

    // Chamado pela thread do dispositivo. So converte e empilha.
    void feed(const std::complex<float>* iq, size_t n);

    bool gravando() const { return gravando_.load(); }
    QJsonObject statusJson() const;

    static QString pastaDestino();

    // ---- buscar o que foi gravado ---------------------------------------
    //
    // O radio pode estar noutra maquina - no TV box, por exemplo -, e ai o
    // arquivo nasce LA. Estas tres funcoes existem para o navegador poder
    // listar, baixar e apagar o que ficou por la, sem SSH nem pen drive.
    static QJsonArray listarArquivos();
    // Devolve o caminho completo, ou vazio se o nome nao for de um arquivo
    // nosso. Nunca aceita caminho: so nome, e so no formato que geramos.
    static QString    caminhoDe(const QString& nome);
    static bool       apagar(const QString& nome);

private:
    void  threadEscrita();
    bool  abrirArquivo();
    void  fecharArquivo();
    void  escreverSidecar(const QString& caminhoBase) const;
    void  gravarCabecalhoWav();
    void  corrigirCabecalhoWav();
    void  converter(const std::complex<float>* iq, size_t n, QByteArray& saida) const;
    qint64 bytesPorSegundo() const;

    // --- configuracao ---
    uint32_t sps_ = 0;
    uint64_t centro_ = 0;
    int      ganhoDecimos_ = 0;
    int      ppm_ = 0;
    QString  tipoDispositivo_;
    // Um formato so: WAV IQ de 16 bits.
    //
    // O CU8 era o primario, por ser byte a byte o que o rtl_sdr produz. Mas e
    // um .bin sem cabecalho: quem abre precisa DIZER a taxa e o formato, e
    // errar qualquer um dos dois mostra ruido. O WAV carrega a taxa dentro do
    // arquivo e abre com dois cliques no SDR# e no SDR++. Custa o dobro do
    // espaco e, no RTL-SDR, oito bits de zeros - preco baixo por um arquivo
    // que a pessoa consegue abrir sozinha.

    int    minutosPorBloco_ = 2;
    qint64 minimoLivreMb_ = 1024;   // abaixo disso, para sozinho

    // --- pre-gravacao ---
    //
    // Guardado JA CONVERTIDO, no formato de saida. Converter duas vezes seria
    // gastar processador a toa, e guardar em float dobraria a memoria.
    bool   preLigada_ = false;
    int    preSegundos_ = 20;
    std::deque<QByteArray> pre_;
    qint64 preBytes_ = 0;
    qint64 preTeto_ = 0;

    // --- disparo por nivel ---
    bool   gatilhoArmado_ = false;
    double limiarDb_ = -40.0;
    int    silencioSegundos_ = 5;
    std::atomic<qint64> ultimoAcimaMs_{0};
    // O disparo NAO abre nem fecha arquivo na hora.
    //
    // O nivel() e chamado de dentro do callback do dispositivo, e abrir um
    // arquivo ou esperar a fila esvaziar ali dentro seguraria a thread do USB
    // por centenas de milissegundos - o driver descartaria amostras e o
    // arquivo sairia com buracos. Justamente o que este gravador existe para
    // evitar. Entao o gatilho so levanta a mao; quem age e a thread de escrita.
    std::atomic<bool> pedeIniciar_{false};
    std::atomic<bool> pedeParar_{false};
    // Fechar o arquivo quando o que sobrou na fila tiver sido escrito.
    //
    // O parar() publico espera a fila esvaziar antes de fechar - e certo para
    // quem chama de fora. Mas a thread de escrita NAO pode chamar esse parar:
    // ela e justamente quem esvazia a fila, e ficaria esperando por si mesma
    // ate o tempo acabar, fechando o arquivo com o fim da gravacao perdido.
    bool fecharQuandoVazio_ = false;

    // --- fila para o disco ---
    std::vector<QByteArray> fila_;
    mutable std::mutex mutex_;
    std::thread escritor_;
    std::atomic<bool> fim_{false};
    std::atomic<bool> gravando_{false};
    std::atomic<int>  overruns_{0};
    std::atomic<qint64> bytesGravados_{0};
    std::atomic<int>  blocos_{0};
    qint64 tetoFilaBytes_ = 64ll * 1024 * 1024;

    // --- arquivo atual ---
    QFile   arquivo_;
    QString caminhoAtual_;
    qint64  inicioBlocoMs_ = 0;
    qint64  inicioGravacaoMs_ = 0;
    qint64  bytesNoBloco_ = 0;
    QString ultimoErro_;
};

} // namespace masdr
