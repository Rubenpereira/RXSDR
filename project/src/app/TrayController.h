#pragma once
#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <memory>

namespace masdr {

class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(const QString& frontendUrl, QObject* parent=nullptr);
    void show();

private slots:
    void openBrowser();
    void quitApp();

private:
    std::unique_ptr<QSystemTrayIcon> tray_;
    std::unique_ptr<QMenu> menu_;
    QString url_;
};

} // namespace masdr
