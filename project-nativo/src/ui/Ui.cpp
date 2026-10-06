#include "Ui.h"

#include "../app/Config.h"
#include "../sdr/RtlTcpClient.h"
#include "../util/Logger.h"
#include "masdr_json.h"
#include "Recursos.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>

namespace masdr {

#define TEXID(t) ((ImTextureID)(intptr_t)(t))

namespace {

constexpr double kPi = 3.14159265358979323846;

// Cores do tema "dark green" da pagina (index.html, :root)
constexpr ImU32 C_BG_DEEP  = IM_COL32(0x04, 0x0a, 0x06, 255);
constexpr ImU32 C_PANEL    = IM_COL32(0x0a, 0x14, 0x0d, 255);
constexpr ImU32 C_METER    = IM_COL32(0x10, 0x1e, 0x14, 255);
constexpr ImU32 C_SOFT     = IM_COL32(0x14, 0x24, 0x19, 255);
constexpr ImU32 C_BORDER   = IM_COL32(0x1c, 0x36, 0x24, 255);
constexpr ImU32 C_BORDER_L = IM_COL32(0x28, 0x4d, 0x33, 255);
constexpr ImU32 C_RX       = IM_COL32(0x00, 0xff, 0x66, 255);
constexpr ImU32 C_LCD      = IM_COL32(0x00, 0xd4, 0xd4, 255);
constexpr ImU32 C_TEXT     = IM_COL32(0xdd, 0xdd, 0xdd, 255);
constexpr ImU32 C_MID      = IM_COL32(0xaa, 0xaa, 0xaa, 255);
constexpr ImU32 C_DIM      = IM_COL32(0x88, 0x88, 0x88, 255);
constexpr ImU32 C_WARN     = IM_COL32(0xff, 0xb0, 0x20, 255);
constexpr ImU32 C_MENU     = IM_COL32(0x03, 0x07, 0x04, 255);
constexpr ImU32 C_BTN      = IM_COL32(0x1a, 0x1a, 0x1a, 255);

const char* const kFiltrosMem[5] = {"Mostrar todas", "Somente utilitárias", "Broadcast — no ar agora",
                                    "Broadcast — no ar, Américas", "Broadcast — todas"};

double agoraS()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Paleta Eclipse do OpenWebRX+ (mesmas 13 cores da pagina)
const float WF_STOPS[13][4] = {
    {0.000f,   0,   0,  32}, {0.083f,   0,   0,  48}, {0.167f,   0,   0,  80},
    {0.250f,   0,   0, 145}, {0.333f,  30, 144, 255}, {0.417f, 255, 255, 255},
    {0.500f, 255, 255,   0}, {0.583f, 254, 109,  22}, {0.667f, 255,   0,   0},
    {0.750f, 198,   0,   0}, {0.833f, 159,   0,   0}, {0.917f, 117,   0,   0},
    {1.000f,  74,   0,   0}};

// cor (A8R8G8B8, formato da textura D3D) para uma posicao 0..1 da paleta
uint32_t corPaleta(float norm)
{
    norm = std::clamp(norm, 0.f, 1.f);
    for (int i = 0; i < 12; ++i) {
        const float a = WF_STOPS[i][0], b = WF_STOPS[i + 1][0];
        if (norm >= a && norm <= b) {
            const float t = b > a ? (norm - a) / (b - a) : 0.f;
            const int R = (int)std::lround(WF_STOPS[i][1] + (WF_STOPS[i + 1][1] - WF_STOPS[i][1]) * t);
            const int G = (int)std::lround(WF_STOPS[i][2] + (WF_STOPS[i + 1][2] - WF_STOPS[i][2]) * t);
            const int B = (int)std::lround(WF_STOPS[i][3] + (WF_STOPS[i + 1][3] - WF_STOPS[i][3]) * t);
            return 0xFF000000u | (uint32_t(R) << 16) | (uint32_t(G) << 8) | uint32_t(B);
        }
    }
    return 0xFF4A0000u;
}

// passos da regua de frequencia (getFreqScaleSteps da pagina)
void passosRegua(double span, double& maior, double& menor)
{
    if (span > 5000000)      { maior = 1000000; menor = 200000; }
    else if (span > 1500000) { maior = 500000;  menor = 100000; }
    else if (span > 500000)  { maior = 100000;  menor = 20000; }
    else if (span > 150000)  { maior = 50000;   menor = 10000; }
    else if (span > 50000)   { maior = 10000;   menor = 2000; }
    else if (span > 15000)   { maior = 5000;    menor = 1000; }
    else                     { maior = 1000;    menor = 200; }
}

// dB calibrado -> posicao do ponteiro (dbToNeedle da pagina)
float dbParaPonteiro(float db)
{
    static const float pts[10][2] = {
        {-110, 0.05f}, {-45, 0.08f}, {-20, 0.145f}, {-14, 0.285f}, {-9, 0.450f},
        {-6, 0.605f}, {-3.5f, 0.675f}, {-1.5f, 0.785f}, {-0.5f, 0.895f}, {0, 0.960f}};
    if (db <= pts[0][0]) return pts[0][1];
    if (db >= pts[9][0]) return pts[9][1];
    for (int i = 0; i < 9; ++i)
        if (db >= pts[i][0] && db <= pts[i + 1][0]) {
            const float f = (db - pts[i][0]) / (pts[i + 1][0] - pts[i][0]);
            return pts[i][1] + f * (pts[i + 1][1] - pts[i][1]);
        }
    return 0.05f;
}

const int   kBandas[] = {200, 500, 1800, 2400, 3000, 6000, 10000, 12500, 25000, 50000, 200000};
const int   kPassos[] = {1, 10, 50, 100, 500, 1000, 2500, 5000, 6250, 8330, 9000, 10000, 12500, 25000, 100000};
const char* kModos[]  = {"LSB", "USB", "CW", "AM", "FM", "NFM", "WFM"};
const int   kBwPadrao[] = {2400, 2400, 500, 6000, 25000, 10000, 200000};
const float kIfLarguras[3] = {220, 300, 420};   // IF DISPLAY: estreita, media, larga
const unsigned kTaxas[] = {250000, 1024000, 1400000, 1800000, 1920000, 2048000, 2400000, 2560000, 2880000, 3200000};
const int   kFfts[] = {1024, 2048, 4096, 8192, 16384};

std::string fmtBw(int hz)
{
    char b[32];
    if (hz >= 1000) std::snprintf(b, sizeof b, (hz % 1000) ? "%.1f kHz" : "%.0f kHz", hz / 1000.0);
    else std::snprintf(b, sizeof b, "%d Hz", hz);
    return b;
}

// Carrega um PNG embutido no .exe como textura D3D
IDirect3DTexture9* texturaDoRecurso(IDirect3DDevice9* dev, int id)
{
    HRSRC r = FindResourceA(nullptr, MAKEINTRESOURCEA(id), (LPCSTR)RT_RCDATA);
    if (!r) return nullptr;
    HGLOBAL g = LoadResource(nullptr, r);
    const unsigned char* dados = (const unsigned char*)LockResource(g);
    const int tam = (int)SizeofResource(nullptr, r);
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(dados, tam, &w, &h, &n, 4);
    if (!px) return nullptr;
    IDirect3DTexture9* t = nullptr;
    if (dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr) == D3D_OK) {
        D3DLOCKED_RECT lr;
        if (t->LockRect(0, &lr, nullptr, 0) == D3D_OK) {
            for (int y = 0; y < h; ++y) {
                uint32_t* d = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
                const unsigned char* s = px + y * w * 4;
                for (int x = 0; x < w; ++x)
                    d[x] = (uint32_t(s[x * 4 + 3]) << 24) | (uint32_t(s[x * 4]) << 16) |
                           (uint32_t(s[x * 4 + 1]) << 8) | uint32_t(s[x * 4 + 2]);
            }
            t->UnlockRect(0);
        }
    }
    stbi_image_free(px);
    return t;
}

std::string pastaDoExe()
{
    char p[MAX_PATH]{};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string d(p);
    const auto pos = d.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : d.substr(0, pos);
}

// modulacao do bookmarks.json (formato OpenWebRX) -> modo do RXSDR
std::string modoDaMemoria(std::string m)
{
    for (auto& c : m) c = (char)std::toupper((unsigned char)c);
    if (m == "AM" || m == "USB" || m == "LSB" || m == "CW" || m == "FM" || m == "NFM" || m == "WFM") return m;
    if (m == "SAM") return "AM";
    return "NFM";                         // dmr, ysf, ais... soam como NFM
}

} // namespace

// ===========================================================================
Ui::Ui(Radio& radio, IDirect3DDevice9* dev, const Fontes& f, float escala)
    : r_(radio), dev_(dev), f_(f), s_(escala)
{
    auto& c = Config::instance();
    vol_ = (float)c.dbl("vol", 50);
    sql_ = (float)c.dbl("sql", -90);
    nb_ = (float)c.dbl("nb", 0);
    nr_ = (float)c.dbl("nr", 0);
    tom_ = (float)c.dbl("tom", 50);
    zoom_ = (float)c.dbl("zoom", 0);
    // valores de abertura da pagina (WF_ABRIR_RANGE / WF_ABRIR_BRILHO)
    wfRange_ = (float)c.dbl("wf_range", 53);
    wfBrilho_ = (float)c.dbl("wf_brilho", 106);
    wfSpeed_ = (float)c.dbl("wf_speed", 2);
    autoWf_ = c.flag("wf_auto", true);
    nrEsp_ = c.flag("nr_esp", false);
    nrEspForca_ = (float)c.dbl("nr_esp_forca", 40);
    mudo_ = c.flag("mudo", false);
    passo_ = (int)c.num("passo", 1000);
    memVisivel_ = c.flag("mem_regua", true);
    memFiltroTipo_ = std::clamp((int)c.num("mem_filtro", 0), 0, 4);
    decAj_.rttyBaud = (float)c.dbl("dec_rtty_baud", 45.45);
    decAj_.rttyShift = (float)c.dbl("dec_rtty_shift", 170);
    decAj_.cwTom = (float)c.dbl("dec_cw_tom", 0);
    smRetorno_ = std::clamp((float)c.dbl("smeter_retorno", 0.9), 0.1f, 5.f);
    decHfdlBanda_ = std::clamp((int)c.num("dec_hfdl_banda", 5), 0, 10);
    decAprsCanal_ = std::clamp((int)c.num("dec_aprs_canal", 0), 0, 3);
    espFrac_ = std::clamp((float)c.dbl("esp_frac", 0.32), 0.10f, 0.80f);
    ifOn_ = c.flag("if_on", false);
    ifTam_ = std::clamp((int)c.num("if_larg", 1), 0, 2);
    ganhoRf_ = std::clamp(100.f - c.gainTenths() / 4.96f, 0.f, 100.f);
    carregarImagens();
    wfAcimaPiso_ = (float)c.dbl("wf_acima_piso", 45);
    wfRangeCal_ = (float)c.dbl("wf_range_cal", 53);
    carregarMemorias();
    saidas_ = SomNativo::listarSaidas();
}

Ui::~Ui()
{
    if (wfTex_) wfTex_->Release();
    if (ifTex_) ifTex_->Release();
    if (texSmeter_) texSmeter_->Release();
    if (texOlho_) texOlho_->Release();
    if (wfxTex_) wfxTex_->Release();
    for (auto& f : wfxHist_) if (f.tex) f.tex->Release();
    if (sstvTex_) sstvTex_->Release();
    for (auto& f : sstvHist_) if (f.tex) f.tex->Release();
}

void Ui::carregarImagens()
{
    texSmeter_ = texturaDoRecurso(dev_, IDR_SMETER_PNG);
    texOlho_ = texturaDoRecurso(dev_, IDR_OLHO_PNG);
}

void Ui::salvarEstado()
{
    auto& c = Config::instance();
    c.set("vol", (double)vol_);  c.set("sql", (double)sql_);
    c.set("nb", (double)nb_);    c.set("nr", (double)nr_);
    c.set("tom", (double)tom_);  c.set("zoom", (double)zoom_);
    c.set("wf_range", (double)wfRange_); c.set("wf_brilho", (double)wfBrilho_);
    c.set("wf_speed", (double)wfSpeed_); c.set("wf_auto", autoWf_);
    c.set("nr_esp", nrEsp_); c.set("nr_esp_forca", (double)nrEspForca_);
    c.set("mudo", mudo_); c.set("passo", passo_);
    c.set("mem_regua", memVisivel_);
    c.set("mem_filtro", memFiltroTipo_);
    c.set("dec_rtty_baud", (double)decAj_.rttyBaud);
    c.set("dec_rtty_shift", (double)decAj_.rttyShift);
    c.set("dec_cw_tom", (double)decAj_.cwTom);
    c.set("dec_hfdl_banda", decHfdlBanda_);
    c.set("dec_aprs_canal", decAprsCanal_);
    c.set("esp_frac", (double)espFrac_);
    c.set("if_on", ifOn_);
    c.set("if_larg", ifTam_);
    if (memSujo_) salvarMemorias();
    c.salvar();
}

// ===========================================================================
//  Quadro
// ===========================================================================
void Ui::quadro()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!r_.aoVivo()) { centroMostrado_ = r_.centro(); taxaMostrada_ = r_.taxa(); }
    const double t = agoraS();
    // O que se desenha fica centrado em centroVisual_. Normalmente e o centro
    // dos dados; durante o arrasto segue o mouse (interacaoEspectro); depois de
    // soltar, segura o novo centro ate o primeiro quadro dele chegar - assim a
    // imagem nao volta para tras por um instante.
    if (!arrastando_) {
        if (alvoCentro_ > 0 && t < alvoAte_ && std::llabs((long long)centroMostrado_ - (long long)alvoCentro_) > 1)
            centroVisual_ = alvoCentro_;
        else { alvoCentro_ = 0; centroVisual_ = (double)centroMostrado_; }
    }
    const float dt = (float)std::min(0.25, t - ultimoQuadroT_);
    ultimoQuadroT_ = t;

    novoQuadroFft();
    // Cachoeira: linhas por segundo da pagina (Speed 1..10 = 5..450 l/s),
    // contadas pelo relogio da tela e nao pela chegada da FFT (25/s) - antes
    // o Speed parava de fazer diferenca acima de 6.
    // (O AUTO nao acompanha mais o piso sozinho: so ajusta quando clicado,
    //  com a aparencia guardada no PADRAO - igual era antes.)
    if (r_.aoVivo() && !fft_.bins.empty()) {
        static const float kLinhasSeg[] = {0, 5, 15, 30, 55, 90, 135, 190, 260, 350, 450};
        wfAcum_ += dt * kLinhasSeg[std::clamp((int)std::lround(wfSpeed_), 1, 10)];
        if (wfAcum_ >= 1.0) {
            const int n = (int)wfAcum_;
            for (int i = 0; i < std::min(n, 10); ++i) { linhaCachoeira(); linhaIf(); }
            wfAcum_ = n > 10 ? 0.0 : wfAcum_ - n;
        }
    } else {
        wfAcum_ = 0;
    }
    if (sqlMedindo_) {
        const double tq = agoraS();
        if (tq - sqlUlt_ >= 0.1) { sqlUlt_ = tq; sqlAmostras_.push_back(peakDbSql_); }
        if (tq - sqlIni_ >= 4.0) {
            sqlMedindo_ = false;
            if (!sqlAmostras_.empty()) {
                float topo = -200, soma = 0;
                for (float a : sqlAmostras_) { topo = std::max(topo, a); soma += a; }
                sql_ = std::clamp(std::round(topo + 2.f), -90.f, 30.f);
                char d[160];
                std::snprintf(d, sizeof d, "Ruído medido: pico %.0f dB, médio %.0f dB. Squelch em %.0f dB. Clique de novo para medir outra vez.",
                              topo, soma / sqlAmostras_.size(), sql_);
                sqlDica_ = d;
            }
        }
    }
    atualizarSmeter(dt > 0 ? dt : 0.033f);
    atualizarSom();

    // CPU do processo (a cada segundo)
    if (t - cpuT_ > 1.0) {
        FILETIME a, b, k, u, agora;
        GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u);
        GetSystemTimeAsFileTime(&agora);
        const unsigned long long proc = ((unsigned long long)k.dwHighDateTime << 32 | k.dwLowDateTime) +
                                        ((unsigned long long)u.dwHighDateTime << 32 | u.dwLowDateTime);
        const unsigned long long wall = ((unsigned long long)agora.dwHighDateTime << 32 | agora.dwLowDateTime);
        if (cpuWall_) {
            SYSTEM_INFO si; GetSystemInfo(&si);
            cpu_ = float(double(proc - cpuProc_) / double(wall - cpuWall_) / si.dwNumberOfProcessors * 100.0);
        }
        cpuProc_ = proc; cpuWall_ = wall; cpuT_ = t;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##principal", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                 ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), C_BG_DEEP);

    const float hTopo = 24 * s_, hCtrl = 132 * s_, hModos = 30 * s_, hRod = 22 * s_;
    barraTopo();
    painelControles(hTopo, hCtrl);
    linhaModos(hTopo + hCtrl, hModos);

    const float yArea = hTopo + hCtrl + hModos;
    const float hArea = std::max(100.f, H - yArea - hRod);
    const float hEsc = 18 * s_;
    const float hEsp = std::floor(std::clamp(espFrac_, 0.10f, 0.80f) * hArea);
    float xm = 0;                                  // onde comeca o espectro principal
    if (ifOn_) {
        const float wIf = std::min(kIfLarguras[ifTam_] * s_, W * 0.5f);
        ifDisplay(2 * s_, yArea + 2 * s_, wIf, hArea - 4 * s_);
        xm = wIf + 6 * s_;
    }
    const float Wm = W - xm;
    espectro(xm, yArea, Wm, hEsp);
    escala(xm, yArea + hEsp, Wm, hEsc);
    {   // A regua de frequencia e a alca do divisor: arrastar para cima ou
        // para baixo muda o tamanho do espectro e da cachoeira (como na pagina).
        ImGui::SetCursorScreenPos(ImVec2(xm, yArea + hEsp));
        ImGui::InvisibleButton("##divisor", ImVec2(Wm, hEsc));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            espFrac_ = std::clamp((io.MousePos.y - yArea - hEsc * 0.5f) / hArea, 0.10f, 0.80f);
    }
    cachoeira(xm, yArea + hEsp + hEsc, Wm, hArea - hEsp - hEsc);
    rodape(H - hRod, hRod);

    // teclado: setas sintonizam um passo
    if (!io.WantTextInput && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) sintonizar(r_.vfo() + passo_, false);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && r_.vfo() > (uint64_t)passo_) sintonizar(r_.vfo() - passo_, false);
        // para cima / para baixo: exatamente 1 MHz
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) sintonizar(r_.vfo() + 1000000, false);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && r_.vfo() > 1000000 + 1000) sintonizar(r_.vfo() - 1000000, false);
    }

    ImGui::End();

    if (cfgAberta_) janelaConfig();
    if (sobreAberta_) janelaSobre();
    if (memAberta_) janelaMemorias();
    if (decAberta_) janelaDecoders();

    // aviso de erro (ex.: dongle nao encontrado)
    if (!erro_.empty() && t < erroAte_) {
        ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::Begin("##erro", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 120, 120, 255));
        ImGui::PushTextWrapPos(460 * s_);
        ImGui::TextUnformatted(erro_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) erroAte_ = 0;
        ImGui::End();
    }
}

// ===========================================================================
//  Barra de cima (menu)
// ===========================================================================
void Ui::barraTopo()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetIO().DisplaySize.x, h = 24 * s_;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, h), C_MENU);
    dl->AddLine(ImVec2(0, h - 1), ImVec2(W, h - 1), C_BORDER);

    float x = 8 * s_;
    auto item = [&](const char* txt, ImU32 cor) -> bool {
        ImGui::PushFont(f_.negrito);
        const ImVec2 ts = ImGui::CalcTextSize(txt);
        ImGui::PopFont();
        const float w = ts.x + 18 * s_;
        ImGui::SetCursorScreenPos(ImVec2(x, 0));
        ImGui::PushID(txt);
        const bool clicou = ImGui::InvisibleButton("##m", ImVec2(w, h));
        const bool sobre = ImGui::IsItemHovered();
        ImGui::PopID();
        if (sobre) dl->AddRectFilled(ImVec2(x, 0), ImVec2(x + w, h - 1), IM_COL32(0x14, 0x29, 0x1a, 255));
        ImGui::PushFont(f_.negrito);
        dl->AddText(ImVec2(x + 9 * s_, (h - ts.y) * 0.5f), cor, txt);
        ImGui::PopFont();
        x += w;
        return clicou;
    };
    if (item("CONFIGURAÇÃO", IM_COL32(0xff, 0x9a, 0x1f, 255))) {
        cfgAberta_ = true;
        cfgAssin_.clear(); cfgMsg_.clear();
        // prepara os campos com o que esta valendo
        auto& c = Config::instance();
        lista_ = Radio::listar(r_.temDispositivo() ? r_.tipo() : std::string(), r_.serial(), c.device() == "sdrplay");
        // O dongle que o proprio RXSDR esta usando nao aparece na busca do
        // Windows (esta ocupado). Entra na lista como "em uso".
        if (r_.temDispositivo() && r_.tipo() != "rtltcp") {
            bool achou = false;
            for (const auto& d : lista_) if (d.tipo == r_.tipo() && d.serial == r_.serial()) achou = true;
            if (!achou) lista_.insert(lista_.begin(), DispositivoInfo{r_.tipo(), r_.serial(), r_.tipo() == "rtlsdr" ? "RTL-SDR (em uso)" : "SDRplay (em uso)"});
        }
        const std::string tp = c.device();
        cfgTipo_ = tp == "rtltcp" ? 1 : tp == "sdrplay" ? 2 : 0;
        cfgDisp_ = 0;
        for (size_t i = 0; i < lista_.size(); ++i)
            if (lista_[i].serial == c.serial()) cfgDisp_ = (int)i;
        std::snprintf(cfgHost_, sizeof cfgHost_, "%s", c.tcpHost().c_str());
        cfgPorta_ = c.tcpPort();
        cfgTaxa_ = 1;
        for (int i = 0; i < (int)(sizeof kTaxas / sizeof kTaxas[0]); ++i) if (kTaxas[i] == c.sampleRate()) cfgTaxa_ = i;
        cfgQ_ = c.qmode() == "off" ? 0 : c.qmode() == "on" ? 1 : 2;
        cfgFft_ = 3;
        for (int i = 0; i < 5; ++i) if (kFfts[i] == c.fftSize()) cfgFft_ = i;
        cfgAgc_ = c.agc(); cfgBias_ = c.biasT(); cfgPpm_ = c.ppm();
        cfgGanho_ = c.gainTenths() / 10.f;
        cfgLna_ = c.sdrplayLna(); cfgIfGain_ = c.sdrplayIfGain(); cfgIfAgc_ = c.sdrplayIfAgc();
        cfgS9Hf_ = (int)c.num("s9_hf", -94); cfgS9Vhf_ = (int)c.num("s9_vhf", -93);
        saidas_ = SomNativo::listarSaidas();
        cfgSaida_ = 0;
        for (size_t i = 0; i < saidas_.size(); ++i) if (saidas_[i] == c.str("audio_saida")) cfgSaida_ = (int)i + 1;
    }
    if (item("SALVAR", C_RX)) {
        salvarEstado();
        Config::instance().salvar(true);
        erro_ = "Configuração salva em " + Config::instance().caminho();
        erroAte_ = agoraS() + 2.5;
    }
    {   // MEMORIAS abre um menu (igual ao da pagina)
        const float xm = x;
        if (item("MEMÓRIAS", IM_COL32(0x7f, 0xb0, 0xff, 255))) ImGui::OpenPopup("menuMem");
        if (ImGui::IsPopupOpen("menuMem")) ImGui::SetNextWindowPos(ImVec2(xm, h));
        menuMemorias();
    }
    if (item("DECODERS", IM_COL32(0xb0, 0xf0, 0x20, 255))) decAberta_ = true;
    {
        const bool g = r_.som().gravando();
        char rot[48];
        if (g) {
            const int sgs = (int)r_.som().segundosGravados();
            std::snprintf(rot, sizeof rot, "● GRAVANDO %02d:%02d", sgs / 60, sgs % 60);
        } else std::snprintf(rot, sizeof rot, "GRAVAR");
        if (item(rot, g ? IM_COL32(0xff, 0x52, 0x52, 255) : IM_COL32(0xff, 0x80, 0x80, 255))) alternarGravacao();
    }
    if (item("SOBRE", C_MID)) sobreAberta_ = true;

    ImGui::PushFont(f_.negrito);
    const char* nome = "RXSDR Nativo v" RXSDR_VERSAO;
    const ImVec2 ts = ImGui::CalcTextSize(nome);
    dl->AddText(ImVec2(W - ts.x - 12 * s_, (h - ts.y) * 0.5f), C_LCD, nome);
    ImGui::PopFont();
}

// ===========================================================================
//  Linha dos controles
// ===========================================================================
void Ui::painelControles(float y, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetIO().DisplaySize.x;
    dl->AddRectFilled(ImVec2(0, y), ImVec2(W, y + h), C_PANEL);
    dl->AddLine(ImVec2(0, y + h - 1), ImVec2(W, y + h - 1), C_BORDER);

    const float m = 8 * s_, ih = h - 2 * m;
    float x = m;
    painelEsquerdo(x, y + m, 112 * s_, ih);      x += 112 * s_ + m;
    smeter(x, y + m, 210 * s_, ih);               x += 210 * s_ + m;
    mostradorFreq(x, y + m, 180 * s_, ih);        x += 180 * s_ + m;
    olhoMagico(x, y + m, 150 * s_, ih);           x += 150 * s_ + m;

    // Os tres grupos de sliders ocupam todo o espaco que sobra, como na
    // pagina (antes ficavam estreitos, encostados a direita, com um vao).
    const float livre = W - m - x - 2 * m;
    float g1 = 128 * s_, g2 = 172 * s_, g3 = 172 * s_;
    if (livre > g1 + g2 + g3) {
        g1 = std::floor(livre * 0.26f);
        g2 = std::floor(livre * 0.37f);
        g3 = livre - g1 - g2;
    }
    float xr = W - m - g3;
    grupoSliders(xr, y + m, g3, ih, 3); xr -= g2 + m;
    grupoSliders(xr, y + m, g2, ih, 2); xr -= g1 + m;
    if (xr >= x - 1) grupoSliders(xr, y + m, g1, ih, 1);
}

bool Ui::botao(const char* rot, float x, float y, float w, float h, bool ativo, unsigned corAtiva, bool borda)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushID(rot); ImGui::PushID((int)(x * 7 + y));
    const bool clicou = ImGui::InvisibleButton("##b", ImVec2(w, h));
    const bool sobre = ImGui::IsItemHovered();
    ImGui::PopID(); ImGui::PopID();
    const ImU32 cor = corAtiva ? (ImU32)corAtiva : C_RX;
    ImU32 fundo = ativo ? IM_COL32(0x10, 0x3a, 0x1c, 255) : C_BTN;
    if (sobre) fundo = ativo ? IM_COL32(0x16, 0x4a, 0x24, 255) : IM_COL32(0x24, 0x2c, 0x26, 255);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fundo, 3 * s_);
    if (borda) dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), ativo ? cor : C_BORDER_L, 3 * s_);
    // texto em ate duas linhas ("6 kHz\nBW")
    const char* nl = std::strchr(rot, '\n');
    ImGui::PushFont(f_.negrito);
    if (!nl) {
        const ImVec2 ts = ImGui::CalcTextSize(rot);
        dl->AddText(ImVec2(x + (w - ts.x) * 0.5f, y + (h - ts.y) * 0.5f), ativo ? cor : C_TEXT, rot);
        ImGui::PopFont();
    } else {
        const std::string l1(rot, nl);
        const ImVec2 t1 = ImGui::CalcTextSize(l1.c_str());
        ImGui::PopFont();
        ImGui::PushFont(f_.pequena);
        const ImVec2 t2 = ImGui::CalcTextSize(nl + 1);
        ImGui::PopFont();
        const float tot = t1.y + t2.y;
        ImGui::PushFont(f_.negrito);
        dl->AddText(ImVec2(x + (w - t1.x) * 0.5f, y + (h - tot) * 0.5f), ativo ? cor : C_TEXT, l1.c_str());
        ImGui::PopFont();
        ImGui::PushFont(f_.pequena);
        dl->AddText(ImVec2(x + (w - t2.x) * 0.5f, y + (h - tot) * 0.5f + t1.y), C_DIM, nl + 1);
        ImGui::PopFont();
    }
    return clicou;
}

void Ui::painelEsquerdo(float x, float y, float w, float h)
{
    const float g = 4 * s_;
    const float hOn = 30 * s_;
    const bool lig = r_.ligado();
    if (botao(lig ? "ON" : "OFF", x, y, w, hOn, lig, lig ? C_RX : IM_COL32(0xff, 0x40, 0x40, 255)))
        ligarDesligar();
    const float bw = (w - g) * 0.5f;
    const float hb = (h - hOn - 3 * g) / 3.f;
    float yy = y + hOn + g;
    const std::string rotBw = fmtBw(r_.banda()) + "\nBW";
    const std::string rotPasso = fmtBw(passo_) + "\nSTEP";
    if (botao(rotBw.c_str(), x, yy, bw, hb, false)) ImGui::OpenPopup("bw");
    if (botao(rotPasso.c_str(), x + bw + g, yy, bw, hb, false)) ImGui::OpenPopup("passo");
    yy += hb + g;
    if (botao(">.<", x, yy, bw, hb, false)) {          // poe o centro do dongle no VFO
        r_.centralizar(r_.vfo());
        muteAte_ = agoraS() + 0.25;
    }
    const bool dc = Config::instance().dcRemove();
    if (botao("DC", x + bw + g, yy, bw, hb, dc)) Config::instance().set("dc_remove", !dc);
    yy += hb + g;
    if (botao("AUTO", x, yy, bw, hb, autoWf_)) autoCachoeira();
    const bool msg = agoraS() < padraoMsgAte_;
    if (botao(msg ? padraoMsg_.c_str() : "PADRÃO", x + bw + g, yy, bw, hb, msg)) padrao();

    if (ImGui::BeginPopup("bw")) {
        for (int b : kBandas) {
            const bool sel = b == r_.banda();
            if (ImGui::Selectable(fmtBw(b).c_str(), sel)) r_.setBanda(b);
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("passo")) {
        for (int p : kPassos) {
            const bool sel = p == passo_;
            if (ImGui::Selectable(fmtBw(p).c_str(), sel)) passo_ = p;
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
//  S-meter de ponteiro: a mesma imagem da pagina (smeter_face.png, 900x450) e
//  o ponteiro com o mesmo pivo e angulo (-128,1 + 78,2 x posicao graus).
// ---------------------------------------------------------------------------
void Ui::smeter(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool lig = r_.ligado();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0x03, 0x07, 0x04, 255), 4 * s_);
    const ImU32 tinta = lig ? IM_COL32_WHITE : IM_COL32(110, 104, 96, 255);
    if (texSmeter_) dl->AddImageRounded(TEXID(texSmeter_), ImVec2(x, y), ImVec2(x + w, y + h),
                                        ImVec2(0, 0), ImVec2(1, 1), tinta, 4 * s_);
    const float pos = smAtual_;   // squelch fechado ja leva o alvo a zero, respeitando o Retorno
    const float sx = w / 900.f, sy = h / 450.f;
    const float ang = (float)((-128.1 + 78.2 * pos) * kPi / 180.0);
    const ImVec2 piv(x + 406 * sx, y + 625 * sy);
    const ImVec2 ponta(piv.x + 524 * std::cos(ang) * sx, piv.y + 524 * std::sin(ang) * sy);
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
    dl->AddLine(piv, ponta, IM_COL32(0xff, 0x2a, 0x2a, 255), std::max(2.f, 5.f * sx));
    dl->PopClipRect();
    if (lig) dl->AddRect(ImVec2(x - 2, y - 2), ImVec2(x + w + 2, y + h + 2), IM_COL32(255, 160, 56, 70), 6 * s_, 0, 3 * s_);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0x20, 0xaa, 0xdd, 255), 4 * s_);
}

// ---------------------------------------------------------------------------
//  Frequencia em digitos. Roda do mouse sobre um digito muda aquele digito;
//  clique na metade de cima sobe, na de baixo desce; duplo clique digita.
// ---------------------------------------------------------------------------
void Ui::mostradorFreq(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0x0a, 0x0e, 0x14, 255), 4 * s_);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0x20, 0xaa, 0xdd, 255), 4 * s_);

    const uint64_t f = r_.vfo();
    char dig[16];
    std::snprintf(dig, sizeof dig, "%010llu", (unsigned long long)(f % 10000000000ULL));
    // 4 digitos de MHz . 6 digitos
    ImGui::PushFont(f_.digitos);
    const ImVec2 td = ImGui::CalcTextSize("0");
    const ImVec2 tp = ImGui::CalcTextSize(".");
    ImGui::PopFont();
    const float larg = td.x * 10 + tp.x * 0.6f;
    const float esc = std::min(1.f, (w - 12 * s_) / larg);
    const float dw = td.x * esc, dh = td.y * esc;
    float xx = x + (w - (dw * 10 + tp.x * 0.6f * esc)) * 0.5f;
    const float yy = y + 22 * s_;
    // primeiro digito que nao e zero a esquerda
    int prim = 0;
    while (prim < 3 && dig[prim] == '0') ++prim;

    ImGui::PushFont(f_.digitos);
    for (int i = 0; i < 10; ++i) {
        if (i == 4) {                                   // ponto dos MHz
            dl->AddText(f_.digitos, f_.digitos->FontSize * esc, ImVec2(xx - tp.x * 0.2f * esc, yy), IM_COL32(0xf4, 0xf7, 0xfb, 255), ".");
            xx += tp.x * 0.6f * esc;
        }
        const char c[2] = {dig[i], 0};
        const bool apagado = i < prim;
        const bool hz = i >= 7;
        const ImU32 cor = apagado ? IM_COL32(0x4a, 0x55, 0x66, 255) : hz ? IM_COL32(0xb8, 0xc4, 0xd4, 255) : IM_COL32(0xf4, 0xf7, 0xfb, 255);
        dl->AddText(f_.digitos, f_.digitos->FontSize * esc, ImVec2(xx, yy), cor, c);
        // area de clique/roda do digito
        ImGui::SetCursorScreenPos(ImVec2(xx, yy));
        ImGui::PushID(i);
        ImGui::InvisibleButton("##dg", ImVec2(dw, dh));
        const bool sobre = ImGui::IsItemHovered();
        const bool clicou = ImGui::IsItemClicked(0);
        const bool duplo = sobre && ImGui::IsMouseDoubleClicked(0);
        ImGui::PopID();
        const uint64_t peso = (uint64_t)std::pow(10.0, 9 - i);
        if (sobre) {
            dl->AddLine(ImVec2(xx, yy + dh + 1), ImVec2(xx + dw, yy + dh + 1), IM_COL32(0x20, 0xaa, 0xdd, 255), 1.5f * s_);
            const float roda = ImGui::GetIO().MouseWheel;
            if (roda > 0) sintonizar(f + peso, false);
            if (roda < 0 && f > peso) sintonizar(f - peso, false);
            if (clicou && !duplo) {
                const float my = ImGui::GetIO().MousePos.y;
                if (my < yy + dh * 0.5f) sintonizar(f + peso, false);
                else if (f > peso) sintonizar(f - peso, false);
            }
            if (duplo) {
                editandoFreq_ = true;
                std::snprintf(freqTxt_, sizeof freqTxt_, "%.3f", f / 1000.0);
                ImGui::OpenPopup("##editfreq");
            }
        }
        xx += dw;
    }
    ImGui::PopFont();

    // barra de sinal fina embaixo (vfoSbarFill da pagina)
    const float bx = x + 12 * s_, by = y + h - 24 * s_, bw = w - 24 * s_;
    const float pos = r_.ligado() ? smAtual_ : 0.f;
    const float fr = std::clamp(0.2f + pos * 0.7f, 0.f, 1.f);
    const ImU32 cb = pos > 0.66f ? IM_COL32(0xe2, 0x3b, 0x3b, 255) : pos > 0.55f ? IM_COL32(0xe0, 0xa5, 0x2f, 255)
                                                                   : IM_COL32(0x2f, 0x7f, 0xd6, 255);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 11 * s_), IM_COL32(0x0c, 0x14, 0x1f, 255), 2 * s_);
    if (r_.ligado()) dl->AddRectFilled(ImVec2(bx + 1, by + 1), ImVec2(bx + 1 + (bw - 2) * fr, by + 10 * s_), cb, 2 * s_);
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + 11 * s_), IM_COL32(0x24, 0x32, 0x46, 255), 2 * s_);
    // escala de S embaixo da barra
    ImGui::PushFont(f_.pequena);
    const char* marcas[] = {"S1", "S3", "S5", "S7", "S9", "+20", "+40"};
    for (int i = 0; i < 7; ++i) {
        const float px = bx + bw * (0.2f + 0.7f * (0.05f + i * 0.14f));
        dl->AddText(ImVec2(px - 6 * s_, by - 13 * s_), IM_COL32(0x87, 0x94, 0xa8, 255), marcas[i]);
    }
    ImGui::PopFont();
    // modo
    ImGui::PushFont(f_.pequena);
    {   // "RX" verde e o modo em ciano, como na pagina
        const float fy = y + 5 * s_;
        dl->AddRectFilled(ImVec2(x + 10 * s_, fy), ImVec2(x + 32 * s_, fy + 12 * s_), IM_COL32(0x1f, 0x7a, 0x2e, 255), 3 * s_);
        dl->AddText(ImVec2(x + 14 * s_, fy - 1 * s_), IM_COL32(0xea, 0xfb, 0xe9, 255), "RX");
        dl->AddText(ImVec2(x + 38 * s_, fy - 1 * s_), IM_COL32(0x37, 0xc0, 0xd6, 255), r_.modo().c_str());
        const std::string bwTxt = fmtBw(r_.banda());
        const ImVec2 tb = ImGui::CalcTextSize(bwTxt.c_str());
        dl->AddText(ImVec2(x + w - tb.x - 10 * s_, fy - 1 * s_), IM_COL32(0x9f, 0xb0, 0xc4, 255), bwTxt.c_str());
    }
    ImGui::PopFont();

    if (ImGui::BeginPopup("##editfreq")) {
        ImGui::TextUnformatted("Frequência (kHz):");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##ft", freqTxt_, sizeof freqTxt_,
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
            const double k = std::atof(freqTxt_);
            if (k > 0) sintonizar((uint64_t)std::llround(k * 1000.0), false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
//  Olho magico: circulo verde com a sombra que fecha com o audio (a mesma
//  conta da pagina: meia-abertura = 78 - 70 x nivel graus), e as setas do
//  passo embaixo.
// ---------------------------------------------------------------------------
void Ui::olhoMagico(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float setas = 34 * s_;
    const float d = std::min(w, h - setas - 6 * s_);
    const ImVec2 c(x + w * 0.5f, y + d * 0.5f);
    const float r = d * 0.5f * (250.f / 300.f);
    const bool lig = r_.ligado();
    dl->AddCircleFilled(c, r, IM_COL32(0x07, 0x2a, 0x0e, 255), 48);
    if (lig) {
        dl->AddCircleFilled(c, r, IM_COL32(0x2f, 0xd2, 0x4a, 230), 48);
        const float hAng = (78.f - 70.f * std::min(1.f, olho_)) * (float)kPi / 180.f;
        const float a0 = (float)kPi * 0.5f - hAng, a1 = (float)kPi * 0.5f + hAng;
        dl->PathLineTo(c);
        dl->PathArcTo(c, r * 1.02f, a0, a1, 32);
        dl->PathFillConvex(IM_COL32(0x04, 0x20, 0x0a, 255));
    }
    if (texOlho_) dl->AddImage(TEXID(texOlho_), ImVec2(c.x - d * 0.5f, c.y - d * 0.5f), ImVec2(c.x + d * 0.5f, c.y + d * 0.5f));
    dl->AddCircle(c, d * 0.5f - 1 * s_, IM_COL32(0xff, 0x9a, 0x1f, 255), 48, 2 * s_);
    // setas: um passo para baixo / para cima
    // Como na pagina: dois botoes de 128 px no total, 36 px de altura, aro
    // azul #25467a, fundo verde escuro em degrade e o triangulo ciano.
    const float tot = std::min(w, 128 * s_), gap = 6 * s_;
    const float bw = (tot - gap) * 0.5f, by = y + h - setas, bx = x + (w - tot) * 0.5f;
    auto seta = [&](const char* id, float sx, int dir) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(sx, by));
        const bool cl = ImGui::InvisibleButton(id, ImVec2(bw, setas));
        const bool at = ImGui::IsItemActive(), so = ImGui::IsItemHovered();
        const ImVec2 a(sx, by), b(sx + bw, by + setas);
        if (at) dl->AddRectFilled(a, b, IM_COL32(0x37, 0xc0, 0xd6, 255), 6 * s_);
        else dl->AddRectFilled(a, b, IM_COL32(0x0d, 0x29, 0x22, 255), 6 * s_);
        dl->AddRect(ImVec2(a.x + 1 * s_, a.y + 1 * s_), ImVec2(b.x - 1 * s_, b.y - 1 * s_),
                    so && !at ? IM_COL32(0x37, 0xc0, 0xd6, 255) : IM_COL32(0x25, 0x46, 0x7a, 255), 6 * s_, 0, 2.f * s_);
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float t = 7 * s_;
        const ImU32 ct = at ? IM_COL32(0x00, 0x33, 0x11, 255) : IM_COL32(0x37, 0xc0, 0xd6, 255);
        if (dir < 0) dl->AddTriangleFilled(ImVec2(c.x - t, c.y), ImVec2(c.x + t * 0.8f, c.y - t), ImVec2(c.x + t * 0.8f, c.y + t), ct);
        else dl->AddTriangleFilled(ImVec2(c.x + t, c.y), ImVec2(c.x - t * 0.8f, c.y - t), ImVec2(c.x - t * 0.8f, c.y + t), ct);
        return cl;
    };
    if (seta("##setaEsq", bx, -1) && r_.vfo() > (uint64_t)passo_) sintonizar(r_.vfo() - passo_, false);
    if (seta("##setaDir", bx + bw + gap, +1)) sintonizar(r_.vfo() + passo_, false);
}

// ---------------------------------------------------------------------------
//  Fader de mesa de som, deitado: canaleta preta com a escala embaixo e o
//  botao preto brilhante com o friso branco no meio. Arrastando, o botao fica
//  vermelho. A parte ja percorrida da canaleta tem um filete colorido (verde,
//  ou a cor pedida - laranja piscando no Ganho de RF com AGC).
//  Tudo desenhado aqui, sem arquivo de imagem.
// ---------------------------------------------------------------------------
namespace {
ImU32 misturar(ImU32 a, int d)       // clareia (d>0) ou escurece (d<0)
{
    auto c = [&](int sh) { return std::clamp((int)((a >> sh) & 0xff) + d, 0, 255); };
    return IM_COL32(c(IM_COL32_R_SHIFT), c(IM_COL32_G_SHIFT), c(IM_COL32_B_SHIFT), (a >> IM_COL32_A_SHIFT) & 0xff);
}

void desenharFader(ImDrawList* dl, float x, float ty, float w, float t,
                   bool ativo, bool sobre, unsigned corTrilho, float s, bool escala = true)
{
    // escala: marquinhas embaixo da canaleta (maiores a cada 50%)
    for (int i = 0; escala && i <= 10; ++i) {
        const float xi = std::floor(x + w * i / 10.f) + 0.5f;
        const float comp = (i % 5 == 0) ? 5.f * s : 3.f * s;
        dl->AddLine(ImVec2(xi, ty + 5.f * s), ImVec2(xi, ty + 5.f * s + comp), IM_COL32(0x8a, 0x94, 0x8e, 150), 1.f);
    }
    // canaleta
    const float g = 2.6f * s;
    dl->AddRectFilled(ImVec2(x - 1 * s, ty - g - 1 * s), ImVec2(x + w + 1 * s, ty + g + 1 * s), IM_COL32(0x3a, 0x44, 0x3e, 255), g + 1 * s);
    dl->AddRectFilled(ImVec2(x, ty - g), ImVec2(x + w, ty + g), IM_COL32(0x04, 0x06, 0x05, 255), g);
    const ImU32 cor = corTrilho ? (ImU32)corTrilho : IM_COL32(0x18, 0xa0, 0x50, 255);
    if (t > 0) dl->AddRectFilled(ImVec2(x + 1 * s, ty - 0.9f * s), ImVec2(x + std::max(1.f * s, w * t), ty + 0.9f * s), cor, 1 * s);

    // botao
    const float cx = std::floor(x + w * t) + 0.5f;
    const float hw = 7.f * s, hh = 8.f * s, r = 2.f * s;
    const ImVec2 a(cx - hw, ty - hh), b(cx + hw, ty + hh);
    dl->AddRectFilled(ImVec2(a.x + 1.5f * s, a.y + 2 * s), ImVec2(b.x + 1.5f * s, b.y + 2 * s), IM_COL32(0, 0, 0, 130), r);   // sombra

    ImU32 c1, c2, c3, c4;                // alto claro -> meio -> meio -> base
    if (ativo) { c1 = IM_COL32(0xff, 0x6a, 0x6a, 255); c2 = IM_COL32(0xd8, 0x1c, 0x1c, 255);
                 c3 = IM_COL32(0xb0, 0x0c, 0x0c, 255); c4 = IM_COL32(0x62, 0x00, 0x00, 255); }
    else       { c1 = IM_COL32(0x78, 0x78, 0x78, 255); c2 = IM_COL32(0x30, 0x30, 0x30, 255);
                 c3 = IM_COL32(0x1c, 0x1c, 0x1c, 255); c4 = IM_COL32(0x06, 0x06, 0x06, 255); }
    if (sobre && !ativo) { c1 = misturar(c1, 0x20); c2 = misturar(c2, 0x18); c3 = misturar(c3, 0x14); c4 = misturar(c4, 0x10); }

    dl->AddRectFilled(a, b, c4, r);
    const float ym = ty - 0.5f * s;
    dl->AddRectFilledMultiColor(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, ym), c1, c1, c2, c2);
    dl->AddRectFilledMultiColor(ImVec2(a.x + 1, ym), ImVec2(b.x - 1, b.y - 1), c3, c3, c4, c4);
    dl->AddLine(ImVec2(a.x + 1.5f, a.y + 1.5f), ImVec2(b.x - 1.5f, a.y + 1.5f), IM_COL32(255, 255, 255, 80), 1.f);   // brilho no alto
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 255), r, 0, 1.f);
    // friso branco no meio, com a sombrinha ao lado
    dl->AddLine(ImVec2(cx + 1.2f * s, a.y + 2.5f * s), ImVec2(cx + 1.2f * s, b.y - 2.5f * s), IM_COL32(0, 0, 0, 160), 1.2f * s);
    dl->AddLine(ImVec2(cx, a.y + 2.5f * s), ImVec2(cx, b.y - 2.5f * s), IM_COL32(0xf2, 0xf2, 0xf2, 245), 1.4f * s);
}
// Rodinha do mouse: o botao fica vermelho enquanto gira e mais 0,6 s depois,
// igual a quando se arrasta. Guarda a hora do ultimo giro de cada fader.
std::unordered_map<ImGuiID, double>& giroRoda() { static std::unordered_map<ImGuiID, double> m; return m; }
bool rodaRecente(ImGuiID id, bool girou)
{
    const double agora = ImGui::GetTime();
    if (girou) giroRoda()[id] = agora;
    const auto it = giroRoda().find(id);
    return it != giroRoda().end() && agora - it->second < 0.6;
}
} // namespace

// ---------------------------------------------------------------------------
//  Sliders no estilo da pagina: rotulo a esquerda, valor a direita e o fader
//  deitado embaixo. Roda do mouse ajusta.
// ---------------------------------------------------------------------------
bool Ui::slider(const char* id, const char* rotulo, const char* valor, float* v,
                float mn, float mx, float x, float y, float w, float passoRoda, unsigned corRotulo,
                unsigned corValor, unsigned corTrilho)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(f_.pequenaNeg);
    dl->AddText(ImVec2(x, y), corRotulo ? (ImU32)corRotulo : IM_COL32(0xdd, 0xdd, 0xdd, 255), rotulo);
    const ImVec2 tv = ImGui::CalcTextSize(valor);
    dl->AddText(ImVec2(x + w - tv.x, y), corValor ? (ImU32)corValor : C_RX, valor);
    const float th = ImGui::GetFontSize();
    ImGui::PopFont();
    const float ty = y + th + 9 * s_;
    const float kr = 5.5f * s_;
    ImGui::SetCursorScreenPos(ImVec2(x - kr, ty - kr - 2 * s_));
    ImGui::PushID(id);
    ImGui::InvisibleButton("##s", ImVec2(w + 2 * kr, 2 * kr + 4 * s_));
    const bool ativo = ImGui::IsItemActive(), sobre = ImGui::IsItemHovered();
    const bool vermelho = ativo || rodaRecente(ImGui::GetItemID(), sobre && ImGui::GetIO().MouseWheel != 0 && passoRoda > 0);
    ImGui::PopID();
    bool mudou = false;
    if (ativo) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - x) / w, 0.f, 1.f);
        const float nv = mn + t * (mx - mn);
        if (nv != *v) { *v = nv; mudou = true; }
    }
    if (sobre && ImGui::GetIO().MouseWheel != 0 && passoRoda > 0) {
        const float nv = std::clamp(*v + (ImGui::GetIO().MouseWheel > 0 ? passoRoda : -passoRoda), mn, mx);
        if (nv != *v) { *v = nv; mudou = true; }
    }
    const float t = (mx > mn) ? (*v - mn) / (mx - mn) : 0.f;
    desenharFader(dl, x, ty, w, t, vermelho, sobre, corTrilho, s_);
    return mudou;
}

void Ui::grupoSliders(float x, float y, float w, float h, int qual)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Mesma moldura da pagina: fundo verde bem escuro e aro azul #20aadd,
    // o mesmo do mostrador de frequencia.
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 40, 12, 46), 4 * s_);
    dl->AddRect(ImVec2(x + 1 * s_, y + 1 * s_), ImVec2(x + w - 1 * s_, y + h - 1 * s_),
                IM_COL32(0x20, 0xaa, 0xdd, 255), 4 * s_, 0, 2.f * s_);          // aro de 2 px
    const float px = x + 12 * s_, pw = w - 24 * s_;
    char b[32];
    if (qual == 1) {
        const char* t = tom_ < 45 ? "Grave" : tom_ > 55 ? "Agudo" : "Plano";
        if (tom_ < 45 || tom_ > 55) std::snprintf(b, sizeof b, "%s %d%%", t, (int)std::lround(std::fabs(tom_ - 50) * 2));
        else std::snprintf(b, sizeof b, "%s", t);
        slider("tom", "Tonalidade", b, &tom_, 0, 100, px, y + h * 0.5f - 12 * s_, pw, 2);
        return;
    }
    const float lh = (h - 12 * s_) / 3.f;
    float yy = y + 6 * s_;
    if (qual == 2) {
        std::snprintf(b, sizeof b, "%.1fx", zoom_);
        if (slider("zoom", "Zoom", b, &zoom_, 0, 8, px, yy, pw, 0.5f)) {
            zoom_ = std::round(zoom_ * 10) / 10;
            centrarParaZoom(true);          // a estacao fica no meio enquanto se amplia
        }
        yy += lh;
        std::snprintf(b, sizeof b, "%d%%", (int)std::lround(vol_));
        slider("vol", mudo_ ? "Volume (MUDO)" : "Volume", b, &vol_, 0, 100, px, yy, pw, 2);
        yy += lh;
        if (sql_ <= -90) std::snprintf(b, sizeof b, "aberto");
        else std::snprintf(b, sizeof b, "%ddB", (int)std::lround(sql_));
        {
            // SQL e um botao (igual a pagina): o clique mede o ruido por 4 s e
            // encosta o squelch 2 dB acima do maior pico. Ambar = aberto.
            const char* rotSql = sqlMedindo_ ? (((int)(agoraS() * 5) % 2) ? "···" : "SQL") : "SQL";
            const unsigned corSql = sqlMedindo_ ? IM_COL32(0x37, 0xc0, 0xd6, 255)
                                  : (sqlAberto_ && r_.aoVivo()) ? IM_COL32(0xff, 0xa0, 0x38, 255) : 0;
            slider("sql", rotSql, b, &sql_, -90, 30, px, yy, pw, 2, corSql);
            ImGui::PushFont(f_.pequenaNeg);
            const ImVec2 ts = ImGui::CalcTextSize("SQL");
            ImGui::PopFont();
            const ImVec2 a(px - 4 * s_, yy - 1 * s_), bb(px + ts.x + 4 * s_, yy + ts.y + 1 * s_);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##sqlauto", ImVec2(bb.x - a.x, bb.y - a.y)) && !sqlMedindo_ && r_.aoVivo()) {
                sqlMedindo_ = true; sqlAmostras_.clear(); sqlIni_ = agoraS(); sqlUlt_ = 0;
            }
            const bool so = ImGui::IsItemHovered();
            dl->AddRect(a, bb, sqlMedindo_ ? IM_COL32(0x37, 0xc0, 0xd6, 255) : corSql ? (ImU32)corSql
                        : so ? IM_COL32(0x25, 0x46, 0x7a, 255) : IM_COL32(0x1c, 0x36, 0x24, 255), 3 * s_);
            if (so) ImGui::SetTooltip("%s", sqlDica_.empty() ? "Clique para ajustar o squelch na beirada do silêncio" : sqlDica_.c_str());
        }
    } else {
        std::snprintf(b, sizeof b, "%d%%", (int)std::lround(nb_));
        slider("nb", "Noise Blanker NB", b, &nb_, 0, 100, px, yy, pw, 5);
        yy += lh;
        std::snprintf(b, sizeof b, "%d%%", (int)std::lround(nr_));
        slider("nr", "Redutor de Ruído", b, &nr_, 0, 100, px, yy, pw, 1);
        yy += lh;
        const bool semGanho = r_.tipo() == "sdrplay" || Config::instance().agc();
        std::snprintf(b, sizeof b, semGanho ? "AGC" : "%.1f dB", 49.6f * (1.f - ganhoRf_ / 100.f));
        // AGC ligado na CONFIGURACAO: rotulo, "AGC" e o trilho piscam em laranja
        unsigned corAgc = 0;
        if (Config::instance().agc() && r_.tipo() != "sdrplay") {
            const float p = 0.5f + 0.5f * std::sin((float)agoraS() * 5.f);
            corAgc = IM_COL32(0xff, 0x9a, 0x1f, (int)(90 + 165 * p));
        }
        if (slider("rf", "Ganho de RF", b, &ganhoRf_, 0, 100, px, yy, pw, 5, corAgc, corAgc, corAgc))
            r_.setGanho((int)std::lround(496.f * (1.f - ganhoRf_ / 100.f)));
    }
}

// ---------------------------------------------------------------------------
//  Linha dos modos + ajustes da cachoeira + NR ESPECTRAL
// ---------------------------------------------------------------------------
void Ui::linhaModos(float y, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetIO().DisplaySize.x;
    dl->AddRectFilled(ImVec2(0, y), ImVec2(W, y + h), C_PANEL);
    dl->AddLine(ImVec2(0, y + h - 1), ImVec2(W, y + h - 1), C_BORDER);
    float x = 8 * s_;
    ImGui::PushFont(f_.pequena);
    dl->AddText(ImVec2(x, y + (h - ImGui::GetFontSize()) * 0.5f), C_DIM, "Mode:");
    x += ImGui::CalcTextSize("Mode:").x + 6 * s_;
    ImGui::PopFont();
    const float bh = h - 8 * s_, by = y + 4 * s_, bw = 40 * s_;
    const std::string m = r_.modo();
    for (const char* md : kModos) {
        if (botao(md, x, by, bw, bh, m == md)) mudarModo(md);
        x += bw + 3 * s_;
    }
    if (botao("MUTE", x, by, bw + 6 * s_, bh, mudo_, IM_COL32(0xff, 0x50, 0x50, 255))) mudo_ = !mudo_;
    x += bw + 20 * s_;

    // Range / Brilho / Speed da cachoeira
    char b[16];
    // Faders compridos (160) quando sobra espaco; numa tela mais estreita eles
    // encolhem (ate 130, abaixo disso o trilho some) para tudo caber na linha, inclusive o relogio UTC no
    // fim - sem isso o relogio saia cortado na borda direita.
    ImGui::PushFont(f_.freqMouse ? f_.freqMouse : f_.negrito);
    const float wRelogio = ImGui::CalcTextSize("00:00 UTC").x + 16 * s_;
    ImGui::PopFont();
    const float resto = (10 + 118 + 3 + 22 + 10 + 56 + 6 + 64 + 6 + 96 + 10 + 8) * s_ + wRelogio;
    const float sw = std::clamp((W - x - resto) / 3.f - 6 * s_, 130 * s_, 160 * s_);
    auto grupo = [&](const char* id, const char* rot, float* v, float mn, float mx, const char* fmt, float passo) {
        dl->AddRect(ImVec2(x, by), ImVec2(x + sw, by + bh), C_BORDER_L, bh * 0.5f);
        std::snprintf(b, sizeof b, fmt, *v);
        ImGui::PushFont(f_.pequenaNeg);
        const float ty = by + (bh - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(x + 8 * s_, ty), IM_COL32(0xdd, 0xdd, 0xdd, 255), rot);
        const float lx = x + 8 * s_ + ImGui::CalcTextSize(rot).x + 6 * s_;
        const float vw = ImGui::CalcTextSize("200x").x;
        ImGui::PopFont();
        const float trilho = x + sw - vw - 20 * s_ - lx;
        // slider compacto
        const float cy = by + bh * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(lx - 4 * s_, by));
        ImGui::PushID(id);   // o fader comeca 4 px depois de lx (espaco para o botao)
        ImGui::InvisibleButton("##c", ImVec2(trilho + 16 * s_, bh));
        const bool at = ImGui::IsItemActive(), so = ImGui::IsItemHovered();
        const bool vermelho = at || rodaRecente(ImGui::GetItemID(), so && ImGui::GetIO().MouseWheel != 0);
        ImGui::PopID();
        bool mudou = false;
        if (at) { *v = std::round(mn + std::clamp((ImGui::GetIO().MousePos.x - lx - 4 * s_) / trilho, 0.f, 1.f) * (mx - mn)); mudou = true; }
        if (so && ImGui::GetIO().MouseWheel != 0) { *v = std::clamp(*v + (ImGui::GetIO().MouseWheel > 0 ? passo : -passo), mn, mx); mudou = true; }
        const float t = (*v - mn) / (mx - mn);
        desenharFader(dl, lx + 4 * s_, cy, trilho, t, vermelho, so, 0, s_, false);
        ImGui::PushFont(f_.pequenaNeg);
        dl->AddText(ImVec2(x + sw - vw - 4 * s_, by + (bh - ImGui::GetFontSize()) * 0.5f), C_RX, b);
        ImGui::PopFont();
        if (mudou) autoWf_ = false;
        x += sw + 6 * s_;
    };
    grupo("rng", "Range", &wfRange_, 20, 120, "%.0f", 1);
    grupo("bri", "Brilho", &wfBrilho_, 0, 200, "%.0f", 1);
    grupo("spd", "Speed", &wfSpeed_, 1, 10, "%.0fx", 1);
    x += 10 * s_;

    // NR ESPECTRAL + setinha da forca
    const float nw = 118 * s_;
    if (botao("NR ESPECTRAL", x, by, nw, bh, nrEsp_, IM_COL32(0xff, 0x9a, 0x1f, 255))) nrEsp_ = !nrEsp_;
    if (botao("v", x + nw + 3 * s_, by, 22 * s_, bh, false)) ImGui::OpenPopup("forca");
    if (ImGui::BeginPopup("forca")) {
        ImGui::Text("Força do NR ESPECTRAL: %d  (%d dB)", (int)nrEspForca_, (int)std::lround(10 + 35 * nrEspForca_ / 100.f));
        ImGui::SetNextItemWidth(260 * s_);
        ImGui::SliderFloat("##fz", &nrEspForca_, 0, 100, "%.0f");
        ImGui::EndPopup();
    }
    x += nw + 3 * s_ + 22 * s_ + 10 * s_;

    // DMR (abre os decodificadores ja no DMR) e IF DISPLAY - como na pagina
    auto botaoAzul = [&](const char* rot, float bx, float bwid, bool ligado) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        ImGui::PushID(rot);
        const bool cl = ImGui::InvisibleButton("##ab", ImVec2(bwid, bh));
        const bool so = ImGui::IsItemHovered();
        ImGui::PopID();
        ImU32 cor = IM_COL32(0x20, 0xaa, 0xdd, 255), fundo = IM_COL32(0x07, 0x12, 0x1a, 255);
        if (ligado) {
            // pisca em laranja no ritmo de 0,9 s (o que esta ligado se anuncia)
            const float f = 0.5f + 0.5f * std::sin((float)(agoraS() * 2 * kPi / 0.9));
            cor = IM_COL32(0xff, (int)(0x9a + (0xd9 - 0x9a) * f), (int)(0x1f + (0xa0 - 0x1f) * f), 255);
            fundo = IM_COL32(0x20, 0x14, 0x0a, 255);
        } else if (so) fundo = IM_COL32(0x0c, 0x20, 0x2c, 255);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bwid, by + bh), fundo, 4 * s_);
        dl->AddRect(ImVec2(bx, by), ImVec2(bx + bwid, by + bh), cor, 4 * s_);
        ImGui::PushFont(f_.negrito);
        const ImVec2 ts = ImGui::CalcTextSize(rot);
        dl->AddText(ImVec2(bx + (bwid - ts.x) * 0.5f, by + (bh - ts.y) * 0.5f), cor, rot);
        ImGui::PopFont();
        return cl;
    };
    const bool dmrLig = r_.decoders().tipo() == Decoders::DMR;
    if (botaoAzul("DMR", x, 56 * s_, dmrLig)) {
        decAberta_ = true;
        if (!dmrLig) escolherDecoder(Decoders::DMR, true);
    }
    x += 56 * s_ + 6 * s_;
    const bool tetraLig = r_.decoders().tipo() == Decoders::TETRA;
    if (botaoAzul("TETRA", x, 64 * s_, tetraLig)) {
        decAberta_ = true;
        if (!tetraLig) escolherDecoder(Decoders::TETRA, true);
    }
    x += 64 * s_ + 6 * s_;
    if (botaoAzul("IF DISPLAY", x, 96 * s_, ifOn_)) ifOn_ = !ifOn_;
    x += 96 * s_ + 10 * s_;

    // Relogio digital em UTC ("15:36 UTC"): as grades das estacoes (SITOR-B,
    // DSC, PACTOR, WEFAX, DRM) sao todas em UTC.
    {
        SYSTEMTIME st; GetSystemTime(&st);
        char hora[16];
        std::snprintf(hora, sizeof hora, "%02d:%02d UTC", st.wHour, st.wMinute);
        ImGui::PushFont(f_.freqMouse ? f_.freqMouse : f_.negrito);
        const ImVec2 ts = ImGui::CalcTextSize(hora);
        const float rw = ts.x + 16 * s_;
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + rw, by + bh), IM_COL32(0x02, 0x06, 0x03, 255), 4 * s_);
        dl->AddRect(ImVec2(x, by), ImVec2(x + rw, by + bh), IM_COL32(0x24, 0x61, 0x32, 255), 4 * s_);
        dl->AddText(ImVec2(x + 8 * s_, by + (bh - ts.y) * 0.5f), IM_COL32(0x40, 0xff, 0x70, 255), hora);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(x, by));
        ImGui::Dummy(ImVec2(rw, bh));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hora UTC (a das grades de horários das estações)");
    }
}

// ===========================================================================
//  Espectro, regua e cachoeira
// ===========================================================================
double Ui::hzParaX(double hz, float x, float w) const
{
    const double span = taxaMostrada_ / (1.0 + zoom_);
    return x + ((hz - centroVisual_) / span + 0.5) * w;
}

double Ui::xParaHz(float px, float x, float w) const
{
    const double span = taxaMostrada_ / (1.0 + zoom_);
    return centroVisual_ + ((px - x) / w - 0.5) * span;
}

// Mouse no espectro/cachoeira (igual a pagina):
//  - clique: sintoniza (no multiplo do passo);
//  - arrastar: a faixa corre junto com o mouse - o centro desliza liso e o
//    VFO anda de passo em passo, os dois juntos;
//  - arrastar a BORDA do filtro: muda a largura (BW);
//  - roda: um passo.
bool Ui::interacaoEspectro(float x, float y, float w, float h, const char* id)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool sobre = ImGui::IsItemHovered();
    const double span = taxaMostrada_ / (1.0 + zoom_);

    // bordas do filtro
    const uint64_t v = r_.vfo();
    const int bw = r_.banda();
    const std::string m = r_.modo();
    double f0 = (double)v - bw / 2.0, f1 = (double)v + bw / 2.0;
    if (m == "USB") { f0 = (double)v; f1 = (double)v + bw; }
    if (m == "LSB") { f0 = (double)v - bw; f1 = (double)v; }
    const float xb0 = (float)hzParaX(f0, x, w), xb1 = (float)hzParaX(f1, x, w);
    const float tol = 6 * s_;
    int borda = 0;
    if (std::fabs(io.MousePos.x - xb0) <= tol && m != "USB") borda = -1;
    else if (std::fabs(io.MousePos.x - xb1) <= tol && m != "LSB") borda = +1;
    if (sobre && borda != 0 && !arrastando_) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (ImGui::IsItemActivated()) {
        arrastoX0_ = io.MousePos.x;
        arrastoW_ = w;
        arrastoCentro0_ = (uint64_t)std::llround(centroVisual_);
        arrastoVfo0_ = v;
        arrastando_ = false;
        arrastandoBw_ = borda != 0;
        bordaBw_ = borda;
    }
    if (ImGui::IsItemActive()) {
        if (arrastandoBw_) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            const double f = xParaHz(io.MousePos.x, x, w);
            double nbw = (m == "USB") ? f - (double)v : (m == "LSB") ? (double)v - f : 2.0 * std::fabs(f - (double)v);
            const double q = nbw < 5000 ? 50 : nbw < 30000 ? 500 : 5000;
            nbw = std::clamp(std::round(nbw / q) * q, 50.0, 250000.0);
            if ((int)nbw != r_.banda()) r_.setBanda((int)nbw);
        } else {
            if (std::fabs(io.MousePos.x - arrastoX0_) > 4 * s_) arrastando_ = true;
            if (arrastando_) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const double desl = -(io.MousePos.x - arrastoX0_) / arrastoW_ * span;
                centroVisual_ = (double)arrastoCentro0_ + desl;
                const uint64_t p = (uint64_t)std::max(1, passo_);
                const double nv = std::round(((double)arrastoVfo0_ + desl) / p) * p;
                const double t = agoraS();
                if (nv > 1000 && (uint64_t)nv != r_.vfo() && t - ultTuneArrasto_ > 0.04) {
                    ultTuneArrasto_ = t;
                    if (r_.sintonizar((uint64_t)nv)) muteAte_ = t + 0.25;
                }
            }
        }
    }
    if (ImGui::IsItemDeactivated()) {
        if (arrastandoBw_) {
            // nada: a largura ja foi aplicada durante o arrasto
        } else if (arrastando_) {
            const double novo = centroVisual_;
            const uint64_t p = (uint64_t)std::max(1, passo_);
            const double nv = std::round(((double)arrastoVfo0_ - (io.MousePos.x - arrastoX0_) / arrastoW_ * span) / p) * p;
            if (nv > 1000) r_.sintonizar((uint64_t)nv);
            if (novo > 0) {
                r_.centralizar((uint64_t)std::llround(novo));
                alvoCentro_ = novo; alvoAte_ = agoraS() + 1.5;   // segura a imagem ate chegar o quadro novo
                muteAte_ = agoraS() + 0.25;
            }
        } else {
            const double hz = xParaHz(io.MousePos.x, x, w);
            if (hz > 0) sintonizar((uint64_t)hz, true);
        }
        arrastando_ = false;
        arrastandoBw_ = false;
    }
    if (sobre && io.MouseWheel != 0) {
        if (io.MouseWheel > 0) sintonizar(r_.vfo() + passo_, false);
        else if (r_.vfo() > (uint64_t)passo_) sintonizar(r_.vfo() - passo_, false);
    }
    return sobre;
}

void Ui::espectro(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0x07, 0x14, 0x0b, 255),
                                IM_COL32(0x07, 0x14, 0x0b, 255), IM_COL32(0x04, 0x0c, 0x06, 255), IM_COL32(0x04, 0x0c, 0x06, 255));
    const float range = std::clamp(wfRange_, 10.f, 120.f);
    auto dbY = [&](float db) { return y + h - std::clamp((db + range) / range, 0.f, 1.f) * h; };
    for (int i = 1; i < 10; ++i) dl->AddLine(ImVec2(x + w * i / 10, y), ImVec2(x + w * i / 10, y + h), IM_COL32(0x1a, 0x3a, 0x24, 255));
    for (int db = -(int)std::floor(range / 10) * 10; db <= 0; db += 10)
        dl->AddLine(ImVec2(x, dbY((float)db)), ImVec2(x + w, dbY((float)db)),
                    (db % 20 == 0) ? IM_COL32(0x2a, 0x5a, 0x35, 255) : IM_COL32(0x1a, 0x3a, 0x24, 255));

    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
    const int nb = (int)suave_.size();
    if (nb > 1 && r_.ligado()) {
        const double span = taxaMostrada_ / (1.0 + zoom_);
        const double binsPorHz = nb / (double)taxaMostrada_;
        // bin (fracionario) da borda esquerda da tela e bins por coluna
        const double bIni = ((centroVisual_ - span / 2) - (double)centroMostrado_) * binsPorHz + nb / 2.0;
        const int cols = std::max(2, (int)w);
        const double bPorCol = span * binsPorHz / cols;
        std::vector<ImVec2> linha(cols), linhaPico(cols);
        for (int c = 0; c < cols; ++c) {
            const int b0 = (int)std::floor(bIni + c * bPorCol);
            const int b1 = std::max(b0 + 1, (int)std::floor(bIni + (c + 1) * bPorCol));
            float m = -200, mp = -200;
            for (int b = std::max(0, b0); b < b1 && b < nb; ++b) { m = std::max(m, suave_[b]); mp = std::max(mp, picoHold_[b]); }
            linha[c] = ImVec2(x + c * w / (cols - 1), dbY(m));
            linhaPico[c] = ImVec2(linha[c].x, dbY(mp));
        }
        // preenchimento dourado com degrade
        for (int c = 0; c + 1 < cols; ++c) {
            const float yt = std::min(linha[c].y, linha[c + 1].y);
            const float a = 1.f - (yt - y) / h;
            dl->AddRectFilledMultiColor(ImVec2(linha[c].x, yt), ImVec2(linha[c + 1].x + 1, y + h),
                                        IM_COL32(255, 200, 0, (int)(20 + 120 * a)), IM_COL32(255, 200, 0, (int)(20 + 120 * a)),
                                        IM_COL32(255, 200, 0, 3), IM_COL32(255, 200, 0, 3));
        }
        dl->AddPolyline(linhaPico.data(), cols, IM_COL32(255, 225, 120, 102), 0, 1.0f);
        dl->AddPolyline(linha.data(), cols, IM_COL32(0xff, 0xc8, 0x00, 255), 0, 1.5f * s_);
    }
    // filtro e marca de sintonia
    const uint64_t v = r_.vfo();
    const int bw = r_.banda();
    const std::string m = r_.modo();
    double f0 = (double)v - bw / 2.0, f1 = (double)v + bw / 2.0;
    if (m == "USB") { f0 = (double)v; f1 = (double)v + bw; }
    if (m == "LSB") { f0 = (double)v - bw; f1 = (double)v; }
    const float x0 = (float)hzParaX(f0, x, w), x1 = (float)hzParaX(f1, x, w), xv = (float)hzParaX((double)v, x, w);
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(std::max(x1, x0 + 1), y + h), IM_COL32(255, 159, 59, 26));
    ImU32 cm = IM_COL32(0xff, 0x9f, 0x3b, 255);
    if (smAtual_ >= 0.67f) cm = IM_COL32(0xff, 0x1a, 0x1a, 255);
    else if (smAtual_ >= 0.60f) cm = IM_COL32(0xff, 0x6b, 0x6b, 255);
    dl->AddLine(ImVec2(xv, y), ImVec2(xv, y + h), cm, 1.5f * s_);
    // rotulos de dB
    ImGui::PushFont(f_.pequena);
    for (int db = -(int)std::floor(range / 10) * 10; db <= 0; db += 10) {
        char b[8]; std::snprintf(b, sizeof b, "%d", db);
        const ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddText(ImVec2(x + 33 * s_ - ts.x, dbY((float)db) - ts.y * 0.5f), IM_COL32(0x5f, 0xaf, 0x68, 255), b);
    }
    ImGui::PopFont();
    dl->PopClipRect();
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0x24, 0x61, 0x32, 255));

    if (memVisivel_) reguaMemorias(x, y, w, 16 * s_);
    espSobre_ = interacaoEspectro(x, y, w, h, "##esp");
    if (espSobre_) {
        // cruz com a frequencia sob o mouse
        const float mx = ImGui::GetIO().MousePos.x;
        dl->AddLine(ImVec2(mx, y), ImVec2(mx, y + h), IM_COL32(255, 255, 255, 60));
        const std::string t = fmtFreq((uint64_t)std::max(0.0, xParaHz(mx, x, w)));
        ImGui::PushFont(f_.freqMouse ? f_.freqMouse : f_.negrito);
        const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        dl->AddRectFilled(ImVec2(mx + 5 * s_, y + 3 * s_), ImVec2(mx + 13 * s_ + ts.x, y + 7 * s_ + ts.y), IM_COL32(0, 0, 0, 170), 3 * s_);
        dl->AddText(ImVec2(mx + 9 * s_, y + 5 * s_), IM_COL32(255, 255, 255, 255), t.c_str());
        ImGui::PopFont();
    }
}

void Ui::escala(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0x06, 0x0e, 0x09, 255));
    const double span = taxaMostrada_ / (1.0 + zoom_);
    double maior, menor;
    passosRegua(span, maior, menor);
    const double lo = centroVisual_ - span / 2, hi = centroVisual_ + span / 2;
    ImGui::PushFont(f_.pequena);
    for (double f = std::ceil(lo / menor) * menor; f <= hi; f += menor) {
        const float px = (float)hzParaX(f, x, w);
        const bool mj = std::fmod(std::fabs(f), maior) < 1.0;
        dl->AddLine(ImVec2(px, y), ImVec2(px, y + (mj ? 6 : 3) * s_), mj ? C_MID : C_DIM);
        if (mj) {
            char b[32];
            if (maior >= 1000000) std::snprintf(b, sizeof b, "%.0f", f / 1e6);
            else if (maior >= 100000) std::snprintf(b, sizeof b, "%.1f", f / 1e6);
            else if (maior >= 10000) std::snprintf(b, sizeof b, "%.2f", f / 1e6);
            else std::snprintf(b, sizeof b, "%.3f", f / 1e6);
            const ImVec2 ts = ImGui::CalcTextSize(b);
            dl->AddText(ImVec2(px - ts.x * 0.5f, y + h - ts.y - 1), C_TEXT, b);
        }
    }
    ImGui::PopFont();
}

void Ui::criarTexturaCachoeira(int largura)
{
    if (wfTex_) { wfTex_->Release(); wfTex_ = nullptr; }
    wfW_ = largura;
    if (dev_->CreateTexture(wfW_, wfH_, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &wfTex_, nullptr) != D3D_OK) {
        wfTex_ = nullptr; wfW_ = 0; return;
    }
    D3DLOCKED_RECT lr;
    if (wfTex_->LockRect(0, &lr, nullptr, 0) == D3D_OK) {
        const uint32_t fundo = corPaleta(0);
        for (int y = 0; y < wfH_; ++y) {
            uint32_t* d = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
            for (int x = 0; x < wfW_; ++x) d[x] = fundo;
        }
        wfTex_->UnlockRect(0);
    }
    wfPos_ = 0; wfLinhas_ = 0;
    wfDb_.assign((size_t)wfW_ * wfH_, (int8_t)-128);
}

// Tabela dB -> cor para o Range/Brilho atuais. Devolve true se mudou.
static bool montarLut(std::vector<uint32_t>& lut, float& lutRef, float& lutRange, float ref, float range)
{
    if (ref == lutRef && range == lutRange && !lut.empty()) return false;
    lut.resize(256);                               // indice = dB + 128
    for (int i = 0; i < 256; ++i) {
        const float db = (float)(i - 128);
        lut[i] = i == 0 ? corPaleta(0) : corPaleta((db - (ref - range)) / range);
    }
    lutRef = ref; lutRange = range;
    return true;
}

// Mexeu no Range/Brilho (ou AUTO): a cachoeira INTEIRA muda de cor, e nao so
// as linhas novas - era por isso que o PADRAO/AUTO "nao faziam nada".
void Ui::recolorirCachoeira()
{
    if (!wfTex_ || wfDb_.empty() || lut_.size() < 256) return;
    D3DLOCKED_RECT lr;
    if (wfTex_->LockRect(0, &lr, nullptr, 0) != D3D_OK) return;
    for (int y = 0; y < wfH_; ++y) {
        uint32_t* d = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
        const int8_t* o = &wfDb_[(size_t)y * wfW_];
        for (int x = 0; x < wfW_; ++x) d[x] = lut_[(int)o[x] + 128];
    }
    wfTex_->UnlockRect(0);
    recolorirIf();
}

// Uma linha nova no topo da cachoeira (paleta Eclipse, mapeamento linear)
void Ui::linhaCachoeira()
{
    const int nb = (int)fft_.bins.size();
    if (nb < 2) return;
    const int larg = std::min(nb, 4096);
    if (!wfTex_ || wfW_ != larg) criarTexturaCachoeira(larg);
    if (!wfTex_) return;
    if (montarLut(lut_, lutRef_, lutRange_, (wfBrilho_ - 100.f) * 0.5f, std::max(10.f, wfRange_)))
        recolorirCachoeira();
    wfPos_ = (wfPos_ + wfH_ - 1) % wfH_;       // a mais nova vai em cima
    RECT rc{0, wfPos_, wfW_, wfPos_ + 1};
    D3DLOCKED_RECT lr;
    if (wfTex_->LockRect(0, &lr, &rc, 0) != D3D_OK) return;
    uint32_t* d = (uint32_t*)lr.pBits;
    int8_t* o = &wfDb_[(size_t)wfPos_ * wfW_];
    const int grupo = nb / larg;
    for (int x = 0; x < larg; ++x) {
        // media dos bins do grupo (como o OpenWebRX+)
        float s = 0;
        for (int k = 0; k < grupo; ++k) s += fft_.bins[x * grupo + k];
        const int db = std::clamp((int)std::lround(s / grupo), -127, 127);
        o[x] = (int8_t)db;
        d[x] = lut_[db + 128];
    }
    wfTex_->UnlockRect(0);
    wfLinhas_ = std::min(wfLinhas_ + 1, wfH_);
}

void Ui::cachoeira(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 32, 255));   // cor 0 da paleta
    if (wfTex_ && r_.ligado()) {
        // faixa da textura (0..1) que cai na tela; fora dela, fundo
        const double span = taxaMostrada_ / (1.0 + zoom_);
        double u0 = ((centroVisual_ - span / 2) - (double)centroMostrado_) / taxaMostrada_ + 0.5;
        double u1 = ((centroVisual_ + span / 2) - (double)centroMostrado_) / taxaMostrada_ + 0.5;
        float xa = x, xb = x + w;
        if (u0 < 0) { xa = x + (float)((0 - u0) / (u1 - u0) * w); u0 = 0; }
        if (u1 > 1) { xb = x + w - (float)((u1 - 1) / (u1 - u0) * w); u1 = 1; }
        const float ux0 = (float)u0, ux1 = (float)u1;
        const int hd = std::min((int)h, wfH_);
        const float esc = h / hd;
        const int p = wfPos_;
        if (p + hd <= wfH_) {
            dl->AddImage(TEXID(wfTex_), ImVec2(xa, y), ImVec2(xb, y + h),
                         ImVec2(ux0, (float)p / wfH_), ImVec2(ux1, (float)(p + hd) / wfH_));
        } else {
            const int a = wfH_ - p;             // linhas do fim da textura
            dl->AddImage(TEXID(wfTex_), ImVec2(xa, y), ImVec2(xb, y + a * esc),
                         ImVec2(ux0, (float)p / wfH_), ImVec2(ux1, 1.f));
            dl->AddImage(TEXID(wfTex_), ImVec2(xa, y + a * esc), ImVec2(xb, y + h),
                         ImVec2(ux0, 0.f), ImVec2(ux1, (float)(hd - a) / wfH_));
        }
    }
    // filtro e marca de sintonia por cima
    const uint64_t v = r_.vfo();
    const int bw = r_.banda();
    const std::string m = r_.modo();
    double f0 = (double)v - bw / 2.0, f1 = (double)v + bw / 2.0;
    if (m == "USB") { f0 = (double)v; f1 = (double)v + bw; }
    if (m == "LSB") { f0 = (double)v - bw; f1 = (double)v; }
    dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, y + h), true);
    const float x0 = (float)hzParaX(f0, x, w), x1 = (float)hzParaX(f1, x, w), xv = (float)hzParaX((double)v, x, w);
    // Com o mouse no espectro, a marca da sintonia some da cachoeira (igual a
    // pagina): assim da para ver o sinal que esta embaixo dela.
    if (!espSobre_) {
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(std::max(x1, x0 + 1), y + h), IM_COL32(255, 159, 59, 22));
        ImU32 cm = IM_COL32(0xff, 0x9f, 0x3b, 200);
        if (smAtual_ >= 0.67f) cm = IM_COL32(0xff, 0x1a, 0x1a, 220);
        else if (smAtual_ >= 0.60f) cm = IM_COL32(0xff, 0x6b, 0x6b, 220);
        dl->AddLine(ImVec2(xv, y), ImVec2(xv, y + h), cm, 1.5f * s_);
    }
    dl->PopClipRect();

    if (interacaoEspectro(x, y, w, h, "##wf")) {
        const float mx = ImGui::GetIO().MousePos.x, my = ImGui::GetIO().MousePos.y;
        dl->AddLine(ImVec2(mx, y), ImVec2(mx, y + h), IM_COL32(255, 255, 255, 50));
        const std::string t = fmtFreq((uint64_t)std::max(0.0, xParaHz(mx, x, w)));
        ImGui::PushFont(f_.freqMouse ? f_.freqMouse : f_.negrito);
        const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        dl->AddRectFilled(ImVec2(mx + 6 * s_, my - ts.y - 6 * s_), ImVec2(mx + 16 * s_ + ts.x, my), IM_COL32(0, 0, 0, 185), 4 * s_);
        dl->AddText(ImVec2(mx + 11 * s_, my - ts.y - 3 * s_), IM_COL32(255, 255, 255, 255), t.c_str());
        ImGui::PopFont();
    }
    if (!r_.ligado()) {
        ImGui::PushFont(f_.negrito);
        const char* msg = r_.temDispositivo() ? "Rádio desligado - clique em OFF para ligar"
                                              : "Clique em OFF para ligar (ou escolha o aparelho em CONFIGURAÇÃO)";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(x + (w - ts.x) * 0.5f, y + h * 0.4f), C_DIM, msg);
        ImGui::PopFont();
    }
}

// ===========================================================================
//  Rodape
// ===========================================================================
void Ui::rodape(float y, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetIO().DisplaySize.x;
    dl->AddRectFilled(ImVec2(0, y), ImVec2(W, y + h), C_MENU);
    dl->AddLine(ImVec2(0, y), ImVec2(W, y), C_BORDER);
    const auto& som = r_.som();
    char b[512];
    const double span = taxaMostrada_ / (1.0 + zoom_);
    std::snprintf(b, sizeof b,
        "%s  |  %s  |  Q: %s  |  Span: %.0f kHz  |  FFT %d  |  Sinal: %.1f dB (SQL %s)  |  %s %s  |  %.3f Msps  |  CPU %.0f%%",
        fmtFreq(r_.vfo()).c_str(), r_.modo().c_str(), r_.qAtivo() ? "ON (HF)" : "OFF",
        span / 1000.0, (int)fft_.bins.size(), peakDb_, sqlAberto_ ? "aberto" : "fechado",
        r_.temDispositivo() ? r_.tipo().c_str() : "sem aparelho", r_.serial().c_str(),
        r_.taxa() / 1e6, cpu_);
    ImGui::PushFont(f_.pequena);
    const float ty = y + (h - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(8 * s_, ty), C_RX, b);
    char sm[128];
    if (som.ativo()) std::snprintf(sm, sizeof sm, "Som: fila %d ms | ajuste x%.3f | faltas %d", som.filaMs(), som.passo(), som.faltas());
    else std::snprintf(sm, sizeof sm, "Som: placa de som não abriu");
    const ImVec2 ts = ImGui::CalcTextSize(sm);
    dl->AddText(ImVec2(W - ts.x - 8 * s_, ty), som.faltas() > 0 ? C_WARN : IM_COL32(0x7f, 0xb0, 0xff, 255), sm);
    ImGui::PopFont();
}

// ===========================================================================
//  Logica
// ===========================================================================
std::string Ui::fmtFreq(uint64_t hz) const
{
    char b[32];
    std::snprintf(b, sizeof b, "%llu.%03llu.%03llu", (unsigned long long)(hz / 1000000),
                  (unsigned long long)(hz / 1000 % 1000), (unsigned long long)(hz % 1000));
    return b;
}

void Ui::sintonizar(uint64_t hz, bool arredondar)
{
    if (arredondar) {
        const uint64_t p = passo_ >= 1000 ? (uint64_t)passo_ : 10;
        hz = (hz + p / 2) / p * p;
    }
    if (hz < 1000) hz = 1000;
    if (r_.sintonizar(hz)) {
        // O dongle foi recentralizado - e o Radio poe o centro CRAVADO na
        // sintonia, justo onde mora o "apito" do DC. Tira de cima:
        //   com zoom : o centrarParaZoom poe o centro 1,5 kHz ao lado (fora do que se ouve)
        //   sem zoom : o centro vai 20 kHz para o lado (o DC fica longe do canal)
        muteAte_ = agoraS() + 0.25;
        const auto td = r_.decoders().tipo();
        const bool centroFixo = td == Decoders::HFDL || td == Decoders::ACARS || td == Decoders::VDL2;
        if (zoom_ > 0) centrarParaZoom(true);
        else if (!centroFixo && r_.modo() != "WFM" && r_.taxa() >= 200000) {
            const std::string m = r_.modo();
            r_.centralizar(m == "LSB" ? hz + 20000 : hz - 20000);   // USB/CW: o DC do lado rejeitado
        }
        return;
    }
    centrarParaZoom(false);                              // com zoom, nao deixa a estacao sair da tela
}

// Com zoom a janela visivel e estreita: a estacao sintonizada fica no MEIO
// dela. So nao fica cravada no centro do dongle - ali mora o vazamento do
// oscilador (o "apito"/risco do DC). O centro vai um pouco para o lado, de
// modo que o DC caia fora do que se ouve:
//   USB e CW : centro 20 kHz ABAIXO  -> o DC cai na banda lateral rejeitada, longe da marca
//   LSB      : centro 20 kHz ACIMA
//   AM / FM  : centro meia banda + 20 kHz ao lado -> fora do canal
// (era 1,5 kHz: o risco do DC ficava colado na marca da sintonia - pedido do autor)
//   WFM      : no centro (200 kHz de canal; o DC nao atrapalha)
// forcar = mexeu no zoom (recentraliza sempre que o centro nao estiver no
// lugar); senao so quando a estacao chega perto da borda do que se ve.
void Ui::centrarParaZoom(bool forcar)
{
    if (zoom_ <= 0 || !r_.temDispositivo()) return;
    {   // HFDL, ACARS e VDL2 precisam do centro que combinaram com o programa
        const auto td = r_.decoders().tipo();
        if (td == Decoders::HFDL || td == Decoders::ACARS || td == Decoders::VDL2) return;
    }
    const double vis = r_.taxa() / (1.0 + zoom_);
    const std::string m = r_.modo();
    int64_t d = 0;
    if (m == "USB" || m == "CW") d = -20000;
    else if (m == "LSB") d = 20000;
    else if (m != "WFM") d = -(int64_t)(r_.banda() / 2 + 20000);
    const int64_t teto = (int64_t)(vis * 0.15);          // nunca mais que 15% da tela para o lado
    d = std::clamp(d, -teto, teto);
    const int64_t vfo = (int64_t)r_.vfo();
    const int64_t alvo = vfo + d;
    const int64_t centro = (int64_t)r_.centro();
    const bool precisa = forcar ? std::llabs(centro - alvo) > 50
                                : std::llabs(vfo - centro) > vis * 0.42;
    if (!precisa || alvo <= 0) return;
    r_.centralizar((uint64_t)alvo);
    alvoCentro_ = (double)alvo; alvoAte_ = agoraS() + 1.5;   // segura a imagem ate chegar o quadro novo
    muteAte_ = agoraS() + 0.25;
}

void Ui::mudarModo(const std::string& m)
{
    if (m == r_.modo()) return;
    r_.setModo(m);
    for (int i = 0; i < 7; ++i) if (m == kModos[i]) r_.setBanda(kBwPadrao[i]);
    muteAte_ = agoraS() + 0.25;
}

void Ui::ligarDesligar()
{
    std::string e;
    if (!r_.ligar(!r_.ligado(), e)) {
        erro_ = "Não foi possível ligar: " + e + "\n\nConfira o aparelho em CONFIGURAÇÃO. No RTL-SDR o driver WinUSB "
                "tem de estar instalado (Zadig), como no SDR#.";
        erroAte_ = agoraS() + 8;
    } else if (r_.ligado()) {
        muteAte_ = agoraS() + 0.4;
    }
}

// Piso de ruido do que esta na tela: percentil 25 dos bins visiveis (sem as
// bordas de 10%). O minimo iria a um unico buraco; o maximo, a uma portadora.
bool Ui::medirPiso(float& piso) const
{
    const int nb = (int)fft_.bins.size();
    if (!nb || !r_.aoVivo()) return false;
    const int vis = std::max(2, (int)std::lround(nb / (1.0 + zoom_)));
    const int vi = std::max(0, (nb - vis) / 2), vf = std::min(nb, vi + vis);
    const int ini = std::max(vi, (int)std::floor(nb * 0.10)), fim = std::min(vf, (int)std::ceil(nb * 0.90));
    if (fim - ini < 8) return false;
    std::vector<int8_t> v(fft_.bins.begin() + ini, fft_.bins.begin() + fim);
    std::nth_element(v.begin(), v.begin() + v.size() / 4, v.end());
    piso = v[v.size() / 4];
    return true;
}

// AUTO (igual a pagina): mede o piso desta banda e reproduz a aparencia
// guardada pelo PADRAO. Sem espectro ainda: valores de abertura (53 / 106).
void Ui::autoCachoeira()
{
    float piso;
    if (medirPiso(piso)) {
        wfRange_ = std::clamp(std::round(wfRangeCal_), 20.f, 120.f);
        wfBrilho_ = std::clamp(std::round((piso + wfAcimaPiso_) * 2.f + 100.f), 0.f, 200.f);
    } else {
        wfRange_ = 53; wfBrilho_ = 106;
    }
    wfSpeed_ = std::max(wfSpeed_, 1.f);
    autoWf_ = true;
}

// Com AUTO aceso, acompanha o piso: se ele andar mais de 4 dB (troca de
// ganho, AGC, banda nova), reajusta o brilho sozinho. Parado, nao mexe.
void Ui::seguirAuto()
{
    if (!autoWf_) return;
    const double t = agoraS();
    if (t - ultSeguirAuto_ < 1.5) return;
    ultSeguirAuto_ = t;
    float piso;
    if (!medirPiso(piso)) return;
    const float alvo = std::clamp(std::round((piso + wfAcimaPiso_) * 2.f + 100.f), 0.f, 200.f);
    if (std::fabs(alvo - wfBrilho_) > 8.f) wfBrilho_ = alvo;
}

// PADRAO (igual a pagina): guarda a aparencia que esta na tela como alvo do AUTO
void Ui::padrao()
{
    float piso;
    if (medirPiso(piso)) {
        wfAcimaPiso_ = (wfBrilho_ - 100.f) * 0.5f - piso;
        wfRangeCal_ = wfRange_;
        auto& c = Config::instance();
        c.set("wf_acima_piso", (double)wfAcimaPiso_);
        c.set("wf_range_cal", (double)wfRangeCal_);
        c.salvar();
        padraoMsg_ = "OK";
    } else {
        padraoMsg_ = "SEM SINAL";
    }
    padraoMsgAte_ = agoraS() + 1.2;
}

void Ui::novoQuadroFft()
{
    QuadroFft q;
    if (!r_.pegarFft(q)) return;
    // O dongle mudou de centro (arrasto, ">.<", sintonia fora da janela): a
    // cachoeira antiga anda junto, para cada sinal continuar na sua coluna -
    // igual a pagina. Mudou a taxa: nao da para aproveitar, comeca limpa.
    if (wfTex_ && !wfDb_.empty() && dadosCentro_ != 0 && q.centro != dadosCentro_) {
        if (q.taxa == dadosTaxa_) {
            const double col = ((double)q.centro - (double)dadosCentro_) / (double)q.taxa * wfW_;
            const int d = (int)std::lround(col);
            if (d != 0) {
                for (int y = 0; y < wfH_; ++y) {
                    int8_t* o = &wfDb_[(size_t)y * wfW_];
                    if (std::abs(d) >= wfW_) { std::fill(o, o + wfW_, (int8_t)-128); continue; }
                    if (d > 0) { std::memmove(o, o + d, wfW_ - d); std::fill(o + wfW_ - d, o + wfW_, (int8_t)-128); }
                    else { std::memmove(o - d, o, wfW_ + d); std::fill(o, o - d, (int8_t)-128); }
                }
                recolorirCachoeira();
            }
        } else {
            std::fill(wfDb_.begin(), wfDb_.end(), (int8_t)-128);
            recolorirCachoeira();
        }
    }
    dadosCentro_ = q.centro; dadosTaxa_ = q.taxa;
    fft_ = std::move(q);
    centroMostrado_ = fft_.centro;
    taxaMostrada_ = fft_.taxa ? fft_.taxa : taxaMostrada_;
    const int nb = (int)fft_.bins.size();
    const double t = agoraS();
    if ((int)suave_.size() != nb) {
        suave_.assign(nb, -120.f);
        picoHold_.assign(nb, -200.f);
        for (int i = 0; i < nb; ++i) suave_[i] = fft_.bins[i];
    }
    // suavizacao por TEMPO (40 ms subindo, 130 ms descendo), como na pagina
    static double tAnt = t;
    double d = (t - tAnt) * 1000.0; tAnt = t;
    if (!(d > 0)) d = 33; if (d > 250) d = 250;
    const float aS = 1.f - (float)std::exp(-d / 40.0), aD = 1.f - (float)std::exp(-d / 130.0);
    for (int i = 0; i < nb; ++i) {
        const float v = fft_.bins[i];
        suave_[i] += (v - suave_[i]) * (v > suave_[i] ? aS : aD);
        picoHold_[i] = std::max(picoHold_[i] - (float)(d * 0.004), v);   // pico que desce devagar
    }
    // S-meter: pico dentro do filtro (updateFromFft da pagina)
    const uint64_t v = r_.vfo();
    const int bw = r_.banda();
    const std::string m = r_.modo();
    double f0 = (double)v, f1 = (double)v;
    if (m == "USB") f1 = (double)v + bw;
    else if (m == "LSB") f0 = (double)v - bw;
    else { f0 = (double)v - bw / 2.0; f1 = (double)v + bw / 2.0; }
    const double sr = fft_.taxa ? fft_.taxa : 1024000;
    auto bin = [&](double f) { return std::clamp((int)std::lround((f - (double)fft_.centro) / sr * nb + nb / 2.0), 0, nb - 1); };
    int a = bin(f0), b = bin(f1);
    if (a > b) std::swap(a, b);
    const int cb = nb / 2;
    const bool noDc = std::fabs((double)v - (double)fft_.centro) < 2000;
    float pk = -200;
    for (int i = a; i <= b; ++i) {
        if (!noDc && std::abs(i - cb) <= 1 && (b - a) > 2) continue;
        pk = std::max(pk, (float)fft_.bins[i]);
    }
    pk = (pk == -200) ? -120.f : std::min(0.f, pk + 15.f);
    // calibracao (calibrateDb / calibrateDbSemPiso da pagina)
    const auto& c = Config::instance();
    const bool hf = r_.qAtivo() || v < 30000000ULL;
    const float s9Ref = (float)(hf ? c.num("s9_hf", -94) : c.num("s9_vhf", -93));
    const float desloc = s9Ref - (hf ? -94.f : -93.f);
    float dbCal = pk - desloc;
    peakDbSql_ = std::clamp(dbCal, -120.f, 0.f);
    // piso: tabela rms->dB com hfEmpty = 36
    const float er = 36.f;
    const float tab[10][2] = {{5, -110}, {8, -45}, {14.5f, -20}, {28.5f, -14}, {45, -9}, {60.5f, -6}, {67.5f, -3.5f}, {78.5f, -1.5f}, {89.5f, -0.5f}, {96, 0}};
    float alvo = -20;
    for (int i = 0; i < 9; ++i)
        if (er >= tab[i][0] && er <= tab[i + 1][0]) { alvo = tab[i][1] + (er - tab[i][0]) / (tab[i + 1][0] - tab[i][0]) * (tab[i + 1][1] - tab[i][1]); break; }
    if (hf) {
        if (hfPiso_ > 1e8) hfPiso_ = dbCal;
        else if (dbCal < hfPiso_) hfPiso_ += (dbCal - hfPiso_) * 0.25;
        else hfPiso_ += (dbCal - hfPiso_) * 0.003;
        const double shift = alvo - hfPiso_, delta = std::max(0.0, dbCal - hfPiso_);
        dbCal = (float)(dbCal + shift * std::exp(-delta / 8.0));
    } else if (dbCal <= alvo) {
        dbCal = alvo;
    }
    peakDb_ = std::clamp(dbCal, -120.f, 0.f);
    sqlAberto_ = (sql_ <= -90) || (peakDbSql_ >= sql_);
    const float alvoPont = !r_.ligado() ? 0.01f : !sqlAberto_ ? 0.01f : dbParaPonteiro(peakDb_);
    const double agora = agoraS();
    smHist_.emplace_back(agora, alvoPont);
    size_t velhos = 0;
    while (velhos < smHist_.size() && smHist_[velhos].first < agora - 4.0) ++velhos;
    if (velhos) smHist_.erase(smHist_.begin(), smHist_.begin() + velhos);
}

void Ui::atualizarSmeter(float dt)
{
    // O ponteiro segue o que se OUVE: o nivel medido agora so e mostrado quando
    // o audio daquele instante sai na placa (o som passa pela fila da placa,
    // que com rtl_tcp por Wi-Fi chega a ~1 s). Sem isso o ponteiro ia na frente.
    {
        const double atraso = std::min(3.0, r_.som().filaMs() / 1000.0 + 0.03);
        const double quando = agoraS() - atraso;
        for (auto it = smHist_.rbegin(); it != smHist_.rend(); ++it)
            if (it->first <= quando) { smAlvo_ = it->second; break; }
    }
    if (!r_.aoVivo()) smAlvo_ = r_.ligado() ? 0.05f : 0.01f;
    // subida como a pagina (0,28 por quadro a 60 q/s); a volta leva smRetorno_
    // segundos (ajustavel na CONFIGURACAO - 0,9 s era o valor fixo de antes)
    const float k = dt * 60.f;
    const float a = smAlvo_ > smAtual_ ? 1.f - std::pow(1.f - 0.28f, k)
                                       : 1.f - std::exp(-dt / std::max(0.05f, smRetorno_));
    smAtual_ += (smAlvo_ - smAtual_) * a;
    // olho magico
    const float alvoOlho = (r_.ligado() && !mudo_) ? r_.som().nivel() : 0.f;
    olho_ += (alvoOlho - olho_) * (alvoOlho > olho_ ? 1.f - std::pow(0.65f, k) : 1.f - std::pow(0.88f, k));
}

// Monta o ajuste do som e manda ao SomNativo so quando muda
void Ui::atualizarSom()
{
    AjusteAudio a;
    // Com o DMR tocando, a voz vem do dsd-fme: o squelch do FM nao pode cala-la.
    const bool dmr = r_.decoders().substituiAudio();
    a.mudo = mudo_ || (!sqlAberto_ && !dmr) || agoraS() < muteAte_ || !r_.ligado();
    a.volume = vol_ <= 0 ? 0.f : (float)(std::pow(vol_ / 100.0, 1.5) * 8.0);
    a.nb = nb_ / 100.f;
    a.nrEsp = nrEsp_;
    a.nrEspForca = (int)std::lround(nrEspForca_);
    a.nr = nr_ / 100.f;
    a.tom = (int)std::lround(tom_);
    if (ajPrimeiro_ || a.mudo != ajEnviado_.mudo || a.volume != ajEnviado_.volume || a.nb != ajEnviado_.nb ||
        a.nrEsp != ajEnviado_.nrEsp || a.nrEspForca != ajEnviado_.nrEspForca || a.nr != ajEnviado_.nr || a.tom != ajEnviado_.tom) {
        r_.som().ajustar(a);
        ajEnviado_ = a;
        ajPrimeiro_ = false;
    }
}

// ===========================================================================
//  Janelas
// ===========================================================================
void Ui::janelaConfig()
{
    ImGui::SetNextWindowSize(ImVec2(520 * s_, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Configuração", &cfgAberta_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }
    const float lw = 170 * s_;
    auto rot = [&](const char* t) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(t); ImGui::SameLine(lw); ImGui::SetNextItemWidth(300 * s_); };

    ImGui::SeparatorText("Aparelho");
    const char* tipos[] = {"RTL-SDR (USB)", "RTL-TCP (rede)", "SDRplay"};
    rot("Tipo");
    if (ImGui::Combo("##tipo", &cfgTipo_, tipos, 3) && cfgTipo_ == 2) {
        // SDRplay escolhido agora: procura os aparelhos dela (so aqui carrega a API)
        auto extra = Radio::listar("rtlsdr", "-", true);
        for (const auto& d : extra) if (d.tipo == "sdrplay") lista_.push_back(d);
    }
    if (cfgTipo_ == 1) {
        rot("Endereço (IP)"); ImGui::InputText("##host", cfgHost_, sizeof cfgHost_);
        rot("Porta"); ImGui::InputInt("##porta", &cfgPorta_, 0);
    } else {
        const std::string alvo = cfgTipo_ == 0 ? "rtlsdr" : "sdrplay";
        std::vector<int> idx;
        for (int i = 0; i < (int)lista_.size(); ++i) if (lista_[i].tipo == alvo) idx.push_back(i);
        std::string atual = idx.empty() ? std::string("(nenhum encontrado)") : std::string();
        int sel = 0;
        for (int k = 0; k < (int)idx.size(); ++k) if (idx[k] == cfgDisp_) sel = k;
        if (!idx.empty()) atual = lista_[idx[sel]].nome + "  [" + lista_[idx[sel]].serial + "]";
        rot("Dispositivo");
        if (ImGui::BeginCombo("##disp", atual.c_str())) {
            for (int k = 0; k < (int)idx.size(); ++k) {
                const auto& d = lista_[idx[k]];
                const std::string t = d.nome + "  [" + d.serial + "]";
                if (ImGui::Selectable(t.c_str(), k == sel)) cfgDisp_ = idx[k];
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Procurar")) {
            lista_ = Radio::listar(r_.temDispositivo() ? r_.tipo() : std::string(), r_.serial(), cfgTipo_ == 2);
            if (r_.temDispositivo() && r_.tipo() != "rtltcp") {
                bool achou = false;
                for (const auto& d : lista_) if (d.tipo == r_.tipo() && d.serial == r_.serial()) achou = true;
                if (!achou) lista_.insert(lista_.begin(), DispositivoInfo{r_.tipo(), r_.serial(), "(em uso)"});
            }
        }
    }

    ImGui::SeparatorText("Recepção");
    char tx[64];
    std::snprintf(tx, sizeof tx, "%.3f Msps", kTaxas[cfgTaxa_] / 1e6);
    rot("Taxa de amostragem");
    if (ImGui::BeginCombo("##taxa", tx)) {
        for (int i = 0; i < (int)(sizeof kTaxas / sizeof kTaxas[0]); ++i) {
            std::snprintf(tx, sizeof tx, "%.3f Msps (janela de %.3f MHz)", kTaxas[i] / 1e6, kTaxas[i] / 1e6);
            if (ImGui::Selectable(tx, i == cfgTaxa_)) cfgTaxa_ = i;
        }
        ImGui::EndCombo();
    }
    const char* qs[] = {"Desligada (VHF/UHF)", "Ligada (HF)", "Automática (abaixo de 24 MHz)"};
    rot("Amostragem direta (Q)"); ImGui::Combo("##q", &cfgQ_, qs, 3);
    const char* ffts[] = {"1024", "2048", "4096", "8192", "16384"};
    rot("Tamanho da FFT"); ImGui::Combo("##fft", &cfgFft_, ffts, 5);
    if (cfgTipo_ != 2) {
        rot("AGC do dongle"); ImGui::Checkbox("##agc", &cfgAgc_);
        if (!cfgAgc_) { rot("Ganho (dB)"); ImGui::SliderFloat("##g", &cfgGanho_, 0, 49.6f, "%.1f dB"); }
        rot("Correção PPM");
        ImGui::SetNextItemWidth(190 * s_);
        ImGui::SliderInt("##ppm", &cfgPpm_, -150, 150, "");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(102 * s_);
        if (ImGui::InputInt("##ppmn", &cfgPpm_, 1, 10)) cfgPpm_ = std::clamp(cfgPpm_, -150, 150);
        rot("Bias-T (alimenta LNA)"); ImGui::Checkbox("##bias", &cfgBias_);
    } else {
        rot("LNA (0-9)"); ImGui::SliderInt("##lna", &cfgLna_, 0, 9);
        rot("Ganho FI (dB)"); ImGui::SliderInt("##ifg", &cfgIfGain_, 20, 59);
        rot("AGC da FI"); ImGui::Checkbox("##ifagc", &cfgIfAgc_);
    }
    ImGui::SeparatorText("Som");
    {
        const char* atual = cfgSaida_ <= 0 || cfgSaida_ > (int)saidas_.size() ? "Padrão do Windows" : saidas_[cfgSaida_ - 1].c_str();
        rot("Placa de som (saída)");
        if (ImGui::BeginCombo("##saida", atual)) {
            if (ImGui::Selectable("Padrão do Windows", cfgSaida_ == 0)) cfgSaida_ = 0;
            for (int i = 0; i < (int)saidas_.size(); ++i)
                if (ImGui::Selectable(saidas_[i].c_str(), cfgSaida_ == i + 1)) cfgSaida_ = i + 1;
            ImGui::EndCombo();
        }
    }
    ImGui::SeparatorText("S-meter");
    rot("Referência S9 em HF"); ImGui::SliderInt("##s9h", &cfgS9Hf_, -120, -50, "%d dBm");
    rot("Referência S9 em VHF"); ImGui::SliderInt("##s9v", &cfgS9Vhf_, -120, -50, "%d dBm");
    rot("Retorno do ponteiro");
    if (ImGui::SliderFloat("##smret", &smRetorno_, 0.1f, 5.f, "%.1f s")) {
        Config::instance().set("smeter_retorno", (double)smRetorno_);
        if (!ImGui::IsItemActive()) Config::instance().salvar();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) Config::instance().salvar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Quanto tempo o ponteiro leva para voltar quando o sinal some\n"
                                                  "(ex.: quando o squelch fecha em NFM). Mais alto = volta mais devagar.");

    // ---- Ajustes ao vivo: Recepcao, Som e S-meter valem enquanto se mexe ----
    {
        char sig[160];
        std::snprintf(sig, sizeof sig, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d",
                      cfgTaxa_, cfgQ_, cfgFft_, (int)cfgAgc_, (int)std::lround(cfgGanho_ * 10), cfgPpm_,
                      (int)cfgBias_, cfgLna_, cfgIfGain_, (int)cfgIfAgc_, cfgS9Hf_, cfgS9Vhf_, cfgSaida_);
        if (cfgAssin_.empty()) cfgAssin_ = sig;                       // janela acabou de abrir
        else if (cfgAssin_ != sig && agoraS() - cfgUltAplic_ > 0.12) {  // no maximo ~8 vezes por segundo
            cfgAssin_ = sig; cfgUltAplic_ = agoraS();
            gravarAjustes();
            r_.aplicarConfig();
        }
    }

    // ---- resposta do teste do rtl_tcp (feito em segundo plano) ----
    if (tcpTeste_ && tcpTeste_->estado.load() != 0) {
        auto t = tcpTeste_; tcpTeste_.reset();
        if (t->estado.load() > 0) {
            if (trocarAparelho("rtltcp", tcpTesteAlvo_)) {
                erro_ = "Conectado ao rtl_tcp " + tcpTesteAlvo_ + (t->info.empty() ? "" : "  (" + t->info + ")");
                erroAte_ = agoraS() + 4;
                cfgAberta_ = false;
            }
        } else {
            cfgMsg_ = "RTL-TCP " + tcpTesteAlvo_ + ": " + t->erro; cfgMsgErro_ = true;
        }
    }
    const bool testando = (bool)tcpTeste_;

    ImGui::Separator();
    ImGui::BeginDisabled(testando);
    if (ImGui::Button(testando ? "Conectando..." : "Aplicar", ImVec2(120 * s_, 0))) {
        auto& c = Config::instance();
        gravarAjustes();
        const std::string tipoNovo = cfgTipo_ == 0 ? "rtlsdr" : cfgTipo_ == 1 ? "rtltcp" : "sdrplay";
        std::string serialNovo;
        if (cfgTipo_ == 1) {
            // aceita "192.168.3.203:8728" digitado direto no campo do endereco
            std::string h = cfgHost_;
            h.erase(0, h.find_first_not_of(" \t"));
            h.erase(h.find_last_not_of(" \t") + 1);
            const auto dp = h.rfind(':');
            if (dp != std::string::npos && dp > 0) {
                const int p = std::atoi(h.c_str() + dp + 1);
                if (p > 0 && p <= 65535) cfgPorta_ = p;
                h.resize(dp);
            }
            std::snprintf(cfgHost_, sizeof cfgHost_, "%s", h.c_str());
            c.set("rtltcp_host", h);
            c.set("rtltcp_port", cfgPorta_);
            serialNovo = h + ":" + std::to_string(cfgPorta_);
        } else if (cfgDisp_ >= 0 && cfgDisp_ < (int)lista_.size() && lista_[cfgDisp_].tipo == tipoNovo) {
            serialNovo = lista_[cfgDisp_].serial;
        }
        const bool trocou = !r_.temDispositivo() || tipoNovo != r_.tipo() ||
                            (!serialNovo.empty() && serialNovo != r_.serial() && tipoNovo != "rtltcp") ||
                            (tipoNovo == "rtltcp" && serialNovo != r_.serial());
        cfgMsg_.clear();
        if (!trocou) {
            r_.aplicarConfig();
            c.salvar();
            cfgAberta_ = false;
        } else if (tipoNovo == "rtltcp") {
            // Testa primeiro numa thread: caixa desligada nao congela a tela.
            tcpTesteAlvo_ = serialNovo;
            auto t = std::make_shared<TesteTcp>();
            tcpTeste_ = t;
            std::thread([t, alvo = serialNovo] {
                std::string e, info;
                const bool ok = RtlTcpClient::testar(alvo, 3000, e, info);
                t->erro = e; t->info = info;
                t->estado.store(ok ? 1 : -1);
            }).detach();
            cfgMsg_ = "Conectando a " + serialNovo + " ..."; cfgMsgErro_ = false;
        } else if (trocarAparelho(tipoNovo, serialNovo)) {
            cfgAberta_ = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Fechar", ImVec2(120 * s_, 0))) { Config::instance().salvar(); cfgAberta_ = false; }
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", Config::instance().caminho().c_str());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 490 * s_);
    if (!cfgMsg_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, cfgMsgErro_ ? ImVec4(1.f, 0.42f, 0.42f, 1.f) : ImVec4(0.22f, 0.75f, 0.84f, 1.f));
        ImGui::TextWrapped("%s", cfgMsg_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("Recepção, Som e S-meter valem na hora, enquanto você mexe. "
                        "\"Aplicar\" só é preciso para trocar de aparelho ou de endereço.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}

// Campos da janela -> Config (nao mexe no aparelho)
void Ui::gravarAjustes()
{
    auto& c = Config::instance();
    c.set("sample_rate", (long long)kTaxas[cfgTaxa_]);
    c.set("qmode", std::string(cfgQ_ == 0 ? "off" : cfgQ_ == 1 ? "on" : "auto"));
    c.set("fft_size", kFfts[cfgFft_]);
    c.set("agc", cfgAgc_); c.set("bias_t", cfgBias_); c.set("ppm", cfgPpm_);
    c.set("gain", (int)std::lround(cfgGanho_ * 10));
    ganhoRf_ = std::clamp(100.f - cfgGanho_ * 10 / 4.96f, 0.f, 100.f);
    c.set("sdrplay_lna", cfgLna_); c.set("sdrplay_if_gain", cfgIfGain_); c.set("sdrplay_if_agc", cfgIfAgc_);
    c.set("s9_hf", cfgS9Hf_); c.set("s9_vhf", cfgS9Vhf_);
    const std::string saida = cfgSaida_ > 0 && cfgSaida_ <= (int)saidas_.size() ? saidas_[cfgSaida_ - 1] : std::string();
    if (saida != c.str("audio_saida")) r_.trocarSaida(saida);
    if (!ImGui::IsAnyItemActive()) c.salvar();        // grava quando solta o slider
}

// Abre outro aparelho; se deu certo ja liga (quem escolheu quer ouvir).
bool Ui::trocarAparelho(const std::string& tipo, const std::string& serial)
{
    std::string e;
    if (!r_.selecionar(tipo, serial, e)) {
        cfgMsg_ = "Não foi possível abrir o aparelho: " + e; cfgMsgErro_ = true;
        return false;
    }
    r_.ligar(true, e);
    Config::instance().salvar();
    return true;
}

// ===========================================================================
//  Memorias (bookmarks.json - o mesmo formato do OpenWebRX e do RXSDR)
// ===========================================================================
void Ui::carregarMemorias()
{
    mem_.clear();
    std::ifstream f(pastaDoExe() + "\\bookmarks.json", std::ios::binary);
    if (!f) return;
    std::stringstream ss; ss << f.rdbuf();
    const Json j = Json::parse(ss.str());
    if (!j.isArray()) return;
    for (size_t i = 0; i < j.size(); ++i) {
        const Json& o = j[i];
        Memoria m;
        m.nome = o["name"].getString();
        m.f = (uint64_t)o["frequency"].getInt(0);
        m.modo = modoDaMemoria(o["modulation"].getString("nfm"));
        m.desc = o["description"].getString();
        m.modOriginal = o["modulation"].getString();
        m.alvo = o["alvo"].getString();
        m.pais = o["pais"].getString();
        if (o.contains("bc") && o["bc"].isBool()) m.bc = o["bc"].getBool() ? 1 : 0;
        const Json& hr = o["hor"];
        if (hr.isArray())
            for (size_t k = 0; k < hr.size(); ++k)
                if (hr[k].isArray() && hr[k].size() >= 2)
                    m.hor.push_back({(int)hr[k][(size_t)0].getInt(), (int)hr[k][(size_t)1].getInt()});
        if (m.f > 0) mem_.push_back(m);
    }
    std::sort(mem_.begin(), mem_.end(), [](const Memoria& a, const Memoria& b) { return a.f < b.f; });
    Logger::info("Memorias: " + std::to_string(mem_.size()));
}

void Ui::salvarMemorias()
{
    Json arr = Json::array();
    for (const auto& m : mem_) {
        Json o = Json::object();
        std::string mod = m.modo;
        for (auto& c : mod) c = (char)std::tolower((unsigned char)c);
        o["name"] = Json(m.nome);
        o["frequency"] = Json((long long)m.f);
        o["modulation"] = Json(!m.modOriginal.empty() && modoDaMemoria(m.modOriginal) == m.modo ? m.modOriginal : mod);
        o["underlying"] = Json("");
        o["description"] = Json(m.desc);
        o["scannable"] = Json(false);
        if (!m.hor.empty()) {
            Json hr = Json::array();
            for (const auto& p : m.hor) { Json par = Json::array(); par.push(Json(p.first)); par.push(Json(p.second)); hr.push(par); }
            o["hor"] = hr;
        }
        if (!m.alvo.empty()) o["alvo"] = Json(m.alvo);
        if (!m.pais.empty()) o["pais"] = Json(m.pais);
        if (m.bc >= 0) o["bc"] = Json(m.bc == 1);
        arr.push(o);
    }
    const std::string cam = pastaDoExe() + "\\bookmarks.json";
    std::ofstream f(cam + ".tmp", std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << arr.serialize();
    f.close();
    MoveFileExA((cam + ".tmp").c_str(), cam.c_str(), MOVEFILE_REPLACE_EXISTING);
    memSujo_ = false;
}

// Faixas de radiodifusao da UIT (kHz) - as mesmas da pagina
static const double FAIXAS_BC[][2] = {
    {148.5, 283.5}, {526.5, 1710}, {2300, 2495}, {3200, 3400}, {3900, 4000}, {4750, 5060},
    {5800, 6200}, {7200, 7600}, {9400, 9900}, {11600, 12100}, {13570, 13870}, {15100, 15830},
    {17480, 17900}, {18900, 19020}, {21450, 21850}, {25670, 26100}, {87500, 108000}};

// Mesma regra da pagina: a marca "bc" manda; sem ela, AM/DRM abaixo de 30 MHz
// e radiodifusao; dentro das faixas BC, AM/FM/WFM/DRM (ou sem modo) tambem.
bool Ui::ehBroadcast(const Memoria& m) const
{
    if (m.bc >= 0) return m.bc == 1;
    std::string mod = m.modOriginal;
    for (auto& c : mod) c = (char)std::tolower((unsigned char)c);
    if (m.f < 30000000ULL && (mod == "am" || mod == "drm")) return true;
    const double k = m.f / 1000.0;
    bool naFaixa = false;
    for (const auto& fx : FAIXAS_BC) if (k >= fx[0] && k <= fx[1]) { naFaixa = true; break; }
    if (!naFaixa) return false;
    return mod.empty() || mod == "am" || mod == "wfm" || mod == "fm" || mod == "drm";
}

static int horaUtcHhmm()
{
    static int cache = -1; static double quando = -10;
    const double ag = agoraS();
    if (ag - quando > 1.0) {
        SYSTEMTIME st; GetSystemTime(&st);
        cache = st.wHour * 100 + st.wMinute; quando = ag;
    }
    return cache;
}

bool Ui::passaFiltro(const Memoria& m) const
{
    switch (memFiltroTipo_) {
    case 1: return !ehBroadcast(m);           // somente utilitarias
    case 4: return ehBroadcast(m);            // broadcast - todas
    case 2: case 3: {                         // broadcast no ar agora (UTC)
        if (!ehBroadcast(m)) return false;
        const int t = horaUtcHhmm();
        bool noAr = m.hor.empty();
        for (const auto& p : m.hor) {
            const int a = p.first, b = p.second;
            if (a == b || (a < b && t >= a && t < b) || (a > b && (t >= a || t < b))) { noAr = true; break; }
        }
        if (!noAr) return false;
        if (memFiltroTipo_ == 3 && !m.alvo.empty()) {   // mirando as Americas
            size_t i = 0;
            while (i <= m.alvo.size()) {
                size_t j = m.alvo.find('/', i);
                if (j == std::string::npos) j = m.alvo.size();
                const std::string a = m.alvo.substr(i, j - i);
                if (a == "SAm" || a == "LAm" || a == "CAm" || a == "NAm" || a == "Am") return true;
                i = j + 1;
            }
            return false;
        }
        return true;
    }
    default: return true;
    }
}

// Menu MEMORIAS (os mesmos itens da pagina)
void Ui::menuMemorias()
{
    if (!ImGui::BeginPopup("menuMem")) return;
    if (ImGui::MenuItem("Criar a partir do VFO atual")) {
        memAberta_ = true;
        std::snprintf(memNome_, sizeof memNome_, "%s %s", fmtFreq(r_.vfo()).c_str(), r_.modo().c_str());
    }
    if (ImGui::MenuItem("Editar / excluir…")) memAberta_ = true;
    ImGui::Separator();
    for (int i = 0; i < 5; ++i)
        if (ImGui::MenuItem(kFiltrosMem[i], nullptr, memFiltroTipo_ == i)) { memFiltroTipo_ = i; memVisivel_ = true; }
    ImGui::Separator();
    if (ImGui::MenuItem(memVisivel_ ? "Ocultar a régua" : "Mostrar a régua")) memVisivel_ = !memVisivel_;
    ImGui::EndPopup();
}

void Ui::sintonizarMemoria(size_t i)
{
    if (i >= mem_.size()) return;
    mudarModo(mem_[i].modo);
    sintonizar(mem_[i].f, false);
}

// Nomes das memorias em cima do espectro, na frequencia de cada uma. Clique
// sintoniza (frequencia e modo). Com muitas juntas, pula as que encavalariam.
void Ui::reguaMemorias(float x, float y, float w, float h)
{
    if (mem_.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const double span = taxaMostrada_ / (1.0 + zoom_);
    const double lo = centroVisual_ - span / 2, hi = centroVisual_ + span / 2;
    auto it = std::lower_bound(mem_.begin(), mem_.end(), (uint64_t)std::max(0.0, lo),
                               [](const Memoria& m, uint64_t f) { return m.f < f; });
    ImGui::PushFont(f_.pequena);
    float fimUltimo = -1e9f;
    int n = 0;
    for (; it != mem_.end() && (double)it->f <= hi && n < 400; ++it) {
        if (!passaFiltro(*it)) continue;
        ++n;
        const float px = (float)hzParaX((double)it->f, x, w);
        std::string rot = it->nome.size() > 22 ? it->nome.substr(0, 21) + "…" : it->nome;
        const ImVec2 ts = ImGui::CalcTextSize(rot.c_str());
        const float lx = px + 2 * s_;
        dl->AddLine(ImVec2(px, y), ImVec2(px, y + h + 6 * s_), IM_COL32(0x7f, 0xb0, 0xff, 140));
        if (lx < fimUltimo + 4 * s_) continue;          // encavalaria: so o risquinho
        fimUltimo = lx + ts.x + 6 * s_;
        dl->AddRectFilled(ImVec2(lx - 1, y + 1), ImVec2(lx + ts.x + 5 * s_, y + h - 1), IM_COL32(0x08, 0x18, 0x30, 210), 3 * s_);
        ImGui::SetCursorScreenPos(ImVec2(lx - 1, y + 1));
        ImGui::PushID((int)(it - mem_.begin()));
        const bool clicou = ImGui::InvisibleButton("##mr", ImVec2(ts.x + 6 * s_, h - 2));
        const bool sobre = ImGui::IsItemHovered();
        ImGui::PopID();
        dl->AddText(ImVec2(lx + 2 * s_, y + (h - ts.y) * 0.5f), sobre ? IM_COL32_WHITE : IM_COL32(0x9f, 0xc8, 0xff, 255), rot.c_str());
        if (sobre) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(it->nome.c_str());
            ImGui::Text("%s kHz  %s", fmtFreq(it->f).c_str(), it->modo.c_str());
            if (!it->desc.empty()) ImGui::TextDisabled("%s", it->desc.c_str());
            ImGui::EndTooltip();
        }
        if (clicou) sintonizarMemoria((size_t)(it - mem_.begin()));
    }
    ImGui::PopFont();
}

void Ui::janelaMemorias()
{
    ImGui::SetNextWindowSize(ImVec2(560 * s_, 460 * s_), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Memórias", &memAberta_, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    ImGui::Checkbox("Mostrar os nomes em cima do espectro", &memVisivel_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230 * s_);
    ImGui::Combo("##filtroTipo", &memFiltroTipo_, kFiltrosMem, 5);
    ImGui::SetNextItemWidth(260 * s_);
    ImGui::InputTextWithHint("##filtro", "procurar (nome ou kHz)", memFiltro_, sizeof memFiltro_);
    ImGui::SameLine();
    ImGui::TextDisabled("%d memórias", (int)mem_.size());

    // nova memoria com a sintonia atual
    ImGui::SetNextItemWidth(260 * s_);
    ImGui::InputTextWithHint("##nome", "nome para a frequência atual", memNome_, sizeof memNome_);
    ImGui::SameLine();
    if (ImGui::Button("Salvar atual") && memNome_[0]) {
        Memoria m; m.nome = memNome_; m.f = r_.vfo(); m.modo = r_.modo();
        mem_.insert(std::upper_bound(mem_.begin(), mem_.end(), m, [](const Memoria& a, const Memoria& b) { return a.f < b.f; }), m);
        memNome_[0] = 0; memSujo_ = true; salvarMemorias();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apagar selecionada") && memSel_ >= 0 && memSel_ < (int)mem_.size()) {
        mem_.erase(mem_.begin() + memSel_); memSel_ = -1; memSujo_ = true; salvarMemorias();
    }
    ImGui::Separator();

    std::string filtro = memFiltro_;
    for (auto& c : filtro) c = (char)std::tolower((unsigned char)c);
    if (ImGui::BeginTable("##mem", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("kHz", ImGuiTableColumnFlags_WidthFixed, 110 * s_);
        ImGui::TableSetupColumn("Modo", ImGuiTableColumnFlags_WidthFixed, 50 * s_);
        ImGui::TableHeadersRow();
        std::vector<int> lista;
        for (int i = 0; i < (int)mem_.size(); ++i) {
            if (!passaFiltro(mem_[i])) continue;
            if (!filtro.empty()) {
                std::string n = mem_[i].nome + " " + mem_[i].desc + " " + std::to_string(mem_[i].f / 1000);
                for (auto& c : n) c = (char)std::tolower((unsigned char)c);
                if (n.find(filtro) == std::string::npos) continue;
            }
            lista.push_back(i);
        }
        ImGuiListClipper clip;
        clip.Begin((int)lista.size());
        while (clip.Step()) {
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
                const int i = lista[k];
                const auto& m = mem_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                if (ImGui::Selectable(m.nome.c_str(), memSel_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    memSel_ = i;
                    sintonizarMemoria((size_t)i);
                }
                if (ImGui::IsItemHovered() && !m.desc.empty()) ImGui::SetTooltip("%s", m.desc.c_str());
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f", m.f / 1000.0);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(m.modo.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ===========================================================================
//  Gravacao do audio (WAV) - fica na pasta "Gravacoes" ao lado do programa
// ===========================================================================
void Ui::alternarGravacao()
{
    SomNativo& som = r_.som();
    static std::string arquivo;
    if (som.gravando()) {
        som.pararGravacao();
        erro_ = "Gravação salva em:\n" + arquivo;
        erroAte_ = agoraS() + 5;
        return;
    }
    std::string pasta = pastaDoExe() + "\\Gravacoes";
    CreateDirectoryA(pasta.c_str(), nullptr);
    std::time_t t = std::time(nullptr);
    std::tm lt{}; localtime_s(&lt, &t);
    char nome[128];
    std::snprintf(nome, sizeof nome, "\\RXSDR_%04d%02d%02d_%02d%02d%02d_%.3fkHz_%s.wav",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec,
                  r_.vfo() / 1000.0, r_.modo().c_str());
    arquivo = pasta + nome;
    if (!som.gravar(arquivo)) {
        erro_ = "Não foi possível criar o arquivo de gravação:\n" + arquivo;
        erroAte_ = agoraS() + 6;
    }
}

void Ui::janelaSobre()
{
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Sobre", &sobreAberta_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }
    ImGui::PushFont(f_.negrito);
    ImGui::TextColored(ImVec4(0, 0.83f, 0.83f, 1), "RXSDR Nativo v" RXSDR_VERSAO);
    ImGui::PopFont();
    ImGui::TextUnformatted("Receptor SDR que roda inteiro no Windows (7, 8, 10 e 11), sem navegador.");
    ImGui::TextUnformatted("Portátil: não precisa instalar - a configuração fica no RXSDR.ini ao lado do programa.");
    ImGui::TextUnformatted("RTL-SDR, RTL-TCP e SDRplay.  Por PU1XTB.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Licença: uso livre e NÃO COMERCIAL, mantendo o crédito ao autor (LICENSE.txt).");
    ImGui::TextDisabled("Decodificadores da pasta decoders: de outros autores, com a licença de cada um (TERCEIROS.txt).");
    ImGui::TextDisabled("Tela: Dear ImGui (MIT).  Imagens: stb_image (domínio público).");
    ImGui::End();
}


// ===========================================================================
//  Decodificadores (mesmos nucleos do RXSDR principal, sem programa externo)
// ===========================================================================
namespace {
// hor: janelas UTC "hhmm-hhmm hhmm-hhmm" (grade de horarios) - no ar agora fica verde
// dica: estacoes e horarios (aparece com o mouse em cima). hz = 0: titulo de grupo.
struct CanalDec { uint64_t hz; const char* nome; float baud; float shift; bool meio;
                  const char* hor = nullptr; const char* dica = nullptr; };

// RTTY - "meio": a frequencia publicada e o CENTRO do sinal (DWD); o VFO vai
// 1500 Hz abaixo e os dois tons caem no meio da faixa USB (igual a pagina).
const CanalDec kCanaisRtty[] = {
    {14080000, "14080 kHz - 20 m radioamador", 45.45f, 170, false},
    {7040000, "7040 kHz - 40 m radioamador", 45.45f, 170, false},
    {3580000, "3580 kHz - 80 m radioamador", 45.45f, 170, false},
    {21080000, "21080 kHz - 15 m radioamador", 45.45f, 170, false},
    {28080000, "28080 kHz - 10 m radioamador", 45.45f, 170, false},
    {10100800, "10100,8 kHz - DWD Alemanha (meteorologia)", 50, 450, true},
    {11039000, "11039 kHz - DWD Alemanha (meteorologia)", 50, 450, true},
    {14467300, "14467,3 kHz - DWD Alemanha (meteorologia)", 50, 450, true},
    {7646000, "7646 kHz - DWD Alemanha (meteorologia)", 50, 450, true},
    {4583000, "4583 kHz - DWD Alemanha (meteorologia)", 50, 450, true},
    {147300, "147,3 kHz - DWD Alemanha (ondas longas)", 50, 450, true},
};
// SITOR-B e DSC (lista de estacoes do autor, 04/10/2026): uma linha por
// transmissao, em ordem de horario UTC; estacoes sem identificacao ficam de fora.
// A frequencia publicada e o CENTRO do sinal FSK: o VFO (USB) vai 1700 Hz abaixo.
const CanalDec kCanaisSitor[] = {
    {16808000, "0000-0030   16808 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "0000-0030", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0600-0630, 0900-0930, 1200-1230, 1400-1430, 1800-1830, 2100-2130"},
    {490000, "0000-2010   490 kHz - NAVTEX Canarias (Canary Islands) +1", 100, 170, false, "0000-2010", "NAVTEX Canarias (Canary Islands)\nNAVTEX Malin Head (Ireland)\n\nOutros horários nesta frequência: 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0000-2010   518 kHz - NAVTEX Cross Corsen (France)", 100, 170, false, "0000-2010", "NAVTEX Cross Corsen (France)\n\nOutros horários nesta frequência: 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {490000, "0000-2400   490 kHz - NAVTEX WEATHER BROADCASTS (Worldwide)", 100, 170, false, "0000-2400", "NAVTEX WEATHER BROADCASTS (Worldwide)\n\nOutros horários nesta frequência: 0000-2010, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0000-2400   518 kHz - NAVTEX WEATHER BROADCASTS (Worldwide)", 100, 170, false, "0000-2400", "NAVTEX WEATHER BROADCASTS (Worldwide)\n\nOutros horários nesta frequência: 0000-2010, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {4172500, "0000-2400   4172,5 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {4209500, "0000-2400   4209,5 kHz - Guangzhou Radio (China) +1", 100, 170, false, "0000-2400", "Guangzhou Radio (China)\nIstanbul Radio (Turkiye)\n\nOutros horários nesta frequência: 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {4210500, "0000-2400   4210,5 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {4211500, "0000-2400   4211,5 kHz - Rogaland Radio MRCC (Norway)", 100, 170, false, "0000-2400", "Rogaland Radio MRCC (Norway)"},
    {4211700, "0000-2400   4211,7 kHz - Argentine Army (Argentina)", 100, 170, false, "0000-2400", "Argentine Army (Argentina)"},
    {4212000, "0000-2400   4212 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {4212500, "0000-2400   4212,5 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)\n\nOutros horários nesta frequência: 1200-1230, 1600-1630, 2000-2030"},
    {4215000, "0000-2400   4215 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)"},
    {4216000, "0000-2400   4216 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {4217700, "0000-2400   4217,7 kHz - Guangzhou Radio MRCC (China)", 100, 170, false, "0000-2400", "Guangzhou Radio MRCC (China)"},
    {4219000, "0000-2400   4219 kHz - Guangzhou Radio (China) +1", 100, 170, false, "0000-2400", "Guangzhou Radio (China)\nIstanbul Radio (Turkiye)"},
    {4241000, "0000-2400   4241 kHz - Vladivostok Radio (Russia)", 100, 170, false, "0000-2400", "Vladivostok Radio (Russia)"},
    {4560000, "0000-2400   4560 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)\n\nOutros horários nesta frequência: 2000-2010"},
    {4590000, "0000-2400   4590 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {4718500, "0000-2400   4718,5 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {6264500, "0000-2400   6264,5 kHz - Guangzhou Radio MRCC (China)", 100, 170, false, "0000-2400", "Guangzhou Radio MRCC (China)"},
    {6316000, "0000-2400   6316 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {6320000, "0000-2400   6320 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {6320500, "0000-2400   6320,5 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {6321500, "0000-2400   6321,5 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {6324500, "0000-2400   6324,5 kHz - KPH Point Reyes (USA)", 100, 170, false, "0000-2400", "KPH Point Reyes (USA)"},
    {6326000, "0000-2400   6326 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)"},
    {6329000, "0000-2400   6329 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)\n\nOutros horários nesta frequência: 0120-0130, 0320-0330, 0720-0730, 0920-0930"},
    {6488500, "0000-2400   6488,5 kHz - Argentina Radio (Argentina)", 100, 170, false, "0000-2400", "Argentina Radio (Argentina)"},
    {8383000, "0000-2400   8383 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {8384000, "0000-2400   8384 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {8387000, "0000-2400   8387 kHz - Madrid Radio (Spain)", 100, 170, false, "0000-2400", "Madrid Radio (Spain)"},
    {8390000, "0000-2400   8390 kHz - Shanghai Radio MRCC (China)", 100, 170, false, "0000-2400", "Shanghai Radio MRCC (China)"},
    {8391000, "0000-2400   8391 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {8391500, "0000-2400   8391,5 kHz - Moskva Radio (Russia)", 100, 170, false, "0000-2400", "Moskva Radio (Russia)"},
    {8393000, "0000-2400   8393 kHz - Guangzhou Radio MRCC (China) +2", 100, 170, false, "0000-2400", "Guangzhou Radio MRCC (China)\nShanghai Radio Meteo (China)\nIstanbul Radio (Turkiye)"},
    {8394000, "0000-2400   8394 kHz - Guangzhou Radio MRCC (China) +1", 100, 170, false, "0000-2400", "Guangzhou Radio MRCC (China)\nIstanbul Radio (Turkiye)"},
    {8417500, "0000-2400   8417,5 kHz - Tianjin Radio (China)", 100, 170, false, "0000-2400", "Tianjin Radio (China)\n\nOutros horários nesta frequência: 0500-0530, 0700-0730, 1200-1230, 1600-1630, 2000-2030, 2300-2330"},
    {8418000, "0000-2400   8418 kHz - Argentina Radio (Argentina)", 100, 170, false, "0000-2400", "Argentina Radio (Argentina)"},
    {8418200, "0000-2400   8418,2 kHz - Tianjin Radio (China)", 100, 170, false, "0000-2400", "Tianjin Radio (China)"},
    {8419000, "0000-2400   8419 kHz - Tianjin Radio (China)", 100, 170, false, "0000-2400", "Tianjin Radio (China)"},
    {8421000, "0000-2400   8421 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {8423000, "0000-2400   8423 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {8424000, "0000-2400   8424 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)\n\nOutros horários nesta frequência: 0630-0645, 0700-0710, 0930-0945, 1000-1010, 1100-1110, 1300-1315, 1600-1610, 2130-2145"},
    {8425500, "0000-2400   8425,5 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)\n\nOutros horários nesta frequência: 0850-0900"},
    {8427000, "0000-2400   8427 kHz - KPH Point Reyes (USA)", 100, 170, false, "0000-2400", "KPH Point Reyes (USA)"},
    {8431000, "0000-2400   8431 kHz - Guangzhou Radio (China) +1", 100, 170, false, "0000-2400", "Guangzhou Radio (China)\nIstanbul Radio (Turkiye)\n\nOutros horários nesta frequência: 0120-0130, 0320-0330, 0720-0730, 0800-0815, 0920-0930, 1320-1330, 1520-1530, 2000-2015, 2220-2230"},
    {8431500, "0000-2400   8431,5 kHz - Moskva Radio (Russia)", 100, 170, false, "0000-2400", "Moskva Radio (Russia)"},
    {8433000, "0000-2400   8433 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)"},
    {8434000, "0000-2400   8434 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {8435000, "0000-2400   8435 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {8617000, "0000-2400   8617 kHz - Tianjin Radio (China)", 100, 170, false, "0000-2400", "Tianjin Radio (China)"},
    {8630000, "0000-2400   8630 kHz - Marinha do Brasil", 100, 170, false, "0000-2400", "Marinha do Brasil"},
    {8741700, "0000-2400   8741,7 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {8750700, "0000-2400   8750,7 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {8813700, "0000-2400   8813,7 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {10926000, "0000-2400   10926 kHz - Marinha do Brasil", 100, 170, false, "0000-2400", "Marinha do Brasil"},
    {12190000, "0000-2400   12190 kHz - Marinha do Brasil", 100, 170, false, "0000-2400", "Marinha do Brasil"},
    {12424000, "0000-2400   12424 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {12501000, "0000-2400   12501 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {12510000, "0000-2400   12510 kHz - Guangzhou Radio MRCC (China)", 100, 170, false, "0000-2400", "Guangzhou Radio MRCC (China)"},
    {12510500, "0000-2400   12510,5 kHz - Guangzhou Radio MRCC (China)", 100, 170, false, "0000-2400", "Guangzhou Radio MRCC (China)"},
    {12519500, "0000-2400   12519,5 kHz - Guangzhou Radio MRCC (China)", 100, 170, false, "0000-2400", "Guangzhou Radio MRCC (China)"},
    {12522000, "0000-2400   12522 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {12526000, "0000-2400   12526 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {12532500, "0000-2400   12532,5 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {12535500, "0000-2400   12535,5 kHz - Shanghai Radio MRCC (China)", 100, 170, false, "0000-2400", "Shanghai Radio MRCC (China)"},
    {12537000, "0000-2400   12537 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {12557000, "0000-2400   12557 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {12566700, "0000-2400   12566,7 kHz - Marinha do Brasil", 100, 170, false, "0000-2400", "Marinha do Brasil"},
    {12580500, "0000-2400   12580,5 kHz - Tianjin Radio (China)", 100, 170, false, "0000-2400", "Tianjin Radio (China)"},
    {12581000, "0000-2400   12581 kHz - Argentina Radio (Argentina)", 100, 170, false, "0000-2400", "Argentina Radio (Argentina)"},
    {12581500, "0000-2400   12581,5 kHz - Tianjin Radio (China)", 100, 170, false, "0000-2400", "Tianjin Radio (China)\n\nOutros horários nesta frequência: 0500-0530, 0700-0730, 1200-1230, 1600-1630, 2000-2030, 2300-2330"},
    {12585500, "0000-2400   12585,5 kHz - KPH Point Reyes (USA)", 100, 170, false, "0000-2400", "KPH Point Reyes (USA)"},
    {12603500, "0000-2400   12603,5 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {12613000, "0000-2400   12613 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {12622500, "0000-2400   12622,5 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0000-2400", "Guangzhou Radio Meteo (China)"},
    {12624000, "0000-2400   12624 kHz - Guangzhou Radio (China) +1", 100, 170, false, "0000-2400", "Guangzhou Radio (China)\nIstanbul Radio (Turkiye)"},
    {12629000, "0000-2400   12629 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {12637500, "0000-2400   12637,5 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)\n\nOutros horários nesta frequência: 0250-0300, 0850-0900, 1350-1420"},
    {12648500, "0000-2400   12648,5 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {12654000, "0000-2400   12654 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)\n\nOutros horários nesta frequência: 0800-0815, 2000-2015"},
    {13173000, "0000-2400   13173 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0000-2400", "Istanbul Radio (Turkiye)"},
    {16695000, "0000-2400   16695 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)"},
    {16762000, "0000-2400   16762 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {16778000, "0000-2400   16778 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {16808000, "0000-2400   16808 kHz - Tianjin Radio (China)", 100, 170, false, "0000-2400", "Tianjin Radio (China)\n\nOutros horários nesta frequência: 0000-0030, 0600-0630, 0900-0930, 1200-1230, 1400-1430, 1800-1830, 2100-2130"},
    {16830500, "0000-2400   16830,5 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {16854000, "0000-2400   16854 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0000-2400", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0320-0330, 0720-0730, 1320-1340"},
    {16880000, "0000-2400   16880 kHz - Guangzhou Radio (China)", 100, 170, false, "0000-2400", "Guangzhou Radio (China)"},
    {16898500, "0000-2400   16898,5 kHz - Shanghai Radio (China)", 100, 170, false, "0000-2400", "Shanghai Radio (China)\n\nOutros horários nesta frequência: 0250-0300, 0850-0900, 1350-1420"},
    {22744000, "0000-2400   22744 kHz - Olympia Radio (Greece)", 100, 170, false, "0000-2400", "Olympia Radio (Greece)"},
    {490000, "0010-2020   490 kHz - NAVTEX Oostende (Belgium)", 100, 170, false, "0010-2020", "NAVTEX Oostende (Belgium)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {12799500, "0015-0030   12799,5 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "0015-0030", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0430-0445, 0700-0705, 0900-0905, 2100-2105"},
    {8416500, "0015-0130   8416,5 kHz - USCG San Francisco (USA)", 100, 170, false, "0015-0130", "USCG San Francisco (USA)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {16806500, "0015-0130   16806,5 kHz - USCG San Francisco (USA)", 100, 170, false, "0015-0130", "USCG San Francisco (USA)\n\nOutros horários nesta frequência: 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {4209500, "0020-0030   4209,5 kHz - Nha Trang Radio (Vietnam)", 100, 170, false, "0020-0030", "Nha Trang Radio (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {490000, "0020-2030   490 kHz - NAVTEX Portpatric (United Kingdom)", 100, 170, false, "0020-2030", "NAVTEX Portpatric (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {4210000, "0030-0110   4210 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "0030-0110", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0300-0410, 1000-1140, 1400-1440, 1515-1610, 1900-1940, 2100-2140"},
    {8416500, "0030-0110   8416,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "0030-0110", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {12580700, "0030-0110   12580,7 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "0030-0110", "Servicio de Hidrografia Naval (Argentina)\nA lista publica 12579 kHz, mas o sinal sai em 12580,7 kHz (medido no RXSDR: tons em 12580,6 e 12580,8 kHz, shift 200 Hz)\n\nOutros horários nesta frequência: 1000-1140, 1400-1440, 1515-1610, 1900-1940, 2100-2140"},
    {16806500, "0030-0110   16806,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 170, false, "0030-0110", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {518000, "0030-2040   518 kHz - NAVTEX La Coruna (Spain) +1", 100, 170, false, "0030-2040", "NAVTEX La Coruna (Spain)\nNAVTEX Faroe Islands (Faroe Islands)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {490000, "0040-2050   490 kHz - NAVTEX Cross Corsen (France) +1", 100, 170, false, "0040-2050", "NAVTEX Cross Corsen (France)\nNAVTEX Reykjavik (Iceland)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0040-2050   518 kHz - NAVTEX Niton (United Kingdom)", 100, 170, false, "0040-2050", "NAVTEX Niton (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {518000, "0050-2100   518 kHz - NAVTEX Acores (Azores) +1", 100, 170, false, "0050-2100", "NAVTEX Acores (Azores)\nNAVTEX Tallinn (Estonia)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {490000, "0100-2110   490 kHz - NAVTEX Lisbon (Portugal)", 100, 170, false, "0100-2110", "NAVTEX Lisbon (Portugal)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0100-2110   518 kHz - NAVTEX Tarifa (Spain) +1", 100, 170, false, "0100-2110", "NAVTEX Tarifa (Spain)\nNAVTEX Cullercoats (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {518000, "0110-2120   518 kHz - NAVTEX Stockholm (Sweden)", 100, 170, false, "0110-2120", "NAVTEX Stockholm (Sweden)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {6329000, "0120-0130   6329 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0120-0130", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0320-0330, 0720-0730, 0920-0930"},
    {8431000, "0120-0130   8431 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0120-0130", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0320-0330, 0720-0730, 0800-0815, 0920-0930, 1320-1330, 1520-1530, 2000-2015, 2220-2230"},
    {490000, "0120-2130   490 kHz - NAVTEX Niton (United Kingdom)", 100, 170, false, "0120-2130", "NAVTEX Niton (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0120-2130   518 kHz - NAVTEX Canarias (Canary Islands) +1", 100, 170, false, "0120-2130", "NAVTEX Canarias (Canary Islands)\nNAVTEX Stockholm (Sweden)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {8416500, "0130-0220   8416,5 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "0130-0220", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {12579000, "0130-0220   12579 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "0130-0220", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0030-0110, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {22376000, "0130-0220   22376 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "0130-0220", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0230-0300, 0500-0530, 0900-0925, 1500-1530, 1900-1940, 2030-2120, 2315-2345"},
    {490000, "0130-2140   490 kHz - NAVTEX Acores (Azores)", 100, 170, false, "0130-2140", "NAVTEX Acores (Azores)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0130-2140   518 kHz - NAVTEX Stockholm (Sweden)", 100, 170, false, "0130-2140", "NAVTEX Stockholm (Sweden)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {6314000, "0140-0230   6314 kHz - USCG Boston (USA)", 100, 170, false, "0140-0230", "USCG Boston (USA)"},
    {8416500, "0140-0230   8416,5 kHz - USCG Boston (USA)", 100, 170, false, "0140-0230", "USCG Boston (USA)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {12579000, "0140-0230   12579 kHz - USCG Boston (USA)", 100, 170, false, "0140-0230", "USCG Boston (USA)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {490000, "0140-2150   490 kHz - NAVTEX Reykjavik (Iceland)", 100, 170, false, "0140-2150", "NAVTEX Reykjavik (Iceland)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0150-2200, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0140-2150   518 kHz - NAVTEX Niton (United Kingdom)", 100, 170, false, "0140-2150", "NAVTEX Niton (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {490000, "0150-2200   490 kHz - NAVTEX Pinneberg (Germany)", 100, 170, false, "0150-2200", "NAVTEX Pinneberg (Germany)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0200-2210, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0150-2200   518 kHz - NAVTEX Arkhangelsk (Norway) +1", 100, 170, false, "0150-2200", "NAVTEX Arkhangelsk (Norway)\nNAVTEX Rogaland (Norway)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {4209500, "0200-0220   4209,5 kHz - Hai Phong Radio Meteo (Vietnam)", 100, 170, false, "0200-0220", "Hai Phong Radio Meteo (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {4209500, "0200-0245   4209,5 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "0200-0245", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {490000, "0200-2210   490 kHz - NAVTEX Madeira (Madeira)", 100, 170, false, "0200-2210", "NAVTEX Madeira (Madeira)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0310-2320, 0320-2330, 0340-2350"},
    {518000, "0200-2210   518 kHz - NAVTEX Jeloy (Norway)", 100, 170, false, "0200-2210", "NAVTEX Jeloy (Norway)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {518000, "0210-2220   518 kHz - NAVTEX Orland (Norway)", 100, 170, false, "0210-2220", "NAVTEX Orland (Norway)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {518000, "0220-2230   518 kHz - NAVTEX Portpatric (United Kingdom)", 100, 170, false, "0220-2230", "NAVTEX Portpatric (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {12579000, "0230-0300   12579 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0230-0300", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {16806500, "0230-0300   16806,5 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0230-0300", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {22376000, "0230-0300   22376 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0230-0300", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0130-0220, 0500-0530, 0900-0925, 1500-1530, 1900-1940, 2030-2120, 2315-2345"},
    {518000, "0230-2240   518 kHz - NAVTEX Netherlands (Netherlands) +1", 100, 170, false, "0230-2240", "NAVTEX Netherlands (Netherlands)\nNAVTEX Madeira (Madeira)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {4209500, "0240-0250   4209,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0240-0250", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {518000, "0240-2250   518 kHz - NAVTEX Malin Head (Ireland)", 100, 170, false, "0240-2250", "NAVTEX Malin Head (Ireland)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {12579000, "0250-0300   12579 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0250-0300", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {12637500, "0250-0300   12637,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0250-0300", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0850-0900, 1350-1420"},
    {16898500, "0250-0300   16898,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0250-0300", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0850-0900, 1350-1420"},
    {518000, "0250-2300   518 kHz - NAVTEX Reykjavik (Iceland) +1", 100, 170, false, "0250-2300", "NAVTEX Reykjavik (Iceland)\nNAVTEX Lisbon (Portugal)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0300-2310, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {4209500, "0300-0310   4209,5 kHz - Olympia Radio METEO (Greece)", 100, 170, false, "0300-0310", "Olympia Radio METEO (Greece)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {4210000, "0300-0410   4210 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "0300-0410", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0030-0110, 1000-1140, 1400-1440, 1515-1610, 1900-1940, 2100-2140"},
    {8416500, "0300-0410   8416,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "0300-0410", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {518000, "0300-2310   518 kHz - NAVTEX Pinneberg (Germany)", 100, 170, false, "0300-2310", "NAVTEX Pinneberg (Germany)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0310-2320, 0330-2340, 0340-2350, 0350-2400"},
    {490000, "0310-2320   490 kHz - NAVTEX Tarifa (Spain) +1", 100, 170, false, "0310-2320", "NAVTEX Tarifa (Spain)\nNAVTEX Niton (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0320-2330, 0340-2350"},
    {518000, "0310-2320   518 kHz - NAVTEX Oostende (Belgium)", 100, 170, false, "0310-2320", "NAVTEX Oostende (Belgium)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0330-2340, 0340-2350, 0350-2400"},
    {6329000, "0320-0330   6329 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0320-0330", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0720-0730, 0920-0930"},
    {8431000, "0320-0330   8431 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0320-0330", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0720-0730, 0800-0815, 0920-0930, 1320-1330, 1520-1530, 2000-2015, 2220-2230"},
    {16854000, "0320-0330   16854 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0320-0330", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0720-0730, 1320-1340"},
    {490000, "0320-2330   490 kHz - NAVTEX Cullercoats (United Kingdom)", 100, 170, false, "0320-2330", "NAVTEX Cullercoats (United Kingdom)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0340-2350"},
    {8416500, "0330-0400   8416,5 kHz - Iqaluit Coast Guard Radio (Canada)", 100, 170, false, "0330-0400", "Iqaluit Coast Guard Radio (Canada)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {518000, "0330-2340   518 kHz - NAVTEX Oostende (Belgium)", 100, 170, false, "0330-2340", "NAVTEX Oostende (Belgium)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0340-2350, 0350-2400"},
    {490000, "0340-2350   490 kHz - NAVTEX La Coruna (Spain)", 100, 170, false, "0340-2350", "NAVTEX La Coruna (Spain)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0010-2020, 0020-2030, 0040-2050, 0100-2110, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0310-2320, 0320-2330"},
    {518000, "0340-2350   518 kHz - NAVTEX Valentia (Ireland)", 100, 170, false, "0340-2350", "NAVTEX Valentia (Ireland)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0350-2400"},
    {518000, "0350-2400   518 kHz - NAVTEX Reykjavik (Iceland)", 100, 170, false, "0350-2400", "NAVTEX Reykjavik (Iceland)\n\nOutros horários nesta frequência: 0000-2010, 0000-2400, 0030-2040, 0040-2050, 0050-2100, 0100-2110, 0110-2120, 0120-2130, 0130-2140, 0140-2150, 0150-2200, 0200-2210, 0210-2220, 0220-2230, 0230-2240, 0240-2250, 0250-2300, 0300-2310, 0310-2320, 0330-2340, 0340-2350"},
    {16768200, "0400-1800   16768,2 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0400-1800", "Istanbul Radio (Turkiye)"},
    {16886000, "0400-1800   16886 kHz - Istanbul Radio (Turkiye)", 100, 170, false, "0400-1800", "Istanbul Radio (Turkiye)"},
    {4209500, "0420-0430   4209,5 kHz - Nha Trang Radio (Vietnam)", 100, 170, false, "0420-0430", "Nha Trang Radio (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {12799500, "0430-0445   12799,5 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "0430-0445", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0015-0030, 0700-0705, 0900-0905, 2100-2105"},
    {8417500, "0500-0530   8417,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "0500-0530", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0700-0730, 1200-1230, 1600-1630, 2000-2030, 2300-2330"},
    {12579000, "0500-0530   12579 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0500-0530", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {12581500, "0500-0530   12581,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "0500-0530", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0700-0730, 1200-1230, 1600-1630, 2000-2030, 2300-2330"},
    {16806500, "0500-0530   16806,5 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0500-0530", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {22376000, "0500-0530   22376 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0500-0530", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0130-0220, 0230-0300, 0900-0925, 1500-1530, 1900-1940, 2030-2120, 2315-2345"},
    {22814800, "0500-0900   22814,8 kHz - Olympia Radio (Greece)", 100, 170, false, "0500-0900", "Olympia Radio (Greece)"},
    {22387500, "0500-2100   22387,5 kHz - Olympia Radio (Greece)", 100, 170, false, "0500-2100", "Olympia Radio (Greece)"},
    {4209500, "0600-0620   4209,5 kHz - Hai Phong Radio Meteo (Vietnam)", 100, 170, false, "0600-0620", "Hai Phong Radio Meteo (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {16808000, "0600-0630   16808 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "0600-0630", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-0030, 0000-2400, 0900-0930, 1200-1230, 1400-1430, 1800-1830, 2100-2130"},
    {4209500, "0600-0645   4209,5 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "0600-0645", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {26163000, "0600-1800   26163 kHz - Olympia Radio (Greece)", 100, 170, false, "0600-1800", "Olympia Radio (Greece)"},
    {8424000, "0630-0645   8424 kHz - Olympia Radio (Greece)", 100, 170, false, "0630-0645", "Olympia Radio (Greece)\n\nOutros horários nesta frequência: 0000-2400, 0700-0710, 0930-0945, 1000-1010, 1100-1110, 1300-1315, 1600-1610, 2130-2145"},
    {4209500, "0640-0650   4209,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0640-0650", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {12799500, "0700-0705   12799,5 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "0700-0705", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0015-0030, 0430-0445, 0900-0905, 2100-2105"},
    {4209500, "0700-0710   4209,5 kHz - Olympia Radio METEO (Greece)", 100, 170, false, "0700-0710", "Olympia Radio METEO (Greece)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {6322500, "0700-0710   6322,5 kHz - Murmansk Radio (Russia)", 100, 170, false, "0700-0710", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 1000-1010, 1100-1110, 1300-1315, 1600-1610"},
    {8424000, "0700-0710   8424 kHz - Murmansk Radio (Russia)", 100, 170, false, "0700-0710", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0000-2400, 0630-0645, 0930-0945, 1000-1010, 1100-1110, 1300-1315, 1600-1610, 2130-2145"},
    {12586000, "0700-0710   12586 kHz - Murmansk Radio (Russia)", 100, 170, false, "0700-0710", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 1000-1010, 1100-1110, 1300-1315, 1600-1610"},
    {8417500, "0700-0730   8417,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "0700-0730", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 1200-1230, 1600-1630, 2000-2030, 2300-2330"},
    {12581500, "0700-0730   12581,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "0700-0730", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 1200-1230, 1600-1630, 2000-2030, 2300-2330"},
    {12579000, "0700-1400   12579 kHz - Moskva Radio (Russia)", 100, 170, false, "0700-1400", "Moskva Radio (Russia)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {16822500, "0700-1700   16822,5 kHz - Murmansk Radio (Russia)", 100, 170, false, "0700-1700", "Murmansk Radio (Russia)"},
    {8416500, "0710-0930   8416,5 kHz - Moskva Radio (Russia)", 100, 170, false, "0710-0930", "Moskva Radio (Russia)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {6329000, "0720-0730   6329 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0720-0730", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0920-0930"},
    {8431000, "0720-0730   8431 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0720-0730", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0800-0815, 0920-0930, 1320-1330, 1520-1530, 2000-2015, 2220-2230"},
    {16854000, "0720-0730   16854 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0720-0730", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0320-0330, 1320-1340"},
    {8416500, "0730-0850   8416,5 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "0730-0850", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {12579000, "0730-0850   12579 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "0730-0850", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {8431000, "0800-0815   8431 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "0800-0815", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0720-0730, 0920-0930, 1320-1330, 1520-1530, 2000-2015, 2220-2230"},
    {12654000, "0800-0815   12654 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "0800-0815", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 2000-2015"},
    {12579000, "0800-0840   12579 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "0800-0840", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {4209500, "0820-0830   4209,5 kHz - Nha Trang Radio (Vietnam)", 100, 170, false, "0820-0830", "Nha Trang Radio (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {8425500, "0850-0900   8425,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0850-0900", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400"},
    {12579000, "0850-0900   12579 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0850-0900", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {12637500, "0850-0900   12637,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0850-0900", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0250-0300, 1350-1420"},
    {16898500, "0850-0900   16898,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "0850-0900", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0250-0300, 1350-1420"},
    {12799500, "0900-0905   12799,5 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "0900-0905", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0015-0030, 0430-0445, 0700-0705, 2100-2105"},
    {8655000, "0900-0910   8655 kHz - Nakhodka Radio (Russia)", 100, 170, false, "0900-0910", "Nakhodka Radio (Russia)\n\nOutros horários nesta frequência: 1000-1010"},
    {12579000, "0900-0925   12579 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0900-0925", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {16806500, "0900-0925   16806,5 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0900-0925", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {22376000, "0900-0925   22376 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "0900-0925", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0130-0220, 0230-0300, 0500-0530, 1500-1530, 1900-1940, 2030-2120, 2315-2345"},
    {16808000, "0900-0930   16808 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "0900-0930", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-0030, 0000-2400, 0600-0630, 1200-1230, 1400-1430, 1800-1830, 2100-2130"},
    {6329000, "0920-0930   6329 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0920-0930", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0720-0730"},
    {8431000, "0920-0930   8431 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "0920-0930", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0720-0730, 0800-0815, 1320-1330, 1520-1530, 2000-2015, 2220-2230"},
    {8424000, "0930-0945   8424 kHz - Olympia Radio (Greece)", 100, 170, false, "0930-0945", "Olympia Radio (Greece)\n\nOutros horários nesta frequência: 0000-2400, 0630-0645, 0700-0710, 1000-1010, 1100-1110, 1300-1315, 1600-1610, 2130-2145"},
    {6322500, "1000-1010   6322,5 kHz - Murmansk Radio (Russia)", 100, 170, false, "1000-1010", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1100-1110, 1300-1315, 1600-1610"},
    {8424000, "1000-1010   8424 kHz - Murmansk Radio (Russia)", 100, 170, false, "1000-1010", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0000-2400, 0630-0645, 0700-0710, 0930-0945, 1100-1110, 1300-1315, 1600-1610, 2130-2145"},
    {8655000, "1000-1010   8655 kHz - Nakhodka Radio (Russia)", 100, 170, false, "1000-1010", "Nakhodka Radio (Russia)\n\nOutros horários nesta frequência: 0900-0910"},
    {12586000, "1000-1010   12586 kHz - Murmansk Radio (Russia)", 100, 170, false, "1000-1010", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1100-1110, 1300-1315, 1600-1610"},
    {4209500, "1000-1020   4209,5 kHz - Hai Phong Radio Meteo (Vietnam)", 100, 170, false, "1000-1020", "Hai Phong Radio Meteo (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {4228000, "1000-1035   4228 kHz - Kaliningrad Radio Meteo (Russia)", 100, 170, false, "1000-1035", "Kaliningrad Radio Meteo (Russia)\n\nOutros horários nesta frequência: 1600-1745"},
    {8454000, "1000-1035   8454 kHz - Kaliningrad Radio Meteo (Russia)", 100, 170, false, "1000-1035", "Kaliningrad Radio Meteo (Russia)\n\nOutros horários nesta frequência: 1200-1230, 1600-1745"},
    {4209500, "1000-1045   4209,5 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "1000-1045", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {4210000, "1000-1140   4210 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1000-1140", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0030-0110, 0300-0410, 1400-1440, 1515-1610, 1900-1940, 2100-2140"},
    {12580700, "1000-1140   12580,7 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1000-1140", "Servicio de Hidrografia Naval (Argentina)\nA lista publica 12579 kHz, mas o sinal sai em 12580,7 kHz (medido no RXSDR: tons em 12580,6 e 12580,8 kHz, shift 200 Hz)\n\nOutros horários nesta frequência: 0030-0110, 1400-1440, 1515-1610, 1900-1940, 2100-2140"},
    {16806500, "1000-1140   16806,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 170, false, "1000-1140", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {8416500, "1000-1300   8416,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1000-1300", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {4209500, "1040-1050   4209,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "1040-1050", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {6322500, "1100-1110   6322,5 kHz - Murmansk Radio (Russia)", 100, 170, false, "1100-1110", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1000-1010, 1300-1315, 1600-1610"},
    {8424000, "1100-1110   8424 kHz - Murmansk Radio (Russia)", 100, 170, false, "1100-1110", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0000-2400, 0630-0645, 0700-0710, 0930-0945, 1000-1010, 1300-1315, 1600-1610, 2130-2145"},
    {12586000, "1100-1110   12586 kHz - Murmansk Radio (Russia)", 100, 170, false, "1100-1110", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1000-1010, 1300-1315, 1600-1610"},
    {12579000, "1100-1130   12579 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "1100-1130", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {8416500, "1100-1200   8416,5 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "1100-1200", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {4212500, "1200-1230   4212,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1200-1230", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 1600-1630, 2000-2030"},
    {8417500, "1200-1230   8417,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1200-1230", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1600-1630, 2000-2030, 2300-2330"},
    {8454000, "1200-1230   8454 kHz - Kaliningrad Radio Meteo (Russia)", 100, 170, false, "1200-1230", "Kaliningrad Radio Meteo (Russia)\n\nOutros horários nesta frequência: 1000-1035, 1600-1745"},
    {12581500, "1200-1230   12581,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1200-1230", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1600-1630, 2000-2030, 2300-2330"},
    {16808000, "1200-1230   16808 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1200-1230", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-0030, 0000-2400, 0600-0630, 0900-0930, 1400-1430, 1800-1830, 2100-2130"},
    {16806500, "1200-1310   16806,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 170, false, "1200-1310", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {4209500, "1220-1230   4209,5 kHz - Nha Trang Radio (Vietnam)", 100, 170, false, "1220-1230", "Nha Trang Radio (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {4214500, "1300-1315   4214,5 kHz - Murmansk Radio (Russia)", 100, 170, false, "1300-1315", "Murmansk Radio (Russia)"},
    {6322500, "1300-1315   6322,5 kHz - Murmansk Radio (Russia)", 100, 170, false, "1300-1315", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1000-1010, 1100-1110, 1600-1610"},
    {8424000, "1300-1315   8424 kHz - Murmansk Radio (Russia)", 100, 170, false, "1300-1315", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0000-2400, 0630-0645, 0700-0710, 0930-0945, 1000-1010, 1100-1110, 1600-1610, 2130-2145"},
    {12586000, "1300-1315   12586 kHz - Murmansk Radio (Russia)", 100, 170, false, "1300-1315", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1000-1010, 1100-1110, 1600-1610"},
    {12579000, "1300-1340   12579 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "1300-1340", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {8431000, "1320-1330   8431 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "1320-1330", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0720-0730, 0800-0815, 0920-0930, 1520-1530, 2000-2015, 2220-2230"},
    {16854000, "1320-1340   16854 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "1320-1340", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0320-0330, 0720-0730"},
    {8416500, "1330-1450   8416,5 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "1330-1450", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {12579000, "1330-1450   12579 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "1330-1450", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {12579000, "1350-1420   12579 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "1350-1420", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {12637500, "1350-1420   12637,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "1350-1420", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0250-0300, 0850-0900"},
    {16898500, "1350-1420   16898,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "1350-1420", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0250-0300, 0850-0900"},
    {4209500, "1400-1420   4209,5 kHz - Hai Phong Radio Meteo (Vietnam)", 100, 170, false, "1400-1420", "Hai Phong Radio Meteo (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {16808000, "1400-1430   16808 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1400-1430", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-0030, 0000-2400, 0600-0630, 0900-0930, 1200-1230, 1800-1830, 2100-2130"},
    {4210000, "1400-1440   4210 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1400-1440", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0030-0110, 0300-0410, 1000-1140, 1515-1610, 1900-1940, 2100-2140"},
    {12580700, "1400-1440   12580,7 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1400-1440", "Servicio de Hidrografia Naval (Argentina)\nA lista publica 12579 kHz, mas o sinal sai em 12580,7 kHz (medido no RXSDR: tons em 12580,6 e 12580,8 kHz, shift 200 Hz)\n\nOutros horários nesta frequência: 0030-0110, 1000-1140, 1515-1610, 1900-1940, 2100-2140"},
    {16806500, "1400-1440   16806,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 170, false, "1400-1440", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {4209500, "1400-1445   4209,5 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "1400-1445", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {8416500, "1400-1500   8416,5 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "1400-1500", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {8416500, "1400-1610   8416,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1400-1610", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {4209500, "1440-1450   4209,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "1440-1450", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {4209500, "1500-1510   4209,5 kHz - Guangzhou Radio (China) +1", 100, 170, false, "1500-1510", "Guangzhou Radio (China)\nOlympia Radio METEO (Greece)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1620-1630, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {12579000, "1500-1530   12579 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "1500-1530", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {16806500, "1500-1530   16806,5 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "1500-1530", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1515-1610, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {22376000, "1500-1530   22376 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "1500-1530", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0130-0220, 0230-0300, 0500-0530, 0900-0925, 1900-1940, 2030-2120, 2315-2345"},
    {4210000, "1515-1610   4210 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1515-1610", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0030-0110, 0300-0410, 1000-1140, 1400-1440, 1900-1940, 2100-2140"},
    {12580700, "1515-1610   12580,7 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1515-1610", "Servicio de Hidrografia Naval (Argentina)\nA lista publica 12579 kHz, mas o sinal sai em 12580,7 kHz (medido no RXSDR: tons em 12580,6 e 12580,8 kHz, shift 200 Hz)\n\nOutros horários nesta frequência: 0030-0110, 1000-1140, 1400-1440, 1900-1940, 2100-2140"},
    {16806500, "1515-1610   16806,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 170, false, "1515-1610", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1630-1720, 1730-1830, 1900-1940, 2315-2345"},
    {8431000, "1520-1530   8431 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "1520-1530", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0720-0730, 0800-0815, 0920-0930, 1320-1330, 2000-2015, 2220-2230"},
    {8416500, "1530-1600   8416,5 kHz - Iqaluit Coast Guard Radio (Canada)", 100, 170, false, "1530-1600", "Iqaluit Coast Guard Radio (Canada)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {6322500, "1600-1610   6322,5 kHz - Murmansk Radio (Russia)", 100, 170, false, "1600-1610", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1000-1010, 1100-1110, 1300-1315"},
    {8424000, "1600-1610   8424 kHz - Murmansk Radio (Russia)", 100, 170, false, "1600-1610", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0000-2400, 0630-0645, 0700-0710, 0930-0945, 1000-1010, 1100-1110, 1300-1315, 2130-2145"},
    {12586000, "1600-1610   12586 kHz - Murmansk Radio (Russia)", 100, 170, false, "1600-1610", "Murmansk Radio (Russia)\n\nOutros horários nesta frequência: 0700-0710, 1000-1010, 1100-1110, 1300-1315"},
    {4212500, "1600-1630   4212,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1600-1630", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 1200-1230, 2000-2030"},
    {8417500, "1600-1630   8417,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1600-1630", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1200-1230, 2000-2030, 2300-2330"},
    {12581500, "1600-1630   12581,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1600-1630", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1200-1230, 2000-2030, 2300-2330"},
    {4228000, "1600-1745   4228 kHz - Kaliningrad Radio Meteo (Russia)", 100, 170, false, "1600-1745", "Kaliningrad Radio Meteo (Russia)\n\nOutros horários nesta frequência: 1000-1035"},
    {8454000, "1600-1745   8454 kHz - Kaliningrad Radio Meteo (Russia)", 100, 170, false, "1600-1745", "Kaliningrad Radio Meteo (Russia)\n\nOutros horários nesta frequência: 1000-1035, 1200-1230"},
    {4209500, "1620-1630   4209,5 kHz - Nha Trang Radio (Vietnam)", 100, 170, false, "1620-1630", "Nha Trang Radio (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1700-1730, 1800-1820, 1800-1845, 1840-1850 ..."},
    {8416500, "1630-1720   8416,5 kHz - USCG Boston (USA)", 100, 170, false, "1630-1720", "USCG Boston (USA)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1730-1830, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {12579000, "1630-1720   12579 kHz - USCG Boston (USA)", 100, 170, false, "1630-1720", "USCG Boston (USA)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1700-1815, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {16806500, "1630-1720   16806,5 kHz - USCG Boston (USA)", 100, 170, false, "1630-1720", "USCG Boston (USA)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1730-1830, 1900-1940, 2315-2345"},
    {6265000, "1700-0500   6265 kHz - Novorossiysk Radio (Russia)", 100, 170, false, "1700-0500", "Novorossiysk Radio (Russia)"},
    {4209500, "1700-1730   4209,5 kHz - Guangzhou Radio (China)", 100, 170, false, "1700-1730", "Guangzhou Radio (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1800-1820, 1800-1845, 1840-1850 ..."},
    {12579000, "1700-1815   12579 kHz - NAVTEX Lisbon (Portugal)", 100, 170, false, "1700-1815", "NAVTEX Lisbon (Portugal)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1900-1940, 2030-2120, 2100-2140, 2250-2300 ..."},
    {8416500, "1730-1830   8416,5 kHz - USCG San Francisco (USA)", 100, 170, false, "1730-1830", "USCG San Francisco (USA)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1900-1940, 1910-2200, 2030-2120, 2100-2140"},
    {16806500, "1730-1830   16806,5 kHz - USCG San Francisco (USA)", 100, 170, false, "1730-1830", "USCG San Francisco (USA)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1900-1940, 2315-2345"},
    {4209500, "1800-1820   4209,5 kHz - Hai Phong Radio Meteo (Vietnam)", 100, 170, false, "1800-1820", "Hai Phong Radio Meteo (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1845, 1840-1850 ..."},
    {16808000, "1800-1830   16808 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "1800-1830", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-0030, 0000-2400, 0600-0630, 0900-0930, 1200-1230, 1400-1430, 2100-2130"},
    {4209500, "1800-1845   4209,5 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "1800-1845", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1840-1850 ..."},
    {4209500, "1840-1850   4209,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "1840-1850", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {4209500, "1900-1910   4209,5 kHz - Guangzhou Radio (China) +1", 100, 170, false, "1900-1910", "Guangzhou Radio (China)\nOlympia Radio METEO (Greece)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {4210000, "1900-1940   4210 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1900-1940", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0030-0110, 0300-0410, 1000-1140, 1400-1440, 1515-1610, 2100-2140"},
    {8416500, "1900-1940   8416,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1900-1940", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1910-2200, 2030-2120, 2100-2140"},
    {12580700, "1900-1940   12580,7 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "1900-1940", "Servicio de Hidrografia Naval (Argentina)\nA lista publica 12579 kHz, mas o sinal sai em 12580,7 kHz (medido no RXSDR: tons em 12580,6 e 12580,8 kHz, shift 200 Hz)\n\nOutros horários nesta frequência: 0030-0110, 1000-1140, 1400-1440, 1515-1610, 2100-2140"},
    {12579000, "1900-1940   12579 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "1900-1940", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0230-0300, 0500-0530, 0900-0925, 1500-1530, 2315-2345"},
    {16806500, "1900-1940   16806,5 kHz - Servicio de Hidrografia Naval (Argentina) +1", 100, 170, false, "1900-1940", "Servicio de Hidrografia Naval (Argentina)\nUSCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 2315-2345"},
    {22376000, "1900-1940   22376 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "1900-1940", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0130-0220, 0230-0300, 0500-0530, 0900-0925, 1500-1530, 2030-2120, 2315-2345"},
    {8416500, "1910-2200   8416,5 kHz - Moskva Radio (Russia)", 100, 170, false, "1910-2200", "Moskva Radio (Russia)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 2030-2120, 2100-2140"},
    {4209500, "1940-1950   4209,5 kHz - Hai Phong Radio Meteo (Vietnam)", 100, 170, false, "1940-1950", "Hai Phong Radio Meteo (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {4560000, "2000-2010   4560 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "2000-2010", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400"},
    {8431000, "2000-2015   8431 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "2000-2015", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0720-0730, 0800-0815, 0920-0930, 1320-1330, 1520-1530, 2220-2230"},
    {12654000, "2000-2015   12654 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "2000-2015", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0800-0815"},
    {4209500, "2000-2030   4209,5 kHz - Guangzhou Radio (China)", 100, 170, false, "2000-2030", "Guangzhou Radio (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {4212500, "2000-2030   4212,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "2000-2030", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 1200-1230, 1600-1630"},
    {8417500, "2000-2030   8417,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "2000-2030", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1200-1230, 1600-1630, 2300-2330"},
    {12581500, "2000-2030   12581,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "2000-2030", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1200-1230, 1600-1630, 2300-2330"},
    {4209500, "2020-2040   4209,5 kHz - Nha Trang Radio (Vietnam)", 100, 170, false, "2020-2040", "Nha Trang Radio (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {8416500, "2030-2120   8416,5 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "2030-2120", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2100-2140"},
    {12579000, "2030-2120   12579 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "2030-2120", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2100-2140, 2250-2300 ..."},
    {22376000, "2030-2120   22376 kHz - USCG Honolulu (Hawaii)", 100, 170, false, "2030-2120", "USCG Honolulu (Hawaii)\n\nOutros horários nesta frequência: 0130-0220, 0230-0300, 0500-0530, 0900-0925, 1500-1530, 1900-1940, 2315-2345"},
    {12799500, "2100-2105   12799,5 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "2100-2105", "Vladivostok Radio Meteo (Russia)\n\nOutros horários nesta frequência: 0015-0030, 0430-0445, 0700-0705, 0900-0905"},
    {4209500, "2100-2130   4209,5 kHz - Guangzhou Radio (China)", 100, 170, false, "2100-2130", "Guangzhou Radio (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {16808000, "2100-2130   16808 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "2100-2130", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-0030, 0000-2400, 0600-0630, 0900-0930, 1200-1230, 1400-1430, 1800-1830"},
    {4210000, "2100-2140   4210 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "2100-2140", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0030-0110, 0300-0410, 1000-1140, 1400-1440, 1515-1610, 1900-1940"},
    {8416500, "2100-2140   8416,5 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "2100-2140", "Servicio de Hidrografia Naval (Argentina)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0130-0220, 0140-0230, 0300-0410, 0330-0400, 0710-0930, 0730-0850, 1000-1300, 1100-1200, 1330-1450, 1400-1500, 1400-1610, 1530-1600, 1630-1720, 1730-1830, 1900-1940, 1910-2200, 2030-2120"},
    {12580700, "2100-2140   12580,7 kHz - Servicio de Hidrografia Naval (Argentina)", 100, 200, false, "2100-2140", "Servicio de Hidrografia Naval (Argentina)\nA lista publica 12579 kHz, mas o sinal sai em 12580,7 kHz (medido no RXSDR: tons em 12580,6 e 12580,8 kHz, shift 200 Hz)\n\nOutros horários nesta frequência: 0030-0110, 1000-1140, 1400-1440, 1515-1610, 1900-1940"},
    {8424000, "2130-2145   8424 kHz - Olympia Radio (Greece)", 100, 170, false, "2130-2145", "Olympia Radio (Greece)\n\nOutros horários nesta frequência: 0000-2400, 0630-0645, 0700-0710, 0930-0945, 1000-1010, 1100-1110, 1300-1315, 1600-1610"},
    {4209500, "2200-2220   4209,5 kHz - Hai Phong Radio Meteo (Vietnam)", 100, 170, false, "2200-2220", "Hai Phong Radio Meteo (Vietnam)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {4209500, "2200-2245   4209,5 kHz - Istanbul Radio Meteo (Turkiye)", 100, 170, false, "2200-2245", "Istanbul Radio Meteo (Turkiye)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {8431000, "2220-2230   8431 kHz - Guangzhou Radio Meteo (China)", 100, 170, false, "2220-2230", "Guangzhou Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0120-0130, 0320-0330, 0720-0730, 0800-0815, 0920-0930, 1320-1330, 1520-1530, 2000-2015"},
    {4209500, "2240-2250   4209,5 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "2240-2250", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {12579000, "2250-2300   12579 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "2250-2300", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140 ..."},
    {6460000, "2300-2310   6460 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "2300-2310", "Vladivostok Radio Meteo (Russia)"},
    {17155000, "2300-2310   17155 kHz - Vladivostok Radio Meteo (Russia)", 100, 170, false, "2300-2310", "Vladivostok Radio Meteo (Russia)"},
    {4209500, "2300-2330   4209,5 kHz - Guangzhou Radio (China)", 100, 170, false, "2300-2330", "Guangzhou Radio (China)\n\nOutros horários nesta frequência: 0000-2400, 0020-0030, 0200-0220, 0200-0245, 0240-0250, 0300-0310, 0420-0430, 0600-0620, 0600-0645, 0640-0650, 0700-0710, 0820-0830, 1000-1020, 1000-1045, 1040-1050, 1220-1230, 1400-1420, 1400-1445, 1440-1450, 1500-1510, 1620-1630, 1700-1730, 1800-1820, 1800-1845 ..."},
    {8417500, "2300-2330   8417,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "2300-2330", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1200-1230, 1600-1630, 2000-2030"},
    {12581500, "2300-2330   12581,5 kHz - Tianjin Radio Meteo (China)", 100, 170, false, "2300-2330", "Tianjin Radio Meteo (China)\n\nOutros horários nesta frequência: 0000-2400, 0500-0530, 0700-0730, 1200-1230, 1600-1630, 2000-2030"},
    {12579000, "2315-2345   12579 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "2315-2345", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140 ..."},
    {16806500, "2315-2345   16806,5 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "2315-2345", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0015-0130, 0030-0110, 0230-0300, 0500-0530, 0900-0925, 1000-1140, 1200-1310, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1730-1830, 1900-1940"},
    {22376000, "2315-2345   22376 kHz - USCG Apra Harbor (Guam)", 100, 170, false, "2315-2345", "USCG Apra Harbor (Guam)\n\nOutros horários nesta frequência: 0130-0220, 0230-0300, 0500-0530, 0900-0925, 1500-1530, 1900-1940, 2030-2120"},
    {12579000, "2350-2400   12579 kHz - Shanghai Radio Meteo (China)", 100, 170, false, "2350-2400", "Shanghai Radio Meteo (China)\n\nOutros horários nesta frequência: 0030-0110, 0130-0220, 0140-0230, 0230-0300, 0250-0300, 0500-0530, 0700-1400, 0730-0850, 0800-0840, 0850-0900, 0900-0925, 1000-1140, 1100-1130, 1300-1340, 1330-1450, 1350-1420, 1400-1440, 1500-1530, 1515-1610, 1630-1720, 1700-1815, 1900-1940, 2030-2120, 2100-2140 ..."},
    {8415000, "sem horário   8415 kHz - Marinha argentina / costeiras", 100, 0, false, "", "Sem horário conhecido"},
    {12578000, "sem horário   12578 kHz - Marinha argentina (Buenos Aires)", 100, 200, false, "", "Sem horário conhecido"},
};
const CanalDec kCanaisDsc[] = {
    {8414500, "0000-2400   8414,5 kHz - DSC HF (a mais movimentada) (165 estações)", 100, 0, false, "0000-2400", "Manaus Radio (Brasil)\nRecife Radio MRCC (Brasil)\nRio de Janeiro Radio MRCC (Brasil)\nAruba Ports Authoritority (Aruba)\nCape Town Radio MRCC (South Africa)\nCOMMSTA Kodiak (Alaska)\nBuenos Aires Radio MRCC (Argentina)\nComodoro Rivadavia Radio (Argentina)\nMar del Plata Radio (Argentina)\nMRSC Ushuaia Radio (Argentina)\nJeddah Radio (Saudi Arabia)\nCharleville/Wiluna Radio (Australia)\nBaku Radio (Azerbaijan)\nDelgada Radio (Azores)\n... e mais 151 estações"},
    {2187500, "0000-2400   2187,5 kHz - DSC costeiro (238 estações)", 100, 0, false, "0000-2400", "Punta Delgada Radio (Chile)\nCape Town Radio MRCC (South Africa)\nAlger Radio (Algeria)\nAnnaba Radio (Algeria)\nCROSS Oran Radio (Algeria)\nBuenos Aires Radio MRCC (Argentina)\nComodoro Rivadavia Radio (Argentina)\nMar del Plata Radio (Argentina)\nMRSC Ushuaia Radio (Argentina)\nJeddah Radio (Saudi Arabia)\nCharleville/Wiluna Radio (Australia)\nBaku Radio (Azerbaijan)\nDelgada Radio (Azores)\nAntwerpen Radio (Belgium)\n... e mais 224 estações"},
    {4207500, "0000-2400   4207,5 kHz - DSC HF (126 estações)", 100, 0, false, "0000-2400", "Manaus Radio (Brasil)\nRecife Radio MRCC (Brasil)\nRio de Janeiro Radio MRCC (Brasil)\nCape Town Radio MRCC (South Africa)\nAnnaba Radio (Algeria)\nGhazaouet Radio (Algeria)\nCOMMSTA Kodiak (Alaska)\nBuenos Aires Radio MRCC (Argentina)\nComodoro Rivadavia Radio (Argentina)\nMar del Plata Radio (Argentina)\nCharleville/Wiluna Radio (Australia)\nBaku Radio (Azerbaijan)\nDelgada Radio (Azores)\nOostende Radio (Belgium)\n... e mais 112 estações"},
    {6312000, "0000-2400   6312 kHz - DSC HF (110 estações)", 100, 0, false, "0000-2400", "Manaus Radio (Brasil)\nRecife Radio MRCC (Brasil)\nRio de Janeiro Radio MRCC (Brasil)\nCape Town Radio MRCC (South Africa)\nCOMMSTA Kodiak (Alaska)\nBuenos Aires Radio MRCC (Argentina)\nComodoro Rivadavia Radio (Argentina)\nMar del Plata Radio (Argentina)\nCharleville/Wiluna Radio (Australia)\nBaku Radio (Azerbaijan)\nDelgada Radio (Azores)\nOostende Radio (Belgium)\nVarna Radio MRCC (Bulgaria)\nIqaluit Coast Guard Radio (Canada)\n... e mais 96 estações"},
    {12577000, "0000-2400   12577 kHz - DSC HF (129 estações)", 100, 0, false, "0000-2400", "Manaus Radio (Brasil)\nRecife Radio MRCC (Brasil)\nRio de Janeiro Radio MRCC (Brasil)\nCape Town Radio MRCC (South Africa)\nCOMMSTA Kodiak (Alaska)\nBuenos Aires Radio MRCC (Argentina)\nComodoro Rivadavia Radio (Argentina)\nMar del Plata Radio (Argentina)\nJeddah Radio (Saudi Arabia)\nCharleville/Wiluna Radio (Australia)\nBaku Radio (Azerbaijan)\nDelgada Radio (Azores)\nAntwerpen Radio (Belgium)\nVarna Radio MRCC (Bulgaria)\n... e mais 115 estações"},
    {16804500, "0000-2400   16804,5 kHz - DSC HF (144 estações)", 100, 0, false, "0000-2400", "Manaus Radio (Brasil)\nRecife Radio MRCC (Brasil)\nRio de Janeiro Radio MRCC (Brasil)\nPunta Delgada Radio (Chile)\nCape Town Radio MRCC (South Africa)\nCOMMSTA Kodiak (Alaska)\nBuenos Aires Radio MRCC (Argentina)\nComodoro Rivadavia Radio (Argentina)\nMar del Plata Radio (Argentina)\nMRSC Ushuaia Radio (Argentina)\nJeddah Radio (Saudi Arabia)\nCharleville/Wiluna Radio (Australia)\nBaku Radio (Azerbaijan)\nDelgada Radio (Azores)\n... e mais 130 estações"},
    {2177000, "0000-2400   2177 kHz - Las Palmas Radio MRCC (Canary Islands) (9 estações)", 100, 0, false, "0000-2400", "Las Palmas Radio MRCC (Canary Islands)\nLyngby Radio MRCC (Denmark)\nCoruna Radio (Spain)\nValencia Radio (Spain)\nReykjavik Radio (Iceland)\nFloroe Radio MRCC (Norway)\nPublic Correspondence (Norway)\nRogaland Radio MRCC (Norway)\nPolish Rescue Radio (Poland)"},
    {2182000, "0000-2400   2182 kHz - Hokkaido Coast Guard Radio (Japan)", 100, 0, false, "0000-2400", "Hokkaido Coast Guard Radio (Japan)"},
    {8392500, "0000-2400   8392,5 kHz - Novorossiysk Radio (Russia)", 100, 0, false, "0000-2400", "Novorossiysk Radio (Russia)"},
    {8414000, "0000-2400   8414 kHz - Dalian Radio (China)", 100, 0, false, "0000-2400", "Dalian Radio (China)"},
    {1656000, "0840-0845   1656 kHz - Valencia Radio (Spain)", 100, 0, false, "0840-0845", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 0903-0908, 1503-1508, 2003-2008, 2303-2308"},
    {1656000, "0903-0908   1656 kHz - Valencia Radio (Spain)", 100, 0, false, "0903-0908", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 0840-0845, 1503-1508, 2003-2008, 2303-2308"},
    {20120000, "1000-0200   20120 kHz - Argentine Military (Argentina)", 100, 0, false, "1000-0200", "Argentine Military (Argentina)"},
    {1704000, "1003-1018   1704 kHz - Valencia Radio (Spain)", 100, 0, false, "1003-1018", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 1533-1548, 2003-2005, 2033-2048, 2333-2348"},
    {1656000, "1503-1508   1656 kHz - Valencia Radio (Spain)", 100, 0, false, "1503-1508", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 0840-0845, 0903-0908, 2003-2008, 2303-2308"},
    {1704000, "1533-1548   1704 kHz - Valencia Radio (Spain)", 100, 0, false, "1533-1548", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 1003-1018, 2003-2005, 2033-2048, 2333-2348"},
    {1704000, "2003-2005   1704 kHz - Valencia Radio (Spain)", 100, 0, false, "2003-2005", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 1003-1018, 1533-1548, 2033-2048, 2333-2348"},
    {1656000, "2003-2008   1656 kHz - Valencia Radio (Spain)", 100, 0, false, "2003-2008", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 0840-0845, 0903-0908, 1503-1508, 2303-2308"},
    {1704000, "2033-2048   1704 kHz - Valencia Radio (Spain)", 100, 0, false, "2033-2048", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 1003-1018, 1533-1548, 2003-2005, 2333-2348"},
    {1656000, "2303-2308   1656 kHz - Valencia Radio (Spain)", 100, 0, false, "2303-2308", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 0840-0845, 0903-0908, 1503-1508, 2003-2008"},
    {1704000, "2333-2348   1704 kHz - Valencia Radio (Spain)", 100, 0, false, "2333-2348", "Valencia Radio (Spain)\n\nOutros horários nesta frequência: 1003-1018, 1533-1548, 2003-2005, 2033-2048"},
};
// PACTOR-I: a frequencia publicada e o centro do FSK (200 Hz de shift); o VFO vai
// 1500 Hz abaixo. A Marinha do Brasil (Rio) transmite meteoromarinha e avisos assim.
const CanalDec kCanaisPactor[] = {
    {12566700, "0000-2400   12566,7 kHz - Marinha do Brasil (PWPU)", 100, 200, false, "0000-2400", "Marinha do Brasil (PWPU)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0600-2400"},
    {6448000, "0230-0330   6448 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0230-0330", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {8580000, "0230-0330   8580 kHz - Marinha do Brasil (Rio) - principal", 100, 200, false, "0230-0330", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {12709000, "0230-0330   12709 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0230-0330", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {16974000, "0230-0330   16974 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0230-0330", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {8582000, "0230-0350   8582 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0230-0350", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0400-0515, 0600-0715, 1430-1545, 1930-2230"},
    {6448000, "0400-0445   6448 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0400-0445", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {8580000, "0400-0445   8580 kHz - Marinha do Brasil (Rio) - principal", 100, 200, false, "0400-0445", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {12709000, "0400-0445   12709 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0400-0445", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {16974000, "0400-0445   16974 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0400-0445", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {6422000, "0400-0515   6422 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0400-0515", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 1930-2200"},
    {6450000, "0400-0515   6450 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0400-0515", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0600-0715, 1430-1545, 1930-2200"},
    {8570000, "0400-0515   8570 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0400-0515", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 1930-2200"},
    {8582000, "0400-0515   8582 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0400-0515", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0230-0350, 0600-0715, 1430-1545, 1930-2230"},
    {6450000, "0600-0715   6450 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0600-0715", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0400-0515, 1430-1545, 1930-2200"},
    {8582000, "0600-0715   8582 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0600-0715", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0230-0350, 0400-0515, 1430-1545, 1930-2230"},
    {6448000, "0600-0730   6448 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0600-0730", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {8580000, "0600-0730   8580 kHz - Marinha do Brasil (Rio) - principal", 100, 200, false, "0600-0730", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {12709000, "0600-0730   12709 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0600-0730", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {16974000, "0600-0730   16974 kHz - Marinha do Brasil (Rio)", 100, 200, false, "0600-0730", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {19502200, "0600-1800   19502,2 kHz - Ministério do Exterior - Cartum (Sudão)", 100, 200, false, "0600-1800", "Ministério do Exterior - Cartum (Sudão)\nPACTOR-I 100 baud, shift 200 Hz"},
    {20204200, "0600-1800   20204,2 kHz - Ministério do Exterior - Cartum (Sudão)", 100, 200, false, "0600-1800", "Ministério do Exterior - Cartum (Sudão)\nPACTOR-I 100 baud, shift 200 Hz"},
    {12566700, "0600-2400   12566,7 kHz - Marinha do Brasil - Rio Grande (PWR44)", 100, 200, false, "0600-2400", "Marinha do Brasil - Rio Grande (PWR44)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0000-2400"},
    {16984000, "0800-0830   16984 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "0800-0830", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 1600-1800, 1930-2155"},
    {6450000, "1430-1545   6450 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1430-1545", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0400-0515, 0600-0715, 1930-2200"},
    {8582000, "1430-1545   8582 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1430-1545", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0230-0350, 0400-0515, 0600-0715, 1930-2230"},
    {12731000, "1430-1545   12731 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1430-1545", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 1930-2155"},
    {16984000, "1600-1800   16984 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1600-1800", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0800-0830, 1930-2155"},
    {6448000, "1845-1930   6448 kHz - Marinha do Brasil (Rio)", 100, 200, false, "1845-1930", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {8580000, "1845-1930   8580 kHz - Marinha do Brasil (Rio) - principal", 100, 200, false, "1845-1930", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {12709000, "1845-1930   12709 kHz - Marinha do Brasil (Rio)", 100, 200, false, "1845-1930", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {16974000, "1845-1930   16974 kHz - Marinha do Brasil (Rio)", 100, 200, false, "1845-1930", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {12701000, "1930-2155   12701 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1930-2155", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)"},
    {12731000, "1930-2155   12731 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1930-2155", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 1430-1545"},
    {16984000, "1930-2155   16984 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1930-2155", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0800-0830, 1600-1800"},
    {6422000, "1930-2200   6422 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1930-2200", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0400-0515"},
    {6450000, "1930-2200   6450 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1930-2200", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0400-0515, 0600-0715, 1430-1545"},
    {8570000, "1930-2200   8570 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1930-2200", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0400-0515"},
    {8582000, "1930-2230   8582 kHz - Marinha do Brasil - Rio de Janeiro (PWZ33)", 100, 200, false, "1930-2230", "Marinha do Brasil - Rio de Janeiro (PWZ33)\nPACTOR-I 100 baud, shift 200 Hz (meteoromarinha / avisos)\n\nOutros horários nesta frequência: 0230-0350, 0400-0515, 0600-0715, 1430-1545"},
    {6448000, "2130-2215   6448 kHz - Marinha do Brasil (Rio)", 100, 200, false, "2130-2215", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {8580000, "2130-2215   8580 kHz - Marinha do Brasil (Rio) - principal", 100, 200, false, "2130-2215", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {12709000, "2130-2215   12709 kHz - Marinha do Brasil (Rio)", 100, 200, false, "2130-2215", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {16974000, "2130-2215   16974 kHz - Marinha do Brasil (Rio)", 100, 200, false, "2130-2215", "Estação Rádio da Marinha no Rio de Janeiro (NAVAREA V), em USB:\nAvisos-Rádio Náuticos e SAR: 0400-0445 e 2130-2215\nMeteoromarinha: 0230-0330, 0600-0730 e 1845-1930\n(PACTOR-I - antes estava na lista do SITOR-B)"},
    {4266000, "sem horário   4266 kHz - Marinha do Brasil (Rio) - só a pedido", 100, 200, false, "", "Sai só a pedido do navegante"},
};

// WEFAX: a frequencia ja e a do mostrador em USB (centro - 1,9 kHz).
// baud = LPM, shift = IOC. Uma linha por transmissao, em ordem de horario UTC.
const CanalDec kCanaisWefax[] = {
    {4316000, "0000-0320   4316 kHz - USCG New Orleans (USA)", 120, 576, false, "0000-0320", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0600-0920, 1200-1520, 1800-2120"},
    {8502000, "0000-0320   8502 kHz - USCG New Orleans (USA)", 120, 576, false, "0000-0320", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0600-0920, 1200-1520, 1800-2120"},
    {12788000, "0000-0320   12788 kHz - USCG New Orleans (USA)", 120, 576, false, "0000-0320", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0600-0920, 1200-1520, 1800-2120"},
    {4197850, "0000-1045   4197,85 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "0000-1045", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1225-1715, 2015-2050"},
    {8410600, "0000-1045   8410,6 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "0000-1045", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1225-1715, 2015-2050"},
    {12627350, "0000-1045   12627,35 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "0000-1045", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1225-1715, 2015-2050"},
    {16824350, "0000-1045   16824,35 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "0000-1045", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1225-1715, 2015-2050"},
    {4168100, "0000-1415   4168,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "0000-1415", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 2000-2055"},
    {8300100, "0000-1415   8300,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "0000-1415", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 2000-2055"},
    {8789100, "0000-1415   8789,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "0000-1415", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 2000-2055"},
    {12380100, "0000-1415   12380,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "0000-1415", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 2000-2055"},
    {16557100, "0000-1415   16557,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "0000-1415", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 2000-2055"},
    {19785100, "0000-1415   19785,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "0000-1415", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 2000-2055"},
    {20492100, "0000-1415   20492,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "0000-1415", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {3583100, "0000-2400   3583,1 kHz - Seoul Meteo (Korea-South)", 120, 576, false, "0000-2400", "Seoul Meteo (Korea-South)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {3620600, "0000-2400   3620,6 kHz - Tokyo Meteo (Japan)", 120, 576, false, "0000-2400", "Tokyo Meteo (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {4314100, "0000-2400   4314,1 kHz - KYODO News Tokyo (Japan)", 60, 576, false, "0000-2400", "KYODO News Tokyo (Japan)\nWEFAX 60 LPM, IOC 576 (USB, frequência do mostrador)"},
    {4608100, "0000-2400   4608,1 kHz - Northwood Meteo JOMOC (United Kingdom)", 120, 576, false, "0000-2400", "Northwood Meteo JOMOC (United Kingdom)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {5098100, "0000-2400   5098,1 kHz - Australia Weather East (Australia)", 120, 576, false, "0000-2400", "Australia Weather East (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {7431600, "0000-2400   7431,6 kHz - Seoul Meteo (Korea-South)", 120, 576, false, "0000-2400", "Seoul Meteo (Korea-South)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {7533100, "0000-2400   7533,1 kHz - Australia Weather West (Australia)", 120, 576, false, "0000-2400", "Australia Weather West (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {7793100, "0000-2400   7793,1 kHz - Tokyo Meteo (Japan)", 120, 576, false, "0000-2400", "Tokyo Meteo (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {8038100, "0000-2400   8038,1 kHz - Northwood Meteo JOMOC (United Kingdom)", 120, 576, false, "0000-2400", "Northwood Meteo JOMOC (United Kingdom)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {8465600, "0000-2400   8465,6 kHz - KYODO News Tokyo (Japan)", 60, 576, false, "0000-2400", "KYODO News Tokyo (Japan)\nWEFAX 60 LPM, IOC 576 (USB, frequência do mostrador)"},
    {8672200, "0000-2400   8672,2 kHz - Northwood Meteo JOMOC (United Kingdom)", 120, 576, false, "0000-2400", "Northwood Meteo JOMOC (United Kingdom)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {9152100, "0000-2400   9152,1 kHz - Seoul Meteo (Korea-South)", 120, 576, false, "0000-2400", "Seoul Meteo (Korea-South)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {9163100, "0000-2400   9163,1 kHz - Seoul Meteo (Korea-South)", 120, 576, false, "0000-2400", "Seoul Meteo (Korea-South)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {10553100, "0000-2400   10553,1 kHz - Australia Weather West (Australia)", 120, 576, false, "0000-2400", "Australia Weather West (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {11028100, "0000-2400   11028,1 kHz - Australia Weather East (Australia)", 120, 576, false, "0000-2400", "Australia Weather East (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {12743600, "0000-2400   12743,6 kHz - KYODO News Tokyo (Japan)", 60, 576, false, "0000-2400", "KYODO News Tokyo (Japan)\nWEFAX 60 LPM, IOC 576 (USB, frequência do mostrador)"},
    {13568100, "0000-2400   13568,1 kHz - Seoul Meteo (Korea-South)", 120, 576, false, "0000-2400", "Seoul Meteo (Korea-South)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {13918100, "0000-2400   13918,1 kHz - Australia Weather East (Australia)", 120, 576, false, "0000-2400", "Australia Weather East (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {13986600, "0000-2400   13986,6 kHz - Tokyo Meteo (Japan)", 120, 576, false, "0000-2400", "Tokyo Meteo (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {15613100, "0000-2400   15613,1 kHz - Australia Weather West (Australia)", 120, 576, false, "0000-2400", "Australia Weather West (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {16133100, "0000-2400   16133,1 kHz - Honolulu Meteo (Hawaii)", 120, 576, false, "0000-2400", "Honolulu Meteo (Hawaii)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {16969100, "0000-2400   16969,1 kHz - KYODO News Tokyo (Japan)", 60, 576, false, "0000-2400", "KYODO News Tokyo (Japan)\nWEFAX 60 LPM, IOC 576 (USB, frequência do mostrador)"},
    {17067700, "0000-2400   17067,7 kHz - KYODO News Tokyo (Japan)", 60, 576, false, "0000-2400", "KYODO News Tokyo (Japan)\nWEFAX 60 LPM, IOC 576 (USB, frequência do mostrador)"},
    {22540100, "0000-2400   22540,1 kHz - KYODO News Tokyo (Japan)", 60, 576, false, "0000-2400", "KYODO News Tokyo (Japan)\nWEFAX 60 LPM, IOC 576 (USB, frequência do mostrador)"},
    {8656100, "0015-0045   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "0015-0045", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0330-0400, 0430-0500, 0600-0630, 0700-0730, 0830-0840, 0930-0945, 1100-1115, 1800-1815, 2200-2315"},
    {13072100, "0015-0050   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0015-0050", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0600-0645, 0730-0800, 0830-0840, 0930-0945, 1700-1715, 2200-2215, 2300-2315"},
    {16907500, "0030-0045   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0030-0045", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0130-0200, 0300-0330, 0400-0430, 0600-0630, 0700-0730, 0810-0845, 1030-1050"},
    {7395000, "0050-0620   7395 kHz - Bangkok Meteorological (Thailand)", 120, 576, false, "0050-0620", "Bangkok Meteorological (Thailand)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0700-0840, 1000-1042, 1300-1322, 1700-1742, 2300-2342"},
    {7708100, "0100-0300   7708,1 kHz - VFR Resolute (Canada)", 120, 576, false, "0100-0300", "VFR Resolute (Canada)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0600-0800, 1000-1200"},
    {16907500, "0130-0200   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0130-0200", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0030-0045, 0300-0330, 0400-0430, 0600-0630, 0700-0730, 0810-0845, 1030-1050"},
    {4344100, "0140-0420   4344,1 kHz - USCG San Francisco (USA)", 120, 576, false, "0140-0420", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0655-1030, 1120-1237, 1400-1630"},
    {8680100, "0140-0420   8680,1 kHz - USCG San Francisco (USA)", 120, 576, false, "0140-0420", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0655-1030, 1120-1237, 1400-1630, 1840-2356"},
    {12784100, "0140-0420   12784,1 kHz - USCG San Francisco (USA)", 120, 576, false, "0140-0420", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0655-1030, 1120-1237, 1400-1630, 1840-2356"},
    {17149300, "0140-0420   17149,3 kHz - USCG San Francisco (USA)", 120, 576, false, "0140-0420", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0655-1030, 1120-1237, 1400-1630, 1840-2356"},
    {8442100, "0200-0300   8442,1 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "0200-0300", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0700-0715, 1000-1100, 1330-1350, 2000-2020"},
    {4233100, "0230-0455   4233,1 kHz - USCG Boston (USA)", 120, 576, false, "0230-0455", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0745-1039"},
    {6338600, "0230-0455   6338,6 kHz - USCG Boston (USA)", 120, 576, false, "0230-0455", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0745-1039, 1400-1610, 1720-2315"},
    {9108100, "0230-0455   9108,1 kHz - USCG Boston (USA)", 120, 576, false, "0230-0455", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0745-1039, 1400-1610, 1720-2315"},
    {16907500, "0300-0330   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0300-0330", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0030-0045, 0130-0200, 0400-0430, 0600-0630, 0700-0730, 0810-0845, 1030-1050"},
    {8656100, "0330-0400   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "0330-0400", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0430-0500, 0600-0630, 0700-0730, 0830-0840, 0930-0945, 1100-1115, 1800-1815, 2200-2315"},
    {2052100, "0340-0608   2052,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0340-0608", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0950-1213, 1540-1818, 2150-0028"},
    {4296100, "0340-0608   4296,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0340-0608", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0950-1213, 1540-1818, 2150-0028"},
    {8457100, "0340-0608   8457,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0340-0608", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0950-1213, 1540-1818, 2150-0028"},
    {12410600, "0340-0608   12410,6 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0340-0608", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0950-1213, 1540-1818, 2150-0028"},
    {4320100, "0350-0415   4320,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "0350-0415", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1550-1630, 1730-1755, 2005-2040, 2240-2340"},
    {8694100, "0350-0415   8694,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "0350-0415", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1550-1630, 1730-1755, 2005-2040, 2240-2340"},
    {16907500, "0400-0430   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0400-0430", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0030-0045, 0130-0200, 0300-0330, 0600-0630, 0700-0730, 0810-0845, 1030-1050"},
    {22559600, "0400-0430   22559,6 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0400-0430", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0500-0530, 0600-0630, 0700-0730, 0800-0830"},
    {8656100, "0430-0500   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "0430-0500", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0600-0630, 0700-0730, 0830-0840, 0930-0945, 1100-1115, 1800-1815, 2200-2315"},
    {3853100, "0430-2400   3853,1 kHz - Deutscher Wetterdienst (Germany)", 120, 576, false, "0430-2400", "Deutscher Wetterdienst (Germany)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {7878100, "0430-2400   7878,1 kHz - Deutscher Wetterdienst (Germany)", 120, 576, false, "0430-2400", "Deutscher Wetterdienst (Germany)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {13880600, "0430-2400   13880,6 kHz - Deutscher Wetterdienst (Germany)", 120, 576, false, "0430-2400", "Deutscher Wetterdienst (Germany)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {15986400, "0430-2400   15986,4 kHz - Deutscher Wetterdienst (Germany)", 120, 576, false, "0430-2400", "Deutscher Wetterdienst (Germany)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {22559600, "0500-0530   22559,6 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0500-0530", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0400-0430, 0600-0630, 0700-0730, 0800-0830"},
    {9980600, "0519-1556   9980,6 kHz - Honolulu Meteo (Hawaii)", 120, 576, false, "0519-1556", "Honolulu Meteo (Hawaii)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {11088100, "0519-1556   11088,1 kHz - Honolulu Meteo (Hawaii)", 120, 576, false, "0519-1556", "Honolulu Meteo (Hawaii)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1719-0356"},
    {8656100, "0600-0630   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "0600-0630", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0430-0500, 0700-0730, 0830-0840, 0930-0945, 1100-1115, 1800-1815, 2200-2315"},
    {16907500, "0600-0630   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0600-0630", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0030-0045, 0130-0200, 0300-0330, 0400-0430, 0700-0730, 0810-0845, 1030-1050"},
    {22559600, "0600-0630   22559,6 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0600-0630", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0400-0430, 0500-0530, 0700-0730, 0800-0830"},
    {13072100, "0600-0645   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0600-0645", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0050, 0730-0800, 0830-0840, 0930-0945, 1700-1715, 2200-2215, 2300-2315"},
    {7708100, "0600-0800   7708,1 kHz - VFR Resolute (Canada)", 120, 576, false, "0600-0800", "VFR Resolute (Canada)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0100-0300, 1000-1200"},
    {4316000, "0600-0920   4316 kHz - USCG New Orleans (USA)", 120, 576, false, "0600-0920", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 1200-1520, 1800-2120"},
    {8502000, "0600-0920   8502 kHz - USCG New Orleans (USA)", 120, 576, false, "0600-0920", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 1200-1520, 1800-2120"},
    {12788000, "0600-0920   12788 kHz - USCG New Orleans (USA)", 120, 576, false, "0600-0920", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 1200-1520, 1800-2120"},
    {11084600, "0600-2100   11084,6 kHz - Northwood Meteo JOMOC (United Kingdom)", 120, 576, false, "0600-2100", "Northwood Meteo JOMOC (United Kingdom)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {4344100, "0655-1030   4344,1 kHz - USCG San Francisco (USA)", 120, 576, false, "0655-1030", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 1120-1237, 1400-1630"},
    {8680100, "0655-1030   8680,1 kHz - USCG San Francisco (USA)", 120, 576, false, "0655-1030", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 1120-1237, 1400-1630, 1840-2356"},
    {12784100, "0655-1030   12784,1 kHz - USCG San Francisco (USA)", 120, 576, false, "0655-1030", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 1120-1237, 1400-1630, 1840-2356"},
    {17149300, "0655-1030   17149,3 kHz - USCG San Francisco (USA)", 120, 576, false, "0655-1030", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 1120-1237, 1400-1630, 1840-2356"},
    {8442100, "0700-0715   8442,1 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "0700-0715", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0200-0300, 1000-1100, 1330-1350, 2000-2020"},
    {8656100, "0700-0730   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "0700-0730", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0430-0500, 0600-0630, 0830-0840, 0930-0945, 1100-1115, 1800-1815, 2200-2315"},
    {16907500, "0700-0730   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0700-0730", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0030-0045, 0130-0200, 0300-0330, 0400-0430, 0600-0630, 0810-0845, 1030-1050"},
    {22559600, "0700-0730   22559,6 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0700-0730", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0400-0430, 0500-0530, 0600-0630, 0800-0830"},
    {17231000, "0700-0745   17231 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0700-0745", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {7395000, "0700-0840   7395 kHz - Bangkok Meteorological (Thailand)", 120, 576, false, "0700-0840", "Bangkok Meteorological (Thailand)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0050-0620, 1000-1042, 1300-1322, 1700-1742, 2300-2342"},
    {13072100, "0730-0800   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0730-0800", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0050, 0600-0645, 0830-0840, 0930-0945, 1700-1715, 2200-2215, 2300-2315"},
    {4233100, "0745-1039   4233,1 kHz - USCG Boston (USA)", 120, 576, false, "0745-1039", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0230-0455"},
    {6338600, "0745-1039   6338,6 kHz - USCG Boston (USA)", 120, 576, false, "0745-1039", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0230-0455, 1400-1610, 1720-2315"},
    {9108100, "0745-1039   9108,1 kHz - USCG Boston (USA)", 120, 576, false, "0745-1039", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0230-0455, 1400-1610, 1720-2315"},
    {12748100, "0745-1039   12748,1 kHz - USCG Boston (USA)", 120, 576, false, "0745-1039", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1400-1610, 1720-2315"},
    {22559600, "0800-0830   22559,6 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0800-0830", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0400-0430, 0500-0530, 0600-0630, 0700-0730"},
    {16907500, "0810-0845   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0810-0845", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0030-0045, 0130-0200, 0300-0330, 0400-0430, 0600-0630, 0700-0730, 1030-1050"},
    {22557600, "0810-0845   22557,6 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0810-0845", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {8105000, "0815-1055   8105 kHz - Hellenic National Meteo Sce. (Greece)", 120, 576, false, "0815-1055", "Hellenic National Meteo Sce. (Greece)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {8656100, "0830-0840   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "0830-0840", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0430-0500, 0600-0630, 0700-0730, 0930-0945, 1100-1115, 1800-1815, 2200-2315"},
    {13072100, "0830-0840   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0830-0840", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0050, 0600-0645, 0730-0800, 0930-0945, 1700-1715, 2200-2215, 2300-2315"},
    {4481000, "0835-1055   4481 kHz - Hellenic National Meteo Sce. (Greece)", 120, 576, false, "0835-1055", "Hellenic National Meteo Sce. (Greece)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {2626100, "0900-1900   2626,1 kHz - Australia Weather East (Australia)", 120, 576, false, "0900-1900", "Australia Weather East (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {8656100, "0930-0945   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "0930-0945", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0430-0500, 0600-0630, 0700-0730, 0830-0840, 1100-1115, 1800-1815, 2200-2315"},
    {13072100, "0930-0945   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "0930-0945", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0050, 0600-0645, 0730-0800, 0830-0840, 1700-1715, 2200-2215, 2300-2315"},
    {6412600, "0930-1030   6412,6 kHz - Fukushima Prefec.Fishery Radio (Japan)", 120, 576, false, "0930-1030", "Fukushima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {2052100, "0950-1213   2052,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0950-1213", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 1540-1818, 2150-0028"},
    {4296100, "0950-1213   4296,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0950-1213", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 1540-1818, 2150-0028"},
    {8457100, "0950-1213   8457,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0950-1213", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 1540-1818, 2150-0028"},
    {12410600, "0950-1213   12410,6 kHz - USCG Kodiak (Alaska)", 120, 576, false, "0950-1213", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 1540-1818, 2150-0028"},
    {4205500, "1000-1020   4205,5 kHz - Canadian Forces (Canada)", 120, 576, false, "1000-1020", "Canadian Forces (Canada)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {4272100, "1000-1020   4272,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "1000-1020", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {7395000, "1000-1042   7395 kHz - Bangkok Meteorological (Thailand)", 120, 576, false, "1000-1042", "Bangkok Meteorological (Thailand)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0050-0620, 0700-0840, 1300-1322, 1700-1742, 2300-2342"},
    {6326000, "1000-1100   6326 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "1000-1100", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1330-1345, 1430-1500, 2000-2020"},
    {8442100, "1000-1100   8442,1 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "1000-1100", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0200-0300, 0700-0715, 1330-1350, 2000-2020"},
    {7708100, "1000-1200   7708,1 kHz - VFR Resolute (Canada)", 120, 576, false, "1000-1200", "VFR Resolute (Canada)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0100-0300, 0600-0800"},
    {16907500, "1030-1050   16907,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "1030-1050", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0030-0045, 0130-0200, 0300-0330, 0400-0430, 0600-0630, 0700-0730, 0810-0845"},
    {16905500, "1030-1100   16905,5 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "1030-1100", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {4226100, "1058-1145   4226,1 kHz - Valparaiso Radio (Chile)", 120, 576, false, "1058-1145", "Valparaiso Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1628-1715, 1913-2000, 2158-2345"},
    {8675100, "1058-1145   8675,1 kHz - Valparaiso Meteo Radio (Chile)", 120, 576, false, "1058-1145", "Valparaiso Meteo Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1628-1715, 1913-2000, 2158-2345"},
    {17144500, "1058-1145   17144,5 kHz - Valparaiso Meteo Radio (Chile)", 120, 576, false, "1058-1145", "Valparaiso Meteo Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1200-1520, 1628-1705, 1800-2120, 2158-2345"},
    {8656100, "1100-1115   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "1100-1115", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0430-0500, 0600-0630, 0700-0730, 0830-0840, 0930-0945, 1800-1815, 2200-2315"},
    {5753100, "1100-2100   5753,1 kHz - Australia Weather West (Australia)", 120, 576, false, "1100-2100", "Australia Weather West (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {4344100, "1120-1237   4344,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1120-1237", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1400-1630"},
    {8680100, "1120-1237   8680,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1120-1237", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1400-1630, 1840-2356"},
    {12784100, "1120-1237   12784,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1120-1237", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1400-1630, 1840-2356"},
    {17149300, "1120-1237   17149,3 kHz - USCG San Francisco (USA)", 120, 576, false, "1120-1237", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1400-1630, 1840-2356"},
    {4316000, "1200-1520   4316 kHz - USCG New Orleans (USA)", 120, 576, false, "1200-1520", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 0600-0920, 1800-2120"},
    {8502000, "1200-1520   8502 kHz - USCG New Orleans (USA)", 120, 576, false, "1200-1520", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 0600-0920, 1800-2120"},
    {12788000, "1200-1520   12788 kHz - USCG New Orleans (USA)", 120, 576, false, "1200-1520", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 0600-0920, 1800-2120"},
    {17144500, "1200-1520   17144,5 kHz - USCG New Orleans (USA)", 120, 576, false, "1200-1520", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1628-1705, 1800-2120, 2158-2345"},
    {4197850, "1225-1715   4197,85 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "1225-1715", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 2015-2050"},
    {8410600, "1225-1715   8410,6 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "1225-1715", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 2015-2050"},
    {12627350, "1225-1715   12627,35 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "1225-1715", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 2015-2050"},
    {16824350, "1225-1715   16824,35 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "1225-1715", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 2015-2050"},
    {7395000, "1300-1322   7395 kHz - Bangkok Meteorological (Thailand)", 120, 576, false, "1300-1322", "Bangkok Meteorological (Thailand)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0050-0620, 0700-0840, 1000-1042, 1700-1742, 2300-2342"},
    {6326000, "1330-1345   6326 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "1330-1345", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1000-1100, 1430-1500, 2000-2020"},
    {8442100, "1330-1350   8442,1 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "1330-1350", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0200-0300, 0700-0715, 1000-1100, 2000-2020"},
    {6338600, "1400-1610   6338,6 kHz - USCG Boston (USA)", 120, 576, false, "1400-1610", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0230-0455, 0745-1039, 1720-2315"},
    {9108100, "1400-1610   9108,1 kHz - USCG Boston (USA)", 120, 576, false, "1400-1610", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0230-0455, 0745-1039, 1720-2315"},
    {12748100, "1400-1610   12748,1 kHz - USCG Boston (USA)", 120, 576, false, "1400-1610", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0745-1039, 1720-2315"},
    {4344100, "1400-1630   4344,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1400-1630", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1120-1237"},
    {8680100, "1400-1630   8680,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1400-1630", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1120-1237, 1840-2356"},
    {12784100, "1400-1630   12784,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1400-1630", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1120-1237, 1840-2356"},
    {17149300, "1400-1630   17149,3 kHz - USCG San Francisco (USA)", 120, 576, false, "1400-1630", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1120-1237, 1840-2356"},
    {6326000, "1430-1500   6326 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "1430-1500", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1000-1100, 1330-1345, 2000-2020"},
    {2616600, "1500-0800   2616,6 kHz - Northwood Meteo JOMOC (United Kingdom)", 120, 576, false, "1500-0800", "Northwood Meteo JOMOC (United Kingdom)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {2052100, "1540-1818   2052,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "1540-1818", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 2150-0028"},
    {4296100, "1540-1818   4296,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "1540-1818", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 2150-0028"},
    {8457100, "1540-1818   8457,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "1540-1818", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 2150-0028"},
    {12410600, "1540-1818   12410,6 kHz - USCG Kodiak (Alaska)", 120, 576, false, "1540-1818", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 2150-0028"},
    {4320100, "1550-1630   4320,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "1550-1630", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1730-1755, 2005-2040, 2240-2340"},
    {8694100, "1550-1630   8694,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "1550-1630", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1730-1755, 2005-2040, 2240-2340"},
    {17144500, "1628-1705   17144,5 kHz - Valparaiso Meteo Radio (Chile)", 120, 576, false, "1628-1705", "Valparaiso Meteo Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1200-1520, 1800-2120, 2158-2345"},
    {4226100, "1628-1715   4226,1 kHz - Valparaiso Radio (Chile)", 120, 576, false, "1628-1715", "Valparaiso Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1913-2000, 2158-2345"},
    {8675100, "1628-1715   8675,1 kHz - Valparaiso Meteo Radio (Chile)", 120, 576, false, "1628-1715", "Valparaiso Meteo Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1913-2000, 2158-2345"},
    {13072100, "1700-1715   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "1700-1715", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0050, 0600-0645, 0730-0800, 0830-0840, 0930-0945, 2200-2215, 2300-2315"},
    {7395000, "1700-1742   7395 kHz - Bangkok Meteorological (Thailand)", 120, 576, false, "1700-1742", "Bangkok Meteorological (Thailand)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0050-0620, 0700-0840, 1000-1042, 1300-1322, 2300-2342"},
    {11088100, "1719-0356   11088,1 kHz - Honolulu Meteo (Hawaii)", 120, 576, false, "1719-0356", "Honolulu Meteo (Hawaii)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0519-1556"},
    {6338600, "1720-2315   6338,6 kHz - USCG Boston (USA)", 120, 576, false, "1720-2315", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0230-0455, 0745-1039, 1400-1610"},
    {9108100, "1720-2315   9108,1 kHz - USCG Boston (USA)", 120, 576, false, "1720-2315", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0230-0455, 0745-1039, 1400-1610"},
    {12748100, "1720-2315   12748,1 kHz - USCG Boston (USA)", 120, 576, false, "1720-2315", "USCG Boston (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0745-1039, 1400-1610"},
    {4320100, "1730-1755   4320,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "1730-1755", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1550-1630, 2005-2040, 2240-2340"},
    {8694100, "1730-1755   8694,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "1730-1755", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1550-1630, 2005-2040, 2240-2340"},
    {8656100, "1800-1815   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "1800-1815", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0430-0500, 0600-0630, 0700-0730, 0830-0840, 0930-0945, 1100-1115, 2200-2315"},
    {4316000, "1800-2120   4316 kHz - USCG New Orleans (USA)", 120, 576, false, "1800-2120", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 0600-0920, 1200-1520"},
    {8502000, "1800-2120   8502 kHz - USCG New Orleans (USA)", 120, 576, false, "1800-2120", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 0600-0920, 1200-1520"},
    {12788000, "1800-2120   12788 kHz - USCG New Orleans (USA)", 120, 576, false, "1800-2120", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-0320, 0600-0920, 1200-1520"},
    {17144500, "1800-2120   17144,5 kHz - USCG New Orleans (USA)", 120, 576, false, "1800-2120", "USCG New Orleans (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1200-1520, 1628-1705, 2158-2345"},
    {8680100, "1840-2356   8680,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1840-2356", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1120-1237, 1400-1630"},
    {12784100, "1840-2356   12784,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1840-2356", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1120-1237, 1400-1630"},
    {17149300, "1840-2356   17149,3 kHz - USCG San Francisco (USA)", 120, 576, false, "1840-2356", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0140-0420, 0655-1030, 1120-1237, 1400-1630"},
    {22525100, "1840-2356   22525,1 kHz - USCG San Francisco (USA)", 120, 576, false, "1840-2356", "USCG San Francisco (USA)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {20467100, "1900-0900   20467,1 kHz - Australia Weather East (Australia)", 120, 576, false, "1900-0900", "Australia Weather East (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {4226100, "1913-2000   4226,1 kHz - Valparaiso Radio (Chile)", 120, 576, false, "1913-2000", "Valparaiso Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1628-1715, 2158-2345"},
    {8675100, "1913-2000   8675,1 kHz - Valparaiso Meteo Radio (Chile)", 120, 576, false, "1913-2000", "Valparaiso Meteo Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1628-1715, 2158-2345"},
    {6326000, "2000-2020   6326 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "2000-2020", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1000-1100, 1330-1345, 1430-1500"},
    {8442100, "2000-2020   8442,1 kHz - Murmansk Meteo Radio (Russia)", 120, 576, false, "2000-2020", "Murmansk Meteo Radio (Russia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0200-0300, 0700-0715, 1000-1100, 1330-1350"},
    {4168100, "2000-2055   4168,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "2000-2055", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1415"},
    {8300100, "2000-2055   8300,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "2000-2055", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1415"},
    {8789100, "2000-2055   8789,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "2000-2055", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1415"},
    {12380100, "2000-2055   12380,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "2000-2055", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1415"},
    {16557100, "2000-2055   16557,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "2000-2055", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1415"},
    {19785100, "2000-2055   19785,1 kHz - Shanghai Radio Meteo (China)", 120, 576, false, "2000-2055", "Shanghai Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1415"},
    {4320100, "2005-2040   4320,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "2005-2040", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1550-1630, 1730-1755, 2240-2340"},
    {8694100, "2005-2040   8694,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "2005-2040", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1550-1630, 1730-1755, 2240-2340"},
    {4197850, "2015-2050   4197,85 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "2015-2050", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 1225-1715"},
    {8410600, "2015-2050   8410,6 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "2015-2050", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 1225-1715"},
    {12627350, "2015-2050   12627,35 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "2015-2050", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 1225-1715"},
    {16824350, "2015-2050   16824,35 kHz - Guangzhou Radio Meteo (China)", 120, 576, false, "2015-2050", "Guangzhou Radio Meteo (China)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0000-1045, 1225-1715"},
    {18058100, "2100-1100   18058,1 kHz - Australia Weather West (Australia)", 120, 576, false, "2100-1100", "Australia Weather West (Australia)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)"},
    {2052100, "2150-0028   2052,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "2150-0028", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 1540-1818"},
    {4296100, "2150-0028   4296,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "2150-0028", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 1540-1818"},
    {8457100, "2150-0028   8457,1 kHz - USCG Kodiak (Alaska)", 120, 576, false, "2150-0028", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 1540-1818"},
    {12410600, "2150-0028   12410,6 kHz - USCG Kodiak (Alaska)", 120, 576, false, "2150-0028", "USCG Kodiak (Alaska)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0340-0608, 0950-1213, 1540-1818"},
    {4226100, "2158-2345   4226,1 kHz - Valparaiso Radio (Chile)", 120, 576, false, "2158-2345", "Valparaiso Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1628-1715, 1913-2000"},
    {8675100, "2158-2345   8675,1 kHz - Valparaiso Meteo Radio (Chile)", 120, 576, false, "2158-2345", "Valparaiso Meteo Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1628-1715, 1913-2000"},
    {17144500, "2158-2345   17144,5 kHz - Valparaiso Meteo Radio (Chile)", 120, 576, false, "2158-2345", "Valparaiso Meteo Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 1058-1145, 1200-1520, 1628-1705, 1800-2120"},
    {13072100, "2200-2215   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "2200-2215", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0050, 0600-0645, 0730-0800, 0830-0840, 0930-0945, 1700-1715, 2300-2315"},
    {8656100, "2200-2315   8656,1 kHz - Kagoshima Prefec.Fishery Radio (Japan)", 120, 576, false, "2200-2315", "Kagoshima Prefec.Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0045, 0330-0400, 0430-0500, 0600-0630, 0700-0730, 0830-0840, 0930-0945, 1100-1115, 1800-1815"},
    {4320100, "2240-2340   4320,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "2240-2340", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1550-1630, 1730-1755, 2005-2040"},
    {8694100, "2240-2340   8694,1 kHz - Magallanes Radio (Chile)", 120, 576, false, "2240-2340", "Magallanes Radio (Chile)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0350-0415, 1550-1630, 1730-1755, 2005-2040"},
    {13072100, "2300-2315   13072,1 kHz - Misaki Fishery Radio (Japan)", 120, 576, false, "2300-2315", "Misaki Fishery Radio (Japan)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0015-0050, 0600-0645, 0730-0800, 0830-0840, 0930-0945, 1700-1715, 2200-2215"},
    {7395000, "2300-2342   7395 kHz - Bangkok Meteorological (Thailand)", 120, 576, false, "2300-2342", "Bangkok Meteorological (Thailand)\nWEFAX 120 LPM, IOC 576 (USB, frequência do mostrador)\n\nOutros horários nesta frequência: 0050-0620, 0700-0840, 1000-1042, 1300-1322, 1700-1742"},
};

const CanalDec kCanaisAle[] = {
    {14109000, "14109 kHz - Radioamador HFN - 20 m (o mais ativo)", 0, 0, false},
    {7102000, "7102 kHz - Radioamador HFN - 40 m", 0, 0, false},
    {10136500, "10136,5 kHz - Radioamador HFN - 30 m", 0, 0, false},
    {18106000, "18106 kHz - Radioamador HFN - 17 m", 0, 0, false},
    {21096000, "21096 kHz - Radioamador HFN - 15 m", 0, 0, false},
    {3596000, "3596 kHz - Radioamador HFN - 80 m", 0, 0, false},
    {5357000, "5357 kHz - Radioamador HFN - 60 m", 0, 0, false},
    {24926000, "24926 kHz - Radioamador HFN - 12 m", 0, 0, false},
    {28146000, "28146 kHz - Radioamador HFN - 10 m", 0, 0, false},
#include "CanaisAleBrasil.inc"
};
const float kBauds[] = {45.45f, 50, 75, 100};
const float kShifts[] = {170, 200, 425, 450, 850};

// "hhmm-hhmm hhmm-hhmm": alguma janela contem a hora UTC de agora?
bool canalNoAr(const char* hor)
{
    if (!hor || !*hor) return false;
    SYSTEMTIME st; GetSystemTime(&st);
    const int agora = st.wHour * 60 + st.wMinute;
    int a, b, n = 0;
    for (const char* p = hor; *p; p += n) {
        if (std::sscanf(p, " %4d-%4d%n", &a, &b, &n) != 2 || n <= 0) break;
        const int ma = a / 100 * 60 + a % 100, mb = b / 100 * 60 + b % 100;
        if (ma <= mb ? (agora >= ma && agora < mb) : (agora >= ma || agora < mb)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  Busca nas listas de canais (SITOR-B, DSC, RTTY, ALE, PACTOR-I, WEFAX e
//  as emissoras do DRM): saber se uma frequencia ja esta
//  na lista sem rolar centenas de linhas.
//    * so numeros (com ponto ou virgula): compara com a frequencia do canal,
//      do jeito que se escreve no dia a dia - "8416,5", "8416.5", "8.416.5",
//      "8,4165" e "8416500" acham todos o canal de 8416,5 kHz. Vale ir
//      digitando: "841" ja mostra tudo que comeca por 841x kHz.
//    * com letras: procura no nome e na dica (estacao, pais) - "argentina".
// ---------------------------------------------------------------------------
static std::string soDigitos(const std::string& s)
{
    std::string d;
    for (char ch : s) if (ch >= '0' && ch <= '9') d += ch;
    return d;
}
static std::string minusculas(const char* s)
{
    std::string r = s ? s : "";
    for (auto& ch : r) ch = (char)std::tolower((unsigned char)ch);
    return r;
}
bool buscaBate(uint64_t hzCanal, const std::string& texto, const std::string& busca)
{
    if (busca.empty()) return true;
    bool soNumero = true;
    for (char ch : busca)
        if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == ',' || ch == ' ')) { soNumero = false; break; }
    if (soNumero) {
        std::string q = soDigitos(busca);
        while (q.size() > 1 && q[0] == '0') q.erase(0, 1);   // "08416" = "8416"
        if (q.empty()) return true;
        // kHz sem a virgula (8416500 Hz -> "84165") e em Hz ("8416500")
        char k[32];
        const uint64_t khz = hzCanal / 1000, resto = hzCanal % 1000;
        if (resto == 0) std::snprintf(k, sizeof k, "%llu", (unsigned long long)khz);
        else {
            std::snprintf(k, sizeof k, "%llu%03llu", (unsigned long long)khz, (unsigned long long)resto);
            size_t n = std::strlen(k);
            while (n > 0 && k[n - 1] == '0') k[--n] = 0;
        }
        const std::string hz = std::to_string((unsigned long long)hzCanal);
        return std::string(k).rfind(q, 0) == 0 || hz.rfind(q, 0) == 0;
    }
    return minusculas(texto.c_str()).find(minusculas(busca.c_str())) != std::string::npos;
}
bool canalPassaBusca(const CanalDec& c, const std::string& busca)
{
    if (busca.empty()) return true;
    return buscaBate(c.hz, std::string(c.nome ? c.nome : "") + "\n" + (c.dica ? c.dica : ""), busca);
}

// Campo de busca no topo de uma lista aberta (combo): limpa e recebe o cursor
// ao abrir. Devolve o texto aparado; 'achados'/'total' escrevem o resultado.
std::string campoBuscaLista(const char* dica)
{
    static char busca[64] = "";
    if (ImGui::IsWindowAppearing()) { busca[0] = 0; ImGui::SetKeyboardFocusHere(); }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##buscacanal", dica, busca, sizeof busca);
    std::string q = busca;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    while (!q.empty() && q.front() == ' ') q.erase(0, 1);
    return q;
}
void resultadoBusca(const std::string& q, int achados)
{
    if (q.empty()) return;
    if (achados) ImGui::TextColored(ImVec4(0.3f, 1.f, 0.5f, 1), "%d na lista com \"%s\"", achados, q.c_str());
    else ImGui::TextColored(ImVec4(1.f, 0.55f, 0.3f, 1), "\"%s\" não está na lista", q.c_str());
}

template <size_t N>
bool comboCanal(const char* id, const CanalDec (&c)[N], int& sel, bool comBusca = true)
{
    bool mudou = false;
    const char* prev = sel >= 0 && sel < (int)N ? c[sel].nome : "- escolha para sintonizar -";
    if (ImGui::BeginCombo(id, prev, ImGuiComboFlags_HeightLarge)) {
        // ao abrir, a lista (em ordem de horario UTC) rola ate o escolhido ou,
        // sem escolha, ate as transmissoes que comecam agora (meia hora atras em diante)
        bool rolar = ImGui::IsWindowAppearing();
        std::string q;
        if (comBusca) {
            q = campoBuscaLista("buscar: frequência (8416,5 ou 8.416.5) ou nome (argentina)");
            if (!q.empty()) {
                int achados = 0;
                for (int i = 0; i < (int)N; ++i) if (c[i].hz != 0 && canalPassaBusca(c[i], q)) ++achados;
                resultadoBusca(q, achados);
                rolar = false;
            }
            ImGui::Separator();
            ImGui::BeginChild("##listacanais", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 20));
        }
        SYSTEMTIME st; GetSystemTime(&st);
        const int agora = st.wHour * 60 + st.wMinute;
        for (int i = 0; i < (int)N; ++i) {
            if (c[i].hz == 0) { if (q.empty()) ImGui::SeparatorText(c[i].nome); continue; }   // titulo de grupo
            if (!q.empty() && !canalPassaBusca(c[i], q)) continue;
            const bool noAr = canalNoAr(c[i].hor);
            int hi = -1, ini = -1;
            if (c[i].hor && std::sscanf(c[i].hor, "%4d", &hi) == 1) ini = hi / 100 * 60 + hi % 100;
            if (rolar && (sel >= 0 ? i == sel : ini >= agora - 30)) { ImGui::SetScrollHereY(0.1f); rolar = false; }
            if (noAr) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.f, 0.5f, 1));
            ImGui::PushID(i);
            if (ImGui::Selectable(c[i].nome, sel == i)) { sel = i; mudou = true; }
            ImGui::PopID();
            if (noAr) ImGui::PopStyleColor();
            if (c[i].dica && *c[i].dica && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s(horarios em UTC)", c[i].dica, noAr ? "\n\nNO AR AGORA pela grade " : "\n\n");
        }
        if (comBusca) ImGui::EndChild();
        if (comBusca && mudou) ImGui::CloseCurrentPopup();   // a lista fica numa janela-filha
        ImGui::EndCombo();
    }
    return mudou;
}
}

// HFDL: as bandas da pagina (canais da tabela de estacoes do dumphfdl).
// O centro e a taxa saem dos proprios canais: ate 250 kHz de largura (com
// 30% de folga) basta 0,250 Msps; a de 5,5 MHz precisa de 1,024 Msps.
struct BandaHfdl { const char* nome; std::vector<double> canais; };
static const std::vector<BandaHfdl> kHfdl = {
    {"2,9 - 3,0 MHz",   {2941, 2944, 2986, 2992, 2998, 3007, 3016}},
    {"3,4 - 3,5 MHz",   {3455, 3497}},
    {"4,6 - 4,7 MHz",   {4654, 4660, 4681, 4687}},
    {"5,4 - 5,7 MHz",   {5451, 5502, 5508, 5514, 5529, 5538, 5544, 5547, 5583, 5589, 5622, 5652, 5655, 5720}},
    {"6,5 - 6,7 MHz",   {6529, 6532, 6535, 6559, 6565, 6589, 6596, 6619, 6628, 6646, 6652, 6661, 6712}},
    {"8,8 - 9,0 MHz",   {8825, 8834, 8843, 8885, 8886, 8894, 8912, 8921, 8927, 8936, 8939, 8942, 8948, 8957, 8977}},
    {"10,0 - 10,1 MHz", {10027, 10030, 10060, 10063, 10066, 10081, 10084, 10087, 10093}},
    {"11,3 - 11,4 MHz", {11306, 11312, 11318, 11321, 11327, 11348, 11354, 11384, 11387}},
    {"13,2 - 13,4 MHz", {13264, 13270, 13276, 13303, 13312, 13315, 13321, 13324, 13342, 13351}},
    {"17,9 - 18,0 MHz", {17901, 17912, 17916, 17919, 17922, 17928, 17934, 17958, 17967, 17985}},
    {"21,9 - 22,0 MHz", {21928, 21931, 21934, 21937, 21949, 21955, 21982, 21990, 21997}},
};
static void medidasHfdl(const BandaHfdl& b, double& loKHz, double& centroKHz, unsigned& taxa)
{
    loKHz = *std::min_element(b.canais.begin(), b.canais.end());
    const double hi = *std::max_element(b.canais.begin(), b.canais.end());
    centroKHz = (loKHz + hi) / 2;
    taxa = (hi - loKHz) * 1000 * 1.3 <= 250000 ? 250000u : 1024000u;
}

struct CanalAprs { uint64_t hz; const char* nome; int baud; };
static const CanalAprs kAprs[] = {
    {145570000, "145,570 MHz - Brasil (FM, 1200 baud)", 1200},
    {144390000, "144,390 MHz - EUA (FM, 1200 baud)", 1200},
    {144800000, "144,800 MHz - Europa (FM, 1200 baud)", 1200},
    {10147600,  "10,1476 MHz - APRS de HF (USB, 300 baud)", 300},
};

// DRM: grade de ondas curtas (drmrx.org, temporada A26, atualizada em 03/10/2026).
// Horas em UTC (HHMM). As duas primeiras sao as que chegam bem aqui.
struct EmissoraDrm { int khz, ini, fim; const char* nome; const char* local; const char* lingua; bool fav; };
static const EmissoraDrm kDrm[] = {
    {17560, 1530, 1600, "TDF DRM", "Issoudun, França", "francês", true},
    {17575, 1500, 1600, "BBC World Service", "Woofferton, Inglaterra (para a Índia)", "inglês", true},
    {3205, 2000, 1800, "Korean Central Broadcasting", "Pyongyang, Coreia do Norte", "coreano", false},
    {3955, 500, 600, "BBC World Service", "Woofferton, Inglaterra (Europa)", "inglês", false},
    {5910, 1800, 1830, "Radio Romania International", "Saftica, Romênia (Europa)", "italiano", false},
    {5950, 2300, 2330, "TDF DRM", "Issoudun, França", "francês", false},
    {5960, 2130, 2200, "TDF DRM", "Issoudun, França", "francês", false},
    {5980, 0, 30, "TDF DRM", "Issoudun, França", "francês", false},
    {5980, 30, 100, "TDF DRM", "Issoudun, França", "francês", false},
    {5980, 100, 130, "TDF DRM", "Issoudun, França", "francês", false},
    {5980, 200, 230, "TDF DRM", "Issoudun, França", "francês", false},
    {5980, 230, 300, "TDF DRM", "Issoudun, França", "francês", false},
    {5980, 530, 600, "TDF DRM", "Issoudun, França", "francês", false},
    {6030, 2025, 1805, "CNR-1 Voz da China", "Pequim, China", "chinês", false},
    {6120, 530, 600, "TDF DRM", "Issoudun, França", "francês", false},
    {6120, 600, 630, "TDF DRM", "Issoudun, França", "francês", false},
    {6120, 1900, 1930, "TDF DRM", "Issoudun, França", "francês", false},
    {6120, 2000, 2030, "TDF DRM", "Issoudun, França", "francês", false},
    {6120, 2030, 2100, "TDF DRM", "Issoudun, França", "francês", false},
    {6140, 2000, 1800, "Korean Central Broadcasting", "Pyongyang, Coreia do Norte", "coreano", false},
    {6175, 1900, 1930, "TDF DRM", "Issoudun, França", "francês", false},
    {7205, 300, 330, "TDF DRM", "Issoudun, França", "francês", false},
    {7205, 700, 730, "TDF DRM", "Issoudun, França", "francês", false},
    {7425, 1651, 1758, "RNZ Pacific", "Rangitaiki, Nova Zelândia (Pacífico)", "inglês", false},
    {9430, 800, 900, "CNR-1 Voz da China", "China", "chinês", false},
    {9520, 530, 600, "TDF DRM", "Issoudun, França", "francês", false},
    {9570, 1800, 1900, "Radio Romania International", "Tiganesti, Romênia (Europa)", "alemão", false},
    {9610, 530, 600, "TDF DRM", "Issoudun, França", "francês", false},
    {9610, 830, 900, "TDF DRM", "Issoudun, França", "francês", false},
    {9655, 800, 1200, "CNR-1 Voz da China", "Urumqi, China", "chinês", false},
    {9655, 2200, 100, "CNR-1 Voz da China", "Urumqi, China", "chinês", false},
    {9720, 2300, 2330, "TDF DRM", "Issoudun, França", "francês", false},
    {9780, 1759, 1858, "RNZ Pacific", "Rangitaiki, Nova Zelândia (Pacífico)", "inglês", false},
    {9865, 200, 230, "TDF DRM", "Issoudun, França", "francês", false},
    {9865, 400, 430, "TDF DRM", "Issoudun, França", "francês", false},
    {11615, 2000, 2100, "Music 4 Joy", "Nauen, Alemanha (África Ocidental)", "música", false},
    {11640, 1130, 1200, "TDF DRM", "Issoudun, França", "francês", false},
    {11650, 700, 730, "TDF DRM", "Issoudun, França", "francês", false},
    {11650, 2330, 2400, "TDF DRM", "Issoudun, França", "francês", false},
    {11660, 1200, 1230, "TDF DRM", "Issoudun, França", "francês", false},
    {11690, 1859, 1958, "RNZ Pacific", "Rangitaiki, Nova Zelândia (Pacífico)", "inglês", false},
    {11695, 100, 900, "CNR-1 Voz da China", "Dongfang, China", "chinês", false},
    {11710, 1830, 1930, "Music 4 Joy", "Nauen, Alemanha (Leste Europeu)", "música", false},
    {11790, 830, 900, "TDF DRM", "Issoudun, França", "francês", false},
    {11820, 1130, 1200, "TDF DRM", "Issoudun, França", "francês", false},
    {11870, 400, 430, "TDF DRM", "Issoudun, França", "francês", false},
    {11875, 530, 600, "TDF DRM", "Issoudun, França", "francês", false},
    {11900, 1530, 1600, "TDF DRM", "Issoudun, França", "francês", false},
    {12070, 1300, 1330, "TDF DRM", "Issoudun, França", "francês", false},
    {13600, 1000, 1030, "TDF DRM", "Issoudun, França", "francês", false},
    {13600, 1700, 1730, "TDF DRM", "Issoudun, França", "francês", false},
    {13690, 1600, 1700, "Radio Romania International", "Tiganesti, Romênia (Europa)", "francês", false},
    {13710, 2330, 2400, "TDF DRM", "Issoudun, França", "francês", false},
    {13730, 1800, 1900, "Music 4 Joy", "Nauen, Alemanha (África Oriental)", "música", false},
    {13740, 2200, 2300, "TDF DRM", "Issoudun, França", "francês", false},
    {13750, 1700, 1800, "Radio Romania International", "Tiganesti, Romênia (Europa)", "inglês", false},
    {13790, 0, 1000, "CNR-1 Voz da China", "Qiqihar, China", "chinês", false},
    {13810, 400, 1100, "CNR-1 Voz da China", "Kunming, China", "chinês", false},
    {13825, 100, 900, "CNR-1 Voz da China", "Pequim, China", "chinês", false},
    {13835, 1000, 1200, "CNR-1 Voz da China", "Qiqihar, China", "chinês", false},
    {13840, 1959, 2058, "RNZ Pacific", "Rangitaiki, Nova Zelândia (Pacífico)", "inglês", false},
    {15145, 430, 500, "TDF DRM", "Issoudun, França", "francês", false},
    {15180, 100, 400, "CNR-1 Voz da China", "Kunming, China", "chinês", false},
    {15215, 630, 700, "TDF DRM", "Issoudun, França", "francês", false},
    {15290, 800, 1000, "CNR-1 Voz da China", "China", "chinês", false},
    {15460, 900, 930, "TDF DRM", "Issoudun, França", "francês", false},
    {15495, 800, 830, "TDF DRM", "Issoudun, França", "francês", false},
    {15505, 900, 930, "TDF DRM", "Issoudun, França", "francês", false},
    {15610, 700, 800, "CNR-1 Voz da China", "Pequim, China", "chinês", false},
    {15725, 1400, 1500, "TDF DRM", "Issoudun, França", "francês", false},
    {15730, 1300, 1330, "TDF DRM", "Issoudun, França", "francês", false},
    {15750, 1700, 1730, "TDF DRM", "Issoudun, França", "francês", false},
    {15750, 1930, 2000, "TDF DRM", "Issoudun, França", "francês", false},
    {15785, 0, 2400, "funklust", "Erlangen, Alemanha (Europa)", "alemão", false},
    {17580, 2100, 2200, "Radio Romania International", "Tiganesti, Romênia (América do Sul)", "espanhol", false},
    {17585, 1400, 1430, "TDF DRM", "Issoudun, França", "francês", false},
    {17670, 1300, 1400, "Music 4 Joy", "Nauen, Alemanha (China, Japão)", "música", false},
    {17680, 530, 600, "Radio Romania International", "Tiganesti, Romênia (Índia)", "inglês", false},
    {17685, 1600, 1630, "TDF DRM", "Issoudun, França", "francês", false},
    {17710, 930, 1000, "TDF DRM", "Issoudun, França", "francês", false},
    {17710, 1100, 1130, "TDF DRM", "Issoudun, França", "francês", false},
    {17710, 1230, 1300, "TDF DRM", "Issoudun, França", "francês", false},
    {17720, 800, 1200, "CNR-1 Voz da China", "Pequim, China", "chinês", false},
    {17760, 1230, 1300, "Radio Romania International", "Tiganesti, Romênia (China)", "chinês", false},
    {17770, 100, 900, "CNR-1 Voz da China", "Dongfang, China", "chinês", false},
    {17790, 300, 400, "Radio Romania International", "Tiganesti, Romênia (Índia)", "inglês", false},
    {17825, 1130, 1200, "TDF DRM", "Issoudun, França", "francês", false},
    {17830, 100, 800, "CNR-1 Voz da China", "Urumqi, China", "chinês", false},
    {17855, 1300, 1330, "TDF DRM", "Issoudun, França", "francês", false},
};

// No ar agora? (os horarios podem virar a meia-noite: 2200-0100)
static bool drmNoAr(const EmissoraDrm& e)
{
    SYSTEMTIME st; GetSystemTime(&st);
    const int m = st.wHour * 60 + st.wMinute;
    const int a = (e.ini / 100) * 60 + e.ini % 100, b = (e.fim / 100) * 60 + e.fim % 100;
    return a < b ? (m >= a && m < b) : (m >= a || m < b);
}

static std::string rotuloDrm(const EmissoraDrm& e)
{
    char s[220];
    std::snprintf(s, sizeof s, "%s%5d kHz   %02d:%02d-%02d:%02d UTC   %s - %s (%s)",
                  drmNoAr(e) ? "\xE2\x97\x8F NO AR  " : "",
                  e.khz, e.ini / 100, e.ini % 100, e.fim / 100, e.fim % 100, e.nome, e.local, e.lingua);
    return s;
}

// Leva o radio para onde o decodificador precisa estar
void Ui::sintonizarDecoder(int t)
{
    if (t == Decoders::HFDL) {
        const BandaHfdl& b = kHfdl[std::clamp(decHfdlBanda_, 0, (int)kHfdl.size() - 1)];
        double lo, centro; unsigned taxa;
        medidasHfdl(b, lo, centro, taxa);
        // 1) a taxa primeiro; 2) o centro da banda; 3) o VFO no canal mais baixo, em USB
        auto& c = Config::instance();
        if (c.sampleRate() != taxa) { c.set("sample_rate", (long long)taxa); r_.aplicarConfig(); decMudouTaxa_ = true; }
        r_.centralizar((uint64_t)std::llround(centro * 1000));
        mudarModo("USB");
        sintonizar((uint64_t)std::llround(lo * 1000), false);
        decMudouFreq_ = true;
        // o dumphfdl recebe o centro e a taxa REAIS do aparelho
        decAj_.hfdlCanais = b.canais;
        decAj_.hfdlCentroHz = (double)r_.centro();
        decAj_.hfdlTaxa = r_.taxa();
    } else if (t == Decoders::ACARS || t == Decoders::VDL2) {
        // ACARS: 131,550 e 131,825 juntos (centro no meio, 1,024 Msps ou mais)
        // VDL2 : 136,975 com o dongle em 1,05 Msps (o dumpvdl2 so aceita 105 kHz x N)
        const bool ac = t == Decoders::ACARS;
        auto& c = Config::instance();
        const unsigned taxa = ac ? (c.sampleRate() >= 900000 ? c.sampleRate() : 1024000u) : Vdl2::kTaxa;
        if (c.sampleRate() != taxa) { c.set("sample_rate", (long long)taxa); r_.aplicarConfig(); decMudouTaxa_ = true; }
        const std::vector<double> canais = ac ? std::vector<double>{131550000, 131825000} : std::vector<double>{136975000};
        const double centro = ac ? 131687500 : 136875000;
        r_.centralizar((uint64_t)centro);
        mudarModo("AM");
        sintonizar((uint64_t)canais[0], false);
        decMudouFreq_ = true;
        if (ac) { decAj_.acarsCanais = canais; decAj_.acarsCentroHz = (double)r_.centro(); decAj_.acarsTaxa = r_.taxa(); }
        else    { decAj_.vdl2Canais = canais;  decAj_.vdl2CentroHz = (double)r_.centro();  decAj_.vdl2Taxa = r_.taxa(); }
    } else if (t == Decoders::AIS) {
        // canais 87B (161,975) e 88B (162,025): o VFO fica no meio dos dois
        mudarModo("NFM");
        sintonizar(162000000, false);
        decMudouFreq_ = true;
    } else if (t == Decoders::APRS) {
        const CanalAprs& a = kAprs[std::clamp(decAprsCanal_, 0, 3)];
        decAj_.aprsBaud = a.baud;
        if (a.baud == 300) { mudarModo("USB"); r_.setBanda(3000); }
        else { mudarModo("NFM"); r_.setBanda(12500); }
        sintonizar(a.hz, false);
        decMudouFreq_ = true;
    }
}

void Ui::escolherDecoder(int t, bool sintonizarModo)
{
    decTipo_ = t;
    auto& d = r_.decoders();
    if (t == Decoders::NENHUM) { d.parar(); decRodando_ = false; devolverModo(); return; }
    const bool externoComSintonia = t == Decoders::HFDL || t == Decoders::AIS || t == Decoders::APRS ||
                                    t == Decoders::ACARS || t == Decoders::VDL2;
    const bool centroFixo = t == Decoders::HFDL || t == Decoders::ACARS || t == Decoders::VDL2;
    if ((sintonizarModo || externoComSintonia) && !decModoGuardado_) {   // para devolver o radio como estava
        decModoAntes_ = r_.modo(); decBwAntes_ = r_.banda(); decModoGuardado_ = true;
        decFreqAntes_ = r_.vfo(); decTaxaAntes_ = Config::instance().sampleRate();
        decMudouFreq_ = decMudouTaxa_ = false;
    }
    // O HFDL sempre re-sintoniza (o dumphfdl precisa do centro combinado);
    // AIS e APRS so quando escolhidos agora (Reiniciar nao tira de onde voce pos)
    if (centroFixo || (externoComSintonia && sintonizarModo)) sintonizarDecoder(t);
    if (t == Decoders::HFDL) decAj_.hfdlCentroHz = (double)r_.centro(), decAj_.hfdlTaxa = r_.taxa();
    d.iniciar((Decoders::Tipo)t, decAj_);
    decRodando_ = true;
    if (!sintonizarModo || externoComSintonia) return;
    const std::string m = r_.modo();
    if (t == Decoders::DRM) {
        // o dream recebe o IQ (canal inteiro); AM 10 kHz so marca o canal na
        // tela. O audio decodificado toca no lugar do audio do radio.
        if (m != "AM") mudarModo("AM");
        r_.setBanda(10000);
    } else if (t == Decoders::SSTV) {
        // SSTV de radioamador: USB (20/15/10 m), LSB (40/80 m) ou FM (ISS, VHF)
        if (m != "USB" && m != "LSB" && m != "NFM" && m != "FM") mudarModo("USB");
        if ((r_.modo() == "USB" || r_.modo() == "LSB") && r_.banda() < 3000) r_.setBanda(3000);   // o branco e 2300 Hz
        sstvAumentar_ = true;
    } else if (t == Decoders::WEFAX) {
        // fax: USB, 1500-2300 Hz; a janela cresce para caber a imagem
        if (m != "USB") mudarModo("USB");
        if (r_.banda() < 3000) r_.setBanda(3000);
        sstvAumentar_ = true;
    } else if (t == Decoders::DMR) {
        // o dsd-fme quer o FM cru de um canal de 12,5 kHz
        if (m != "NFM" && m != "FM") mudarModo("NFM");
        if (r_.banda() < 12500) r_.setBanda(12500);
    } else if (t == Decoders::TETRA) {
        // igual a pagina: NFM com 25 kHz (so para a marca e o som de fundo;
        // o TETRA usa o IQ, nao o audio)
        if (m != "NFM") mudarModo("NFM");
        r_.setBanda(25000);
    } else if (t == Decoders::CW) {
        if (m != "CW" && m != "USB" && m != "LSB") mudarModo("CW");
    } else if (t != Decoders::ANALISE) {
        // Os nucleos FSK esperam USB (mark = tom alto), igual a pagina.
        // Para RTTY de radioamador em LSB existe o "Inverter".
        if (m != "USB") mudarModo("USB");
        if (t == Decoders::ALE) r_.setBanda(3000);   // o ALE vai ate 2750 Hz
    }
}

// Tabela de avioes com os links do FlightAware (ACARS e VDL2; igual a do HFDL)
void Ui::tabelaAvioes(const char* id, const std::vector<AeronaveHfdl>& avs)
{
    if (avs.empty() || !ImGui::BeginTable(id, 6, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                                          ImVec2(0, 130 * s_)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    for (const char* c : {"Voo", "Prefixo", "ICAO", "Canal", "Msgs", "Visto"}) ImGui::TableSetupColumn(c);
    ImGui::TableHeadersRow();
    const double ta = agoraS();
    auto abrir = [](const std::string& url) { ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    int n = 0;
    for (const auto& a : avs) {
        ImGui::PushID(n++);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (a.voo.empty()) ImGui::TextUnformatted("-");
        else {
            if (ImGui::TextLink(a.voo.c_str())) abrir(a.urlVoo());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir o voo no FlightAware\n%s", a.urlVoo().c_str());
        }
        ImGui::TableSetColumnIndex(1);
        if (a.prefixo.empty()) ImGui::TextUnformatted("-");
        else {
            if (ImGui::TextLink(a.prefixo.c_str())) abrir(a.urlPrefixo());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir o avião no FlightAware\n%s", a.urlPrefixo().c_str());
        }
        ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(a.hex.empty() ? "-" : a.hex.c_str());
        ImGui::TableSetColumnIndex(3);
        if (a.freqKHz > 0) ImGui::Text("%.3f", a.freqKHz / 1000.0); else ImGui::TextUnformatted("-");
        ImGui::TableSetColumnIndex(4); ImGui::Text("%d", a.msgs);
        ImGui::TableSetColumnIndex(5);
        const double s = ta - a.visto;
        if (s < 120) ImGui::Text("%.0f s", s); else ImGui::Text("%.0f min", s / 60);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// Texto novo na janela: quebra as linhas compridas (o SITOR-B e o RTTY
// mandam texto corrido) e guarda no maximo ~60 kB.
void Ui::anexarDecTexto(const std::string& novo)
{
    const int kColunas = 100;
    for (char c : novo) {
        if (c == '\r') continue;
        // caractere de controle (ou um \0 perdido) cortaria o texto na caixa:
        // o que vem depois dele simplesmente nao aparecia
        if ((unsigned char)c < 0x20 && c != '\n' && c != '\t') continue;
        if (c == '\t') c = ' ';
        if (c == '\n') { decTexto_ += '\n'; decCol_ = 0; ++decLinhas_; continue; }
        if (decCol_ >= kColunas) { decTexto_ += '\n'; decCol_ = 0; ++decLinhas_; }
        decTexto_ += c;
        ++decCol_;
    }
    if (decTexto_.size() > 60000) {
        size_t corte = decTexto_.find('\n', decTexto_.size() - 50000);
        decTexto_.erase(0, corte == std::string::npos ? decTexto_.size() - 50000 : corte + 1);
    }
    decLinhas_ = 0;
    for (char c : decTexto_) if (c == '\n') ++decLinhas_;
}

// Fechou a janela (ou escolheu "Nenhum"): o radio volta ao modo e a largura
// de antes do decodificador (o DMR passa para NFM 12,5 kHz, o RTTY para USB...).
void Ui::devolverModo()
{
    if (!decModoGuardado_) return;
    decModoGuardado_ = false;
    if (decMudouTaxa_ && decTaxaAntes_) {
        Config::instance().set("sample_rate", (long long)decTaxaAntes_);
        r_.aplicarConfig();
    }
    if (!decModoAntes_.empty() && r_.modo() != decModoAntes_) mudarModo(decModoAntes_);
    if (decBwAntes_ > 0) r_.setBanda(decBwAntes_);
    if (decMudouFreq_ && decFreqAntes_) sintonizar(decFreqAntes_, false);
    decMudouFreq_ = decMudouTaxa_ = false;
}

// ---------------------------------------------------------------------------
//  SSTV: imagem que esta chegando (textura), espectro do audio com as marcas
//  de 1200/1500/1900/2300 Hz, historico das recebidas e PNG salvo sozinho na
//  pasta SSTV ao lado do RXSDR.exe.
// ---------------------------------------------------------------------------
struct FreqSstv { const char* nome; uint64_t hz; const char* modo; };
static const FreqSstv kFreqSstv[] = {
    {"14.230 MHz  USB  (20 m - a mais movimentada)", 14230000, "USB"},
    {"14.233 MHz  USB  (20 m)", 14233000, "USB"},
    {"21.340 MHz  USB  (15 m)", 21340000, "USB"},
    {"28.680 MHz  USB  (10 m)", 28680000, "USB"},
    {"7.171 MHz  LSB  (40 m - Américas)", 7171000, "LSB"},
    {"7.165 MHz  LSB  (40 m - Europa)", 7165000, "LSB"},
    {"3.845 MHz  LSB  (80 m - Américas)", 3845000, "LSB"},
    {"3.730 MHz  LSB  (80 m - Europa)", 3730000, "LSB"},
    {"145.800 MHz  NFM  (ISS - eventos ARISS)", 145800000, "NFM"},
};

// textura ARGB do tamanho da imagem (D3DPOOL_MANAGED sobrevive ao reset do D3D)
static bool copiarArgb(IDirect3DTexture9* t, const uint32_t* px, int w, int h)
{
    D3DLOCKED_RECT lr;
    if (!t || t->LockRect(0, &lr, nullptr, 0) != D3D_OK) return false;
    for (int y = 0; y < h; ++y)
        std::memcpy((uint8_t*)lr.pBits + size_t(y) * lr.Pitch, px + size_t(y) * w, size_t(w) * 4);
    t->UnlockRect(0);
    return true;
}

static IDirect3DTexture9* texturaArgb(IDirect3DDevice9* dev, const uint32_t* px, int w, int h)
{
    IDirect3DTexture9* t = nullptr;
    if (w <= 0 || h <= 0 || dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr) != D3D_OK)
        return nullptr;
    if (px) copiarArgb(t, px, w, h);
    return t;
}

// Chamado a cada quadro com a janela aberta: imagem nova -> textura;
// imagens terminadas (ja salvas pelo Decoders) -> historico.
void Ui::atualizarSstv()
{
    auto& d = r_.decoders();
    auto& sv = d.sstv();
    d.sstvVfoHz = r_.vfo();
    d.sstvSalvar = sstvSalvar_;
    Decoders::ImagemSstv im;
    while (d.pegarImagemSstv(im)) {
        SstvFeita f;
        f.w = im.w; f.h = im.h; f.rotulo = im.rotulo; f.arquivo = im.arquivo;
        f.tex = texturaArgb(dev_, im.argb.data(), im.w, im.h);
        sstvHist_.insert(sstvHist_.begin(), std::move(f));
        while (sstvHist_.size() > 12) {
            if (sstvHist_.back().tex) sstvHist_.back().tex->Release();
            sstvHist_.pop_back();
        }
    }
    int w = 0, h = 0;
    if (decTipo_ != Decoders::SSTV) return;
    if (!sv.imagem(sstvBuf_, w, h, sstvVer_)) return;
    sstvW_ = w; sstvH_ = h;
    if (w <= 0 || h <= 0) return;
    if (!sstvTex_ || sstvTexW_ != w || sstvTexH_ != h) {
        if (sstvTex_) sstvTex_->Release();
        sstvTex_ = texturaArgb(dev_, nullptr, w, h);
        sstvTexW_ = w; sstvTexH_ = h;
    }
    copiarArgb(sstvTex_, sstvBuf_.data(), w, h);
}

void Ui::painelSstv()
{
    auto& sv = r_.decoders().sstv();
    const SstvCore::Status stt = sv.status();

    // --- linha 1: frequencia, VIS automatico, inclinacao, salvar ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Frequência"); ImGui::SameLine();
    ImGui::SetNextItemWidth(310 * s_);
    const int nF = (int)(sizeof kFreqSstv / sizeof kFreqSstv[0]);
    if (ImGui::BeginCombo("##sstvfreq", sstvFreqSel_ >= 0 && sstvFreqSel_ < nF ? kFreqSstv[sstvFreqSel_].nome
                                                                             : "- escolha para sintonizar -",
                          ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < nF; ++i)
            if (ImGui::Selectable(kFreqSstv[i].nome, sstvFreqSel_ == i)) {
                sstvFreqSel_ = i;
                const FreqSstv& f = kFreqSstv[i];
                mudarModo(f.modo);
                r_.setBanda(std::string(f.modo) == "NFM" ? 20000 : 3000);   // ISS: Doppler de ate 3,5 kHz
                sintonizar(f.hz, false);
                decMudouFreq_ = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    bool vis = sv.aceitarVis.load();
    if (ImGui::Checkbox("VIS automático", &vis)) sv.aceitarVis = vis;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reconhece o modo pelo cabeçalho VIS e começa a imagem sozinho");
    ImGui::SameLine();
    bool incl = sv.autoInclinacao.load();
    if (ImGui::Checkbox("Corrigir inclinação", &incl)) sv.autoInclinacao = incl;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Mede o relógio da placa de som de quem transmite pelos pulsos de 1200 Hz\n"
                          "e endireita a imagem (sem isso ela sai torta)");
    ImGui::SameLine();
    ImGui::Checkbox("Salvar PNG", &sstvSalvar_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cada imagem recebida vai para a pasta SSTV ao lado do RXSDR.exe");

    // --- linha 2: comecar sem VIS, terminar, limpar, pasta ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Modo"); ImGui::SameLine();
    ImGui::SetNextItemWidth(130 * s_);
    sstvModoManual_ = std::clamp(sstvModoManual_, 0, SstvCore::nModos() - 1);
    if (ImGui::BeginCombo("##sstvmodo", SstvCore::nomeModo(sstvModoManual_), ImGuiComboFlags_HeightLarge)) {
        for (int m = 0; m < SstvCore::nModos(); ++m) {
            char t[96];
            std::snprintf(t, sizeof t, "%-10s %dx%d  %.0f s", SstvCore::nomeModo(m), SstvCore::larguraModo(m),
                          SstvCore::alturaModo(m), SstvCore::duracaoModo(m));
            if (ImGui::Selectable(t, m == sstvModoManual_)) sstvModoManual_ = m;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Começar agora")) { if (!decRodando_) escolherDecoder(Decoders::SSTV, false); sv.comecarAgora(sstvModoManual_); }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pegou a transmissão no meio (sem o VIS)? Escolha o modo e clique:\n"
                          "a imagem começa já, alinhada pelos próximos pulsos de sincronismo.");
    ImGui::SameLine();
    if (ImGui::Button("Terminar")) sv.parar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Encerra a imagem atual como está (e salva, se tiver pelo menos 1/4)");
    ImGui::SameLine();
    if (ImGui::Button("Limpar imagem")) sv.limpar();
    ImGui::SameLine();
    if (ImGui::Button("Abrir pasta SSTV")) {
        const std::string pasta = pastaDoExe() + "\\SSTV";
        CreateDirectoryA(pasta.c_str(), nullptr);
        ShellExecuteA(nullptr, "open", pasta.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // --- area: imagem a esquerda, espectro + historico a direita ---
    const float altArea = std::max(160 * s_, ImGui::GetContentRegionAvail().y - 120 * s_);
    const float wDir = 250 * s_;
    if (ImGui::BeginChild("##sstvimg", ImVec2(ImGui::GetContentRegionAvail().x - wDir - 8 * s_, altArea),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 av = ImGui::GetContentRegionAvail();
        const float altBarra = 16 * s_;
        const float aw = av.x, ah = av.y - altBarra - 4 * s_;
        const int iw = sstvW_ > 0 ? sstvW_ : 320, ih = sstvH_ > 0 ? sstvH_ : 256;
        const float esc = std::max(0.1f, std::min(aw / iw, ah / ih));
        const ImVec2 tam(iw * esc, ih * esc);
        const ImVec2 a(p0.x + (aw - tam.x) * 0.5f, p0.y), b(a.x + tam.x, a.y + tam.y);
        dl->AddRectFilled(a, b, IM_COL32(0x08, 0x08, 0x08, 255));
        if (sstvTex_ && sstvW_ > 0) dl->AddImage(TEXID(sstvTex_), a, b);
        else {
            const char* msg = decRodando_ ? "esperando uma imagem..." : "decodificador parado";
            const ImVec2 ts = ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), C_DIM, msg);
        }
        dl->AddRect(a, b, C_BORDER_L);
        // linha que esta chegando (como no MMSSTV)
        if (stt.estado == SstvCore::RECEBENDO && stt.linhas > 0 && stt.linha < stt.linhas) {
            const float y = a.y + tam.y * stt.linha / stt.linhas;
            dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(0xff, 0x40, 0x40, 160), 1.5f * s_);
        }
        // barra de progresso
        const float frac = stt.linhas > 0 ? float(stt.linha) / stt.linhas : 0.f;
        const ImVec2 pa(a.x, b.y + 4 * s_), pb(b.x, b.y + 4 * s_ + altBarra);
        dl->AddRectFilled(pa, pb, C_METER);
        dl->AddRectFilled(pa, ImVec2(pa.x + (pb.x - pa.x) * frac, pb.y),
                          stt.estado == SstvCore::RECEBENDO ? IM_COL32(0x00, 0xb0, 0x50, 255) : IM_COL32(0x30, 0x60, 0x40, 255));
        char pr[160];
        if (stt.modo >= 0)
            std::snprintf(pr, sizeof pr, "%s  %dx%d  -  linha %d de %d%s", SstvCore::nomeModo(stt.modo), iw, ih,
                          stt.linha, stt.linhas, stt.estado == SstvCore::RECEBENDO ? "" : "  (pronta)");
        else std::snprintf(pr, sizeof pr, "sem imagem");
        dl->AddText(ImVec2(pa.x + 6 * s_, pa.y + (altBarra - ImGui::GetTextLineHeight()) * 0.5f), C_TEXT, pr);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##sstvdir", ImVec2(wDir, altArea), ImGuiChildFlags_None)) {
        // espectro do audio, 900 a 2500 Hz, com as marcas do SSTV
        std::vector<float> db;
        double hzBin = 1;
        sv.espectro(db, hzBin);
        if (sstvEsp_.size() != db.size()) sstvEsp_ = db;
        for (size_t i = 0; i < db.size(); ++i) sstvEsp_[i] += (db[i] - sstvEsp_[i]) * 0.35f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float W = ImGui::GetContentRegionAvail().x, H = 90 * s_;
        const ImVec2 q(p0.x + W, p0.y + H);
        dl->AddRectFilled(p0, q, IM_COL32(0x02, 0x06, 0x03, 255));
        const double f0 = 900, f1 = 2500;
        auto xDe = [&](double f) { return p0.x + float((f - f0) / (f1 - f0)) * W; };
        const double dv = stt.estado == SstvCore::RECEBENDO ? stt.desvioHz : 0.0;
        struct Marca { double f; ImU32 c; const char* t; };
        const Marca marcas[] = {{1200, IM_COL32(0xff, 0x50, 0x50, 200), "1200"}, {1500, IM_COL32(0x90, 0x90, 0x90, 200), "1500"},
                                {1900, IM_COL32(0xff, 0xd0, 0x30, 200), "1900"}, {2300, IM_COL32(0xf0, 0xf0, 0xf0, 200), "2300"}};
        ImGui::PushFont(f_.pequena);
        for (const Marca& m : marcas) {
            const float x = xDe(m.f + dv);
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, q.y), m.c, 1.f);
            dl->AddText(ImVec2(x + 2 * s_, p0.y + 1), m.c, m.t);
        }
        ImGui::PopFont();
        if (!sstvEsp_.empty()) {
            float mx = -1e9f, mn = 1e9f;
            for (size_t i = 0; i < sstvEsp_.size(); ++i) {
                const double f = i * hzBin;
                if (f < 300 || f > 3000) continue;
                mx = std::max(mx, sstvEsp_[i]); mn = std::min(mn, sstvEsp_[i]);
            }
            const float topo = mx + 3, piso = std::max(mn, mx - 45);
            ImVec2 ant(0, 0);
            bool tem = false;
            for (size_t i = 0; i < sstvEsp_.size(); ++i) {
                const double f = i * hzBin;
                if (f < f0 || f > f1) continue;
                const float v = std::clamp((sstvEsp_[i] - piso) / std::max(1.f, topo - piso), 0.f, 1.f);
                const ImVec2 pt(xDe(f), q.y - 2 - v * (H - 14 * s_));
                if (tem) dl->AddLine(ant, pt, IM_COL32(0xff, 0xc8, 0x20, 255), 1.3f * s_);
                ant = pt; tem = true;
            }
        }
        dl->AddRect(p0, q, C_BORDER_L);
        ImGui::Dummy(ImVec2(W, H));
        // situacao
        const char* est = stt.estado == SstvCore::RECEBENDO ? "RECEBENDO" : stt.estado == SstvCore::PRONTA ? "PRONTA" : "ESPERANDO VIS";
        ImGui::TextColored(stt.estado == SstvCore::RECEBENDO ? ImVec4(0.2f, 1.f, 0.4f, 1) : ImVec4(1.f, 0.67f, 0.f, 1), "%s", est);
        if (stt.modo >= 0) {
            ImGui::SameLine();
            ImGui::Text("%s%s", SstvCore::nomeModo(stt.modo), stt.porVis ? " (VIS)" : " (manual)");
            ImGui::Text("Sintonia: %+.0f Hz   Inclinação: %+.0f ppm", stt.desvioHz, stt.inclinacaoPpm);
            ImGui::Text("Sincronismos: %d   faltaram: %d", stt.sincronismos, stt.faltas);
        } else {
            ImGui::TextDisabled("Sintonize e espere o cabeçalho VIS");
        }
        ImGui::Separator();
        ImGui::Text("Recebidas (%d)", (int)sstvHist_.size());
        const float tw = (W - 8 * s_) / 2;
        for (size_t i = 0; i < sstvHist_.size(); ++i) {
            const SstvFeita& f = sstvHist_[i];
            if (i % 2) ImGui::SameLine();
            ImGui::PushID((int)i);
            const ImVec2 ts(tw, tw * f.h / std::max(1, f.w));
            if (f.tex) {
                if (ImGui::ImageButton("##mini", TEXID(f.tex), ts, ImVec2(0, 0), ImVec2(1, 1)) && !f.arquivo.empty())
                    ShellExecuteA(nullptr, "open", f.arquivo.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else ImGui::Dummy(ts);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s", f.rotulo.c_str(), f.arquivo.empty() ? "\n(não salva)" : "\nclique para abrir o PNG");
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("USB em 20/15/10 m, LSB em 40/80 m. A imagem começa sozinha no VIS; a linha vermelha mostra onde ela está.");
}

// ---------------------------------------------------------------------------
//  WEFAX: fax meteorologico. A imagem (meia resolucao) vai para uma textura
//  que cresce em blocos de 256 linhas; o PNG inteiro e salvo pelo Decoders.
// ---------------------------------------------------------------------------
void Ui::atualizarWefax()
{
    auto& d = r_.decoders();
    d.wefaxSalvar = wfxSalvar_;
    Decoders::ImagemWefax im;
    while (d.pegarImagemWefax(im)) {
        // miniatura de 240 de largura para o historico
        SstvFeita f;
        f.w = 240; f.h = std::max(1, im.h * 240 / std::max(1, im.w));
        std::vector<uint32_t> mini(size_t(f.w) * size_t(f.h));
        for (int y = 0; y < f.h; ++y)
            for (int x = 0; x < f.w; ++x) {
                const uint32_t v = im.cinza[size_t(y * im.h / f.h) * size_t(im.w) + size_t(x * im.w / f.w)];
                mini[size_t(y) * size_t(f.w) + size_t(x)] = 0xFF000000u | (v << 16) | (v << 8) | v;
            }
        f.tex = texturaArgb(dev_, mini.data(), f.w, f.h);
        f.rotulo = im.rotulo; f.arquivo = im.arquivo;
        wfxHist_.insert(wfxHist_.begin(), std::move(f));
        while (wfxHist_.size() > 10) {
            if (wfxHist_.back().tex) wfxHist_.back().tex->Release();
            wfxHist_.pop_back();
        }
    }
    if (decTipo_ != Decoders::WEFAX) return;
    int w = 0, h = 0;
    if (!d.wefax().imagemMeia(wfxBuf_, w, h, wfxVer_)) return;
    // placas antigas: textura de no maximo MaxTextureHeight linhas (pula linhas se precisar)
    D3DCAPS9 caps{};
    int maxH = 2048;
    if (dev_->GetDeviceCaps(&caps) == D3D_OK && caps.MaxTextureHeight > 0) maxH = (int)std::min<DWORD>(caps.MaxTextureHeight, 8192);
    if (h > maxH) {
        const int passo = (h + maxH - 1) / maxH, nh = h / passo;
        for (int y = 0; y < nh; ++y)
            std::memmove(&wfxBuf_[size_t(y) * size_t(w)], &wfxBuf_[size_t(y * passo) * size_t(w)], size_t(w) * 4);
        h = nh;
    }
    wfxW_ = w; wfxH_ = h;
    if (w <= 0 || h <= 0) return;
    const int hTex = std::min(maxH, (h + 255) / 256 * 256);
    if (!wfxTex_ || wfxTexW_ != w || wfxTexH_ != hTex) {
        if (wfxTex_) wfxTex_->Release();
        wfxTex_ = texturaArgb(dev_, nullptr, w, hTex);
        wfxTexW_ = w; wfxTexH_ = hTex;
    }
    if (!wfxTex_) return;
    D3DLOCKED_RECT lr;
    RECT rc{0, 0, w, h};
    if (wfxTex_->LockRect(0, &lr, &rc, 0) == D3D_OK) {
        for (int y = 0; y < h; ++y)
            std::memcpy((uint8_t*)lr.pBits + size_t(y) * lr.Pitch, &wfxBuf_[size_t(y) * size_t(w)], size_t(w) * 4);
        wfxTex_->UnlockRect(0);
    }
}

void Ui::painelWefax()
{
    auto& wf = r_.decoders().wefax();
    const WefaxCore::Status stt = wf.status();

    // --- linha 1: canal (em ordem de horario), LPM, IOC ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Canal"); ImGui::SameLine();
    ImGui::SetNextItemWidth(400 * s_);
    if (comboCanal("##wfxcanal", kCanaisWefax, decCanalWefax_)) {
        const CanalDec& c = kCanaisWefax[decCanalWefax_];
        wfxLpm_ = (int)c.baud; wfxIoc_ = (int)c.shift;
        wf.configurar(wfxLpm_, wfxIoc_);
        if (r_.modo() != "USB") mudarModo("USB");
        if (r_.banda() < 3000) r_.setBanda(3000);
        sintonizar(c.hz, false);                 // WEFAX: a lista ja da a frequencia do mostrador (USB)
        decMudouFreq_ = true;
        escolherDecoder(Decoders::WEFAX, false); // volta a esperar o tom de inicio
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("LPM"); ImGui::SameLine();
    ImGui::SetNextItemWidth(70 * s_);
    static const int kLpm[] = {60, 90, 100, 120, 180, 240};
    char tl[16]; std::snprintf(tl, sizeof tl, "%d", wfxLpm_);
    if (ImGui::BeginCombo("##wfxlpm", tl)) {
        for (int v : kLpm) { char t[16]; std::snprintf(t, sizeof t, "%d", v);
            if (ImGui::Selectable(t, v == wfxLpm_)) { wfxLpm_ = v; wf.configurar(wfxLpm_, wfxIoc_); } }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Linhas por minuto (vale para o \"Começar agora\"; com o tom de início a fase mede sozinha)");
    ImGui::SameLine();
    ImGui::TextUnformatted("IOC"); ImGui::SameLine();
    ImGui::SetNextItemWidth(70 * s_);
    char ti[16]; std::snprintf(ti, sizeof ti, "%d", wfxIoc_);
    if (ImGui::BeginCombo("##wfxioc", ti)) {
        for (int v : {576, 288}) { char t[16]; std::snprintf(t, sizeof t, "%d", v);
            if (ImGui::Selectable(t, v == wfxIoc_)) { wfxIoc_ = v; wf.configurar(wfxLpm_, wfxIoc_); } }
        ImGui::EndCombo();
    }

    // --- linha 2: opcoes ---
    bool aut = wf.automatico.load();
    if (ImGui::Checkbox("Automático", &aut)) wf.automatico = aut;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("O tom de início (300 Hz) começa a imagem e o de fim (450 Hz) termina e salva");
    ImGui::SameLine();
    bool inv = wf.inverter.load();
    if (ImGui::Checkbox("Inverter (negativo)", &inv)) wf.inverter = inv;
    ImGui::SameLine();
    bool ai = wf.autoInclinacao.load();
    if (ImGui::Checkbox("Endireitar sozinho", &ai)) wf.autoInclinacao = ai;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ao terminar, mede a inclinação pelas linhas verticais do mapa e endireita (só se a melhora for clara)");
    ImGui::SameLine();
    ImGui::Checkbox("Salvar PNG", &wfxSalvar_);
    ImGui::SameLine();
    ImGui::TextDisabled("verde na lista = no ar agora (UTC)");

    // --- linha 3: acoes ---
    if (ImGui::Button("Começar agora")) { if (!decRodando_) escolherDecoder(Decoders::WEFAX, false); wf.configurar(wfxLpm_, wfxIoc_); wf.comecarAgora(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pegou o fax no meio? Começa já com o LPM/IOC escolhidos; depois use \"Alinhar margem\"");
    ImGui::SameLine();
    if (ImGui::Button("Terminar")) wf.parar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Encerra a imagem atual e salva");
    ImGui::SameLine();
    if (ImGui::Button("Limpar")) wf.limpar();
    ImGui::SameLine();
    if (ImGui::Button("Endireitar")) wf.endireitar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mede a inclinação pelas linhas verticais do mapa");
    ImGui::SameLine();
    ImGui::TextUnformatted("Inclinação"); ImGui::SameLine();
    if (ImGui::ArrowButton("##incmenos", ImGuiDir_Left)) wf.definirInclinacao(stt.inclinacao - 0.02);
    ImGui::SameLine();
    ImGui::Text("%+.2f", stt.inclinacao);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##incmais", ImGuiDir_Right)) wf.definirInclinacao(stt.inclinacao + 0.02);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pixels por linha (o relógio de quem transmite fora do nominal)");
    ImGui::SameLine();
    if (ImGui::Button("Zerar")) wf.definirInclinacao(0);
    ImGui::SameLine();
    if (wfxAlinhar_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.35f, 0.05f, 1));
    if (ImGui::Button(wfxAlinhar_ ? "Clique na margem..." : "Alinhar margem")) wfxAlinhar_ = !wfxAlinhar_;
    if (wfxAlinhar_) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clique depois na imagem, onde deveria ser a margem esquerda do mapa");
    ImGui::SameLine();
    if (ImGui::Button("Abrir pasta WEFAX")) {
        const std::string pasta = pastaDoExe() + "\\WEFAX";
        CreateDirectoryA(pasta.c_str(), nullptr);
        ShellExecuteA(nullptr, "open", pasta.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // --- imagem (rola; desce sozinha enquanto chega, se voce estiver no fim) ---
    const float altHist = wfxHist_.empty() ? 0.f : 78 * s_;
    const float altImg = std::max(160 * s_, ImGui::GetContentRegionAvail().y - 110 * s_ - altHist);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0x08, 0x08, 0x08, 255));
    if (ImGui::BeginChild("##wfximg", ImVec2(0, altImg), ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        const float aw = ImGui::GetContentRegionAvail().x;
        if (wfxTex_ && wfxW_ > 0 && wfxH_ > 0) {
            const float esc = aw / wfxW_;
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const bool noFim = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4 * s_;
            ImGui::Image(TEXID(wfxTex_), ImVec2(aw, wfxH_ * esc), ImVec2(0, 0), ImVec2(1.f, float(wfxH_) / wfxTexH_));
            if (wfxAlinhar_) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsItemHovered()) {
                    const float mx = ImGui::GetIO().MousePos.x;
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(mx, p0.y), ImVec2(mx, p0.y + wfxH_ * esc), IM_COL32(255, 160, 0, 200), 2 * s_);
                    if (ImGui::IsItemClicked()) {
                        wf.alinharEm((int)std::lround((mx - p0.x) / esc * 2.0));   // a textura e meia resolucao
                        wfxAlinhar_ = false;
                    }
                }
            }
            if (stt.estado == WefaxCore::RECEBENDO && noFim) ImGui::SetScrollHereY(1.0f);
        } else {
            const char* msg = stt.estado == WefaxCore::INICIO ? "tom de início ouvido - esperando a fase..."
                            : stt.estado == WefaxCore::FASE   ? "fase: medindo a margem e as linhas por minuto..."
                            : decRodando_ ? "esperando o tom de início (ou clique Começar agora)" : "decodificador parado";
            ImGui::SetCursorPos(ImVec2(10 * s_, 10 * s_));
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", msg);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // --- historico (clique abre o PNG) ---
    if (!wfxHist_.empty()) {
        if (ImGui::BeginChild("##wfxhist", ImVec2(0, altHist), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
            for (size_t i = 0; i < wfxHist_.size(); ++i) {
                const SstvFeita& f = wfxHist_[i];
                if (i) ImGui::SameLine();
                ImGui::PushID((int)i + 1000);
                const float th = 56 * s_, tw = th * f.w / std::max(1, f.h);
                if (f.tex && ImGui::ImageButton("##wfxmini", TEXID(f.tex), ImVec2(tw, th)) && !f.arquivo.empty())
                    ShellExecuteA(nullptr, "open", f.arquivo.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s%s", f.rotulo.c_str(), f.arquivo.empty() ? "\n(não salva)" : "\nclique para abrir o PNG");
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
}

void Ui::janelaDecoders()
{
    auto& d = r_.decoders();
    ImGui::SetNextWindowSize(ImVec2(640 * s_, 420 * s_), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    bool aberta = true;
    if (!ImGui::Begin("Decodificadores", &aberta, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!aberta) { decAberta_ = false; escolherDecoder(Decoders::NENHUM, false); atualizarSstv(); atualizarWefax(); }
        return;
    }
    atualizarSstv();
    atualizarWefax();
    // SSTV: a janela cresce para caber a imagem (so na hora de escolher)
    if (sstvAumentar_) {
        sstvAumentar_ = false;
        const ImVec2 tela = ImGui::GetIO().DisplaySize;
        const ImVec2 sz = ImGui::GetWindowSize();
        const ImVec2 alvo(std::min(tela.x * 0.96f, std::max(sz.x, 980 * s_)), std::min(tela.y * 0.92f, std::max(sz.y, 700 * s_)));
        ImGui::SetWindowSize(alvo);
        ImVec2 pos = ImGui::GetWindowPos();
        pos.x = std::clamp(pos.x, 0.f, std::max(0.f, tela.x - alvo.x));
        pos.y = std::clamp(pos.y, 0.f, std::max(0.f, tela.y - alvo.y));
        ImGui::SetWindowPos(pos);
    }

    // --- escolha do decodificador ---
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Decodificador");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200 * s_);
    if (ImGui::BeginCombo("##dectipo", Decoders::nome((Decoders::Tipo)decTipo_), ImGuiComboFlags_HeightLarge)) {
        static const int kOrdem[] = {Decoders::NENHUM, Decoders::CW, Decoders::RTTY, Decoders::SITORB, Decoders::PACTOR, Decoders::DSC,
                                     Decoders::ALE, Decoders::DMR, Decoders::TETRA, Decoders::HFDL, Decoders::AIS,
                                     Decoders::APRS, Decoders::ACARS, Decoders::VDL2, Decoders::DRM, Decoders::SSTV, Decoders::WEFAX,
                                     Decoders::ANALISE};
        static_assert(sizeof kOrdem / sizeof kOrdem[0] == Decoders::N_TIPOS, "faltou um decodificador no menu");
        for (int i : kOrdem)
            if (ImGui::Selectable(Decoders::nome((Decoders::Tipo)i), decTipo_ == i)) escolherDecoder(i, true);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (decRodando_) {
        if (ImGui::Button("Parar")) {
            d.parar();
            decRodando_ = false;
            anexarDecTexto(std::string("[") + Decoders::nome((Decoders::Tipo)decTipo_) + "] parado\n");
        }
    } else if (ImGui::Button("Iniciar") && decTipo_ != Decoders::NENHUM) {
        escolherDecoder(decTipo_, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reiniciar") && decTipo_ != Decoders::NENHUM) escolherDecoder(decTipo_, false);
    ImGui::SameLine();
    if (ImGui::Button("Limpar")) { decTexto_.clear(); decCol_ = 0; decLinhas_ = 0; }
    ImGui::SameLine();
    if (ImGui::Button("Selecionar tudo")) decSelTudo_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Copiar")) ImGui::SetClipboardText(decTexto_.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Salvar .txt") && !decTexto_.empty()) {
        std::string pasta = pastaDoExe() + "\\Decodificados";
        CreateDirectoryA(pasta.c_str(), nullptr);
        SYSTEMTIME st; GetLocalTime(&st);
        static const char* kTag[] = {"", "CW", "RTTY", "SITORB", "DSC", "ALE", "DMR", "TETRA", "HFDL", "AIS", "APRS", "ACARS", "VDL2", "ANALISE", "DRM", "SSTV", "WEFAX", "PACTOR"};
        char nome[96];
        std::snprintf(nome, sizeof nome, "\\RXSDR_%s_%04d%02d%02d_%02d%02d%02d.txt",
                      decTipo_ > 0 && decTipo_ < Decoders::N_TIPOS ? kTag[decTipo_] : "DEC",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        const std::string cam = pasta + nome;
        std::ofstream f(cam, std::ios::binary);
        f << decTexto_;
        erro_ = f ? "Texto salvo em " + cam : "Não foi possível salvar em " + cam;
        erroAte_ = agoraS() + 5;
    }

    // --- ajustes de cada um ---
    const float wCombo = 330 * s_;
    auto linhaCanal = [&](auto& lista, int& sel, bool aplicaFsk) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Canal");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(wCombo);
        if (comboCanal("##canal", lista, sel)) {   // com campo de busca (todas as listas)
            const auto& c = lista[sel];
            if (aplicaFsk && c.baud > 0) { decAj_.rttyBaud = c.baud; decAj_.rttyShift = c.shift; }
            if (decTipo_ == Decoders::SITORB) decAj_.sitorShift = c.shift > 0 ? c.shift : 170.f;
            if (r_.modo() != "USB") mudarModo("USB");
            if (decTipo_ == Decoders::ALE) r_.setBanda(3000);
            // Os tons ficam em ~1700 Hz (SITOR-B/DSC) ou ~1500 Hz (PACTOR): com
            // a largura em 1,4 kHz eles caiam na borda do filtro e o texto saia
            // picado (visto em 16806,5 kHz). Abaixo de 3 kHz, abre para 3 kHz.
            else if (r_.banda() < 3000) r_.setBanda(3000);
            // SITOR-B/DSC: publicado = centro do FSK -> VFO 1700 Hz abaixo (tons em ~1700 Hz)
            const bool centroFsk = decTipo_ == Decoders::SITORB || decTipo_ == Decoders::DSC;
            const bool centroPactor = decTipo_ == Decoders::PACTOR;   // tons em 1400/1600 Hz
            sintonizar(c.meio || centroPactor ? c.hz - 1500 : centroFsk ? c.hz - 1700 : c.hz, false);
            escolherDecoder(decTipo_, false);
        }
    };
    switch (decTipo_) {
    case Decoders::RTTY: {
        linhaCanal(kCanaisRtty, decCanalRtty_, true);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Baud"); ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * s_);
        char vb[16]; std::snprintf(vb, sizeof vb, "%.2f", decAj_.rttyBaud);
        bool mudou = false;
        if (ImGui::BeginCombo("##baud", vb)) {
            for (float b : kBauds) { char t[16]; std::snprintf(t, sizeof t, "%.2f", b);
                if (ImGui::Selectable(t, b == decAj_.rttyBaud)) { decAj_.rttyBaud = b; mudou = true; } }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::TextUnformatted("Shift"); ImGui::SameLine();
        ImGui::SetNextItemWidth(90 * s_);
        char vs[16]; std::snprintf(vs, sizeof vs, "%.0f Hz", decAj_.rttyShift);
        if (ImGui::BeginCombo("##shift", vs)) {
            for (float v : kShifts) { char t[16]; std::snprintf(t, sizeof t, "%.0f Hz", v);
                if (ImGui::Selectable(t, v == decAj_.rttyShift)) { decAj_.rttyShift = v; mudou = true; } }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        mudou |= ImGui::Checkbox("Inverter (LSB)", &decAj_.rttyInverter);
        if (mudou) escolherDecoder(decTipo_, false);
        break;
    }
    case Decoders::SITORB: {
        linhaCanal(kCanaisSitor, decCanalSitor_, false);
        ImGui::SameLine();
        if (ImGui::Checkbox("Inverter", &decAj_.sitorInverter)) escolherDecoder(decTipo_, false);
        ImGui::SameLine();
        ImGui::TextDisabled("verde = no ar agora (grade UTC)");
        break;
    }
    case Decoders::DSC: {
        linhaCanal(kCanaisDsc, decCanalDsc_, false);
        ImGui::SameLine();
        if (ImGui::Checkbox("Inverter", &decAj_.dscInverter)) escolherDecoder(decTipo_, false);
        ImGui::SameLine();
        ImGui::TextDisabled("verde = no ar agora (grade UTC)");
        break;
    }
    case Decoders::ALE:
        linhaCanal(kCanaisAle, decCanalAle_, false);
        break;
    case Decoders::PACTOR:
        linhaCanal(kCanaisPactor, decCanalPactor_, false);
        ImGui::SameLine();
        ImGui::TextDisabled("verde = no ar agora (grade UTC)");
        ImGui::TextDisabled("PACTOR-I (100 ou 200 baud, shift 200 Hz) em USB. O tom, a velocidade e a polaridade sao achados sozinhos;");
        ImGui::TextDisabled("cada pacote e conferido pelo CRC - texto so aparece quando o pacote chega inteiro.");
        break;
    case Decoders::CW: {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Tom (Hz, 0 = medir sozinho)"); ImGui::SameLine();
        ImGui::SetNextItemWidth(110 * s_);
        int tom = (int)decAj_.cwTom;
        if (ImGui::InputInt("##tomcw", &tom, 50, 100)) {
            decAj_.cwTom = (float)std::clamp(tom, 0, 3000);
            escolherDecoder(decTipo_, false);
        }
        break;
    }
    case Decoders::DMR: {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Modo"); ImGui::SameLine();
        ImGui::SetNextItemWidth(160 * s_);
        bool mudou = false;
        if (ImGui::BeginCombo("##dmrmodo", Dsd::nomeModo(decAj_.dmrModo))) {
            for (int i = 0; i < Dsd::nModos(); ++i)
                if (ImGui::Selectable(Dsd::nomeModo(i), decAj_.dmrModo == i)) { decAj_.dmrModo = i; mudou = true; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        mudou |= ImGui::Checkbox("Inverter", &decAj_.dmrInverter);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150 * s_);
        if (ImGui::SliderInt("##taxavoz", &decAj_.dmrTaxaVoz, 14000, 18000, "voz %d Hz"))
            d.dsd().setTaxaVoz(decAj_.dmrTaxaVoz);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ajuste fino da velocidade da voz decodificada (padrão 16150)");
        ImGui::SameLine();
        bool crua = d.dsd().linhasCruas.load();
        if (ImGui::Checkbox("Linhas do dsd-fme", &crua)) d.dsd().linhasCruas = crua;
        if (mudou && decRodando_) escolherDecoder(decTipo_, false);

        // quadro dos dois slots (igual ao painel da pagina)
        const EstadoDmr e = d.dsd().estado();
        const double t = agoraS();
        if (ImGui::BeginTable("##slots", 8, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            const char* cab[] = {"Slot", "Voz", "Tipo", "Origem (SRC)", "Destino (TGT)", "Q. voz", "Q. dados", "FEC"};
            for (const char* c : cab) ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            for (int i = 0; i < 2; ++i) {
                const SlotDmr& s = e.ts[i];
                const bool vozAgora = t - s.ultVoz < 1.0;
                const bool ativo = t - s.ultAtivo < 3.0;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("TS%d", i + 1);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(vozAgora ? ImVec4(0, 1, 0.4f, 1) : ImVec4(0.35f, 0.3f, 0.2f, 1), vozAgora ? "● VOZ" : "●");
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(s.tipo.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(ativo ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", s.src.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextColored(ativo ? ImVec4(1, 1, 1, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", s.tgt.c_str());
                ImGui::TableSetColumnIndex(5); ImGui::Text("%d", s.voz);
                ImGui::TableSetColumnIndex(6); ImGui::Text("%d", s.dados);
                ImGui::TableSetColumnIndex(7); ImGui::Text("%d", s.fec);
            }
            ImGui::EndTable();
        }
        if (ImGui::SmallButton("Zerar contadores")) d.dsd().limparContadores();
        ImGui::SameLine();
        ImGui::TextDisabled("Receba em NFM 12,5 kHz. A voz decodificada sai no lugar do áudio do rádio.");
        break;
    }
    case Decoders::TETRA: {
        if (ImGui::Checkbox("Inverter espectro (IQ conjugado)", &decAj_.tetraInverter) && decRodando_)
            escolherDecoder(decTipo_, false);
        ImGui::SameLine();
        bool cruas = d.tetra().linhasCruas.load();
        if (ImGui::Checkbox("Linhas do tetra-rx", &cruas)) d.tetra().linhasCruas = cruas;
        ImGui::TextDisabled("Sintonize a portadora TETRA (pi/4-DQPSK, 25 kHz). A voz sai no lugar do áudio.");
        const EstadoTetra e = d.tetra().estado();
        const double tq = agoraS();
        const bool vozAgora = tq - e.ultVoz < 1.0;
        auto num = [](int v) { return v >= 0 ? std::to_string(v) : std::string("-"); };
        if (ImGui::BeginTable("##tetra", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.88f, 0.56f, 1.f, 1), "SYNC / CÉLULA");
            ImGui::Text("Sync: %s", e.adquirindo ? "procurando portadora" : e.travado ? "travado" : "procurando");
            if (e.snr > -50) ImGui::Text("SNR: %.1f dB", e.snr); else ImGui::TextUnformatted("SNR: -");
            ImGui::Text("AFC: %+.0f Hz", e.afc);
            ImGui::Text("Color Code: %s", num(e.cc).c_str());
            ImGui::Text("MCC / MNC: %s / %s", num(e.mcc).c_str(), num(e.mnc).c_str());
            ImGui::Text("Cripto: %s", e.cripto.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.88f, 0.56f, 1.f, 1), "FRAME / RECEPÇÃO");
            ImGui::Text("Bursts: %d   TETMON: %d", e.bursts, e.tetmon);
            ImGui::Text("SB sync: %d   NDB sync: %d", e.sb, e.ndb);
            ImGui::Text("Slots: 1 %s  2 %s  3 %s  4 %s", e.ts[0].c_str(), e.ts[1].c_str(), e.ts[2].c_str(), e.ts[3].c_str());
            ImGui::TextColored(vozAgora ? ImVec4(0, 1, 0.4f, 1) : ImVec4(1, 1, 1, 1), "%s Voz: %d quadros", vozAgora ? "●" : "○", e.voz);
            ImGui::Text("Última chamada: %s", e.ultimaChamada.empty() ? "-" : e.ultimaChamada.c_str());
            ImGui::EndTable();
        }
        break;
    }
    case Decoders::HFDL: {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Banda"); ImGui::SameLine();
        ImGui::SetNextItemWidth(wCombo);
        auto rotulo = [](const BandaHfdl& b) {
            double lo, ce; unsigned tx; medidasHfdl(b, lo, ce, tx);
            char s[96]; std::snprintf(s, sizeof s, "%s  (%d canais, %.3f Msps)", b.nome, (int)b.canais.size(), tx / 1e6);
            return std::string(s);
        };
        const int bi = std::clamp(decHfdlBanda_, 0, (int)kHfdl.size() - 1);
        if (ImGui::BeginCombo("##hfdlbanda", rotulo(kHfdl[bi]).c_str())) {
            for (int i = 0; i < (int)kHfdl.size(); ++i)
                if (ImGui::Selectable(rotulo(kHfdl[i]).c_str(), i == bi)) {
                    decHfdlBanda_ = i;
                    escolherDecoder(Decoders::HFDL, false);     // re-sintoniza e recomeca
                }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("O rádio vai para o centro da banda (em USB) e o dumphfdl acompanha todos os canais "
                            "de uma vez. Não mexa na sintonia enquanto ouve.");
        // Os avioes ouvidos, com o voo e o prefixo ligados ao FlightAware (igual a pagina)
        const auto avs = d.hfdl().aeronaves();
        if (!avs.empty() && ImGui::BeginTable("##avioes", 7, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                                              ImVec2(0, 130 * s_))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            for (const char* c : {"Voo", "Prefixo", "ICAO", "Posição", "Canal", "Msgs", "Visto"}) ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            const double ta = agoraS();
            auto abrir = [](const std::string& url) { ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
            int id = 0;
            for (const auto& a : avs) {
                ImGui::PushID(id++);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (a.voo.empty()) ImGui::TextUnformatted("-");
                else {
                    if (ImGui::TextLink(a.voo.c_str())) abrir(a.urlVoo());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir o voo no FlightAware\n%s", a.urlVoo().c_str());
                }
                ImGui::TableSetColumnIndex(1);
                if (a.prefixo.empty()) ImGui::TextUnformatted("-");
                else {
                    if (ImGui::TextLink(a.prefixo.c_str())) abrir(a.urlPrefixo());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir o avião no FlightAware\n%s", a.urlPrefixo().c_str());
                }
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(a.hex.empty() ? "-" : a.hex.c_str());
                ImGui::TableSetColumnIndex(3);
                if (a.lat <= 90) ImGui::Text("%.3f %.3f", a.lat, a.lon); else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(4);
                if (a.freqKHz > 0) ImGui::Text("%.0f", a.freqKHz); else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(5); ImGui::Text("%d", a.msgs);
                ImGui::TableSetColumnIndex(6);
                const double s = ta - a.visto;
                if (s < 120) ImGui::Text("%.0f s", s); else ImGui::Text("%.0f min", s / 60);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        break;
    }
    case Decoders::AIS: {
        const int porta = d.ais().portaWeb();
        if (ImGui::Button("Abrir o mapa dos navios") && porta > 0 && d.ais().rodando()) {
            char url[64]; std::snprintf(url, sizeof url, "http://127.0.0.1:%d", porta);
            ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("162,000 MHz: canais 161,975 e 162,025 ao mesmo tempo (mapa do próprio AIS-catcher).");
        const auto nav = d.ais().navios();
        if (!nav.empty() && ImGui::BeginTable("##navios", 7, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                                              ImVec2(0, 130 * s_))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            for (const char* c : {"MMSI", "Nome", "Indicativo", "Posição", "Velocidade", "Rumo", "Visto"}) ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            const double ta = agoraS();
            int idn = 0;
            for (const auto& n : nav) {
                ImGui::PushID(idn++);
                ImGui::TableNextRow();
                // Igual a pagina: o MMSI abre o VesselFinder (seleciona o navio no
                // mapa pelo MMSI); o "MT" ao lado abre a ficha no MarineTraffic.
                ImGui::TableSetColumnIndex(0);
                char mm[16]; std::snprintf(mm, sizeof mm, "%09lld", n.mmsi);
                const std::string vf = std::string("https://www.vesselfinder.com/?mmsi=") + mm;
                const std::string mt = std::string("https://www.marinetraffic.com/en/ais/details/ships/mmsi:") + mm;
                if (ImGui::TextLink(mm)) ShellExecuteA(nullptr, "open", vf.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir no VesselFinder\n%s", vf.c_str());
                ImGui::SameLine();
                if (ImGui::TextLink("MT")) ShellExecuteA(nullptr, "open", mt.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir no MarineTraffic\n%s", mt.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(n.nome.empty() ? "-" : n.nome.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(n.indicativo.empty() ? "-" : n.indicativo.c_str());
                ImGui::TableSetColumnIndex(3);
                if (n.lat <= 90) ImGui::Text("%.4f %.4f", n.lat, n.lon); else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(4);
                if (n.vel >= 0) ImGui::Text("%.1f nós", n.vel); else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(5);
                if (n.rumo >= 0) ImGui::Text("%.0f°", n.rumo); else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(6);
                const double s = ta - n.visto;
                if (s < 120) ImGui::Text("%.0f s", s); else ImGui::Text("%.0f min", s / 60);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        break;
    }
    case Decoders::DRM: {
        // emissoras: as que estao no ar agora aparecem em verde
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Emissora"); ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(wCombo, ImGui::GetContentRegionAvail().x - 4 * s_));
        const int nDrm = (int)(sizeof kDrm / sizeof kDrm[0]);
        const std::string atual = decDrmSel_ >= 0 && decDrmSel_ < nDrm ? rotuloDrm(kDrm[decDrmSel_])
                                                                        : std::string("- escolha para sintonizar -");
        // sintoniza a emissora i (so quando voce clica; nada e varrido sozinho)
        auto irPara = [&](int i) {
            decDrmSel_ = i;
            const uint64_t hz = (uint64_t)kDrm[i].khz * 1000;
            if (r_.modo() != "AM") mudarModo("AM");
            r_.setBanda(10000);
            sintonizar(hz, false);
            // o centro do dongle 20 kHz ao lado: o DC nao cai no canal
            if (zoom_ <= 0 && r_.taxa() >= 200000) r_.centralizar(hz + 20000);
            decMudouFreq_ = true;
            escolherDecoder(Decoders::DRM, false);    // recomeca a procura do sinal
        };
        if (ImGui::BeginCombo("##drmemis", atual.c_str(), ImGuiComboFlags_HeightLarge)) {
            const std::string q = campoBuscaLista("buscar: frequência (kHz) ou emissora, país, idioma");
            auto bate = [&](int i) { return buscaBate((uint64_t)kDrm[i].khz * 1000, rotuloDrm(kDrm[i]), q); };
            if (!q.empty()) {
                int achados = 0;
                for (int i = 0; i < nDrm; ++i) if (bate(i)) ++achados;
                resultadoBusca(q, achados);
            }
            ImGui::Separator();
            ImGui::BeginChild("##listadrm", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 20));
            bool fechar = false;
            for (int i = 0; i < nDrm; ++i) {
                if (!q.empty() && !bate(i)) continue;
                const bool noAr = drmNoAr(kDrm[i]);
                if (noAr) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.f, 0.5f, 1));
                ImGui::PushID(i);
                const bool escolheu = ImGui::Selectable(rotuloDrm(kDrm[i]).c_str(), decDrmSel_ == i);
                ImGui::PopID();
                if (noAr) ImGui::PopStyleColor();
                if (escolheu) { irPara(i); fechar = true; }
            }
            ImGui::EndChild();
            if (fechar) ImGui::CloseCurrentPopup();   // a lista fica numa janela-filha
            ImGui::EndCombo();
        }
        // No ar agora (pela grade): um botao por emissora; clicou, sintonizou
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.5f, 1), "\xE2\x97\x8F No ar agora:");
            int mostradas = 0;
            for (int i = 0; i < nDrm; ++i) {
                if (!drmNoAr(kDrm[i])) continue;
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < 90 * s_) ImGui::NewLine();
                char rot[48];
                std::snprintf(rot, sizeof rot, "%d##noar%d", kDrm[i].khz, i);
                const bool atualEsta = (int64_t)r_.vfo() == (int64_t)kDrm[i].khz * 1000;
                if (atualEsta) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.45f, 0.2f, 1));
                if (ImGui::SmallButton(rot)) irPara(i);
                if (atualEsta) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s - %s (%s)\n%02d:%02d-%02d:%02d UTC", kDrm[i].nome, kDrm[i].local, kDrm[i].lingua,
                                      kDrm[i].ini / 100, kDrm[i].ini % 100, kDrm[i].fim / 100, kDrm[i].fim % 100);
                ++mostradas;
            }
            if (!mostradas) { ImGui::SameLine(); ImGui::TextDisabled("nenhuma pela grade"); }
        }
        if (ImGui::Checkbox("Inverter espectro", &decAj_.drmInverter) && decRodando_)
            escolherDecoder(decTipo_, false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Só se o sinal nunca travar: troca I e Q (espectro de cabeça para baixo)");
        ImGui::SameLine();
        bool cruas = d.drm().linhasCruas.load();
        if (ImGui::Checkbox("Linhas do dream", &cruas)) d.drm().linhasCruas = cruas;

        const EstadoDrm e = d.drm().estado();
        // as "luzes" do Dream: verde = ok, amarelo = erro de CRC, vermelho = erro, cinza = nada ainda
        auto luz = [&](const char* nome, int v, const char* dica) {
            const ImVec4 c = v == 0 ? ImVec4(0.2f, 1.f, 0.4f, 1) : v == 1 ? ImVec4(1.f, 0.85f, 0.2f, 1)
                           : v == 2 ? ImVec4(1.f, 0.3f, 0.3f, 1) : ImVec4(0.35f, 0.35f, 0.35f, 1);
            ImGui::TextColored(c, "\xE2\x97\x8F");
            ImGui::SameLine(0, 3 * s_);
            ImGui::TextUnformatted(nome);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", dica);
            ImGui::SameLine(0, 14 * s_);
        };
        luz("Entrada", e.io, "IQ chegando ao dream");
        luz("Tempo", e.tempo, "sincronismo de tempo (achou o sinal OFDM)");
        luz("Quadro", e.quadro, "sincronismo de quadro");
        luz("FAC", e.fac, "canal de acesso rápido: modo, largura e QAM");
        luz("SDC", e.sdc, "descrição do serviço: nome da emissora, idioma, país");
        luz("Áudio", e.msc, "canal principal (o áudio)");
        ImGui::NewLine();

        static const char kRob[] = "ABCDE";
        static const char* kQam[] = {"4-QAM", "16-QAM", "64-QAM", "64-QAM (hier.)", "64-QAM (hier.)"};
        if (ImGui::BeginTable("##drm", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.88f, 0.56f, 1.f, 1), "SINAL");
            if (e.comStatus && e.tempo == 0) ImGui::Text("SNR: %.1f dB", e.snr); else ImGui::TextUnformatted("SNR: -");
            if (e.robustez >= 0 && e.robustez < 5)
                ImGui::Text("Modo %c   %.0f kHz   intercalador %s", kRob[e.robustez], e.larguraKHz,
                            e.intercalador == 1 ? "curto" : e.intercalador == 0 ? "longo" : "-");
            else ImGui::TextUnformatted("Modo: -");
            ImGui::Text("MSC %s   SDC %s", e.mscQam >= 0 && e.mscQam < 5 ? kQam[e.mscQam] : "-",
                        e.sdcQam == 0 ? "4-QAM" : e.sdcQam == 1 ? "16-QAM" : "-");
            if (e.doppler >= 0) ImGui::Text("Doppler %.2f Hz   atraso %.2f ms", e.doppler, e.atrasoMs >= 0 ? e.atrasoMs : 0.0);
            else ImGui::TextUnformatted("Doppler / atraso: -");
            ImGui::Text("Áudio guardado: %.1f s", e.bufferS);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.88f, 0.56f, 1.f, 1), "EMISSORA");
            ImGui::PushFont(f_.negrito);
            ImGui::TextColored(ImVec4(0, 0.83f, 0.83f, 1), "%s", e.estacao.empty() ? "-" : e.estacao.c_str());
            ImGui::PopFont();
            std::string lugar = e.pais;
            if (!e.idioma.empty()) lugar += (lugar.empty() ? "" : "  |  ") + e.idioma;
            ImGui::TextUnformatted(lugar.empty() ? "-" : lugar.c_str());
            if (!e.programa.empty()) ImGui::Text("Programa: %s", e.programa.c_str());
            if (!e.codec.empty())
                ImGui::Text("%s  %.1f kbps  %s%s%s", e.codec.c_str(), e.kbps, e.modoAudio.c_str(),
                            e.protecao.empty() ? "" : "  |  ", e.protecao.c_str());
            if (!e.horaDrm.empty()) ImGui::Text("Hora da emissora: %s UTC", e.horaDrm.c_str());
            ImGui::EndTable();
        }
        if (!e.texto.empty()) {
            ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Texto:");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", e.texto.c_str());
        }
        ImGui::TextDisabled("Sintonize a frequência anunciada (o centro do canal). Enquanto o áudio DRM não sai limpo, você");
        ImGui::TextDisabled("ouve o rádio normal; quando a luz Áudio fica verde, entra o som decodificado (AAC / xHE-AAC).");
        ImGui::TextDisabled("Precisa de uns 10 dB de SNR em 16-QAM ou 15 dB em 64-QAM.");
        break;
    }
    case Decoders::ACARS:
        ImGui::TextDisabled("131,550 e 131,825 MHz ao mesmo tempo (AM). O rádio fica com o centro no meio dos dois;");
        ImGui::TextDisabled("não mexa na sintonia enquanto ouve. Voo e prefixo abrem no FlightAware.");
        tabelaAvioes("##avacars", d.acars().aeronaves());
        break;
    case Decoders::VDL2:
        ImGui::TextDisabled("136,975 MHz (VDL modo 2, 31,5 kbit/s). O dongle passa para 1,05 Msps enquanto ouve.");
        ImGui::TextDisabled("Não mexa na sintonia enquanto ouve. Voo e prefixo abrem no FlightAware.");
        tabelaAvioes("##avvdl2", d.vdl2().aeronaves());
        break;
    case Decoders::APRS: {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Canal"); ImGui::SameLine();
        ImGui::SetNextItemWidth(wCombo);
        const int ai = std::clamp(decAprsCanal_, 0, 3);
        if (ImGui::BeginCombo("##aprscanal", kAprs[ai].nome)) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(kAprs[i].nome, i == ai)) {
                    decAprsCanal_ = i;
                    escolherDecoder(Decoders::APRS, true);
                }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("O direwolf ouve o áudio do rádio: 1200 baud em FM (VHF) ou 300 baud em USB (HF).");
        // estacoes ouvidas: indicativo e digipeaters abrem no aprs.fi (igual a pagina)
        const auto est = d.aprs().estacoes();
        if (!est.empty() && ImGui::BeginTable("##aprs", 6, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                                              ImVec2(0, 130 * s_))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Indicativo", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Via", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("Posição", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Pac.", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Visto", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn("Mensagem", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableHeadersRow();
            const double ta = agoraS();
            auto link = [](const std::string& call) {
                if (ImGui::TextLink(call.c_str()))
                    ShellExecuteA(nullptr, "open", Aprs::urlAprsFi(call).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir no aprs.fi\n%s", Aprs::urlAprsFi(call).c_str());
            };
            int id = 0;
            for (const auto& e : est) {
                ImGui::PushID(id++);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); link(e.indicativo);
                ImGui::TableSetColumnIndex(1);
                bool primeiro = true;
                for (const auto& v : e.via) {
                    std::string u = v; for (auto& ch : u) ch = (char)std::toupper((unsigned char)ch);
                    if (!primeiro) { ImGui::SameLine(0, 0); ImGui::TextUnformatted(","); ImGui::SameLine(0, 2 * s_); }
                    primeiro = false;
                    if (u.rfind("WIDE", 0) == 0 || u == "RELAY" || u == "TRACE" || u.rfind("TCPIP", 0) == 0) ImGui::TextUnformatted(v.c_str());
                    else { ImGui::PushID(v.c_str()); link(v); ImGui::PopID(); }
                }
                if (e.via.empty()) ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(2);
                if (e.lat <= 90) ImGui::Text("%.4f %.4f", e.lat, e.lon); else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(3); ImGui::Text("%d", e.pacotes);
                ImGui::TableSetColumnIndex(4);
                const double s = ta - e.visto;
                if (s < 120) ImGui::Text("%.0f s", s); else ImGui::Text("%.0f min", s / 60);
                ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(e.info.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        break;
    }
    case Decoders::SSTV:
        painelSstv();
        break;
    case Decoders::WEFAX:
        painelWefax();
        break;
    case Decoders::ANALISE: {
        const float p = d.progressoAnalise();
        ImGui::ProgressBar(p, ImVec2(260 * s_, 0), p >= 1 ? "pronta" : nullptr);
        ImGui::SameLine();
        ImGui::TextDisabled("Sintonize o sinal em USB; mede tons, shift e velocidade.");
        break;
    }
    default:
        ImGui::TextDisabled("Escolha um decodificador. DMR, TETRA, HFDL, AIS e APRS usam programas da pasta decoders.");
        break;
    }

    // --- situacao ---
    if (decTipo_ != Decoders::NENHUM) {
        const bool trav = d.travado();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float r = 5 * s_;
        dl->AddCircleFilled(ImVec2(p.x + r + 1, p.y + ImGui::GetTextLineHeight() * 0.5f + 1), r,
                            trav ? IM_COL32(0x00, 0xff, 0x66, 255) : IM_COL32(0x55, 0x40, 0x10, 255));
        ImGui::Dummy(ImVec2(2 * r + 6 * s_, ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.f, 0.67f, 0.f, 1.f), "%s", d.estado().c_str());
    }
    ImGui::Separator();

    // --- texto ---
    const std::string novo = d.pegarTexto();
    if (!novo.empty()) anexarDecTexto(novo);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0x02, 0x06, 0x03, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0x02, 0x06, 0x03, 255));
    if (ImGui::BeginChild("##dectexto", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const bool noFim = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4 * s_;
        ImGui::PushFont(f_.mono);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0xb0, 0xf0, 0x20, 255));
        // Caixa de texto SO DE LEITURA, alta o bastante para caber tudo: quem
        // rola e a janela de fora. Da para selecionar com o mouse, Ctrl+A e
        // Ctrl+C - e o botao "Selecionar tudo" faz o Ctrl+A.
        const float lh = ImGui::GetTextLineHeight();
        const float alt = std::max(ImGui::GetContentRegionAvail().y,
                                   (decLinhas_ + 2) * lh + 2 * ImGui::GetStyle().FramePadding.y);
        if (decSelTudo_) { ImGui::SetKeyboardFocusHere(); decSelTudo_ = false; decSelPend_ = 30; decSelFeito_ = 0; }
        ImGui::InputTextMultiline("##dectxt", decTexto_.data(), decTexto_.size() + 1, ImVec2(-FLT_MIN, alt),
                                  ImGuiInputTextFlags_ReadOnly);
        // A caixa de varias linhas nao tem "selecionar tudo ao ativar": assim
        // que ela fica ativa (um ou dois quadros depois do foco), seleciona.
        // So vale com a caixa JA ATIVA: o estado que existia antes (de um clique
        // anterior nela) e recriado quando ela ativa, e a selecao se perdia -
        // por isso o botao parecia nao fazer nada. Seleciona por 3 quadros
        // seguidos depois de ativa, para nao ser desfeito pela ativacao.
        if (decSelPend_ > 0) {
            ImGuiInputTextState* st = ImGui::GetInputTextState(ImGui::GetItemID());
            if (st && ImGui::IsItemActive()) {
                st->SelectAll();
                if (++decSelFeito_ >= 3) decSelPend_ = 0;
            } else {
                --decSelPend_;
            }
        }
        ImGui::PopStyleColor();
        ImGui::PopFont();
        // rola sozinho quando ja estava no fim (quem subiu para ler, fica)
        if (!novo.empty() && noFim) ImGui::SetScrollY(ImGui::GetScrollMaxY() + alt);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::End();
    if (!aberta) { decAberta_ = false; escolherDecoder(Decoders::NENHUM, false); }
}


// ===========================================================================
//  IF DISPLAY (igual a pagina): 20 kHz fixos em torno da sintonia, a
//  esquerda do espectro. Nao ha FFT nova: e a mesma da cachoeira, recortada.
// ===========================================================================
static const double kIfSpan = 20000.0;

void Ui::criarTexturaIf()
{
    if (ifTex_) { ifTex_->Release(); ifTex_ = nullptr; }
    if (dev_->CreateTexture(ifW_, ifH_, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &ifTex_, nullptr) != D3D_OK) {
        ifTex_ = nullptr; return;
    }
    D3DLOCKED_RECT lr;
    if (ifTex_->LockRect(0, &lr, nullptr, 0) == D3D_OK) {
        const uint32_t fundo = corPaleta(0);
        for (int y = 0; y < ifH_; ++y) {
            uint32_t* d = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
            for (int x = 0; x < ifW_; ++x) d[x] = fundo;
        }
        ifTex_->UnlockRect(0);
    }
    ifPos_ = 0;
    ifDb_.assign((size_t)ifW_ * ifH_, (int8_t)-128);
}

void Ui::recolorirIf()
{
    if (!ifTex_ || ifDb_.empty() || lut_.size() < 256) return;
    D3DLOCKED_RECT lr;
    if (ifTex_->LockRect(0, &lr, nullptr, 0) != D3D_OK) return;
    for (int y = 0; y < ifH_; ++y) {
        uint32_t* d = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
        const int8_t* o = &ifDb_[(size_t)y * ifW_];
        for (int x = 0; x < ifW_; ++x) d[x] = lut_[(int)o[x] + 128];
    }
    ifTex_->UnlockRect(0);
}

// dB na frequencia pedida, lido de um vetor de bins (interpolado)
template <typename T>
static float dbNoBin(const std::vector<T>& bins, double hz, uint64_t centro, uint32_t taxa, bool& dentro)
{
    const int nb = (int)bins.size();
    dentro = false;
    if (nb < 2 || taxa == 0) return -128.f;
    const double f = (hz - (double)centro) / taxa * nb + nb / 2.0;
    if (f < 0 || f > nb - 1) return -128.f;
    const int i = (int)f;
    const int j = std::min(nb - 1, i + 1);
    const float fr = (float)(f - i);
    dentro = true;
    return (float)bins[i] * (1 - fr) + (float)bins[j] * fr;
}

void Ui::linhaIf()
{
    if (!ifOn_ || fft_.bins.size() < 2 || lut_.size() < 256) return;
    if (!ifTex_) criarTexturaIf();
    if (!ifTex_) return;
    ifPos_ = (ifPos_ + ifH_ - 1) % ifH_;
    RECT rc{0, ifPos_, ifW_, ifPos_ + 1};
    D3DLOCKED_RECT lr;
    if (ifTex_->LockRect(0, &lr, &rc, 0) != D3D_OK) return;
    uint32_t* d = (uint32_t*)lr.pBits;
    int8_t* o = &ifDb_[(size_t)ifPos_ * ifW_];
    const double f0 = (double)r_.vfo() - kIfSpan / 2;
    for (int x = 0; x < ifW_; ++x) {
        bool dentro;
        const float db = dbNoBin(fft_.bins, f0 + x * kIfSpan / (ifW_ - 1), fft_.centro, fft_.taxa, dentro);
        const int v = dentro ? std::clamp((int)std::lround(db), -127, 127) : -128;
        o[x] = (int8_t)v;
        d[x] = lut_[v + 128];
    }
    ifTex_->UnlockRect(0);
}

void Ui::ifDisplay(float x, float y, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 azul = IM_COL32(0x20, 0xaa, 0xdd, 255);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 255), 4 * s_);

    // cabecalho: titulo e largura da janela
    const float hc = 20 * s_;
    dl->AddRectFilled(ImVec2(x + 1, y + 1), ImVec2(x + w - 1, y + hc), IM_COL32(0x06, 0x0c, 0x08, 255), 4 * s_, ImDrawFlags_RoundCornersTop);
    ImGui::PushFont(f_.pequena);
    dl->AddText(ImVec2(x + 6 * s_, y + (hc - ImGui::GetFontSize()) * 0.5f), IM_COL32(0x7f, 0xb0, 0xff, 255), "IF Display — 20 kHz");
    static const char* nomes[3] = {"estreita", "média", "larga"};
    const float cw = 74 * s_;
    ImGui::SetCursorScreenPos(ImVec2(x + w - cw - 4 * s_, y + 2 * s_));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * s_, 1 * s_));
    ImGui::SetNextItemWidth(cw);
    if (ImGui::BeginCombo("##iflarg", nomes[ifTam_])) {
        for (int i = 0; i < 3; ++i) if (ImGui::Selectable(nomes[i], ifTam_ == i)) ifTam_ = i;
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    dl->AddLine(ImVec2(x, y + hc), ImVec2(x + w, y + hc), C_BORDER);

    const float ys = y + hc, hs = std::floor((h - hc) * 0.34f);
    const float yw = ys + hs + 1, hw = y + h - yw - 1;
    const double f0 = (double)r_.vfo() - kIfSpan / 2;
    auto hzX = [&](double hz) { return x + (float)((hz - f0) / kIfSpan * w); };

    // --- espectro ---
    dl->PushClipRect(ImVec2(x + 1, ys), ImVec2(x + w - 1, ys + hs), true);
    // faixa passante (muda de lado conforme o modo, como na pagina)
    const std::string m = r_.modo();
    const int bw = r_.banda();
    double fa = (double)r_.vfo() - bw / 2.0;
    if (m == "USB") fa = (double)r_.vfo();
    else if (m == "LSB") fa = (double)r_.vfo() - bw;
    dl->AddRectFilled(ImVec2(hzX(fa), ys), ImVec2(std::max(hzX(fa + bw), hzX(fa) + 1), ys + hs), IM_COL32(255, 220, 0, 31));
    if (r_.ligado() && suave_.size() > 1) {
        const float ref = (wfBrilho_ - 100.f) * 0.5f, range = std::max(10.f, wfRange_);
        const int cols = std::max(2, (int)w);
        std::vector<ImVec2> pts(cols);
        for (int c = 0; c < cols; ++c) {
            bool dentro;
            const float db = dbNoBin(suave_, f0 + c * kIfSpan / (cols - 1), centroMostrado_, taxaMostrada_, dentro);
            const float n = std::clamp((db - (ref - range)) / range, 0.f, 1.f);
            pts[c] = ImVec2(x + c * w / (cols - 1), ys + hs - n * hs);
        }
        dl->AddPolyline(pts.data(), cols, IM_COL32(0x39, 0xff, 0x14, 255), 0, 1.f * s_);
    }
    const float xc = hzX((double)r_.vfo());
    dl->AddLine(ImVec2(xc, ys), ImVec2(xc, ys + hs), IM_COL32(255, 255, 255, 140));
    dl->PopClipRect();
    dl->AddLine(ImVec2(x, ys + hs), ImVec2(x + w, ys + hs), C_BORDER);

    // --- cachoeira ---
    dl->AddRectFilled(ImVec2(x + 1, yw), ImVec2(x + w - 1, yw + hw), IM_COL32(0, 0, 32, 255));
    if (ifTex_ && r_.ligado() && hw > 2) {
        const int hd = std::min((int)hw, ifH_);
        const float esc = hw / hd;
        const int p = ifPos_;
        if (p + hd <= ifH_) {
            dl->AddImage(TEXID(ifTex_), ImVec2(x + 1, yw), ImVec2(x + w - 1, yw + hw),
                         ImVec2(0, (float)p / ifH_), ImVec2(1, (float)(p + hd) / ifH_));
        } else {
            const int a = ifH_ - p;
            dl->AddImage(TEXID(ifTex_), ImVec2(x + 1, yw), ImVec2(x + w - 1, yw + a * esc),
                         ImVec2(0, (float)p / ifH_), ImVec2(1, 1));
            dl->AddImage(TEXID(ifTex_), ImVec2(x + 1, yw + a * esc), ImVec2(x + w - 1, yw + hw),
                         ImVec2(0, 0), ImVec2(1, (float)(hd - a) / ifH_));
        }
    }
    dl->AddLine(ImVec2(xc, yw), ImVec2(xc, yw + hw), IM_COL32(255, 255, 255, 90));
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), azul, 4 * s_, 0, 1.f);

    // mouse: clique sintoniza, roda anda um passo
    ImGui::SetCursorScreenPos(ImVec2(x, ys));
    ImGui::InvisibleButton("##ifarea", ImVec2(w, h - hc));
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        const double hz = f0 + (io.MousePos.x - x) / w * kIfSpan;
        dl->AddLine(ImVec2(io.MousePos.x, ys), ImVec2(io.MousePos.x, y + h), IM_COL32(255, 255, 255, 50));
        if (ImGui::IsItemClicked()) sintonizar((uint64_t)std::max(0.0, hz), true);
        if (io.MouseWheel != 0) {
            const int64_t v = (int64_t)r_.vfo() + (io.MouseWheel > 0 ? passo_ : -passo_);
            if (v > 0) sintonizar((uint64_t)v, false);
        }
    }
}

} // namespace masdr
