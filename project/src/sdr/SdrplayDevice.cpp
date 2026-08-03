// SdrplayDevice.cpp — Suporte a hardware SDRplay via API v3 (carregamento dinâmico)
//
// A DLL sdrplay_api.dll NÃO é redistribuída com o RXSDR.
// Ela faz parte do pacote oficial "SDRplay RSP API" disponível em:
//   https://www.sdrplay.com/api/
//
// Arquitetura:
//   ┌─────────────────┐   LoadLibrary   ┌────────────────────┐   USB/HID   ┌──────────┐
//   │  RXSDR.exe  │ ─────────────►  │ sdrplay_api.dll    │ ─────────►  │ RSP hw   │
//   └─────────────────┘                 │ SDRplay API Service │             └──────────┘
//                                       └────────────────────┘
//
// Se a DLL não for encontrada, o RXSDR continua funcionando normalmente
// com RTL-SDR e RTL-TCP; apenas a opção SDRplay ficará indisponível.

#include "SdrplayDevice.h"
#include "../util/Logger.h"
#include <QThread>

#if SDRPLAY_AVAILABLE

// ─── Abstração de carregamento dinâmico (Windows: LoadLibrary / Linux: dlopen) ───
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HMODULE LibHandle;
#define LIB_LOAD(path)       ::LoadLibraryA(path)
#define LIB_SYM(h, name)    reinterpret_cast<void*>(::GetProcAddress(h, name))
#define LIB_FREE(h)          ::FreeLibrary(h)
#define LIB_NULL             nullptr
#else
#include <dlfcn.h>
typedef void* LibHandle;
#define LIB_LOAD(path)       dlopen(path, RTLD_NOW)
#define LIB_SYM(h, name)    dlsym(h, name)
#define LIB_FREE(h)          dlclose(h)
#define LIB_NULL             nullptr
// HANDLE é usado pela API SDRplay nos callbacks (igual a void* no Linux)
typedef void* HANDLE;
#endif

// Incluir o header apenas neste .cpp (não exposto ao resto do projeto)
#include "sdrplay_api.h"

// Logger está em namespace masdr — importar para uso nos escopos globais
using masdr::Logger;

#include <cstring>
#include <vector>
#include <complex>
#include <mutex>

// ─── Carregador dinâmico da API SDRplay ──────────────────────────────────────────

struct SdrplayApiLoader {
    LibHandle dll = LIB_NULL;

    // Ponteiros de função (tipos definidos em sdrplay_api.h)
    sdrplay_api_Open_t             Open            = nullptr;
    sdrplay_api_Close_t            Close           = nullptr;
    sdrplay_api_ApiVersion_t       ApiVersion      = nullptr;
    sdrplay_api_LockDeviceApi_t    LockDeviceApi   = nullptr;
    sdrplay_api_UnlockDeviceApi_t  UnlockDeviceApi = nullptr;
    sdrplay_api_GetDevices_t       GetDevices      = nullptr;
    sdrplay_api_SelectDevice_t     SelectDevice    = nullptr;
    sdrplay_api_ReleaseDevice_t    ReleaseDevice   = nullptr;
    sdrplay_api_GetErrorString_t   GetErrorString  = nullptr;
    sdrplay_api_GetDeviceParams_t  GetDeviceParams = nullptr;
    sdrplay_api_Init_t             Init            = nullptr;
    sdrplay_api_Uninit_t           Uninit          = nullptr;
    sdrplay_api_Update_t           Update          = nullptr;

    bool load() {
        if (dll) return true;  // já carregado

#ifdef _WIN32
        static const char* s_paths[] = {
            "sdrplay_api.dll",
            "C:\\RXSDR\\sdrpp_windows_x64\\sdrplay_api.dll",
            "C:\\Program Files\\SDRplay\\API\\x64\\sdrplay_api.dll",
            "C:\\Users\\Ruben\\Radioamador\\SDRplay API\\API\\x64\\sdrplay_api.dll",
            "C:\\Program Files (x86)\\SDRplay\\API\\x86\\sdrplay_api.dll",
            "C:\\Users\\Ruben\\Radioamador\\SDRplay API\\API\\x86\\sdrplay_api.dll",
            nullptr
        };
#else
        // Linux: a API SDRplay instala libsdrplay_api.so em /usr/local/lib
        static const char* s_paths[] = {
            "libsdrplay_api.so",
            "/usr/local/lib/libsdrplay_api.so",
            "/usr/lib/libsdrplay_api.so",
            "/usr/lib/aarch64-linux-gnu/libsdrplay_api.so",
            "/usr/lib/arm-linux-gnueabihf/libsdrplay_api.so",
            nullptr
        };
#endif

        for (int i = 0; s_paths[i]; ++i) {
            dll = LIB_LOAD(s_paths[i]);
            if (dll) {
                Logger::info(QString("SDRplay: API carregada — %1")
                    .arg(QString::fromLocal8Bit(s_paths[i])));
                break;
            }
        }

        if (!dll) {
            Logger::error(
                "SDRplay: biblioteca não encontrada.\n"
#ifdef _WIN32
                "  Para usar hardware SDRplay, instale a API oficial em:\n"
                "  https://www.sdrplay.com/api/  →  'SDRplay RSP API Windows 3.15'"
#else
                "  Para usar hardware SDRplay no Linux, instale a API oficial em:\n"
                "  https://www.sdrplay.com/api/  →  'SDRplay RSP API Linux ARM'"
#endif
            );
            return false;
        }

        // Macro para carregar e validar cada símbolo
        auto loadSym = [&](const char* name, void** ptr) -> bool {
            *ptr = LIB_SYM(dll, name);
            if (!*ptr) {
                Logger::error(QString("SDRplay: símbolo ausente na biblioteca: %1")
                    .arg(QString::fromLocal8Bit(name)));
                unload();
                return false;
            }
            return true;
        };

#define SDRPLAY_LOAD(fn) \
    if (!loadSym("sdrplay_api_" #fn, reinterpret_cast<void**>(&fn))) return false

        SDRPLAY_LOAD(Open);
        SDRPLAY_LOAD(Close);
        SDRPLAY_LOAD(ApiVersion);
        SDRPLAY_LOAD(LockDeviceApi);
        SDRPLAY_LOAD(UnlockDeviceApi);
        SDRPLAY_LOAD(GetDevices);
        SDRPLAY_LOAD(SelectDevice);
        SDRPLAY_LOAD(ReleaseDevice);
        SDRPLAY_LOAD(GetErrorString);
        SDRPLAY_LOAD(GetDeviceParams);
        SDRPLAY_LOAD(Init);
        SDRPLAY_LOAD(Uninit);
        SDRPLAY_LOAD(Update);
#undef SDRPLAY_LOAD

        // Verificar versão
        float ver = 0.0f;
        if (ApiVersion(&ver) == sdrplay_api_Success) {
            Logger::info(QString("SDRplay: versão da API: %1").arg(static_cast<double>(ver), 0, 'f', 2));
            if (ver < 3.06f) {
                Logger::warn("SDRplay: versão da API muito antiga. Recomendado >= 3.06");
            }
        }

        return true;
    }

    void unload() {
        if (dll) {
            LIB_FREE(dll);
            dll = LIB_NULL;
        }
        Open = nullptr; Close = nullptr; ApiVersion = nullptr;
        LockDeviceApi = nullptr; UnlockDeviceApi = nullptr;
        GetDevices = nullptr; SelectDevice = nullptr; ReleaseDevice = nullptr;
        GetErrorString = nullptr; GetDeviceParams = nullptr;
        Init = nullptr; Uninit = nullptr; Update = nullptr;
    }

    bool isLoaded() const { return dll != LIB_NULL && Open != nullptr; }

    const char* errStr(sdrplay_api_ErrT e) const {
        return (GetErrorString && e != sdrplay_api_Success)
               ? GetErrorString(e)
               : "ok";
    }
};

// Singleton do loader (uma instância por processo)
static SdrplayApiLoader s_api;

// Instância ativa global para recuperação no enumerate se a API falhar ou omitir o dispositivo ativo
static std::atomic<masdr::SdrplayDevice*> s_activeDevice{ nullptr };
static std::mutex s_callbackMutex;

static bool ensureApiOpen() {
    static bool s_apiOpened = false;
    if (s_apiOpened) return true;
    if (!s_api.load()) return false;
    sdrplay_api_ErrT err = s_api.Open();
    if (err != sdrplay_api_Success) {
        Logger::error(QString("SDRplay: Open() falhou — %1").arg(s_api.errStr(err)));
        return false;
    }
    s_apiOpened = true;
    Logger::info("SDRplay: Open() OK (sessão de API persistente aberta)");
    return true;
}

// ─── Mapeamento hwVer → nome ──────────────────────────────────────────────────────
static QString hwVerToName(unsigned char hwVer) {
    switch (hwVer) {
        case SDRPLAY_RSP1_ID:    return QStringLiteral("SDRplay RSP1");
        case SDRPLAY_RSP1A_ID:   return QStringLiteral("SDRplay RSP1A");
        case SDRPLAY_RSP1B_ID:   return QStringLiteral("SDRplay RSP1B");
        case SDRPLAY_RSP2_ID:    return QStringLiteral("SDRplay RSP2");
        case SDRPLAY_RSPduo_ID:  return QStringLiteral("SDRplay RSPduo");
        case SDRPLAY_RSPdx_ID:   return QStringLiteral("SDRplay RSPdx");
        case SDRPLAY_RSPdxR2_ID: return QStringLiteral("SDRplay RSPdx-R2");
        default:
            return QString("SDRplay (hwVer=%1)").arg(static_cast<int>(hwVer));
    }
}

// Escolher largura de banda segura baseada no sample rate atual (evitando alias e incompatibilidade na API)
static sdrplay_api_Bw_MHzT selectBandwidth(uint32_t sps) {
    if (sps < 300000)        return sdrplay_api_BW_0_200;
    else if (sps < 600000)   return sdrplay_api_BW_0_300;
    else if (sps < 1536000)  return sdrplay_api_BW_0_600;
    else if (sps < 5000000)  return sdrplay_api_BW_1_536;
    else if (sps < 6000000)  return sdrplay_api_BW_5_000;
    else if (sps < 7000000)  return sdrplay_api_BW_6_000;
    else if (sps < 8000000)  return sdrplay_api_BW_7_000;
    else                     return sdrplay_api_BW_8_000;
}

// ─── Configurar parâmetros do canal RX ───────────────────────────────────────────
static void configChannel(sdrplay_api_RxChannelParamsT* ch,
                           uint64_t freqHz,
                           uint32_t sps,
                           uint32_t decimationFactor,
                           int ifMode,
                           int lnaState,
                           int ifGain,
                           bool ifAgc,
                           int bwSetting,
                           unsigned char hwVer)
{
    if (!ch) return;

    ch->tunerParams.rfFreq.rfHz   = static_cast<double>(freqHz);
    
    // Compute LNAstate based on hwVer and lnaState
    // RSP1=4 estados (0-3), RSP2=9 estados (0-8), demais=10 (0-9)
    // Estado 0 = máximo ganho LNA, estado N = mínimo ganho LNA
    // Mapeamento proporcional: slider 0 (min UI) → estado maxLnaState (min ganho)
    //                          slider 9 (max UI) → estado 0 (max ganho)
    int maxLnaState = 9;
    if (hwVer == SDRPLAY_RSP1_ID) {
        maxLnaState = 3;
    } else if (hwVer == SDRPLAY_RSP2_ID) {
        maxLnaState = 8;
    }
    // Escala proporcional: lnaState 0..9 (UI) → LNAstate maxLnaState..0 (API)
    int targetLna = maxLnaState - (lnaState * maxLnaState / 9);
    if (targetLna < 0) targetLna = 0;
    if (targetLna > maxLnaState) targetLna = maxLnaState;
    ch->tunerParams.gain.LNAstate = static_cast<unsigned char>(targetLna);

    // EXTENDED_MIN_GR permite redução mínima de 0 dB (vs 20 dB no NORMAL)
    // Isso adiciona até 20 dB de ganho extra disponível no baseband
    ch->tunerParams.gain.minGr = sdrplay_api_EXTENDED_MIN_GR;

    sdrplay_api_Bw_MHzT bw = selectBandwidth(sps);

    // IF Mode
    sdrplay_api_If_kHzT targetIf = sdrplay_api_IF_Zero;
    if (ifMode == 0) {
        targetIf = sdrplay_api_IF_Zero;
        if (bwSetting != -1) {
            bw = static_cast<sdrplay_api_Bw_MHzT>(bwSetting);
        } else {
            bw = selectBandwidth(sps);
        }
    } else if (ifMode == 1) {
        targetIf = sdrplay_api_IF_2_048;
        bw = sdrplay_api_BW_1_536;
    } else if (ifMode == 2) {
        targetIf = sdrplay_api_IF_2_048;
        bw = sdrplay_api_BW_5_000;
    } else if (ifMode == 3) {
        targetIf = sdrplay_api_IF_1_620;
        bw = sdrplay_api_BW_1_536;
    } else if (ifMode == 4) {
        targetIf = sdrplay_api_IF_0_450;
        bw = sdrplay_api_BW_0_600;
    } else if (ifMode == 5) {
        targetIf = sdrplay_api_IF_0_450;
        bw = sdrplay_api_BW_0_300;
    } else if (ifMode == 6) {
        targetIf = sdrplay_api_IF_0_450;
        bw = sdrplay_api_BW_0_200;
    }

    // Garantir que a largura de banda não exceda a taxa de amostragem se em modo automático (apenas para Zero-IF)
    if (ifMode == 0 && bwSetting == -1 && static_cast<uint32_t>(bw) * 1000 > sps) {
        bw = selectBandwidth(sps);
    }

    ch->tunerParams.bwType = bw;
    ch->tunerParams.ifType = targetIf;
    ch->tunerParams.loMode = sdrplay_api_LO_Auto;

    // Configurar AGC / ganho manual
    if (ifAgc) {
        ch->ctrlParams.agc.enable = sdrplay_api_AGC_50HZ;
        // gRdB inicial alto (pouco ganho), o AGC vai ajustar automaticamente
        // Usar EXTENDED permite o AGC trabalhar em faixa completa 0-59
        ch->tunerParams.gain.gRdB = 40;
    } else {
        ch->ctrlParams.agc.enable = sdrplay_api_AGC_DISABLE;
        // Com EXTENDED_MIN_GR, mapeamento: ifGain 20..59 (UI) → gRdB 59..0 (API)
        // ifGain=59 (max UI) → gRdB=0  (ganho máximo, redução mínima)
        // ifGain=20 (min UI) → gRdB=59 (ganho mínimo, redução máxima)
        int targetGr = 59 - (ifGain - 20) * 59 / 39;
        targetGr -= 1; // +1 dB de ganho RF (API SDRplay): -1 dB de reducao de ganho
        if (targetGr < 0)  targetGr = 0;
        if (targetGr > 59) targetGr = 59;
        ch->tunerParams.gain.gRdB = targetGr;
    }
    // setPoint -30 dBfs: nível alvo do AGC — evita o muting causado pelo -60
    // Valores recomendados pela SDRplay: entre -30 e -20 dBfs
    ch->ctrlParams.agc.setPoint_dBfs = -29; // +1 dB de ganho RF (API SDRplay) no modo IF AGC
    ch->ctrlParams.agc.attack_ms          = 0;
    ch->ctrlParams.agc.decay_ms           = 0;
    ch->ctrlParams.agc.decay_delay_ms     = 0;
    ch->ctrlParams.agc.decay_threshold_dB = 0;

    // Correção de DC e imbalance IQ ligada
    ch->ctrlParams.dcOffset.DCenable = 1;
    ch->ctrlParams.dcOffset.IQenable = 1;

    if (decimationFactor > 1) {
        ch->ctrlParams.decimation.enable          = 1;
        ch->ctrlParams.decimation.decimationFactor = static_cast<unsigned char>(decimationFactor);
        ch->ctrlParams.decimation.wideBandSignal  = (ifMode == 0) ? 1 : 0;
    } else {
        ch->ctrlParams.decimation.enable          = 0;
        ch->ctrlParams.decimation.decimationFactor = 1;
        ch->ctrlParams.decimation.wideBandSignal  = 0;
    }
}

// ─── processBatchSamples ─────────────────────────────────────────────────────────
// Converte amostras brutas short do ADC para complex<float>, sem modificação.
//
// O SDRplay RSP entrega IQ com espectro NORMAL (convenção padrão I+jQ):
//   sinais acima do centro → frequência positiva no IQ.
//
// A correção de convenção de espectro (inversão de Q) é feita em Application.cpp,
// SOMENTE na entrada do demodulador e APÓS o VFO DDC — NÃO aqui.
// Isso garante que o FFT/waterfall receba IQ original (frequências corretas no display)
// e que o VFO DDC funcione no IQ não-invertido (deslocamento na direção certa).
//
void masdr::SdrplayDevice::processBatchSamples(
    const short* xi, const short* xq,
    unsigned numSamples,
    std::vector<std::complex<float>>& buf)
{
    // A API SDRplay v3 entrega shorts em [±32767] independente do ADC interno (12/14 bits).
    // Fator de escala padrão de referência (SDR++, GQRX, CubicSDR): 1/32768.
    constexpr float kScale = 1.0f / 32768.0f;
    buf.resize(numSamples);

    // IQ normal — sem modificação. Ver Application.cpp para a correção de espectro.
    for (unsigned i = 0; i < numSamples; ++i) {
        buf[i] = { xi[i] * kScale, xq[i] * kScale };
    }
}

// ─── Callback de stream (chamado pela thread da API SDRplay) ──────────────────────
static void SDRPLAY_streamCb(
    short* xi, short* xq,
    sdrplay_api_StreamCbParamsT* /*params*/,
    unsigned int numSamples,
    unsigned int /*reset*/,
    void* cbContext)
{
    std::lock_guard<std::mutex> lk(s_callbackMutex);
    auto* dev = static_cast<masdr::SdrplayDevice*>(cbContext);
    if (!dev || s_activeDevice.load() != dev || !dev->isRunning() || numSamples == 0) return;

    std::vector<std::complex<float>> buf;
    dev->processBatchSamples(xi, xq, numSamples, buf);
    dev->deliverSamples(buf.data(), buf.size());
}

// ─── Callback de eventos ──────────────────────────────────────────────────────────
static void SDRPLAY_eventCb(
    sdrplay_api_EventT eventId,
    sdrplay_api_TunerSelectT /*tuner*/,
    sdrplay_api_EventParamsT* params,
    void* /*cbContext*/)
{
    switch (eventId) {
        case sdrplay_api_GainChange:
            Logger::info(QString("SDRplay: ganho ajustado — gRdB=%1, LNA=%2")
                .arg(params->gainParams.gRdB)
                .arg(params->gainParams.lnaGRdB));
            break;
        case sdrplay_api_PowerOverloadChange:
            if (params->powerOverloadParams.powerOverloadChangeType
                == sdrplay_api_Overload_Detected)
                Logger::warn("SDRplay: SOBRECARGA detectada — reduza o ganho!");
            else
                Logger::info("SDRplay: sobrecarga corrigida.");
            break;
        case sdrplay_api_DeviceRemoved:
            Logger::error("SDRplay: dispositivo removido durante operação!");
            break;
        default:
            break;
    }
}

#endif // SDRPLAY_AVAILABLE

// ─── SdrplayDevice ────────────────────────────────────────────────────────────────

namespace masdr {

SdrplayDevice::SdrplayDevice() = default;

SdrplayDevice::~SdrplayDevice()
{
    close();
}

std::vector<SdrplayInfo> SdrplayDevice::enumerate()
{
    std::vector<SdrplayInfo> out;

#if SDRPLAY_AVAILABLE
    if (!ensureApiOpen()) {
        // Se a API falhou porque há um dispositivo ativo, retorna suas informações
        SdrplayDevice* active = s_activeDevice.load();
        if (active && !active->serial().isEmpty()) {
            SdrplayInfo info;
            info.serial = active->serial();
            info.name   = active->name();
            out.push_back(info);
        }
        return out;
    }

    sdrplay_api_ErrT err = sdrplay_api_Success;
    s_api.LockDeviceApi();

    sdrplay_api_DeviceT devs[SDRPLAY_MAX_DEVICES];
    unsigned int nDevs = 0;

    // Retry: a API SDRplay v3 pode retornar lista vazia na 1ª chamada após Open()
    // Tentamos até 3x com 200ms de intervalo para garantir detecção do hardware
    for (int attempt = 0; attempt < 3; ++attempt) {
        nDevs = 0;
        err = s_api.GetDevices(devs, &nDevs, SDRPLAY_MAX_DEVICES);
        if (err == sdrplay_api_Success && nDevs > 0) break;
        if (attempt < 2) {
            QThread::msleep(200);
            Logger::info(QString("SDRplay: enumerate tentativa %1 — nDevs=%2 err=%3")
                .arg(attempt + 2).arg(nDevs).arg(s_api.errStr(err)));
        }
    }

    Logger::info(QString("SDRplay: enumerate encontrou %1 dispositivo(s)").arg(nDevs));
    if (err == sdrplay_api_Success) {
        for (unsigned int i = 0; i < nDevs; ++i) {
            SdrplayInfo info;
            info.serial = QString::fromLatin1(devs[i].SerNo);
            info.name   = hwVerToName(devs[i].hwVer);
            Logger::info(QString("  [%1] %2 (s/n: %3, hwVer=%4)")
                .arg(i).arg(info.name).arg(info.serial).arg(devs[i].hwVer));
            out.push_back(info);
        }
    } else {
        Logger::error(QString("SDRplay: enumerate: GetDevices falhou — %1").arg(s_api.errStr(err)));
    }

    s_api.UnlockDeviceApi();

    // Garante que dispositivo ativo apareça mesmo se a API omitir dispositivos em uso
    SdrplayDevice* active = s_activeDevice.load();
    if (active && !active->serial().isEmpty()) {
        bool found = false;
        for (const auto& d : out) {
            if (d.serial == active->serial()) { found = true; break; }
        }
        if (!found) {
            SdrplayInfo info;
            info.serial = active->serial();
            info.name   = active->name();
            out.push_back(info);
        }
    }
#endif

    return out;
}

bool SdrplayDevice::open(const QString& serial)
{
#if SDRPLAY_AVAILABLE
    if (!ensureApiOpen()) {
        Logger::error("SDRplay: Open() falhou ao iniciar a API persistente.");
        return false;
    }

    Logger::info(QString("SDRplay: tentando abrir dispositivo serial='%1'").arg(serial));
    Logger::info("SDRplay: Open() OK");

    sdrplay_api_ErrT err = sdrplay_api_Success;
    s_api.LockDeviceApi();

    sdrplay_api_DeviceT devs[SDRPLAY_MAX_DEVICES];
    unsigned int nDevs = 0;

    // Retry: a API às vezes precisa de uma tentativa extra após Open()
    for (int attempt = 0; attempt < 3; ++attempt) {
        nDevs = 0;
        err = s_api.GetDevices(devs, &nDevs, SDRPLAY_MAX_DEVICES);
        if (err == sdrplay_api_Success && nDevs > 0) break;
        if (attempt < 2) QThread::msleep(200);
    }

    Logger::info(QString("SDRplay: GetDevices encontrou %1 dispositivo(s)").arg(nDevs));
    for (unsigned int i = 0; i < nDevs; ++i) {
        Logger::info(QString("  [%1] hwVer=%2 serial='%3'")
            .arg(i).arg(devs[i].hwVer).arg(QString::fromLatin1(devs[i].SerNo)));
    }

    if (err != sdrplay_api_Success || nDevs == 0) {
        s_api.UnlockDeviceApi();
        Logger::error(QString("SDRplay: nenhum dispositivo encontrado — err=%1").arg(s_api.errStr(err)));
        Logger::error("  → Verifique o cabo USB e se o driver WinUSB/Zadig está instalado.");
        return false;
    }

    // Selecionar o dispositivo (por serial ou o primeiro disponível)
    int idx = -1;
    for (unsigned int i = 0; i < nDevs; ++i) {
        if (serial.isEmpty() ||
            serial == QString::fromLatin1(devs[i].SerNo)) {
            idx = static_cast<int>(i);
            break;
        }
    }

    // Se não encontrou por serial exato, mas há dispositivos conectados,
    // usamos o primeiro disponível como fallback robusto para evitar falha.
    if (idx < 0 && nDevs > 0) {
        idx = 0;
        Logger::warn(QString("SDRplay: dispositivo com serial '%1' nao encontrado. Usando '%2' como fallback.")
            .arg(serial, QString::fromLatin1(devs[0].SerNo)));
    }

    if (idx < 0) {
        s_api.UnlockDeviceApi();
        Logger::error(QString("SDRplay: dispositivo '%1' não encontrado.").arg(serial));
        return false;
    }

    auto* devCopy = new sdrplay_api_DeviceT;
    std::memcpy(devCopy, &devs[idx], sizeof(sdrplay_api_DeviceT));
    devStruct_  = devCopy;

    serial_ = QString::fromLatin1(devCopy->SerNo);
    name_   = hwVerToName(devCopy->hwVer);

    err = s_api.SelectDevice(devCopy);
    s_api.UnlockDeviceApi();

    if (err != sdrplay_api_Success) {
        Logger::error(QString("SDRplay: SelectDevice falhou — %1").arg(s_api.errStr(err)));
        delete devCopy;
        devStruct_ = nullptr;
        devHandle_ = nullptr;
        return false;
    }
    Logger::info(QString("SDRplay: SelectDevice OK — %1 (s/n: %2)").arg(name_).arg(serial_));

    devHandle_  = devCopy->dev;

    // Obter ponteiro de parâmetros
    sdrplay_api_DeviceParamsT* params = nullptr;
    err = s_api.GetDeviceParams(devCopy->dev, &params);
    if (err != sdrplay_api_Success || !params) {
        Logger::error(QString("SDRplay: GetDeviceParams falhou — %1").arg(s_api.errStr(err)));
        s_api.ReleaseDevice(devCopy);
        delete devCopy;
        devStruct_ = nullptr;
        devHandle_ = nullptr;
        return false;
    }
    devParams_ = params;

    uint32_t decimationFactor = 1;
    uint32_t adcFs = sps_;
    while (adcFs < 2000000 && decimationFactor < 32) {
        decimationFactor *= 2;
        adcFs = sps_ * decimationFactor;
    }

    // Configurar taxa de amostragem
    if (params->devParams) {
        params->devParams->fsFreq.fsHz = static_cast<double>(adcFs);
        params->devParams->ppm         = static_cast<double>(ppm_);
        params->devParams->mode        = sdrplay_api_ISOCH;
    }

    // Configurar canal A (tuner principal)
    configChannel(params->rxChannelA, freq_, sps_, decimationFactor,
                  sdrplayIfMode_, sdrplayLnaState_, sdrplayIfGain_, sdrplayIfAgc_,
                  sdrplayBw_,
                  devCopy->hwVer);

    Logger::info(QString("SDRplay: aberto — %1 (s/n: %2)").arg(name_).arg(serial_));
    {
        std::lock_guard<std::mutex> lk(s_callbackMutex);
        s_activeDevice.store(this);
    }
    return true;

#else
    (void)serial;
    Logger::error("SDRplay: suporte não compilado (SDRPLAY_AVAILABLE=0).");
    return false;
#endif
}

void SdrplayDevice::close()
{
    {
        std::lock_guard<std::mutex> lk(s_callbackMutex);
        s_activeDevice.store(nullptr);
    }
    stop();

#if SDRPLAY_AVAILABLE
    if (devStruct_) {
        auto* devCopy = static_cast<sdrplay_api_DeviceT*>(devStruct_);
        if (s_api.isLoaded()) {
            s_api.ReleaseDevice(devCopy);
        }
        delete devCopy;
        devStruct_  = nullptr;
        devHandle_  = nullptr;
        devParams_  = nullptr;
    }
#endif
}

void SdrplayDevice::start()
{
#if SDRPLAY_AVAILABLE
    if (running_) return;
    if (!devStruct_ || !devHandle_) {
        Logger::error("SDRplay: start() chamado sem dispositivo aberto.");
        return;
    }

    running_ = true;

    sdrplay_api_CallbackFnsT cbs{};
    cbs.StreamACbFn = SDRPLAY_streamCb;
    cbs.StreamBCbFn = nullptr;   // single-tuner
    cbs.EventCbFn   = SDRPLAY_eventCb;

    sdrplay_api_ErrT err = s_api.Init(
        static_cast<HANDLE>(devHandle_), &cbs, this);

    if (err != sdrplay_api_Success) {
        Logger::error(QString("SDRplay: Init falhou — %1").arg(s_api.errStr(err)));
        running_ = false;
        return;
    }

    Logger::info("SDRplay: streaming iniciado.");

#else
    Logger::warn("SDRplay: sem suporte — streaming não iniciado.");
#endif
}

void SdrplayDevice::stop()
{
#if SDRPLAY_AVAILABLE
    if (running_) {
        running_ = false;
        if (devHandle_ && s_api.isLoaded()) {
            s_api.Uninit(static_cast<HANDLE>(devHandle_));
            Logger::info("SDRplay: streaming parado.");
        }
    }
#endif
    if (thread_) {
        thread_->wait();
        delete thread_;
        thread_ = nullptr;
    }
}

void SdrplayDevice::setCenterFreq(uint64_t hz)
{
    freq_ = hz;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->rxChannelA) return;

    p->rxChannelA->tunerParams.rfFreq.rfHz = static_cast<double>(hz);
    if (running_ && devHandle_) {
        s_api.Update(
            static_cast<HANDLE>(devHandle_),
            sdrplay_api_Tuner_A,
            static_cast<sdrplay_api_ReasonForUpdateT>(sdrplay_api_Update_Tuner_Frf | sdrplay_api_Update_Tuner_Gr),
            sdrplay_api_Update_Ext1_None);
    }
#endif
}

void SdrplayDevice::setSampleRate(uint32_t sps)
{
    sps_ = sps;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->devParams || !p->rxChannelA) return;

    uint32_t decimationFactor = 1;
    uint32_t adcFs = sps;
    while (adcFs < 2000000 && decimationFactor < 32) {
        decimationFactor *= 2;
        adcFs = sps * decimationFactor;
    }

    p->devParams->fsFreq.fsHz = static_cast<double>(adcFs);

    if (decimationFactor > 1) {
        p->rxChannelA->ctrlParams.decimation.enable = 1;
        p->rxChannelA->ctrlParams.decimation.decimationFactor = static_cast<unsigned char>(decimationFactor);
        p->rxChannelA->ctrlParams.decimation.wideBandSignal  = (sdrplayIfMode_ == 0) ? 1 : 0;
    } else {
        p->rxChannelA->ctrlParams.decimation.enable = 0;
        p->rxChannelA->ctrlParams.decimation.decimationFactor = 1;
        p->rxChannelA->ctrlParams.decimation.wideBandSignal  = 0;
    }

    // Escolhe a largura de banda apropriada
    sdrplay_api_Bw_MHzT bw = sdrplay_api_BW_Undefined;
    if (sdrplayIfMode_ == 0) {
        if (sdrplayBw_ != -1) {
            bw = static_cast<sdrplay_api_Bw_MHzT>(sdrplayBw_);
        } else {
            bw = selectBandwidth(sps);
        }
    } else {
        if (sdrplayIfMode_ == 1) bw = sdrplay_api_BW_1_536;
        else if (sdrplayIfMode_ == 2) bw = sdrplay_api_BW_5_000;
        else if (sdrplayIfMode_ == 3) bw = sdrplay_api_BW_1_536;
        else if (sdrplayIfMode_ == 4) bw = sdrplay_api_BW_0_600;
        else if (sdrplayIfMode_ == 5) bw = sdrplay_api_BW_0_300;
        else if (sdrplayIfMode_ == 6) bw = sdrplay_api_BW_0_200;
        else bw = selectBandwidth(sps);
    }
    p->rxChannelA->tunerParams.bwType = bw;

    if (running_ && devHandle_) {
        s_api.Update(
            static_cast<HANDLE>(devHandle_),
            sdrplay_api_Tuner_A,
            static_cast<sdrplay_api_ReasonForUpdateT>(
                sdrplay_api_Update_Dev_Fs |
                sdrplay_api_Update_Ctrl_Decimation |
                sdrplay_api_Update_Tuner_BwType
            ),
            sdrplay_api_Update_Ext1_None);
    }
#endif
}

void SdrplayDevice::setGain(int tenthsDb)
{
    gainTenths_ = tenthsDb;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->rxChannelA) return;

    sdrplay_api_ReasonForUpdateT reason = sdrplay_api_Update_None;

    if (tenthsDb == -1) {
        sdrplayIfAgc_ = true;
    } else {
        sdrplayIfAgc_ = false;
        // Mapeia tenthsDb (0..500) para sdrplayIfGain_ (20..59)
        sdrplayIfGain_ = 20 + (tenthsDb * 39 / 500);
        if (sdrplayIfGain_ < 20)  sdrplayIfGain_ = 20;
        if (sdrplayIfGain_ > 59)  sdrplayIfGain_ = 59;
    }

    sdrplay_api_AgcControlT targetAgc = sdrplayIfAgc_ ? sdrplay_api_AGC_50HZ : sdrplay_api_AGC_DISABLE;
    if (p->rxChannelA->ctrlParams.agc.enable != targetAgc) {
        p->rxChannelA->ctrlParams.agc.enable = targetAgc;
        p->rxChannelA->ctrlParams.agc.setPoint_dBfs = -29; // +1 dB de ganho RF (API SDRplay) no modo IF AGC
        reason = static_cast<sdrplay_api_ReasonForUpdateT>(reason | sdrplay_api_Update_Ctrl_Agc);
    }

    if (!sdrplayIfAgc_) {
        // Com EXTENDED_MIN_GR: ifGain 20..59 (UI) → gRdB 59..0 (API)
        int targetGr = 59 - (sdrplayIfGain_ - 20) * 59 / 39;
        targetGr -= 1; // +1 dB de ganho RF (API SDRplay)
        if (targetGr < 0)  targetGr = 0;
        if (targetGr > 59) targetGr = 59;
        if (p->rxChannelA->tunerParams.gain.gRdB != targetGr) {
            p->rxChannelA->tunerParams.gain.gRdB = targetGr;
            reason = static_cast<sdrplay_api_ReasonForUpdateT>(reason | sdrplay_api_Update_Tuner_Gr);
        }
    }

    if (running_ && devHandle_ && reason != sdrplay_api_Update_None) {
        sdrplay_api_ErrT upErr = s_api.Update(
            static_cast<HANDLE>(devHandle_),
            sdrplay_api_Tuner_A,
            reason,
            sdrplay_api_Update_Ext1_None);
        if (upErr != sdrplay_api_Success)
            Logger::warn(QString("SDRplay: setGain Update — %1").arg(s_api.errStr(upErr)));
    }
#endif
}

void SdrplayDevice::setPpm(int ppm)
{
    ppm_ = ppm;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->devParams) return;

    p->devParams->ppm = static_cast<double>(ppm);
    if (running_ && devHandle_) {
        sdrplay_api_ErrT upErr = s_api.Update(
            static_cast<HANDLE>(devHandle_),
            sdrplay_api_Tuner_A,
            sdrplay_api_Update_Dev_Ppm,
            sdrplay_api_Update_Ext1_None);
        if (upErr != sdrplay_api_Success)
            Logger::warn(QString("SDRplay: setPpm Update — %1").arg(s_api.errStr(upErr)));
    }
#endif
}

void SdrplayDevice::setSdrplayParams(int ifMode, int lnaState, int ifGain, bool ifAgc, int bw)
{
    sdrplayIfMode_ = ifMode;
    sdrplayLnaState_ = lnaState;
    sdrplayIfGain_ = ifGain;
    sdrplayIfAgc_ = ifAgc;
    sdrplayBw_ = bw;

#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->rxChannelA) return;

    sdrplay_api_ReasonForUpdateT reason = sdrplay_api_Update_None;

    // 1. IF Mode (ifType and bwType)
    sdrplay_api_If_kHzT targetIf = sdrplay_api_IF_Zero;
    sdrplay_api_Bw_MHzT targetBw = p->rxChannelA->tunerParams.bwType;
    
    if (ifMode == 0) {
        targetIf = sdrplay_api_IF_Zero;
        if (bw != -1) {
            targetBw = static_cast<sdrplay_api_Bw_MHzT>(bw);
        } else {
            targetBw = selectBandwidth(sps_);
        }
    } else if (ifMode == 1) {
        targetIf = sdrplay_api_IF_2_048;
        targetBw = sdrplay_api_BW_1_536;
    } else if (ifMode == 2) {
        targetIf = sdrplay_api_IF_2_048;
        targetBw = sdrplay_api_BW_5_000;
    } else if (ifMode == 3) {
        targetIf = sdrplay_api_IF_1_620;
        targetBw = sdrplay_api_BW_1_536;
    } else if (ifMode == 4) {
        targetIf = sdrplay_api_IF_0_450;
        targetBw = sdrplay_api_BW_0_600;
    } else if (ifMode == 5) {
        targetIf = sdrplay_api_IF_0_450;
        targetBw = sdrplay_api_BW_0_300;
    } else if (ifMode == 6) {
        targetIf = sdrplay_api_IF_0_450;
        targetBw = sdrplay_api_BW_0_200;
    }

    // Garantir que a largura de banda não exceda a taxa de amostragem se em modo automático (apenas para Zero-IF)
    if (ifMode == 0 && bw == -1 && static_cast<uint32_t>(targetBw) * 1000 > sps_) {
        targetBw = selectBandwidth(sps_);
    }

    if (p->rxChannelA->tunerParams.ifType != targetIf) {
        p->rxChannelA->tunerParams.ifType = targetIf;
        reason = static_cast<sdrplay_api_ReasonForUpdateT>(reason | sdrplay_api_Update_Tuner_IfType);
    }
    if (p->rxChannelA->tunerParams.bwType != targetBw) {
        p->rxChannelA->tunerParams.bwType = targetBw;
        reason = static_cast<sdrplay_api_ReasonForUpdateT>(reason | sdrplay_api_Update_Tuner_BwType);
    }

    // 2. LNA Gain State — escala proporcional ao número de estados do hardware
    auto* devCopy = static_cast<sdrplay_api_DeviceT*>(devStruct_);
    unsigned char hwVer = devCopy ? devCopy->hwVer : SDRPLAY_RSP1A_ID;
    int maxLnaState = 9;
    if (hwVer == SDRPLAY_RSP1_ID) {
        maxLnaState = 3;
    } else if (hwVer == SDRPLAY_RSP2_ID) {
        maxLnaState = 8;
    }
    // Proporcional: lnaState 0 (min UI) → maxLnaState (min ganho API)
    //               lnaState 9 (max UI) → 0 (max ganho API)
    int targetLna = maxLnaState - (lnaState * maxLnaState / 9);
    if (targetLna < 0) targetLna = 0;
    if (targetLna > maxLnaState) targetLna = maxLnaState;

    p->rxChannelA->tunerParams.gain.LNAstate = static_cast<unsigned char>(targetLna);
    p->rxChannelA->tunerParams.gain.minGr = sdrplay_api_EXTENDED_MIN_GR;
    reason = static_cast<sdrplay_api_ReasonForUpdateT>(reason | sdrplay_api_Update_Tuner_Gr);

    // 3. IF AGC
    sdrplay_api_AgcControlT targetAgc = ifAgc ? sdrplay_api_AGC_50HZ : sdrplay_api_AGC_DISABLE;
    if (p->rxChannelA->ctrlParams.agc.enable != targetAgc) {
        p->rxChannelA->ctrlParams.agc.enable = targetAgc;
        p->rxChannelA->ctrlParams.agc.setPoint_dBfs = -29; // +1 dB de ganho RF (API SDRplay) no modo IF AGC
        reason = static_cast<sdrplay_api_ReasonForUpdateT>(reason | sdrplay_api_Update_Ctrl_Agc);
    }

    // 4. IF Gain (gRdB) — com EXTENDED_MIN_GR faixa é 0..59
    if (!ifAgc) {
        // ifGain 20 (min UI) → gRdB=59 (máx redução = mín ganho)
        // ifGain 59 (max UI) → gRdB=0  (mín redução = máx ganho)
        int targetGr = 59 - (ifGain - 20) * 59 / 39;
        targetGr -= 1; // +1 dB de ganho RF (API SDRplay)
        if (targetGr < 0)  targetGr = 0;
        if (targetGr > 59) targetGr = 59;

        p->rxChannelA->tunerParams.gain.gRdB = targetGr;
        reason = static_cast<sdrplay_api_ReasonForUpdateT>(reason | sdrplay_api_Update_Tuner_Gr);
    }

    if (running_ && devHandle_ && reason != sdrplay_api_Update_None) {
        sdrplay_api_ErrT upErr = s_api.Update(
            static_cast<HANDLE>(devHandle_),
            sdrplay_api_Tuner_A,
            reason,
            sdrplay_api_Update_Ext1_None);
        if (upErr != sdrplay_api_Success)
            Logger::warn(QString("SDRplay: setSdrplayParams Update — %1").arg(s_api.errStr(upErr)));
    }
#endif
}

} // namespace masdr
