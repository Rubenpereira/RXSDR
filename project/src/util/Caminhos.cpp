#include "Caminhos.h"
#include "Logger.h"

#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

namespace masdr {

QString areaDeTrabalho()
{
    static QString escolhida;
    static bool jaDisse = false;

    if (!escolhida.isEmpty()) return escolhida;

#ifdef Q_OS_WIN
    // Pergunta ao Windows, que sabe do desvio do OneDrive.
    PWSTR bruto = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &bruto))
        && bruto) {
        const QString p = QDir::fromNativeSeparators(QString::fromWCharArray(bruto));
        CoTaskMemFree(bruto);
        if (!p.isEmpty() && QDir(p).exists()) escolhida = p;
    }
#endif

    if (escolhida.isEmpty()) {
        const QString p = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (!p.isEmpty() && QDir(p).exists()) escolhida = p;
    }
    if (escolhida.isEmpty()) escolhida = QDir::homePath();

    if (!jaDisse) {
        jaDisse = true;
        Logger::info(QStringLiteral("Area de Trabalho: ") + escolhida);
    }
    return escolhida;
}

} // namespace masdr
