#include "DeviceFactory.h"
#include "RtlSdrDevice.h"
#include "RtlTcpClient.h"
#include "SdrplayDevice.h"
#include "ExtIoDevice.h"

#include <QFileInfo>

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

    // ExtIO: uma entrada por DLL encontrada na pasta extio/.
    //
    // Nao ha como "enumerar" hardware aqui: quem sabe o que esta ligado e a
    // propria DLL, e descobrir isso exigiria abrir cada uma - o que tomaria o
    // aparelho de quem ja estivesse usando. Entao listamos os ARQUIVOS, e o
    // usuario escolhe. Vale para SDR-IQ, Perseus, FiFi, Si570 e o que mais
    // tiver ExtIO.
#ifdef Q_OS_WIN
    for (const QString& dll : ExtIoDevice::procurarDlls()) {
        QJsonObject o;
        o["type"]   = "extio";
        o["serial"] = dll;
        const bool ativo = (activeType == QStringLiteral("extio") && dll == activeSerial);
        o["name"]   = QStringLiteral("ExtIO: %1%2")
                          .arg(QFileInfo(dll).completeBaseName(),
                               ativo ? QStringLiteral(" (Em uso)") : QString());
        arr.append(o);
    }
    // Ja escolhida uma DLL de fora da pasta: ela continua na lista.
    if (activeType == QStringLiteral("extio") && !activeSerial.isEmpty()) {
        bool listada = false;
        for (const auto v : arr) {
            const QJsonObject o = v.toObject();
            if (o.value("type").toString() == QStringLiteral("extio") &&
                o.value("serial").toString() == activeSerial) { listada = true; break; }
        }
        if (!listada) {
            QJsonObject o;
            o["type"]   = "extio";
            o["serial"] = activeSerial;
            o["name"]   = QStringLiteral("ExtIO: %1 (Em uso)")
                              .arg(QFileInfo(activeSerial).completeBaseName());
            arr.append(o);
        }
    }
#endif
    return arr;
}

std::shared_ptr<ISdrDevice> DeviceFactory::create(const QString& type) {
    if (type == "rtlsdr")   return std::make_shared<RtlSdrDevice>();
    if (type == "rtltcp")   return std::make_shared<RtlTcpClient>();
    if (type == "sdrplay")  return std::make_shared<SdrplayDevice>();
    if (type == "extio")    return std::make_shared<ExtIoDevice>();
    return nullptr;
}

} // namespace masdr
