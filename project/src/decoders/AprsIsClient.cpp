#include "AprsIsClient.h"

#include <QRegularExpression>

namespace masdr {

AprsIsClient::AprsIsClient(QObject* parent) : QObject(parent)
{
    sock_ = new QTcpSocket(this);
    connect(sock_, &QTcpSocket::connected, this, &AprsIsClient::aoConectar);
    connect(sock_, &QTcpSocket::readyRead, this, &AprsIsClient::aoReceber);
    connect(sock_, &QTcpSocket::errorOccurred, this, &AprsIsClient::aoErro);

    prazo_ = new QTimer(this);
    prazo_->setSingleShot(true);
    connect(prazo_, &QTimer::timeout, this, &AprsIsClient::aoEstourarPrazo);
}

// ---------------------------------------------------------------------------
//  Passcode do APRS-IS. Algoritmo publico, o mesmo de todos os clientes.
//  Confere: N0CALL -> 13023 (valor documentado).
// ---------------------------------------------------------------------------
int AprsIsClient::passcode(const QString& indicativo)
{
    // Tira o SSID e passa para maiusculas
    QString raiz = indicativo.section(QLatin1Char('-'), 0, 0).toUpper().trimmed();
    if (raiz.isEmpty()) return -1;
    if (raiz.size() > 9) raiz = raiz.left(9);

    const QByteArray b = raiz.toLatin1();
    short hash = 0x73e2;
    int i = 0;
    const int len = b.size();
    while (i < len) {
        hash ^= short(static_cast<unsigned char>(b[i]) << 8);
        if (i + 1 < len)
            hash ^= short(static_cast<unsigned char>(b[i + 1]));
        i += 2;
    }
    return int(hash & 0x7fff);
}

void AprsIsClient::enviar(const QString& de, const QString& para, const QString& texto)
{
    if (ocupado_) {
        emit resultado(false, QStringLiteral("Ja existe um envio em andamento."));
        return;
    }

    de_    = de.trimmed().toUpper();
    para_  = para.trimmed().toUpper();
    texto_ = texto.trimmed();

    // O indicativo precisa ter cara de indicativo. A regra tem que servir para
    // qualquer pais, porque o programa e distribuido: base de 3 a 8 caracteres
    // (cobre indicativos de evento como GB100MCG) e SSID opcional alfanumerico
    // (o "-A" de alguns paises, alem do "-15" usual). O total nao passa de 9,
    // que e o limite do campo de destinatario do APRS.
    static const QRegularExpression reOrigem(
        QStringLiteral("^[A-Z0-9]{3,8}(-[A-Z0-9]{1,2})?$"));

    // O destino aceita mais coisas que a origem: alem de indicativos, ele pode
    // ser um endereco coletivo. "BLN0".."BLN9" e "BLNA".."BLNZ" sao boletins,
    // que todas as estacoes da area recebem; "CQ" e a chamada geral classica,
    // com so 2 letras. Por isso o minimo aqui e 2, e nao 3.
    static const QRegularExpression reDestino(
        QStringLiteral("^[A-Z0-9]{2,9}(-[A-Z0-9]{1,2})?$"));

    if (de_.size() > 9 || !reOrigem.match(de_).hasMatch()) {
        emit resultado(false, QStringLiteral("Indicativo de origem invalido: %1").arg(de_));
        return;
    }
    if (para_.size() > 9 || !reDestino.match(para_).hasMatch()) {
        emit resultado(false, QStringLiteral(
            "Destino invalido: %1. Use um indicativo, ou BLN1 para falar com todos.").arg(para_));
        return;
    }
    if (texto_.isEmpty()) {
        emit resultado(false, QStringLiteral("Mensagem vazia."));
        return;
    }
    // O padrao APRS limita a mensagem a 67 caracteres
    if (texto_.size() > 67) texto_ = texto_.left(67);
    // Estes caracteres quebram o formato da mensagem
    texto_.remove(QLatin1Char('|')).remove(QLatin1Char('~')).remove(QLatin1Char('{'));

    ocupado_ = true;
    logado_  = false;
    buf_.clear();

    emit logLine(QStringLiteral("[APRS-IS] conectando a %1:%2 ...")
                     .arg(QLatin1String(kServidor)).arg(kPorta));
    prazo_->start(kPrazoMs);
    sock_->abort();
    sock_->connectToHost(QLatin1String(kServidor), kPorta);
}

void AprsIsClient::aoConectar()
{
    const int code = passcode(de_);
    // "vers" identifica o programa; APZ... e o prefixo reservado para
    // software experimental/caseiro, que e o nosso caso.
    const QString login = QStringLiteral("user %1 pass %2 vers RXSDR 1.0\r\n")
                              .arg(de_).arg(code);
    sock_->write(login.toLatin1());
    sock_->flush();
    emit logLine(QStringLiteral("[APRS-IS] autenticando como %1 ...").arg(de_));
}

void AprsIsClient::aoReceber()
{
    buf_.append(sock_->readAll());

    while (true) {
        const int nl = buf_.indexOf('\n');
        if (nl < 0) break;
        const QString linha = QString::fromLatin1(buf_.left(nl)).trimmed();
        buf_.remove(0, nl + 1);
        if (linha.isEmpty()) continue;

        // O servidor responde o login com "# logresp <call> verified/unverified"
        if (linha.startsWith(QLatin1Char('#'))) {
            if (linha.contains(QStringLiteral("logresp"), Qt::CaseInsensitive)) {
                if (linha.contains(QStringLiteral("unverified"), Qt::CaseInsensitive)) {
                    encerrar(false, QStringLiteral(
                        "O servidor recusou o indicativo %1 (nao verificado). "
                        "Confira se esta correto.").arg(de_));
                    return;
                }
                logado_ = true;

                // Formato da mensagem APRS: o destinatario ocupa exatamente
                // 9 caracteres, completados com espacos.
                const QString dest = para_.leftJustified(9, QLatin1Char(' '), true);
                const QString pacote = QStringLiteral("%1>APZRXS,TCPIP*::%2:%3\r\n")
                                           .arg(de_).arg(dest).arg(texto_);
                sock_->write(pacote.toLatin1());
                sock_->flush();

                emit logLine(QStringLiteral("[APRS-IS] enviado para %1: %2")
                                 .arg(para_).arg(texto_));
                // Da um instante para o pacote sair antes de fechar
                QTimer::singleShot(1200, this, [this]() {
                    encerrar(true, QStringLiteral("Mensagem entregue ao APRS-IS."));
                });
                return;
            }
        }
    }
}

void AprsIsClient::aoErro(QAbstractSocket::SocketError e)
{
    Q_UNUSED(e)
    if (!ocupado_) return;
    encerrar(false, QStringLiteral("Falha de rede: %1").arg(sock_->errorString()));
}

void AprsIsClient::aoEstourarPrazo()
{
    if (!ocupado_) return;
    encerrar(false, logado_
        ? QStringLiteral("O servidor aceitou o login mas nao confirmou o envio.")
        : QStringLiteral("Tempo esgotado falando com o servidor APRS-IS."));
}

void AprsIsClient::encerrar(bool ok, const QString& detalhe)
{
    prazo_->stop();
    sock_->abort();
    ocupado_ = false;
    logado_  = false;
    buf_.clear();
    if (!ok) emit logLine(QStringLiteral("[APRS-IS] ") + detalhe);
    emit resultado(ok, detalhe);
}

} // namespace masdr
