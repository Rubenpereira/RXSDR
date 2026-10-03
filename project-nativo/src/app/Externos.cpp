#include "Externos.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

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

std::string utf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring largo(const std::string& s)
{
    std::wstring w;
    for (char c : s) w += (wchar_t)(unsigned char)c;
    return w;
}

std::string horaUtc()
{
    SYSTEMTIME st; GetSystemTime(&st);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return b;
}

bool existe(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Porta TCP livre em 127.0.0.1 (a partir de "desejada"; 0 = qualquer uma)
int portaLivre(int desejada)
{
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    for (int tent = 0; tent < 40; ++tent) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return desejada;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((u_short)(desejada ? desejada + tent : 0));
        int alen = sizeof a;
        if (bind(s, (sockaddr*)&a, sizeof a) == 0 && getsockname(s, (sockaddr*)&a, &alen) == 0) {
            const int p = ntohs(a.sin_port);
            closesocket(s);
            return p;
        }
        closesocket(s);
        if (!desejada) break;
    }
    return desejada;
}

// JSON de uma linha so (o AIS-catcher escreve assim): pega um numero ou texto
bool numJson(const std::string& l, const char* chave, double& v)
{
    const std::string k = std::string("\"") + chave + "\":";
    const auto p = l.find(k);
    if (p == std::string::npos) return false;
    const char* c = l.c_str() + p + k.size();
    while (*c == ' ') ++c;
    if (*c == '"' || *c == '{' || *c == '[') return false;
    char* fim = nullptr;
    v = std::strtod(c, &fim);
    return fim != c;
}

bool txtJson(const std::string& l, const char* chave, std::string& v)
{
    const std::string k = std::string("\"") + chave + "\":\"";
    const auto p = l.find(k);
    if (p == std::string::npos) return false;
    const auto f = l.find('"', p + k.size());
    if (f == std::string::npos) return false;
    v = l.substr(p + k.size(), f - p - k.size());
    while (!v.empty() && (v.back() == ' ' || v.back() == '@')) v.pop_back();
    return true;
}

// --- FlightAware (o mesmo FaLink da pagina) ---------------------------------
bool ehLetra(char c) { return c >= 'A' && c <= 'Z'; }
bool ehDig(char c) { return c >= '0' && c <= '9'; }

// "TC0235" -> "TC235", "TAM0803" -> "TAM803" (o FlightAware nao acha com os zeros)
std::string normVoo(std::string v)
{
    for (auto& c : v) c = (char)std::toupper((unsigned char)c);
    auto resto = [](const std::string& r, std::string& out) {
        size_t i = 0;
        while (i < r.size() && r[i] == '0') ++i;
        size_t j = i;
        while (j < r.size() && ehDig(r[j])) ++j;
        const size_t nd = j - i;
        if (nd < 1 || nd > 4) return false;
        if (j < r.size()) { if (j + 1 != r.size() || !ehLetra(r[j])) return false; }
        out = r.substr(i);
        return true;
    };
    std::string r;
    if (v.size() > 2 && ((ehLetra(v[0]) && (ehLetra(v[1]) || ehDig(v[1]))) || (ehDig(v[0]) && ehLetra(v[1]))) &&
        resto(v.substr(2), r))
        return v.substr(0, 2) + r;
    if (v.size() > 3 && ehLetra(v[0]) && ehLetra(v[1]) && ehLetra(v[2]) && resto(v.substr(3), r))
        return v.substr(0, 3) + r;
    return v;
}

std::string soAlnum(const std::string& s)
{
    std::string o;
    for (char c : s) { c = (char)std::toupper((unsigned char)c); if (ehLetra(c) || ehDig(c)) o += c; }
    return o;
}

std::string urlFa(const std::string& ident, const std::string& hex)
{
    std::string h = hex;
    for (auto& c : h) c = (char)std::tolower((unsigned char)c);
    return h.empty() ? "https://flightaware.com/live/flight/" + ident
                     : "https://flightaware.com/live/modes/" + h + "/ident/" + ident + "/redirect";
}

// Primeiro "token" depois de um rotulo ("Reg: .PR-TYN" -> "PR-TYN"), so no
// comeco de palavra, com 3..maxLen caracteres [A-Z0-9] (+ os de "extra").
std::string tokenApos(const std::string& s, const std::string& rot, bool pulaPontos, const char* extra, size_t maxLen)
{
    size_t p = 0;
    while ((p = s.find(rot, p)) != std::string::npos) {
        if (p > 0 && std::isalnum((unsigned char)s[p - 1])) { p += rot.size(); continue; }
        size_t i = p + rot.size();
        while (i < s.size() && s[i] == ' ') ++i;
        if (pulaPontos) while (i < s.size() && s[i] == '.') ++i;
        size_t j = i;
        while (j < s.size() && (ehLetra(s[j]) || ehDig(s[j]) || std::strchr(extra, s[j]))) ++j;
        if (j - i >= 3 && j - i <= maxLen) return s.substr(i, j - i);
        p = j;
    }
    return {};
}

bool numApos(const std::string& s, const std::string& rot, double& v)
{
    size_t p = 0;
    while ((p = s.find(rot, p)) != std::string::npos) {
        if (p > 0 && std::isalnum((unsigned char)s[p - 1])) { p += rot.size(); continue; }
        const char* c = s.c_str() + p + rot.size();
        char* fim = nullptr;
        v = std::strtod(c, &fim);
        if (fim != c) return true;
        p += rot.size();
    }
    return false;
}

} // namespace

std::string AeronaveHfdl::urlVoo() const { return urlFa(voo, hex); }
std::string AeronaveHfdl::urlPrefixo() const { return urlFa(!hex.empty() && !voo.empty() ? voo : soAlnum(prefixo), hex); }

// ===========================================================================
//  Externo
// ===========================================================================
bool Externo::iniciar(const std::wstring& exe, const std::wstring& args, const std::wstring& pasta,
                      const std::wstring& pipeNomeado, std::string& erro)
{
    parar();
    if (!existe(exe)) { erro = "nao achei " + utf8(exe); return false; }

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 1 << 16)) { erro = "CreatePipe falhou"; return false; }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    usaPipe_ = !pipeNomeado.empty();
    if (usaPipe_) {
        nomePipe_ = L"\\\\.\\pipe\\" + pipeNomeado;
        HANDLE p = CreateNamedPipeW(nomePipe_.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT,
                                    1, 1 << 20, 0, 0, nullptr);
        if (p == INVALID_HANDLE_VALUE) {
            CloseHandle(outR); CloseHandle(outW);
            erro = "nao consegui criar o cano " + utf8(nomePipe_);
            return false;
        }
        entrada_ = p;
        inR = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    } else {
        if (!CreatePipe(&inR, &inW, &sa, 1 << 20)) {
            CloseHandle(outR); CloseHandle(outW);
            erro = "CreatePipe falhou"; return false;
        }
        SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
        entrada_ = inW;
    }

    HANDLE binR = nullptr, binW = nullptr;
    if (aoBinario) {
        if (!CreatePipe(&binR, &binW, &sa, 1 << 18)) {
            CloseHandle(outR); CloseHandle(outW);
            if (inR && inR != INVALID_HANDLE_VALUE) CloseHandle(inR);
            CloseHandle((HANDLE)entrada_); entrada_ = nullptr;
            erro = "CreatePipe falhou"; return false;
        }
        SetHandleInformation(binR, HANDLE_FLAG_INHERIT, 0);
    }

    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR; si.hStdOutput = binW ? binW : outW; si.hStdError = outW;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> linha(cmd.begin(), cmd.end()); linha.push_back(0);
    const BOOL ok = CreateProcessW(nullptr, linha.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   pasta.c_str(), &si, &pi);
    if (inR && inR != INVALID_HANDLE_VALUE) CloseHandle(inR);
    CloseHandle(outW);
    if (binW) CloseHandle(binW);
    if (!ok) {
        const DWORD e = GetLastError();
        CloseHandle((HANDLE)entrada_); entrada_ = nullptr;
        CloseHandle(outR);
        if (binR) CloseHandle(binR);
        erro = "nao consegui abrir " + utf8(exe) + " (erro " + std::to_string(e) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    proc_ = pi.hProcess;
    saidaR_ = outR;
    {
        std::lock_guard<std::mutex> lk(filaMutex_);
        fila_.clear(); sair_ = false;
    }
    descartados_ = 0;
    vivo_ = true;
    thLer_ = std::thread([this] { lerSaida(); });
    thEsc_ = std::thread([this] { lacoEscrita(); });
    if (binR) {
        binR_ = binR;
        thBin_ = std::thread([this] {
            std::vector<char> buf(1 << 16);
            for (;;) {
                DWORD n = 0;
                if (!ReadFile((HANDLE)binR_, buf.data(), (DWORD)buf.size(), &n, nullptr) || n == 0) break;
                if (aoBinario) aoBinario(buf.data(), n);
            }
        });
    }
    return true;
}

void Externo::parar()
{
    if (!proc_ && !thLer_.joinable() && !thEsc_.joinable() && !thBin_.joinable()) return;
    vivo_ = false;
    {
        std::lock_guard<std::mutex> lk(filaMutex_);
        sair_ = true;
    }
    filaCv_.notify_all();
    if (proc_) {
        TerminateProcess((HANDLE)proc_, 0);
        WaitForSingleObject((HANDLE)proc_, 2000);
    }
    // quem espera o programa abrir o cano nomeado: abre-se o cano daqui
    // mesmo, so para soltar o ConnectNamedPipe
    if (usaPipe_ && thEsc_.joinable()) {
        HANDLE c = CreateFileW(nomePipe_.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (c != INVALID_HANDLE_VALUE) CloseHandle(c);
    }
    if (thEsc_.joinable()) thEsc_.join();
    if (entrada_) { CloseHandle((HANDLE)entrada_); entrada_ = nullptr; }
    if (thLer_.joinable()) thLer_.join();
    if (thBin_.joinable()) thBin_.join();
    if (binR_) { CloseHandle((HANDLE)binR_); binR_ = nullptr; }
    if (saidaR_) { CloseHandle((HANDLE)saidaR_); saidaR_ = nullptr; }
    if (proc_) { CloseHandle((HANDLE)proc_); proc_ = nullptr; }
}

void Externo::escrever(const void* p, size_t n)
{
    if (!vivo_ || !p || !n) return;
    {
        std::lock_guard<std::mutex> lk(filaMutex_);
        if (fila_.size() + n > teto) {
            // o programa nao esta acompanhando: joga fora o mais VELHO
            const size_t sobra = std::min(fila_.size(), fila_.size() + n - teto);
            fila_.erase(fila_.begin(), fila_.begin() + sobra);
            descartados_ += sobra;
        }
        const char* c = (const char*)p;
        fila_.insert(fila_.end(), c, c + n);
    }
    filaCv_.notify_one();
}

void Externo::lacoEscrita()
{
    if (usaPipe_) {
        if (!ConnectNamedPipe((HANDLE)entrada_, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            vivo_ = false;
            return;
        }
    }
    std::vector<char> lote;
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(filaMutex_);
            filaCv_.wait(lk, [this] { return sair_ || !fila_.empty(); });
            if (sair_) return;
            lote.swap(fila_);
        }
        const char* p = lote.data();
        size_t resta = lote.size();
        while (resta > 0) {
            DWORD w = 0;
            if (!WriteFile((HANDLE)entrada_, p, (DWORD)std::min<size_t>(resta, 1 << 20), &w, nullptr)) {
                vivo_ = false;
                return;
            }
            p += w; resta -= w;
        }
        lote.clear();
    }
}

void Externo::lerSaida()
{
    char buf[8192];
    std::string resto;
    for (;;) {
        DWORD n = 0;
        if (!ReadFile((HANDLE)saidaR_, buf, sizeof buf, &n, nullptr) || n == 0) break;
        resto.append(buf, n);
        size_t ini = 0;
        for (;;) {
            const size_t f = resto.find('\n', ini);
            if (f == std::string::npos) break;
            std::string l = resto.substr(ini, f - ini);
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
            if (aoLinha) aoLinha(l);
            ini = f + 1;
        }
        resto.erase(0, ini);
        if (resto.size() > 65536) { if (aoLinha) aoLinha(resto); resto.clear(); }
    }
    if (!resto.empty() && aoLinha) aoLinha(resto);
    vivo_ = false;
}

// ===========================================================================
//  HFDL (dumphfdl)
// ===========================================================================
bool Hfdl::iniciar(const std::vector<double>& canaisKHz, double centroHz, uint32_t taxa, std::string& erro)
{
    parar();
    {
        std::lock_guard<std::mutex> lk(m_);
        est_ = EstadoExterno();
        emMsg_ = false;
    }
    if (canaisKHz.empty() || taxa == 0) { erro = "faltou a banda ou a taxa"; return false; }
    const std::wstring pasta = pastaDecoders();
    const std::wstring exe = pasta + L"\\dumphfdl.exe";
    wchar_t b[64];
    std::wstring args = L"--iq-file - --sample-format CF32 --sample-rate ";
    args += std::to_wstring(taxa);
    std::swprintf(b, 64, L" --centerfreq %.3f --utc", centroHz / 1000.0);
    args += b;
    // A tabela de estacoes da o NOME de quem transmite (senao so o numero)
    if (existe(pasta + L"\\hfdl_systable.conf")) args += L" --system-table \"" + pasta + L"\\hfdl_systable.conf\"";
    for (double c : canaisKHz) { std::swprintf(b, 64, L" %.1f", c); args += b; }
    centroHz_ = centroHz; taxa_ = taxa; fora_ = false;
    ext_.teto = 64u << 20;          // ~8 s de 1 Msps em CF32
    ext_.aoLinha = [this](const std::string& l) { linha(l); };
    if (!ext_.iniciar(exe, args, pasta, L"", erro)) {
        std::lock_guard<std::mutex> lk(m_); est_.erro = erro;
        return false;
    }
    char t[160];
    std::snprintf(t, sizeof t, "[HFDL] dumphfdl iniciado - %d canais, centro %.3f MHz, %.3f Msps\n",
                  (int)canaisKHz.size(), centroHz / 1e6, taxa / 1e6);
    if (aoTexto) aoTexto(t);
    // Diagnostico: decoders\hfdl_teste.txt (saida gravada do dumphfdl) e lido como se tivesse chegado agora
    std::ifstream teste(pasta + L"\\hfdl_teste.txt");
    for (std::string l; teste && std::getline(teste, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        linha(l);
    }
    return true;
}

void Hfdl::parar() { ext_.parar(); }

void Hfdl::alimentarIQCru(const std::complex<float>* iq, size_t n, uint32_t sps, uint64_t centro)
{
    if (!ext_.vivo() || !iq || !n) return;
    // O dumphfdl conta os canais a partir do centro e da taxa combinados no
    // inicio: com o dongle em outro lugar, o que ele decodificasse estaria errado.
    if (sps != taxa_ || std::fabs((double)centro - centroHz_) > 1.0) { fora_ = true; return; }
    fora_ = false;
    // Diagnostico: com decoders\hfdl_dump.on, grava 30 s do que vai para o dumphfdl
    static FILE* dump = nullptr; static bool testado = false; static size_t gravadas = 0;
    if (!testado) {
        testado = true;
        if (existe(pastaDecoders() + L"\\hfdl_dump.on")) dump = _wfopen((pastaDecoders() + L"\\hfdl_iq.cf32").c_str(), L"wb");
    }
    if (dump && gravadas < (size_t)taxa_ * 30) { fwrite(iq, 8, n, dump); gravadas += n; if (gravadas >= (size_t)taxa_ * 30) { fclose(dump); dump = nullptr; } }
    ext_.escrever(iq, n * sizeof(std::complex<float>));
}

void Hfdl::linha(const std::string& l)
{
    // O dumphfdl escreve cada mensagem em varias linhas, separadas por uma em branco
    if (l.find_first_not_of(" \t") == std::string::npos) {
        if (emMsg_ && aoTexto) aoTexto("\n");
        emMsg_ = false;
        processarBloco();
        return;
    }
    if (l.rfind("[20", 0) == 0 && l.find(" kHz]") != std::string::npos) processarBloco();   // comecou outra
    bloco_ += l;
    bloco_ += '\n';
    // cabecalho de cada mensagem: "[2026-09-29 18:37:25 GMT] [13312.0 kHz] [8.3 Hz] ..."
    if (l.rfind("[20", 0) == 0 && l.find(" kHz]") != std::string::npos) {
        std::lock_guard<std::mutex> lk(m_);
        ++est_.mensagens;
        emMsg_ = true;
    }
    if (aoTexto) aoTexto(l + "\n");
}

// Uma mensagem inteira: tira voo, prefixo, codigo ICAO e posicao
void Hfdl::processarBloco()
{
    if (bloco_.empty()) return;
    std::string b;
    b.swap(bloco_);
    std::string hex = tokenApos(b, "ICAO:", false, "", 6);
    if (hex.size() != 6) hex.clear();
    if (hex.empty()) {                                  // "AC: 221 (39CF01)"
        const auto p = b.find("AC: ");
        if (p != std::string::npos) {
            const auto a = b.find('(', p), f = b.find(')', p);
            if (a != std::string::npos && f == a + 7 && b.find('\n', p) > a) hex = b.substr(a + 1, 6);
        }
    }
    for (char c : hex) if (!std::isxdigit((unsigned char)c)) { hex.clear(); break; }
    std::string voo = tokenApos(b, "Flight ID:", false, "", 8);
    if (voo.empty()) voo = tokenApos(b, "Flight:", false, "", 8);
    if (!voo.empty()) voo = normVoo(voo);
    const std::string reg = tokenApos(b, "Reg:", true, "-", 8);
    if (voo.empty() && reg.empty() && hex.empty()) return;
    double la = 91, lo = 181, fk = 0;
    const bool pos = numApos(b, "Lat:", la) && numApos(b, "Lon:", lo) && std::fabs(la) <= 90 && std::fabs(lo) <= 180;
    {
        const auto k = b.find(" kHz]");
        const auto a = b.rfind('[', k);
        if (k != std::string::npos && a != std::string::npos) fk = std::atof(b.c_str() + a + 1);
    }
    std::lock_guard<std::mutex> lk(m_);
    // o mesmo aviao costuma ter mandado antes um "Logon" com o codigo ICAO
    if (!hex.empty()) {
        if (!voo.empty()) hexConhecido_["V" + voo] = hex;
        if (!reg.empty()) hexConhecido_["R" + reg] = hex;
    } else {
        if (!voo.empty() && hexConhecido_.count("V" + voo)) hex = hexConhecido_["V" + voo];
        else if (!reg.empty() && hexConhecido_.count("R" + reg)) hex = hexConhecido_["R" + reg];
    }
    // acha o registro do aviao por codigo, voo ou prefixo
    std::string chave;
    for (auto& [k, a] : avioes_)
        if ((!hex.empty() && a.hex == hex) || (!voo.empty() && a.voo == voo) || (!reg.empty() && a.prefixo == reg)) { chave = k; break; }
    if (chave.empty()) chave = !hex.empty() ? hex : !voo.empty() ? voo : reg;
    AeronaveHfdl& a = avioes_[chave];
    if (!hex.empty()) a.hex = hex;
    if (!voo.empty()) a.voo = voo;
    if (!reg.empty()) a.prefixo = reg;
    if (pos) { a.lat = la; a.lon = lo; }
    if (fk > 0) a.freqKHz = fk;
    ++a.msgs;
    a.visto = agora();
    if (avioes_.size() > 300) {                         // esquece o mais antigo
        auto velho = avioes_.begin();
        for (auto it = avioes_.begin(); it != avioes_.end(); ++it) if (it->second.visto < velho->second.visto) velho = it;
        avioes_.erase(velho);
    }
}

std::vector<AeronaveHfdl> Hfdl::aeronaves()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<AeronaveHfdl> v;
    for (const auto& [k, a] : avioes_) v.push_back(a);
    std::sort(v.begin(), v.end(), [](const AeronaveHfdl& x, const AeronaveHfdl& y) { return x.visto > y.visto; });
    return v;
}

EstadoExterno Hfdl::estado()
{
    std::lock_guard<std::mutex> lk(m_);
    EstadoExterno e = est_;
    e.rodando = ext_.vivo();
    if (!e.rodando && e.erro.empty() && e.mensagens >= 0) e.erro = "";
    return e;
}

// ===========================================================================
//  AIS (AIS-catcher)
// ===========================================================================
static constexpr uint32_t kAisTaxa = 192000;

bool Ais::iniciar(std::string& erro)
{
    parar();
    {
        std::lock_guard<std::mutex> lk(m_);
        est_ = EstadoExterno();
        navios_.clear();
    }
    sps_ = 0; pos_ = 0; cauda_.clear();
    const std::wstring pasta = pastaDecoders() + L"\\ais";
    const std::wstring exe = pasta + L"\\AIS-catcher.exe";
    portaWeb_ = portaLivre(8100);
    const std::wstring nome = L"rxsdr_ais_" + std::to_wstring(GetCurrentProcessId());
    // -m 2  modelo que trabalha com IQ     -o 4  JSON (ja decodificado)
    // -N    mapa no navegador              -X off nao manda para o aiscatcher.org
    std::wstring args = L"-m 2 -r CS16 \\\\.\\pipe\\" + nome + L" -s " + std::to_wstring(kAisTaxa) +
                        L" -o 4 -N " + std::to_wstring(portaWeb_) + L" -X off";
    ext_.teto = 8u << 20;
    ext_.aoLinha = [this](const std::string& l) { linha(l); };
    if (!ext_.iniciar(exe, args, pasta, nome, erro)) {
        std::lock_guard<std::mutex> lk(m_); est_.erro = erro;
        return false;
    }
    if (aoTexto) {
        char t[200];
        std::snprintf(t, sizeof t, "[AIS] AIS-catcher iniciado - canais 161,975 e 162,025 MHz. Mapa: http://127.0.0.1:%d\n",
                      portaWeb_);
        aoTexto(t);
    }
    return true;
}

void Ais::parar() { ext_.parar(); }

void Ais::alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps)
{
    if (!ext_.vivo() || !iq || !n || sps < kAisTaxa) return;
    if (sps != sps_) {
        sps_ = sps;
        M_ = std::max<size_t>(1, (size_t)std::llround((double)sps / kAisTaxa));
        anel_.assign(M_, {0, 0}); soma_ = {0, 0}; idx_ = 0;
        // dois passa-baixas de 1 polo em ~60 kHz: os canais AIS estao a +-25 kHz
        a_ = (float)(1.0 - std::exp(-2.0 * 3.14159265358979 * 60000.0 / sps));
        lp1_ = lp2_ = {0, 0};
        cauda_.clear(); pos_ = 0;
    }
    const double invM = 1.0 / (double)M_;
    const size_t base = cauda_.size();
    cauda_.resize(base + n);
    for (size_t i = 0; i < n; ++i) {
        lp1_ += a_ * (iq[i] - lp1_);
        lp2_ += a_ * (lp1_ - lp2_);
        soma_ += std::complex<double>(lp2_.real(), lp2_.imag()) -
                 std::complex<double>(anel_[idx_].real(), anel_[idx_].imag());
        anel_[idx_] = lp2_;
        if (++idx_ >= M_) idx_ = 0;
        cauda_[base + i] = std::complex<float>((float)(soma_.real() * invM), (float)(soma_.imag() * invM));
    }
    const double passo = (double)sps / kAisTaxa;
    std::vector<int16_t> out;
    out.reserve((size_t)(n / passo) * 2 + 8);
    while (pos_ + 1.0 < (double)cauda_.size()) {
        const size_t i0 = (size_t)pos_;
        const float fr = (float)(pos_ - (double)i0);
        const std::complex<float> v = cauda_[i0] + (cauda_[i0 + 1] - cauda_[i0]) * fr;
        out.push_back((int16_t)std::lround(std::clamp(v.real(), -1.f, 1.f) * 32767.f));
        out.push_back((int16_t)std::lround(std::clamp(v.imag(), -1.f, 1.f) * 32767.f));
        pos_ += passo;
    }
    const size_t usados = std::min((size_t)pos_, cauda_.size() - 1);
    cauda_.erase(cauda_.begin(), cauda_.begin() + usados);
    pos_ -= (double)usados;
    if (!out.empty()) ext_.escrever(out.data(), out.size() * sizeof(int16_t));
}

void Ais::linha(const std::string& l)
{
    if (l.empty()) return;
    if (l[0] != '{') {
        // diagnostico do AIS-catcher (poucas linhas, quase so na abertura).
        // "FILE: timeout." so diz que o IQ demorou um pouco (radio parado,
        // troca de taxa) - repetido, so polui.
        if (l.find("timeout") != std::string::npos) return;
        if (aoTexto) aoTexto("[AIS] " + l + "\n");
        return;
    }
    double v = 0;
    if (!numJson(l, "mmsi", v)) return;
    const long long mmsi = (long long)v;
    int tipo = 0;
    if (numJson(l, "type", v) || numJson(l, "msgtype", v)) tipo = (int)v;
    std::string linhaTxt;
    {
        std::lock_guard<std::mutex> lk(m_);
        ++est_.mensagens;
        NavioAis& n = navios_[mmsi];
        n.mmsi = mmsi;
        n.tipo = tipo;
        ++n.msgs;
        n.visto = agora();
        std::string s;
        if (txtJson(l, "shipname", s) && !s.empty()) n.nome = s;
        if (txtJson(l, "callsign", s) && !s.empty()) n.indicativo = s;
        if (txtJson(l, "destination", s) && !s.empty()) n.destino = s;
        double la, lo;
        if (numJson(l, "lat", la) && numJson(l, "lon", lo) && std::fabs(la) <= 90 && std::fabs(lo) <= 180) {
            n.lat = la; n.lon = lo;
        }
        if (numJson(l, "speed", v) && v < 102.2) n.vel = v;
        if (numJson(l, "course", v) && v < 360) n.rumo = v;
        char b[320];
        int k = std::snprintf(b, sizeof b, "[AIS] %s  MMSI %lld", horaUtc().c_str(), mmsi);
        if (tipo > 0) k += std::snprintf(b + k, sizeof b - k, "  msg %d", tipo);
        if (!n.nome.empty()) k += std::snprintf(b + k, sizeof b - k, "  %s", n.nome.c_str());
        if (!n.indicativo.empty()) k += std::snprintf(b + k, sizeof b - k, " (%s)", n.indicativo.c_str());
        if (n.lat <= 90) k += std::snprintf(b + k, sizeof b - k, "  %.5f %.5f", n.lat, n.lon);
        if (n.vel >= 0) k += std::snprintf(b + k, sizeof b - k, "  %.1f nós", n.vel);
        if (n.rumo >= 0) k += std::snprintf(b + k, sizeof b - k, "  rumo %.0f°", n.rumo);
        if (!n.destino.empty()) k += std::snprintf(b + k, sizeof b - k, "  -> %s", n.destino.c_str());
        linhaTxt = std::string(b) + "\n";
    }
    if (aoTexto) aoTexto(linhaTxt);
}

EstadoExterno Ais::estado()
{
    std::lock_guard<std::mutex> lk(m_);
    EstadoExterno e = est_;
    e.rodando = ext_.vivo();
    char b[64];
    std::snprintf(b, sizeof b, "%d navios", (int)navios_.size());
    e.info = b;
    return e;
}

std::vector<NavioAis> Ais::navios()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<NavioAis> v;
    for (const auto& [k, n] : navios_) v.push_back(n);
    std::sort(v.begin(), v.end(), [](const NavioAis& a, const NavioAis& b) { return a.visto > b.visto; });
    return v;
}

// ===========================================================================
//  APRS (direwolf)
// ===========================================================================
bool Aprs::iniciar(int baud, std::string& erro)
{
    parar();
    {
        std::lock_guard<std::mutex> lk(m_);
        est_ = EstadoExterno();
    }
    agwPronto_ = false;
    const std::wstring pasta = pastaDecoders();
    const std::wstring exe = pasta + L"\\direwolf.exe";
    // A configuracao vai para a pasta temporaria (a do programa pode ser so leitura)
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    conf_ = std::wstring(tmp) + L"rxsdr_direwolf_" + std::to_wstring(GetCurrentProcessId()) + L".conf";   // um por copia aberta
    portaAgw_ = portaLivre(8000);
    {
        FILE* f = _wfopen(conf_.c_str(), L"wb");
        if (!f) { erro = "nao consegui gravar " + utf8(conf_); return false; }
        std::fprintf(f, "# gerado pelo RXSDR Nativo\r\nADEVICE stdin null\r\nCHANNEL 0\r\n");
        if (baud == 300)
            // HF: varios decodificadores espalhados de 30 em 30 Hz (sintonia nunca e exata)
            std::fprintf(f, "MODEM 300 1600:1800 7@30 D\r\n");
        else
            std::fprintf(f, "MODEM 1200\r\n");
        std::fprintf(f, "AGWPORT %d\r\nKISSPORT 0\r\n", portaAgw_);
        std::fclose(f);
    }
    const std::wstring args = L"-c \"" + conf_ + L"\" -r 48000 -b 16 -t 0 -";
    ext_.teto = 4u << 20;
    ext_.aoLinha = [this](const std::string& l) { linha(l); };
    if (!ext_.iniciar(exe, args, pasta, L"", erro)) {
        std::lock_guard<std::mutex> lk(m_); est_.erro = erro;
        return false;
    }
    if (aoTexto) aoTexto(baud == 300 ? "[APRS] direwolf iniciado - HF, 300 baud (USB)\n"
                                     : "[APRS] direwolf iniciado - VHF, 1200 baud (FM)\n");
    agwVivo_ = true;
    thAgw_ = std::thread([this] { lacoAgw(); });
    return true;
}

void Aprs::parar()
{
    agwVivo_ = false;
    // so desliga o socket (o recv volta); quem fecha e a propria thread
    if (sockAgw_ != ~(uintptr_t)0) shutdown((SOCKET)sockAgw_, SD_BOTH);
    if (thAgw_.joinable()) thAgw_.join();
    ext_.parar();
}

void Aprs::escrever48k(const int16_t* pcm, size_t n)
{
    if (ext_.vivo() && pcm && n) ext_.escrever(pcm, n * 2);
}

void Aprs::linha(const std::string& l)
{
    // Com a porta AGW entregando na hora, o stdout (que chega atrasado, em
    // blocos) so repetiria os mesmos pacotes.
    if (agwPronto_ || l.empty()) return;
    if (aoTexto) aoTexto("[APRS] " + l + "\n");
}

// Cliente AGW: cabecalho de 36 bytes (tipo em [4], tamanho dos dados em [28..31]).
// Um cabecalho com tipo 'm' liga a monitoracao de tudo o que for recebido.
void Aprs::lacoAgw()
{
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = INVALID_SOCKET;
    for (int tent = 0; tent < 40 && agwVivo_; ++tent) {
        Sleep(500);
        if (!agwVivo_) break;
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((u_short)portaAgw_);
        if (connect(s, (sockaddr*)&a, sizeof a) == 0) break;
        closesocket(s); s = INVALID_SOCKET;
    }
    if (s == INVALID_SOCKET) {
        if (agwVivo_ && aoTexto) aoTexto("[APRS] a porta AGW do direwolf nao respondeu - os pacotes vem pelo texto (com atraso)\n");
        return;
    }
    sockAgw_ = (uintptr_t)s;
    char cab[36] = {0};
    cab[4] = 'm';
    send(s, cab, 36, 0);
    std::string buf;
    char tmp[4096];
    while (agwVivo_) {
        const int n = recv(s, tmp, sizeof tmp, 0);
        if (n <= 0) break;
        buf.append(tmp, n);
        while (buf.size() >= 36) {
            const unsigned char* p = (const unsigned char*)buf.data();
            const char tipo = (char)p[4];
            const uint32_t tam = p[28] | (p[29] << 8) | (p[30] << 16) | ((uint32_t)p[31] << 24);
            if (tam > (1u << 20)) { buf.clear(); break; }
            if (buf.size() < 36 + tam) break;
            std::string dados = buf.substr(36, tam);
            buf.erase(0, 36 + tam);
            if (tipo != 'U' && tipo != 'I' && tipo != 'S' && tipo != 'T') continue;
            agwPronto_ = true;
            for (auto& c : dados) if (c == '\r') c = '\n';
            std::string saida;
            size_t ini = 0;
            bool primeira = true;
            while (ini < dados.size()) {
                size_t f = dados.find('\n', ini);
                if (f == std::string::npos) f = dados.size();
                std::string l = dados.substr(ini, f - ini);
                ini = f + 1;
                const size_t a0 = l.find_first_not_of(" \t\0", 0, 3);
                if (a0 == std::string::npos) continue;
                l.erase(0, a0);
                saida += primeira ? "[APRS] " + horaUtc() + "  " + l + "\n" : "        " + l + "\n";
                primeira = false;
            }
            if (!saida.empty()) {
                { std::lock_guard<std::mutex> lk(m_); ++est_.mensagens; }
                registrar(dados);
                if (aoTexto) aoTexto(saida);
            }
        }
    }
    sockAgw_ = ~(uintptr_t)0;
    closesocket(s);
}

// " 1:Fm PU4TDC-15 To APNV01 Via PU1PZP-15*,WIDE2-2 <UI pid=F0 Len=62 >[17:55:28]"
// "!2040.68S/04337.12W#145.390MHz ..."
void Aprs::registrar(const std::string& dados)
{
    auto palavra = [&](const char* rot) {
        const auto p = dados.find(rot);
        if (p == std::string::npos) return std::string();
        const size_t i = p + std::strlen(rot);
        const size_t f = dados.find_first_of(" \n<", i);
        return dados.substr(i, (f == std::string::npos ? dados.size() : f) - i);
    };
    const std::string call = palavra("Fm ");
    if (call.size() < 3) return;
    EstacaoAprs e;
    e.indicativo = call;
    e.destino = palavra("To ");
    {
        const auto p = dados.find("Via ");
        const auto f = dados.find(" <", p == std::string::npos ? 0 : p);
        if (p != std::string::npos && f != std::string::npos && f > p) {
            std::string v = dados.substr(p + 4, f - p - 4);
            size_t i = 0;
            while (i <= v.size()) {
                size_t k = v.find(',', i);
                if (k == std::string::npos) k = v.size();
                std::string d = v.substr(i, k - i);
                while (!d.empty() && (d.back() == '*' || d.back() == ' ')) d.pop_back();
                if (!d.empty()) e.via.push_back(d);
                i = k + 1;
            }
        }
    }
    {   // a linha de informacao e a primeira depois do cabecalho
        const auto p = dados.find('\n');
        if (p != std::string::npos) {
            size_t i = p + 1;
            while (i < dados.size() && (dados[i] == '\n' || dados[i] == ' ')) ++i;
            const auto f = dados.find('\n', i);
            e.info = dados.substr(i, (f == std::string::npos ? dados.size() : f) - i);
        }
    }
    // posicao sem compressao: ddmm.mmN/dddmm.mmW (com qualquer simbolo no meio)
    const std::string& s = e.info;
    for (size_t i = 0; i + 19 <= s.size(); ++i) {
        auto dig = [&](size_t a, size_t n) { for (size_t k = a; k < a + n; ++k) if (!ehDig(s[k])) return false; return true; };
        if (dig(i, 4) && s[i + 4] == '.' && dig(i + 5, 2) && (s[i + 7] == 'N' || s[i + 7] == 'S') &&
            dig(i + 9, 5) && s[i + 14] == '.' && dig(i + 15, 2) && (s[i + 17] == 'E' || s[i + 17] == 'W')) {
            const double la = std::atoi(s.substr(i, 2).c_str()) + std::atof(s.substr(i + 2, 5).c_str()) / 60.0;
            const double lo = std::atoi(s.substr(i + 9, 3).c_str()) + std::atof(s.substr(i + 12, 5).c_str()) / 60.0;
            e.lat = s[i + 7] == 'S' ? -la : la;
            e.lon = s[i + 17] == 'W' ? -lo : lo;
            break;
        }
    }
    std::lock_guard<std::mutex> lk(m_);
    EstacaoAprs& x = estacoes_[call];
    const int n = x.pacotes;
    const double la = x.lat, lo = x.lon;
    x = e;
    x.pacotes = n + 1;
    if (x.lat > 90) { x.lat = la; x.lon = lo; }      // sem posicao neste pacote: fica a anterior
    x.visto = agora();
}

std::vector<EstacaoAprs> Aprs::estacoes()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<EstacaoAprs> v;
    for (const auto& [k, e] : estacoes_) v.push_back(e);
    std::sort(v.begin(), v.end(), [](const EstacaoAprs& a, const EstacaoAprs& b) { return a.visto > b.visto; });
    return v;
}

// o mesmo endereco da pagina: https://aprs.fi/?call=PU1XTB-9
std::string Aprs::urlAprsFi(const std::string& indicativo)
{
    std::string u = "https://aprs.fi/?call=";
    for (char c : indicativo) {
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') u += c;
        else { char h[4]; std::snprintf(h, sizeof h, "%%%02X", (unsigned char)c); u += h; }
    }
    return u;
}

EstadoExterno Aprs::estado()
{
    std::lock_guard<std::mutex> lk(m_);
    EstadoExterno e = est_;
    e.rodando = ext_.vivo();
    e.info = agwPronto_ ? "AGW em tempo real" : "esperando o primeiro pacote";
    return e;
}

// ===========================================================================
//  ACARS (acarsdec)
// ===========================================================================
static constexpr uint32_t kAcarsAudio = 12000;     // o acarsdec quer multiplo de 12 kHz

bool Acars::iniciar(const std::vector<double>& canaisHz, double centroHz, uint32_t taxa, std::string& erro)
{
    parar();
    {
        std::lock_guard<std::mutex> lk(m_);
        est_ = EstadoExterno();
        bloco_.clear();
    }
    if (canaisHz.empty() || taxa < 100000) { erro = "faltou o canal ou a taxa"; return false; }
    centroHz_ = centroHz; taxa_ = taxa; fora_ = false;
    freqs_ = canaisHz;
    canais_.assign(canaisHz.size(), Canal());
    M_ = std::max<size_t>(1, (size_t)std::llround((double)taxa / 48000.0));
    a_ = (float)(1.0 - std::exp(-2.0 * 3.14159265358979 * 7000.0 / taxa));   // ~7 kHz: o canal ACARS tem ~5 kHz
    for (size_t c = 0; c < canais_.size(); ++c) {
        Canal& k = canais_[c];
        k.desloc = canaisHz[c] - centroHz;
        const double w = -2.0 * 3.14159265358979 * k.desloc / taxa;
        k.passo = {std::cos(w), std::sin(w)};
        k.anel.assign(M_, {0, 0});
    }
    idx_ = 0; pos_ = 0; nco_ = 0;
    passoSaida_ = (double)taxa / kAcarsAudio;                     // ~85 amostras de entrada por saida
    const std::wstring pasta = pastaDecoders();
    const std::wstring exe = pasta + L"\\acarsdec.exe";
    const std::wstring args = L"--sndfile file=-,subtype=2,channels=" + std::to_wstring(canais_.size()) +
                              L" --output full:file:path=- -m 1";
    ext_.teto = 4u << 20;
    ext_.aoLinha = [this](const std::string& l) { linha(l); };
    if (!ext_.iniciar(exe, args, pasta, L"", erro)) {
        std::lock_guard<std::mutex> lk(m_); est_.erro = erro;
        return false;
    }
    if (aoTexto) {
        std::string t = "[ACARS] acarsdec iniciado - canais";
        char b[32];
        for (double f : canaisHz) { std::snprintf(b, sizeof b, " %.3f", f / 1e6); t += b; }
        t += " MHz, ao mesmo tempo\n";
        aoTexto(t);
    }
    return true;
}

void Acars::parar() { ext_.parar(); }

void Acars::alimentarIQCru(const std::complex<float>* iq, size_t n, uint32_t sps, uint64_t centro)
{
    if (!ext_.vivo() || !iq || !n) return;
    if (sps != taxa_ || std::fabs((double)centro - centroHz_) > 1.0) { fora_ = true; return; }
    fora_ = false;
    const size_t nc = canais_.size();
    const float invM = 1.f / (float)M_;
    std::vector<int16_t> out;
    out.reserve((size_t)(n / passoSaida_ + 4) * nc);
    std::vector<std::complex<float>> cur(nc);
    for (size_t i = 0; i < n; ++i) {
        const std::complex<double> x(iq[i].real(), iq[i].imag());
        for (size_t c = 0; c < nc; ++c) {
            Canal& k = canais_[c];
            const std::complex<double> y = x * k.nco;
            k.nco *= k.passo;
            const std::complex<float> v((float)y.real(), (float)y.imag());
            k.lp1 += a_ * (v - k.lp1);
            k.lp2 += a_ * (k.lp1 - k.lp2);
            k.soma += std::complex<double>(k.lp2.real(), k.lp2.imag()) -
                      std::complex<double>(k.anel[idx_].real(), k.anel[idx_].imag());
            k.anel[idx_] = k.lp2;
            cur[c] = std::complex<float>((float)k.soma.real(), (float)k.soma.imag()) * invM;
        }
        if (++idx_ >= M_) idx_ = 0;
        // amostras de saida (12 kHz) entre a anterior e esta, por interpolacao linear
        while (pos_ < 1.0) {
            for (size_t c = 0; c < nc; ++c) {
                Canal& k = canais_[c];
                const std::complex<float> z = k.ant + (cur[c] - k.ant) * (float)pos_;
                const float env = std::abs(z);                        // envelope AM
                k.dc += 0.002f * (env - k.dc);                        // nivel da portadora
                const float a = (env - k.dc) / (k.dc + 1e-7f) * 0.5f; // modulacao, independe do nivel
                out.push_back((int16_t)std::lround(std::clamp(a, -1.f, 1.f) * 30000.f));
            }
            pos_ += passoSaida_;                                       // em amostras de entrada
        }
        pos_ -= 1.0;
        for (size_t c = 0; c < nc; ++c) canais_[c].ant = cur[c];
    }
    // renormaliza os osciladores de vez em quando (erro de arredondamento)
    if (++nco_ >= 16) {
        nco_ = 0;
        for (auto& k : canais_) k.nco /= std::abs(k.nco);
    }
    if (!out.empty()) ext_.escrever(out.data(), out.size() * sizeof(int16_t));
}

void Acars::linha(const std::string& l)
{
    // cada mensagem comeca com "[#1 (F:131.550 L:-35.2/-47.0 E:0) 29/09/2026 18:40:12.123 ---..."
    if (l.rfind("[#", 0) == 0) {
        processarBloco();
        std::lock_guard<std::mutex> lk(m_);
        ++est_.mensagens;
    }
    if (l.find_first_not_of(" \t") == std::string::npos) processarBloco();
    else { bloco_ += l; bloco_ += '\n'; }
    if (aoTexto) aoTexto(l + "\n");
}

void Acars::processarBloco()
{
    if (bloco_.empty()) return;
    std::string b;
    b.swap(bloco_);
    std::string voo = tokenApos(b, "Flight id:", false, "", 8);
    if (voo.empty()) voo = tokenApos(b, "Flight:", false, "", 8);
    if (!voo.empty()) voo = normVoo(voo);
    const std::string reg = tokenApos(b, "reg:", true, "-", 8);
    if (voo.empty() && reg.empty()) return;
    // com audio por arquivo o acarsdec nao sabe a frequencia: "[#2 (L:..." = segundo canal
    double fk = 0;
    const auto pf = b.find("(F:");
    if (pf != std::string::npos) fk = std::atof(b.c_str() + pf + 3) * 1000.0;
    else if (b.rfind("[#", 0) == 0) {
        const int ic = std::atoi(b.c_str() + 2) - 1;
        if (ic >= 0 && ic < (int)freqs_.size()) fk = freqs_[ic] / 1000.0;
    }
    std::lock_guard<std::mutex> lk(m_);
    std::string chave;
    for (auto& [k, a] : avioes_)
        if ((!voo.empty() && a.voo == voo) || (!reg.empty() && a.prefixo == reg)) { chave = k; break; }
    if (chave.empty()) chave = !reg.empty() ? reg : voo;
    AeronaveHfdl& a = avioes_[chave];
    if (!voo.empty()) a.voo = voo;
    if (!reg.empty()) a.prefixo = reg;
    if (fk > 0) a.freqKHz = fk;
    ++a.msgs;
    a.visto = agora();
}

std::vector<AeronaveHfdl> Acars::aeronaves()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<AeronaveHfdl> v;
    for (const auto& [k, a] : avioes_) v.push_back(a);
    std::sort(v.begin(), v.end(), [](const AeronaveHfdl& x, const AeronaveHfdl& y) { return x.visto > y.visto; });
    return v;
}

EstadoExterno Acars::estado()
{
    std::lock_guard<std::mutex> lk(m_);
    EstadoExterno e = est_;
    e.rodando = ext_.vivo();
    return e;
}

// ===========================================================================
//  VDL2 (dumpvdl2)
// ===========================================================================
bool Vdl2::iniciar(const std::vector<double>& canaisHz, double centroHz, uint32_t taxa, std::string& erro)
{
    parar();
    {
        std::lock_guard<std::mutex> lk(m_);
        est_ = EstadoExterno();
        bloco_.clear(); emMsg_ = false;
    }
    if (canaisHz.empty()) { erro = "faltou o canal"; return false; }
    if (taxa != kTaxa) { erro = "o VDL2 precisa do dongle em 1,05 Msps"; return false; }
    centroHz_ = centroHz; taxa_ = taxa; fora_ = false;
    const std::wstring pasta = pastaDecoders();
    const std::wstring exe = pasta + L"\\dumpvdl2.exe";
    std::wstring args = L"--iq-file - --sample-format S16_LE --oversample 10 --centerfreq " +
                        std::to_wstring((long long)std::llround(centroHz)) + L" --utc";
    for (double c : canaisHz) args += L" " + std::to_wstring((long long)std::llround(c));
    ext_.teto = 32u << 20;
    ext_.aoLinha = [this](const std::string& l) { linha(l); };
    if (!ext_.iniciar(exe, args, pasta, L"", erro)) {
        std::lock_guard<std::mutex> lk(m_); est_.erro = erro;
        return false;
    }
    if (aoTexto) {
        std::string t = "[VDL2] dumpvdl2 iniciado - canais";
        char b[32];
        for (double f : canaisHz) { std::snprintf(b, sizeof b, " %.3f", f / 1e6); t += b; }
        t += " MHz\n";
        aoTexto(t);
    }
    return true;
}

void Vdl2::parar() { ext_.parar(); }

void Vdl2::alimentarIQCru(const std::complex<float>* iq, size_t n, uint32_t sps, uint64_t centro)
{
    if (!ext_.vivo() || !iq || !n) return;
    if (sps != taxa_ || std::fabs((double)centro - centroHz_) > 1.0) { fora_ = true; return; }
    fora_ = false;
    std::vector<int16_t> out(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out[2 * i]     = (int16_t)std::lround(std::clamp(iq[i].real() * 0.25f, -1.f, 1.f) * 32767.f);
        out[2 * i + 1] = (int16_t)std::lround(std::clamp(iq[i].imag() * 0.25f, -1.f, 1.f) * 32767.f);
    }
    ext_.escrever(out.data(), out.size() * sizeof(int16_t));
}

void Vdl2::linha(const std::string& l)
{
    if (l.find_first_not_of(" \t") == std::string::npos) {
        if (emMsg_ && aoTexto) aoTexto("\n");
        emMsg_ = false;
        processarBloco();
        return;
    }
    if (l.rfind("[20", 0) == 0 && l.find("] [") != std::string::npos) {   // "[2026-09-29 18:50:00 GMT] [136.975] ..."
        processarBloco();
        std::lock_guard<std::mutex> lk(m_);
        ++est_.mensagens;
        emMsg_ = true;
    }
    bloco_ += l; bloco_ += '\n';
    if (aoTexto) aoTexto(l + "\n");
}

void Vdl2::processarBloco()
{
    if (bloco_.empty()) return;
    std::string b;
    b.swap(bloco_);
    // endereco do aviao: segunda linha "E48D27 (Aircraft, Airborne) -> 10A2C4 (Ground station)"
    // "E488AA (reserved, Airborne) -> F07FA8 (reserved): Command" - o endereco do
    // AVIAO e o do lado que esta no ar (Airborne/On ground), antes ou depois da seta
    std::string hex;
    for (const char* marca : {"Airborne)", "On ground)", "Aircraft"}) {
        const auto pm = b.find(marca);
        if (pm == std::string::npos) continue;
        const auto pp = b.rfind(" (", pm);
        if (pp == std::string::npos || pp < 6) continue;
        std::string h = b.substr(pp - 6, 6);
        bool ok = true;
        for (char c : h) if (!std::isxdigit((unsigned char)c)) ok = false;
        if (ok) { hex = h; break; }
    }
    std::string voo = tokenApos(b, "Flight:", false, "", 8);
    if (!voo.empty()) voo = normVoo(voo);
    const std::string reg = tokenApos(b, "Reg:", true, "-", 8);
    if (voo.empty() && reg.empty() && hex.empty()) return;
    double fk = 0;
    {
        const auto p = b.find("] [");
        if (p != std::string::npos) fk = std::atof(b.c_str() + p + 3) * 1000.0;
    }
    std::lock_guard<std::mutex> lk(m_);
    std::string chave;
    for (auto& [k, a] : avioes_)
        if ((!hex.empty() && a.hex == hex) || (!voo.empty() && a.voo == voo) || (!reg.empty() && a.prefixo == reg)) { chave = k; break; }
    if (chave.empty()) chave = !hex.empty() ? hex : !reg.empty() ? reg : voo;
    AeronaveHfdl& a = avioes_[chave];
    if (!hex.empty()) a.hex = hex;
    if (!voo.empty()) a.voo = voo;
    if (!reg.empty()) a.prefixo = reg;
    if (fk > 0) a.freqKHz = fk;
    ++a.msgs;
    a.visto = agora();
}

std::vector<AeronaveHfdl> Vdl2::aeronaves()
{
    std::lock_guard<std::mutex> lk(m_);
    std::vector<AeronaveHfdl> v;
    for (const auto& [k, a] : avioes_) v.push_back(a);
    std::sort(v.begin(), v.end(), [](const AeronaveHfdl& x, const AeronaveHfdl& y) { return x.visto > y.visto; });
    return v;
}

EstadoExterno Vdl2::estado()
{
    std::lock_guard<std::mutex> lk(m_);
    EstadoExterno e = est_;
    e.rodando = ext_.vivo();
    return e;
}

// ===========================================================================
//  DRM - Digital Radio Mondiale (dream.exe)
// ===========================================================================
namespace {
constexpr double kPiDrm = 3.14159265358979323846;
constexpr uint32_t kDrmTaxa = 48000;           // sinal e audio do dream

// texto JSON com escapes (\" \\ \n \uXXXX), procurado so em [de, ate)
bool txtJsonEm(const std::string& l, size_t de, size_t ate, const char* chave, std::string& v)
{
    const std::string k = std::string("\"") + chave + "\":\"";
    const auto p = l.find(k, de);
    if (p == std::string::npos || p >= ate) return false;
    v.clear();
    for (size_t i = p + k.size(); i < l.size(); ++i) {
        const char c = l[i];
        if (c == '"') {
            while (!v.empty() && (v.back() == ' ' || v.back() == '\0')) v.pop_back();
            return true;
        }
        if (c == '\\' && i + 1 < l.size()) {
            const char e = l[++i];
            if (e == 'n' || e == 'r' || e == 't') v += ' ';
            else if (e == 'u' && i + 4 < l.size()) {
                const unsigned cp = (unsigned)std::strtoul(l.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp == 0) continue;
                if (cp < 0x80) v += (char)cp;
                else if (cp < 0x800) { v += (char)(0xC0 | (cp >> 6)); v += (char)(0x80 | (cp & 0x3F)); }
                else { v += (char)(0xE0 | (cp >> 12)); v += (char)(0x80 | ((cp >> 6) & 0x3F)); v += (char)(0x80 | (cp & 0x3F)); }
            } else v += e;
        } else v += c;
    }
    return false;
}

bool numJsonEm(const std::string& l, size_t de, size_t ate, const char* chave, double& v)
{
    const std::string k = std::string("\"") + chave + "\":";
    const auto p = l.find(k, de);
    if (p == std::string::npos || p >= ate) return false;
    const char* c = l.c_str() + p + k.size();
    if (*c == '"' || *c == '{' || *c == '[') return false;
    if (std::strncmp(c, "true", 4) == 0) { v = 1; return true; }
    if (std::strncmp(c, "false", 5) == 0) { v = 0; return true; }
    char* f = nullptr;
    v = std::strtod(c, &f);
    return f != c;
}

// fim (exclusivo) do objeto/lista que comeca em l[i] ('{' ou '[')
size_t fimJson(const std::string& l, size_t i)
{
    int nivel = 0;
    bool aspas = false;
    for (size_t j = i; j < l.size(); ++j) {
        const char c = l[j];
        if (aspas) { if (c == '\\') ++j; else if (c == '"') aspas = false; continue; }
        if (c == '"') aspas = true;
        else if (c == '{' || c == '[') ++nivel;
        else if (c == '}' || c == ']') { if (--nivel == 0) return j + 1; }
    }
    return std::string::npos;
}

// "chave":{...} dentro de [de, ate) -> [ini, fim)
bool objJsonEm(const std::string& l, size_t de, size_t ate, const char* chave, size_t& ini, size_t& fim)
{
    const std::string k = std::string("\"") + chave + "\":";
    const auto p = l.find(k, de);
    if (p == std::string::npos || p >= ate) return false;
    const size_t i = p + k.size();
    if (i >= l.size() || (l[i] != '{' && l[i] != '[')) return false;
    const size_t f = fimJson(l, i);
    if (f == std::string::npos) return false;
    ini = i; fim = f;
    return true;
}

double blackman(double x)   // x em [0, 1]
{
    return 0.42 - 0.5 * std::cos(2 * kPiDrm * x) + 0.08 * std::cos(4 * kPiDrm * x);
}
} // namespace

bool Drm::iniciar(std::string& erro)
{
    parar();
    const std::wstring exe = pastaDecoders() + L"\\drm\\dream.exe";
    {
        std::lock_guard<std::mutex> lk(m_);
        est_ = EstadoDrm{};
        ultTexto_.clear(); ultEstacao_.clear();
    }
    {
        std::lock_guard<std::mutex> la(audMutex_);
        aud_.clear(); audIni_ = 0; tocando_ = false; restoBin_.clear(); posAud_ = 0;
    }
    sps_ = 0; ganho_ = 0; potMedia_ = 0;
    ultMscOk_ = 0;

    // pasta de trabalho propria: o dream le/grava o Dream.ini na pasta atual
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring trab = std::wstring(tmp) + L"rxsdr_drm_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(trab.c_str(), nullptr);

    ext_.aoLinha = [this](const std::string& l) { linha(l); };
    ext_.aoBinario = [this](const char* p, size_t n) { binario(p, n); };
    ext_.teto = 4u << 20;     // ~20 s de IQ (48 kS/s x 4 bytes); se o dream atrasar, perde o mais velho
    std::wstring args = std::wstring(L"-c ") + (inverter ? L"7" : L"6") +
                        // -i 2: duas iteracoes do decodificador MLC (~1 dB a mais em sinal fraco)
                        L" -i 2 --sigsrate 48000 --audsrate 48000 -I - -O - --status-socket -";
    if (!ext_.iniciar(exe, args, trab, L"", erro)) {
        std::lock_guard<std::mutex> lk(m_);
        est_.erro = erro;
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        est_.rodando = true;
    }
    if (aoTexto) aoTexto("[DRM] " + horaUtc() + " UTC  dream iniciado - procurando o sinal DRM...\n");
    return true;
}

void Drm::parar()
{
    ext_.parar();
    std::lock_guard<std::mutex> lk(m_);
    est_.rodando = false;
}

void Drm::prepararReamostragem(uint32_t sps)
{
    sps_ = sps;
    // 1) FIR + decimacao inteira ate ~200 kS/s (corte no meio da faixa de
    //    transicao 12 kHz .. S1-12 kHz, para nada dobrar em cima do canal)
    D1_ = std::max(1, (int)(sps / 192000));
    S1_ = double(sps) / D1_;
    cont1_ = 0; pos1_ = 0;
    h1_.clear(); hist1_.clear();
    if (D1_ > 1) {
        const double trans = std::max(20000.0, S1_ - 24000.0);
        int L = (int)std::ceil(5.5 * double(sps) / trans);
        L = std::clamp(L | 1, 31, 1201);
        const double fc = (S1_ / 2.0) / double(sps);
        h1_.resize(L);
        double soma = 0;
        for (int i = 0; i < L; ++i) {
            const double t = i - (L - 1) / 2.0;
            const double s = t == 0 ? 2 * fc : std::sin(2 * kPiDrm * fc * t) / (kPiDrm * t);
            const double w = blackman(double(i) / (L - 1));
            h1_[i] = (float)(s * w);
            soma += s * w;
        }
        for (auto& v : h1_) v = (float)(v / soma);
        hist1_.assign((size_t)2 * L, {0, 0});
    }
    // 2) interpolador polifasico S1 -> 48 kS/s (passa ate 12 kHz, corta a
    //    partir de 36 kHz: o que sobrar dobra fora do canal DRM)
    passo2_ = S1_ / kDrmTaxa;
    const double fc2 = 24000.0 / S1_;
    L2_ = (int)std::ceil(5.5 * S1_ / 24000.0);
    if (L2_ & 1) ++L2_;
    L2_ = std::clamp(L2_, 8, 256);
    P2_ = 256;
    tab2_.assign((size_t)P2_ * L2_, 0.f);
    const int meio = L2_ / 2 - 1;
    for (int p = 0; p < P2_; ++p) {
        const double frac = double(p) / P2_;
        double soma = 0;
        for (int j = 0; j < L2_; ++j) {
            const double t = (j - meio) - frac;
            const double s = std::fabs(t) < 1e-9 ? 2 * fc2 : std::sin(2 * kPiDrm * fc2 * t) / (kPiDrm * t);
            const double x = (t + L2_ / 2.0) / L2_;
            const double w = (x <= 0 || x >= 1) ? 0 : blackman(x);
            tab2_[(size_t)p * L2_ + j] = (float)(s * w);
            soma += s * w;
        }
        if (soma != 0)
            for (int j = 0; j < L2_; ++j) tab2_[(size_t)p * L2_ + j] = (float)(tab2_[(size_t)p * L2_ + j] / soma);
    }
    buf2_.assign((size_t)L2_, {0, 0});
    pos2_ = L2_;
}

void Drm::alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps)
{
    if (!ext_.vivo() || !iq || !n || sps < 96000) return;
    if (sps != sps_) prepararReamostragem(sps);

    // estagio 1
    if (D1_ > 1) {
        const int L = (int)h1_.size();
        for (size_t i = 0; i < n; ++i) {
            hist1_[pos1_] = hist1_[pos1_ + L] = iq[i];
            pos1_ = (pos1_ + 1) % (size_t)L;
            if (++cont1_ < D1_) continue;
            cont1_ = 0;
            const std::complex<float>* x = &hist1_[pos1_];     // L amostras seguidas (da mais velha para a nova)
            float re = 0, im = 0;
            for (int k = 0; k < L; ++k) { re += h1_[k] * x[k].real(); im += h1_[k] * x[k].imag(); }
            buf2_.emplace_back(re, im);
        }
    } else {
        buf2_.insert(buf2_.end(), iq, iq + n);
    }

    // estagio 2
    const int meio = L2_ / 2 - 1;
    saida_.clear();
    std::vector<std::complex<float>> y;
    y.reserve((size_t)(buf2_.size() / passo2_) + 4);
    for (;;) {
        const size_t base = (size_t)pos2_;
        if (base + (size_t)(L2_ / 2) >= buf2_.size()) break;
        const double frac = pos2_ - (double)base;
        const int p = std::min(P2_ - 1, (int)(frac * P2_ + 0.5));
        const float* h = &tab2_[(size_t)p * L2_];
        const std::complex<float>* x = &buf2_[base - meio];
        float re = 0, im = 0;
        for (int j = 0; j < L2_; ++j) { re += h[j] * x[j].real(); im += h[j] * x[j].imag(); }
        y.emplace_back(re, im);
        pos2_ += passo2_;
    }
    const size_t gasto = (size_t)pos2_ > (size_t)meio ? (size_t)pos2_ - (size_t)meio : 0;
    if (gasto > 0) {
        buf2_.erase(buf2_.begin(), buf2_.begin() + (long long)std::min(gasto, buf2_.size()));
        pos2_ -= (double)gasto;
    }
    if (y.empty()) return;

    // ganho automatico lento (~2 s): o OFDM tem picos ~10 dB acima da media,
    // entao o valor eficaz fica perto de 3000 (de 32767)
    double pot = 0;
    for (const auto& v : y) pot += (double)v.real() * v.real() + (double)v.imag() * v.imag();
    pot /= (double)y.size();
    const double a = 1.0 - std::exp(-(double)y.size() / (kDrmTaxa * 2.0));
    potMedia_ = potMedia_ <= 0 ? pot : potMedia_ + a * (pot - potMedia_);
    ganho_ = potMedia_ > 1e-20 ? 3000.0 / std::sqrt(potMedia_) : 1.0;

    saida_.resize(y.size() * 2);
    for (size_t i = 0; i < y.size(); ++i) {
        saida_[2 * i]     = (int16_t)std::clamp(std::lround(y[i].real() * ganho_), -32767L, 32767L);
        saida_[2 * i + 1] = (int16_t)std::clamp(std::lround(y[i].imag() * ganho_), -32767L, 32767L);
    }
    ext_.escrever(saida_.data(), saida_.size() * sizeof(int16_t));
}

// audio do dream: S16 estereo 48 kHz -> mono na fila
void Drm::binario(const char* p, size_t n)
{
    std::lock_guard<std::mutex> la(audMutex_);
    restoBin_.append(p, n);
    const size_t quadros = restoBin_.size() / 4;
    const int16_t* s = reinterpret_cast<const int16_t*>(restoBin_.data());
    for (size_t i = 0; i < quadros; ++i)
        aud_.push_back((int16_t)(((int)s[2 * i] + (int)s[2 * i + 1]) / 2));
    restoBin_.erase(0, quadros * 4);
    // nunca mais de 6 s guardados (sobra o mais novo). Enquanto o audio nao
    // presta (o radio esta tocando o proprio som), so 1 s: quando o DRM
    // entrar, entra sem atraso
    const size_t disp = aud_.size() - audIni_;
    if (!audioBom()) {
        if (disp > kDrmTaxa) audIni_ = aud_.size() - kDrmTaxa;
        tocando_ = false; posAud_ = 0;
    }
    else if (disp > kDrmTaxa * 6) audIni_ = aud_.size() - kDrmTaxa * 3;
    if (audIni_ > kDrmTaxa * 4) { aud_.erase(aud_.begin(), aud_.begin() + (long long)audIni_); audIni_ = 0; }
    if (quadros) {
        std::lock_guard<std::mutex> lk(m_);
        est_.ultAudio = agora();
    }
}

void Drm::puxarAudio(int16_t* out, size_t n, uint32_t sps)
{
    if (!out || !n) return;
    std::lock_guard<std::mutex> la(audMutex_);
    size_t disp = aud_.size() - audIni_;
    if (!tocando_ && disp >= kDrmTaxa * 8 / 10) tocando_ = true;     // 0,8 s de folga
    size_t i = 0;
    if (tocando_ && sps >= 8000) {
        // relogios diferentes (dongle x dream): corrige devagar pelo tamanho da fila
        double passo = double(kDrmTaxa) / double(sps);
        if (disp > kDrmTaxa * 25 / 10) passo *= 1.005;
        else if (disp < kDrmTaxa * 4 / 10) passo *= 0.995;
        for (; i < n; ++i) {
            const size_t k = audIni_ + (size_t)posAud_;
            if (k + 1 >= aud_.size()) { tocando_ = false; break; }
            const double f = posAud_ - std::floor(posAud_);
            out[i] = (int16_t)std::lround(aud_[k] * (1.0 - f) + aud_[k + 1] * f);
            posAud_ += passo;
        }
        const size_t pulo = (size_t)posAud_;
        audIni_ = std::min(aud_.size(), audIni_ + pulo);
        posAud_ -= (double)pulo;
    }
    for (; i < n; ++i) out[i] = 0;
}

void Drm::linha(const std::string& l)
{
    if (l.compare(0, 7, "STATUS ") != 0) {
        if (linhasCruas && aoTexto && !l.empty()) aoTexto("[dream] " + l + "\n");
        return;
    }
    EstadoDrm e;
    {
        std::lock_guard<std::mutex> lk(m_);
        e = est_;
    }
    e.comStatus = true;
    e.ultStatus = agora();
    const size_t N = l.size();
    size_t a = 0, b = 0;
    double v = 0;
    if (objJsonEm(l, 0, N, "status", a, b)) {
        if (numJsonEm(l, a, b, "io", v)) e.io = (int)v;
        if (numJsonEm(l, a, b, "time", v)) e.tempo = (int)v;
        if (numJsonEm(l, a, b, "frame", v)) e.quadro = (int)v;
        if (numJsonEm(l, a, b, "fac", v)) e.fac = (int)v;
        if (numJsonEm(l, a, b, "sdc", v)) e.sdc = (int)v;
        if (numJsonEm(l, a, b, "msc", v)) e.msc = (int)v;
    }
    const bool travado = e.tempo == 0 && e.fac == 0;
    if (travado && e.msc == 0) ultMscOk_ = agora();
    if (objJsonEm(l, 0, N, "signal", a, b)) {
        if (numJsonEm(l, a, b, "snr_db", v)) e.snr = v;
        if (numJsonEm(l, a, b, "if_level_db", v)) e.nivelDb = v;
        e.doppler = numJsonEm(l, a, b, "doppler_hz", v) ? v : -1;
        e.atrasoMs = numJsonEm(l, a, b, "delay_max_ms", v) ? v : (numJsonEm(l, a, b, "delay_min_ms", v) ? v : -1);
    }
    if (objJsonEm(l, 0, N, "mode", a, b)) {
        if (numJsonEm(l, a, b, "robustness", v)) e.robustez = (int)v;
        if (numJsonEm(l, a, b, "bandwidth_khz", v)) e.larguraKHz = v;
        if (numJsonEm(l, a, b, "interleaver", v)) e.intercalador = (int)v;
    } else if (!travado) {
        e.robustez = -1; e.larguraKHz = 0;
    }
    if (objJsonEm(l, 0, N, "coding", a, b)) {
        if (numJsonEm(l, a, b, "msc_qam", v)) e.mscQam = (int)v;
        if (numJsonEm(l, a, b, "sdc_qam", v)) e.sdcQam = (int)v;
    }
    if (objJsonEm(l, 0, N, "services", a, b)) {
        if (numJsonEm(l, a, b, "audio", v)) e.servicosAudio = (int)v;
        if (numJsonEm(l, a, b, "data", v)) e.servicosDados = (int)v;
    }
    // o primeiro servico de audio da lista (e o que o dream toca)
    if (objJsonEm(l, 0, N, "service_list", a, b)) {
        size_t oi = std::string::npos, of = 0;
        for (size_t i = a + 1; i < b; ++i) {
            if (l[i] != '{') continue;
            const size_t f = fimJson(l, i);
            if (f == std::string::npos || f > b) break;
            double au = 0;
            if (numJsonEm(l, i, f, "is_audio", au) && au > 0) { oi = i; of = f; break; }
            i = f - 1;
        }
        if (oi != std::string::npos) {
            std::string s;
            if (txtJsonEm(l, oi, of, "label", s) && !s.empty()) e.estacao = s;
            if (txtJsonEm(l, oi, of, "id", s)) e.idServico = s;
            if (numJsonEm(l, oi, of, "audio_coding", v)) {
                const int c = (int)v;
                e.codec = c == 0 ? "AAC" : c == 1 ? "Opus" : c == 3 ? "xHE-AAC" : "?";
            }
            if (numJsonEm(l, oi, of, "bitrate_kbps", v)) e.kbps = v;
            if (txtJsonEm(l, oi, of, "audio_mode", s)) e.modoAudio = s == "Mono" ? "mono" : s == "Stereo" ? "estéreo" : s == "P-Stereo" ? "estéreo paramétrico" : s;
            if (txtJsonEm(l, oi, of, "protection_mode", s)) e.protecao = s;
            if (txtJsonEm(l, oi, of, "text", s)) e.texto = s;
            size_t si = 0, sf = 0;
            if (objJsonEm(l, oi, of, "language", si, sf) && txtJsonEm(l, si, sf, "name", s)) e.idioma = s;
            if (objJsonEm(l, oi, of, "program_type", si, sf) && txtJsonEm(l, si, sf, "name", s)) e.programa = s;
            if (objJsonEm(l, oi, of, "country", si, sf) && txtJsonEm(l, si, sf, "name", s)) e.pais = s;
        }
    }
    if (objJsonEm(l, 0, N, "drm_time", a, b)) {
        double ok = 0, h = 0, mi = 0;
        if (numJsonEm(l, a, b, "valid", ok) && ok > 0 && numJsonEm(l, a, b, "hour", h) && numJsonEm(l, a, b, "min", mi)) {
            char t[16];
            std::snprintf(t, sizeof t, "%02d:%02d", (int)h, (int)mi);
            e.horaDrm = t;
        }
    }

    // mensagens para a caixa de texto: estacao nova e texto novo
    std::string novo;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!e.estacao.empty() && e.estacao != ultEstacao_) {
            ultEstacao_ = e.estacao;
            static const char kRob[] = "ABCDE";
            char q[200];
            std::snprintf(q, sizeof q, "  (modo %c, %.0f kHz, %s %.1f kbps, SNR %.1f dB)",
                          e.robustez >= 0 && e.robustez < 5 ? kRob[e.robustez] : '?', e.larguraKHz,
                          e.codec.c_str(), e.kbps, e.snr);
            novo += "[DRM] " + horaUtc() + " UTC  Estacao: " + e.estacao + q + "\n";
            std::string extra;
            if (!e.pais.empty()) extra += e.pais;
            if (!e.idioma.empty()) extra += (extra.empty() ? "" : ", ") + e.idioma;
            if (!e.programa.empty()) extra += (extra.empty() ? "" : ", ") + e.programa;
            if (!extra.empty()) novo += "      " + extra + "\n";
        }
        if (!e.texto.empty() && e.texto != ultTexto_) {
            ultTexto_ = e.texto;
            novo += "[DRM] " + horaUtc() + " UTC  " + e.texto + "\n";
        }
        est_ = e;
    }
    if (!novo.empty() && aoTexto) aoTexto(novo);
}

bool Drm::audioBom() const
{
    const double t = ultMscOk_.load();
    return t > 0 && agora() - t < 5.0;
}

EstadoDrm Drm::estado()
{
    EstadoDrm e;
    {
        std::lock_guard<std::mutex> lk(m_);
        e = est_;
    }
    e.rodando = e.rodando && ext_.vivo();
    {
        std::lock_guard<std::mutex> la(audMutex_);
        e.bufferS = double(aud_.size() - audIni_) / kDrmTaxa;
    }
    return e;
}

} // namespace masdr
