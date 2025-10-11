#include "installedappswidget.h"
#include "afcexplorerwidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "qprocessindicator.h"
#include "zlineedit.h"
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QEnterEvent>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QStyle>
#include <QtConcurrent/QtConcurrent>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/house_arrest.h>
#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/lockdown.h>
#include <plist/plist.h>

AppTabWidget::AppTabWidget(const QString &appName, const QString &bundleId,
                           const QString &version, QWidget *parent)
    : QGroupBox(parent), m_appName(appName), m_bundleId(bundleId),
      m_version(version), m_selected(false), m_hovered(false)
{
    setFixedHeight(60);
    setMinimumWidth(100);
    setCursor(Qt::PointingHandCursor);

    setupUI();
    fetchAppIcon();
}

void AppTabWidget::fetchAppIcon()
{
    fetchAppIconFromApple(
        m_bundleId,
        [this](const QPixmap &pixmap) {
            if (!pixmap.isNull()) {
                QPixmap scaled =
                    pixmap.scaled(32, 32, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
                QPixmap rounded(32, 32);
                rounded.fill(Qt::transparent);

                QPainter painter(&rounded);
                painter.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                path.addRoundedRect(QRectF(0, 0, 32, 32), 8, 8);
                painter.setClipPath(path);
                painter.drawPixmap(0, 0, scaled);
                painter.end();

                m_iconLabel->setPixmap(rounded);
            }
        },
        this);
}

void AppTabWidget::setSelected(bool selected)
{
    m_selected = selected;
    updateStyles();
}

void AppTabWidget::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 8, 10, 8);
    mainLayout->setSpacing(10);
    // m_defaultBg = this->palette().color(QPalette::Window);
    // Icon label
    m_iconLabel = new QLabel();
    m_iconLabel->setFixedSize(32, 32);
    m_iconLabel->setScaledContents(true);

    QPixmap placeholderIcon = QApplication::style()
                                  ->standardIcon(QStyle::SP_ComputerIcon)
                                  .pixmap(32, 32);
    m_iconLabel->setPixmap(placeholderIcon);
    mainLayout->addWidget(m_iconLabel);

    // Text container
    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    // App name label
    m_nameLabel = new QLabel();
    QFont nameFont = m_nameLabel->font();
    nameFont.setWeight(QFont::Medium);
    m_nameLabel->setFont(nameFont);

    QString displayText = m_appName;
    if (displayText.length() > 20) {
        displayText = displayText.left(17) + "...";
    }
    m_nameLabel->setText(displayText);
    textLayout->addWidget(m_nameLabel);

    // Version label
    if (!m_version.isEmpty()) {
        m_versionLabel = new QLabel(m_version);
        m_versionLabel->setStyleSheet("font-size: 11px;");
        textLayout->addWidget(m_versionLabel);
    } else {
        m_versionLabel = nullptr;
    }

    mainLayout->addLayout(textLayout);
    mainLayout->addStretch();

    updateStyles();
}

void AppTabWidget::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    emit clicked();
}

void AppTabWidget::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event)
    m_hovered = true;
    updateStyles();
}

void AppTabWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    m_hovered = false;
    updateStyles();
}

void AppTabWidget::updateStyles()
{
    // QStyleHints::colorScheme()
    QString borderStyle;
    // QColor bgColor = qApp->palette().color(QPalette::Window);
    QColor bgColor = isDarkMode() ? qApp->palette().color(QPalette::Light)
                                  : qApp->palette().color(QPalette::Dark);
    qDebug() << styleSheet();
    if (m_selected) {
        borderStyle = "QGroupBox { background-color: " +
                      qApp->palette().color(QPalette::Highlight).name() +
                      "; border-radius: "
                      "10px; border : 1px solid " +
                      bgColor.lighter().name() + "; }";
    } else {
        borderStyle = "QGroupBox { background-color: " + bgColor.name() +
                      "; border-radius: 10px; border: 1px solid " +
                      bgColor.lighter().name() + "; }";
    }
    // update();
    setStyleSheet(borderStyle);
}

InstalledAppsWidget::InstalledAppsWidget(iDescriptorDevice *device,
                                         QWidget *parent)
    : QWidget(parent), m_device(device)
{
    m_watcher = new QFutureWatcher<QVariantMap>(this);
    m_containerWatcher = new QFutureWatcher<QVariantMap>(this);
    setupUI();

    connect(m_watcher, &QFutureWatcher<QVariantMap>::finished, this,
            &InstalledAppsWidget::onAppsDataReady);
    connect(m_containerWatcher, &QFutureWatcher<QVariantMap>::finished, this,
            &InstalledAppsWidget::onContainerDataReady);
    setStyleSheet("InstalledAppsWidget { background: transparent; }");
    fetchInstalledApps();
}

void InstalledAppsWidget::setupUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Create stacked widget for different states
    m_stackedWidget = new QStackedWidget(this);
    m_mainLayout->addWidget(m_stackedWidget);

    // Create loading widget
    createLoadingWidget();

    // Create error widget
    createErrorWidget();

    // Create content widget
    createContentWidget();

    // Start in loading state
    showLoadingState();

    connect(qApp, &QApplication::paletteChanged, this, [this]() {
        for (AppTabWidget *tab : m_appTabs) {
            tab->updateStyles();
        }
    });
}

void InstalledAppsWidget::showLoadingState()
{
    m_stackedWidget->setCurrentWidget(m_loadingWidget);
}

void InstalledAppsWidget::showErrorState(const QString &error)
{
    m_errorLabel->setText(QString("Error loading apps: %1").arg(error));
    m_stackedWidget->setCurrentWidget(m_errorWidget);
}

void InstalledAppsWidget::createLoadingWidget()
{
    m_loadingWidget = new QWidget();
    QVBoxLayout *loadingLayout = new QVBoxLayout(m_loadingWidget);
    loadingLayout->setAlignment(Qt::AlignCenter);

    QProcessIndicator *spinner = new QProcessIndicator();
    spinner->setType(QProcessIndicator::line_rotate);
    spinner->setFixedSize(48, 48);
    spinner->start();
    loadingLayout->addWidget(spinner, 0, Qt::AlignCenter);

    QLabel *loadingLabel = new QLabel("Loading installed apps...");
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingLabel->setStyleSheet(
        "font-size: 14px; color: #666; margin-top: 10px;");
    loadingLayout->addWidget(loadingLabel);

    m_stackedWidget->addWidget(m_loadingWidget);
}

void InstalledAppsWidget::createErrorWidget()
{
    m_errorWidget = new QWidget();
    QVBoxLayout *errorLayout = new QVBoxLayout(m_errorWidget);
    errorLayout->setAlignment(Qt::AlignCenter);

    m_errorLabel = new QLabel();
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setStyleSheet(
        "font-size: 14px; color: #d32f2f; margin: 20px;");
    m_errorLabel->setWordWrap(true);
    errorLayout->addWidget(m_errorLabel);

    QPushButton *retryButton = new QPushButton("Retry");
    retryButton->setFixedSize(100, 30);
    connect(retryButton, &QPushButton::clicked, this,
            &InstalledAppsWidget::fetchInstalledApps);
    errorLayout->addWidget(retryButton, 0, Qt::AlignCenter);

    m_stackedWidget->addWidget(m_errorWidget);
}

void InstalledAppsWidget::createContentWidget()
{
    m_contentWidget = new QWidget();
    QHBoxLayout *contentLayout = new QHBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Create main splitter
    m_splitter = new ModernSplitter(Qt::Horizontal, m_contentWidget);
    m_splitter->setChildrenCollapsible(false);
    contentLayout->addWidget(m_splitter);

    // Left side - App list
    createLeftPanel();

    // Right side - Content area
    createRightPanel();

    // Set initial splitter sizes (400px for tabs, rest for content)
    m_splitter->setSizes({400, 600});

    // Connect signals
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            &InstalledAppsWidget::filterApps);
    connect(m_fileSharingCheckBox, &QCheckBox::toggled, this,
            &InstalledAppsWidget::onFileSharingFilterChanged);

    m_stackedWidget->addWidget(m_contentWidget);
}

// todo: move to services
void InstalledAppsWidget::fetchInstalledApps()
{
    if (!m_device || !m_device->device) {
        showErrorState("Invalid device");
        return;
    }

    QFuture<QVariantMap> future = QtConcurrent::run([this]() -> QVariantMap {
        QVariantMap result;
        QVariantList apps;

        // result["success"] = true;
        // result["apps"] = apps;
        // return result;

        instproxy_client_t instproxy = nullptr;
        lockdownd_client_t lockdownClient = nullptr;
        lockdownd_service_descriptor_t lockdowndService = nullptr;

        try {
            if (lockdownd_client_new_with_handshake(
                    m_device->device, &lockdownClient, APP_LABEL) !=
                LOCKDOWN_E_SUCCESS) {
                result["error"] = "Could not connect to lockdown service";
                return result;
            }

            if (lockdownd_start_service(
                    lockdownClient, "com.apple.mobile.installation_proxy",
                    &lockdowndService) != LOCKDOWN_E_SUCCESS) {
                result["error"] = "Could not start installation proxy service";
                lockdownd_client_free(lockdownClient);
                return result;
            }

            if (instproxy_client_new(m_device->device, lockdowndService,
                                     &instproxy) != INSTPROXY_E_SUCCESS) {
                result["error"] = "Could not connect to installation proxy";
                lockdownd_service_descriptor_free(lockdowndService);
                lockdownd_client_free(lockdownClient);
                return result;
            }

            lockdownd_service_descriptor_free(lockdowndService);
            lockdowndService = nullptr;

            // Get both User and System apps
            QStringList appTypes = {"User", "System"};

            for (const QString &appType : appTypes) {
                plist_t client_opts = plist_new_dict();
                plist_dict_set_item(
                    client_opts, "ApplicationType",
                    plist_new_string(appType.toUtf8().constData()));

                plist_t return_attrs = plist_new_array();
                plist_array_append_item(return_attrs,
                                        plist_new_string("CFBundleIdentifier"));
                plist_array_append_item(
                    return_attrs, plist_new_string("CFBundleDisplayName"));
                plist_array_append_item(
                    return_attrs,
                    plist_new_string("CFBundleShortVersionString"));
                plist_array_append_item(return_attrs,
                                        plist_new_string("CFBundleVersion"));
                plist_array_append_item(
                    return_attrs, plist_new_string("UIFileSharingEnabled"));

                plist_dict_set_item(client_opts, "ReturnAttributes",
                                    return_attrs);

                plist_t apps_plist = nullptr;
                if (instproxy_browse(instproxy, client_opts, &apps_plist) ==
                        INSTPROXY_E_SUCCESS &&
                    apps_plist) {
                    if (plist_get_node_type(apps_plist) == PLIST_ARRAY) {
                        for (uint32_t i = 0;
                             i < plist_array_get_size(apps_plist); i++) {
                            plist_t app_info =
                                plist_array_get_item(apps_plist, i);
                            if (!app_info)
                                continue;

                            QVariantMap appData;

                            // Get bundle identifier
                            plist_t bundle_id = plist_dict_get_item(
                                app_info, "CFBundleIdentifier");
                            if (bundle_id && plist_get_node_type(bundle_id) ==
                                                 PLIST_STRING) {
                                char *bundle_id_str = nullptr;
                                plist_get_string_val(bundle_id, &bundle_id_str);
                                if (bundle_id_str) {
                                    appData["bundleId"] =
                                        QString(bundle_id_str);
                                    free(bundle_id_str);
                                }
                            }

                            // Get display name
                            plist_t display_name = plist_dict_get_item(
                                app_info, "CFBundleDisplayName");
                            if (display_name &&
                                plist_get_node_type(display_name) ==
                                    PLIST_STRING) {
                                char *display_name_str = nullptr;
                                plist_get_string_val(display_name,
                                                     &display_name_str);
                                if (display_name_str) {
                                    appData["displayName"] =
                                        QString(display_name_str);
                                    free(display_name_str);
                                }
                            }

                            // Get version
                            plist_t version = plist_dict_get_item(
                                app_info, "CFBundleShortVersionString");
                            if (version &&
                                plist_get_node_type(version) == PLIST_STRING) {
                                char *version_str = nullptr;
                                plist_get_string_val(version, &version_str);
                                if (version_str) {
                                    appData["version"] = QString(version_str);
                                    free(version_str);
                                }
                            }

                            // Get file sharing enabled status
                            plist_t file_sharing = plist_dict_get_item(
                                app_info, "UIFileSharingEnabled");
                            if (file_sharing &&
                                plist_get_node_type(file_sharing) ==
                                    PLIST_BOOLEAN) {
                                uint8_t file_sharing_enabled = 0;
                                plist_get_bool_val(file_sharing,
                                                   &file_sharing_enabled);
                                appData["fileSharingEnabled"] =
                                    (file_sharing_enabled != 0);
                            } else {
                                appData["fileSharingEnabled"] = false;
                            }

                            appData["type"] = appType;

                            if (!appData["bundleId"].toString().isEmpty()) {
                                apps.append(appData);
                            }
                        }
                    }
                    plist_free(apps_plist);
                }
                plist_free(client_opts);
            }

            instproxy_client_free(instproxy);
            lockdownd_client_free(lockdownClient);

            result["apps"] = apps;
            result["success"] = true;

        } catch (const std::exception &e) {
            if (instproxy)
                instproxy_client_free(instproxy);
            if (lockdownClient)
                lockdownd_client_free(lockdownClient);
            if (lockdowndService)
                lockdownd_service_descriptor_free(lockdowndService);

            result["error"] = QString("Exception: %1").arg(e.what());
        }

        return result;
    });

    m_watcher->setFuture(future);
}

void InstalledAppsWidget::onAppsDataReady()
{
    QVariantMap result = m_watcher->result();

    if (!result.value("success", false).toBool()) {
        showErrorState(result.value("error", "Unknown error").toString());
        return;
    }

    QVariantList apps = result.value("apps").toList();
    if (apps.isEmpty()) {
        showErrorState("No apps found");
        return;
    }

    // Switch to content view once data is loaded
    m_stackedWidget->setCurrentWidget(m_contentWidget);

    // Sort apps by display name
    std::sort(apps.begin(), apps.end(),
              [](const QVariant &a, const QVariant &b) {
                  QString nameA = a.toMap().value("displayName").toString();
                  QString nameB = b.toMap().value("displayName").toString();
                  if (nameA.isEmpty())
                      nameA = a.toMap().value("bundleId").toString();
                  if (nameB.isEmpty())
                      nameB = b.toMap().value("bundleId").toString();
                  return nameA.compare(nameB, Qt::CaseInsensitive) < 0;
              });

    // Clear existing tabs
    qDeleteAll(m_appTabs);
    m_appTabs.clear();
    m_selectedTab = nullptr;

    // Create tabs for each app
    for (const QVariant &appVariant : apps) {
        QVariantMap appData = appVariant.toMap();
        QString displayName = appData.value("displayName").toString();
        QString bundleId = appData.value("bundleId").toString();
        QString version = appData.value("version").toString();
        QString appType = appData.value("type").toString();
        bool fileSharingEnabled =
            appData.value("fileSharingEnabled", false).toBool();

        // Filter by file sharing status if checkbox is checked
        if (m_fileSharingCheckBox->isChecked() && !fileSharingEnabled) {
            continue;
        }

        if (displayName.isEmpty()) {
            displayName = bundleId;
        }

        // Create tab name with type indicator
        QString tabName = displayName;
        if (appType == "System") {
            tabName += " (System)";
        }

        createAppTab(tabName, bundleId, version);
    }

    // m_contentLabel->setText(
    //     QString("Found %1 installed apps").arg(apps.count()));

    // Select first tab if available
    if (!m_appTabs.isEmpty()) {
        selectAppTab(m_appTabs.first());
    }
}

void InstalledAppsWidget::createAppTab(const QString &appName,
                                       const QString &bundleId,
                                       const QString &version)
{
    AppTabWidget *tabWidget =
        new AppTabWidget(appName, bundleId, version, this);
    connect(tabWidget, &AppTabWidget::clicked, this,
            &InstalledAppsWidget::onAppTabClicked);
    m_appTabs.append(tabWidget);

    // Remove the stretch before adding the new tab
    m_tabLayout->removeItem(m_tabLayout->itemAt(m_tabLayout->count() - 1));

    m_tabLayout->addWidget(tabWidget);
    m_tabLayout->addStretch(); // Add stretch back at the end

    m_appTabs.append(tabWidget);
}

void InstalledAppsWidget::onAppTabClicked()
{
    AppTabWidget *clickedTab = qobject_cast<AppTabWidget *>(sender());
    if (clickedTab) {
        selectAppTab(clickedTab);
    }
}

void InstalledAppsWidget::selectAppTab(AppTabWidget *tab)
{
    // Deselect previous tab
    if (m_selectedTab) {
        m_selectedTab->setSelected(false);
    }

    // Select new tab
    m_selectedTab = tab;
    tab->setSelected(true);

    QString bundleId = tab->getBundleId();

    // Load app container data
    loadAppContainer(bundleId);
}

void InstalledAppsWidget::filterApps(const QString &searchText)
{
    QString lowerSearchText = searchText.toLower();

    for (AppTabWidget *tab : m_appTabs) {
        bool shouldShow = false;

        if (lowerSearchText.isEmpty()) {
            shouldShow = true;
        } else {
            // Search in app name and bundle ID
            QString appName = tab->getAppName().toLower();
            QString bundleId = tab->getBundleId().toLower();

            shouldShow = appName.contains(lowerSearchText) ||
                         bundleId.contains(lowerSearchText);
        }

        tab->setVisible(shouldShow);
    }
}

/*
    FIXME: maybe we better have this in servicemanager,
    for now it's ok as it's only used here
*/
void InstalledAppsWidget::loadAppContainer(const QString &bundleId)
{
    if (!m_device || !m_device->device) {
        return;
    }

    // Clear previous container data
    QLayoutItem *item;
    while ((item = m_containerLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    // Create a centered loading widget
    QWidget *loadingWidget = new QWidget();
    QVBoxLayout *loadingLayout = new QVBoxLayout(loadingWidget);
    loadingLayout->setAlignment(Qt::AlignCenter);

    QProcessIndicator *l = new QProcessIndicator();
    l->setType(QProcessIndicator::line_rotate);
    l->setFixedSize(32, 32);
    l->start();
    loadingLayout->addWidget(l, 0, Qt::AlignCenter);

    m_containerLayout->addWidget(loadingWidget);

    QFuture<QVariantMap> future = QtConcurrent::run([this, bundleId]()
                                                        -> QVariantMap {
        QVariantMap result;

        afc_client_t afcClient = nullptr;
        lockdownd_client_t lockdownClient = nullptr;
        lockdownd_service_descriptor_t lockdowndService = nullptr;
        house_arrest_client_t houseArrestClient = nullptr;

        try {
            if (lockdownd_client_new_with_handshake(
                    m_device->device, &lockdownClient, APP_LABEL) !=
                LOCKDOWN_E_SUCCESS) {
                result["error"] = "Could not connect to lockdown service";
                return result;
            }

            if (lockdownd_start_service(
                    lockdownClient, "com.apple.mobile.house_arrest",
                    &lockdowndService) != LOCKDOWN_E_SUCCESS) {
                result["error"] = "Could not start house arrest service";
                lockdownd_client_free(lockdownClient);
                return result;
            }

            if (house_arrest_client_new(m_device->device, lockdowndService,
                                        &houseArrestClient) !=
                HOUSE_ARREST_E_SUCCESS) {
                result["error"] = "Could not connect to house arrest";
                lockdownd_service_descriptor_free(lockdowndService);
                lockdownd_client_free(lockdownClient);
                return result;
            }

            lockdownd_service_descriptor_free(lockdowndService);
            lockdowndService = nullptr;

            // Send vendor container command
            if (house_arrest_send_command(houseArrestClient, "VendDocuments",
                                          bundleId.toUtf8().constData()) !=
                HOUSE_ARREST_E_SUCCESS) {
                result["error"] = "Could not send VendDocuments command";
                house_arrest_client_free(houseArrestClient);
                lockdownd_client_free(lockdownClient);
                return result;
            }

            // Get result
            plist_t dict = nullptr;
            if (house_arrest_get_result(houseArrestClient, &dict) !=
                    HOUSE_ARREST_E_SUCCESS ||
                !dict) {
                result["error"] = "App container not available for this app";
                house_arrest_client_free(houseArrestClient);
                lockdownd_client_free(lockdownClient);
                return result;
            }

            // Check for error in response
            plist_t error_node = plist_dict_get_item(dict, "Error");
            if (error_node) {
                char *error_str = nullptr;
                plist_get_string_val(error_node, &error_str);
                if (error_str) {
                    result["error"] =
                        QString("Container access denied: %1").arg(error_str);
                    free(error_str);
                } else {
                    result["error"] = "Container access denied";
                }
                plist_free(dict);
                house_arrest_client_free(houseArrestClient);
                lockdownd_client_free(lockdownClient);
                return result;
            }

            plist_free(dict);

            // Get AFC client for file access
            if (afc_client_new_from_house_arrest_client(
                    houseArrestClient, &afcClient) != AFC_E_SUCCESS) {
                result["error"] =
                    "Could not create AFC client for app container";
                house_arrest_client_free(houseArrestClient);
                lockdownd_client_free(lockdownClient);
                return result;
            }

            // List root directory contents
            char **list = nullptr;
            if (afc_read_directory(afcClient, "/", &list) != AFC_E_SUCCESS) {
                result["error"] = "Could not read app container directory";
                afc_client_free(afcClient);
                house_arrest_client_free(houseArrestClient);
                lockdownd_client_free(lockdownClient);
                return result;
            }

            QStringList files;
            if (list) {
                for (int i = 0; list[i]; i++) {
                    QString fileName = QString::fromUtf8(list[i]);
                    if (fileName != "." && fileName != "..") {
                        files.append(fileName);
                    }
                }
                afc_dictionary_free(list);
            }
            qDebug() << "App container files:" << files;
            result["files"] = files;
            result["afcClient"] =
                QVariant::fromValue(reinterpret_cast<void *>(afcClient));
            result["houseArrestClient"] = QVariant::fromValue(
                reinterpret_cast<void *>(houseArrestClient));
            result["success"] = true;

            // Don't free the clients here - they will be used by
            // AfcExplorerWidget afc_client_free(afcClient);
            // house_arrest_client_free(houseArrestClient);
            lockdownd_client_free(lockdownClient);

        } catch (const std::exception &e) {
            if (afcClient)
                afc_client_free(afcClient);
            if (houseArrestClient)
                house_arrest_client_free(houseArrestClient);
            if (lockdownClient)
                lockdownd_client_free(lockdownClient);
            if (lockdowndService)
                lockdownd_service_descriptor_free(lockdowndService);

            result["error"] = QString("Exception: %1").arg(e.what());
        }

        return result;
    });

    m_containerWatcher->setFuture(future);
}

void InstalledAppsWidget::onContainerDataReady()
{
    QVariantMap result = m_containerWatcher->result();

    // Clear loading state
    QLayoutItem *item;
    while ((item = m_containerLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (!result.value("success", false).toBool()) {
        qDebug() << "Error loading app container:"
                 << result.value("error").toString();
        QLabel *errorLabel = new QLabel("No data available for this app");
        errorLabel->setAlignment(Qt::AlignCenter);
        m_containerLayout->addWidget(errorLabel);
        return;
    }

    // Get the AFC clients from the result
    afc_client_t afcClient = reinterpret_cast<afc_client_t>(
        result.value("afcClient").value<void *>());

    if (!afcClient) {
        QLabel *errorLabel =
            new QLabel("Failed to get AFC client for app container");
        m_containerLayout->addWidget(errorLabel);
        return;
    }

    // Create AfcExplorerWidget with the house arrest AFC client
    // todo:afcClient never gets freed
    AfcExplorerWidget *explorer =
        new AfcExplorerWidget(afcClient, []() {}, m_device, this);
    explorer->setStyleSheet("border :none;");
    m_containerLayout->addWidget(explorer);
}

void InstalledAppsWidget::onFileSharingFilterChanged(bool enabled)
{
    Q_UNUSED(enabled)
    // Refresh the apps list when filter changes
    fetchInstalledApps();
}

void InstalledAppsWidget::createLeftPanel()
{
    QWidget *tabWidget = new QWidget();
    tabWidget->setMinimumWidth(100);
    tabWidget->setMaximumWidth(500);

    QVBoxLayout *tabWidgetLayout = new QVBoxLayout(tabWidget);
    tabWidgetLayout->setContentsMargins(0, 0, 0, 0);
    tabWidgetLayout->setSpacing(0);

    // Search container
    QWidget *searchContainer = new QWidget();
    searchContainer->setFixedHeight(60);
    QHBoxLayout *searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(5, 0, 5, 5);

    // Search box
    m_searchEdit = new ZLineEdit();
    m_searchEdit->setPlaceholderText("Search apps...");
    searchLayout->addWidget(m_searchEdit);

    // File sharing filter checkbox
    m_fileSharingCheckBox = new QCheckBox("Show Only File Sharing Enabled");
    m_fileSharingCheckBox->setChecked(true);
    m_fileSharingCheckBox->setStyleSheet("QCheckBox { font-size: 10px; }");
    searchLayout->addWidget(m_fileSharingCheckBox);

    tabWidgetLayout->addWidget(searchContainer);

    // App list scroll area
    m_tabScrollArea = new QScrollArea();
    m_tabScrollArea->setWidgetResizable(true);
    m_tabScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tabScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tabScrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }");
    m_tabScrollArea->viewport()->setStyleSheet("background: transparent;");

    m_tabContainer = new QWidget();
    m_tabContainer->setStyleSheet("QWidget { background: transparent; }");
    m_tabLayout = new QVBoxLayout(m_tabContainer);
    m_tabLayout->setContentsMargins(0, 0, 10, 0);
    m_tabLayout->setSpacing(10);
    m_tabLayout->addStretch();

    m_tabScrollArea->setWidget(m_tabContainer);
    tabWidgetLayout->addWidget(m_tabScrollArea);

    m_splitter->addWidget(tabWidget);
}

void InstalledAppsWidget::createRightPanel()
{
    QWidget *rightContentWidget = new QWidget();

    QVBoxLayout *contentLayout = new QVBoxLayout(rightContentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 5);
    contentLayout->setSpacing(0);

    m_containerWidget = new QWidget();
    m_containerWidget->setObjectName("containerWidget");
    m_containerWidget->setStyleSheet(
        "QWidget#containerWidget { border: none; }");
    m_containerLayout = new QVBoxLayout(m_containerWidget);
    m_containerLayout->setContentsMargins(0, 0, 0, 0);
    m_containerLayout->setSpacing(0);

    contentLayout->addWidget(m_containerWidget);

    m_splitter->addWidget(rightContentWidget);
}
