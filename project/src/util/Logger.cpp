#include "Logger.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QMutex>

namespace masdr {

namespace {

QFile& logFile() {
    static QFile f;
    static bool opened = false;
    if (!opened) {
        opened = true;
        // Grava no mesmo diretório do executável
        QString dir = QCoreApplication::applicationDirPath();
        if (dir.isEmpty()) dir = QDir::currentPath();
        f.setFileName(dir + "/run.log");
        f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
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

} // namespace masdr
