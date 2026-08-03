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

#include <chrono>
#include <cstdlib>
#include <thread>

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
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&masterApp, lockDir]() {
        // --- Cao de guarda do encerramento -------------------------------
        // O stop() pode ficar preso esperando a thread de leitura do dongle:
        // o rtlsdr_cancel_async nem sempre consegue interromper o driver USB
        // (dongle removido, libusb travada) e o wait() nao tem prazo. Quando
        // isso acontecia, o RXSDR sumia da tela mas continuava vivo no
        // Gerenciador de Tarefas segurando o lock, e a abertura seguinte
        // reclamava "O RXSDR ja esta em execucao".
        //
        // Este fio dorme 5 s em paralelo. Se o encerramento correr normal, o
        // processo termina antes e ele morre junto. Se travar, ele apaga o
        // lock e derruba o processo a forca com _Exit - sem destrutores, para
        // nao esbarrar em objetos que a thread presa ainda esteja usando.
        std::thread([lockDir]{
            std::this_thread::sleep_for(std::chrono::seconds(5));
            QFile::remove(lockDir + "/rxsdr.lock");
            std::_Exit(0);
        }).detach();

        masterApp.stop();
    });

#ifndef RXSDR_HEADLESS
    // Abre o navegador padrão na UI
    QDesktopServices::openUrl(QUrl(masterApp.frontendUrl()));
#endif

    return app.exec();
}

