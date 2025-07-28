#ifndef DISKUSAGEWIDGET_H
#define DISKUSAGEWIDGET_H
#include "iDescriptor.h"

#include <QWidget>
#include <cstdint>

class DiskUsageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DiskUsageWidget(iDescriptorDevice *device,
                             QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void fetchData();

    enum State { Loading, Ready, Error };

    iDescriptorDevice *m_device;
    State m_state;
    QString m_errorMessage;

    uint64_t m_totalCapacity;
    uint64_t m_systemUsage;
    uint64_t m_appsUsage;
    uint64_t m_mediaUsage;
    uint64_t m_othersUsage;
    uint64_t m_freeSpace;
};

#endif // DISKUSAGEWIDGET_H
