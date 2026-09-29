#include "DeviceFactory.h"
#include "RtlSdrDevice.h"
#include "RtlTcpClient.h"
#include "SdrplayDevice.h"

namespace masdr {

Json DeviceFactory::scanAll(const std::string& activeType, const std::string& activeSerial) {
    Json arr = Json::array();

    // RTL-SDR
    if (activeType == "rtlsdr" && !activeSerial.empty()) {
        Json o = Json::object();
        o["type"]   = Json("rtlsdr");
        o["serial"] = Json(activeSerial);
        o["name"]   = Json("RTL-SDR (Em uso)");
        arr.push(o);
    } else {
        for (const auto& info : RtlSdrDevice::enumerate()) {
            Json o = Json::object();
            o["type"]   = Json("rtlsdr");
            o["serial"] = Json(info.serial);
            o["name"]   = Json(info.name);
            arr.push(o);
        }
    }

    // SDRplay
    if (activeType == "sdrplay" && !activeSerial.empty()) {
        Json o = Json::object();
        o["type"]   = Json("sdrplay");
        o["serial"] = Json(activeSerial);
        o["name"]   = Json("SDRplay (Em uso)");
        arr.push(o);
    } else {
        for (const auto& info : SdrplayDevice::enumerate()) {
            Json o = Json::object();
            o["type"]   = Json("sdrplay");
            o["serial"] = Json(info.serial);
            o["name"]   = Json(info.name);
            arr.push(o);
        }
    }

    // RTL-TCP (sempre disponivel)
    {
        Json o = Json::object();
        o["type"]   = Json("rtltcp");
        o["serial"] = Json("127.0.0.1:1234");
        o["name"]   = Json("RTL-TCP (manual endpoint)");
        arr.push(o);
    }

    return arr;
}

std::shared_ptr<ISdrDevice> DeviceFactory::create(const std::string& type) {
    if (type == "rtlsdr")  return std::make_shared<RtlSdrDevice>();
    if (type == "rtltcp")  return std::make_shared<RtlTcpClient>();
    if (type == "sdrplay") return std::make_shared<SdrplayDevice>();
    return nullptr;
}

} // namespace masdr
