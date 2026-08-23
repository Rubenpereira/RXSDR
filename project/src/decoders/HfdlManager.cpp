#include "HfdlManager.h"

#include "../util/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMetaObject>

namespace masdr {

HfdlManager::HfdlManager(QObject* parent) : QObject(parent) {}

HfdlManager::~HfdlManager() {
    stop();
}

QString HfdlManager::pastaDecoders() const {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/decoders");
}

QString HfdlManager::binaryPath() const {
#ifdef Q_OS_WIN
    return pastaDecoders() + QStringLiteral("/dumphfdl.exe");
#else
    return pastaDecoders() + QStringLiteral("/dumphfdl");
#endif
}

QString HfdlManager::systablePath() const {
    return pastaDecoders() + QStringLiteral("/hfdl_systable.conf");
}

bool HfdlManager::binaryExists() const {
    return QFileInfo::exists(binaryPath());
}

QString HfdlManager::stateString() const {
    switch (state_) {
        case State::Stopped:  return QStringLiteral("parado");
        case State::Starting: return QStringLiteral("iniciando");
        case State::Running:  return QStringLiteral("rodando");
        case State::Error:    return QStringLiteral("erro");
    }
    return QStringLiteral("?");
}

void HfdlManager::mudarEstado(State novo) {
    if (state_ == novo) {
        return;
    }
    state_ = novo;
    emit stateChanged(novo);
}

QJsonObject HfdlManager::statusJson() const {
    QJsonObject o;
    o.insert("estado", stateString());
    o.insert("erro", lastError_);
    o.insert("mensagens", mensagensRecebidas_);
    o.insert("centroKHz", params_.centroKHz);
    o.insert("sampleRate", static_cast<int>(params_.sampleRate));
    QJsonArray canais;
    for (double c : params_.canaisKHz) {
        canais.append(c);
    }
    o.insert("canais", canais);
    o.insert("binario", binaryExists());
    return o;
}

bool HfdlManager::start() {
    if (process_) {
        return true;
    }

    if (!binaryExists()) {
        lastError_ = QStringLiteral("dumphfdl nao encontrado em ") + binaryPath();
        mudarEstado(State::Error);
        emit error(lastError_);
        return false;
    }
    if (params_.canaisKHz.isEmpty() || params_.sampleRate == 0) {
        lastError_ = QStringLiteral("faltou dizer os canais ou a taxa de amostragem");
        mudarEstado(State::Error);
        emit error(lastError_);
        return false;
    }

    QStringList args;
    args << QStringLiteral("--iq-file") << QStringLiteral("-")
         << QStringLiteral("--sample-format") << QStringLiteral("CF32")
         << QStringLiteral("--sample-rate") << QString::number(params_.sampleRate)
         << QStringLiteral("--centerfreq") << QString::number(params_.centroKHz, 'f', 3)
         << QStringLiteral("--utc");

    // A tabela de estacoes da nome a quem transmite: sem ela as mensagens vem
    // com o numero da estacao, e ninguem decora que 13 e Santa Cruz.
    if (QFileInfo::exists(systablePath())) {
        args << QStringLiteral("--system-table") << systablePath();
    }

    for (double c : params_.canaisKHz) {
        args << QString::number(c, 'f', 1);
    }

    process_ = std::make_unique<QProcess>();
    process_->setProgram(binaryPath());
    process_->setArguments(args);
    process_->setWorkingDirectory(pastaDecoders());
    process_->setProcessChannelMode(QProcess::SeparateChannels);

    // Contexto explicito nos connects.
    //
    // Sem o "this" como terceiro argumento, a lambda roda na thread que EMITE
    // o sinal. Foi assim que doze decodificadores derrubaram a conexao do
    // navegador em 20/08/2026: escreviam na tela a partir da thread errada.
    connect(process_.get(), &QProcess::started,
            this, &HfdlManager::onProcessStarted);
    connect(process_.get(), &QProcess::finished,
            this, &HfdlManager::onProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred,
            this, &HfdlManager::onProcessError);
    connect(process_.get(), &QProcess::readyReadStandardOutput,
            this, &HfdlManager::onReadyReadStdout);
    connect(process_.get(), &QProcess::readyReadStandardError,
            this, &HfdlManager::onReadyReadStderr);

    lastError_.clear();
    mensagensRecebidas_ = 0;
    parcial_.clear();
    restoStdout_.clear();
    restoStderr_.clear();
    {
        QMutexLocker trava(&pendenteMutex_);
        pendente_.clear();
    }

    mudarEstado(State::Starting);
    Logger::info(QStringLiteral("HFDL: %1 %2").arg(binaryPath(), args.join(' ')));
    process_->start();

    if (!process_->waitForStarted(5000)) {
        lastError_ = QStringLiteral("o dumphfdl nao subiu: ") + process_->errorString();
        mudarEstado(State::Error);
        emit error(lastError_);
        process_.reset();
        return false;
    }
    return true;
}

void HfdlManager::stop() {
    if (!process_) {
        mudarEstado(State::Stopped);
        return;
    }

    // Fechar a entrada e o jeito educado: o dumphfdl ve o fim do IQ e encerra
    // sozinho, gravando o que ainda estava no meio do caminho. Matar de vez
    // vem so depois, se ele nao entender o recado.
    process_->closeWriteChannel();
    if (!process_->waitForFinished(1500)) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    process_.reset();

    {
        QMutexLocker trava(&pendenteMutex_);
        pendente_.clear();
    }
    mudarEstado(State::Stopped);
}

void HfdlManager::feedIQ(const std::complex<float>* iq, size_t count, uint32_t sps) {
    if (!process_ || state_ == State::Stopped || count == 0) {
        return;
    }
    // A taxa combinada no start e a que o dumphfdl esta usando para contar as
    // frequencias. Se o radio mudar de taxa no meio, o que ele decodifica
    // deixa de bater com o que esta no ar - melhor nao entregar nada do que
    // entregar errado.
    if (sps != params_.sampleRate) {
        return;
    }

    const char* bytes = reinterpret_cast<const char*>(iq);
    const int tamanho = static_cast<int>(count * sizeof(std::complex<float>));

    {
        QMutexLocker trava(&pendenteMutex_);
        if (pendente_.size() + tamanho > kMaxPendenteBytes) {
            // Fila cheia: o dumphfdl nao esta acompanhando. Descarta o mais
            // ANTIGO, nao o novo - o que interessa e o que esta no ar agora.
            const int sobra = pendente_.size() + tamanho - kMaxPendenteBytes;
            pendente_.remove(0, qMin(sobra, pendente_.size()));
        }
        pendente_.append(bytes, tamanho);
    }

    // A escrita tem de acontecer na thread dona do QProcess - ver o cabecalho.
    QMetaObject::invokeMethod(this, "drenarStdin", Qt::QueuedConnection);
}

void HfdlManager::drenarStdin() {
    if (!process_ || process_->state() != QProcess::Running) {
        return;
    }
    QByteArray lote;
    {
        QMutexLocker trava(&pendenteMutex_);
        lote.swap(pendente_);
    }
    if (lote.isEmpty()) {
        return;
    }
    process_->write(lote);
}

void HfdlManager::onProcessStarted() {
    mudarEstado(State::Running);
    emit logLine(QStringLiteral("[HFDL] decodificador no ar, acompanhando %1 canais")
                     .arg(params_.canaisKHz.size()));
}

void HfdlManager::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status)
    emit logLine(QStringLiteral("[HFDL] decodificador encerrou (codigo %1)").arg(exitCode));
    mudarEstado(State::Stopped);
}

void HfdlManager::onProcessError(QProcess::ProcessError err) {
    Q_UNUSED(err)
    if (!process_) {
        return;
    }
    lastError_ = process_->errorString();
    mudarEstado(State::Error);
    emit error(lastError_);
}

void HfdlManager::onReadyReadStdout() {
    if (!process_) {
        return;
    }
    restoStdout_ += QString::fromUtf8(process_->readAllStandardOutput());

    // Guarda o pedaco final sem quebra de linha: ele quase sempre e uma linha
    // que chegou pela metade, e juntar com o proximo bloco e o que evita ver
    // uma mensagem partida no meio.
    const int ultimaQuebra = restoStdout_.lastIndexOf('\n');
    if (ultimaQuebra < 0) {
        return;
    }
    const QString completo = restoStdout_.left(ultimaQuebra);
    restoStdout_.remove(0, ultimaQuebra + 1);

    const QStringList linhas = completo.split('\n');
    for (const QString& l : linhas) {
        processarLinha(l);
    }
}

void HfdlManager::processarLinha(const QString& linha) {
    const QString limpa = linha.trimmed();

    // Linha em branco fecha a mensagem. O dumphfdl escreve cada mensagem em
    // varias linhas e separa uma da outra com uma linha vazia.
    if (limpa.isEmpty()) {
        if (!parcial_.isEmpty()) {
            mensagensRecebidas_++;
            emit mensagem(parcial_);
            parcial_.clear();
        }
        return;
    }

    if (!parcial_.isEmpty()) {
        parcial_ += '\n';
    }
    parcial_ += limpa;
}

void HfdlManager::onReadyReadStderr() {
    if (!process_) {
        return;
    }

    // A saida de erro chega em PEDACOS, nao em linhas.
    //
    // O sistema entrega o que ja tem no cano, e isso corta palavra no meio. A
    // primeira versao mandava cada pedaco para a tela como se fosse uma linha,
    // e "Aircraft cache TTL set to 3600 seconds" apareceu picado em onze
    // linhas: "Aircraft c", "ac", "he", "TTL s", "e", "to", "36", "00 s",
    // "co", "nds". Parecia defeito do dumphfdl, mas era daqui.
    //
    // Agora o pedaco final sem quebra de linha fica guardado e se junta ao
    // proximo bloco - o mesmo cuidado que a saida normal ja tinha.
    restoStderr_ += QString::fromUtf8(process_->readAllStandardError());

    const int ultimaQuebra = restoStderr_.lastIndexOf('\n');
    if (ultimaQuebra < 0) {
        return;
    }
    const QString completo = restoStderr_.left(ultimaQuebra);
    restoStderr_.remove(0, ultimaQuebra + 1);

    for (const QString& l : completo.split('\n')) {
        const QString limpa = l.trimmed();
        if (!limpa.isEmpty()) {
            emit logLine(QStringLiteral("[HFDL] ") + limpa);
        }
    }
}

} // namespace masdr
