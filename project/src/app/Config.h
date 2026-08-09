#pragma once
#include <QString>
#include <QSettings>

namespace masdr {

class Config {
public:
    static Config& instance() { static Config c; return c; }

    quint16 httpPort()  const { return s_.value("server/httpPort", 8070).toUInt(); }
    quint16 wsPort()    const { return s_.value("server/wsPort",   8071).toUInt(); }

    QString lastDevice()    const { return s_.value("device/last",   "rtlsdr").toString(); }
    QString lastSerial()    const { return s_.value("device/serial", "").toString(); }
    QString rtltcpHost()    const { return s_.value("device/rtltcpHost", "127.0.0.1").toString(); }
    int     rtltcpPort()    const { return s_.value("device/rtltcpPort", 1234).toInt(); }
    quint64 vfoA()          const { return s_.value("vfo/A", 50173000ULL).toULongLong(); }
    quint64 vfoB()          const { return s_.value("vfo/B", 14283000ULL).toULongLong(); }
    QString mode()          const { return s_.value("vfo/mode", "USB").toString(); }
    int     gainTenths()    const { return s_.value("rx/gain",  496).toInt(); }
    uint    sampleRate()    const { return s_.value("rx/sps", 2048000).toUInt(); }
    QString wfPalette()     const { return s_.value("ui/palette", "classic").toString(); }

    int     fftSize()       const { return s_.value("rx/fftSize", 8192).toInt(); }

    int     sdrplayIfMode()    const { return s_.value("device/sdrplayIfMode", 0).toInt(); }
    int     sdrplayLnaState()  const { return s_.value("device/sdrplayLnaState", 9).toInt(); }
    int     sdrplayIfGain()    const { return s_.value("device/sdrplayIfGain", 40).toInt(); }
    bool    sdrplayIfAgc()     const { return s_.value("device/sdrplayIfAgc", false).toBool(); }
    int     sdrplayBw()        const { return s_.value("device/sdrplayBw", -1).toInt(); }

    bool    agc()           const { return s_.value("rx/agc", true).toBool(); }
    bool    biasT()         const { return s_.value("rx/bias", false).toBool(); }
    bool    quadrature()    const { return s_.value("rx/quadrature", false).toBool(); }

    // ---- Amostragem direta (ramo Q) -------------------------------------
    // Tres modos, como no OpenSDR+:
    //   "off"  - nunca usa amostragem direta (so VHF/UHF pelo tuner)
    //   "on"   - forca amostragem direta sempre, em qualquer frequencia
    //   "auto" - liga abaixo de 24 MHz e desliga acima, sozinho
    // O limite e 24 MHz porque acima disso o ramo Q do RTL-SDR ja passou de
    // metade da taxa de amostragem do ADC e so devolve imagem dobrada.
    static constexpr quint64 kLimiteHfHz = 24000000ULL;

    QString qMode() const {
        const QString m = s_.value("rx/qmode").toString();
        if (m == QLatin1String("off") || m == QLatin1String("on")
            || m == QLatin1String("auto")) return m;
        // Compatibilidade com as versoes antigas, em que isto era so um bool:
        // o "ligado" de antes ja desligava acima de 24 MHz, ou seja, era o
        // que hoje chamamos de automatico.
        return quadrature() ? QStringLiteral("auto") : QStringLiteral("off");
    }

    // Amostragem direta que vale para esta frequencia, dado o modo escolhido.
    bool quadratureEm(quint64 hz) const {
        const QString m = qMode();
        if (m == QLatin1String("on"))  return true;
        if (m == QLatin1String("off")) return false;
        return hz < kLimiteHfHz;
    }
    int     ppm()           const { return s_.value("rx/ppm", 0).toInt(); }
    bool    iqCorrection()  const { return s_.value("rx/iqCorrection", true).toBool(); }

    double  smeterHfOffset()   const { return s_.value("smeter/hfOffset", 0.0).toDouble(); }
    double  smeterVhfOffset()  const { return s_.value("smeter/vhfOffset", 0.0).toDouble(); }
    int     smeterS9Hf()       const { return s_.value("smeter/s9Hf", -94).toInt(); }
    int     smeterS9Vhf()      const { return s_.value("smeter/s9Vhf", -93).toInt(); }
    int     smeterHfEmpty()    const { return s_.value("smeter/hfEmpty", 36).toInt(); }
    int     smeterVhfEmpty()   const { return s_.value("smeter/vhfEmpty", 36).toInt(); }
    bool    smeterRmsAligned() const { return s_.value("smeter/rmsAligned", true).toBool(); }

    void setVfoA(quint64 v)    { s_.setValue("vfo/A", v); }
    void setVfoB(quint64 v)    { s_.setValue("vfo/B", v); }
    void setMode(const QString& m) { s_.setValue("vfo/mode", m); }
    void setLastDevice(const QString& t) { s_.setValue("device/last", t); }
    void setLastSerial(const QString& s) { s_.setValue("device/serial", s); }
    void setRtltcpHost(const QString& h) { s_.setValue("device/rtltcpHost", h); }
    void setRtltcpPort(int p)           { s_.setValue("device/rtltcpPort", p); }
    void setGain(int t)        { s_.setValue("rx/gain", t); }
    void setSampleRate(uint r) { s_.setValue("rx/sps", r); }
    void setFftSize(int v)     { s_.setValue("rx/fftSize", v); }
    void setAgc(bool v)        { s_.setValue("rx/agc", v); }

    void setSdrplayIfMode(int v)    { s_.setValue("device/sdrplayIfMode", v); }
    void setSdrplayLnaState(int v)  { s_.setValue("device/sdrplayLnaState", v); }
    void setSdrplayIfGain(int v)    { s_.setValue("device/sdrplayIfGain", v); }
    void setSdrplayIfAgc(bool v)    { s_.setValue("device/sdrplayIfAgc", v); }
    void setSdrplayBw(int v)        { s_.setValue("device/sdrplayBw", v); }
    void setBiasT(bool v)      { s_.setValue("rx/bias", v); }
    void setQuadrature(bool v) { s_.setValue("rx/quadrature", v); }
    void setQMode(const QString& m) {
        const QString v = (m == QLatin1String("on") || m == QLatin1String("auto"))
                          ? m : QStringLiteral("off");
        s_.setValue("rx/qmode", v);
        // Mantem o campo antigo coerente para quem ainda o le.
        s_.setValue("rx/quadrature", v != QLatin1String("off"));
    }
    void setPpm(int v)         { s_.setValue("rx/ppm", v); }
    void setIqCorrection(bool v) { s_.setValue("rx/iqCorrection", v); }
    void setSmeterHfOffset(double v)   { s_.setValue("smeter/hfOffset", v); }
    void setSmeterVhfOffset(double v)  { s_.setValue("smeter/vhfOffset", v); }
    void setSmeterS9Hf(int v)          { s_.setValue("smeter/s9Hf", v); }
    void setSmeterS9Vhf(int v)         { s_.setValue("smeter/s9Vhf", v); }
    void setSmeterHfEmpty(int v)       { s_.setValue("smeter/hfEmpty", v); }
    void setSmeterVhfEmpty(int v)      { s_.setValue("smeter/vhfEmpty", v); }
    void setSmeterRmsAligned(bool v)   { s_.setValue("smeter/rmsAligned", v); }

    void sync() { s_.sync(); }

private:
    Config() : s_("PU1XTB","RXSDR") {}
    QSettings s_;
};

} // namespace masdr
