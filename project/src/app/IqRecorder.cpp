#include "IqRecorder.h"
#include "../util/Logger.h"
#include "../util/Caminhos.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QFileInfo>

#include <chrono>

#include <algorithm>
#include <cmath>

namespace masdr {

namespace {
qint64 agoraMs() { return QDateTime::currentMSecsSinceEpoch(); }
}

IqRecorder::IqRecorder() {
    escritor_ = std::thread(&IqRecorder::threadEscrita, this);
    // Dizer o destino ANTES de qualquer tentativa. Se algum dia os arquivos
    // "sumirem", a primeira pergunta e onde eles deveriam estar - e a resposta
    // tem de estar no log antes do problema, nao depois.
    Logger::info(QStringLiteral("IQ: destino das gravacoes: ") + pastaDestino());
}

IqRecorder::~IqRecorder() {
    parar();
    fim_.store(true);
    if (escritor_.joinable()) escritor_.join();
}

QString IqRecorder::pastaDestino() {
    // Ver o cabecalho de Caminhos.h: nao basta perguntar ao Qt.
    return areaDeTrabalho();
}

// Nome valido e SO o que nos mesmos geramos.
//
// Esta funcao decide o que o mundo de fora pode pedir para baixar ou apagar.
// Aceitar um nome qualquer seria abrir a maquina inteira: bastaria pedir
// "../../etc/passwd". Por isso nao se filtra o que e proibido - permite-se
// apenas o que casa exatamente com o padrao dos nossos arquivos.
static bool nomeNosso(const QString& nome) {
    static const QRegularExpression re(
        QStringLiteral("^SDRSharp_\\d{8}_\\d{6}Z_\\d+Hz_IQ\\.wav(\\.json)?$"));
    return re.match(nome).hasMatch();
}

QJsonArray IqRecorder::listarArquivos() {
    QJsonArray a;
    QDir d(pastaDestino());
    const auto lista = d.entryInfoList(QStringList() << "SDRSharp_*_IQ.wav",
                                       QDir::Files, QDir::Time);
    for (const QFileInfo& fi : lista) {
        if (!nomeNosso(fi.fileName())) continue;
        QJsonObject o;
        o["nome"]  = fi.fileName();
        o["bytes"] = double(fi.size());
        o["quando"] = fi.lastModified().toString(QStringLiteral("dd/MM HH:mm"));
        o["json"]  = QFileInfo(fi.absoluteFilePath() + ".json").exists();
        a.append(o);
    }
    return a;
}

QString IqRecorder::caminhoDe(const QString& nome) {
    if (!nomeNosso(nome)) return QString();
    const QString c = QDir(pastaDestino()).filePath(nome);
    return QFileInfo::exists(c) ? c : QString();
}

bool IqRecorder::apagar(const QString& nome) {
    const QString c = caminhoDe(nome);
    if (c.isEmpty()) return false;
    QFile::remove(c + QStringLiteral(".json"));   // o sidecar vai junto
    return QFile::remove(c);
}

void IqRecorder::configurar(uint32_t sampleRate, uint64_t centroHz, int ganhoDecimos,
                            int ppm, const QString& tipoDispositivo) {
    std::lock_guard<std::mutex> t(mutex_);
    sps_ = sampleRate;
    centro_ = centroHz;
    ganhoDecimos_ = ganhoDecimos;
    ppm_ = ppm;
    tipoDispositivo_ = tipoDispositivo;

    preTeto_ = qint64(preSegundos_) * bytesPorSegundo();
    // Teto de memoria: a 2,4 MSPS em CU8 sao 4,8 MB por segundo, e 20 s dao
    // 96 MB. Aceitavel. Mas em 16 bits a 2,4 MSPS seriam 384 MB, e ai a
    // pre-gravacao comeria a maquina. Encurta o tempo em vez de estourar.
    const qint64 kTetoPreMb = 192;
    if (preTeto_ > kTetoPreMb * 1024 * 1024) preTeto_ = kTetoPreMb * 1024 * 1024;
}

qint64 IqRecorder::bytesPorSegundo() const {
    return qint64(sps_) * 4;   // I e Q, 16 bits cada
}

void IqRecorder::armar(bool preGravacao, int preSegundos,
                       bool gatilho, double limiarDb, int silencioSegundos) {
    std::lock_guard<std::mutex> t(mutex_);
    preLigada_ = preGravacao;
    if (preSegundos > 0) preSegundos_ = std::min(preSegundos, 120);
    gatilhoArmado_ = gatilho;
    limiarDb_ = limiarDb;
    if (silencioSegundos > 0) silencioSegundos_ = silencioSegundos;
    preTeto_ = qint64(preSegundos_) * bytesPorSegundo();
    const qint64 kTetoPreMb = 192;
    if (preTeto_ > kTetoPreMb * 1024 * 1024) preTeto_ = kTetoPreMb * 1024 * 1024;
    if (!preLigada_) { pre_.clear(); preBytes_ = 0; }
    Logger::info(QStringLiteral("IQ: pre-gravacao %1 (%2 s), disparo %3 (%4 dB, %5 s de silencio)")
                     .arg(preLigada_ ? "ligada" : "desligada")
                     .arg(preSegundos_)
                     .arg(gatilhoArmado_ ? "ligado" : "desligado")
                     .arg(limiarDb_).arg(silencioSegundos_));
}

// ---------------------------------------------------------------------------
//  conversao
// ---------------------------------------------------------------------------
void IqRecorder::converter(const std::complex<float>* iq, size_t n, QByteArray& saida) const {
    saida.resize(int(n * 4));
    auto* p = reinterpret_cast<short*>(saida.data());
    for (size_t i = 0; i < n; ++i) {
        p[2*i]   = (short)std::clamp(int(std::lround(iq[i].real() * 32767.0f)), -32768, 32767);
        p[2*i+1] = (short)std::clamp(int(std::lround(iq[i].imag() * 32767.0f)), -32768, 32767);
    }
}

// ---------------------------------------------------------------------------
//  o caminho quente: chamado pela thread do dispositivo
// ---------------------------------------------------------------------------
void IqRecorder::feed(const std::complex<float>* iq, size_t n) {
    if (!iq || n == 0 || sps_ == 0) return;
    const bool grav = gravando_.load();

    QByteArray bloco;
    {
        // A conversao e feita FORA da trava sempre que possivel, mas ela le
        // formato_, que so muda em configurar(). Um bloco convertido com o
        // formato antigo, no instante da troca, sairia com o tamanho errado;
        // por isso a trava curta aqui e nao depois.
        std::lock_guard<std::mutex> t(mutex_);
        if (!grav && !preLigada_) return;
        converter(iq, n, bloco);

        if (!grav) {
            // So alimentando a pre-gravacao.
            pre_.push_back(bloco);
            preBytes_ += bloco.size();
            while (preBytes_ > preTeto_ && !pre_.empty()) {
                preBytes_ -= pre_.front().size();
                pre_.pop_front();
            }
            return;
        }
        // Gravando: a pre-gravacao ja foi despejada no arquivo, nao acumula.
    }

    {
        std::lock_guard<std::mutex> t(mutex_);
        qint64 naFila = 0;
        for (const auto& b : fila_) naFila += b.size();
        if (naFila + bloco.size() > tetoFilaBytes_) {
            // Disco nao acompanha. Descartar e ruim, mas gravar um arquivo com
            // buraco silencioso e pior: quem analisa depois nao tem como saber
            // que faltou pedaco. Aqui isso e CONTADO e vai para a tela.
            overruns_.fetch_add(1);
            return;
        }
        fila_.push_back(std::move(bloco));
    }
}

void IqRecorder::nivel(double peakDb) {
    if (!gatilhoArmado_) return;
    const qint64 t = agoraMs();
    if (peakDb >= limiarDb_) {
        ultimoAcimaMs_.store(t);
        if (!gravando_.load() && !pedeIniciar_.load()) {
            Logger::info(QStringLiteral("IQ: disparo por nivel (%1 dB acima de %2)")
                             .arg(peakDb, 0, 'f', 1).arg(limiarDb_, 0, 'f', 1));
            pedeIniciar_.store(true);      // ver o comentario no cabecalho
        }
        return;
    }
    if (gravando_.load() && !pedeParar_.load()) {
        const qint64 desde = ultimoAcimaMs_.load();
        if (desde > 0 && (t - desde) > qint64(silencioSegundos_) * 1000) {
            Logger::info(QStringLiteral("IQ: %1 s de silencio, encerrando").arg(silencioSegundos_));
            pedeParar_.store(true);
        }
    }
}

// ---------------------------------------------------------------------------
//  arquivos
// ---------------------------------------------------------------------------
bool IqRecorder::abrirArquivo() {
    const QString pasta = pastaDestino();
    QDir().mkpath(pasta);

    // Nome no padrao do SDR#, e nao por gosto.
    //
    // O SDR# e o SDR++ LEEM a frequencia central deste nome ao abrir o
    // arquivo, e ja mostram a escala de frequencia certa. Com um nome proprio
    // eles abririam o sinal como se estivesse em zero, e caberia a voce
    // lembrar em que frequencia gravou. O sidecar .json continua guardando
    // tudo com nome de gente.
    const QString quando = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmmss");
    // O "Hz" no fim do numero NAO e enfeite: e o que o analisador procura.
    //
    // O padrao e SDRSharp_<data>_<hora>Z_<frequencia>Hz_IQ.wav. Sem o sufixo o
    // SDR# nao reconhece o campo e abre o arquivo como se estivesse em zero -
    // ou seja, eu tinha escolhido este nome exatamente para ganhar a escala de
    // frequencia certa e depois esqueci a letra que faz isso funcionar.
    caminhoAtual_ = QStringLiteral("%1/SDRSharp_%2Z_%3Hz_IQ.wav")
                        .arg(pasta, quando).arg(centro_);

    arquivo_.setFileName(caminhoAtual_);
    if (!arquivo_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        ultimoErro_ = QStringLiteral("nao consegui criar ") + caminhoAtual_;
        Logger::error(QStringLiteral("IQ: ") + ultimoErro_);
        return false;
    }
    gravarCabecalhoWav();
    inicioBlocoMs_ = agoraMs();
    bytesNoBloco_ = 0;
    escreverSidecar(caminhoAtual_);
    Logger::info(QStringLiteral("IQ: gravando em ") + caminhoAtual_);
    return true;
}

void IqRecorder::fecharArquivo() {
    if (!arquivo_.isOpen()) return;
    corrigirCabecalhoWav();
    arquivo_.close();
    Logger::info(QStringLiteral("IQ: fechado %1 (%2 MB)")
                     .arg(caminhoAtual_).arg(bytesNoBloco_ / (1024.0*1024.0), 0, 'f', 1));
}

// WAV IQ de 16 bits: dois canais, esquerda = I, direita = Q. E o que o SDR#
// e o SDR++ esperam ao abrir um arquivo de IQ.
void IqRecorder::gravarCabecalhoWav() {
    const uint32_t taxa = sps_;
    const uint16_t canais = 2, bits = 16;
    const uint32_t bytesPorSeg = taxa * canais * bits / 8;
    const uint16_t alinhamento = canais * bits / 8;
    QByteArray h;
    auto u32 = [&h](uint32_t v){ h.append(char(v&0xFF)); h.append(char((v>>8)&0xFF));
                                 h.append(char((v>>16)&0xFF)); h.append(char((v>>24)&0xFF)); };
    auto u16 = [&h](uint16_t v){ h.append(char(v&0xFF)); h.append(char((v>>8)&0xFF)); };
    h.append("RIFF", 4); u32(0);           // tamanho corrigido no fim
    h.append("WAVE", 4);
    h.append("fmt ", 4); u32(16); u16(1); u16(canais);
    u32(taxa); u32(bytesPorSeg); u16(alinhamento); u16(bits);
    h.append("data", 4); u32(0);           // idem
    arquivo_.write(h);
}

void IqRecorder::corrigirCabecalhoWav() {
    // Os dois tamanhos so podem ser escritos no fim, quando se sabe quanto
    // entrou. Sem isto o arquivo abre como se tivesse zero segundos.
    const qint64 dados = arquivo_.size() - 44;
    if (dados < 0) return;
    auto por = [this](qint64 pos, uint32_t v) {
        arquivo_.seek(pos);
        char b[4] = { char(v&0xFF), char((v>>8)&0xFF), char((v>>16)&0xFF), char((v>>24)&0xFF) };
        arquivo_.write(b, 4);
    };
    por(4,  uint32_t(dados + 36));
    por(40, uint32_t(dados));
}

void IqRecorder::escreverSidecar(const QString& caminhoBase) const {
    QJsonObject o;
    o["arquivo"]      = QFileInfo(caminhoBase).fileName();
    o["formato"]      = "wav_s16_iq";
    o["sampleRate"]   = double(sps_);
    // A frequencia central REAL do sintonizador, e nao o VFO da tela.
    //
    // O RXSDR desloca o oscilador de proposito, para o vazamento dele nao cair
    // em cima do sinal. Quem abrisse o arquivo confiando no numero do VFO
    // procuraria o sinal no lugar errado e concluiria que a gravacao falhou.
    o["centroHz"]     = double(centro_);
    o["ganhoDb"]      = ganhoDecimos_ / 10.0;
    o["ppm"]          = ppm_;
    o["dispositivo"]  = tipoDispositivo_;
    o["utc"]          = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    o["programa"]     = QStringLiteral("RXSDR");
    QFile f(caminhoBase + QStringLiteral(".json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.close();
    }
}

// ---------------------------------------------------------------------------
bool IqRecorder::iniciar() {
    Logger::info(QStringLiteral("IQ: pedido de inicio (taxa=%1, centro=%2)")
                     .arg(sps_).arg(centro_));
    if (gravando_.load()) return true;
    if (sps_ == 0) {
        // Esta recusa era CALADA, e e a mais provavel de todas: sem IQ
        // chegando, o gravador nunca soube a taxa nem o centro, e sem isso
        // nao ha nome de arquivo nem formato a escolher. Acontece com o radio
        // desligado - e do lado de fora parece que a gravacao funcionou e os
        // arquivos sumiram.
        ultimoErro_ = QStringLiteral("o radio nao esta entregando IQ - ligue o radio antes");
        Logger::warn(QStringLiteral("IQ: ") + ultimoErro_);
        return false;
    }

    const QStorageInfo st(pastaDestino());
    if (st.isValid() && (st.bytesAvailable() / (1024*1024)) < minimoLivreMb_) {
        ultimoErro_ = QStringLiteral("espaco livre abaixo de %1 MB - nao comecei")
                          .arg(minimoLivreMb_);
        Logger::warn(QStringLiteral("IQ: ") + ultimoErro_);
        return false;
    }

    std::lock_guard<std::mutex> t(mutex_);
    if (!abrirArquivo()) return false;
    ultimoErro_.clear();
    overruns_.store(0);
    bytesGravados_.store(0);
    blocos_.store(1);
    inicioGravacaoMs_ = agoraMs();

    // O pre-buffer vai inteiro para o arquivo, na ordem.
    //
    // E o motivo de tudo isto existir: quando o operador ouve o sinal e
    // clica, o comeco da transmissao ja passou. Sem estes segundos guardados,
    // todo arquivo comeca tarde demais - foi o que estragou as gravacoes de
    // 160.900.
    qint64 recuperados = 0;
    for (auto& b : pre_) { fila_.push_back(b); recuperados += b.size(); }
    pre_.clear(); preBytes_ = 0;
    if (recuperados > 0) {
        Logger::info(QStringLiteral("IQ: %1 s recuperados da pre-gravacao")
                         .arg(double(recuperados) / double(std::max<qint64>(1, bytesPorSegundo())),
                              0, 'f', 1));
    }
    gravando_.store(true);
    Logger::info(QStringLiteral("IQ: gravacao iniciada"));
    return true;
}

void IqRecorder::parar() {
    if (!gravando_.load()) return;
    gravando_.store(false);
    // Deixa o escritor drenar o que ficou antes de fechar.
    for (int i = 0; i < 50; ++i) {
        {
            std::lock_guard<std::mutex> t(mutex_);
            if (fila_.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::lock_guard<std::mutex> t(mutex_);
    fecharArquivo();
}

void IqRecorder::threadEscrita() {
    std::vector<QByteArray> lote;
    while (!fim_.load()) {
        // O que o gatilho pediu la do callback e feito AQUI, onde bloquear
        // nao machuca ninguem.
        if (pedeIniciar_.exchange(false)) iniciar();
        if (pedeParar_.exchange(false)) {
            gravando_.store(false);
            fecharQuandoVazio_ = true;    // ver o comentario no cabecalho
        }
        {
            std::lock_guard<std::mutex> t(mutex_);
            lote.swap(fila_);
            fila_.clear();
        }
        if (lote.empty()) {
            if (fecharQuandoVazio_) {
                fecharQuandoVazio_ = false;
                std::lock_guard<std::mutex> t(mutex_);
                fecharArquivo();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        for (const auto& b : lote) {
            std::lock_guard<std::mutex> t(mutex_);
            if (!arquivo_.isOpen()) break;
            arquivo_.write(b);
            bytesNoBloco_ += b.size();
            bytesGravados_.fetch_add(b.size());

            // Fatiamento: sem isto uma noite de gravacao vira um arquivo unico
            // que nao abre em lugar nenhum.
            if (minutosPorBloco_ > 0 &&
                (agoraMs() - inicioBlocoMs_) > qint64(minutosPorBloco_) * 60000) {
                fecharArquivo();
                if (!abrirArquivo()) { gravando_.store(false); break; }
                blocos_.fetch_add(1);
            }
        }
        lote.clear();

        // Guarda de disco: para limpo em vez de travar a maquina.
        //
        // Uma vez por segundo, e nao a cada volta: perguntar o espaco livre e
        // uma ida ao sistema de arquivos, e num laco de 5 ms isso custaria
        // mais que a propria gravacao.
        static qint64 ultimaChecagem = 0;
        const qint64 agora = agoraMs();
        if (agora - ultimaChecagem < 1000) continue;
        ultimaChecagem = agora;
        const QStorageInfo st(pastaDestino());
        if (gravando_.load() && st.isValid() &&
            (st.bytesAvailable() / (1024*1024)) < minimoLivreMb_) {
            Logger::warn(QStringLiteral("IQ: espaco livre acabando, encerrando a gravacao"));
            gravando_.store(false);
            std::lock_guard<std::mutex> t(mutex_);
            fecharArquivo();
        }
    }
}

QJsonObject IqRecorder::statusJson() const {
    std::lock_guard<std::mutex> t(mutex_);
    QJsonObject o;
    o["gravando"]   = gravando_.load();
    o["arquivo"]    = gravando_.load() ? QFileInfo(caminhoAtual_).fileName() : QString();
    o["pasta"]      = pastaDestino();
    o["bytes"]      = double(bytesGravados_.load());
    o["blocos"]     = blocos_.load();
    o["overruns"]   = overruns_.load();
    o["formato"]    = "WAV IQ 16 bits";
    o["sampleRate"] = double(sps_);
    o["centroHz"]   = double(centro_);
    o["preLigada"]  = preLigada_;
    o["preSegundos"]= preSegundos_;
    o["preCheio"]   = bytesPorSegundo() > 0
                        ? double(preBytes_) / double(bytesPorSegundo()) : 0.0;
    o["gatilho"]    = gatilhoArmado_;
    o["limiarDb"]   = limiarDb_;
    o["segundos"]   = gravando_.load()
                        ? (agoraMs() - inicioGravacaoMs_) / 1000.0 : 0.0;
    const QStorageInfo st(pastaDestino());
    o["livreMb"]    = st.isValid() ? double(st.bytesAvailable() / (1024*1024)) : 0.0;
    if (!ultimoErro_.isEmpty()) o["erro"] = ultimoErro_;
    return o;
}

} // namespace masdr
