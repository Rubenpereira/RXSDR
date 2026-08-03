// RXSDR - main.cpp (Win7 sem Qt)
// Bootstrap: cria janela de mensagens, sobe HTTP+WS, instala tray, abre navegador.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "app/Application.h"
#include "app/TrayController.h"
#include "util/Logger.h"

static masdr::TrayController* g_tray = nullptr;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_tray && g_tray->handleMessage(msg, wp, lp)) return 0;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Instância única
    HANDLE mutex = CreateMutexA(nullptr, TRUE, "RXSDR_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(nullptr,
            "O RXSDR ja esta em execucao.\nUse o icone da bandeja para reabrir a interface.",
            "RXSDR", MB_ICONINFORMATION | MB_OK);
        return 0;
    }

    masdr::Logger::info("=== RXSDR iniciando ===");

    // Registra classe de janela de mensagens
    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "RXSDR_MsgWnd";
    RegisterClassA(&wc);

    HWND msgWnd = CreateWindowExA(0, "RXSDR_MsgWnd", "RXSDR",
                                   0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!msgWnd) {
        masdr::Logger::error("Falha ao criar janela de mensagens");
        return 1;
    }

    masdr::Application app;
    if (!app.start()) {
        MessageBoxA(nullptr, "Falha ao iniciar o backend. Veja run.log.", "RXSDR", MB_ICONERROR | MB_OK);
        return 2;
    }

    // Tray
    masdr::TrayController tray(app.frontendUrl());
    g_tray = &tray;
    tray.create(msgWnd);

    // Abre o navegador
    ShellExecuteA(nullptr, "open", app.frontendUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    // Loop de mensagens Win32
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    tray.destroy();
    g_tray = nullptr;
    app.stop();
    masdr::Logger::info("=== RXSDR encerrado ===");

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
