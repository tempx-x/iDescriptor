#ifndef NETWORKDEVICESWIDGET_H
#define NETWORKDEVICESWIDGET_H

#ifdef __linux__
#include "core/services/avahi/avahi_service.h"
#else
#include "core/services/dnssd/dnssd_service.h"
#endif

#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

class NetworkDevicesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NetworkDevicesWidget(QWidget *parent = nullptr);
    ~NetworkDevicesWidget();

private slots:
    void onWirelessDeviceAdded(const NetworkDevice &device);
    void onWirelessDeviceRemoved(const QString &deviceName);

private:
    void setupUI();
    void createDeviceCard(const NetworkDevice &device);
    void clearDeviceCards();
    void updateDeviceList();

    QGroupBox *m_deviceGroup = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_deviceLayout = nullptr;
    QLabel *m_statusLabel = nullptr;

#ifdef __linux__
    AvahiService *m_networkProvider = nullptr;
#else
    DnssdService *m_networkProvider = nullptr;
#endif

    QList<QWidget *> m_deviceCards;
};

#endif // NETWORKDEVICESWIDGET_H