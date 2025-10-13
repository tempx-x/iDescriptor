#include "jailbrokenwidget.h"
#include "appcontext.h"
#include "responsiveqlabel.h"
#include "sshterminalwidget.h"

#ifdef __linux__
#include "core/services/avahi/avahi_service.h"
#else
#include "core/services/dnssd/dnssd_service.h"
#endif

#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include <QButtonGroup>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

// TODO: theming is broken
JailbrokenWidget::JailbrokenWidget(QWidget *parent) : QWidget{parent}
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    // Create responsive image label
    ResponsiveQLabel *deviceImageLabel = new ResponsiveQLabel(this);
    deviceImageLabel->setPixmap(QPixmap(":/resources/iphone.png"));
    deviceImageLabel->setMinimumWidth(200);
    deviceImageLabel->setSizePolicy(QSizePolicy::Ignored,
                                    QSizePolicy::Expanding);
    deviceImageLabel->setStyleSheet("background: transparent; border: none;");

    mainLayout->addWidget(deviceImageLabel, 1);

    // Connect to AppContext for device events
    connect(AppContext::sharedInstance(), &AppContext::deviceAdded, this,
            &JailbrokenWidget::onWiredDeviceAdded);
    connect(AppContext::sharedInstance(), &AppContext::deviceRemoved, this,
            &JailbrokenWidget::onWiredDeviceRemoved);

#ifdef __linux__
    m_wirelessProvider = new AvahiService(this);
    connect(m_wirelessProvider, &AvahiService::deviceAdded, this,
            &JailbrokenWidget::onWirelessDeviceAdded);
    connect(m_wirelessProvider, &AvahiService::deviceRemoved, this,
            &JailbrokenWidget::onWirelessDeviceRemoved);
#else
    m_wirelessProvider = new DnssdService(this);
    connect(m_wirelessProvider, &DnssdService::deviceAdded, this,
            &JailbrokenWidget::onWirelessDeviceAdded);
    connect(m_wirelessProvider, &DnssdService::deviceRemoved, this,
            &JailbrokenWidget::onWirelessDeviceRemoved);
#endif

    // Right side: Device selection and Terminal
    QWidget *rightContainer = new QWidget();
    rightContainer->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Expanding);
    rightContainer->setMinimumWidth(400);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(15, 15, 15, 15);
    rightLayout->setSpacing(10);

    setupDeviceSelectionUI(rightLayout);

    mainLayout->addWidget(rightContainer, 3);

    // Start scanning for wireless devices
    m_wirelessProvider->startBrowsing();

    // Populate initial devices
    updateDeviceList();
}

void JailbrokenWidget::setupDeviceSelectionUI(QVBoxLayout *layout)
{
    // Create scroll area for device selection
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(200);
    scrollArea->setMaximumHeight(300);
    scrollArea->setObjectName("devicescrollArea");

    scrollArea->setStyleSheet("QWidget#devicescrollArea {border: none;}");
    QWidget *scrollContent = new QWidget();
    m_deviceLayout = new QVBoxLayout(scrollContent);
    m_deviceLayout->setContentsMargins(5, 5, 5, 5);
    m_deviceLayout->setSpacing(10);

    // Button group for device selection
    m_deviceButtonGroup = new QButtonGroup(this);
    connect(m_deviceButtonGroup,
            QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, &JailbrokenWidget::onDeviceSelected);

    // Wired devices group
    m_wiredDevicesGroup = new QGroupBox("Connected Devices");
    m_wiredDevicesLayout = new QVBoxLayout(m_wiredDevicesGroup);
    m_deviceLayout->addWidget(m_wiredDevicesGroup);

    // Wireless devices group
    m_wirelessDevicesGroup = new QGroupBox("Network Devices");
    m_wirelessDevicesLayout = new QVBoxLayout(m_wirelessDevicesGroup);
    m_deviceLayout->addWidget(m_wirelessDevicesGroup);

    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea);

    // Info and connect button
    m_infoLabel = new QLabel("Select a device to connect");
    layout->addWidget(m_infoLabel);

    m_connectButton = new QPushButton("Open SSH Terminal");
    m_connectButton->setEnabled(false);
    connect(m_connectButton, &QPushButton::clicked, this,
            &JailbrokenWidget::onOpenSSHTerminal);
    layout->addWidget(m_connectButton);
}

void JailbrokenWidget::updateDeviceList()
{
    // Clear existing devices
    clearDeviceButtons();

    // Add wired devices
    QList<iDescriptorDevice *> wiredDevices =
        AppContext::sharedInstance()->getAllDevices();
    for (iDescriptorDevice *device : wiredDevices) {
        addWiredDevice(device);
    }

    // Add wireless devices
    QList<NetworkDevice> wirelessDevices =
        m_wirelessProvider->getNetworkDevices();
    for (const NetworkDevice &device : wirelessDevices) {
        addWirelessDevice(device);
    }
}

void JailbrokenWidget::clearDeviceButtons()
{
    // Remove all buttons from button group and layouts
    for (QAbstractButton *button : m_deviceButtonGroup->buttons()) {
        m_deviceButtonGroup->removeButton(button);
        button->deleteLater();
    }

    // Clear layouts
    QLayoutItem *item;
    while ((item = m_wiredDevicesLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    while ((item = m_wirelessDevicesLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
}

void JailbrokenWidget::addWiredDevice(iDescriptorDevice *device)
{
    QString deviceName = QString::fromStdString(device->deviceInfo.deviceName);
    QString udid = QString::fromStdString(device->udid);
    QString displayText = QString("%1\n%2").arg(deviceName, udid);

    QRadioButton *radioButton = new QRadioButton(displayText);
    radioButton->setProperty("deviceType", "wired");
    radioButton->setProperty("devicePointer",
                             QVariant::fromValue(static_cast<void *>(device)));
    radioButton->setProperty("udid", udid);

    m_deviceButtonGroup->addButton(radioButton);
    m_wiredDevicesLayout->addWidget(radioButton);
}

void JailbrokenWidget::addWirelessDevice(const NetworkDevice &device)
{
    QString displayText = QString("%1\n%2").arg(device.name, device.address);

    QRadioButton *radioButton = new QRadioButton(displayText);
    radioButton->setProperty("deviceType", "wireless");
    radioButton->setProperty("deviceAddress", device.address);
    radioButton->setProperty("deviceName", device.name);
    radioButton->setProperty("devicePort", device.port);

    m_deviceButtonGroup->addButton(radioButton);
    m_wirelessDevicesLayout->addWidget(radioButton);
}

void JailbrokenWidget::onWiredDeviceAdded(iDescriptorDevice *device)
{
    addWiredDevice(device);
}

void JailbrokenWidget::onWiredDeviceRemoved(const std::string &udid)
{
    QString qudid = QString::fromStdString(udid);

    // Find and remove the corresponding radio button
    for (QAbstractButton *button : m_deviceButtonGroup->buttons()) {
        if (button->property("deviceType").toString() == "wired" &&
            button->property("udid").toString() == qudid) {
            m_deviceButtonGroup->removeButton(button);
            button->deleteLater();
            break;
        }
    }

    // Reset selection if this device was selected
    if (m_selectedDeviceType == DeviceType::Wired && m_selectedWiredDevice &&
        m_selectedWiredDevice->udid == udid) {
        resetSelection();
    }
}

void JailbrokenWidget::onWirelessDeviceAdded(const NetworkDevice &device)
{
    addWirelessDevice(device);
}

void JailbrokenWidget::onWirelessDeviceRemoved(const QString &deviceName)
{
    // Find and remove the corresponding radio button
    for (QAbstractButton *button : m_deviceButtonGroup->buttons()) {
        if (button->property("deviceType").toString() == "wireless" &&
            button->property("deviceName").toString() == deviceName) {
            m_deviceButtonGroup->removeButton(button);
            button->deleteLater();
            break;
        }
    }

    // Reset selection if this device was selected
    if (m_selectedDeviceType == DeviceType::Wireless &&
        m_selectedNetworkDevice.name == deviceName) {
        resetSelection();
    }
}

void JailbrokenWidget::onDeviceSelected(QAbstractButton *button)
{
    QString deviceType = button->property("deviceType").toString();

    if (deviceType == "wired") {
        m_selectedDeviceType = DeviceType::Wired;
        m_selectedWiredDevice = static_cast<iDescriptorDevice *>(
            button->property("devicePointer").value<void *>());

        if (m_selectedWiredDevice->deviceInfo.jailbroken) {
            m_infoLabel->setText("Jailbroken device selected");
        } else {
            m_infoLabel->setText(
                "Device selected (detected as non-jailbroken)");
        }
    } else if (deviceType == "wireless") {
        m_selectedDeviceType = DeviceType::Wireless;
        m_selectedNetworkDevice.name =
            button->property("deviceName").toString();
        m_selectedNetworkDevice.address =
            button->property("deviceAddress").toString();
        m_selectedNetworkDevice.port = button->property("devicePort").toUInt();

        m_infoLabel->setText(
            "Network device selected (jailbreak status unknown)");
    }

    m_connectButton->setEnabled(true);
    m_connectButton->setText("Open SSH Terminal");
}

void JailbrokenWidget::resetSelection()
{
    m_selectedDeviceType = DeviceType::None;
    m_selectedWiredDevice = nullptr;
    m_selectedNetworkDevice = NetworkDevice{};
    m_connectButton->setEnabled(false);
    m_infoLabel->setText("Select a device to connect");

    // Uncheck all radio buttons
    if (m_deviceButtonGroup->checkedButton()) {
        m_deviceButtonGroup->setExclusive(false);
        m_deviceButtonGroup->checkedButton()->setChecked(false);
        m_deviceButtonGroup->setExclusive(true);
    }
}

void JailbrokenWidget::onOpenSSHTerminal()
{
    if (m_selectedDeviceType == DeviceType::None) {
        m_infoLabel->setText("Please select a device first");
        return;
    }

    // Prepare connection info
    ConnectionInfo connectionInfo;

    if (m_selectedDeviceType == DeviceType::Wired) {
        if (!m_selectedWiredDevice) {
            m_infoLabel->setText("No wired device selected");
            return;
        }

        connectionInfo.type = ConnectionType::Wired;
        connectionInfo.deviceName = QString::fromStdString(
            m_selectedWiredDevice->deviceInfo.deviceName);
        connectionInfo.deviceUdid =
            QString::fromStdString(m_selectedWiredDevice->udid);
        connectionInfo.hostAddress = "127.0.0.1";
        connectionInfo.port = 22;

    } else if (m_selectedDeviceType == DeviceType::Wireless) {
        connectionInfo.type = ConnectionType::Wireless;
        connectionInfo.deviceName = m_selectedNetworkDevice.name;
        connectionInfo.deviceUdid = "";
        connectionInfo.hostAddress = m_selectedNetworkDevice.address;
        connectionInfo.port = m_selectedNetworkDevice.port;
    }

    // Create and show SSH terminal widget in a new window
    SSHTerminalWidget *sshTerminal = new SSHTerminalWidget(connectionInfo);
    sshTerminal->setAttribute(Qt::WA_DeleteOnClose);
    sshTerminal->show();
    sshTerminal->raise();
    sshTerminal->activateWindow();
}

JailbrokenWidget::~JailbrokenWidget() {}
