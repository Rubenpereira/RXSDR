// RXSDR - main.cpp
// Bootstrap do backend: cria QApplication, sobe servidor HTTP+WS,
// instala ícone na bandeja e abre o navegador padrão.

#include <QCoreApplication>
#ifndef RXSDR_HEADLESS
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMessageBox>
#endif
#include <QLockFile>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

#include "app/Application.h"

int main(int argc, char** argv)
{
#ifdef RXSDR_HEADLESS
    QCoreApplication app(argc, argv);
#else
    QApplication app(argc, argv);
#endif
    QCoreApplication::setApplicationName("RXSDR");
    QCoreApplication::setApplicationVersion("1.0.0");
    QCoreApplication::setOrganizationName("PU1XTB");
#ifndef RXSDR_HEADLESS
    QApplication::setQuitOnLastWindowClosed(false);
#endif

    const QString lockDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(lockDir);
#ifdef RXSDR_HEADLESS
    QFile::remove(lockDir + "/rxsdr.lock");
#endif
    QLockFile appLock(lockDir + "/rxsdr.lock");
    appLock.setStaleLockTime(0);
    if (!appLock.tryLock(50)) {
#ifdef RXSDR_HEADLESS
        qCritical("O RXSDR ja esta em execucao.");
#else
        QMessageBox::information(nullptr, "RXSDR",
            "O RXSDR ja esta em execucao.\nUse o icone da bandeja para reabrir a interface.");
#endif
        return 0;
    }

#ifndef RXSDR_HEADLESS
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "RXSDR",
            "Sistema sem suporte a tray icon. Encerrando.");
        return 1;
    }
#endif

    masdr::Application masterApp;
#ifdef RXSDR_HEADLESS
    masterApp.setHeadless(true);
#endif
    if (!masterApp.start()) {
#ifdef RXSDR_HEADLESS
        qCritical("Falha ao iniciar o backend. Veja o log.");
#else
        QMessageBox::critical(nullptr, "RXSDR",
            "Falha ao iniciar o backend. Veja o log.");
#endif
        return 2;
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&masterApp]() {
        masterApp.stop();
    });

#ifndef RXSDR_HEADLESS
    // Abre o navegador padrão na UI
    QDesktopServices::openUrl(QUrl(masterApp.frontendUrl()));
#endif

    return app.exec();
}

