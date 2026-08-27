#include "Logger.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QMutex>
#include <QStandardPaths>
#include <QFileInfo>

namespace masdr {

namespace {

// Onde o run.log pode ser escrito de verdade.
//
// ELE FICAVA SO AO LADO DO EXECUTAVEL, E ISSO ESCONDIA O ARQUIVO.
//
// O instalador poe o RXSDR em Arquivos de Programas, onde usuario comum NAO
// escreve. O open falhava calado - ha um "if (f.isOpen())" antes de cada
// escrita -, ou entao o Windows desviava a gravacao para a pasta VirtualStore
// sem avisar ninguem. Nos dois casos o que se ve na pasta do programa e um
// run.log ANTIGO, de alguma execucao feita como administrador, que nunca mais
// muda. Foi exatamente o que aconteceu: tres relatos seguidos vieram com os
// mesmos carimbos de hora, e o problema nao estava em quem enviou.
//
// Agora: tenta ao lado do executavel, e caindo fora vai para a pasta de dados
// do usuario, que sempre aceita escrita.
static QString g_caminhoLog;

QFile& logFile() {
    static QFile f;
    static bool opened = false;
    if (!opened) {
        opened = true;
        QString dir = QCoreApplication::applicationDirPath();
        if (dir.isEmpty()) dir = QDir::currentPath();

        f.setFileName(dir + "/run.log");
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            const QString alt = QStandardPaths::writableLocation(
                                    QStandardPaths::AppLocalDataLocation);
            if (!alt.isEmpty()) {
                QDir().mkpath(alt);
                f.setFileName(alt + "/run.log");
                f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
            }
        }
        g_caminhoLog = f.isOpen() ? QFileInfo(f).absoluteFilePath() : QString();

        // O primeiro relato de todos e onde ele mesmo esta.
        //
        // De nada adianta gravar bem num lugar que o dono nao encontra - e a
        // frase "me mande o run.log" so funciona se houver como responder
        // "ele esta aqui".
        if (f.isOpen()) {
            const QString cab = QStringLiteral("=== run.log em %1 ===\n").arg(g_caminhoLog);
            f.write(cab.toUtf8());
            f.flush();
        }
    }
    return f;
}

QMutex& logMutex() {
    static QMutex m;
    return m;
}

void writeLog(const char* level, const QString& s) {
    const QString line = QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
                         + " [" + level + "] " + s + "\n";
    QMutexLocker lk(&logMutex());
    auto& f = logFile();
    if (f.isOpen()) {
        f.write(line.toUtf8());
        f.flush();
    }
}

} // namespace

void Logger::info (const QString& s) { qInfo()    << "[INFO ]" << s; writeLog("INFO ", s); }
void Logger::warn (const QString& s) { qWarning() << "[WARN ]" << s; writeLog("WARN ", s); }
void Logger::error(const QString& s) { qCritical()<< "[ERROR]" << s; writeLog("ERROR", s); }
void Logger::debug(const QString& s) { qDebug()   << "[DEBUG]" << s; writeLog("DEBUG", s); }

QString Logger::caminhoArquivo() {
    QMutexLocker lk(&logMutex());
    logFile();               // garante que ja foi decidido
    return g_caminhoLog;
}

} // namespace masdr
