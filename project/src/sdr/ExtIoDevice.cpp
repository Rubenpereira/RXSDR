#include "ExtIoDevice.h"
#include "../util/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <cstring>   // memcpy
#ifdef Q_OS_WIN
#include <windows.h>   // CREATE_NO_WINDOW
#endif

namespace masdr {

namespace {
// Quanto esperar por IQ depois de o start ser aceito.
//
// Dois segundos e folgado: a 55555 amostras por segundo, a taxa mais lenta do
// SDR-IQ, um bloco de 2048 leva 37 ms. Se em 2 s nao veio nada, nao e lentidao.
constexpr int kEsperaPrimeiroDadoMs = 2000;
}

ExtIoDevice::ExtIoDevice() {
    // Este objeto tem de viver na thread do laco de eventos.
    //
    // Ele e criado pelo DeviceFactory, chamado de dentro do tratador de HTTP.
    // Um QProcess so entrega readyRead se a thread dona tiver laco de eventos
    // rodando; nascendo na thread errada, TUDO funcionaria - abrir, sintonizar,
    // ate o rele bater - e nenhum byte de IQ chegaria, porque o sinal que
    // avisa "tem dado para ler" nunca seria despachado. Sintoma sem pista.
    if (QCoreApplication::instance() && thread() != QCoreApplication::instance()->thread()) {
        moveToThread(QCoreApplication::instance()->thread());
    }
    vigia_.setInterval(500);
    // Contexto explicito no connect - ver o cabecalho do HfdlManager. Sem o
    // "this", a lambda correria na thread que emite o sinal.
    connect(&vigia_, &QTimer::timeout, this, [this]() {
        if (!rodando_) return;

        // Pulso: quantas amostras ESTE lado ja recebeu, a cada 5 segundos.
        //
        // A ponte conta as dela e o RXSDR conta as dele. Com os dois numeros
        // lado a lado no log da para separar dois problemas que na tela
        // parecem o mesmo: "o aparelho nao entrega" e "entrega, mas o sinal
        // se perde no caminho ate aqui".
        if (++pulsos_ >= 10) {
            pulsos_ = 0;
            Logger::info(QStringLiteral("ExtIO: recebidas %1 amostras ate agora (taxa em uso %2 Hz)")
                             .arg(amostras_).arg(sps_));
            enviar(QStringLiteral("status"));
        }

        if (jaAvisouSemDado_) return;
        if (amostras_ > 0) {
            jaAvisouSemDado_ = true;
            // Um retrato do fluxo assim que ele comeca: taxa em uso e quantas
            // amostras a ponte precisou descartar. Sem isto so se sabe que
            // "chega alguma coisa", e nao se chega tudo.
            enviar(QStringLiteral("status"));
            return;
        }
        if (desdeStart_.elapsed() < kEsperaPrimeiroDadoMs) return;
        jaAvisouSemDado_ = true;
        lastError_ = QStringLiteral(
            "a ExtIO aceitou tudo mas nao chegou nenhuma amostra - o aparelho "
            "provavelmente nao esta ligado ou nao esta conectado. Esta DLL nao "
            "avisa quando o radio nao esta la; quem percebeu foi o RXSDR.");
        Logger::warn(QStringLiteral("ExtIO: ") + lastError_);
    });
}

ExtIoDevice::~ExtIoDevice() {
    close();
}

QString ExtIoDevice::pastaExtio() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/extio");
}

QString ExtIoDevice::caminhoPonte() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/rxsdr_extio_bridge.exe");
}

QStringList ExtIoDevice::procurarDlls() {
    QStringList achados;
    QDir d(pastaExtio());
    if (!d.exists()) return achados;
    // Nao filtramos por nome comecando em "ExtIO_" de proposito: ha DLLs por
    // ai com outros nomes, e quem largou o arquivo na pasta sabe o que quer.
    for (const QFileInfo& fi : d.entryInfoList(QStringList() << "*.dll", QDir::Files, QDir::Name)) {
        achados << fi.absoluteFilePath();
    }
    return achados;
}

void ExtIoDevice::enviar(const QString& comando) {
    if (!proc_ || proc_->state() != QProcess::Running) return;
    proc_->write(comando.toUtf8() + "\n");
}

// Le stderr ate aparecer a marca esperada, ou ate o tempo acabar.
//
// So usado na abertura. Depois disso tudo corre por sinais; bloquear com o
// radio no ar prenderia a interface inteira.
bool ExtIoDevice::esperarPor(const QString& marca, int ms) {
    QElapsedTimer t; t.start();
    ultimaMarca_.clear();
    while (t.elapsed() < ms) {
        if (!proc_ || proc_->state() != QProcess::Running) return false;
        proc_->waitForReadyRead(100);
        lerEstado();
        if (ultimaMarca_ == marca) return true;
        if (ultimaMarca_ == QStringLiteral("error")) return false;
    }
    return false;
}

bool ExtIoDevice::open(const QString& serial) {
#ifndef Q_OS_WIN
    lastError_ = QStringLiteral("ExtIO so existe no Windows - nas caixas com Armbian "
                                "nao ha equivalente");
    return false;
#else
    close();
    dllPath_ = serial.trimmed();
    if (dllPath_.isEmpty()) {
        const QStringList d = procurarDlls();
        if (d.isEmpty()) {
            lastError_ = QStringLiteral("nenhuma DLL na pasta extio/ e nenhum caminho informado");
            return false;
        }
        dllPath_ = d.first();
    }
    if (!QFileInfo::exists(dllPath_)) {
        lastError_ = QStringLiteral("nao achei ") + dllPath_;
        return false;
    }
    const QString ponte = caminhoPonte();
    if (!QFileInfo::exists(ponte)) {
        lastError_ = QStringLiteral("falta o rxsdr_extio_bridge.exe - rode o COMPILAR_EXTIO.bat");
        return false;
    }

    proc_ = std::make_unique<QProcess>();
    proc_->setProgram(ponte);
    proc_->setArguments(QStringList() << dllPath_);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    // Sem janela de console.
    //
    // O RXSDR e programa de janela e nao tem console; ao criar um filho, o
    // Windows abre um console NOVO para ele - uma tela preta que fica aberta
    // o tempo todo ao lado do radio, e que o usuario e capaz de fechar sem
    // saber que esta desligando a fonte de sinal.
    proc_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* a) {
            a->flags |= CREATE_NO_WINDOW;
        });
#endif
    connect(proc_.get(), &QProcess::readyReadStandardOutput, this, &ExtIoDevice::lerSaida);
    connect(proc_.get(), &QProcess::readyReadStandardError,  this, &ExtIoDevice::lerEstado);
    // A ponte sumir nao pode virar silencio.
    //
    // Ja aconteceu: por um engano na deteccao de clique duplo, ela fazia o
    // autoexame e encerrava sozinha cinco segundos depois de abrir. Do lado de
    // ca nada acusava - o radio ficava mandando comando para um processo que
    // nao existia mais, e a tela so mostrava cachoeira parada.
    connect(proc_.get(), &QProcess::finished, this,
            [this](int codigo, QProcess::ExitStatus) {
                if (!rodando_ && lastError_.isEmpty() && codigo == 0) return;
                lastError_ = QStringLiteral(
                    "a ponte ExtIO encerrou sozinha (codigo %1) - o radio ficou sem fonte de sinal")
                    .arg(codigo);
                Logger::warn(QStringLiteral("ExtIO: ") + lastError_);
                rodando_ = false;
                vigia_.stop();
            });

    sobra_.clear(); sobraTexto_.clear(); amostras_ = 0; taxas_.clear();
    lastError_.clear();

    proc_->start();
    if (!proc_->waitForStarted(5000)) {
        lastError_ = QStringLiteral("a ponte nao subiu: ") + proc_->errorString();
        proc_.reset();
        return false;
    }
    if (!esperarPor(QStringLiteral("loaded"), 8000)) {
        if (lastError_.isEmpty()) lastError_ = QStringLiteral("a ponte nao conseguiu carregar a DLL");
        close();
        return false;
    }
    enviar(QStringLiteral("open"));
    if (!esperarPor(QStringLiteral("ready"), 15000)) {
        if (lastError_.isEmpty()) lastError_ = QStringLiteral("a ExtIO nao abriu o aparelho");
        close();
        return false;
    }
    Logger::info(QStringLiteral("ExtIO aberta: %1 (%2)").arg(nome_, dllPath_));
    return true;
#endif
}

void ExtIoDevice::close() {
    vigia_.stop();
    rodando_ = false;
    if (!proc_) return;
    enviar(QStringLiteral("quit"));
    // Fechar a entrada e o jeito educado; a ponte ve o fim e encerra sozinha,
    // devolvendo o aparelho. Matar vem depois, se ela nao entender.
    proc_->closeWriteChannel();
    if (!proc_->waitForFinished(2000)) {
        proc_->kill();
        proc_->waitForFinished(1000);
    }
    proc_.reset();
}

void ExtIoDevice::start() {
    if (rodando_) return;
    amostras_ = 0;
    jaAvisouSemDado_ = false;
    enviar(QStringLiteral("start"));
    rodando_ = true;
    desdeStart_.start();
    vigia_.start();
}

void ExtIoDevice::stop() {
    if (!rodando_) return;
    enviar(QStringLiteral("stop"));
    rodando_ = false;
    vigia_.stop();
}

void ExtIoDevice::setCenterFreq(uint64_t hz) {
    freq_ = hz;
    enviar(QStringLiteral("freq %1").arg(hz));
}

void ExtIoDevice::setSampleRate(uint32_t sps) {
    enviar(QStringLiteral("rate %1").arg(sps));
    // sps_ nao muda aqui de proposito: quem manda e a resposta da ponte, que
    // traz a taxa REAL escolhida. Guardar o pedido faria toda a conta de
    // frequencia por bin da FFT sair errada quando o aparelho desse outra.
}

void ExtIoDevice::setGain(int tenthsDb) {
    gainTenths_ = tenthsDb;
    enviar(QStringLiteral("gain %1").arg(tenthsDb));
}

void ExtIoDevice::mostrarGui(bool mostrar) {
    enviar(mostrar ? QStringLiteral("gui show") : QStringLiteral("gui hide"));
}

// ---------------------------------------------------------------------------
//  IQ
// ---------------------------------------------------------------------------
void ExtIoDevice::lerSaida() {
    if (!proc_) return;
    sobra_ += proc_->readAllStandardOutput();

    // Uma amostra sao dois floats. O que sobrar de um par incompleto fica para
    // o proximo bloco - cortar no meio de uma amostra trocaria I por Q dali
    // para a frente, e o espectro viraria espelho sem nada acusar.
    const int porAmostra = int(sizeof(float) * 2);
    const int n = sobra_.size() / porAmostra;
    if (n <= 0) return;

    bloco_.resize(size_t(n));
    memcpy(bloco_.data(), sobra_.constData(), size_t(n) * size_t(porAmostra));
    sobra_.remove(0, n * porAmostra);

    amostras_ += quint64(n);
    if (cb_) cb_(bloco_.data(), size_t(n));
}

// ---------------------------------------------------------------------------
//  texto
// ---------------------------------------------------------------------------
void ExtIoDevice::lerEstado() {
    if (!proc_) return;
    sobraTexto_ += proc_->readAllStandardError();

    // Chega em pedacos, nao em linhas - a mesma armadilha que ja picotou uma
    // frase do HFDL em onze partes. Guarda o rabo sem quebra de linha.
    int corte = sobraTexto_.lastIndexOf('\n');
    if (corte < 0) return;
    const QByteArray completo = sobraTexto_.left(corte);
    sobraTexto_.remove(0, corte + 1);

    for (const QByteArray& l : completo.split('\n')) {
        const QString linha = QString::fromUtf8(l).trimmed();
        if (linha.isEmpty()) continue;
        if (linha.startsWith('#')) {
            Logger::info(QStringLiteral("ExtIO: ") + linha.mid(1));
        } else if (linha.startsWith('!')) {
            // Vai para o log tambem. Na primeira versao so as linhas de '#'
            // eram gravadas, e por isso o relatorio que veio de fora nao
            // trazia a taxa em uso nem quantas amostras foram descartadas -
            // justamente os dois numeros que eu precisava para conferir se o
            // fluxo estava inteiro.
            Logger::info(QStringLiteral("ExtIO> ") + linha.mid(1));
            tratarResposta(linha.mid(1));
        }
    }
}

void ExtIoDevice::tratarResposta(const QString& linha) {
    const QString l = linha.trimmed();
    const int esp = l.indexOf(' ');
    const QString verbo = (esp < 0) ? l : l.left(esp);
    const QString resto = (esp < 0) ? QString() : l.mid(esp + 1).trimmed();
    ultimaMarca_ = verbo;

    if (verbo == QStringLiteral("name")) {
        nome_ = resto;
    } else if (verbo == QStringLiteral("rates")) {
        taxas_.clear();
        for (const QString& t : resto.split(',', Qt::SkipEmptyParts)) {
            const uint32_t v = t.trimmed().toUInt();
            if (v > 0) taxas_ << v;
        }
        Logger::info(QStringLiteral("ExtIO: o aparelho aceita %1 taxas").arg(taxas_.size()));
    } else if (verbo == QStringLiteral("rate")) {
        const uint32_t v = resto.toUInt();
        if (v > 0) sps_ = v;
    } else if (verbo == QStringLiteral("freq")) {
        const quint64 v = resto.toULongLong();
        if (v > 0) freq_ = v;
    } else if (verbo == QStringLiteral("stats")) {
        Logger::info(QStringLiteral("ExtIO: fluxo - ") + resto);
    } else if (verbo == QStringLiteral("error")) {
        lastError_ = resto;
        Logger::warn(QStringLiteral("ExtIO: ") + resto);
    }
}

} // namespace masdr
