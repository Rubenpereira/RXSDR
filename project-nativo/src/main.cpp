// ---------------------------------------------------------------------------
//  RXSDR Nativo - ponto de entrada
//
//  Uma janela Win32 comum com Direct3D 9 por baixo. D3D9 foi escolhido de
//  proposito: vem dentro de todo Windows desde o XP, entao roda no Windows 7
//  do amigo (AMD E-350) e no 11 sem instalar DirectX, runtime nem nada.
//  Programa portatil: RXSDR.exe + DLLs do dongle na mesma pasta, e so.
// ---------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>

#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"

#include "app/Config.h"
#include "app/Radio.h"
#include "ui/Recursos.h"
#include "ui/Ui.h"
#include "util/Logger.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

LPDIRECT3D9           g_d3d = nullptr;
LPDIRECT3DDEVICE9     g_dev = nullptr;
D3DPRESENT_PARAMETERS g_pp = {};
UINT g_novaLarg = 0, g_novaAlt = 0;
bool g_perdido = false;
WINDOWPLACEMENT g_posicao = {sizeof(WINDOWPLACEMENT)};
bool g_temPosicao = false;

bool criarD3D(HWND h)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return false;
    g_pp = {};
    g_pp.Windowed = TRUE;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_pp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_pp.EnableAutoDepthStencil = TRUE;
    g_pp.AutoDepthStencilFormat = D3DFMT_D16;
    g_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;   // acompanha o monitor
    // Placa de video fraca/antiga: tenta com aceleracao de vertices por
    // hardware; se nao der, por software (continua funcionando).
    if (g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, h, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_dev) < 0 &&
        g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, h, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_pp, &g_dev) < 0)
        return false;
    return true;
}

void resetar()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    const HRESULT hr = g_dev->Reset(&g_pp);
    if (hr == D3DERR_INVALIDCALL) IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

LRESULT WINAPI WndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    // Clique sempre com a posicao certa: se o ultimo evento do Windows foi
    // "o mouse saiu da janela", o ImGui fica sem posicao e o clique seguinte
    // nao acerta nada (acontecia com alguns cliques em botoes).
    if (ImGui::GetCurrentContext() &&
        (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP ||
         msg == WM_LBUTTONDBLCLK || msg == WM_MOUSEWHEEL)) {
        POINT pt{(short)LOWORD(l), (short)HIWORD(l)};
        if (msg == WM_MOUSEWHEEL) ScreenToClient(h, &pt);   // na roda, a posicao vem em coordenadas da tela
        ImGui::GetIO().AddMousePosEvent((float)pt.x, (float)pt.y);
    }
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return true;
    switch (msg) {
    case WM_SIZE:
        if (w == SIZE_MINIMIZED) return 0;
        g_novaLarg = (UINT)LOWORD(l); g_novaAlt = (UINT)HIWORD(l);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize.x = 1024; mm->ptMinTrackSize.y = 600;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU) return 0;   // Alt nao abre menu
        break;
    case WM_CLOSE:
        // guarda a posicao ANTES de a janela ser destruida
        g_temPosicao = GetWindowPlacement(h, &g_posicao) != 0;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// Fonte do Windows (existe no 7 e no 11); se nao achar, a padrao do ImGui.
ImFont* fonte(const char* arquivo, float px)
{
    char win[MAX_PATH]{};
    GetWindowsDirectoryA(win, MAX_PATH);
    const std::string p = std::string(win) + "\\Fonts\\" + arquivo;
    ImGuiIO& io = ImGui::GetIO();
    // Latim (acentos) + "…" (U+2026) + "●" (U+25CF, do GRAVANDO)
    static const ImWchar faixas[] = {0x0020, 0x00FF, 0x2014, 0x2014, 0x2026, 0x2026, 0x25CB, 0x25CB, 0x25CF, 0x25CF, 0};
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
        return io.Fonts->AddFontFromFileTTF(p.c_str(), px, nullptr, faixas);
    ImFontConfig cfg; cfg.SizePixels = px;
    return io.Fonts->AddFontDefault(&cfg);
}

void estilo(float s)
{
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 6; st.FrameRounding = 4; st.PopupRounding = 4; st.GrabRounding = 8;
    st.WindowBorderSize = 1; st.FrameBorderSize = 1;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.039f, 0.078f, 0.051f, 0.98f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.039f, 0.078f, 0.051f, 0.98f);
    c[ImGuiCol_Border]          = ImVec4(0.157f, 0.302f, 0.200f, 1);
    c[ImGuiCol_FrameBg]         = ImVec4(0.078f, 0.141f, 0.098f, 1);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.110f, 0.212f, 0.141f, 1);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.130f, 0.260f, 0.170f, 1);
    c[ImGuiCol_TitleBg]         = ImVec4(0.012f, 0.027f, 0.016f, 1);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.050f, 0.110f, 0.070f, 1);
    c[ImGuiCol_Button]          = ImVec4(0.100f, 0.100f, 0.100f, 1);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.080f, 0.230f, 0.110f, 1);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.060f, 0.350f, 0.150f, 1);
    c[ImGuiCol_Header]          = ImVec4(0.060f, 0.230f, 0.110f, 1);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.080f, 0.300f, 0.140f, 1);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.100f, 0.380f, 0.180f, 1);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.000f, 1.000f, 0.400f, 1);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.500f, 1.000f, 0.650f, 1);
    c[ImGuiCol_CheckMark]       = ImVec4(0.000f, 1.000f, 0.400f, 1);
    c[ImGuiCol_Separator]       = ImVec4(0.157f, 0.302f, 0.200f, 1);
    c[ImGuiCol_Text]            = ImVec4(0.867f, 0.867f, 0.867f, 1);
    st.ScaleAllSizes(s);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    using namespace masdr;
    // Um RXSDR Nativo por PASTA. Cada pasta tem o seu RXSDR.ini (aparelho,
    // frequencia, memorias...), entao duas copias em pastas diferentes podem
    // rodar juntas - ex.: uma na caixa de HF e outra na de VHF, por rtl_tcp.
    // Abrir de novo a MESMA pasta so traz a janela dela para a frente (duas
    // brigariam pelo mesmo .ini e pelo mesmo dongle).
    static std::wstring pastaExe, nomePasta, sufixo, classe;
    {
        wchar_t p[MAX_PATH]{};
        GetModuleFileNameW(nullptr, p, MAX_PATH);
        pastaExe = p;
        const auto b = pastaExe.find_last_of(L"\\/");
        if (b != std::wstring::npos) pastaExe.resize(b);
        const auto b2 = pastaExe.find_last_of(L"\\/");
        nomePasta = b2 == std::wstring::npos ? pastaExe : pastaExe.substr(b2 + 1);
        uint32_t h = 2166136261u;                     // FNV-1a do caminho (sem maiusculas)
        for (wchar_t ch : pastaExe) { h ^= (uint32_t)towlower(ch); h *= 16777619u; }
        wchar_t hx[16]; swprintf(hx, 16, L"%08X", h);
        sufixo = hx;
        classe = L"RXSDRNativo_" + sufixo;
    }
    HANDLE unico = CreateMutexW(nullptr, TRUE, (L"RXSDR_Nativo_unico_" + sufixo).c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND outra = FindWindowW(classe.c_str(), nullptr);
        if (outra) { ShowWindow(outra, SW_RESTORE); SetForegroundWindow(outra); }
        return 0;
    }

    SetProcessDPIAware();
    timeBeginPeriod(1);
    Logger::info("=== RXSDR Nativo " RXSDR_VERSAO " iniciando ===");
    Config::instance().carregar();
    auto& c = Config::instance();

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst,
                      LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP)), LoadCursor(nullptr, IDC_ARROW),
                      nullptr, nullptr, classe.c_str(), LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP))};
    RegisterClassExW(&wc);
    const int jx = (int)c.num("janela_x", CW_USEDEFAULT), jy = (int)c.num("janela_y", CW_USEDEFAULT);
    const int jw = (int)c.num("janela_w", 1360), jh = (int)c.num("janela_h", 760);
    // titulo: so o nome e a versao (pedido do autor)
    const std::wstring titulo = std::wstring(L"RXSDR Nativo " RXSDR_VERSAO_W);
    HWND hwnd = CreateWindowW(wc.lpszClassName, titulo.c_str(), WS_OVERLAPPEDWINDOW,
                              jx, jy, jw, jh, nullptr, nullptr, hInst, nullptr);
    if (!criarD3D(hwnd)) {
        MessageBoxW(hwnd, L"Não foi possível iniciar o Direct3D 9 (placa de vídeo).", L"RXSDR Nativo", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, c.flag("janela_max", false) ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // escala pela resolucao da tela (125%, 150% no Windows 10/11)
    HDC dc = GetDC(hwnd);
    const float s = std::max(1.0f, GetDeviceCaps(dc, LOGPIXELSX) / 96.0f);
    ReleaseDC(hwnd, dc);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // as janelas nao precisam lembrar posicao
    estilo(s);
    Fontes f;
    f.normal     = fonte("segoeui.ttf", 15 * s);
    f.negrito    = fonte("segoeuib.ttf", 13 * s);
    f.pequena    = fonte("segoeui.ttf", 12 * s);
    f.pequenaNeg = fonte("segoeuib.ttf", 12 * s);
    f.digitos    = fonte("consola.ttf", 36 * s);
    f.digitosPeq = fonte("consola.ttf", 22 * s);
    f.mono       = fonte("consola.ttf", 14 * s);
    f.freqMouse  = fonte("segoeuib.ttf", 18 * s);
    io.FontDefault = f.normal;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_dev);

    Radio radio;
    radio.iniciar();
    auto ui = std::make_unique<Ui>(radio, g_dev, f, s);
    // Liga sozinho se estava ligado ao fechar
    if (c.flag("ligado", false)) { std::string e; radio.ligar(true, e); }

    bool fim = false;
    auto ultimoSalvo = std::chrono::steady_clock::now();
    while (!fim) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) fim = true;
        }
        if (fim) break;

        if (g_perdido) {
            const HRESULT hr = g_dev->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
            if (hr == D3DERR_DEVICENOTRESET) resetar();
            g_perdido = false;
        }
        if (g_novaLarg && g_novaAlt) {
            g_pp.BackBufferWidth = g_novaLarg; g_pp.BackBufferHeight = g_novaAlt;
            g_novaLarg = g_novaAlt = 0;
            resetar();
        }
        if (IsIconic(hwnd)) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }

        const auto inicioQuadro = std::chrono::steady_clock::now();
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ui->quadro();
        ImGui::EndFrame();

        g_dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_RGBA(4, 10, 6, 255), 1.0f, 0);
        if (g_dev->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_dev->EndScene();
        }
        if (g_dev->Present(nullptr, nullptr, nullptr, nullptr) == D3DERR_DEVICELOST) g_perdido = true;

        // No maximo ~30 quadros por segundo: a tela fica fluida e sobra
        // processador para o som em PC fraco (o E-350 do amigo).
        const auto gasto = std::chrono::steady_clock::now() - inicioQuadro;
        const auto alvo = std::chrono::milliseconds(33);
        if (gasto < alvo) std::this_thread::sleep_for(alvo - gasto);

        // grava a configuracao de vez em quando (se o PC desligar, nao perde tudo)
        const auto agora = std::chrono::steady_clock::now();
        if (agora - ultimoSalvo > std::chrono::seconds(20)) { ui->salvarEstado(); ultimoSalvo = agora; }
    }

    // Guarda posicao da janela e o estado para a proxima vez
    WINDOWPLACEMENT wp = g_posicao;
    if (g_temPosicao || GetWindowPlacement(hwnd, &wp)) {
        c.set("janela_max", wp.showCmd == SW_SHOWMAXIMIZED);
        c.set("janela_x", (int)wp.rcNormalPosition.left);
        c.set("janela_y", (int)wp.rcNormalPosition.top);
        c.set("janela_w", (int)(wp.rcNormalPosition.right - wp.rcNormalPosition.left));
        c.set("janela_h", (int)(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top));
    }
    c.set("ligado", radio.ligado());
    ui->prepararSaida();
    ui->salvarEstado();
    ui.reset();
    c.salvar(true);                 // grava antes de soltar o aparelho
    radio.encerrar();
    Logger::info("Radio encerrado");
    c.salvar(true);

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_dev) g_dev->Release();
    if (g_d3d) g_d3d->Release();
    DestroyWindow(hwnd);
    Logger::info("Janela fechada");
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    timeEndPeriod(1);
    if (unico) CloseHandle(unico);
    Logger::info("RXSDR Nativo encerrado");
    // Sai direto: tudo ja foi gravado e fechado. Evita que DLLs de terceiros
    // (libusb, API da SDRplay) derrubem o programa ao serem descarregadas.
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
