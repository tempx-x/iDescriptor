#ifndef DEVICEINFOWIDGET_H
#define DEVICEINFOWIDGET_H
#include "iDescriptor.h"
#include <QWidget>

class DeviceInfoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceInfoWidget(iDescriptorDevice *device,
                              QWidget *parent = nullptr);

private slots:
    void onBatteryMoreClicked();

private:
    QPixmap getDeviceIcon(const std::string &productType);
    iDescriptorDevice *device;
};

#endif // DEVICEINFOWIDGET_H
