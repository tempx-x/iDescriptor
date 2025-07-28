#pragma once
#include <QImage>
#include <QtCore/QObject>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libirecovery.h>
#include <string>
#include <unordered_map>
#include <vector>

#define RECOVERY_CLIENT_CONNECTION_TRIES 3
#define APPLE_VENDOR_ID 0x05ac

struct BatteryInfo {
    QString health;
    uint64_t cycleCount;
    // uint64_t designCapacity;
    // uint64_t maxCapacity;
    // uint64_t fullChargeCapacity;
    std::string serialNumber;
};

//! IOS 12
/* {
"AmountDataAvailable": 6663077888,
"AmountDataReserved": 209715200,
"AmountRestoreAvailable": 11524079616,
"CalculateDiskUsage": "OkilyDokily",
"NANDInfo": <01000000 01000000 01000000 00000080 ... 00 00000000 000000>,
"TotalDataAvailable": 6872793088,
"TotalDataCapacity": 11306721280,
"TotalDiskCapacity": 16000000000,
"TotalSystemAvailable": 0,
"TotalSystemCapacity": 4693204992
}*/
struct DiskInfo {
    uint64_t totalDiskCapacity;
    uint64_t totalDataCapacity;
    uint64_t totalSystemCapacity;
    uint64_t totalDataAvailable;
};

struct DeviceInfo {
    enum class ActivationState {
        Activated,
        FactoryActivated,
        Unactivated
    } activationState;
    std::string activationStateAcknowledged;
    std::string productType;
    bool jailbroken;
    std::string basebandActivationTicketVersion;
    std::string basebandCertId;
    std::string basebandChipID;
    std::string basebandKeyHashInformation;
    std::string aKeyStatus;
    std::string sKeyHash;
    std::string sKeyStatus;
    std::string basebandMasterKeyHash;
    std::string basebandRegionSKU;
    std::string basebandSerialNumber;
    std::string basebandStatus;
    std::string basebandVersion;
    std::string bluetoothAddress;
    std::string boardId;
    std::string productVersion;
    bool brickState;
    std::string buildVersion;
    std::string cpuArchitecture;
    std::string carrierBundleInfoArray_1;
    std::string cfBundleIdentifier;
    std::string cfBundleVersion;
    std::string gid1;
    std::string gid2;
    std::string integratedCircuitCardIdentity;
    std::string internationalMobileSubscriberIdentity;
    std::string mcc;
    std::string mnc;
    std::string mobileEquipmentIdentifier;
    std::string simGid1;
    std::string simGid2;
    std::string slot;
    std::string kCTPostponementInfoAvailable;
    std::string certID;
    std::string chipID;
    std::string chipSerialNo;
    std::string deviceClass;
    std::string deviceColor;
    std::string deviceName;
    std::string dieID;
    std::string ethernetAddress;
    std::string firmwareVersion;
    int fusingStatus;
    std::string hardwareModel;
    std::string hardwarePlatform;
    bool hasSiDP;
    bool hostAttached;
    std::string internationalMobileEquipmentIdentity;
    bool internationalMobileSubscriberIdentityOverride;
    std::string mlbSerialNumber;
    std::string mobileSubscriberCountryCode;
    std::string mobileSubscriberNetworkCode;
    std::string modelNumber;
    // NonVolatileRAM omitted (unknown type)
    std::string ioNVRAMSyncNowProperty;
    bool systemAudioVolumeSaved;
    bool autoBoot;
    int backlightLevel;
    bool productionDevice;
    BatteryInfo batteryInfo;
    DiskInfo diskInfo;
};

struct iDescriptorDevice {
    std::string udid;
    idevice_connection_type conn_type;
    idevice_t device;
    DeviceInfo deviceInfo;
};

struct IDescriptorInitDeviceResult {
    bool success;
    lockdownd_error_t error;
    idevice_t device;
    DeviceInfo deviceInfo;
};

// Device model identifier to marketing name mapping
const std::unordered_map<std::string, std::string> DEVICE_MAP = {
    {"iPhone1,1", "iPhone 2G"},
    {"iPhone1,2", "iPhone 3G"},
    {"iPhone2,1", "iPhone 3GS"},
    {"iPhone3,1", "iPhone 4 (GSM)"},
    {"iPhone3,2", "iPhone 4 (GSM Rev A)"},
    {"iPhone3,3", "iPhone 4 (CDMA)"},
    {"iPhone4,1", "iPhone 4S"},
    {"iPhone5,1", "iPhone 5 (GSM)"},
    {"iPhone5,2", "iPhone 5 (GSM+CDMA)"},
    {"iPhone5,3", "iPhone 5c (GSM)"},
    {"iPhone5,4", "iPhone 5c (GSM+CDMA)"},
    {"iPhone6,1", "iPhone 5s (GSM)"},
    {"iPhone6,2", "iPhone 5s (GSM+CDMA)"},
    {"iPhone7,1", "iPhone 6 Plus"},
    {"iPhone7,2", "iPhone 6"},
    {"iPhone8,1", "iPhone 6s"},
    {"iPhone8,2", "iPhone 6s Plus"},
    {"iPhone8,4", "iPhone SE (1st generation)"},
    {"iPhone9,1", "iPhone 7 (GSM)"},
    {"iPhone9,2", "iPhone 7 Plus (GSM)"},
    {"iPhone9,3", "iPhone 7 (GSM+CDMA)"},
    {"iPhone9,4", "iPhone 7 Plus (GSM+CDMA)"},
    {"iPhone10,1", "iPhone 8 (GSM)"},
    {"iPhone10,2", "iPhone 8 Plus (GSM)"},
    {"iPhone10,3", "iPhone X (GSM)"}};

struct RecoveryDeviceInfo : public QObject {
    Q_OBJECT
public:
    RecoveryDeviceInfo(const irecv_device_event_t *event,
                       QObject *parent = nullptr)
        : QObject(parent)
    {
        if (event && event->device_info) {
            ecid = event->device_info->ecid;
            mode = event->mode;
            cpid = event->device_info->cpid;
            bdid = event->device_info->bdid;
        }
    }
    uint64_t ecid;
    irecv_mode mode;
    uint32_t cpid;
    uint32_t bdid;
    QString product;
    QString model;
    QString board_id;
};

struct TakeScreenshotResult {
    bool success;
    QImage img;
};

struct IDescriptorInitDeviceResultRecovery {
    irecv_client_t client = nullptr;
    irecv_device_info deviceInfo;
    bool success = false;
    irecv_mode mode = IRECV_K_RECOVERY_MODE_1;
};

void warn(const QString &message, const QString &title = "Warning",
          QWidget *parent = nullptr);

enum class AddType { Regular, Pairing };

#define APP_LABEL "iDescriptor"
#define APP_VERSION "0.0.1"
#define APP_COPYRIGHT "© 2023 Uncore. All rights reserved."

class PlistNavigator
{
private:
    plist_t current_node;

public:
    PlistNavigator(plist_t node) : current_node(node) {}

    PlistNavigator operator[](const char *key)
    {
        if (!current_node || plist_get_node_type(current_node) != PLIST_DICT) {
            return PlistNavigator(nullptr);
        }
        plist_t next = plist_dict_get_item(current_node, key);
        return PlistNavigator(next);
    }

    operator plist_t() const { return current_node; }
    bool valid() const { return current_node != nullptr; }
};
