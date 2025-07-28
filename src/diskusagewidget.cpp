#include "diskusagewidget.h"
#include "iDescriptor.h"
#include <QDebug>
#include <QFutureWatcher>
#include <QPainter>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrent>

#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

DiskUsageWidget::DiskUsageWidget(iDescriptorDevice *device, QWidget *parent)
    : QWidget(parent), m_device(device), m_state(Loading), m_totalCapacity(0),
      m_systemUsage(0), m_appsUsage(0), m_mediaUsage(0), m_othersUsage(0),
      m_freeSpace(0)
{
    setMinimumHeight(80);
    fetchData();
}

QSize DiskUsageWidget::sizeHint() const { return QSize(400, 80); }

void DiskUsageWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_state == Loading) {
        painter.setPen(Qt::black);
        painter.drawText(rect(), Qt::AlignCenter, "Loading disk usage...");
        return;
    }

    if (m_state == Error) {
        painter.setPen(Qt::black);
        painter.drawText(rect(), Qt::AlignCenter, "Error: " + m_errorMessage);
        return;
    }

    // Title
    painter.setPen(Qt::black);
    QFont titleFont = font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    QRectF titleRect(0, 5, width(), 20);
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignTop, "Disk Usage");
    painter.setFont(font()); // Reset font

    if (m_totalCapacity == 0) {
        painter.setPen(Qt::black);
        painter.drawText(QRect(0, 30, width(), height() - 30), Qt::AlignCenter,
                         "No disk information available.");
        return;
    }

    painter.setPen(Qt::NoPen);

    const int barHeight = 20;
    QRectF barRect(10, 30, width() - 20, barHeight);

    double scale = (double)barRect.width() / m_totalCapacity;
    double currentX = barRect.left();

    auto drawSegment = [&](uint64_t value, const QColor &color) {
        if (value > 0) {
            double segmentWidth = value * scale;
            painter.fillRect(
                QRectF(currentX, barRect.top(), segmentWidth, barRect.height()),
                color);
            currentX += segmentWidth;
        }
    };

    const QColor systemColor("#E74C3C");
    const QColor appsColor("#3498DB");
    const QColor mediaColor("#2ECC71");
    const QColor othersColor("#F39C12");
    const QColor freeColor("#BDC3C7");

    // System
    drawSegment(m_systemUsage, systemColor);
    // Apps
    drawSegment(m_appsUsage, appsColor);
    // Media
    drawSegment(m_mediaUsage, mediaColor);
    // Others
    drawSegment(m_othersUsage, othersColor);
    // Free
    drawSegment(m_freeSpace, freeColor);

    // Legend
    painter.setPen(Qt::black);
    qreal legendY = barRect.bottom() + 15;
    const int legendBoxSize = 10;
    const int legendSpacing = 5;
    qreal currentLegendX = barRect.left();

    auto drawLegendItem = [&](const QColor &color, const QString &text) {
        painter.fillRect(
            QRectF(currentLegendX, legendY, legendBoxSize, legendBoxSize),
            color);
        currentLegendX += legendBoxSize + legendSpacing;

        QFontMetrics fm(font());
        QRect textRect = fm.boundingRect(text);
        painter.drawText(QPointF(currentLegendX, legendY + legendBoxSize),
                         text);
        currentLegendX += textRect.width() + legendSpacing * 2;
    };

    drawLegendItem(systemColor, "System");
    drawLegendItem(appsColor, "Apps");
    drawLegendItem(mediaColor, "Media");
    drawLegendItem(othersColor, "Others");
    drawLegendItem(freeColor, "Free");
}

void DiskUsageWidget::fetchData()
{
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watcher]() {
                QVariantMap result = watcher->result();
                if (result.contains("error")) {
                    m_state = Error;
                    m_errorMessage = result["error"].toString();
                } else {
                    m_totalCapacity = result["totalCapacity"].toULongLong();
                    m_systemUsage = result["systemUsage"].toULongLong();
                    m_appsUsage = result["appsUsage"].toULongLong();
                    m_mediaUsage = result["mediaUsage"].toULongLong();
                    m_freeSpace = result["freeSpace"].toULongLong();

                    uint64_t usedKnown =
                        m_systemUsage + m_appsUsage + m_mediaUsage;
                    if (m_totalCapacity > (m_freeSpace + usedKnown)) {
                        m_othersUsage =
                            m_totalCapacity - m_freeSpace - usedKnown;
                    } else {
                        m_othersUsage = 0;
                    }

                    m_state = Ready;
                }
                update(); // Trigger repaint
                watcher->deleteLater();
            });

    QFuture<QVariantMap> future = QtConcurrent::run([this]() {
        QVariantMap result;
        if (!m_device || !m_device->device) {
            result["error"] = "Invalid device.";
            return result;
        }

        result["totalCapacity"] = QVariant::fromValue(
            m_device->deviceInfo.diskInfo.totalDiskCapacity);
        result["freeSpace"] = QVariant::fromValue(
            m_device->deviceInfo.diskInfo.totalDataAvailable);
        result["systemUsage"] = QVariant::fromValue(
            m_device->deviceInfo.diskInfo.totalSystemCapacity);

        // Apps usage
        uint64_t totalAppsSpace = 0;
        instproxy_client_t instproxy = nullptr;
        lockdownd_client_t lockdownClient = nullptr;

        if (lockdownd_client_new_with_handshake(m_device->device,
                                                &lockdownClient, APP_LABEL) !=
            LOCKDOWN_E_SUCCESS) {
            result["error"] = "Could not connect to lockdown service.";
            return result;
        }
        lockdownd_service_descriptor_t lockdowndService = nullptr;
        if (lockdownd_start_service(lockdownClient,
                                    "com.apple.mobile.installation_proxy",
                                    &lockdowndService) != LOCKDOWN_E_SUCCESS) {
        }

        if (instproxy_client_new(m_device->device, lockdowndService,
                                 &instproxy) != INSTPROXY_E_SUCCESS) {
            lockdownd_service_descriptor_free(lockdowndService);
        }

        if (instproxy_client_new(m_device->device, lockdowndService,
                                 &instproxy) != INSTPROXY_E_SUCCESS) {
            result["error"] = "Could not connect to installation proxy.";
            return result;
        }

        lockdownd_service_descriptor_free(lockdowndService);

        plist_t client_opts = instproxy_client_options_new();
        plist_dict_set_item(client_opts, "ApplicationType",
                            plist_new_string("User"));

        plist_t return_attrs = plist_new_array();
        plist_array_append_item(return_attrs,
                                plist_new_string("StaticDiskUsage"));
        plist_array_append_item(return_attrs,
                                plist_new_string("DynamicDiskUsage"));
        plist_dict_set_item(client_opts, "ReturnAttributes", return_attrs);

        plist_t apps = nullptr;
        if (instproxy_browse(instproxy, client_opts, &apps) ==
                INSTPROXY_E_SUCCESS &&
            apps) {
            if (plist_get_node_type(apps) == PLIST_ARRAY) {
                for (uint32_t i = 0; i < plist_array_get_size(apps); i++) {
                    plist_t app_info = plist_array_get_item(apps, i);
                    if (!app_info)
                        continue;

                    plist_t static_usage =
                        plist_dict_get_item(app_info, "StaticDiskUsage");
                    if (static_usage &&
                        plist_get_node_type(static_usage) == PLIST_UINT) {
                        uint64_t static_size = 0;
                        plist_get_uint_val(static_usage, &static_size);
                        totalAppsSpace += static_size;
                    }

                    plist_t dynamic_usage =
                        plist_dict_get_item(app_info, "DynamicDiskUsage");
                    if (dynamic_usage &&
                        plist_get_node_type(dynamic_usage) == PLIST_UINT) {
                        uint64_t dynamic_size = 0;
                        plist_get_uint_val(dynamic_usage, &dynamic_size);
                        totalAppsSpace += dynamic_size;
                    }
                }
            }
            plist_free(apps);
        }
        result["appsUsage"] = QVariant::fromValue(totalAppsSpace);
        plist_free(client_opts);
        instproxy_client_free(instproxy);

        // Media usage
        uint64_t mediaSpace = 0;
        plist_t node = nullptr;
        if (lockdownd_get_value(lockdownClient, "com.apple.mobile.iTunes",
                                nullptr, &node) == LOCKDOWN_E_SUCCESS &&
            node) {
            plist_t mediaNode = plist_dict_get_item(node, "MediaLibrarySize");
            if (mediaNode && plist_get_node_type(mediaNode) == PLIST_UINT) {
                plist_get_uint_val(mediaNode, &mediaSpace);
            }
            plist_free(node);
        }
        result["mediaUsage"] = QVariant::fromValue(mediaSpace);

        return result;
    });
    watcher->setFuture(future);
}
