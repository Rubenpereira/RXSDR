#pragma once
#include <functional>
#include <string>
#include <cstdint>
#include "../../third_party/masdr_json.h"
#include "HttpServer.h"  // HttpRequest / HttpResponse

namespace masdr {

class RestApi {
public:
    RestApi() = default;

    // Ponto de entrada chamado pelo HttpServer
    HttpResponse handle(const HttpRequest& req);

    // Callbacks instalados pelo Application
    std::function<Json()>                                   onListDevices;
    std::function<Json(const std::string&, const std::string&)> onSelectDevice;
    std::function<bool(const std::string&, uint64_t, const std::string&, int)> onTune;
    std::function<bool(uint64_t)>                           onSetCenter;
    std::function<bool(int)>                                onSetGain;
    std::function<bool(bool)>                               onPower;
    std::function<Json()>                                   onStatus;
    std::function<Json()>                                   onGetConfig;
    std::function<bool(const Json&)>                        onSetConfig;
};

} // namespace masdr
