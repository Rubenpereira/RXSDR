#include "Dsd.h"
#include "../ui/Idioma.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <regex>

namespace masdr {

namespace {

double agora()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::wstring pastaDecoders()
{
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring d(p);
    const auto pos = d.find_last_of(L"\\/");
    return (pos == std::wstring::npos ? std::wstring(L".") : d.substr(0, pos)) + L"\\decoders";
}

// Windows 8.1 = 6.3. O GetVersionEx mente sem manifesto; o RtlGetVersion nao.
bool windowsNovoParaCygwin()
{
    typedef LONG(WINAPI * Fn)(PRTL_OSVERSIONINFOW);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    Fn f = nt ? (Fn)GetProcAddress(nt, "RtlGetVersion") : nullptr;
    if (!f) return true;
    RTL_OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (f(&v) != 0) return true;
    return v.dwMajorVersion > 6 || (v.dwMajorVersion == 6 && v.dwMinorVersion >= 3);
}

std::string horaUtc()
{
    SYSTEMTIME st; GetSystemTime(&st);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return b;
}

// Tabela de modos - a mesma do DsdManager (opcoes da ajuda do dsd-fme.exe)
struct Modo { const char* nome; const char* proto; const char* mod; const char* inv; bool lsn; };
const Modo kModos[] = {
    {"Automático", "-fa", "-ma", "", false},
    {"DMR", "-fs", "-mg", "-xr", true},
    {"P25 fase 1", "-f1", "-ma", "", false},
    {"P25 fase 2", "-f2", "-m2", "", false},
    {"NXDN 96", "-fn", "-ma", "", false},
    {"NXDN 48", "-fi", "-ma", "", false},
    {"dPMR", "-fm", "-ma", "-xd", false},
    {"YSF (Fusion)", "-fy", "-ma", "", false},
    {"D-STAR", "-fd", "-ma", "", false},
    {"M17", "-fz", "-ma", "", false},
    {"X2-TDMA", "-fx", "-ma", "-xx", false},
    {"ProVoice", "-fp", "-ma", "", false},
    {"EDACS", "-fh", "-ma", "", false},
};
} // namespace

const char* Dsd::nomeModo(int i) { return (i >= 0 && i < nModos()) ? T(kModos[i].nome) : "?"; }
int Dsd::nModos() { return (int)(sizeof kModos / sizeof kModos[0]); }

Dsd::Dsd() {}
Dsd::~Dsd() { parar(); }

bool Dsd::iniciar(int modo, bool inverter, std::string& erro)
{
    parar();
    {
        std::lock_guard<std::mutex> lk(estMutex_);
        est_ = EstadoDmr();
        ultimoSlot_ = 0;
        ultimaMsg_.clear(); ultimaCrua_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(vozMutex_);
        voz_.clear(); tocando_ = false; caudaVoz_.clear(); posVoz_ = 0;
    }
    if (!windowsNovoParaCygwin()) {
        erro = T("O decodificador DMR (dsd-fme) precisa do Windows 8.1, 10 ou 11 - no Windows 7 ele nao abre.");
        std::lock_guard<std::mutex> lk(estMutex_); est_.erro = erro;
        return false;
    }
    const std::wstring pasta = pastaDecoders();
    const std::wstring exe = pasta + L"\\dsd-fme.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        erro = T("Nao achei decoders\\dsd-fme.exe ao lado do RXSDR.exe.");
        std::lock_guard<std::mutex> lk(estMutex_); est_.erro = erro;
        return false;
    }

    // UDP local para a voz decodificada
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { erro = T("sem socket UDP"); return false; }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    int alen = sizeof a;
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || getsockname(s, (sockaddr*)&a, &alen) != 0) {
        closesocket(s); erro = T("nao consegui reservar a porta UDP da voz"); return false;
    }
    DWORD to = 200;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
    int rb = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rb, sizeof rb);
    const int porta = ntohs(a.sin_port);

    // canos: stdin (nos escrevemos) e stdout+stderr (nos lemos)
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
    if (!CreatePipe(&inR, &inW, &sa, 1 << 20) || !CreatePipe(&outR, &outW, &sa, 1 << 16)) {
        closesocket(s); erro = T("CreatePipe falhou"); return false;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    const Modo& m = kModos[std::clamp(modo, 0, nModos() - 1)];
    std::wstring cmd = L"\"" + exe + L"\"";
    auto arg = [&](const char* s) { if (s && *s) { cmd += L" "; for (const char* p = s; *p; ++p) cmd += (wchar_t)*p; } };
    arg(m.proto);
    if (m.lsn) arg("-l");
    arg(m.mod);
    if (inverter) arg(m.inv);
    arg("-V"); arg("3"); arg("-i"); arg("-"); arg("-o");
    char udp[48]; std::snprintf(udp, sizeof udp, "udp:127.0.0.1:%d", porta);
    arg(udp);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = outW;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> linha(cmd.begin(), cmd.end()); linha.push_back(0);
    const BOOL ok = CreateProcessW(nullptr, linha.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   pasta.c_str(), &si, &pi);
    CloseHandle(inR); CloseHandle(outW);
    if (!ok) {
        CloseHandle(inW); CloseHandle(outR); closesocket(s);
        erro = T("nao consegui abrir o dsd-fme.exe (erro ") + std::to_string(GetLastError()) + ")";
        std::lock_guard<std::mutex> lk(estMutex_); est_.erro = erro;
        return false;
    }
    CloseHandle(pi.hThread);
    proc_ = pi.hProcess; stdinW_ = inW; saidaR_ = outR; sock_ = (uintptr_t)s;
    vivo_ = true;
    { std::lock_guard<std::mutex> lk(estMutex_); est_.rodando = true; }
    thTexto_ = std::thread([this] { lerTexto(); });
    thUdp_ = std::thread([this] { lerUdp(); });
    if (aoTexto) {
        std::string t = T("[DMR] dsd-fme iniciado - modo ");
        t += m.nome;
        if (inverter && *m.inv) t += T(" (invertido)");
        t += "\n";
        aoTexto(t);
    }
    return true;
}

void Dsd::parar()
{
    if (!proc_ && !thTexto_.joinable() && !thUdp_.joinable()) return;
    vivo_ = false;
    if (proc_) {
        TerminateProcess((HANDLE)proc_, 0);          // solta quem estiver escrevendo no cano
        WaitForSingleObject((HANDLE)proc_, 2000);
    }
    {
        std::lock_guard<std::mutex> lk(escritaMutex_);
        if (stdinW_) { CloseHandle((HANDLE)stdinW_); stdinW_ = nullptr; }
    }
    if (sock_ != ~(uintptr_t)0) { closesocket((SOCKET)sock_); sock_ = ~(uintptr_t)0; }
    {
        std::lock_guard<std::mutex> lk(escritaMutex_);
        if (dump_) { fclose((FILE*)dump_); dump_ = nullptr; }
        dumpTestado_ = false;
    }
    if (thTexto_.joinable()) thTexto_.join();
    if (thUdp_.joinable()) thUdp_.join();
    if (saidaR_) { CloseHandle((HANDLE)saidaR_); saidaR_ = nullptr; }
    if (proc_) { CloseHandle((HANDLE)proc_); proc_ = nullptr; }
    std::lock_guard<std::mutex> lk(estMutex_);
    est_.rodando = false;
}

void Dsd::escrever(const int16_t* pcm, size_t n)
{
    if (!vivo_ || !pcm || !n) return;
    std::lock_guard<std::mutex> lk(escritaMutex_);
    if (!stdinW_) return;
    // Diagnostico (igual ao /tmp/dsd_dump do RXSDR): se existir o arquivo
    // decoders\dsd_dump.on, grava o que vai para o dsd-fme em dsd_feed.s16.
    if (dump_ == nullptr && !dumpTestado_) {
        dumpTestado_ = true;
        if (GetFileAttributesW((pastaDecoders() + L"\\dsd_dump.on").c_str()) != INVALID_FILE_ATTRIBUTES)
            dump_ = _wfopen((pastaDecoders() + L"\\dsd_feed.s16").c_str(), L"wb");
    }
    if (dump_) fwrite(pcm, 2, n, (FILE*)dump_);
    const char* p = (const char*)pcm;
    DWORD resta = (DWORD)(n * 2), w = 0;
    while (resta > 0) {
        if (!WriteFile((HANDLE)stdinW_, p, resta, &w, nullptr)) {
            vivo_ = false;
            std::lock_guard<std::mutex> le(estMutex_);
            est_.rodando = false;
            if (est_.erro.empty()) est_.erro = T("o dsd-fme fechou");
            return;
        }
        p += w; resta -= w;
    }
}

// --- voz: UDP -> 48 kHz --------------------------------------------------------
void Dsd::lerUdp()
{
    std::vector<char> buf(65536);
    std::vector<float> x;
    while (vivo_) {
        const int n = recv((SOCKET)sock_, buf.data(), (int)buf.size(), 0);
        if (n <= 0) continue;                         // tempo esgotado ou fechando
        if (n < 2 || (n & 1)) continue;
        const int ns = n / 2;
        x.resize(ns);
        const int16_t* s = (const int16_t*)buf.data();
        for (int i = 0; i < ns; ++i) x[i] = s[i] * (1.f / 32768.f);
        guardarVoz(x.data(), x.size());
    }
}

void Dsd::guardarVoz(const float* x, size_t n)
{
    std::lock_guard<std::mutex> lk(vozMutex_);
    caudaVoz_.insert(caudaVoz_.end(), x, x + n);
    const double passo = std::clamp(taxaVoz_, 4000, 96000) / (double)taxaSaida_.load();   // na taxa do som
    size_t usados = 0;
    while (true) {
        const size_t i0 = (size_t)posVoz_;
        if (i0 + 1 >= caudaVoz_.size()) break;
        const float fr = (float)(posVoz_ - i0);
        float v = caudaVoz_[i0] + fr * (caudaVoz_[i0 + 1] - caudaVoz_[i0]);
        v = std::clamp(v * 2.5f, -1.f, 1.f);          // a voz do dsd-fme sai baixa (igual ao RXSDR)
        voz_.push_back((int16_t)std::lround(v * 32767.f));
        posVoz_ += passo;
    }
    usados = std::min((size_t)posVoz_, caudaVoz_.size() - 1);
    caudaVoz_.erase(caudaVoz_.begin(), caudaVoz_.begin() + usados);
    posVoz_ -= usados;
    while (voz_.size() > taxaSaida_.load()) voz_.pop_front();     // mais de 1 s atrasado: descarta o velho
}

void Dsd::puxarVoz(int16_t* out, size_t n)
{
    std::lock_guard<std::mutex> lk(vozMutex_);
    // Espera juntar 150 ms antes de comecar a tocar: a voz chega aos
    // quadros (20-60 ms) e sem essa folga picotaria.
    if (!tocando_ && voz_.size() >= taxaSaida_.load() * 3 / 20) tocando_ = true;
    size_t i = 0;
    if (tocando_) {
        for (; i < n && !voz_.empty(); ++i) { out[i] = voz_.front(); voz_.pop_front(); }
        if (voz_.empty()) tocando_ = false;
    }
    for (; i < n; ++i) out[i] = 0;
}

// --- texto do dsd-fme ------------------------------------------------------------
void Dsd::lerTexto()
{
    std::string acc;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile((HANDLE)saidaR_, buf, sizeof buf, &n, nullptr) && n > 0) {
        acc.append(buf, n);
        size_t ini = 0;
        for (size_t i = 0; i < acc.size(); ++i) {
            if (acc[i] == '\r' || acc[i] == '\n') {       // o dsd-fme reescreve a linha com \r
                if (i > ini) tratarLinha(acc.substr(ini, i - ini));
                ini = i + 1;
            }
        }
        acc.erase(0, ini);
        if (acc.size() > 8192) acc.clear();
    }
    if (vivo_) {
        vivo_ = false;
        std::lock_guard<std::mutex> lk(estMutex_);
        est_.rodando = false;
        if (est_.erro.empty()) est_.erro = T("o dsd-fme fechou sozinho");
    }
}

// O std::regex do MSVC LANCA excecao em linha comprida (error_stack) - e
// excecao solta numa thread derruba o programa (0xc0000409). Foi o que
// fechou o RXSDR com o TETRA; aqui fica protegido do mesmo jeito.
void Dsd::tratarLinha(std::string l)
{
    if (l.size() > 600) l.resize(600);
    try { tratarLinhaSegura(l); } catch (...) {}
}

void Dsd::tratarLinhaSegura(std::string l)
{
    // tira as cores (ANSI) antes de ler - igual a pagina
    static const std::regex ansi("\x1b\\[[0-9;]*[A-Za-z]");
    l = std::regex_replace(l, ansi, "");
    while (!l.empty() && (l.back() == ' ' || l.back() == '\t')) l.pop_back();
    size_t p = l.find_first_not_of(" \t");
    if (p == std::string::npos) return;
    l.erase(0, p);

    static const std::regex reCC("Color\\s*Code\\s*=\\s*(\\d+)", std::regex::icase);
    static const std::regex reProto("Sync:\\s*[+\\-]?\\s*(P25p[12]|X2-TDMA|NXDN\\d*|dPMR|DSTAR|YSF|M17|EDACS(?:\\s*EA)?|ProVoice|DMR)",
                                    std::regex::icase);
    static const std::regex reSrc("\\b(?:SRC|RID|Source)\\s*[=:]?\\s*(\\d+)", std::regex::icase);
    static const std::regex reTgt("\\b(?:TGT|TG|Target)\\s*[=:]?\\s*(\\d+)", std::regex::icase);
    static const std::regex reFec("FEC\\s*ERRS?\\s*[=:]?\\s*(\\d+)", std::regex::icase);
    static const std::regex reVoz("\\bVC[\\d*]");
    static const std::regex reMsg("\\b(SRC|TGT)\\s*=|Source:|Target:|Data\\s*Header|\\(P_", std::regex::icase);
    static const std::regex reSyncErr("Sync:|FEC\\s*ERR|CRC\\s*ERR", std::regex::icase);
    std::smatch mm;

    std::string baixo = l;
    for (auto& c : baixo) c = (char)std::tolower((unsigned char)c);
    static const std::regex reXX("Color\\s*Code\\s*=\\s*XX", std::regex::icase);
    const bool semValidacao = std::regex_search(l, reXX);
    int slot = 0;
    if (baixo.find("slot 1") != std::string::npos || baixo.find("slot1") != std::string::npos) slot = 1;
    else if (baixo.find("slot 2") != std::string::npos || baixo.find("slot2") != std::string::npos) slot = 2;
    int slotVoz = 0;
    if (l.find("[SLOT1]") != std::string::npos) slotVoz = 1;
    else if (l.find("[SLOT2]") != std::string::npos) slotVoz = 2;
    const bool quadroVoz = !semValidacao && (slotVoz > 0 || std::regex_search(l, reVoz));

    std::string msg;
    {
        std::lock_guard<std::mutex> lk(estMutex_);
        est_.linhas++;
        const double t = agora();
        if (std::regex_search(l, mm, reCC)) est_.cc = std::stoi(mm[1]);
        if (!semValidacao && std::regex_search(l, mm, reProto)) {
            std::string pr = mm[1];
            for (auto& c : pr) c = (char)std::toupper((unsigned char)c);
            if (pr == "DSTAR") pr = "D-STAR";
            est_.protocolo = pr;
        }
        if (slotVoz) ultimoSlot_ = slotVoz;
        else if (slot) ultimoSlot_ = slot;
        if (slot) {
            SlotDmr& s = est_.ts[slot - 1];
            s.ultAtivo = t;
            if (std::regex_search(l, mm, reSrc)) s.src = mm[1];
            if (std::regex_search(l, mm, reTgt)) s.tgt = mm[1];
            if (baixo.find("group") != std::string::npos) s.tipo = T("Grupo");
            else if (baixo.find("priv") != std::string::npos) s.tipo = T("Privado");
            else if (baixo.find("data") != std::string::npos) s.tipo = T("Dados");
            if (!quadroVoz && !semValidacao && baixo.find("data") != std::string::npos) s.dados++;
            if (std::regex_search(l, mm, reFec)) s.fec = std::stoi(mm[1]);
        }
        const int sv = slotVoz ? slotVoz : slot ? slot : ultimoSlot_;
        if (sv > 0 && quadroVoz) {
            SlotDmr& s = est_.ts[sv - 1];
            s.voz++; s.ultVoz = t; s.ultAtivo = t;
            if (s.tipo == "-") s.tipo = T("Voz");
        }
        if (!std::regex_search(l, reSyncErr) && std::regex_search(l, reMsg) && l != ultimaMsg_) {
            ultimaMsg_ = l;
            msg = "[" + horaUtc() + "] " + l + "\n";
        }
        if (linhasCruas && l != ultimaCrua_) {
            ultimaCrua_ = l;
            if (msg.empty()) msg = "  " + l + "\n";
        }
    }
    if (!msg.empty() && aoTexto) aoTexto(msg);
}

EstadoDmr Dsd::estado()
{
    std::lock_guard<std::mutex> lk(estMutex_);
    return est_;
}

void Dsd::limparContadores()
{
    std::lock_guard<std::mutex> lk(estMutex_);
    const bool r = est_.rodando;
    const std::string e = est_.erro;
    est_ = EstadoDmr();
    est_.rodando = r; est_.erro = e;
}

} // namespace masdr
