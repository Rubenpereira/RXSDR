#include "AisCatcherManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

#include <QStandardPaths>

namespace masdr {

AisCatcherManager::AisCatcherManager(QObject* parent)
    : QObject(parent)
{
    connect(this, &AisCatcherManager::writeToStdin,
            this, &AisCatcherManager::onWriteToStdin,
            Qt::QueuedConnection);
}

AisCatcherManager::~AisCatcherManager()
{
    stop();
}

void AisCatcherManager::setBinaryPath(const QString& path) { binaryPath_  = path; }
void AisCatcherManager::setWebPort(quint16 port)           { webPort_     = port; }
void AisCatcherManager::setDeviceIndex(int index)          { deviceIndex_ = index; }
void AisCatcherManager::setSampleRate(double /*rate*/)     { /* não usado no modo audio */ }

// ---------------------------------------------------------------------------
//  Utilitários
// ---------------------------------------------------------------------------

// O AIS-catcher e de 64 bits, mas a pasta decoders e MISTURADA: DSDPlus, FMP24
// e outros sao de 32 bits e sao donos dos nomes rtlsdr.dll, airspy.dll e
// libusb-1.0.dll naquela pasta. Como o Windows procura DLL primeiro no
// diretorio do proprio executavel, o AIS-catcher carregava as versoes de 32
// bits e morria com 0xc000007b antes de rodar uma linha. Por isso ele agora
// mora em decoders/ais, sozinho com as DLLs x64 dele.
QString AisCatcherManager::defaultBinaryPath() const
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + QStringLiteral("/decoders/ais/AIS-catcher.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/decoders/ais/AIS-catcher");
#endif
}

QString AisCatcherManager::resolvedBinaryPath() const
{
    if (!binaryPath_.isEmpty()) return binaryPath_;

    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QStringList candidates = {
        dir + QStringLiteral("/decoders/ais/AIS-catcher.exe"),
        dir + QStringLiteral("/decoders/AIS-catcher.exe"),
        dir + QStringLiteral("/decoders/aiscatcher.exe"),
        dir + QStringLiteral("/AIS-catcher.exe"),
        QDir::cleanPath(dir + QStringLiteral("/../AIS-catcher.exe")),
        dir + QStringLiteral("/decoders/aisdeco2.exe")
    };
#else
    const QStringList candidates = {
        dir + QStringLiteral("/decoders/ais/AIS-catcher"),
        dir + QStringLiteral("/decoders/AIS-catcher"),
        dir + QStringLiteral("/decoders/aiscatcher"),
        dir + QStringLiteral("/AIS-catcher"),
        QDir::cleanPath(dir + QStringLiteral("/../AIS-catcher")),
        dir + QStringLiteral("/decoders/aisdeco2")
    };
#endif
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }

    // Tenta encontrar no PATH do sistema como fallback
#ifdef Q_OS_WIN
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("AIS-catcher.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("aiscatcher.exe"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("aisdeco2.exe"));
#else
    QString sysBin = QStandardPaths::findExecutable(QStringLiteral("AIS-catcher"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("aiscatcher"));
    if (sysBin.isEmpty()) sysBin = QStandardPaths::findExecutable(QStringLiteral("aisdeco2"));
#endif
    if (!sysBin.isEmpty()) return sysBin;

    return defaultBinaryPath();
}

QString AisCatcherManager::webUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(webPort_);
}

bool AisCatcherManager::binaryExists() const
{
    return QFileInfo::exists(resolvedBinaryPath());
}

void AisCatcherManager::rememberProcessOutput(const QString& line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return;
    recentOutput_.append(trimmed);
    while (recentOutput_.size() > 12)
        recentOutput_.removeFirst();
}

QString AisCatcherManager::recentProcessOutput() const
{
    return recentOutput_.join(QStringLiteral(" | "));
}

QString AisCatcherManager::stateString() const
{
    switch (state_) {
    case State::Stopped:  return QStringLiteral("stopped");
    case State::Starting: return QStringLiteral("starting");
    case State::Running:  return QStringLiteral("running");
    case State::Error:    return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

QJsonObject AisCatcherManager::statusJson() const
{
    QJsonObject o;
    o["state"]   = stateString();
    o["webUrl"]  = webUrl();
    o["webPort"] = webPort_;
    if (!lastError_.isEmpty())
        o["error"] = lastError_;
    return o;
}

// ---------------------------------------------------------------------------
//  Ciclo de vida
// ---------------------------------------------------------------------------

bool AisCatcherManager::start()
{
    if (state_ == State::Running || state_ == State::Starting) return true;

    const QString bin = resolvedBinaryPath();
    if (!QFileInfo::exists(bin)) {
#ifdef Q_OS_WIN
        lastError_ = QStringLiteral("Nenhum decodificador AIS encontrado (AIS-catcher.exe ou aisdeco2.exe).");
#else
        lastError_ = QStringLiteral("Nenhum decodificador AIS encontrado (AIS-catcher ou aisdeco2).");
#endif
        state_ = State::Error;
        emit error(lastError_);
        emit stateChanged(state_);
        return false;
    }

    recentOutput_.clear();
    resetResampler();

    if (localClient_) {
        localClient_->disconnect();
        localClient_->close();
        localClient_ = nullptr;
    }
    if (localServer_) {
        localServer_->close();
        localServer_->deleteLater();
        localServer_ = nullptr;
    }

    process_ = std::make_unique<QProcess>(this);
    connect(process_.get(), &QProcess::started,
            this, &AisCatcherManager::onProcessStarted);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &AisCatcherManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &AisCatcherManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &AisCatcherManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &AisCatcherManager::onReadyReadStderr);

    process_->setWorkingDirectory(QFileInfo(bin).absolutePath());
    process_->setProcessChannelMode(QProcess::SeparateChannels);

    QStringList args;
    const QString fileName = QFileInfo(bin).fileName().toLower();

    if (fileName.contains(QStringLiteral("aisdeco2"))) {
        // aisdeco2 usa diretamente o RTL-SDR (modo exclusivo, não audio)
        args << QStringLiteral("--device-index") << QString::number(deviceIndex_)
             << QStringLiteral("--http-port") << QString::number(webPort_)
             << QStringLiteral("--freq") << QStringLiteral("161975000")
             << QStringLiteral("--freq") << QStringLiteral("162025000");
    } else {
#ifdef Q_OS_WIN
        // AIS-catcher: cria pipe nomeado local e passa para o decodificador
        QLocalServer::removeServer(QStringLiteral("rxsdr_ais"));
        localServer_ = new QLocalServer(this);
        if (!localServer_->listen(QStringLiteral("rxsdr_ais"))) {
            lastError_ = QStringLiteral("Falha ao iniciar servidor local de pipe: %1").arg(localServer_->errorString());
            state_ = State::Error;
            emit error(lastError_);
            emit stateChanged(state_);
            localServer_->deleteLater();
            localServer_ = nullptr;
            return false;
        }
        connect(localServer_, &QLocalServer::newConnection, this, &AisCatcherManager::onNewConnection);

        // passamos o pipe nomeado do Windows: \\.\pipe\rxsdr_ais
        // -m 2  : modelo padrao, o que trabalha com IQ. O -m 3 e o "FM
        //         discriminator model", que espera audio de discriminador e
        //         so aceita algumas taxas - era ele que recusava a nossa.
        // -o 1  : NMEA puro no stdout. O padrao (-o 2, "NMEA+") acrescenta
        //         detalhes e as linhas deixam de comecar com "!", que e o
        //         filtro usado em onReadyReadStdout().
        // -X off: nao compartilhar com o aiscatcher.org (vem ligado de fabrica).
        args << QStringLiteral("-m") << QStringLiteral("2")
             << QStringLiteral("-r") << QStringLiteral("CS16") << QStringLiteral("\\\\.\\pipe\\rxsdr_ais")
             << QStringLiteral("-s") << QString::number(kAisIqRate)
             << QStringLiteral("-o") << QStringLiteral("1")
             << QStringLiteral("-N") << QString::number(webPort_)
             << QStringLiteral("-X") << QStringLiteral("off");
#else
        // No Linux, lemos via stdin.
        //
        // O nome da entrada padrao no AIS-catcher e PONTO, nao hifen. A propria
        // ajuda diz: "-r [optional: yy] filename - read IQ data from file or
        // stdin (.)". Com "-" ele procura um arquivo chamado "-", nao acha e
        // encerra com "FILE: Cannot open input" - era o erro do painel AIS.
        args << QStringLiteral("-m") << QStringLiteral("2")
             << QStringLiteral("-r") << QStringLiteral("CS16") << QStringLiteral(".")
             << QStringLiteral("-s") << QString::number(kAisIqRate)
             << QStringLiteral("-o") << QStringLiteral("1")
             << QStringLiteral("-N") << QString::number(webPort_)
             << QStringLiteral("-X") << QStringLiteral("off");
#endif
    }

    state_ = State::Starting;
    emit stateChanged(state_);

#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    process_->start(bin, args);
    return true;
}

void AisCatcherManager::stop()
{
    state_ = State::Stopped;

    if (localClient_) {
        localClient_->disconnect();
        localClient_->close();
        localClient_ = nullptr;
    }
    if (localServer_) {
        localServer_->close();
        localServer_->deleteLater();
        localServer_ = nullptr;
    }

    if (process_) {
        process_->terminate();
        if (!process_->waitForFinished(4000)) {
            process_->kill();
            process_->waitForFinished(2000);
        }
        process_.reset();
    }
    resetResampler();

    emit stateChanged(State::Stopped);
}

// ---------------------------------------------------------------------------
//  Alimentação de áudio PCM (igual ao ACARS/Packet)
// ---------------------------------------------------------------------------

void AisCatcherManager::feedAudio(const int16_t* samples, int count, uint32_t sampleRate)
{
    if (!samples || count <= 0 || !process_ || state_ != State::Running) return;

    QByteArray pcm;

    if (static_cast<int>(sampleRate) == kAisAudioRate) {
        // Taxa já correta — copia diretamente
        pcm.resize(count * static_cast<int>(sizeof(qint16)));
        std::copy_n(reinterpret_cast<const char*>(samples), pcm.size(), pcm.data());
    } else {
        // Reamostrar para kAisAudioRate (48000 Hz)
        std::vector<float> input;
        input.reserve(static_cast<size_t>(count) + 1);
        if (hasResampleLast_) input.push_back(resampleLast_);
        for (int i = 0; i < count; ++i)
            input.push_back(static_cast<float>(samples[i]) / 32768.0f);

        if (input.size() < 2 || sampleRate == 0) return;

        const double step = static_cast<double>(sampleRate) / static_cast<double>(kAisAudioRate);
        std::vector<qint16> out;
        double pos = resamplePos_;
        while (pos + 1.0 < static_cast<double>(input.size())) {
            const int idx = static_cast<int>(pos);
            const double frac = pos - static_cast<double>(idx);
            const float v = input[static_cast<size_t>(idx)]     * static_cast<float>(1.0 - frac)
                          + input[static_cast<size_t>(idx + 1)] * static_cast<float>(frac);
            const float c = std::clamp(v, -1.0f, 1.0f);
            out.push_back(static_cast<qint16>(std::lround(c * 32767.0f)));
            pos += step;
        }

        hasResampleLast_ = true;
        resampleLast_    = input.back();
        resamplePos_     = pos - static_cast<double>(input.size() - 1);

        if (out.empty()) return;
        pcm.resize(static_cast<int>(out.size() * sizeof(qint16)));
        std::copy_n(reinterpret_cast<const char*>(out.data()), pcm.size(), pcm.data());
    }

    // Emite sinal para escrever no stdin do AIS-catcher de forma segura na thread principal
    emit writeToStdin(pcm);
}

// ---------------------------------------------------------------------------
//  Slots do processo
// ---------------------------------------------------------------------------

void AisCatcherManager::onProcessStarted()
{
    state_ = State::Running;
    lastError_.clear();
    emit stateChanged(state_);
    const QString name = QFileInfo(resolvedBinaryPath()).fileName();
    emit logLine(QStringLiteral("[%1] iniciado — console web: %2").arg(name).arg(webUrl()));
}

void AisCatcherManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (state_ != State::Stopped) {
        const QString name   = QFileInfo(resolvedBinaryPath()).fileName();
        const QString output = recentProcessOutput();
        if (status == QProcess::NormalExit && exitCode == 0) {
            lastError_ = QStringLiteral("%1 parou sozinho.").arg(name);
        } else {
            lastError_ = QStringLiteral("%1 falhou (codigo %2).").arg(name).arg(exitCode);
        }
        if (!output.isEmpty())
            lastError_ += QStringLiteral(" Ultima saida: %1").arg(output);
        state_ = State::Error;
        emit error(lastError_);
    }
    emit stateChanged(state_);
}

void AisCatcherManager::onProcessError(QProcess::ProcessError /*err*/)
{
    const QString name = QFileInfo(resolvedBinaryPath()).fileName();
    lastError_ = QStringLiteral("Erro de processo do %1: %2")
                     .arg(name)
                     .arg(process_ ? process_->errorString() : QString());
    state_ = State::Error;
    emit error(lastError_);
    emit stateChanged(state_);
}

void AisCatcherManager::onReadyReadStdout()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardOutput();
    for (const QString& line : QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        rememberProcessOutput(trimmed);
        
        // Só repassa para o terminal se for uma mensagem NMEA decodificada (começa com ! ou $)
        if (trimmed.startsWith(QLatin1Char('!')) || trimmed.startsWith(QLatin1Char('$'))) {
            emit logLine(trimmed);
        }
    }
}

void AisCatcherManager::onReadyReadStderr()
{
    if (!process_) return;
    const QByteArray data = process_->readAllStandardError();
    for (const QString& line : QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        rememberProcessOutput(trimmed);
        // Filtramos todas as mensagens de status, estatísticas e timeout do stderr para não poluir o terminal
    }
}

// ---------------------------------------------------------------------------
//  feedIQ - caminho correto do sinal
//  O AIS-catcher e lancado com "-r CS16", ou seja, ele espera IQ COMPLEXO.
//  Antes o RXSDR mandava audio do discriminador FM (um numero real por
//  amostra) e o decodificador lia duas amostras de audio como se fossem I e Q
//  de uma so - por isso nunca saia NMEA nenhum e o mapa ficava vazio.
// ---------------------------------------------------------------------------
void AisCatcherManager::feedIQ(const std::complex<float>* iq, size_t count, uint32_t sps)
{
    if (!iq || count == 0 || !process_ || state_ != State::Running) return;
    if (sps == 0) return;

    // Anti-aliasing: media movel de M amostras, com M = sps / taxa de saida.
    // A resposta sinc zera exatamente na taxa de saida, matando o alias
    // dominante da decimacao (2 MS/s -> 96 kS/s e cerca de 21:1).
    const size_t M = std::max<size_t>(1,
        static_cast<size_t>(std::llround(double(sps) / double(kAisIqRate))));
    if (sps != aaSps_ || M != aaLen_) {
        aaLen_ = M;
        aaRing_.assign(M, {0.0f, 0.0f});
        aaSum_ = {0.0, 0.0};
        aaIdx_ = 0;
        aaSps_ = sps;
        // Corte em 50 kHz: passa os dois canais AIS (+-25 kHz) e ainda derruba
        // o que vem de perto da borda da janela. Medido com um intruso forte
        // em +105 kHz (caso do DMR em 162,105 com o VFO em 162,000):
        //   40 kHz -> AIS 20,4 dB acima do intruso
        //   50 kHz -> AIS 13,3 dB acima, e 2,8 dB mais sensivel  <= escolhido
        //   60 kHz -> AIS so 7,5 dB acima; margem curta demais
        // Os 2,8 dB extras valem para quem nao tem vizinho forte, e 13 dB de
        // folga continuam confortaveis para quem tem.
        for (IirLpf* f : { &iqLpf_, &iqLpf2_, &iqLpf3_ }) {
            f->setCutoff(50000.0f, float(sps));
            f->reset();
        }
        iqTail_.clear();
        iqPos_ = 0.0;
    }

    const double invM = 1.0 / double(M);
    std::vector<std::complex<float>> filtrado(count);
    for (size_t i = 0; i < count; ++i) {
        std::complex<float> v = iqLpf3_.process(iqLpf2_.process(iqLpf_.process(iq[i])));
        aaSum_ += std::complex<double>(v.real(), v.imag())
                - std::complex<double>(aaRing_[aaIdx_].real(), aaRing_[aaIdx_].imag());
        aaRing_[aaIdx_] = v;
        aaIdx_ = (aaIdx_ + 1) % M;
        filtrado[i] = std::complex<float>(float(aaSum_.real() * invM),
                                          float(aaSum_.imag() * invM));
    }

    // Decimacao para kAisIqRate por interpolacao linear, guardando a sobra
    // entre chamadas para nao perder continuidade de fase.
    const double passo = double(sps) / double(kAisIqRate);
    if (passo <= 0.0) return;

    iqTail_.insert(iqTail_.end(), filtrado.begin(), filtrado.end());

    std::vector<std::complex<float>> saida;
    saida.reserve(size_t(double(count) / passo) + 4);
    while (true) {
        const int i0 = int(std::floor(iqPos_));
        if (i0 + 1 >= int(iqTail_.size())) break;
        const float frac = float(iqPos_ - double(i0));
        const std::complex<float>& a = iqTail_[i0];
        const std::complex<float>& b = iqTail_[i0 + 1];
        saida.emplace_back(a + (b - a) * frac);
        iqPos_ += passo;
    }
    const int consumido = std::min(int(std::floor(iqPos_)), int(iqTail_.size()) - 1);
    if (consumido > 0) {
        iqTail_.erase(iqTail_.begin(), iqTail_.begin() + consumido);
        iqPos_ -= double(consumido);
    }
    if (saida.empty()) return;

    // CS16: I e Q intercalados, 16 bits com sinal.
    QByteArray bloco;
    bloco.resize(int(saida.size() * 2 * sizeof(qint16)));
    qint16* dst = reinterpret_cast<qint16*>(bloco.data());
    for (size_t i = 0; i < saida.size(); ++i) {
        const float I = std::clamp(saida[i].real(), -1.0f, 1.0f);
        const float Q = std::clamp(saida[i].imag(), -1.0f, 1.0f);
        dst[2*i]     = static_cast<qint16>(std::lround(I * 32767.0f));
        dst[2*i + 1] = static_cast<qint16>(std::lround(Q * 32767.0f));
    }

    emit writeToStdin(bloco);
}

void AisCatcherManager::onWriteToStdin(const QByteArray& data)
{
#ifdef Q_OS_WIN
    if (!localClient_ || !localClient_->isOpen()) return;

    qint64 written = 0;
    const char* ptr = data.constData();
    const qint64 size = data.size();
    while (written < size) {
        const qint64 w = localClient_->write(ptr + written, size - written);
        if (w < 0) {
            lastError_ = QStringLiteral("Escrita pipe AIS-catcher falhou: %1").arg(localClient_->errorString());
            emit error(lastError_);
            stop();
            return;
        }
        if (w == 0) {
            if (!localClient_->waitForBytesWritten(100)) return;
            continue;
        }
        written += w;
    }
#else
    if (!process_ || state_ != State::Running) return;

    qint64 written = 0;
    const char* ptr = data.constData();
    const qint64 size = data.size();
    while (written < size) {
        if (!process_ || state_ != State::Running) return;
        const qint64 w = process_->write(ptr + written, size - written);
        if (w < 0) {
            lastError_ = QStringLiteral("Escrita stdin AIS-catcher falhou: %1").arg(process_->errorString());
            emit error(lastError_);
            stop();
            return;
        }
        if (w == 0) {
            if (!process_->waitForBytesWritten(100)) return;
            continue;
        }
        written += w;
    }
#endif
}

void AisCatcherManager::onNewConnection()
{
    localClient_ = localServer_->nextPendingConnection();
    connect(localClient_, &QLocalSocket::disconnected, this, &AisCatcherManager::onClientDisconnected);
    emit logLine(QStringLiteral("[AIS] Cliente pipe conectado."));
}

void AisCatcherManager::onClientDisconnected()
{
    emit logLine(QStringLiteral("[AIS] Cliente pipe desconectado."));
    if (localClient_) {
        localClient_->deleteLater();
        localClient_ = nullptr;
    }
}

void AisCatcherManager::resetResampler()
{
    resamplePos_     = 0.0;
    resampleLast_    = 0.0f;
    hasResampleLast_ = false;
}

} // namespace masdr
