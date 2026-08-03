#include "TrayController.h"
#include "../util/Logger.h"

#include <shellapi.h>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace masdr {

#define WM_TRAYICON  (WM_APP + 1)
#define IDM_OPEN     1001
#define IDM_QUIT     1002

TrayController::TrayController(const std::string& url) : url_(url) {}

TrayController::~TrayController() { destroy(); }

bool TrayController::create(HWND msgWindow) {
    hwnd_ = msgWindow;

    NOTIFYICONDATAA nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd_;
    nid.uID              = iconId_;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1));
    if (!nid.hIcon) nid.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    strncpy_s(nid.szTip, "RXSDR — clique para abrir a UI", 64);

    if (!Shell_NotifyIconA(NIM_ADD, &nid)) {
        Logger::warn("TrayController: Shell_NotifyIconA NIM_ADD falhou");
        return false;
    }
    created_ = true;
    Logger::info("TrayController: icone criado na bandeja");
    return true;
}

void TrayController::destroy() {
    if (!created_) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = iconId_;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    created_ = false;
}

bool TrayController::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_TRAYICON && wParam == iconId_) {
        switch ((UINT)lParam) {
            case WM_LBUTTONDBLCLK:
            case WM_LBUTTONUP:
                openBrowser();
                return true;
            case WM_RBUTTONUP:
                showContextMenu();
                return true;
        }
    }
    return false;
}

void TrayController::openBrowser() {
    Logger::info("Abrindo navegador: " + url_);

    // Tenta abrir em Chrome, Firefox ou Edge antes do navegador padrao (IE).
    // No Windows 7 o padrao e o IE que nao suporta JS moderno.
    static const char* browsers[] = {
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Mozilla Firefox\\firefox.exe",
        "C:\\Program Files (x86)\\Mozilla Firefox\\firefox.exe",
        nullptr
    };

    for (int i = 0; browsers[i] != nullptr; ++i) {
        if (GetFileAttributesA(browsers[i]) != INVALID_FILE_ATTRIBUTES) {
            Logger::info(std::string("Usando: ") + browsers[i]);
            ShellExecuteA(nullptr, "open", browsers[i],
                          url_.c_str(), nullptr, SW_SHOWNORMAL);
            return;
        }
    }

    // Fallback: navegador padrao do sistema (pode ser IE no Win7)
    Logger::warn("Chrome/Firefox nao encontrado — usando navegador padrao");
    ShellExecuteA(nullptr, "open", url_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void TrayController::quitApp() {
    Logger::info("Saindo pelo menu da bandeja");
    PostQuitMessage(0);
}

void TrayController::showContextMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING, IDM_OPEN, "Abrir UI no navegador");
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(menu, MF_STRING, IDM_QUIT, "Sair");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);

    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (cmd == IDM_OPEN) openBrowser();
    else if (cmd == IDM_QUIT) quitApp();
}

} // namespace masdr
