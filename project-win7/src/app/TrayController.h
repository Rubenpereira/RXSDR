#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <functional>

namespace masdr {

class TrayController {
public:
    explicit TrayController(const std::string& frontendUrl);
    ~TrayController();

    bool create(HWND msgWindow);
    void destroy();

    // Retorna true se a mensagem foi consumida
    bool handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    void openBrowser();
    void quitApp();
    void showContextMenu();

    std::string url_;
    HWND        hwnd_    = nullptr;
    UINT        iconId_  = 1;
    bool        created_ = false;
};

} // namespace masdr
