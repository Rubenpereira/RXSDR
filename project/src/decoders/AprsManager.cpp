#include "AprsManager.h"
#include <QMutex>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <cmath>
#include <algorithm>

#include <QStandardPaths>

namespace masdr {

AprsManager::AprsManager(QObject* parent)
    : QObject(parent)
{}

AprsManager::~AprsManager()
{
    stop();
}

void AprsManager::setBinaryPath(const QString& path) { binaryPath_ = path; }

QString AprsManager::defaultBinaryPath() const
{
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    return dir + QStringLiteral("/decoders/direwolf.exe");
#else
    return dir + QStringLiteral("/decoders/direwolf");
#endif
}

QString AprsManager::resolvedBinaryPath() const
{
    if (!binaryPath_.isEmpty()) return binaryPath_;
    
    const QString defaultPath = defaultBinaryPath();
    if (QFileInfo::exists(defaultPath)) return defaultPath;

    // Tenta encontrar no PATH do sistema como fallback
#ifdef Q_OS_WIN
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("direwolf.exe"));
#else
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("direwolf"));
#endif
    if (!sysBin.isEmpty()) return sysBin;

    return defaultPath;
}

bool AprsManager::binaryExists() const
{
    return QFileInfo::exists(resolvedBinaryPath());
}

QString AprsManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject AprsManager::statusJson() const
{
    QJsonObject o;
    o["state"]         = stateString();
    o["binaryPresent"] = binaryExists();
    if (!lastError_.isEmpty())
        o["error"] = lastError_;
    return o;
}

bool AprsManager::createConfigFile(const QString& configPath)
{
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    out << "# Direwolf auto-generated config for RXSDR\n";
    out << "ADEVICE stdin null\n"; // Input via stdin, no output audio device
    out << "CHANNEL 0\n";
    out << "MODEM 1200\n"; // Standard APRS speed
    out << "AGWPORT 8000\n"; // Disable AGW port or leave default
    out << "KISSPORT 8001\n"; // Disable KISS port or leave default
    return true;
}

bool AprsManager::start()
{
    if (state_ == State::Running || state_ == State::Starting) return true;

    const QString bin = resolvedBinaryPath();
    if (!QFileInfo::exists(bin)) {
#ifdef Q_OS_WIN
        lastError_ = QStringLiteral("O decodificador Direwolf não foi encontrado em ./decoders/direwolf.exe");
#else
        lastError_ = QStringLiteral("O decodificador Direwolf não foi encontrado em ./decoders/direwolf");
#endif
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    const QString dir = QFileInfo(bin).absolutePath();
    const QString configPath = dir + QStringLiteral("/direwolf_rxsdr.conf");
    if (!createConfigFile(configPath)) {
        lastError_ = QStringLiteral("Não foi possível criar o arquivo de configuração para o Direwolf.");
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    process_ = std::make_unique<QProcess>(this);
    connect(process_.get(), &QProcess::started,
            this, &AprsManager::onProcessStarted);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &AprsManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &AprsManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &AprsManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &AprsManager::onReadyReadStderr);

    process_->setWorkingDirectory(dir);

    // Argumentos do Direwolf
    // -c <config>
    // -r 48000 (taxa de amostragem)
    // -b 16 (16 bits)
    // - (ler do stdin)
    // -t 0 (desabilitar cores no console)
    QStringList args;
    args << QStringLiteral("-c") << QStringLiteral("direwolf_rxsdr.conf")
         << QStringLiteral("-r") << QStringLiteral("48000")
         << QStringLiteral("-b") << QStringLiteral("16")
         << QStringLiteral("-t") << QStringLiteral("0")
         << QStringLiteral("-");

#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif

    state_ = State::Starting;
    emit stateChanged(state_);

    process_->start(bin, args);
    return true;
}

void AprsManager::stop()
{
    if (agwRetry_) agwRetry_->stop();
    if (agw_) { agw_->abort(); }
    agwPronto_ = false;
    agwBuf_.clear();

    m_stdinPending.clear();
    m_feedResampleTail.clear();
    m_feedResamplePos = 0.0;
    
    if (!process_) return;

    process_->disconnect();
    process_->terminate();
    if (!process_->waitForFinished(2000)) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    process_.reset();

    state_ = State::Stopped;
    emit stateChanged(state_);
}

void AprsManager::feedAudio(const int16_t* samples, int count, uint32_t sps)
{
    if (!process_ || state_ != State::Running || count <= 0) return;

    if (sps == 48000) {
        QByteArray pcm;
        pcm.resize(count * 2);
        memcpy(pcm.data(), samples, count * 2);
        { QMutexLocker lk(&m_stdinMutex); m_stdinPending.append(pcm); }
    } else {
        m_feedResampleTail.insert(m_feedResampleTail.end(), samples, samples + count);

        const double srcHz = double(sps);
        constexpr double dstHz = 48000.0;
        const double inPerOutput = srcHz / dstHz;

        std::vector<int16_t> resampledBlock;

        while (true) {
            const int i0 = int(std::floor(m_feedResamplePos));
            if (i0 + 1 >= int(m_feedResampleTail.size()))
                break;
            double frac = m_feedResamplePos - double(i0);
            double y0 = m_feedResampleTail[i0];
            double y1 = m_feedResampleTail[i0 + 1];
            double sample = y0 + frac * (y1 - y0);

            sample = std::clamp(sample, -32768.0, 32767.0);
            resampledBlock.push_back(static_cast<int16_t>(std::lround(sample)));

            m_feedResamplePos += inPerOutput;
        }

        const int next_i0 = int(std::floor(m_feedResamplePos));
        const int numConsumed = std::min(next_i0, int(m_feedResampleTail.size()) - 1);
        if (numConsumed > 0) {
            m_feedResampleTail.erase(m_feedResampleTail.begin(), m_feedResampleTail.begin() + numConsumed);
            m_feedResamplePos -= double(numConsumed);
        }

        if (!resampledBlock.empty()) {
            QByteArray pcm;
            pcm.resize(static_cast<int>(resampledBlock.size() * 2));
            memcpy(pcm.data(), resampledBlock.data(), resampledBlock.size() * 2);
            { QMutexLocker lk(&m_stdinMutex); m_stdinPending.append(pcm); }
        }
    }

    // A escrita NAO acontece aqui. Este metodo roda na thread de leitura do
    // dongle, e o QProcess pertence a thread principal - escrever nele daqui
    // enfileira bytes que o laco de eventos da outra thread nunca envia.
    // Pede-se a drenagem por conexao enfileirada, e ela ocorre na thread certa.
    QMetaObject::invokeMethod(this, "drenarStdin", Qt::QueuedConnection);
}

void AprsManager::drenarStdin()
{
    if (!process_ || state_ != State::Running) return;
    QMutexLocker lk(&m_stdinMutex);

    static constexpr int kMaxStdinBacklog = 8 * 1024 * 1024;
    if (m_stdinPending.size() > kMaxStdinBacklog) {
        lastError_ = QStringLiteral("Backlog stdin Direwolf muito grande.");
        emit error(lastError_);
        m_stdinPending.clear();
        stop();
        return;
    }

    int stall = 0;
    while (!m_stdinPending.isEmpty()) {
        const qint64 w = process_->write(m_stdinPending.constData(), m_stdinPending.size());
        if (w < 0) {
            lastError_ = QStringLiteral("Escrita stdin Direwolf falhou: %1").arg(process_->errorString());
            emit error(lastError_);
            m_stdinPending.clear();
            stop();
            return;
        }
        if (w == 0) {
            if (!process_->waitForBytesWritten(10)) {
                if (++stall >= 3) break;
                continue;
            }
            stall = 0;
            continue;
        }
        stall = 0;
        m_stdinPending.remove(0, int(w));
    }
}

// ---------------------------------------------------------------------------
//  Cliente AGW - recebe os quadros decodificados em tempo real
//
//  O protocolo AGW nasceu no AGW Packet Engine (SV2AGW) e virou padrao de
//  fato; o Direwolf o implementa nativamente, entao nao ha programa externo
//  nenhum envolvido - so um socket TCP para 127.0.0.1:8000.
//
//  Cabecalho de 36 bytes, little-endian:
//    [0]  porta      [4]  tipo do quadro    [6]  pid
//    [8]  chamada de origem (10)            [18] chamada de destino (10)
//    [28] tamanho dos dados (4)             [32] reservado (4)
//  Para ligar a monitoracao basta mandar um cabecalho com tipo 'm'.
// ---------------------------------------------------------------------------
void AprsManager::tentarConectarAgw()
{
    if (state_ != State::Running) return;
    if (agw_ && (agw_->state() == QAbstractSocket::ConnectedState
              || agw_->state() == QAbstractSocket::ConnectingState)) return;

    if (!agw_) {
        agw_ = new QTcpSocket(this);
        connect(agw_, &QTcpSocket::connected,    this, &AprsManager::onAgwConectado);
        connect(agw_, &QTcpSocket::readyRead,    this, &AprsManager::onAgwDados);
        connect(agw_, &QTcpSocket::disconnected, this, &AprsManager::onAgwDesconectado);
    }

    // O Direwolf leva alguns segundos para abrir a porta; tentamos ate 15x.
    if (++agwTentativas_ > 15) {
        emit logLine(QStringLiteral("[APRS] Porta AGW nao respondeu; ficando so com o stdout (com atraso)."));
        return;
    }
    agw_->abort();
    agw_->connectToHost(QStringLiteral("127.0.0.1"), kAgwPort);
}

void AprsManager::onAgwConectado()
{
    agwTentativas_ = 0;
    agwBuf_.clear();

    QByteArray cab(36, '\0');
    cab[4] = 'm';                    // 'm' = ligar monitoracao
    agw_->write(cab);
    agw_->flush();

    emit logLine(QStringLiteral("[APRS] Conectado a porta AGW do Direwolf - pacotes em tempo real."));
}

void AprsManager::onAgwDados()
{
    if (!agw_) return;
    agwBuf_.append(agw_->readAll());

    while (agwBuf_.size() >= 36) {
        const uchar* p = reinterpret_cast<const uchar*>(agwBuf_.constData());
        const char tipo = char(p[4]);
        const quint32 tam = quint32(p[28]) | (quint32(p[29]) << 8)
                          | (quint32(p[30]) << 16) | (quint32(p[31]) << 24);

        // Sanidade: quadro absurdo significa fluxo dessincronizado
        if (tam > 4u * 1024u * 1024u) {
            agwBuf_.clear();
            return;
        }
        if (quint32(agwBuf_.size()) < 36u + tam) return;   // ainda incompleto

        const QByteArray dados = agwBuf_.mid(36, int(tam));
        agwBuf_.remove(0, int(36 + tam));

        // 'U' UI, 'I' informacao, 'S' supervisao, 'T' proprio transmitido:
        // todos vem com o texto ja formatado pelo Direwolf.
        if (tipo == 'U' || tipo == 'I' || tipo == 'S' || tipo == 'T') {
            QString txt = QString::fromUtf8(dados).trimmed();
            if (!txt.isEmpty()) {
                agwPronto_ = true;
                for (const QString& l : txt.split('\n', Qt::SkipEmptyParts)) {
                    const QString t = l.trimmed();
                    if (!t.isEmpty()) emit logLine(QStringLiteral("[APRS] ") + t);
                }
            }
        }
    }
}

void AprsManager::onAgwDesconectado()
{
    if (state_ == State::Running && agwRetry_) agwRetry_->start(2000);
}

void AprsManager::onProcessStarted()
{
    state_ = State::Running;
    lastError_.clear();
    emit stateChanged(state_);

    emit logLine(QStringLiteral("[Direwolf] iniciado — processando APRS"));

    // Liga o cliente AGW: e por ele que os pacotes chegam sem atraso.
    agwTentativas_ = 0;
    agwPronto_     = false;
    if (!agwRetry_) {
        agwRetry_ = new QTimer(this);
        agwRetry_->setSingleShot(true);
        connect(agwRetry_, &QTimer::timeout, this, &AprsManager::tentarConectarAgw);
    }
    agwRetry_->start(2000);   // da tempo do Direwolf abrir a porta
}

void AprsManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (state_ != State::Stopped) {
        lastError_ = QStringLiteral("Direwolf encerrou inesperadamente (código %1)").arg(exitCode);
        state_ = State::Error;
        emit error(lastError_);
    }
    emit stateChanged(state_);
}

void AprsManager::onProcessError(QProcess::ProcessError err)
{
    Q_UNUSED(err)
    lastError_ = QStringLiteral("Erro do Direwolf: %1").arg(process_ ? process_->errorString() : QString());
    state_ = State::Error;
    emit error(lastError_);
    emit stateChanged(state_);
}

void AprsManager::onReadyReadStdout()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardOutput();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (t.isEmpty()) continue;

        // Com o AGW entregando em tempo real, o stdout so traria os mesmos
        // pacotes de novo, minutos atrasados. Entao para de repetir no painel.
        if (agwPronto_) continue;

        // O filtro antigo descartava justamente as linhas de diagnostico do
        // Direwolf ("Dire Wolf version", "Audio device for", "Channel 0:",
        // "Ready to accept"). Quando o APRS parava de decodificar nao sobrava
        // nada para investigar: nem sabiamos qual taxa de audio ele assumiu.
        // Agora tudo passa; sao poucas linhas e so na abertura.
        emit logLine(QStringLiteral("[APRS] ") + t);
    }
}

void AprsManager::onReadyReadStderr()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardError();
    const QStringList lines = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        QString t = line.trimmed();
        if (!t.isEmpty()) {
            emit logLine(QStringLiteral("[APRS-ERR] ") + t);
        }
    }
}

} // namespace masdr
