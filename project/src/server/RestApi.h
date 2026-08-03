#pragma once
#include <QObject>
#include <QHttpServer>
#include <functional>
#include <QJsonArray>
#include <QJsonObject>

namespace masdr {

class ISdrDevice;

class RestApi : public QObject {
    Q_OBJECT
public:
    explicit RestApi(QObject* parent=nullptr);

    void install(QHttpServer* server);

    void setDevice(ISdrDevice* dev) { dev_ = dev; }
    ISdrDevice* device() const { return dev_; }

    // callbacks que o Application instala para responder aos POSTs
    std::function<QJsonArray()>                              onListDevices;
    std::function<QJsonObject(const QString&, const QString&)> onSelectDevice;
    std::function<bool(const QString&, quint64, const QString&, int)> onTune;
    std::function<bool(quint64)>                             onSetCenter;
    std::function<bool(int)>                                 onSetGain;
    std::function<bool(bool)>                                onPower;
    std::function<QJsonObject()>                             onStatus;
    std::function<QJsonObject()>                             onGetConfig;
    std::function<bool(const QJsonObject&)>                  onSetConfig;

    // AIS Decoder (AIS-catcher)
    std::function<QJsonObject()>                             onAisStatus;
    std::function<QJsonObject(const QJsonObject&)>           onAisStart;
    std::function<QJsonObject()>                             onAisStop;

    // ACARS Decoder (acarsdeco2)
    std::function<QJsonObject()>                             onAcarsStatus;
    std::function<QJsonObject(const QJsonObject&)>           onAcarsStart;
    std::function<QJsonObject()>                             onAcarsStop;

    // DSD Decoder (DSDPlus/dsd)
    std::function<QJsonObject()>                             onDsdStatus;
    std::function<QJsonObject(const QJsonObject&)>           onDsdStart;
    std::function<QJsonObject()>                             onDsdStop;
    std::function<QJsonObject()>                             onDsdTogglePolarity;
    std::function<QJsonObject(int)>                          onDsdSetPcmHz;



    // APRS Decoder (Direwolf)
    std::function<QJsonObject()>                             onAprsStatus;
    std::function<QJsonObject(const QJsonObject&)>           onAprsStart;
    std::function<QJsonObject()>                             onAprsStop;

    // Packet Decoder (Direwolf Packet)
    std::function<QJsonObject()>                             onPacketStatus;
    std::function<QJsonObject(const QJsonObject&)>           onPacketStart;
    std::function<QJsonObject()>                             onPacketStop;


    // SITOR-B Decoder (Transmissões Marinhas)
    std::function<QJsonObject()>                             onSitorBStatus;
    std::function<QJsonObject(const QJsonObject&)>           onSitorBStart;
    std::function<QJsonObject()>                             onSitorBStop;

    // PACTOR Decoder (Pactor-I FSK)
    std::function<QJsonObject()>                             onPactorStatus;
    std::function<QJsonObject(const QJsonObject&)>           onPactorStart;
    std::function<QJsonObject()>                             onPactorStop;

    // DSC Decoder (ITU-R M.493)
    std::function<QJsonObject()>                             onDscStatus;
    std::function<QJsonObject(const QJsonObject&)>           onDscStart;
    std::function<QJsonObject()>                             onDscStop;






    // SELCAL Decoder
    std::function<QJsonObject()>                             onSelcalStatus;
    std::function<QJsonObject(const QJsonObject&)>           onSelcalStart;
    std::function<QJsonObject()>                             onSelcalStop;

    // TETRA Decoder (π/4-DQPSK 18 ksym/s, runner Python)
    std::function<QJsonObject()>                             onTetraStatus;
    std::function<QJsonObject(const QJsonObject&)>           onTetraStart;
    std::function<QJsonObject()>                             onTetraStop;

private:
    ISdrDevice* dev_ = nullptr;
};

} // namespace masdr
