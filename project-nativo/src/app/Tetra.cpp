#include "Tetra.h"
#include "../ui/Idioma.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <regex>

namespace masdr {

namespace {

constexpr double kPiT = 3.14159265358979323846;
constexpr int    kFs = 36000;          // IQ do demodulador
constexpr int    kSps = 2;             // 18 ksimbolos/s
constexpr double kBeta = 0.35;         // roll-off
constexpr size_t kAcelp = 1380;        // quadro ACELP do tetra-rx
constexpr size_t kPcm = 960;           // 480 amostras de 8 kHz

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

bool existe(const std::wstring& f) { return GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string horaUtc()
{
    SYSTEMTIME st; GetSystemTime(&st);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return b;
}

// FFT radix-2 in-place (so para a aquisicao inicial)
void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPiT / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1, 0);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Root raised cosine com energia unitaria (igual ao tetra_demod.py)
std::vector<float> rrcTaps(int sps, int span, double beta)
{
    const int N = span * sps + 1;
    std::vector<double> t(N);
    double e = 0;
    for (int i = 0; i < N; ++i) {
        const double tt = (i - (N - 1) / 2.0) / sps;
        double v;
        if (tt == 0.0) v = (1.0 - beta) + 4.0 * beta / kPiT;
        else if (std::fabs(std::fabs(4.0 * beta * tt) - 1.0) < 1e-12)
            v = (beta / std::sqrt(2.0)) * ((1.0 + 2.0 / kPiT) * std::sin(kPiT / (4.0 * beta)) +
                                          (1.0 - 2.0 / kPiT) * std::cos(kPiT / (4.0 * beta)));
        else {
            const double num = std::sin(kPiT * tt * (1.0 - beta)) + 4.0 * beta * tt * std::cos(kPiT * tt * (1.0 + beta));
            const double den = kPiT * tt * (1.0 - (4.0 * beta * tt) * (4.0 * beta * tt));
            v = num / den;
        }
        t[i] = v; e += v * v;
    }
    std::vector<float> h(N);
    for (int i = 0; i < N; ++i) h[i] = (float)(t[i] / std::sqrt(e));
    return h;
}

long numero(const std::string& v, bool hexPrimeiro)
{
    if (v.empty()) return 0;
    try {
        if (v.size() > 2 && (v[0] == '0') && (v[1] == 'x' || v[1] == 'X')) return std::stol(v.substr(2), nullptr, 16);
        if (v.size() >= 5 && v.find_first_not_of("01") == std::string::npos) return std::stol(v, nullptr, 2);
        size_t p = 0;
        if (hexPrimeiro) { long r = std::stol(v, &p, 16); if (p == v.size()) return r; }
        long r = std::stol(v, &p, 10); if (p == v.size()) return r;
        return std::stol(v, nullptr, 16);
    } catch (...) { return 0; }
}

const char* nomeTea(long c) { return c == 0 ? T("sem cripto") : c == 1 ? "TEA1" : c == 2 ? "TEA2" : c == 3 ? "TEA3" : "?"; }

// Bloco de ambiente com as variaveis que o tetra-rx usa para mandar o TETMON
std::vector<wchar_t> ambienteCom(const std::vector<std::wstring>& extras)
{
    std::vector<wchar_t> b;
    wchar_t* env = GetEnvironmentStringsW();
    for (wchar_t* p = env; p && *p; p += wcslen(p) + 1) {
        std::wstring s(p);
        bool troca = false;
        for (const auto& e : extras) {
            const auto k = e.substr(0, e.find(L'=') + 1);
            if (_wcsnicmp(s.c_str(), k.c_str(), k.size()) == 0) troca = true;
        }
        if (!troca) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
    }
    if (env) FreeEnvironmentStringsW(env);
    for (const auto& e : extras) { b.insert(b.end(), e.begin(), e.end()); b.push_back(0); }
    b.push_back(0);
    return b;
}

} // namespace

Tetra::Tetra()
{
    // FIR anti-alias 13 kHz, 127 taps (sinc x Hamming), ganho 1
    const int nt = 127;
    const double fc = 13000.0 / kFs;
    fir_.resize(nt);
    double s = 0;
    for (int k = 0; k < nt; ++k) {
        const double n = k - (nt - 1) / 2.0;
        const double x = 2.0 * fc * n;
        const double sinc = n == 0 ? 1.0 : std::sin(kPiT * x) / (kPiT * x);
        const double w = 0.54 - 0.46 * std::cos(2 * kPiT * k / (nt - 1));
        fir_[k] = (float)(sinc * w); s += fir_[k];
    }
    for (auto& v : fir_) v = (float)(v / s);

    // filtros de borda do FLL (45 taps, janela de Hann)
    const int L = 45;
    const double fEdge = ((1.0 + kBeta) / 2.0) / kSps;
    hLow_.resize(L); hHigh_.resize(L);
    double sl = 0;
    for (int k = 0; k < L; ++k) {
        const double n = k - (L - 1) / 2.0;
        const double w = 0.5 - 0.5 * std::cos(2 * kPiT * k / (L - 1));
        hLow_[k] = std::complex<float>((float)(w * std::cos(-2 * kPiT * fEdge * n)), (float)(w * std::sin(-2 * kPiT * fEdge * n)));
        hHigh_[k] = std::conj(hLow_[k]);
        sl += w;
    }
    for (auto& v : hLow_) v /= (float)sl;
    for (auto& v : hHigh_) v /= (float)sl;
    const double zeta = 0.707, wn = 2 * kPiT * 40.0 / kFs;
    const double den = 1 + 2 * zeta * wn + wn * wn;
    fllKp_ = 4 * zeta * wn / den;
    fllKi_ = 4 * wn * wn / den;

    rrc_ = rrcTaps(kSps, 15, kBeta);
}

Tetra::~Tetra() { parar(); }

void Tetra::msg(const std::string& s) { if (aoTexto) aoTexto(s); }

bool Tetra::iniciar(bool inverterIQ, std::string& erro)
{
    parar();
    inverter_ = inverterIQ;
    {
        std::lock_guard<std::mutex> lk(estMutex_);
        est_ = EstadoTetra();
        ultMeta_.clear();
        tetmonCripto_ = false;
    }
    { std::lock_guard<std::mutex> lk(vozMutex_); voz_.clear(); tocando_ = false; caudaVoz_.clear(); posVoz_ = 0; }
    { std::lock_guard<std::mutex> lk(filaMutex_); fila_.clear(); }
    // zera o demodulador
    decSps_ = 0; decCauda_.clear(); decPos_ = 0;
    acq_.clear(); acqFeita_ = false;
    firHist_.assign(fir_.size() - 1, {0, 0});
    agcHist_.assign(64, 0.f); agcPos_ = 0; agcSoma_ = 0;
    fllHist_.assign(hLow_.size() - 1, {0, 0});
    fllInteg_ = fllFreq_ = fllFase_ = 0; errVarEma_ = errBiasEma_ = 1; fllTravado_ = false;
    mfHist_.assign(rrc_.size() - 1, {0, 0}); mfY_.clear(); mfPos_ = 2; mu_ = 0; rate_ = 0; ultSym_ = {0, 0};
    pllFase_ = pllFreq_ = 0; prevSym_ = {1, 0}; snrEma_ = -99; snrOk_ = false;

    const std::wstring pasta = pastaDecoders();
    const std::wstring rx = pasta + L"\\tetra-rx.exe";
    if (!existe(rx) || !existe(pasta + L"\\cdecoder.exe") || !existe(pasta + L"\\sdecoder.exe")) {
        erro = T("Faltam arquivos do TETRA na pasta decoders (tetra-rx.exe, cdecoder.exe, sdecoder.exe).");
        std::lock_guard<std::mutex> lk(estMutex_); est_.erro = erro;
        return false;
    }

    // UDP local para o TETMON do tetra-rx
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    int alen = sizeof a;
    if (s == INVALID_SOCKET || bind(s, (sockaddr*)&a, sizeof a) != 0 || getsockname(s, (sockaddr*)&a, &alen) != 0) {
        if (s != INVALID_SOCKET) closesocket(s);
        erro = T("nao consegui reservar a porta UDP do TETMON");
        return false;
    }
    DWORD to = 200; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
    int rb = 1 << 20; setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rb, sizeof rb);
    portaTetmon_ = ntohs(a.sin_port);

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE inR, inW, outR, outW;
    if (!CreatePipe(&inR, &inW, &sa, 1 << 20) || !CreatePipe(&outR, &outW, &sa, 1 << 16)) {
        closesocket(s); erro = T("CreatePipe falhou"); return false;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    std::wstring cmd = L"\"" + rx + L"\" -i -r -s -e";
    if (existe(pasta + L"\\keyfile")) cmd += L" -k \"" + pasta + L"\\keyfile\"";
    cmd += L" -";
    auto env = ambienteCom({L"TETRA_HACK_PORT=" + std::to_wstring(portaTetmon_), L"TETRA_HACK_IP=127.0.0.1",
                            L"TETRA_HACK_RXID=1"});
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = nul;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> linha(cmd.begin(), cmd.end()); linha.push_back(0);
    const BOOL ok = CreateProcessW(nullptr, linha.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(), pasta.c_str(), &si, &pi);
    CloseHandle(inR); CloseHandle(outW); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(inW); CloseHandle(outR); closesocket(s);
        erro = T("nao consegui abrir o tetra-rx.exe (erro ") + std::to_string(GetLastError()) + ")";
        std::lock_guard<std::mutex> lk(estMutex_); est_.erro = erro;
        return false;
    }
    CloseHandle(pi.hThread);
    procRx_ = pi.hProcess; rxStdinW_ = inW; rxSaidaR_ = outR; sockTetmon_ = (uintptr_t)s;
    vivo_ = true;
    { std::lock_guard<std::mutex> lk(estMutex_); est_.rodando = true; }
    thDemod_ = std::thread([this] { lacoDemod(); });
    thRx_ = std::thread([this] { lerTetraRx(); });
    thTetmon_ = std::thread([this] { lerTetmon(); });
    msg(std::string(T("[TETRA] iniciado - pi/4-DQPSK 18 ksimb/s, osmo-tetra + ACELP")) + (inverter_ ? T(" (espectro invertido)") : "") +
        T("\n[TETRA] procurando a portadora (1 s de aquisicao)...\n"));
    return true;
}

void Tetra::parar()
{
    if (!procRx_ && !thDemod_.joinable()) return;
    vivo_ = false;
    filaCv_.notify_all();
    if (procRx_) { TerminateProcess((HANDLE)procRx_, 0); WaitForSingleObject((HANDLE)procRx_, 2000); }
    // so derruba o codec aqui (solta quem estiver lendo dele); os canos sao
    // fechados depois que as threads terminarem
    for (void* p : {procC_, procS_}) if (p) TerminateProcess((HANDLE)p, 0);
    {
        std::lock_guard<std::mutex> lk(escritaMutex_);
        if (rxStdinW_) { CloseHandle((HANDLE)rxStdinW_); rxStdinW_ = nullptr; }
    }
    if (sockTetmon_ != ~(uintptr_t)0) { closesocket((SOCKET)sockTetmon_); sockTetmon_ = ~(uintptr_t)0; }
    if (thDemod_.joinable()) thDemod_.join();
    if (thRx_.joinable()) thRx_.join();
    if (thTetmon_.joinable()) thTetmon_.join();
    fecharCodec();
    if (rxSaidaR_) { CloseHandle((HANDLE)rxSaidaR_); rxSaidaR_ = nullptr; }
    if (procRx_) { CloseHandle((HANDLE)procRx_); procRx_ = nullptr; }
    std::lock_guard<std::mutex> lk(estMutex_);
    est_.rodando = false;
}

// ---------------------------------------------------------------------------
//  IQ do dongle -> 36 kS/s (passa-baixas 14 kHz + media movel + interpolacao,
//  como o TetraManager do RXSDR). Roda na thread do dongle: so conta.
// ---------------------------------------------------------------------------
void Tetra::alimentarIQ(const std::complex<float>* iq, size_t n, uint32_t sps)
{
    if (!vivo_ || !iq || !n || !sps) return;
    if (sps != decSps_) {
        decSps_ = sps;
        aaM_ = std::max<size_t>(1, (size_t)std::lround((double)sps / kFs));
        aaAnel_.assign(aaM_, {0, 0}); aaSoma_ = {0, 0}; aaIdx_ = 0;
        lpA_ = 1.0f - std::exp(-2.0f * (float)kPiT * 14000.0f / (float)sps);
        lp1_ = lp2_ = {0, 0};
        decCauda_.clear(); decPos_ = 0;
    }
    const double inv = 1.0 / (double)aaM_;
    const size_t base = decCauda_.size();
    decCauda_.resize(base + n);
    for (size_t i = 0; i < n; ++i) {
        std::complex<float> v = iq[i];
        if (inverter_) v = std::conj(v);
        lp1_ += lpA_ * (v - lp1_);
        lp2_ += lpA_ * (lp1_ - lp2_);
        aaSoma_ += std::complex<double>(lp2_.real(), lp2_.imag()) - std::complex<double>(aaAnel_[aaIdx_].real(), aaAnel_[aaIdx_].imag());
        aaAnel_[aaIdx_] = lp2_;
        aaIdx_ = (aaIdx_ + 1) % aaM_;
        decCauda_[base + i] = std::complex<float>((float)(aaSoma_.real() * inv), (float)(aaSoma_.imag() * inv));
    }
    const double passo = (double)sps / kFs;
    std::vector<std::complex<float>> out;
    out.reserve((size_t)(n / passo) + 4);
    while (true) {
        const size_t i0 = (size_t)decPos_;
        if (i0 + 1 >= decCauda_.size()) break;
        const float fr = (float)(decPos_ - i0);
        out.push_back(decCauda_[i0] + (decCauda_[i0 + 1] - decCauda_[i0]) * fr);
        decPos_ += passo;
    }
    const size_t usados = std::min((size_t)decPos_, decCauda_.size() - 1);
    decCauda_.erase(decCauda_.begin(), decCauda_.begin() + usados);
    decPos_ -= (double)usados;
    if (out.empty()) return;
    {
        std::lock_guard<std::mutex> lk(filaMutex_);
        fila_.insert(fila_.end(), out.begin(), out.end());
        while (fila_.size() > (size_t)kFs * 4) fila_.pop_front();   // 4 s atrasado: descarta o velho
    }
    filaCv_.notify_one();
}

// ---------------------------------------------------------------------------
//  Demodulador (traducao do tetra_demod.py)
// ---------------------------------------------------------------------------
void Tetra::lacoDemod()
{
    std::vector<std::complex<float>> blk;
    std::vector<float> soft;
    while (vivo_) {
        {
            std::unique_lock<std::mutex> lk(filaMutex_);
            filaCv_.wait_for(lk, std::chrono::milliseconds(200), [this] { return !vivo_ || fila_.size() >= 1024; });
            if (!vivo_) return;
            if (fila_.size() < 1024) continue;
            blk.assign(fila_.begin(), fila_.begin() + 1024);
            fila_.erase(fila_.begin(), fila_.begin() + 1024);
        }
        if (!acqFeita_) {
            acq_.insert(acq_.end(), blk.begin(), blk.end());
            if (acq_.size() >= (size_t)kFs) adquirir();
            continue;
        }
        soft.clear();
        demodular(blk.data(), blk.size(), soft);
        if (!soft.empty()) escreverSoft(soft);
    }
}

// Aquisicao: 1 s de IQ, espectro medio e correlacao com a largura do canal
// TETRA (~22 kHz) - o pico e o centro da portadora. Ja poe o FLL la, senao
// ele levaria 20 s para andar 8-10 kHz.
void Tetra::adquirir()
{
    const int N = 2048, hop = N / 2;
    std::vector<double> psd(N, 0.0);
    int cont = 0;
    std::vector<std::complex<double>> b(N);
    for (size_t ini = 0; ini + N <= acq_.size(); ini += hop) {
        for (int k = 0; k < N; ++k) {
            const double w = 0.5 - 0.5 * std::cos(2 * kPiT * k / (N - 1));
            b[k] = std::complex<double>(acq_[ini + k].real() * w, acq_[ini + k].imag() * w);
        }
        fft(b);
        for (int k = 0; k < N; ++k) psd[k] += std::norm(b[k]);
        ++cont;
    }
    std::vector<double> sh(N);
    for (int k = 0; k < N; ++k) sh[k] = psd[(k + N / 2) % N];
    const int W = (int)(22000.0 / kFs * N), h = (W - 1) / 2;
    int melhor = N / 2; double maior = -1;
    for (int j = 0; j < N; ++j) {
        double s = 0;
        for (int k = -h; k <= h; ++k) s += sh[((j + k) % N + N) % N];
        if (s > maior) { maior = s; melhor = j; }
    }
    const double hz = (melhor - N / 2) * (double)kFs / N;
    double f0 = 2 * kPiT * hz / kFs;
    f0 = std::clamp(f0, -1.75, 1.75);
    fllFreq_ = fllInteg_ = f0;
    acqFeita_ = true;
    acq_.clear(); acq_.shrink_to_fit();
    {
        std::lock_guard<std::mutex> lk(estMutex_);
        est_.adquirindo = false; est_.afcInicial = (float)hz;
    }
    char m[120];
    std::snprintf(m, sizeof m, T("[TETRA] portadora a %+.0f Hz do VFO - acompanhando\n"), hz);
    msg(m);
}

void Tetra::demodular(const std::complex<float>* x, size_t n, std::vector<float>& soft)
{
    // Diferenca para o tetra_demod.py: o NCO do FLL vem ANTES do FIR de
    // 13 kHz. Com a portadora fora do centro (o que e comum: 3 a 10 kHz), o
    // FIR cortava uma borda do sinal e o FLL de borda de banda puxava para o
    // lado errado (medido: 3 kHz reais -> o laco foi para 2,4 kHz em 12 s).
    // Corrigindo antes, o sinal fica centrado e o FIR corta igual dos dois lados.
    // 1) NCO do FLL
    std::vector<std::complex<float>> y0(n);
    for (size_t i = 0; i < n; ++i) {
        const double ph = fllFase_ + (double)i * fllFreq_;
        y0[i] = x[i] * std::complex<float>((float)std::cos(ph), (float)-std::sin(ph));
    }
    fllFase_ = std::fmod(fllFase_ + (double)n * fllFreq_, 2 * kPiT);

    // 2) FIR anti-alias 13 kHz
    const size_t L = fir_.size();
    std::vector<std::complex<float>> y(n);
    firHist_.insert(firHist_.end(), y0.begin(), y0.end());
    for (size_t i = 0; i < n; ++i) {
        std::complex<float> acc(0, 0);
        const std::complex<float>* p = &firHist_[i];
        for (size_t k = 0; k < L; ++k) acc += p[k] * fir_[L - 1 - k];
        y[i] = acc;
    }
    firHist_.erase(firHist_.begin(), firHist_.begin() + n);

    // 3) AGC (media de |x| nas ultimas 64 amostras)
    for (size_t i = 0; i < n; ++i) {
        const float m = std::abs(y[i]);
        agcSoma_ += m - agcHist_[agcPos_];
        agcHist_[agcPos_] = m;
        agcPos_ = (agcPos_ + 1) % agcHist_.size();
        const double avg = std::max(agcSoma_ / agcHist_.size(), 1e-9);
        y[i] *= (float)(1.0 / avg);
    }

    // FLL de borda de banda: erro medido no sinal ja corrigido (laco PI
    // atualizado uma vez por bloco, como no original)
    const size_t Lf = hLow_.size();
    fllHist_.insert(fllHist_.end(), y.begin(), y.end());
    double somaErr = 0, somaErr2 = 0, somaPot = 0;
    for (size_t i = 0; i < n; ++i) {
        std::complex<float> el(0, 0), eh(0, 0);
        const std::complex<float>* p = &fllHist_[i];
        for (size_t k = 0; k < Lf; ++k) { el += p[k] * hLow_[Lf - 1 - k]; eh += p[k] * hHigh_[Lf - 1 - k]; }
        const double ph = std::norm(eh), pl = std::norm(el);
        const double den = std::max(ph + pl, 1e-12);
        const double e = (ph - pl) / den;
        somaErr += e; somaErr2 += e * e; somaPot += ph + pl;
    }
    fllHist_.erase(fllHist_.begin(), fllHist_.begin() + n);
    const double errAvg = somaErr / n, errVar = std::max(0.0, somaErr2 / n - errAvg * errAvg), pot = somaPot / n;
    errVarEma_ = 0.95 * errVarEma_ + 0.05 * errVar;
    errBiasEma_ = 0.95 * errBiasEma_ + 0.05 * std::fabs(errAvg);
    fllTravado_ = pot > 1e-4 && errVarEma_ < 0.45 && errBiasEma_ < 0.15;
    if (pot > 1e-4) {                 // sem sinal o laco fica parado (senao o NCO foge)
        fllInteg_ += fllKi_ * errAvg;
        fllInteg_ *= 0.99995;
        fllInteg_ = std::clamp(fllInteg_, -1.75, 1.75);
        fllFreq_ = std::clamp(fllKp_ * errAvg + fllInteg_, -1.75, 1.75);
    }

    // 4) filtro casado RRC + relogio (Gardner, laco PI, interpolacao cubica)
    const size_t Lr = rrc_.size();
    mfHist_.insert(mfHist_.end(), y.begin(), y.end());
    for (size_t i = 0; i < n; ++i) {
        std::complex<float> acc(0, 0);
        const std::complex<float>* p = &mfHist_[i];
        for (size_t k = 0; k < Lr; ++k) acc += p[k] * rrc_[Lr - 1 - k];
        mfY_.push_back(acc);
    }
    mfHist_.erase(mfHist_.begin(), mfHist_.begin() + n);
    auto interp = [&](double idx) -> std::complex<float> {
        const long i0 = (long)std::floor(idx);
        const float f = (float)(idx - i0);
        if (i0 >= 1 && i0 + 2 < (long)mfY_.size()) {
            const auto ym1 = mfY_[i0 - 1], y0 = mfY_[i0], y1 = mfY_[i0 + 1], y2 = mfY_[i0 + 2];
            const auto a0 = -0.5f * ym1 + 1.5f * y0 - 1.5f * y1 + 0.5f * y2;
            const auto a1 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const auto a2 = -0.5f * ym1 + 0.5f * y1;
            return y0 + f * (a2 + f * (a1 + f * a0));
        }
        if (i0 < 0 || i0 + 1 >= (long)mfY_.size()) return {0, 0};
        return mfY_[i0] * (1 - f) + mfY_[i0 + 1] * f;
    };
    std::vector<std::complex<float>> sym;
    long i = (long)mfPos_;
    while (true) {
        const double idxMid = i + kSps / 2 + mu_, idxCur = i + kSps + mu_;
        if ((long)std::floor(idxCur) + 2 >= (long)mfY_.size()) break;
        const auto mid = interp(idxMid), cur = interp(idxCur);
        const double err = std::real((cur - ultSym_) * std::conj(mid));
        rate_ = std::clamp(rate_ + 0.001 * err, -0.05, 0.05);
        mu_ -= 0.015 * err + rate_;
        if (mu_ > 0.5) { mu_ -= 1.0; i += 1; }
        else if (mu_ < -0.5) { mu_ += 1.0; i -= 1; }
        sym.push_back(cur);
        ultSym_ = cur;
        i += kSps;
    }
    const long corta = std::max(0L, i - 4);
    mfY_.erase(mfY_.begin(), mfY_.begin() + std::min<long>(corta, (long)mfY_.size()));
    mfPos_ = (double)(i - corta);

    // 5) PLL de portadora (decisao dirigida, 8 pontos) e 6) fase diferencial
    const double passo = kPiT / 4.0;
    double s1 = 0, s2 = 0; int ns = 0;
    for (auto& s : sym) {
        const auto r = s * std::complex<float>((float)std::cos(pllFase_), (float)-std::sin(pllFase_));
        const double ang = std::atan2(r.imag(), r.real());
        double e = ang - std::round(ang / passo) * passo;
        if (e > kPiT) e -= 2 * kPiT;
        if (e < -kPiT) e += 2 * kPiT;
        pllFreq_ = std::clamp(pllFreq_ + 0.00005 * e, -0.20, 0.20);
        pllFase_ += pllFreq_ + 0.02 * e;
        const auto d = r * std::conj(prevSym_);
        prevSym_ = r;
        const double an = std::atan2(d.imag(), d.real());
        soft.push_back((float)std::clamp(an * 4.0 / kPiT, -4.5, 4.5));   // o tetra-rx -i le isto
        double norm = std::fmod(an - kPiT / 4.0, 2 * kPiT); if (norm < 0) norm += 2 * kPiT;
        const int idx = (int)std::lround(norm / (kPiT / 2)) % 4;
        double pe = an - (kPiT / 4.0 + idx * kPiT / 2);
        pe = std::atan2(std::sin(pe), std::cos(pe));
        s1 += pe; s2 += pe * pe; ++ns;
    }
    pllFase_ = std::fmod(pllFase_, 2 * kPiT);
    if (ns >= 16) {
        const double var = std::max(s2 / ns - (s1 / ns) * (s1 / ns), 1e-4);
        const double db = std::clamp(10.0 * std::log10(1.0 / (2.0 * var)), -5.0, 40.0);
        snrEma_ = snrOk_ ? 0.9 * snrEma_ + 0.1 * db : db;
        snrOk_ = true;
    }
    std::lock_guard<std::mutex> lk(estMutex_);
    est_.travado = fllTravado_;
    est_.afc = (float)(fllFreq_ * kFs / (2 * kPiT));
    est_.snr = fllTravado_ && snrOk_ ? (float)snrEma_ : -99.f;
}

void Tetra::escreverSoft(const std::vector<float>& s)
{
    // Diagnostico: com decoders\\tetra_dump.on, grava os simbolos em tetra_soft.f32
    static FILE* dump = nullptr; static bool testado = false;
    if (!testado) {
        testado = true;
        if (existe(pastaDecoders() + L"\\tetra_dump.on")) dump = _wfopen((pastaDecoders() + L"\\tetra_soft.f32").c_str(), L"wb");
    }
    if (dump) { fwrite(s.data(), sizeof(float), s.size(), dump); fflush(dump); }
    std::lock_guard<std::mutex> lk(escritaMutex_);
    if (!rxStdinW_ || !vivo_) return;
    const char* p = (const char*)s.data();
    DWORD resta = (DWORD)(s.size() * sizeof(float)), w = 0;
    while (resta > 0) {
        if (!WriteFile((HANDLE)rxStdinW_, p, resta, &w, nullptr)) return;
        p += w; resta -= w;
    }
}

// ---------------------------------------------------------------------------
//  Texto do tetra-rx (contadores e dados da celula)
// ---------------------------------------------------------------------------
// valor depois de "chave" (a chave nao pode vir colada numa letra antes:
// "CC" nao pode casar dentro de "MCC"), pulando = : e espacos
static bool valorApos(const std::string& t, const char* chave, std::string& v, bool hex)
{
    const size_t kl = std::strlen(chave);
    size_t p = 0;
    while ((p = t.find(chave, p)) != std::string::npos) {
        if (p > 0 && std::isalnum((unsigned char)t[p - 1])) { p += kl; continue; }
        size_t q = p + kl;
        const size_t q0 = q;
        while (q < t.size() && (t[q] == '=' || t[q] == ':' || t[q] == ' ' || t[q] == '\t')) ++q;
        if (q == q0) { p += kl; continue; }
        const size_t ini = q;
        while (q < t.size() && (std::isdigit((unsigned char)t[q]) || (hex && (std::isxdigit((unsigned char)t[q]) || t[q] == 'x')))) ++q;
        if (q > ini) { v = t.substr(ini, q - ini); return true; }
        p += kl;
    }
    return false;
}

// Texto do tetra-rx. Sai MUITO texto (varias linhas por burst): tem de ser
// lido rapido, senao o cano enche, o tetra-rx para de ler a entrada e tudo
// trava. Por isso nada de std::regex aqui - so buscas simples.
void Tetra::lerTetraRx()
{
    std::string acc;
    std::vector<char> buf(65536);
    DWORD n = 0;
    std::string cruas;
    while (ReadFile((HANDLE)rxSaidaR_, buf.data(), (DWORD)buf.size(), &n, nullptr) && n > 0) {
        acc.append(buf.data(), n);
        size_t ini = 0;
        int dB = 0, dSb = 0, dNdb = 0;
        int mcc = -1, mnc = -1, cc = -1, tn = 0; std::string cripto, uso;
        for (size_t i = 0; i < acc.size(); ++i) {
            if (acc[i] != '\n' && acc[i] != '\r') continue;
            const size_t len = i - ini;
            const char* l = acc.data() + ini;
            ini = i + 1;
            if (len == 0) continue;
            if (len == 5 && std::memcmp(l, "BURST", 5) == 0) { ++dB; continue; }
            std::string t(l, len);
            if (linhasCruas) cruas += "  " + t + "\n";
            if (len > 400) continue;
            if (t.find("found SYNC training sequence") != std::string::npos) ++dSb;
            if (t.find("CRC=1") != std::string::npos && t.find("AACH") == std::string::npos) ++dNdb;
            std::string v;
            if (valorApos(t, "MCC", v, false)) mcc = (int)numero(v, false);
            if (valorApos(t, "MNC", v, false)) mnc = (int)numero(v, false);
            if (valorApos(t, "CC", v, true) || valorApos(t, "Color code", v, true) || valorApos(t, "color_code", v, true))
                cc = (int)numero(v, false);
            const size_t ae = t.find("Air encryption:");
            if (ae != std::string::npos) cripto = nomeTea(std::atol(t.c_str() + ae + 15));
            const size_t tp = t.find("TN ");
            const size_t du = t.find("DL_USAGE:");
            if (tp != std::string::npos && du != std::string::npos) {
                const size_t par = t.find('(', tp);
                if (par != std::string::npos) {
                    tn = std::max(1, std::atoi(t.c_str() + par + 1));
                    size_t q = du + 9;
                    while (q < t.size() && t[q] == ' ') ++q;
                    uso = t.substr(q, 1);
                    if (tn >= 1 && tn <= 4) {
                        std::lock_guard<std::mutex> lk(estMutex_);
                        est_.ts[tn - 1] = (uso == "U") ? T("livre") : T("ocupado");
                    }
                }
            }
        }
        acc.erase(0, ini);
        if (acc.size() > 16384) acc.clear();
        {
            std::lock_guard<std::mutex> lk(estMutex_);
            est_.bursts += dB; est_.sb += dSb; est_.ndb += dNdb;
            if (mcc >= 0) est_.mcc = mcc;
            if (mnc >= 0) est_.mnc = mnc;
            if (cc >= 0) est_.cc = cc;
            if (!cripto.empty() && !tetmonCripto_) est_.cripto = cripto;   // o TETMON (ENCINFO) manda mais
        }
        if (!cruas.empty()) {
            if (cruas.size() > 20000) cruas.erase(0, cruas.size() - 20000);   // a tela nao acompanha tudo
            msg(cruas);
            cruas.clear();
        }
    }
    if (vivo_) {
        vivo_ = false;
        filaCv_.notify_all();
        std::lock_guard<std::mutex> lk(estMutex_);
        est_.rodando = false;
        if (est_.erro.empty()) est_.erro = T("o tetra-rx fechou sozinho");
    }
}

// ---------------------------------------------------------------------------
//  TETMON: informacoes da celula, chamadas e os quadros de voz (ACELP)
// ---------------------------------------------------------------------------
static std::string campo(const std::string& payload, const char* chave)
{
    const std::string k = std::string(chave) + ":";
    size_t p = 0;
    while ((p = payload.find(k, p)) != std::string::npos) {
        if (p == 0 || payload[p - 1] == ' ' || payload[p - 1] == '\t' || payload[p - 1] == '\n') {
            const size_t ini = p + k.size();
            size_t fim = ini;
            while (fim < payload.size() && !std::isspace((unsigned char)payload[fim])) ++fim;
            return payload.substr(ini, fim - ini);
        }
        p += k.size();
    }
    return "";
}

void Tetra::lerTetmon()
{
    std::vector<char> buf(65536);
    std::vector<int16_t> pcm;
    double ultNet = 0, ultFreq = 0, ultEnc = 0;
    while (vivo_) {
        const int n = recv((SOCKET)sockTetmon_, buf.data(), (int)buf.size(), 0);
        if (n <= 0) continue;
        const std::string d(buf.data(), (size_t)n);
        { std::lock_guard<std::mutex> lk(estMutex_); est_.tetmon++; }

        // --- voz: "TRA:hex RX:hex [DECR:hex] " + 1380 bytes de ACELP ---
        const size_t tra = d.find("TRA:");
        if (tra != std::string::npos) {
            size_t p = tra + 4;
            auto hex = [&](size_t& q) { size_t s = q; while (q < d.size() && std::isxdigit((unsigned char)d[q])) ++q; return q > s; };
            auto esp = [&](size_t& q) { size_t s = q; while (q < d.size() && std::isspace((unsigned char)d[q])) ++q; return q > s; };
            bool ok = hex(p) && esp(p) && d.compare(p, 3, "RX:") == 0;
            if (ok) { p += 3; ok = hex(p) && esp(p); }
            if (ok && d.compare(p, 5, "DECR:") == 0) {
                size_t q = p + 5;
                if (hex(q) && esp(q)) p = q;
            }
            if (ok && p + kAcelp <= d.size()) {
                if (decodificarAcelp((const uint8_t*)d.data() + p, pcm)) {
                    guardarVoz(pcm.data(), pcm.size());
                    std::lock_guard<std::mutex> lk(estMutex_);
                    est_.voz++;
                    est_.ultVoz = agora();
                }
            }
        }

        // --- metadados ---
        std::string payload;
        const size_t b = d.find("TETMON_begin");
        if (b != std::string::npos) {
            const size_t e = d.find("TETMON_end", b);
            payload = d.substr(b + 12, e == std::string::npos ? std::string::npos : e - b - 12);
        } else {
            const size_t f = d.find("FUNC:");
            if (f == std::string::npos) continue;
            payload = d.substr(f);
        }
        const std::string func = campo(payload, "FUNC");
        const double t = agora();
        std::string linha;
        if (func == "NETINFO1") {
            const long mcc = numero(campo(payload, "MCC"), true), mnc = numero(campo(payload, "MNC"), true);
            const long cc = numero(campo(payload, "CCODE"), true), cr = numero(campo(payload, "CRYPT"), false);
            (void)cr;
            {
                std::lock_guard<std::mutex> lk(estMutex_);
                est_.mcc = (int)mcc; est_.mnc = (int)mnc; est_.cc = (int)cc;
            }
            char m[160];
            std::snprintf(m, sizeof m, T("Celula: MCC %ld  MNC %ld  CC %ld"), mcc, mnc, cc);
            if (m != ultMeta_ || t - ultNet > 60) { ultMeta_ = m; ultNet = t; linha = m; }
        } else if (func == "FREQINFO1") {
            if (t - ultFreq > 30) {
                ultFreq = t;
                const double dl = (double)numero(campo(payload, "DLF"), false), ul = (double)numero(campo(payload, "ULF"), false);
                char m[120];
                if (ul > 0) std::snprintf(m, sizeof m, T("Frequencias da celula: descida %.4f MHz, subida %.4f MHz"), dl / 1e6, ul / 1e6);
                else std::snprintf(m, sizeof m, T("Frequencia da celula (descida): %.4f MHz"), dl / 1e6);
                linha = m;
            }
        } else if (func == "ENCINFO1") {
            const long cr = numero(campo(payload, "CRYPT"), false);
            { std::lock_guard<std::mutex> lk(estMutex_); est_.cripto = nomeTea(cr); tetmonCripto_ = true; }
            if (t - ultEnc > 60) { ultEnc = t; linha = std::string(T("Criptografia da celula: ")) + nomeTea(cr); }
        } else if (func == "DSETUPDEC") {
            linha = T("Chamada: SSI ") + campo(payload, "SSI") + " -> " + campo(payload, "SSI2");
            std::lock_guard<std::mutex> lk(estMutex_);
            est_.ultimaChamada = "SSI " + campo(payload, "SSI") + " -> " + campo(payload, "SSI2");
        } else if (func == "DCONNECTDEC") {
            linha = T("Chamada conectada: SSI ") + campo(payload, "SSI");
        } else if (func == "DTXGRANTDEC") {
            linha = T("Fala liberada para SSI ") + campo(payload, "SSI");
        } else if (func == "DRELEASEDEC" || func == "D-RELEASE") {
            linha = T("Chamada encerrada");
        } else if (func == "DSTATUSDEC") {
            linha = T("Status de SSI ") + campo(payload, "SSI") + ": " + campo(payload, "STATUS");
        } else if (func == "SDSDEC") {
            linha = T("SDS (mensagem curta) de SSI ") + campo(payload, "SSI");
        }
        if (!linha.empty()) msg("[" + horaUtc() + "] " + linha + "\n");
    }
}

// cdecoder.exe | sdecoder.exe persistentes: 1380 bytes entram, 960 saem
bool Tetra::abrirCodec()
{
    const std::wstring pasta = pastaDecoders();
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE aR, aW, bR, bW, cR, cW;
    if (!CreatePipe(&aR, &aW, &sa, 1 << 16)) return false;
    if (!CreatePipe(&bR, &bW, &sa, 1 << 16)) { CloseHandle(aR); CloseHandle(aW); return false; }
    if (!CreatePipe(&cR, &cW, &sa, 1 << 16)) { CloseHandle(aR); CloseHandle(aW); CloseHandle(bR); CloseHandle(bW); return false; }
    SetHandleInformation(aW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(cR, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    auto abre = [&](const wchar_t* exe, HANDLE in, HANDLE out, HANDLE& proc) -> bool {
        std::wstring cmd = L"\"" + pasta + L"\\" + exe + L"\" - -";
        std::vector<wchar_t> l(cmd.begin(), cmd.end()); l.push_back(0);
        STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in; si.hStdOutput = out; si.hStdError = nul;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, l.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, pasta.c_str(), &si, &pi)) return false;
        CloseHandle(pi.hThread); proc = pi.hProcess; return true;
    };
    HANDLE pc = nullptr, ps = nullptr;
    const bool ok = abre(L"cdecoder.exe", aR, bW, pc) && abre(L"sdecoder.exe", bR, cW, ps);
    CloseHandle(aR); CloseHandle(bW); CloseHandle(bR); CloseHandle(cW);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        if (pc) { TerminateProcess(pc, 0); CloseHandle(pc); }
        CloseHandle(aW); CloseHandle(cR);
        return false;
    }
    procC_ = pc; procS_ = ps; codecW_ = aW; codecR_ = cR;
    return true;
}

void Tetra::fecharCodec()
{
    for (void** p : {&procC_, &procS_})
        if (*p) { TerminateProcess((HANDLE)*p, 0); WaitForSingleObject((HANDLE)*p, 1000); CloseHandle((HANDLE)*p); *p = nullptr; }
    if (codecW_) { CloseHandle((HANDLE)codecW_); codecW_ = nullptr; }
    if (codecR_) { CloseHandle((HANDLE)codecR_); codecR_ = nullptr; }
}

bool Tetra::decodificarAcelp(const uint8_t* q, std::vector<int16_t>& pcm)
{
    if (!codecW_ && !abrirCodec()) return false;
    DWORD w = 0;
    size_t resta = kAcelp;
    while (resta) {
        if (!WriteFile((HANDLE)codecW_, q + (kAcelp - resta), (DWORD)resta, &w, nullptr)) { fecharCodec(); return false; }
        resta -= w;
    }
    pcm.resize(kPcm / 2);
    char* d = (char*)pcm.data();
    size_t lido = 0;
    while (lido < kPcm) {
        DWORD r = 0;
        if (!ReadFile((HANDLE)codecR_, d + lido, (DWORD)(kPcm - lido), &r, nullptr) || r == 0) { fecharCodec(); return false; }
        lido += r;
    }
    return true;
}

// voz 8 kHz -> taxa do som do radio
void Tetra::guardarVoz(const int16_t* p, size_t n)
{
    std::lock_guard<std::mutex> lk(vozMutex_);
    for (size_t i = 0; i < n; ++i) caudaVoz_.push_back(p[i] / 32768.f);
    const double passo = 8000.0 / (double)taxaSaida_.load();
    while (true) {
        const size_t i0 = (size_t)posVoz_;
        if (i0 + 1 >= caudaVoz_.size()) break;
        const float fr = (float)(posVoz_ - i0);
        const float v = std::clamp(caudaVoz_[i0] + fr * (caudaVoz_[i0 + 1] - caudaVoz_[i0]), -1.f, 1.f);
        voz_.push_back((int16_t)std::lround(v * 32767.f));
        posVoz_ += passo;
    }
    const size_t usados = std::min((size_t)posVoz_, caudaVoz_.size() - 1);
    caudaVoz_.erase(caudaVoz_.begin(), caudaVoz_.begin() + usados);
    posVoz_ -= (double)usados;
    while (voz_.size() > taxaSaida_.load()) voz_.pop_front();
}

void Tetra::puxarVoz(int16_t* out, size_t n, uint32_t sps)
{
    if (sps >= 8000) taxaSaida_ = sps;
    std::lock_guard<std::mutex> lk(vozMutex_);
    if (!tocando_ && voz_.size() >= taxaSaida_.load() / 5) tocando_ = true;   // 200 ms de folga
    size_t i = 0;
    if (tocando_) {
        for (; i < n && !voz_.empty(); ++i) { out[i] = voz_.front(); voz_.pop_front(); }
        if (voz_.empty()) tocando_ = false;
    }
    for (; i < n; ++i) out[i] = 0;
}

EstadoTetra Tetra::estado()
{
    std::lock_guard<std::mutex> lk(estMutex_);
    return est_;
}

} // namespace masdr
