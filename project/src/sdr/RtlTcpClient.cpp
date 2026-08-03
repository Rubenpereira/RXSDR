#include "RtlTcpClient.h"
#include "../util/Logger.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <complex>
#include <vector>

namespace masdr {

RtlTcpClient::RtlTcpClient() = default;
RtlTcpClient::~RtlTcpClient() { close(); }

static bool parseEndpoint(const QString& serial, QString& host, quint16& port)
{
    const QString t = serial.trimmed();
    if (t.isEmpty()) return false;
    const int colon = t.lastIndexOf(':');
    if (colon > 0) {
        host = t.left(colon).trimmed();
        bool ok = false;
        const int p = t.mid(colon + 1).toInt(&ok);
        if (ok && p > 0 && p <= 65535) {
            port = static_cast<quint16>(p);
            return !host.isEmpty();
        }
    }
    host = t;
    return true;
}

bool RtlTcpClient::open(const QString& serial)
{
    lastError_.clear();
    // Garante sessao limpa em reconexoes do mesmo objeto.
    close();

    if (!serial.trimmed().isEmpty()) {
        endpoint_ = serial.trimmed();
    }

    QString host = "127.0.0.1";
    quint16 port = 1234;
    if (!parseEndpoint(endpoint_, host, port)) {
        lastError_ = QStringLiteral("Endereço RTL-TCP inválido: %1").arg(endpoint_);
        Logger::error(lastError_);
        return false;
    }

    socket_ = std::make_unique<QTcpSocket>();
    QObject::connect(socket_.get(), &QTcpSocket::readyRead, [this]() { onReadyRead(); });

    Logger::info(QString("RTL-TCP a ligar a %1:%2 ...").arg(host).arg(port));
    socket_->connectToHost(host, port);
    if (!socket_->waitForConnected(2000)) {
        lastError_ = QStringLiteral("RTL-TCP %1:%2 — %3")
                         .arg(host)
                         .arg(port)
                         .arg(socket_->errorString());
        Logger::error(lastError_);
        socket_.reset();
        return false;
    }

    endpoint_ = QStringLiteral("%1:%2").arg(host).arg(port);
    gotServerHeader_ = false;
    Logger::info(QString("RTL-TCP conectado em %1").arg(endpoint_));
    setSampleRate(sps_);
    setPpm(ppm_);
    setQuadrature(quadrature_);
    setCenterFreq(freq_);
    setGain(gainTenths_);
    return true;
}

void RtlTcpClient::close()
{
    stop();
    if (!socket_) return;
    socket_->disconnectFromHost();
    if (socket_->state() != QAbstractSocket::UnconnectedState &&
        !socket_->waitForDisconnected(1000)) {
        // Fallback para liberar imediatamente em servidores rtl_tcp mais sensiveis.
        socket_->abort();
    }
    socket_.reset();
    rxBytes_.clear();
    gotServerHeader_ = false;
}

void RtlTcpClient::start()
{
    running_ = socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

void RtlTcpClient::stop()
{
    running_ = false;
}

void RtlTcpClient::setCenterFreq(uint64_t hz)
{
    freq_ = hz;
    sendCommand(0x01, static_cast<uint32_t>(hz));
}

void RtlTcpClient::setSampleRate(uint32_t sps)
{
    sps_ = sps;
    sendCommand(0x02, sps);
}

void RtlTcpClient::setGain(int tenthsDb)
{
    gainTenths_ = tenthsDb;
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) return;

    if (quadrature_) {
        // Q-on (direct sampling): tuner bypassado. NÃO mexer no tuner_gain_mode —
        // só ativar AGC interno do RTL2832U para preservar o ganho em HF.
        sendCommand(0x08, 1); // RTL AGC = on
        return;
    }

    if (tenthsDb < 0) {
        sendCommand(0x03, 0); // Tuner AGC = on
        sendCommand(0x08, 1); // RTL AGC = on (modo auto de ganho do tuner requer RTL AGC)
    } else {
        sendCommand(0x03, 1); // Tuner AGC = off (ganho manual)
        sendCommand(0x04, static_cast<uint32_t>(tenthsDb)); // Ganho manual do tuner
        sendCommand(0x08, 0); // RTL AGC = off
    }
}

void RtlTcpClient::setQuadrature(bool on)
{
    quadrature_ = on;
    // rtl_tcp: 0x09 = direct sampling (0=off, 1=I-branch, 2=Q-branch).
    // Q-branch (valor 2) é o correto para recepção HF com RTL-SDR.
    sendCommand(0x09, quadrature_ ? 2u : 0u);

    // Alinhado ao comportamento do RtlSdrDevice (dongle USB):
    if (on) {
        // Se ativando Q-on (Direct Sampling)
        sendCommand(0x08, 1); // RTL AGC = on
    } else {
        // Se desativando Q-on (voltando para Q-off)
        // Restaura o modo de ganho do tuner ao voltar para Q-off
        sendCommand(0x03, gainTenths_ < 0 ? 0 : 1);
        if (gainTenths_ >= 0) {
            sendCommand(0x04, static_cast<uint32_t>(gainTenths_));
        }
        sendCommand(0x08, gainTenths_ < 0 ? 1 : 0);
    }
}

void RtlTcpClient::setPpm(int ppm)
{
    ppm_ = ppm;
    // rtl_tcp: 0x05 = frequency correction in ppm.
    sendCommand(0x05, static_cast<uint32_t>(ppm_));
}

void RtlTcpClient::onReadyRead()
{
    if (!socket_) return;
    rxBytes_.append(socket_->readAll());

    // rtl_tcp envia um header inicial de 12 bytes (dongle info) antes do IQ.
    if (!gotServerHeader_) {
        if (rxBytes_.size() < 12) return;
        rxBytes_.remove(0, 12);
        gotServerHeader_ = true;
    }

    // rtl_tcp envia IQ intercalado u8 I/Q.
    static thread_local std::vector<std::complex<float>> iq;
    while (rxBytes_.size() >= 16384 * 2) {
        const int take = 16384 * 2;
        const char* data = rxBytes_.constData();
        const size_t n = static_cast<size_t>(take / 2);
        if (iq.size() < n) iq.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float I = (static_cast<uint8_t>(data[2 * i]) - 127.5f) / 127.5f;
            const float Q = (static_cast<uint8_t>(data[2 * i + 1]) - 127.5f) / 127.5f;
            iq[i] = {I, Q};
        }
        rxBytes_.remove(0, take);
        if (running_ && cb_) cb_(iq.data(), n);
    }
}

void RtlTcpClient::sendCommand(uint8_t cmd, uint32_t value)
{
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) return;
    char pkt[5];
    pkt[0] = static_cast<char>(cmd);
    pkt[1] = static_cast<char>((value >> 24) & 0xFF);
    pkt[2] = static_cast<char>((value >> 16) & 0xFF);
    pkt[3] = static_cast<char>((value >> 8) & 0xFF);
    pkt[4] = static_cast<char>(value & 0xFF);
    socket_->write(pkt, 5);
    socket_->flush();
}

} // namespace masdr