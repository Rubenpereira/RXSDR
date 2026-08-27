#pragma once
#include "ISdrDevice.h"

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QElapsedTimer>
#include <QTimer>
#include <QList>
#include <memory>
#include <vector>
#include <complex>

namespace masdr {

// ---------------------------------------------------------------------------
//  ExtIoDevice - qualquer hardware que tenha uma ExtIO
//
//  Nao carrega DLL nenhuma. Quem faz isso e o rxsdr_extio_bridge.exe, que e
//  compilado em 32 bits porque as ExtIO sao de 32 bits e o Windows nao mistura
//  as duas arquiteturas no mesmo processo. Aqui so se conversa com ele:
//  comandos em texto pela entrada, IQ ja normalizado pela saida.
//
//  O CAO DE GUARDA NAO E ZELO EXCESSIVO
//
//  Medido com a ExtIO do SDR-IQ e NENHUM aparelho ligado: InitHW se apresenta,
//  OpenHW aceita, StartHW responde "2048 amostras por bloco" - e nao chega uma
//  amostra sequer. A DLL nunca diz que o radio nao esta la. Sem um relogio
//  vigiando, o usuario veria cachoeira parada e nenhuma explicacao. Por isso,
//  se o start foi aceito e o IQ nao aparece, e ESTE lado que precisa falar.
// ---------------------------------------------------------------------------
class ExtIoDevice : public QObject, public ISdrDevice {
    Q_OBJECT
public:
    ExtIoDevice();
    ~ExtIoDevice() override;

    QString name() const override   { return nome_.isEmpty() ? QStringLiteral("ExtIO") : nome_; }
    QString serial() const override { return dllPath_; }

    // O "serial" aqui e o caminho da ExtIO_*.dll - e o que identifica o
    // aparelho, do mesmo jeito que o endereco identifica o RTL-TCP.
    bool open(const QString& serial = QString()) override;
    void close() override;
    void start() override;
    void stop() override;

    void setCenterFreq(uint64_t hz) override;
    void setSampleRate(uint32_t sps) override;
    void setGain(int tenthsDb) override;

    uint64_t centerFreq() const override { return freq_; }
    uint32_t sampleRate() const override { return sps_; }
    int gain() const override { return gainTenths_; }

    void setCallback(SamplesCallback cb) override { cb_ = std::move(cb); }
    QString lastError() const override { return lastError_; }

    // As taxas que ESTE aparelho aceita, ditas por ele mesmo.
    //
    // O SDR-IQ so tem tres - 55555, 111111 e 196078 -, e nenhuma delas existe
    // na lista fixa da tela de configuracao, que comeca em 125 kHz. Sem
    // perguntar ao aparelho, nao havia como sequer selecionar a taxa certa.
    QList<uint32_t> listSampleRates() const override { return taxas_; }

    // Abre a janela de ajustes da propria ExtIO.
    void mostrarGui(bool mostrar);

    // As DLLs que houver na pasta extio/ ao lado do programa.
    static QStringList procurarDlls();
    static QString     pastaExtio();
    static QString     caminhoPonte();

private:
    void lerSaida();          // IQ
    void lerEstado();         // texto
    void tratarResposta(const QString& linha);
    void enviar(const QString& comando);
    bool esperarPor(const QString& marca, int ms);

    std::unique_ptr<QProcess> proc_;
    QByteArray  sobra_;        // bytes de IQ que nao fecharam uma amostra
    QByteArray  sobraTexto_;
    std::vector<std::complex<float>> bloco_;

    QString  dllPath_;
    QString  nome_;
    QString  lastError_;
    QString  ultimaMarca_;

    QList<uint32_t> taxas_;
    uint64_t freq_ = 7050000ULL;
    uint32_t sps_  = 196078U;
    int      gainTenths_ = 0;
    bool     rodando_ = false;

    QTimer        vigia_;
    QElapsedTimer desdeStart_;
    bool          jaAvisouSemDado_ = false;
    int           pulsos_ = 0;
    quint64       amostras_ = 0;

    SamplesCallback cb_;
};

} // namespace masdr
