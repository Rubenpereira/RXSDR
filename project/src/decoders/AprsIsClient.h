#pragma once

#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

namespace masdr {

// ---------------------------------------------------------------------------
//  AprsIsClient - envia mensagens APRS pela internet (APRS-IS)
//
//  O RTL-SDR so recebe, entao nao ha como responder pelo ar. O caminho e a
//  rede APRS-IS: conecta num servidor, autentica com o indicativo e o
//  passcode, e injeta a mensagem. Se existir um IGate TRANSMISSOR perto do
//  destinatario, ela chega no radio dele; senao fica so no lado da internet.
//
//  Cada envio abre a conexao, manda e fecha. Nao mantemos sessao aberta:
//  o RXSDR e um receptor, nao uma estacao permanente na rede.
// ---------------------------------------------------------------------------
class AprsIsClient : public QObject {
    Q_OBJECT
public:
    explicit AprsIsClient(QObject* parent = nullptr);

    // Passcode do APRS-IS, calculado a partir do indicativo. E um algoritmo
    // publico; nao precisa de site nem cadastro. SSID e caixa sao ignorados,
    // ou seja PU1XTB, pu1xtb e PU1XTB-9 dao o mesmo numero.
    static int passcode(const QString& indicativo);

    // Valida e envia. O resultado vem pelo sinal resultado().
    void enviar(const QString& de, const QString& para, const QString& texto);

    bool ocupado() const { return ocupado_; }

signals:
    void resultado(bool ok, const QString& detalhe);
    void logLine(const QString& linha);

private slots:
    void aoConectar();
    void aoReceber();
    void aoErro(QAbstractSocket::SocketError e);
    void aoEstourarPrazo();

private:
    void encerrar(bool ok, const QString& detalhe);

    QTcpSocket* sock_    = nullptr;
    QTimer*     prazo_   = nullptr;
    bool        ocupado_ = false;
    bool        logado_  = false;

    QString de_, para_, texto_;
    QByteArray buf_;

    static constexpr const char* kServidor = "rotate.aprs2.net";
    static constexpr quint16     kPorta    = 14580;
    static constexpr int         kPrazoMs  = 15000;
};

} // namespace masdr
