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
    std::string addDevice(iDescriptorDevice *device);

    std::string addRecoveryDevice(RecoveryDeviceInfo *deviceInfo);
    // std::string addRecoveryDevice(const RecoveryDeviceInfo& deviceInfo);
    void removeRecoveryDevice(const QString &udid);

    // Returns whether there are any devices connected (regular or recovery)
    QList<RecoveryDeviceInfo *> getAllRecoveryDevices();
    ~AppContext();
    void instanceRemoveDevice(QString _udid);
    int getConnectedDeviceCount() const;

private:
    QMap<std::string, iDescriptorDevice *> m_devices;
    QMap<std::string, RecoveryDeviceInfo *> m_recoveryDevices;
    QStringList m_pendingDevices;
signals:
    void deviceAdded(iDescriptorDevice *device);
    void deviceRemoved(const std::string &udid);
    void devicePaired(iDescriptorDevice *device);
    void devicePasswordProtected(const QString &udid);
    void recoveryDeviceAdded(RecoveryDeviceInfo *deviceInfo);
    void recoveryDeviceRemoved(const QString &udid);
    void devicePairPending(const QString &udid);
    void systemSleepStarting();
    void systemWakeup();
public slots:
    void removeDevice(QString udid);
    void addDevice(QString udid, idevice_connection_type connType,
                   AddType addType);
};

#endif // APPCONTEXT_H
