#pragma once
#include "ISdrDevice.h"
#include <QJsonArray>
#include <memory>

namespace masdr {

class DeviceFactory {
public:
    // Enumera os hardwares disponíveis (RTL-SDR USB + SDRplay).
    // Não inclui RTL-TCP — esse precisa de host/porta informados pelo usuário.
    static QJsonArray scanAll(const QString& activeType = "", const QString& activeSerial = "");

    // Cria instância concreta a partir do tipo ("rtlsdr" | "rtltcp" | "sdrplay").
    static std::shared_ptr<ISdrDevice> create(const QString& type);
};

} // namespace masdr
