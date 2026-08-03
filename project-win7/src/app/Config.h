#pragma once
// Config.h — Armazena configurações no Windows Registry.
// Mesmos keys do QSettings("PU1XTB","RXSDR") para compatibilidade com o build Qt6.
// Caminho: HKCU\Software\PU1XTB\RXSDR\<section>\<key>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace masdr {

class Config {
public:
    static Config& instance() { static Config c; return c; }

    // ── getters ──────────────────────────────────────────────────────────────
    uint16_t    httpPort()       const { return (uint16_t)getInt("server/httpPort", 8080); }
    uint16_t    wsPort()         const { return (uint16_t)getInt("server/wsPort",   8081); }

    std::string lastDevice()     const { return getString("device/last",        "rtlsdr"); }
    std::string lastSerial()     const { return getString("device/serial",      ""); }
    std::string rtltcpHost()     const { return getString("device/rtltcpHost",  "127.0.0.1"); }
    int         rtltcpPort()     const { return getInt("device/rtltcpPort",     1234); }
    uint64_t    vfoA()           const { return getUint64("vfo/A",  50173000ULL); }
    uint64_t    vfoB()           const { return getUint64("vfo/B",  14283000ULL); }
    std::string mode()           const { return getString("vfo/mode",           "USB"); }
    int         gainTenths()     const { return getInt("rx/gain",               496); }
    uint32_t    sampleRate()     const { return (uint32_t)getInt("rx/sps",      2048000); }
    std::string wfPalette()      const { return getString("ui/palette",         "classic"); }

    int  sdrplayIfMode()    const { return getInt("device/sdrplayIfMode",    0); }
    int  sdrplayLnaState()  const { return getInt("device/sdrplayLnaState",  9); }
    int  sdrplayIfGain()    const { return getInt("device/sdrplayIfGain",    40); }
    bool sdrplayIfAgc()     const { return getBool("device/sdrplayIfAgc",    false); }
    int  sdrplayBw()        const { return getInt("device/sdrplayBw",        -1); }

    bool agc()          const { return getBool("rx/agc",        false); }
    bool biasT()        const { return getBool("rx/bias",        false); }
    bool quadrature()   const { return getBool("rx/quadrature",  false); }
    int  ppm()          const { return getInt("rx/ppm",          0); }
    bool iqCorrection() const { return getBool("rx/iqCorrection", true); }

    double smeterHfOffset()   const { return getDouble("smeter/hfOffset",  0.0); }
    double smeterVhfOffset()  const { return getDouble("smeter/vhfOffset", 0.0); }
    int    smeterS9Hf()       const { return getInt("smeter/s9Hf",    -94); }
    int    smeterS9Vhf()      const { return getInt("smeter/s9Vhf",   -93); }
    int    smeterHfEmpty()    const { return getInt("smeter/hfEmpty",   36); }
    int    smeterVhfEmpty()   const { return getInt("smeter/vhfEmpty",  36); }
    bool   smeterRmsAligned() const { return getBool("smeter/rmsAligned", true); }

    // ── setters ──────────────────────────────────────────────────────────────
    void setVfoA(uint64_t v)            { setUint64("vfo/A", v); }
    void setVfoB(uint64_t v)            { setUint64("vfo/B", v); }
    void setMode(const std::string& m)  { setString("vfo/mode", m); }
    void setLastDevice(const std::string& t){ setString("device/last", t); }
    void setLastSerial(const std::string& s){ setString("device/serial", s); }
    void setRtltcpHost(const std::string& h){ setString("device/rtltcpHost", h); }
    void setRtltcpPort(int p)           { setInt("device/rtltcpPort", p); }
    void setGain(int t)                 { setInt("rx/gain", t); }
    void setSampleRate(uint32_t r)      { setInt("rx/sps", (int)r); }
    void setAgc(bool v)                 { setBool("rx/agc", v); }
    void setBiasT(bool v)               { setBool("rx/bias", v); }
    void setQuadrature(bool v)          { setBool("rx/quadrature", v); }
    void setPpm(int v)                  { setInt("rx/ppm", v); }
    void setIqCorrection(bool v)        { setBool("rx/iqCorrection", v); }

    void setSdrplayIfMode(int v)   { setInt("device/sdrplayIfMode",   v); }
    void setSdrplayLnaState(int v) { setInt("device/sdrplayLnaState", v); }
    void setSdrplayIfGain(int v)   { setInt("device/sdrplayIfGain",   v); }
    void setSdrplayIfAgc(bool v)   { setBool("device/sdrplayIfAgc",   v); }
    void setSdrplayBw(int v)       { setInt("device/sdrplayBw",       v); }

    void setSmeterHfOffset(double v)  { setDouble("smeter/hfOffset",  v); }
    void setSmeterVhfOffset(double v) { setDouble("smeter/vhfOffset", v); }
    void setSmeterS9Hf(int v)         { setInt("smeter/s9Hf",    v); }
    void setSmeterS9Vhf(int v)        { setInt("smeter/s9Vhf",   v); }
    void setSmeterHfEmpty(int v)      { setInt("smeter/hfEmpty",  v); }
    void setSmeterVhfEmpty(int v)     { setInt("smeter/vhfEmpty", v); }
    void setSmeterRmsAligned(bool v)  { setBool("smeter/rmsAligned", v); }

    void sync() { /* Registry auto-syncs */ }

private:
    Config() = default;

    // Converte "section/key" → subkey + valueName
    static void splitKey(const std::string& dotKey, std::string& subkey, std::string& valName) {
        const std::string base = "Software\\PU1XTB\\RXSDR\\";
        auto pos = dotKey.find('/');
        if (pos != std::string::npos) {
            subkey  = base + dotKey.substr(0, pos);
            valName = dotKey.substr(pos + 1);
        } else {
            subkey  = base;
            valName = dotKey;
        }
    }

    std::string getString(const std::string& key, const std::string& def) const {
        std::string sub, val;
        splitKey(key, sub, val);
        HKEY hk;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
            return def;
        char buf[1024]{};
        DWORD sz = sizeof(buf);
        bool ok = RegQueryValueExA(hk, val.c_str(), nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS;
        RegCloseKey(hk);
        return ok ? std::string(buf, sz > 0 ? sz - 1 : 0) : def;
    }

    void setString(const std::string& key, const std::string& v) const {
        std::string sub, val;
        splitKey(key, sub, val);
        HKEY hk; DWORD disp;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, &disp) == ERROR_SUCCESS) {
            RegSetValueExA(hk, val.c_str(), 0, REG_SZ,
                           (const BYTE*)v.c_str(), (DWORD)(v.size() + 1));
            RegCloseKey(hk);
        }
    }

    int getInt(const std::string& key, int def) const {
        std::string sub, val;
        splitKey(key, sub, val);
        HKEY hk;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
            return def;
        DWORD data = 0; DWORD sz = sizeof(data); DWORD type = 0;
        bool ok = RegQueryValueExA(hk, val.c_str(), nullptr, &type, (LPBYTE)&data, &sz) == ERROR_SUCCESS;
        RegCloseKey(hk);
        return ok ? (int)(int32_t)data : def;
    }

    void setInt(const std::string& key, int v) const {
        std::string sub, val;
        splitKey(key, sub, val);
        HKEY hk; DWORD disp;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, &disp) == ERROR_SUCCESS) {
            DWORD d = (DWORD)(int32_t)v;
            RegSetValueExA(hk, val.c_str(), 0, REG_DWORD, (const BYTE*)&d, sizeof(d));
            RegCloseKey(hk);
        }
    }

    bool getBool(const std::string& key, bool def) const {
        return getInt(key, def ? 1 : 0) != 0;
    }
    void setBool(const std::string& key, bool v) const {
        setInt(key, v ? 1 : 0);
    }

    uint64_t getUint64(const std::string& key, uint64_t def) const {
        std::string sub, val;
        splitKey(key, sub, val);
        HKEY hk;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
            return def;
        uint64_t data = 0; DWORD sz = sizeof(data); DWORD type = 0;
        bool ok = RegQueryValueExA(hk, val.c_str(), nullptr, &type, (LPBYTE)&data, &sz) == ERROR_SUCCESS;
        RegCloseKey(hk);
        return ok ? data : def;
    }

    void setUint64(const std::string& key, uint64_t v) const {
        std::string sub, val;
        splitKey(key, sub, val);
        HKEY hk; DWORD disp;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, &disp) == ERROR_SUCCESS) {
            RegSetValueExA(hk, val.c_str(), 0, REG_QWORD, (const BYTE*)&v, sizeof(v));
            RegCloseKey(hk);
        }
    }

    double getDouble(const std::string& key, double def) const {
        // Armazenado como string com sufixo __dbl
        std::string s = getString(key + "__dbl", "");
        if (s.empty()) return def;
        try { return std::stod(s); } catch(...) { return def; }
    }
    void setDouble(const std::string& key, double v) const {
        std::ostringstream ss; ss << std::setprecision(15) << v;
        setString(key + "__dbl", ss.str());
    }
};

} // namespace masdr
