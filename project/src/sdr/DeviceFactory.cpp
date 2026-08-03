#include "DeviceFactory.h"
#include "RtlSdrDevice.h"
#include "RtlTcpClient.h"
#include "SdrplayDevice.h"

#include <QJsonObject>

namespace masdr {

QJsonArray DeviceFactory::scanAll(const QString& activeType, const QString& activeSerial) {
    QJsonArray arr;
    
    // RTL-SDR: enumerar SEMPRE todos os dongles; marcar o ativo como "(Em uso)".
    // (Antes, com um dongle aberto, so ele aparecia - escondia o 2o dongle.)
    {
        bool activeRtl = (activeType == QStringLiteral("rtlsdr") && !activeSerial.isEmpty());
        bool activeListed = false;
        for (const auto& info : RtlSdrDevice::enumerate()) {
            QJsonObject o;
            o["type"]   = "rtlsdr";
            o["serial"] = info.serial;
            bool isActive = (activeRtl && info.serial == activeSerial);
            o["name"]   = isActive ? QStringLiteral("RTL-SDR (Em uso)") : info.name;
            if (isActive) activeListed = true;
            arr.append(o);
        }
        if (activeRtl && !activeListed) {
            QJsonObject o;
            o["type"]   = "rtlsdr";
            o["serial"] = activeSerial;
            o["name"]   = "RTL-SDR (Em uso)";
            arr.append(o);
        }
    }

    // SDRplay (via SDRplay API v3)
    if (activeType == QStringLiteral("sdrplay") && !activeSerial.isEmpty()) {
        QJsonObject o;
        o["type"]   = "sdrplay";
        o["serial"] = activeSerial;
        o["name"]   = "SDRplay (Em uso)";
        arr.append(o);
    } else {
        for (const auto& info : SdrplayDevice::enumerate()) {
            QJsonObject o;
            o["type"]   = "sdrplay";
            o["serial"] = info.serial;
            o["name"]   = info.name;
            arr.append(o);
        }
    }

    // Fonte RTL-TCP remota (manual): sempre disponível na UI.
    {
        QJsonObject o;
        o["type"] = "rtltcp";
        o["serial"] = "127.0.0.1:1234";
        o["name"] = "RTL-TCP (manual endpoint)";
        arr.append(o);
    }
    return arr;
}

std::shared_ptr<ISdrDevice> DeviceFactory::create(const QString& type) {
    if (type == "rtlsdr")   return std::make_shared<RtlSdrDevice>();
    if (type == "rtltcp")   return std::make_shared<RtlTcpClient>();
    if (type == "sdrplay")  return std::make_shared<SdrplayDevice>();
    return nullptr;
}

} // namespace masdr
