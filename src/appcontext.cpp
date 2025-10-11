#include "appcontext.h"
#include "iDescriptor.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QUuid>

AppContext *AppContext::sharedInstance()
{
    static AppContext instance;
    return &instance;
}

AppContext::AppContext(QObject *parent) : QObject{parent}
{
    // TODO: IMPLEMENT
    // QDBusConnection bus = QDBusConnection::systemBus();

    // // Connect to the logind Manager's PrepareForSleep signal
    // bool connected = bus.connect("org.freedesktop.login1",  // service
    //                              "/org/freedesktop/login1", // object path
    //                              "org.freedesktop.login1.Manager", //
    //                              interface "PrepareForSleep", // signal name
    //                              this,              // receiver
    //                              SLOT(handleDBusSignal(QDBusMessage &)) //
    //                              slot
    // );

    // if (!connected) {
    //     qDebug() << "Failed to connect to PrepareForSleep signal.";
    // } else {
    //     qDebug() << "Successfully connected to PrepareForSleep signal.";
    // }
}

void AppContext::handleDBusSignal(const QDBusMessage &msg)
{
    if (msg.arguments().isEmpty()) {
        qWarning() << "Received PrepareForSleep signal with no arguments.";
        return;
    }

    QVariant firstArg = msg.arguments().at(0);
    if (!firstArg.canConvert<bool>()) {
        qWarning()
            << "First argument of PrepareForSleep signal is not a boolean.";
        return;
    }

    bool isSleeping = firstArg.toBool();
    if (isSleeping) {
        qDebug() << "System is going to sleep...";
        emit systemSleepStarting();
        // Clean up device resources before sleep
        // cleanupAllDevices();
    } else {
        qDebug() << "System is resuming from sleep.";
        emit systemWakeup();
    }
}

void AppContext::addDevice(QString udid, idevice_connection_type conn_type,
                           AddType addType)
{
    try {
        iDescriptorInitDeviceResult initResult =
            init_idescriptor_device(udid.toStdString().c_str());

        qDebug() << "init_idescriptor_device success ?: " << initResult.success;
        qDebug() << "init_idescriptor_device error code: " << initResult.error;

        if (!initResult.success) {
            qDebug() << "Failed to initialize device with UDID: " << udid;
            // return onDeviceInitFailed(udid, initResult.error);
            if (initResult.error == LOCKDOWN_E_PASSWORD_PROTECTED) {
                if (addType == AddType::Regular) {
                    m_pendingDevices.append(udid);
                    emit devicePasswordProtected(udid);
                    emit deviceChange();
                    QTimer::singleShot(30000, this, [this, udid]() {
                        // After 30 seconds, if the device is still pending,
                        // consider the pairing expired
                        qDebug()
                            << "Pairing timer fired for device UDID: " << udid;
                        if (m_pendingDevices.contains(udid)) {
                            qDebug()
                                << "Pairing expired for device UDID: " << udid;
                            m_pendingDevices.removeAll(udid);
                            emit devicePairingExpired(udid);
                            emit deviceChange();
                        }
                    });
                }
            } else if (initResult.error ==
                       LOCKDOWN_E_PAIRING_DIALOG_RESPONSE_PENDING) {
                m_pendingDevices.append(udid);
                emit devicePairPending(udid);
                emit deviceChange();
                QTimer::singleShot(30000, this, [this, udid]() {
                    // After 30 seconds, if the device is still pending,
                    // consider the pairing expired
                    qDebug() << "Pairing timer fired for device UDID: " << udid;
                    if (m_pendingDevices.contains(udid)) {
                        qDebug() << "Pairing expired for device UDID: " << udid;
                        m_pendingDevices.removeAll(udid);
                        emit devicePairingExpired(udid);
                        emit deviceChange();
                    }
                });
            } else {
                qDebug() << "Unhandled error for device UDID: " << udid
                         << " Error code: " << initResult.error;
            }
            return;
        }
        qDebug() << "Device initialized: " << udid;

        iDescriptorDevice *device = new iDescriptorDevice{
            .udid = udid.toStdString(),
            .conn_type = conn_type,
            .device = initResult.device,
            .deviceInfo = initResult.deviceInfo,
            .afcClient = initResult.afcClient,
            .afc2Client = initResult.afc2Client,
            .mutex = new std::recursive_mutex(),
        };
        m_devices[device->udid] = device;
        if (addType == AddType::Regular) {
            emit deviceAdded(device);
            emit deviceChange();
            return;
        }
        emit devicePaired(device);
        emit deviceChange();
        m_pendingDevices.removeAll(udid);

    } catch (const std::exception &e) {
        qDebug() << "Exception in onDeviceAdded: " << e.what();
    }
}

int AppContext::getConnectedDeviceCount() const
{
    return m_devices.size() + m_recoveryDevices.size();
}

/*
    FIXME:
    on macOS, sometimes you get wireless disconnects even though we are not
    listening for wireless devices it does not have any to do with us, but it
    still happens so be aware of that
*/
void AppContext::removeDevice(QString _udid)
{
    const std::string uuid = _udid.toStdString();
    qDebug() << "AppContext::removeDevice device with UUID:"
             << QString::fromStdString(uuid);

    if (m_pendingDevices.contains(_udid)) {
        emit devicePairingExpired(_udid);
        emit deviceChange();
        m_pendingDevices.removeAll(_udid);
        return;
    } else {
        qDebug() << "Device with UUID " + _udid +
                        " not found in pending devices.";
    }

    if (!m_devices.contains(uuid)) {
        qDebug() << "Device with UUID " + _udid +
                        " not found in normal devices.";
        return;
    }

    iDescriptorDevice *device = m_devices[uuid];
    m_devices.remove(uuid);

    emit deviceRemoved(uuid);
    emit deviceChange();

    std::lock_guard<std::recursive_mutex> lock(*device->mutex);

    if (device->afcClient)
        afc_client_free(device->afcClient);
    idevice_free(device->device);
    delete device->mutex;
    delete device;
}

void AppContext::removeRecoveryDevice(uint64_t ecid)
{
    if (!m_recoveryDevices.contains(ecid)) {
        qDebug() << "Device with ECID " + QString::number(ecid) +
                        " not found. Please report this issue.";
        return;
    }

    qDebug() << "Removing recovery device with ECID:" << ecid;

    m_recoveryDevices.remove(ecid);
    emit recoveryDeviceRemoved(ecid);
    emit deviceChange();
    iDescriptorRecoveryDevice *deviceInfo = m_recoveryDevices[ecid];

    delete deviceInfo;
}

iDescriptorDevice *AppContext::getDevice(const std::string &uuid)
{
    return m_devices.value(uuid, nullptr);
}

QList<iDescriptorDevice *> AppContext::getAllDevices()
{
    return m_devices.values();
}

QList<iDescriptorRecoveryDevice *> AppContext::getAllRecoveryDevices()
{
    return m_recoveryDevices.values();
}

// Returns whether there are any devices connected (regular or recovery)
bool AppContext::noDevicesConnected() const
{
    return (m_devices.isEmpty() && m_recoveryDevices.isEmpty() &&
            m_pendingDevices.isEmpty());
}

void AppContext::addRecoveryDevice(uint64_t ecid)
{
    iDescriptorInitDeviceResultRecovery res =
        init_idescriptor_recovery_device(ecid);

    if (!res.success) {
        qDebug() << "Failed to initialize recovery device with ECID: "
                 << QString::number(ecid);
        qDebug() << "Error code: " << res.error;
        return;
    }

    iDescriptorRecoveryDevice *recoveryDevice = new iDescriptorRecoveryDevice();
    recoveryDevice->ecid = res.deviceInfo.ecid;
    recoveryDevice->mode = res.mode;
    recoveryDevice->cpid = res.deviceInfo.cpid;
    recoveryDevice->bdid = res.deviceInfo.bdid;
    recoveryDevice->displayName = res.displayName;

    m_recoveryDevices[res.deviceInfo.ecid] = recoveryDevice;
    emit recoveryDeviceAdded(recoveryDevice);
    emit deviceChange();
}

AppContext::~AppContext()
{
    for (auto device : m_devices) {
        emit deviceRemoved(device->udid);
        if (device->afcClient)
            afc_client_free(device->afcClient);
        idevice_free(device->device);
        delete device;
    }

    // TODO
    for (auto recoveryDevice : m_recoveryDevices) {
        // emit
        // recoveryDeviceRemoved(QString::fromStdString(recoveryDevice->ecid));
        // delete recoveryDevice;
    }
}