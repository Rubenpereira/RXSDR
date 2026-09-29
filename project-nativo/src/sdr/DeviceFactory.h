#pragma once
#include "ISdrDevice.h"
#include "../../third_party/masdr_json.h"
#include <memory>

namespace masdr {

class DeviceFactory {
public:
    static Json scanAll(const std::string& activeType = "", const std::string& activeSerial = "");
    static std::shared_ptr<ISdrDevice> create(const std::string& type);
};

} // namespace masdr
