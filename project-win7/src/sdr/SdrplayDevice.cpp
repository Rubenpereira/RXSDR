// SdrplayDevice.cpp — Suporte a hardware SDRplay via API v3 (carregamento dinamico).
// Versao sem Qt: usa std::string e std::atomic apenas.

#include "SdrplayDevice.h"
#include "../util/Logger.h"

#if SDRPLAY_AVAILABLE

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "sdrplay_api.h"
using masdr::Logger;
#include <cstring>
#include <vector>
#include <complex>
#include <string>

// ─── Carregador dinamico da API SDRplay ──────────────────────────────────────
struct SdrplayApiLoader {
    HMODULE dll = nullptr;

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
        if (dll) return true;
        static const char* paths[] = {
            "sdrplay_api.dll",
            "C:\\Program Files\\SDRplay\\API\\x64\\sdrplay_api.dll",
            "C:\\Users\\Ruben\\Radioamador\\SDRplay API\\API\\x64\\sdrplay_api.dll",
            "C:\\Program Files (x86)\\SDRplay\\API\\x86\\sdrplay_api.dll",
            nullptr
        };
        for (int i = 0; paths[i]; ++i) {
            dll = ::LoadLibraryA(paths[i]);
            if (dll) { Logger::info(std::string("SDRplay: API carregada — ") + paths[i]); break; }
        }
        if (!dll) {
            Logger::error("SDRplay: DLL nao encontrada. Instale em https://www.sdrplay.com/api/");
            return false;
        }
        auto loadSym = [&](const char* name, void** ptr) -> bool {
            *ptr = reinterpret_cast<void*>(::GetProcAddress(dll, name));
            if (!*ptr) { Logger::error(std::string("SDRplay: simbolo ausente: ") + name); unload(); return false; }
            return true;
        };
#define SDRPLAY_LOAD(fn) if (!loadSym("sdrplay_api_" #fn, reinterpret_cast<void**>(&fn))) return false
        SDRPLAY_LOAD(Open); SDRPLAY_LOAD(Close); SDRPLAY_LOAD(ApiVersion);
        SDRPLAY_LOAD(LockDeviceApi); SDRPLAY_LOAD(UnlockDeviceApi);
        SDRPLAY_LOAD(GetDevices); SDRPLAY_LOAD(SelectDevice); SDRPLAY_LOAD(ReleaseDevice);
        SDRPLAY_LOAD(GetErrorString); SDRPLAY_LOAD(GetDeviceParams);
        SDRPLAY_LOAD(Init); SDRPLAY_LOAD(Uninit); SDRPLAY_LOAD(Update);
#undef SDRPLAY_LOAD
        float ver = 0.0f;
        if (ApiVersion(&ver) == sdrplay_api_Success)
            Logger::info("SDRplay: versao API " + std::to_string(ver));
        return true;
    }

    void unload() {
        if (dll) { ::FreeLibrary(dll); dll = nullptr; }
        Open=nullptr; Close=nullptr; ApiVersion=nullptr;
        LockDeviceApi=nullptr; UnlockDeviceApi=nullptr;
        GetDevices=nullptr; SelectDevice=nullptr; ReleaseDevice=nullptr;
        GetErrorString=nullptr; GetDeviceParams=nullptr;
        Init=nullptr; Uninit=nullptr; Update=nullptr;
    }

    bool isLoaded() const { return dll && Open; }
    const char* errStr(sdrplay_api_ErrT e) const {
        return (GetErrorString && e != sdrplay_api_Success) ? GetErrorString(e) : "ok";
    }
};

static SdrplayApiLoader s_api;

static std::string hwVerToName(unsigned char hwVer) {
    switch (hwVer) {
        case SDRPLAY_RSP1_ID:    return "SDRplay RSP1";
        case SDRPLAY_RSP1A_ID:   return "SDRplay RSP1A";
        case SDRPLAY_RSP1B_ID:   return "SDRplay RSP1B";
        case SDRPLAY_RSP2_ID:    return "SDRplay RSP2";
        case SDRPLAY_RSPduo_ID:  return "SDRplay RSPduo";
        case SDRPLAY_RSPdx_ID:   return "SDRplay RSPdx";
        case SDRPLAY_RSPdxR2_ID: return "SDRplay RSPdx-R2";
        default: return "SDRplay (hwVer=" + std::to_string((int)hwVer) + ")";
    }
}

static sdrplay_api_Bw_MHzT selectBandwidth(uint32_t sps) {
    if (sps < 300000)       return sdrplay_api_BW_0_200;
    else if (sps < 600000)  return sdrplay_api_BW_0_300;
    else if (sps < 1536000) return sdrplay_api_BW_0_600;
    else if (sps < 5000000) return sdrplay_api_BW_1_536;
    else if (sps < 6000000) return sdrplay_api_BW_5_000;
    else if (sps < 7000000) return sdrplay_api_BW_6_000;
    else if (sps < 8000000) return sdrplay_api_BW_7_000;
    else                    return sdrplay_api_BW_8_000;
}

static void configChannel(sdrplay_api_RxChannelParamsT* ch,
                           uint64_t freqHz, uint32_t sps, uint32_t decimFactor,
                           int ifMode, int lnaState, int ifGain, bool ifAgc, int bwSetting,
                           unsigned char hwVer)
{
    if (!ch) return;
    ch->tunerParams.rfFreq.rfHz = (double)freqHz;

    int maxLna = (hwVer==SDRPLAY_RSP1_ID)?3:(hwVer==SDRPLAY_RSP2_ID)?8:9;
    int targetLna = maxLna - (lnaState * maxLna / 9);
    if (targetLna < 0) targetLna = 0;
    if (targetLna > maxLna) targetLna = maxLna;
    ch->tunerParams.gain.LNAstate = (unsigned char)targetLna;
    ch->tunerParams.gain.minGr = sdrplay_api_EXTENDED_MIN_GR;

    sdrplay_api_Bw_MHzT bw = selectBandwidth(sps);
    sdrplay_api_If_kHzT targetIf = sdrplay_api_IF_Zero;
    if      (ifMode == 1) { targetIf = sdrplay_api_IF_2_048; bw = sdrplay_api_BW_1_536; }
    else if (ifMode == 2) { targetIf = sdrplay_api_IF_2_048; bw = sdrplay_api_BW_5_000; }
    else if (ifMode == 3) { targetIf = sdrplay_api_IF_1_620; bw = sdrplay_api_BW_1_536; }
    else if (ifMode == 4) { targetIf = sdrplay_api_IF_0_450; bw = sdrplay_api_BW_0_600; }
    else if (ifMode == 5) { targetIf = sdrplay_api_IF_0_450; bw = sdrplay_api_BW_0_300; }
    else if (ifMode == 6) { targetIf = sdrplay_api_IF_0_450; bw = sdrplay_api_BW_0_200; }
    else if (bwSetting != -1) bw = (sdrplay_api_Bw_MHzT)bwSetting;

    ch->tunerParams.bwType = bw;
    ch->tunerParams.ifType = targetIf;
    ch->tunerParams.loMode = sdrplay_api_LO_Auto;

    if (ifAgc) {
        ch->ctrlParams.agc.enable  = sdrplay_api_AGC_50HZ;
        ch->tunerParams.gain.gRdB  = 40;
    } else {
        ch->ctrlParams.agc.enable  = sdrplay_api_AGC_DISABLE;
        int gr = 59 - (ifGain - 20) * 59 / 39;
        if (gr < 0) gr = 0; if (gr > 59) gr = 59;
        ch->tunerParams.gain.gRdB  = gr;
    }
    ch->ctrlParams.agc.setPoint_dBfs    = -30;
    ch->ctrlParams.agc.attack_ms        = 0;
    ch->ctrlParams.agc.decay_ms         = 0;
    ch->ctrlParams.agc.decay_delay_ms   = 0;
    ch->ctrlParams.agc.decay_threshold_dB = 0;
    ch->ctrlParams.dcOffset.DCenable = 1;
    ch->ctrlParams.dcOffset.IQenable = 1;

    if (decimFactor > 1) {
        ch->ctrlParams.decimation.enable           = 1;
        ch->ctrlParams.decimation.decimationFactor = (unsigned char)decimFactor;
        ch->ctrlParams.decimation.wideBandSignal   = (ifMode==0)?1:0;
    } else {
        ch->ctrlParams.decimation.enable           = 0;
        ch->ctrlParams.decimation.decimationFactor = 1;
        ch->ctrlParams.decimation.wideBandSignal   = 0;
    }
}

static void SDRPLAY_streamCb(short* xi, short* xq,
    sdrplay_api_StreamCbParamsT*, unsigned int numSamples, unsigned int, void* ctx)
{
    auto* dev = static_cast<masdr::SdrplayDevice*>(ctx);
    if (!dev || !dev->isRunning() || numSamples == 0) return;
    std::vector<std::complex<float>> buf;
    dev->processBatchSamples(xi, xq, numSamples, buf);
    dev->deliverSamples(buf.data(), buf.size());
}

static void SDRPLAY_eventCb(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT,
    sdrplay_api_EventParamsT* params, void*)
{
    switch (eventId) {
        case sdrplay_api_GainChange:
            Logger::info("SDRplay: ganho ajustado gRdB=" + std::to_string(params->gainParams.gRdB));
            break;
        case sdrplay_api_PowerOverloadChange:
            if (params->powerOverloadParams.powerOverloadChangeType == sdrplay_api_Overload_Detected)
                Logger::warn("SDRplay: SOBRECARGA detectada — reduza o ganho!");
            else
                Logger::info("SDRplay: sobrecarga corrigida.");
            break;
        case sdrplay_api_DeviceRemoved:
            Logger::error("SDRplay: dispositivo removido durante operacao!");
            break;
        default: break;
    }
}

#endif // SDRPLAY_AVAILABLE

// ─── SdrplayDevice ────────────────────────────────────────────────────────────
namespace masdr {

static std::atomic<SdrplayDevice*> s_activeDevice{nullptr};

SdrplayDevice::SdrplayDevice()  = default;
SdrplayDevice::~SdrplayDevice() { close(); }

void SdrplayDevice::processBatchSamples(const short* xi, const short* xq,
                                        unsigned numSamples,
                                        std::vector<std::complex<float>>& buf)
{
    constexpr float kScale = 1.0f / 32768.0f;
    buf.resize(numSamples);
    for (unsigned i = 0; i < numSamples; ++i)
        buf[i] = { xi[i] * kScale, xq[i] * kScale };
}

std::vector<SdrplayInfo> SdrplayDevice::enumerate()
{
    std::vector<SdrplayInfo> out;
#if SDRPLAY_AVAILABLE
    if (!s_api.load()) return out;

    sdrplay_api_ErrT err = s_api.Open();
    if (err != sdrplay_api_Success) {
        Logger::error(std::string("SDRplay: enumerate Open falhou — ") + s_api.errStr(err));
        SdrplayDevice* active = s_activeDevice.load();
        if (active && !active->serial().empty()) out.push_back({active->serial(), active->name()});
        return out;
    }

    s_api.LockDeviceApi();
    sdrplay_api_DeviceT devs[SDRPLAY_MAX_DEVICES];
    unsigned int nDevs = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        nDevs = 0;
        err = s_api.GetDevices(devs, &nDevs, SDRPLAY_MAX_DEVICES);
        if (err == sdrplay_api_Success && nDevs > 0) break;
        if (attempt < 2) ::Sleep(200);
    }
    if (err == sdrplay_api_Success) {
        for (unsigned i = 0; i < nDevs; ++i)
            out.push_back({std::string(devs[i].SerNo), hwVerToName(devs[i].hwVer)});
    }
    s_api.UnlockDeviceApi();
    s_api.Close();

    SdrplayDevice* active = s_activeDevice.load();
    if (active && !active->serial().empty()) {
        bool found = false;
        for (const auto& d : out) if (d.serial == active->serial()) { found=true; break; }
        if (!found) out.push_back({active->serial(), active->name()});
    }
#endif
    return out;
}

bool SdrplayDevice::open(const std::string& serial)
{
#if SDRPLAY_AVAILABLE
    if (!s_api.load()) return false;
    Logger::info("SDRplay: abrindo serial='" + serial + "'");

    sdrplay_api_ErrT err = s_api.Open();
    if (err != sdrplay_api_Success) {
        Logger::error(std::string("SDRplay: Open falhou — ") + s_api.errStr(err));
        return false;
    }

    s_api.LockDeviceApi();
    sdrplay_api_DeviceT devs[SDRPLAY_MAX_DEVICES];
    unsigned int nDevs = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        nDevs = 0;
        err = s_api.GetDevices(devs, &nDevs, SDRPLAY_MAX_DEVICES);
        if (err == sdrplay_api_Success && nDevs > 0) break;
        if (attempt < 2) ::Sleep(200);
    }
    Logger::info("SDRplay: GetDevices encontrou " + std::to_string(nDevs) + " dispositivo(s)");

    if (err != sdrplay_api_Success || nDevs == 0) {
        s_api.UnlockDeviceApi(); s_api.Close();
        Logger::error("SDRplay: nenhum dispositivo encontrado");
        return false;
    }

    int idx = -1;
    for (unsigned i = 0; i < nDevs; ++i) {
        if (serial.empty() || serial == std::string(devs[i].SerNo)) { idx=(int)i; break; }
    }
    if (idx < 0 && nDevs > 0) {
        idx = 0;
        Logger::warn("SDRplay: serial '" + serial + "' nao encontrado. Usando primeiro disponivel.");
    }
    if (idx < 0) { s_api.UnlockDeviceApi(); s_api.Close(); return false; }

    auto* devCopy = new sdrplay_api_DeviceT;
    std::memcpy(devCopy, &devs[idx], sizeof(sdrplay_api_DeviceT));
    devStruct_ = devCopy;
    serial_ = std::string(devCopy->SerNo);
    name_   = hwVerToName(devCopy->hwVer);

    err = s_api.SelectDevice(devCopy);
    s_api.UnlockDeviceApi();
    if (err != sdrplay_api_Success) {
        Logger::error(std::string("SDRplay: SelectDevice falhou — ") + s_api.errStr(err));
        delete devCopy; devStruct_=nullptr; devHandle_=nullptr; s_api.Close();
        return false;
    }
    Logger::info("SDRplay: SelectDevice OK — " + name_ + " (s/n: " + serial_ + ")");
    devHandle_ = devCopy->dev;

    sdrplay_api_DeviceParamsT* params = nullptr;
    err = s_api.GetDeviceParams(devCopy->dev, &params);
    if (err != sdrplay_api_Success || !params) {
        Logger::error(std::string("SDRplay: GetDeviceParams falhou — ") + s_api.errStr(err));
        s_api.ReleaseDevice(devCopy); delete devCopy; devStruct_=nullptr; devHandle_=nullptr; s_api.Close();
        return false;
    }
    devParams_ = params;

    uint32_t decimFactor = 1, adcFs = sps_;
    while (adcFs < 2000000 && decimFactor < 32) { decimFactor *= 2; adcFs = sps_ * decimFactor; }

    if (params->devParams) {
        params->devParams->fsFreq.fsHz = (double)adcFs;
        params->devParams->ppm         = (double)ppm_;
        params->devParams->mode        = sdrplay_api_ISOCH;
    }
    configChannel(params->rxChannelA, freq_, sps_, decimFactor,
                  sdrplayIfMode_, sdrplayLnaState_, sdrplayIfGain_, sdrplayIfAgc_,
                  sdrplayBw_, devCopy->hwVer);

    Logger::info("SDRplay: aberto — " + name_ + " (s/n: " + serial_ + ")");
    s_activeDevice.store(this);
    return true;
#else
    (void)serial;
    Logger::error("SDRplay: suporte nao compilado (SDRPLAY_AVAILABLE=0).");
    return false;
#endif
}

void SdrplayDevice::close()
{
    s_activeDevice.store(nullptr);
    stop();
#if SDRPLAY_AVAILABLE
    if (devStruct_) {
        auto* devCopy = static_cast<sdrplay_api_DeviceT*>(devStruct_);
        if (s_api.isLoaded()) { s_api.ReleaseDevice(devCopy); s_api.Close(); }
        delete devCopy;
        devStruct_=nullptr; devHandle_=nullptr; devParams_=nullptr;
    }
#endif
}

void SdrplayDevice::start()
{
#if SDRPLAY_AVAILABLE
    if (running_) return;
    if (!devStruct_ || !devHandle_) { Logger::error("SDRplay: start sem dispositivo aberto."); return; }
    running_ = true;
    sdrplay_api_CallbackFnsT cbs{};
    cbs.StreamACbFn = SDRPLAY_streamCb;
    cbs.StreamBCbFn = nullptr;
    cbs.EventCbFn   = SDRPLAY_eventCb;
    sdrplay_api_ErrT err = s_api.Init(static_cast<HANDLE>(devHandle_), &cbs, this);
    if (err != sdrplay_api_Success) {
        Logger::error(std::string("SDRplay: Init falhou — ") + s_api.errStr(err));
        running_ = false; return;
    }
    Logger::info("SDRplay: streaming iniciado.");
#else
    Logger::warn("SDRplay: sem suporte — streaming nao iniciado.");
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
}

void SdrplayDevice::setCenterFreq(uint64_t hz)
{
    freq_ = hz;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->rxChannelA) return;
    p->rxChannelA->tunerParams.rfFreq.rfHz = (double)hz;
    if (running_ && devHandle_)
        s_api.Update(static_cast<HANDLE>(devHandle_), sdrplay_api_Tuner_A,
            (sdrplay_api_ReasonForUpdateT)(sdrplay_api_Update_Tuner_Frf | sdrplay_api_Update_Tuner_Gr),
            sdrplay_api_Update_Ext1_None);
#endif
}

void SdrplayDevice::setSampleRate(uint32_t sps)
{
    sps_ = sps;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->devParams || !p->rxChannelA) return;
    uint32_t decimFactor=1, adcFs=sps;
    while (adcFs < 2000000 && decimFactor < 32) { decimFactor*=2; adcFs=sps*decimFactor; }
    p->devParams->fsFreq.fsHz = (double)adcFs;
    if (decimFactor > 1) {
        p->rxChannelA->ctrlParams.decimation.enable=1;
        p->rxChannelA->ctrlParams.decimation.decimationFactor=(unsigned char)decimFactor;
        p->rxChannelA->ctrlParams.decimation.wideBandSignal=(sdrplayIfMode_==0)?1:0;
    } else {
        p->rxChannelA->ctrlParams.decimation.enable=0;
        p->rxChannelA->ctrlParams.decimation.decimationFactor=1;
        p->rxChannelA->ctrlParams.decimation.wideBandSignal=0;
    }
    sdrplay_api_Bw_MHzT bw = (sdrplayBw_!=-1 && sdrplayIfMode_==0)
        ? (sdrplay_api_Bw_MHzT)sdrplayBw_ : selectBandwidth(sps);
    p->rxChannelA->tunerParams.bwType = bw;
    if (running_ && devHandle_)
        s_api.Update(static_cast<HANDLE>(devHandle_), sdrplay_api_Tuner_A,
            (sdrplay_api_ReasonForUpdateT)(sdrplay_api_Update_Dev_Fs|
                sdrplay_api_Update_Ctrl_Decimation|sdrplay_api_Update_Tuner_BwType),
            sdrplay_api_Update_Ext1_None);
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
        sdrplayIfGain_ = 20 + (tenthsDb * 39 / 500);
        if (sdrplayIfGain_ < 20) sdrplayIfGain_=20;
        if (sdrplayIfGain_ > 59) sdrplayIfGain_=59;
    }
    sdrplay_api_AgcControlT targetAgc = sdrplayIfAgc_ ? sdrplay_api_AGC_50HZ : sdrplay_api_AGC_DISABLE;
    if (p->rxChannelA->ctrlParams.agc.enable != targetAgc) {
        p->rxChannelA->ctrlParams.agc.enable = targetAgc;
        p->rxChannelA->ctrlParams.agc.setPoint_dBfs = -30;
        reason = (sdrplay_api_ReasonForUpdateT)(reason | sdrplay_api_Update_Ctrl_Agc);
    }
    if (!sdrplayIfAgc_) {
        int gr = 59 - (sdrplayIfGain_-20)*59/39;
        if (gr<0) gr=0; if (gr>59) gr=59;
        p->rxChannelA->tunerParams.gain.gRdB = gr;
        reason = (sdrplay_api_ReasonForUpdateT)(reason | sdrplay_api_Update_Tuner_Gr);
    }
    if (running_ && devHandle_ && reason != sdrplay_api_Update_None)
        s_api.Update(static_cast<HANDLE>(devHandle_), sdrplay_api_Tuner_A, reason, sdrplay_api_Update_Ext1_None);
#endif
}

void SdrplayDevice::setPpm(int ppm)
{
    ppm_ = ppm;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->devParams) return;
    p->devParams->ppm = (double)ppm;
    if (running_ && devHandle_)
        s_api.Update(static_cast<HANDLE>(devHandle_), sdrplay_api_Tuner_A,
            sdrplay_api_Update_Dev_Ppm, sdrplay_api_Update_Ext1_None);
#endif
}

void SdrplayDevice::setSdrplayParams(int ifMode, int lnaState, int ifGain, bool ifAgc, int bw)
{
    sdrplayIfMode_=ifMode; sdrplayLnaState_=lnaState;
    sdrplayIfGain_=ifGain; sdrplayIfAgc_=ifAgc; sdrplayBw_=bw;
#if SDRPLAY_AVAILABLE
    if (!devParams_) return;
    auto* p = static_cast<sdrplay_api_DeviceParamsT*>(devParams_);
    if (!p->rxChannelA) return;
    sdrplay_api_ReasonForUpdateT reason = sdrplay_api_Update_None;

    sdrplay_api_If_kHzT targetIf = sdrplay_api_IF_Zero;
    sdrplay_api_Bw_MHzT targetBw = p->rxChannelA->tunerParams.bwType;
    if      (ifMode==1){targetIf=sdrplay_api_IF_2_048;targetBw=sdrplay_api_BW_1_536;}
    else if (ifMode==2){targetIf=sdrplay_api_IF_2_048;targetBw=sdrplay_api_BW_5_000;}
    else if (ifMode==3){targetIf=sdrplay_api_IF_1_620;targetBw=sdrplay_api_BW_1_536;}
    else if (ifMode==4){targetIf=sdrplay_api_IF_0_450;targetBw=sdrplay_api_BW_0_600;}
    else if (ifMode==5){targetIf=sdrplay_api_IF_0_450;targetBw=sdrplay_api_BW_0_300;}
    else if (ifMode==6){targetIf=sdrplay_api_IF_0_450;targetBw=sdrplay_api_BW_0_200;}
    else { targetIf=sdrplay_api_IF_Zero; targetBw=(bw!=-1)?(sdrplay_api_Bw_MHzT)bw:selectBandwidth(sps_); }

    if (p->rxChannelA->tunerParams.ifType != targetIf) {
        p->rxChannelA->tunerParams.ifType = targetIf;
        reason=(sdrplay_api_ReasonForUpdateT)(reason|sdrplay_api_Update_Tuner_IfType);
    }
    if (p->rxChannelA->tunerParams.bwType != targetBw) {
        p->rxChannelA->tunerParams.bwType = targetBw;
        reason=(sdrplay_api_ReasonForUpdateT)(reason|sdrplay_api_Update_Tuner_BwType);
    }

    auto* devCopy = static_cast<sdrplay_api_DeviceT*>(devStruct_);
    unsigned char hwVer = devCopy ? devCopy->hwVer : SDRPLAY_RSP1A_ID;
    int maxLna=(hwVer==SDRPLAY_RSP1_ID)?3:(hwVer==SDRPLAY_RSP2_ID)?8:9;
    int targetLna = maxLna - (lnaState * maxLna / 9);
    if (targetLna<0) targetLna=0; if (targetLna>maxLna) targetLna=maxLna;
    p->rxChannelA->tunerParams.gain.LNAstate=(unsigned char)targetLna;
    p->rxChannelA->tunerParams.gain.minGr=sdrplay_api_EXTENDED_MIN_GR;
    reason=(sdrplay_api_ReasonForUpdateT)(reason|sdrplay_api_Update_Tuner_Gr);

    sdrplay_api_AgcControlT targetAgc = ifAgc ? sdrplay_api_AGC_50HZ : sdrplay_api_AGC_DISABLE;
    if (p->rxChannelA->ctrlParams.agc.enable != targetAgc) {
        p->rxChannelA->ctrlParams.agc.enable=targetAgc;
        p->rxChannelA->ctrlParams.agc.setPoint_dBfs=-30;
        reason=(sdrplay_api_ReasonForUpdateT)(reason|sdrplay_api_Update_Ctrl_Agc);
    }
    if (!ifAgc) {
        int gr=59-(ifGain-20)*59/39;
        if (gr<0) gr=0; if (gr>59) gr=59;
        p->rxChannelA->tunerParams.gain.gRdB=gr;
        reason=(sdrplay_api_ReasonForUpdateT)(reason|sdrplay_api_Update_Tuner_Gr);
    }
    if (running_ && devHandle_ && reason != sdrplay_api_Update_None)
        s_api.Update(static_cast<HANDLE>(devHandle_), sdrplay_api_Tuner_A, reason, sdrplay_api_Update_Ext1_None);
#endif
}

} // namespace masdr
