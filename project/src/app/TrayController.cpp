#include "TrayController.h"
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>

namespace masdr {

TrayController::TrayController(const QString& url, QObject* parent)
    : QObject(parent), url_(url)
{
    tray_ = std::make_unique<QSystemTrayIcon>(QIcon(":/icons/app.png"));
    tray_->setToolTip("RXSDR — clique para abrir a UI");

    menu_ = std::make_unique<QMenu>();
    auto* openA = menu_->addAction("Abrir UI no navegador");
    menu_->addSeparator();
    auto* quitA = menu_->addAction("Sair");

    connect(openA, &QAction::triggered, this, &TrayController::openBrowser);
    connect(quitA, &QAction::triggered, this, &TrayController::quitApp);
    connect(tray_.get(), &QSystemTrayIcon::activated, this,
        [this](QSystemTrayIcon::ActivationReason r){
            if (r == QSystemTrayIcon::DoubleClick) openBrowser();
        });

    tray_->setContextMenu(menu_.get());
}

void TrayController::show() { tray_->show(); }
void TrayController::openBrowser() { QDesktopServices::openUrl(QUrl(url_)); }
void TrayController::quitApp()     { QApplication::quit(); }

} // namespace masdr
