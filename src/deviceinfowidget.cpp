#include "deviceinfowidget.h"
#include "diskusagewidget.h"
#include "fileexplorerwidget.h"
#include "iDescriptor.h"
#include <QDebug>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QPainter>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QTabWidget>
#include <QVBoxLayout>

// A custom QGraphicsView that keeps the content fitted with aspect ratio on
// resize
class ResponsiveGraphicsView : public QGraphicsView
{
public:
    ResponsiveGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent)
    {
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        if (scene() && !scene()->items().isEmpty()) {
            fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
        }
        QGraphicsView::resizeEvent(event);
    }
};

DeviceInfoWidget::DeviceInfoWidget(iDescriptorDevice *device, QWidget *parent)
    : QWidget(parent), device(device)
{
    // Main layout with horizontal orientation
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    QGraphicsScene *scene = new QGraphicsScene(this);
    QGraphicsPixmapItem *pixmapItem =
        new QGraphicsPixmapItem(QPixmap(":/resources/iphone.png"));
    scene->addItem(pixmapItem);

    QGraphicsView *graphicsView = new ResponsiveGraphicsView(scene, this);
    graphicsView->setRenderHint(QPainter::Antialiasing);
    graphicsView->setMinimumWidth(200);
    graphicsView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    graphicsView->setStyleSheet("background: transparent; border: none;");

    mainLayout->addWidget(graphicsView, 1); // Stretch factor 1

    // Right side: Info Table
    QWidget *infoContainer = new QWidget();
    infoContainer->setObjectName("infoContainer");
    infoContainer->setStyleSheet("QWidget#infoContainer { "
                                 "   border: 1px solid #ccc; "
                                 "   border-radius: 6px; "
                                 "}");
    infoContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    QVBoxLayout *infoLayout = new QVBoxLayout(infoContainer);
    infoLayout->setContentsMargins(15, 15, 15, 15);
    infoLayout->setSpacing(10);

    // Header
    QLabel *headerLabel = new QLabel(
        "Device: " + QString::fromStdString(device->deviceInfo.productType));
    headerLabel->setStyleSheet("font-size: 1rem; padding-bottom: 10px; "
                               "border-bottom: 1px solid #eee; "
                               "font-weight: bold;");
    infoLayout->addWidget(headerLabel);

    // Grid for device details
    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->setSpacing(8);
    gridLayout->setColumnStretch(1, 1); // Allow value column to stretch
    gridLayout->setColumnStretch(
        3, 1); // Allow value column for right side to stretch

    QList<QPair<QString, QWidget *>> infoItems;

    auto createValueLabel = [](const QString &text) {
        return new QLabel(text);
    };

    infoItems.append({"iOS Version:", createValueLabel(QString::fromStdString(
                                          device->deviceInfo.productVersion))});
    infoItems.append({"Device Name:", createValueLabel(QString::fromStdString(
                                          device->deviceInfo.deviceName))});

    // Activation state label with color and tooltip
    QLabel *activationLabel = new QLabel;
    QString stateText;
    QString tooltipText;
    QColor color;

    switch (device->deviceInfo.activationState) {
    case DeviceInfo::ActivationState::Activated:
        stateText = "Activated";
        color = QColor(0, 180, 0); // Green
        tooltipText = "Device is activated and ready for use.";
        break;
    case DeviceInfo::ActivationState::FactoryActivated:
        stateText = "Factory Activated";
        color = QColor(255, 140, 0); // Orange
        tooltipText = "Activation is most likely bypassed.";
        break;
    default:
        stateText = "Unactivated";
        color = QColor(220, 0, 0); // Red
        tooltipText = "Device is not activated and requires setup.";
        break;
    }

    activationLabel->setText(stateText);
    activationLabel->setStyleSheet("color: " + color.name() + ";");
    activationLabel->setToolTip(tooltipText);
    infoItems.append({"Activation State:", activationLabel});

    infoItems.append({"Device Class:", createValueLabel(QString::fromStdString(
                                           device->deviceInfo.deviceClass))});
    infoItems.append({"Device Color:", createValueLabel(QString::fromStdString(
                                           device->deviceInfo.deviceColor))});
    infoItems.append(
        {"Jailbroken:", createValueLabel(QString::fromStdString(
                            device->deviceInfo.jailbroken ? "Yes" : "No"))});
    infoItems.append({"Model Number:", createValueLabel(QString::fromStdString(
                                           device->deviceInfo.modelNumber))});
    infoItems.append(
        {"CPU Architecture:", createValueLabel(QString::fromStdString(
                                  device->deviceInfo.cpuArchitecture))});
    infoItems.append({"Build Version:", createValueLabel(QString::fromStdString(
                                            device->deviceInfo.buildVersion))});
    infoItems.append(
        {"Hardware Model:", createValueLabel(QString::fromStdString(
                                device->deviceInfo.hardwareModel))});
    infoItems.append(
        {"Hardware Platform:", createValueLabel(QString::fromStdString(
                                   device->deviceInfo.hardwarePlatform))});
    infoItems.append(
        {"Ethernet Address:", createValueLabel(QString::fromStdString(
                                  device->deviceInfo.ethernetAddress))});
    infoItems.append(
        {"Bluetooth Address:", createValueLabel(QString::fromStdString(
                                   device->deviceInfo.bluetoothAddress))});
    infoItems.append(
        {"Firmware Version:", createValueLabel(QString::fromStdString(
                                  device->deviceInfo.firmwareVersion))});

    // Battery Info
    QWidget *batteryWidget = new QWidget();
    QHBoxLayout *batteryLayout = new QHBoxLayout(batteryWidget);
    batteryLayout->setContentsMargins(0, 0, 0, 0);
    batteryLayout->setSpacing(5);
    batteryLayout->addWidget(new QLabel(device->deviceInfo.batteryInfo.health));
    QPushButton *moreButton = new QPushButton("More");
    connect(moreButton, &QPushButton::clicked, this,
            &DeviceInfoWidget::onBatteryMoreClicked);
    batteryLayout->addWidget(moreButton);
    batteryLayout->addStretch();
    infoItems.append({"Battery Health:", batteryWidget});

    infoItems.append(
        {"Production Device:",
         createValueLabel(QString::fromStdString(
             device->deviceInfo.productionDevice ? "Yes" : "No"))});

    // Distribute items into the grid
    int numRows = (infoItems.size() + 1) / 2;
    for (int i = 0; i < numRows; ++i) {
        // Left column item
        QLabel *keyLabelLeft = new QLabel(infoItems[i].first);
        keyLabelLeft->setStyleSheet("font-weight: bold;");
        gridLayout->addWidget(keyLabelLeft, i, 0);
        gridLayout->addWidget(infoItems[i].second, i, 1);

        // Right column item
        int rightIndex = i + numRows;
        if (rightIndex < infoItems.size()) {
            QLabel *keyLabelRight = new QLabel(infoItems[rightIndex].first);
            keyLabelRight->setStyleSheet("font-weight: bold;");
            gridLayout->addWidget(keyLabelRight, i, 2);
            gridLayout->addWidget(infoItems[rightIndex].second, i, 3);
        }
    }

    infoLayout->addLayout(gridLayout);
    infoLayout->addStretch(); // Pushes footer to the bottom

    // Footer
    QLabel *footerLabel =
        new QLabel("UDID: " + QString::fromStdString(device->udid));
    footerLabel->setStyleSheet(
        "font-size: 10px; color: #666; padding-top: 5px; "
        "border-top: 1px solid #eee;");
    footerLabel->setWordWrap(true);
    infoLayout->addWidget(footerLabel);

    // Create a vertical layout for the right side to stack info and disk usage
    QVBoxLayout *rightSideLayout = new QVBoxLayout();
    rightSideLayout->setSpacing(10);
    rightSideLayout->addWidget(infoContainer);
    rightSideLayout->addWidget(new DiskUsageWidget(device, this));
    rightSideLayout->setAlignment(Qt::AlignCenter);
    mainLayout->addLayout(rightSideLayout, 2); // Stretch factor 2
}

void DeviceInfoWidget::onBatteryMoreClicked()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("Battery Details");
    QString details =
        "Battery Cycle Count: " +
        QString::number(device->deviceInfo.batteryInfo.cycleCount) + "\n" +
        "Battery Serial Number: " +
        QString::fromStdString(device->deviceInfo.batteryInfo.serialNumber);
    msgBox.setText(details);
    msgBox.exec();
}

QPixmap DeviceInfoWidget::getDeviceIcon(const std::string &productType)
{
    // Create a simple colored icon based on device type
    QPixmap icon(16, 16);
    icon.fill(Qt::transparent);

    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);

    if (productType.find("iPhone") != std::string::npos) {
        painter.setBrush(QColor(0, 122, 255)); // iOS blue
        painter.drawEllipse(2, 2, 12, 12);
    } else if (productType.find("iPad") != std::string::npos) {
        painter.setBrush(QColor(255, 149, 0)); // Orange
        painter.drawRect(2, 2, 12, 12);
    } else {
        painter.setBrush(QColor(128, 128, 128)); // Gray for unknown
        painter.drawEllipse(2, 2, 12, 12);
    }

    return icon;
}