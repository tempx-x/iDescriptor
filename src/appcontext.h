#ifndef APPCONTEXT_H
#define APPCONTEXT_H

#include "iDescriptor.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>

class AppContext : public QObject
{
    Q_OBJECT
public:
    static AppContext *sharedInstance();
    iDescriptorDevice *getDevice(const std::string &udid);
    QList<iDescriptorDevice *> getAllDevices();
    explicit AppContext(QObject *parent = nullptr);
    void handleDBusSignal(const QDBusMessage &msg);
    bool noDevicesConnected() const;

    // Returns whether there are any devices connected (regular or recovery)
    QList<iDescriptorRecoveryDevice *> getAllRecoveryDevices();
    ~AppContext();
    int getConnectedDeviceCount() const;

private:
    QMap<std::string, iDescriptorDevice *> m_devices;
    QMap<uint64_t, iDescriptorRecoveryDevice *> m_recoveryDevices;
    QStringList m_pendingDevices;
signals:
    void deviceAdded(iDescriptorDevice *device);
    void deviceRemoved(const std::string &udid);
    void devicePaired(iDescriptorDevice *device);
    void devicePasswordProtected(const QString &udid);
    void recoveryDeviceAdded(const iDescriptorRecoveryDevice *deviceInfo);
    void recoveryDeviceRemoved(uint64_t ecid);
    void devicePairPending(const QString &udid);
    void devicePairingExpired(const QString &udid);
    void systemSleepStarting();
    void systemWakeup();
    /*
        Generic change event for any device state change we
        need this because many UI elements need to update by
        listening for this only you can watch for any event
        and using the public members of this class you can
        do anything you want
    */
    void deviceChange();
public slots:
    void removeDevice(QString udid);
    void addDevice(QString udid, idevice_connection_type connType,
                   AddType addType);
    void addRecoveryDevice(uint64_t ecid);
    void removeRecoveryDevice(uint64_t ecid);
};

#endif // APPCONTEXT_H
