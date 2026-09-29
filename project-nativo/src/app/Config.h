#pragma once
// ---------------------------------------------------------------------------
//  Config - configuracao PORTATIL do RXSDR Nativo
//
//  Fica num arquivo RXSDR.ini AO LADO do RXSDR.exe: baixa a pasta, roda, e
//  leva a pasta para outro PC com tudo junto (como os SDR# antigos). Nada vai
//  para o Registro do Windows - assim tambem nao se mistura com o RXSDR
//  "de navegador" instalado no mesmo PC (os dois usavam as mesmas chaves).
//
//  So se a pasta nao deixar gravar (ex.: dentro de C:\Arquivos de Programas)
//  o arquivo vai para %APPDATA%\RXSDR-Nativo\RXSDR.ini.
//
//  Formato: chave=valor, uma por linha. Da para abrir no Bloco de Notas.
// ---------------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace masdr {

class Config {
public:
    static Config& instance() { static Config c; return c; }

    // ---- genericos ----------------------------------------------------
    std::string str(const std::string& k, const std::string& def = "") const {
        std::lock_guard<std::mutex> lk(m_);
        auto it = v_.find(k);
        return it == v_.end() ? def : it->second;
    }
    long long num(const std::string& k, long long def = 0) const {
        const std::string s = str(k);
        if (s.empty()) return def;
        return std::strtoll(s.c_str(), nullptr, 10);
    }
    double dbl(const std::string& k, double def = 0) const {
        const std::string s = str(k);
        if (s.empty()) return def;
        return std::strtod(s.c_str(), nullptr);
    }
    bool flag(const std::string& k, bool def = false) const {
        const std::string s = str(k);
        if (s.empty()) return def;
        return s == "1" || s == "true" || s == "sim";
    }
    void set(const std::string& k, const std::string& val) {
        std::lock_guard<std::mutex> lk(m_);
        auto it = v_.find(k);
        if (it != v_.end() && it->second == val) return;
        v_[k] = val; sujo_ = true;
    }
    void set(const std::string& k, long long val) { set(k, std::to_string(val)); }
    void set(const std::string& k, int val)       { set(k, std::to_string(val)); }
    void set(const std::string& k, bool val)      { set(k, std::string(val ? "1" : "0")); }
    void set(const std::string& k, double val) {
        char b[64]; std::snprintf(b, sizeof b, "%.6g", val); set(k, std::string(b));
    }

    // ---- atalhos usados pelo radio --------------------------------------
    std::string device()     const { return str("device", "rtlsdr"); }
    std::string serial()     const { return str("serial", ""); }
    std::string tcpHost()    const { return str("rtltcp_host", "127.0.0.1"); }
    int         tcpPort()    const { return (int)num("rtltcp_port", 1234); }
    uint32_t    sampleRate() const { return (uint32_t)num("sample_rate", 1024000); }
    int         gainTenths() const { return (int)num("gain", 496); }
    bool        agc()        const { return flag("agc", false); }
    bool        biasT()      const { return flag("bias_t", false); }
    int         ppm()        const { return (int)num("ppm", 0); }
    // amostragem direta (ramo Q): "off", "on" ou "auto" (liga abaixo de 24 MHz)
    std::string qmode()      const { return str("qmode", "auto"); }
    bool quadratureEm(uint64_t hz) const {
        const std::string q = qmode();
        if (q == "on") return true;
        if (q == "off") return false;
        return hz < 24000000ULL;
    }
    int  fftSize()     const { return (int)num("fft_size", 8192); }
    bool dcRemove()    const { return flag("dc_remove", false); }
    int  sdrplayIfMode()   const { return (int)num("sdrplay_if_mode", 0); }
    int  sdrplayLna()      const { return (int)num("sdrplay_lna", 9); }
    int  sdrplayIfGain()   const { return (int)num("sdrplay_if_gain", 40); }
    bool sdrplayIfAgc()    const { return flag("sdrplay_if_agc", false); }
    int  sdrplayBw()       const { return (int)num("sdrplay_bw", -1); }

    std::string caminho() const { return arquivo_; }

    void carregar() {
        std::lock_guard<std::mutex> lk(m_);
        escolherArquivo();
        std::ifstream f(arquivo_);
        std::string linha;
        while (std::getline(f, linha)) {
            if (!linha.empty() && linha.back() == '\r') linha.pop_back();
            if (linha.empty() || linha[0] == '#' || linha[0] == ';' || linha[0] == '[') continue;
            const auto p = linha.find('=');
            if (p == std::string::npos) continue;
            v_[linha.substr(0, p)] = linha.substr(p + 1);
        }
        sujo_ = false;
    }

    // Grava so se mudou algo. Grava num temporario e troca: se o PC desligar
    // no meio, o arquivo antigo continua inteiro.
    void salvar(bool forcar = false) {
        std::lock_guard<std::mutex> lk(m_);
        if (!sujo_ && !forcar) return;
        if (arquivo_.empty()) escolherArquivo();
        const std::string tmp = arquivo_ + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f) return;
            f << "# RXSDR Nativo - configuracao (pode editar com o programa FECHADO)\n";
            for (const auto& kv : v_) f << kv.first << '=' << kv.second << '\n';
        }
        MoveFileExA(tmp.c_str(), arquivo_.c_str(), MOVEFILE_REPLACE_EXISTING);
        sujo_ = false;
    }

private:
    Config() = default;

    static std::string pastaDoExe() {
        char p[MAX_PATH]{};
        GetModuleFileNameA(nullptr, p, MAX_PATH);
        std::string d(p);
        const auto pos = d.find_last_of("\\/");
        return pos == std::string::npos ? std::string(".") : d.substr(0, pos);
    }

    void escolherArquivo() {
        if (!arquivo_.empty()) return;
        const std::string ao_lado = pastaDoExe() + "\\RXSDR.ini";
        // Da para gravar ao lado do exe?
        {
            std::ofstream t(ao_lado, std::ios::app);
            if (t) { arquivo_ = ao_lado; return; }
        }
        char ad[MAX_PATH]{};
        if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, ad) == S_OK) {
            std::string d = std::string(ad) + "\\RXSDR-Nativo";
            CreateDirectoryA(d.c_str(), nullptr);
            arquivo_ = d + "\\RXSDR.ini";
        } else {
            arquivo_ = ao_lado;
        }
    }

    mutable std::mutex m_;
    std::map<std::string, std::string> v_;
    std::string arquivo_;
    bool sujo_ = false;
};

} // namespace masdr
