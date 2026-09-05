#include "WhisperManager.h"
#include "../util/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QUuid>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QRegularExpression>
#include <QStringList>
#include <memory>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace { constexpr double kDoisPi = 6.283185307179586; }

namespace masdr {

WhisperManager::WhisperManager(QObject* parent) : QObject(parent) {}

WhisperManager::~WhisperManager() { parar(); }

QString WhisperManager::pastaDecoders() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/decoders");
}
QString WhisperManager::caminhoServidor() {
    return pastaDecoders() + QStringLiteral("/whisper-server.exe");
}
QString WhisperManager::caminhoModelo(const QString& qual) {
    return pastaDecoders() + QStringLiteral("/whisper/ggml-%1.bin").arg(qual);
}
QString WhisperManager::caminhoVad() {
    return pastaDecoders() + QStringLiteral("/whisper/ggml-silero-v5.1.2.bin");
}
bool WhisperManager::tudoInstalado() {
    return QFileInfo::exists(caminhoServidor())
        && QFileInfo::exists(caminhoVad())
        && (QFileInfo::exists(caminhoModelo(QStringLiteral("base")))
         || QFileInfo::exists(caminhoModelo(QStringLiteral("small"))));
}

// ---------------------------------------------------------------------------
bool WhisperManager::iniciar() {
#ifndef Q_OS_WIN
    ultimoErro_ = QStringLiteral("a transcricao existe so na versao Windows - nas "
                                 "caixas ela disputaria processador com o radio");
    return false;
#else
    if (rodando_.load()) return true;
    if (!QFileInfo::exists(caminhoModelo(modelo_))) {
        ultimoErro_ = QStringLiteral("falta o modelo %1 - rode o COMPILAR_WHISPER.bat")
                          .arg(modelo_);
        Logger::warn(QStringLiteral("Whisper: ") + ultimoErro_);
        return false;
    }
    if (!tudoInstalado()) {
        ultimoErro_ = QStringLiteral("falta o whisper-server.exe ou os modelos - "
                                     "rode o COMPILAR_WHISPER.bat");
        Logger::warn(QStringLiteral("Whisper: ") + ultimoErro_);
        return false;
    }

    proc_ = std::make_unique<QProcess>();
    proc_->setProgram(caminhoServidor());
    proc_->setArguments(QStringList()
        << "-m"  << caminhoModelo(modelo_)
        << "-l"  << idioma_
        // Sem carregar texto de um trecho para o outro. Sem isto o modelo
        // entra em laco e repete a mesma frase varias vezes - aconteceu no
        // teste com o anuncio da FM, cinco repeticoes seguidas.
        << "-mc" << "0"
        << "--vad"
        << "-vm" << caminhoVad()
        // Um thread. O radio tem de continuar sendo o dono da maquina; a
        // transcricao pode demorar, ninguem esta esperando por ela.
        << "-t"  << QString::number(nucleos_)
        << "--host" << "127.0.0.1"
        << "--port" << QString::number(porta_));
    proc_->setWorkingDirectory(pastaDecoders());
    proc_->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    proc_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* a) {
            // PRIORIDADE ABAIXO DO NORMAL - e daqui que vem o fim dos estalos.
            //
            // Com prioridade igual a do radio, o Windows reveza os dois em pe
            // de igualdade, e quando a transcricao pega os nucleos a thread do
            // audio chega atrasada: o navegador fica sem amostra para tocar e
            // sai o estalo. Abaixo do normal, o whisper so usa o que sobra -
            // ele pode demorar mais, e ninguem esta esperando por ele.
            //
            // CREATE_NO_WINDOW pelo mesmo motivo da ponte ExtIO: um programa
            // de console criado por um programa de janela ganha um console
            // proprio, preto, que fica aberto e que o usuario pode fechar sem
            // saber que esta matando a transcricao.
            a->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
        });
#endif
    // Contexto explicito no connect - ver o cabecalho do HfdlManager.
    connect(proc_.get(), &QProcess::readyRead, this, [this]() {
        if (!proc_) return;
        for (const QByteArray& l : proc_->readAll().split('\n')) {
            const QString s = QString::fromUtf8(l).trimmed();
            if (!s.isEmpty()) Logger::info(QStringLiteral("Whisper: ") + s);
        }
    });

    proc_->start();
    if (!proc_->waitForStarted(8000)) {
        ultimoErro_ = QStringLiteral("o whisper-server nao subiu: ") + proc_->errorString();
        Logger::error(QStringLiteral("Whisper: ") + ultimoErro_);
        proc_.reset();
        return false;
    }

    // Carregar 466 MB leva alguns segundos. Nada e enviado antes disso, mas o
    // radio nao pode ficar parado esperando: quem espera e a thread de envio,
    // na primeira tentativa.
    idiomaAtivo_ = idioma_.toUtf8();
    fim_.store(false);
    rodando_.store(true);
    trechos_.store(0);
    falas_.store(0);
    msUltimo_.store(0);
    msAudioUltimo_.store(0);
    ultimoErro_.clear();
    { std::lock_guard<std::mutex> t(mutexJanela_);
      janela_.clear(); hpEnt_ = hpSai_ = lp_ = anterior_ = 0.0f;
      blocoSoma_ = 0.0; blocoN_ = 0; fundo_ = 0.0f; fundoOk_ = false;
      msSilencio_ = 0; houveFala_ = false; }
    { std::lock_guard<std::mutex> t(mutexFila_);
      fila_.clear(); parcial_.clear(); marcaParcial_ = 0; }
    enviador_ = std::thread(&WhisperManager::threadEnvio, this);
    Logger::info(QStringLiteral("Whisper: servidor no ar em 127.0.0.1:%1, idioma %2")
                     .arg(porta_).arg(idioma_));
    emit logLine(QStringLiteral("[WHISPER] carregando o modelo, aguarde alguns segundos..."));
    return true;
#endif
}

void WhisperManager::parar() {
    if (!rodando_.load() && !proc_) return;
    rodando_.store(false);
    fim_.store(true);
    if (enviador_.joinable()) enviador_.join();
    if (proc_) {
        proc_->terminate();
        if (!proc_->waitForFinished(3000)) {
            proc_->kill();
            proc_->waitForFinished(1000);
        }
        proc_.reset();
    }
    Logger::info(QStringLiteral("Whisper: parado"));
}

// ---------------------------------------------------------------------------
//  caminho quente - chamado pela thread do dispositivo
// ---------------------------------------------------------------------------
void WhisperManager::feedAudio(const int16_t* pcm, size_t n, uint32_t sps) {
    if (!rodando_.load() || !pcm || n == 0 || sps == 0) return;

    std::lock_guard<std::mutex> t(mutexJanela_);
    if (sps != spsEntrada_) {
        spsEntrada_ = sps;
        passo_ = double(sps) / double(kTaxa);
        fase_ = 0.0;
        Logger::info(QStringLiteral("Whisper: reamostrando de %1 para 16000 Hz").arg(sps));
    }

    // Reamostragem simples com um passa-baixa de primeira ordem antes.
    //
    // Decimar sem filtrar dobraria as frequencias acima de 8 kHz para dentro
    // da banda de voz, e o modelo receberia chiado onde deveria haver
    // silencio. Um polo so ja basta: a fala interessa ate 3,4 kHz, e o que
    // vem acima de 8 kHz num audio de radio e ruido.
    const float a = float(1.0 / (1.0 + double(sps) / (kDoisPi * 3400.0)));

    // Passa-alta de 150 Hz ANTES do passa-baixa.
    //
    // Em SSB o que sai do demodulador vem com nivel continuo e ronco grave.
    // Isso nao e voz, mas conta como amplitude - e era o ronco, e nao a fala,
    // que definia o pico usado para acertar o volume. Cortado aqui, o que
    // sobra para medir e so a voz.
    const double dt   = 1.0 / double(sps);
    const double rcHp = 1.0 / (kDoisPi * 150.0);
    const float  ah   = float(rcHp / (rcHp + dt));

    for (size_t i = 0; i < n; ++i) {
        const float x = float(pcm[i]);
        hpSai_ = ah * (hpSai_ + x - hpEnt_);
        hpEnt_ = x;
        lp_ += a * (hpSai_ - lp_);
        while (fase_ < 1.0) {
            const float v = anterior_ + float(fase_) * (lp_ - anterior_);
            int s = int(std::lround(v));
            if (s >  32767) s =  32767;
            if (s < -32768) s = -32768;
            janela_.push_back(int16_t(s));

            // Acompanha o piso de ruido em blocos de 20 ms para saber quando
            // a fala parou. Piso desce depressa, sobe devagar: assim ele se
            // instala no ruido de fundo e nao acompanha a voz.
            blocoSoma_ += double(s) * double(s);
            if (++blocoN_ >= 320) {
                const float rms = float(std::sqrt(blocoSoma_ / double(blocoN_)));
                blocoSoma_ = 0.0; blocoN_ = 0;
                if (!fundoOk_)        { fundo_ = rms; fundoOk_ = true; }
                else if (rms < fundo_) fundo_ += 0.25f   * (rms - fundo_);
                else                   fundo_ += 0.0008f * (rms - fundo_);
                // O +30 e para o caso do silencio digital, com piso quase
                // zero: sem ele, qualquer coisa seria "bem acima do piso".
                if (rms > fundo_ * 2.2f + 30.0f) { houveFala_ = true; msSilencio_ = 0; }
                else                              msSilencio_ += 20;
            }
            fase_ += passo_;
        }
        fase_ -= 1.0;
        anterior_ = lp_;
    }

    // ---- resultado parcial: a linha crescendo enquanto a pessoa fala ----
    //
    // E ISTO QUE FAZ A TRANSCRICAO PARECER AO VIVO. Sem isto, nada aparece
    // ate a frase acabar; com isto, a linha vai se formando na tela a cada
    // segundo e pouco e so se fecha na pausa.
    //
    // Cada parcial refaz o trecho inteiro desde o comeco da fala - o whisper
    // nao sabe continuar de onde parou, ele so sabe transcrever um audio do
    // inicio ao fim. Por isso a palavra do meio as vezes muda enquanto o
    // contexto cresce; e o mesmo que acontece na legenda ao vivo da televisao.
    //
    // So se produz parcial com a fila de trechos definitivos vazia: quem tem
    // de chegar a tela sem falta e o texto fechado.
    if (houveFala_ && janela_.size() >= marcaParcial_ + kPassoParcial) {
        marcaParcial_ = janela_.size();
        std::lock_guard<std::mutex> tf(mutexFila_);
        if (fila_.empty()) parcial_.assign(janela_.begin(), janela_.end());
    }

    // DOIS MOTIVOS PARA FECHAR A JANELA, e o primeiro e o que importa.
    //
    // 1) A pessoa parou de falar. E o corte natural: a frase acabou, nao ha
    //    o que esperar, e o texto pode sair ja. Um "cambio" de dois segundos
    //    vira texto em pouco mais de dois segundos, e nao nos 12 do relogio.
    // 2) O teto escolhido no painel. Vale para quem fala sem parar - a fila
    //    nao pode crescer para sempre esperando uma pausa que nao vem.
    const size_t alvo = size_t(kTaxa) * size_t(janelaSeg_.load());
    bool porPausa = false;
    if (janela_.size() < alvo) {
        if (!(houveFala_ && msSilencio_ >= kPausaMs && janela_.size() >= kJanelaMinima))
            return;
        porPausa = true;
    }

    // Fecha a janela e guarda o rabo para a proxima.
    //
    // Cortando no teto, a sobreposicao de 1 s existe para nao partir palavra
    // na emenda. Cortando na pausa nao ha palavra partida - ali a sobreposicao
    // so faria o inicio da proxima repetir o fim desta.
    std::vector<int16_t> trecho(janela_.begin(), janela_.end());
    const size_t manter = std::min(porPausa ? kSobreposicaoPausa : kSobreposicao,
                                   janela_.size());
    janela_.erase(janela_.begin(), janela_.end() - manter);
    houveFala_ = false;
    msSilencio_ = 0;
    marcaParcial_ = 0;

    {
        std::lock_guard<std::mutex> tf(mutexFila_);
        if (fila_.size() >= kTetoFila) {
            // A maquina nao acompanha. Descarta o MAIS ANTIGO: o que interessa
            // e o que esta no ar agora, e uma fila crescendo sem limite so
            // adiaria o texto ate ele nao servir mais para nada.
            fila_.pop_front();
            // Uma vez a cada 10 s, nao a cada descarte: a tela ficou coberta
            // de "fila cheia" e o proprio aviso escondeu a transcricao.
            const qint64 agora = QDateTime::currentMSecsSinceEpoch();
            if (agora - ultimoAvisoFila_ > 10000) {
                ultimoAvisoFila_ = agora;
                emit logLine(QStringLiteral("[WHISPER] a maquina nao esta acompanhando - "
                                            "trechos estao sendo descartados"));
            }
        }
        fila_.push_back(std::move(trecho));
    }
}

// ---------------------------------------------------------------------------
QByteArray WhisperManager::montarWav(const std::vector<int16_t>& a, uint32_t sps) {
    QByteArray h;
    auto u32 = [&h](uint32_t v){ h.append(char(v&0xFF)); h.append(char((v>>8)&0xFF));
                                 h.append(char((v>>16)&0xFF)); h.append(char((v>>24)&0xFF)); };
    auto u16 = [&h](uint16_t v){ h.append(char(v&0xFF)); h.append(char((v>>8)&0xFF)); };
    const uint32_t dados = uint32_t(a.size() * 2);
    h.append("RIFF", 4); u32(36 + dados); h.append("WAVE", 4);
    h.append("fmt ", 4); u32(16); u16(1); u16(1);
    u32(sps); u32(sps * 2); u16(2); u16(16);
    h.append("data", 4); u32(dados);
    h.append(reinterpret_cast<const char*>(a.data()), int(dados));
    return h;
}

// Descarta o que nao e fala.
//
// Diante de chiado o whisper nao inventa frase - ele devolve uma etiqueta
// entre colchetes ou parenteses, como [MUSICA DE FUNDO] ou (musica). Foi o
// que aconteceu nos cinco primeiros arquivos de teste, todos so ruido. Essas
// linhas nao tem por que aparecer na tela de ninguem.
bool WhisperManager::soEtiqueta(const QString& t) {
    const QString s = t.trimmed();
    if (s.isEmpty()) return true;
    QString semTags = s;
    semTags.remove(QRegularExpression(QStringLiteral("[\\[\\(][^\\]\\)]*[\\]\\)]")));
    return semTags.trimmed().isEmpty();
}

// Leva a fala sempre ao mesmo volume.
//
// E O AJUSTE QUE MAIS PESA EM SSB. O modelo foi treinado com voz gravada em
// nivel de estudio; audio fraco nao e so "mais baixo" para ele, e outra coisa,
// e ele passa a chutar. E em SSB o nivel varia o tempo todo: nao ha portadora,
// entao cada estacao chega num volume, e o mesmo operador varia conforme o
// desvanecimento. Nao adianta o dono do radio "acertar o volume" uma vez.
//
// Aqui o pico e medido no percentil 98, e nao no maximo: um estalo isolado
// - e SSB e cheio deles - definiria sozinho o maximo e derrubaria o ganho da
// fala inteira. Com o percentil, o estalo fica de fora da conta e depois
// satura, que e o certo.
//
// O alvo e 0,6 da escala cheia, com folga para o pico verdadeiro nao bater no
// teto. O ganho e limitado a 24 vezes: acima disso nao ha fala para levantar,
// so chiado, e amplificar chiado ate a escala cheia e o caminho mais curto
// para o modelo inventar frase.
double WhisperManager::normalizarNivel(std::vector<int16_t>& a) {
    if (a.size() < 1000) return 1.0;

    std::vector<uint16_t> mod;
    mod.reserve(a.size());
    for (int16_t v : a) mod.push_back(uint16_t(v < 0 ? -int32_t(v) : int32_t(v)));

    const size_t k = size_t(double(mod.size()) * 0.98);
    std::nth_element(mod.begin(), mod.begin() + k, mod.end());
    const double pico = double(mod[k]);
    if (pico < 8.0) return 1.0;                 // silencio de verdade

    double g = 19660.0 / pico;                  // 0,6 da escala cheia
    if (g > 24.0)  g = 24.0;
    if (g < 0.25)  g = 0.25;
    if (g > 0.9 && g < 1.1) return 1.0;         // ja esta bom, nao mexe

    for (int16_t& v : a) {
        int s = int(std::lround(double(v) * g));
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        v = int16_t(s);
    }
    return g;
}

// Corta o laco de palavra repetida.
//
// E O DEFEITO CLASSICO DO WHISPER, e apareceu em 9.440 kHz: "que e que e que
// e" por dez linhas seguidas. Diante de audio dificil o modelo entra num ciclo
// e so sai dele no limite de fichas - por isso aquele trecho levou 4,6 s
// enquanto os bons levavam 0,2 s. O laco nao suja so a tela: e ele que enche
// a fila.
//
// A cura oficial e a retentativa por temperatura, mas ela custa ate seis
// decodificacoes por trecho - caro demais para transcricao ao vivo, e foi
// justamente o que se tirou. Entao o laco e cortado aqui, de graca: procura-se
// um grupo de ate 6 palavras que se repita tres vezes ou mais em seguida e
// guardam-se DUAS. Duas, e nao uma, porque "nao, nao" e "muito, muito" sao
// repeticoes legitimas da fala.
QString WhisperManager::podarRepeticao(const QString& t) {
    const QStringList p = t.split(QRegularExpression(QStringLiteral("\\s+")),
                                  Qt::SkipEmptyParts);
    if (p.size() < 6) return t;

    QStringList saida;
    int i = 0;
    while (i < p.size()) {
        int melhorN = 0, melhorRep = 0;
        for (int n = 1; n <= 6 && i + 2 * n <= p.size(); ++n) {
            int rep = 1;
            while (i + (rep + 1) * n <= p.size()) {
                bool igual = true;
                for (int k = 0; k < n; ++k) {
                    if (p[i + k].compare(p[i + rep * n + k], Qt::CaseInsensitive) != 0) {
                        igual = false; break;
                    }
                }
                if (!igual) break;
                ++rep;
            }
            // Fica com o ciclo que cobre mais palavras: um laco de duas
            // palavras nao pode ser lido como dois lacos de uma.
            if (rep >= 3 && rep * n > melhorRep * melhorN) { melhorN = n; melhorRep = rep; }
        }
        if (melhorN > 0) {
            for (int k = 0; k < melhorN * 2; ++k) saida << p[i + k];
            i += melhorN * melhorRep;
        } else {
            saida << p[i];
            ++i;
        }
    }
    return saida.join(QLatin1Char(' '));
}

QString WhisperManager::pedirTranscricao(const std::vector<int16_t>& amostras) {
    // Copia porque o trecho e normalizado antes de sair - o original nao muda.
    std::vector<int16_t> pcm = amostras;
    normalizarNivel(pcm);
    const QByteArray wav = montarWav(pcm, kTaxa);
    // toByteArray() ja devolve QByteArray - o toLatin1() sobrava e nao existe.
    const QByteArray lim = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);

    QByteArray corpo;
    corpo += "--" + lim + "\r\n";
    corpo += "Content-Disposition: form-data; name=\"file\"; filename=\"a.wav\"\r\n";
    corpo += "Content-Type: audio/wav\r\n\r\n";
    corpo += wav;
    auto campo = [&corpo, &lim](const char* nome, const char* valor) {
        corpo += "\r\n--" + QByteArray(lim) + "\r\n";
        corpo += "Content-Disposition: form-data; name=\"";
        corpo += nome;
        corpo += "\"\r\n\r\n";
        corpo += valor;
    };
    campo("response_format", "text");
    // IDIOMA NA REQUISICAO, e nao so no -l da linha de comando.
    //
    // Mesma armadilha do max_context: o servidor le os parametros de cada
    // pedido. Se ele estava caindo no padrao, o modelo tentava adivinhar o
    // idioma a cada trecho - e num trecho ruim de SSB ele adivinha errado e
    // devolve texto em outra lingua. Mandando aqui, a duvida acaba.
    campo("language", idiomaAtivo_.constData());
    // max_context=0 AQUI, e nao so na linha de comando do servidor.
    //
    // O whisper-server le os parametros de CADA requisicao e sobrescreve o que
    // foi passado na partida. Por isso o -mc 0 nao valia nada: o modelo
    // continuava carregando o texto de um trecho para o outro e entrava em
    // laco, repetindo a mesma frase tres, quatro vezes seguidas. Foi
    // exatamente o que apareceu na escuta de 15.140.
    campo("max_context", "0");
    campo("no_timestamps", "true");
    // Guardas contra texto inventado.
    //
    // Diante de musica ou chiado o modelo as vezes produz frases fluentes em
    // varios idiomas, com toda a confianca - foi o que encheu a tela de
    // "ecstasyfikhe, ev Chriem Jewshk startups". Estes tres limiares mandam
    // ele descartar o que saiu com pouca certeza. Conferido no audio de
    // 15.090: o texto bom continua saindo igual.
    campo("no_speech_thold", "0.6");
    campo("logprob_thold",  "-1.0");
    campo("entropy_thold",  "2.4");

    // SEM RETENTATIVA POR TEMPERATURA - e daqui que vinha a lentidao.
    //
    // Quando o resultado nao passa nos limiares acima, o whisper NAO desiste:
    // ele decodifica tudo de novo com temperatura maior, ate seis vezes. Com
    // os limiares apertados que eu tinha posto (-0,6 e 2,2, contra -1,0 e 2,4
    // do padrao), quase todo trecho reprovava - e um trecho de 5 s passava a
    // custar seis decodificacoes. Foi o que encheu a fila em 101,7 MHz mesmo
    // com a maquina em 14 por cento.
    //
    // Numa transcricao ao vivo a retentativa nao paga: o audio nao espera. Uma
    // passada so, e o que sair, saiu.
    campo("temperature", "0.0");
    campo("temperature_inc", "0.0");
    // Teto de fichas por segmento: e o freio do laco na origem. Cinco
    // segundos de fala cabem em umas 25 fichas; 96 e folga larga para
    // qualquer frase de verdade, e curto o bastante para o ciclo nao
    // consumir o trecho inteiro.
    campo("max_tokens", "96");

    // audio_ctx PROPORCIONAL A JANELA - e isto que torna janela curta viavel.
    //
    // O whisper nao processa "os segundos que voce mandou": ele completa tudo
    // ate 30 segundos com silencio e roda o codificador em cima disso. Por
    // isso uma janela de 5 s custava quase o mesmo que uma de 20 - e, como sao
    // quatro vezes mais chamadas, o trabalho total quadruplicava. Foi o que
    // encheu a fila quando o atraso foi para 5 s.
    //
    // O audio_ctx encurta o contexto do codificador na mesma proporcao. E o
    // que o exemplo de tempo real do proprio whisper.cpp faz. Custa alguma
    // precisao nas bordas; em troca, a janela curta passa a caber no tempo.
    // O +2 e uma folga para a sobreposicao nao ficar sem contexto.
    // Medido no trecho que esta indo, e nao no teto do painel: com o corte
    // por pausa a maioria dos trechos e bem menor que o teto, e pagar
    // contexto de 12 s para transcrever 2 s seria jogar processador fora.
    const double segReais = double(amostras.size()) / double(kTaxa);
    //
    // O piso e 768, e nao 256: abaixo disso o codificador perde contexto
    // demais, o texto sai ruim e - com os limiares - reprovava, o que
    // disparava justamente as retentativas que se quer evitar. 768 e o valor
    // que o proprio whisper.cpp recomenda para ganhar velocidade sem estragar
    // o resultado.
    const int ctx = std::min(1500, std::max(768,
        int(std::lround(1500.0 * (segReais + 2.0) / 30.0))));
    campo("audio_ctx", QByteArray::number(ctx).constData());
    corpo += "\r\n--" + lim + "--\r\n";

    QByteArray cab;
    cab += "POST /inference HTTP/1.1\r\n";
    cab += "Host: 127.0.0.1\r\n";
    cab += "Content-Type: multipart/form-data; boundary=" + lim + "\r\n";
    cab += "Content-Length: " + QByteArray::number(corpo.size()) + "\r\n";
    cab += "Connection: close\r\n\r\n";

    // Socket com espera sincrona, sem laco de eventos: estamos numa thread
    // nossa, e bloquear aqui nao segura ninguem.
    QTcpSocket s;
    s.connectToHost(QStringLiteral("127.0.0.1"), porta_);
    if (!s.waitForConnected(5000)) return QString();
    s.write(cab); s.write(corpo);
    if (!s.waitForBytesWritten(15000)) return QString();

    // ESPERA EM FATIAS, E NAO NUM BLOCO SO.
    //
    // Antes era um waitForReadyRead(180000): a thread ficava presa ate tres
    // minutos, e o parar() - que a espera para poder juntar a thread - travava
    // junto. Como o parar() e chamado pela thread do servidor HTTP, o RADIO
    // INTEIRO congelava ao fechar a janela: sem audio, sem cachoeira, so
    // voltando com desliga e liga. Foi exatamente o que aconteceu.
    //
    // Em fatias de 300 ms o fim_ e visto quase na hora, e o tempo total
    // continua generoso: um trecho cheio de fala demora mesmo.
    QByteArray resp;
    const qint64 limite = QDateTime::currentMSecsSinceEpoch() + 180000;
    while (!fim_.load() && QDateTime::currentMSecsSinceEpoch() < limite) {
        if (s.waitForReadyRead(300)) {
            resp += s.readAll();
            if (s.state() != QAbstractSocket::ConnectedState) break;
        } else if (s.state() != QAbstractSocket::ConnectedState) {
            break;
        }
    }
    resp += s.readAll();
    s.abort();

    const int corte = resp.indexOf("\r\n\r\n");
    if (corte < 0) return QString();
    return QString::fromUtf8(resp.mid(corte + 4)).trimmed();
}

void WhisperManager::threadEnvio() {
    while (!fim_.load()) {
        std::vector<int16_t> trecho;
        bool ehParcial = false;
        {
            std::lock_guard<std::mutex> t(mutexFila_);
            // Definitivo primeiro, sempre: parcial e enfeite, definitivo e o
            // que fica no registro.
            if (!fila_.empty()) { trecho = std::move(fila_.front()); fila_.pop_front(); }
            else if (!parcial_.empty()) { trecho.swap(parcial_); ehParcial = true; }
        }
        if (trecho.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            continue;
        }

        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        const size_t nAmostras = trecho.size();
        const QString texto = pedirTranscricao(trecho);

        if (ehParcial) {
            // Tudo numa linha so: e uma frase em formacao, e quebra-la em
            // varias faria a tela pular a cada atualizacao.
            QString junto;
            for (const QString& linha : texto.split('\n')) {
                const QString l = podarRepeticao(linha.trimmed());
                if (l.isEmpty() || soEtiqueta(l)) continue;
                if (!junto.isEmpty()) junto += QLatin1Char(' ');
                junto += l;
            }
            if (!junto.isEmpty()) emit transcricaoParcial(junto);
            continue;
        }

        trechos_.fetch_add(1);
        msUltimo_.store(int(QDateTime::currentMSecsSinceEpoch() - t0));
        msAudioUltimo_.store(int(nAmostras * 1000 / kTaxa));
        if (texto.isEmpty()) { emit fimDeFala(); continue; }

        for (const QString& linha : texto.split('\n')) {
            const QString l = podarRepeticao(linha.trimmed());
            if (l.isEmpty() || soEtiqueta(l)) continue;
            // Rede de seguranca contra repeticao.
            //
            // O max_context resolve na raiz, mas o modelo ainda pode repetir
            // dentro de um mesmo trecho, e a sobreposicao de 1 segundo entre
            // janelas devolve o fim de uma no comeco da seguinte. Frase igual
            // a anterior nao acrescenta nada a quem le.
            if (l == ultimaFala_) continue;
            ultimaFala_ = l;
            falas_.fetch_add(1);
            emit transcricao(l);
        }
        // Fecha a linha que estava crescendo na tela, mesmo que este trecho
        // nao tenha rendido texto novo - senao o proximo parcial apagaria por
        // cima do que ja estava escrito.
        emit fimDeFala();
    }
}

QJsonObject WhisperManager::statusJson() const {
    QJsonObject o;
    o["rodando"]   = rodando_.load();
    o["instalado"] = tudoInstalado();
    o["idioma"]    = idioma_;
    o["nucleos"]   = nucleos_;
    o["modelo"]    = modelo_;
    o["janelaSeg"] = janelaSeg_.load();
    o["temBase"]   = QFileInfo::exists(caminhoModelo(QStringLiteral("base")));
    o["temSmall"]  = QFileInfo::exists(caminhoModelo(QStringLiteral("small")));
    o["trechos"]   = trechos_.load();
    o["falas"]     = falas_.load();
    o["msUltimo"]      = msUltimo_.load();
    o["msAudioUltimo"] = msAudioUltimo_.load();
    {
        std::lock_guard<std::mutex> t(const_cast<std::mutex&>(mutexFila_));
        o["fila"] = int(fila_.size());
    }
    if (!ultimoErro_.isEmpty()) o["erro"] = ultimoErro_;
    return o;
}

} // namespace masdr
