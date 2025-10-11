#include "afcexplorerwidget.h"
#include "iDescriptor-ui.h"
#include "iDescriptor.h"
#include "mediapreviewdialog.h"
#include "servicemanager.h"
#include "settingsmanager.h"
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVariant>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/libimobiledevice.h>

AfcExplorerWidget::AfcExplorerWidget(afc_client_t afcClient,
                                     std::function<void()> onClientInvalidCb,
                                     iDescriptorDevice *device, QWidget *parent)
    : QWidget(parent), m_currentAfcClient(afcClient), m_device(device)
{
    // Initialize current AFC client to default
    m_currentAfcClient = afcClient;

    // Setup file explorer
    setupFileExplorer();

    // Main layout
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(mainLayout);

    mainLayout->addWidget(m_explorer);

    // Initialize
    m_history.push("/");
    m_currentHistoryIndex = 0;
    m_forwardHistory.clear();
    loadPath("/");

    setupContextMenu();
}

void AfcExplorerWidget::goBack()
{
    if (m_history.size() > 1) {
        // Move current path to forward history
        QString currentPath = m_history.pop();
        m_forwardHistory.push(currentPath);

        QString prevPath = m_history.top();
        loadPath(prevPath);
        updateNavigationButtons();
    }
}

void AfcExplorerWidget::goForward()
{
    if (!m_forwardHistory.isEmpty()) {
        QString forwardPath = m_forwardHistory.pop();
        m_history.push(forwardPath);
        loadPath(forwardPath);
        updateNavigationButtons();
    }
}

void AfcExplorerWidget::onItemDoubleClicked(QListWidgetItem *item)
{
    QVariant data = item->data(Qt::UserRole);
    bool isDir = data.toBool();
    QString name = item->text();

    // Use breadcrumb to get current path
    QString currPath = "/";
    if (!m_history.isEmpty())
        currPath = m_history.top();

    if (!currPath.endsWith("/"))
        currPath += "/";
    QString nextPath = currPath == "/" ? "/" + name : currPath + name;

    if (isDir) {
        // Clear forward history when navigating to a new directory
        m_forwardHistory.clear();
        m_history.push(nextPath);
        loadPath(nextPath);
        updateNavigationButtons();
    } else {
        const QString lowerFileName = name.toLower();
        const bool isPreviewable =
            lowerFileName.endsWith(".mp4") || lowerFileName.endsWith(".m4v") ||
            lowerFileName.endsWith(".mov") || lowerFileName.endsWith(".avi") ||
            lowerFileName.endsWith(".mkv") || lowerFileName.endsWith(".jpg") ||
            lowerFileName.endsWith(".jpeg") || lowerFileName.endsWith(".png") ||
            lowerFileName.endsWith(".gif") || lowerFileName.endsWith(".bmp");

        if (isPreviewable) {
            auto *previewDialog = new MediaPreviewDialog(
                m_device, m_currentAfcClient, nextPath, this);
            previewDialog->setAttribute(Qt::WA_DeleteOnClose);
            previewDialog->show();
            // TODO: we need this ?
            emit fileSelected(nextPath);
        } else {
            // TODO: maybe delete in deconstructor
            QTemporaryDir *tempDir = new QTemporaryDir();
            if (tempDir->isValid()) {
                qDebug() << "Created temp dir:" << tempDir->path();
                QString localPath = tempDir->path() + "/" + name;
                int result = export_file_to_path(
                    m_currentAfcClient, nextPath.toUtf8().constData(),
                    localPath.toUtf8().constData());
                qDebug() << "Export result:" << result << "to" << localPath;
                if (result == 0) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
                } else {
                    QMessageBox::warning(
                        this, "Export Failed",
                        "Could not export the file from the device.");
                }
            } else {
                QMessageBox::critical(
                    this, "Error", "Could not create a temporary directory.");
            }
        }
    }
}

void AfcExplorerWidget::onAddressBarReturnPressed()
{
    QString path = m_addressBar->text().trimmed();
    if (path.isEmpty()) {
        path = "/";
    }

    // Normalize the path
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    // Remove duplicate slashes
    path = path.replace(QRegularExpression("/+"), "/");

    // Clear forward history when navigating to a new path
    m_forwardHistory.clear();

    // Update history and load the path
    m_history.push(path);
    loadPath(path);
    updateNavigationButtons();
}

void AfcExplorerWidget::updateNavigationButtons()
{
    // Update button states based on history
    if (m_backButton) {
        m_backButton->setEnabled(m_history.size() > 1);
    }
    if (m_forwardButton) {
        m_forwardButton->setEnabled(!m_forwardHistory.isEmpty());
    }
}

void AfcExplorerWidget::updateAddressBar(const QString &path)
{
    // Update the address bar with the current path
    m_addressBar->setText(path);
}

void AfcExplorerWidget::loadPath(const QString &path)
{
    m_fileList->clear();

    updateAddressBar(path);

    AFCFileTree tree =
        ServiceManager::safeGetFileTree(m_device, path.toStdString());
    if (!tree.success) {
        m_fileList->addItem("Failed to load directory");
        return;
    }

    for (const auto &entry : tree.entries) {
        QListWidgetItem *item =
            new QListWidgetItem(QString::fromStdString(entry.name));
        item->setData(Qt::UserRole, entry.isDir);
        if (entry.isDir)
            item->setIcon(QIcon::fromTheme("folder"));
        else
            item->setIcon(QIcon::fromTheme("text-x-generic"));
        m_fileList->addItem(item);
    }
}

void AfcExplorerWidget::setupContextMenu()
{
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this,
            &AfcExplorerWidget::onFileListContextMenu);
}

void AfcExplorerWidget::onFileListContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_fileList->itemAt(pos);
    if (!item)
        return;

    bool isDir = item->data(Qt::UserRole).toBool();
    if (isDir)
        return; // Only export files

    QMenu menu;
    QAction *exportAction = menu.addAction("Export");
    QAction *selectedAction =
        menu.exec(m_fileList->viewport()->mapToGlobal(pos));
    if (selectedAction == exportAction) {
        QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();
        QList<QListWidgetItem *> filesToExport;
        if (selectedItems.isEmpty())
            filesToExport.append(item); // fallback: just the clicked one
        else {
            for (QListWidgetItem *selItem : selectedItems) {
                if (!selItem->data(Qt::UserRole).toBool())
                    filesToExport.append(selItem);
            }
        }
        if (filesToExport.isEmpty())
            return;
        QString dir =
            QFileDialog::getExistingDirectory(this, "Select Export Directory");
        if (dir.isEmpty())
            return;
        for (QListWidgetItem *selItem : filesToExport) {
            exportSelectedFile(selItem, dir);
        }
    }
}

void AfcExplorerWidget::onExportClicked()
{
    QList<QListWidgetItem *> selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty())
        return;

    // Only files (not directories)
    QList<QListWidgetItem *> filesToExport;
    for (QListWidgetItem *item : selectedItems) {
        if (!item->data(Qt::UserRole).toBool())
            filesToExport.append(item);
    }
    if (filesToExport.isEmpty())
        return;

    // Ask user for a directory to save all files
    QString dir =
        QFileDialog::getExistingDirectory(this, "Select Export Directory");
    if (dir.isEmpty())
        return;

    for (QListWidgetItem *item : filesToExport) {
        exportSelectedFile(item, dir);
    }
}

void AfcExplorerWidget::exportSelectedFile(QListWidgetItem *item,
                                           const QString &directory)
{
    QString fileName = item->text();
    QString currPath = "/";
    if (!m_history.isEmpty())
        currPath = m_history.top();
    if (!currPath.endsWith("/"))
        currPath += "/";
    qDebug() << "Current path:" << currPath;
    QString devicePath = currPath == "/" ? "/" + fileName : currPath + fileName;
    qDebug() << "Exporting file:" << devicePath;

    // Save to selected directory
    QString savePath = directory + "/" + fileName;

    // Get device info and check connections
    qDebug() << "Using device index:" << m_device->udid.c_str();
    qDebug() << "Device UDID:" << QString::fromStdString(m_device->udid);
    qDebug() << "Device Product Type:"
             << QString::fromStdString(m_device->deviceInfo.productType);

    // Export file using the validated connections
    int result = export_file_to_path(m_currentAfcClient,
                                     devicePath.toStdString().c_str(),
                                     savePath.toStdString().c_str());

    qDebug() << "Export result:" << result;

    if (result == 0) {
        qDebug() << "Exported" << devicePath << "to" << savePath;

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(
            this, "Export Successful",
            "File exported successfully. Would you like to see the directory?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
        }
    } else {
        qDebug() << "Failed to export" << devicePath;
        QMessageBox::warning(this, "Export Failed",
                             "Failed to export the file from the device");
    }
}

// TODO : abstract to services
int AfcExplorerWidget::export_file_to_path(afc_client_t afc,
                                           const char *device_path,
                                           const char *local_path)
{
    uint64_t handle = 0;
    if (ServiceManager::safeAfcFileOpen(m_device, device_path, AFC_FOPEN_RDONLY,
                                        &handle) != AFC_E_SUCCESS) {
        qDebug() << "Failed to open file on device:" << device_path;
        return -1;
    }
    FILE *out = fopen(local_path, "wb");
    if (!out) {
        qDebug() << "Failed to open local file:" << local_path;
        afc_file_close(afc, handle);
        return -1;
    }

    char buffer[4096];
    uint32_t bytes_read = 0;
    while (ServiceManager::safeAfcFileRead(m_device, handle, buffer,
                                           sizeof(buffer),
                                           &bytes_read) == AFC_E_SUCCESS &&
           bytes_read > 0) {
        fwrite(buffer, 1, bytes_read, out);
    }

    fclose(out);
    afc_file_close(afc, handle);
    return 0;
}

void AfcExplorerWidget::onImportClicked()
{
    // TODO: check devices

    // Select one or more files to import
    QStringList fileNames = QFileDialog::getOpenFileNames(this, "Import Files");
    if (fileNames.isEmpty())
        return;

    // Use current breadcrumb directory as target
    QString currPath = "/";
    if (!m_history.isEmpty())
        currPath = m_history.top();
    if (!currPath.endsWith("/"))
        currPath += "/";

    // if (!device || !client || !serviceDesc)
    // {
    //     qDebug() << "Failed to connect to device or lockdown service";
    //     return;
    // }

    // Import each file
    for (const QString &localPath : fileNames) {
        QFileInfo fi(localPath);
        QString devicePath = currPath + fi.fileName();
        int result = import_file_to_device(m_currentAfcClient,
                                           devicePath.toStdString().c_str(),
                                           localPath.toStdString().c_str());
        if (result == 0)
            qDebug() << "Imported" << localPath << "to" << devicePath;
        else
            qDebug() << "Failed to import" << localPath;
    }

    // Refresh file list
    loadPath(currPath);
}

// Helper function to import a file from a local path to the device
int AfcExplorerWidget::import_file_to_device(afc_client_t afc,
                                             const char *device_path,
                                             const char *local_path)
{
    QFile in(local_path);
    if (!in.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open local file for import:" << local_path;
        return -1;
    }

    uint64_t handle = 0;
    if (afc_file_open(afc, device_path, AFC_FOPEN_WRONLY, &handle) !=
        AFC_E_SUCCESS) {
        qDebug() << "Failed to open file on device for writing:" << device_path;
        return -1;
    }

    char buffer[4096];
    qint64 bytesRead;
    while ((bytesRead = in.read(buffer, sizeof(buffer))) > 0) {
        uint32_t bytesWritten = 0;
        if (afc_file_write(afc, handle, buffer,
                           static_cast<uint32_t>(bytesRead),
                           &bytesWritten) != AFC_E_SUCCESS ||
            bytesWritten != bytesRead) {
            qDebug() << "Failed to write to device file:" << device_path;
            afc_file_close(afc, handle);
            in.close();
            return -1;
        }
    }

    afc_file_close(afc, handle);
    in.close();
    return 0;
}

void AfcExplorerWidget::setupFileExplorer()
{
    m_explorer = new QWidget();
    QVBoxLayout *explorerLayout = new QVBoxLayout(m_explorer);
    explorerLayout->setContentsMargins(0, 0, 0, 0);
    m_explorer->setStyleSheet("border : none;");

    // Export/Import buttons layout
    QHBoxLayout *exportLayout = new QHBoxLayout();
    m_exportBtn = new QPushButton("Export");
    m_importBtn = new QPushButton("Import");
    m_addToFavoritesBtn = new QPushButton("Add to Favorites");
    exportLayout->addWidget(m_exportBtn);
    exportLayout->addWidget(m_importBtn);
    exportLayout->addWidget(m_addToFavoritesBtn);
    exportLayout->setContentsMargins(0, 0, 0, 0);
    exportLayout->addStretch();
    explorerLayout->addLayout(exportLayout);

    // Navigation layout (Address Bar with embedded icons)
    m_navWidget = new QWidget();
    m_navWidget->setObjectName("navWidget");
    m_navWidget->setFocusPolicy(Qt::StrongFocus); // Make it focusable
    connect(qApp, &QApplication::paletteChanged, this,
            &AfcExplorerWidget::updateNavStyles);

    m_navWidget->setMaximumWidth(500);
    m_navWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QHBoxLayout *navContainerLayout = new QHBoxLayout();
    navContainerLayout->addStretch();
    navContainerLayout->addWidget(m_navWidget);
    navContainerLayout->addStretch();

    QHBoxLayout *navLayout = new QHBoxLayout(m_navWidget);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);

    // Create navigation buttons using ClickableIconWidget
    QWidget *explorerLeftSideNavButtons = new QWidget();
    QHBoxLayout *leftNavLayout = new QHBoxLayout(explorerLeftSideNavButtons);
    // explorerLeftSideNavButtons->setStyleSheet("border-right: 1px solid
    // red;");
    leftNavLayout->setContentsMargins(0, 0, 0, 0);
    leftNavLayout->setSpacing(1);

    m_backButton = new ClickableIconWidget(
        QIcon::fromTheme("go-previous", QIcon("←")), "Go Back");
    m_backButton->setEnabled(false);

    m_forwardButton = new ClickableIconWidget(
        QIcon::fromTheme("go-next", QIcon("→")), "Go Forward");
    m_forwardButton->setEnabled(false);

    m_enterButton = new ClickableIconWidget(
        QIcon::fromTheme("go-jump", QIcon("⏎")), "Navigate to path");

    m_addressBar = new QLineEdit();
    m_addressBar->setPlaceholderText("Enter path...");
    m_addressBar->setText("/");

    // Add widgets to navigation layout
    leftNavLayout->addWidget(m_backButton);
    leftNavLayout->addWidget(m_forwardButton);
    navLayout->addWidget(explorerLeftSideNavButtons);
    navLayout->addWidget(m_addressBar);
    navLayout->addWidget(m_enterButton);

    // Add the container layout (which centers navWidget) to the main layout
    explorerLayout->addLayout(navContainerLayout);

    // File list
    m_fileList = new QListWidget();
    // todo
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);

    QScrollBar *vBar = m_fileList->QAbstractScrollArea::verticalScrollBar();
    // vBar->setStyleSheet("background:red; border: red;");
    vBar->setStyleSheet(styleSheet());
    // vBar->setStyleSheet(
    //     "QScrollArea { background: transparent; border: none; }");
    // m_scrollArea->viewport()->setStyleSheet("background: transparent;");
    // m_fileList->viewport()->setStyleSheet("background: transparent;");
    explorerLayout->addWidget(m_fileList);

    // Connect buttons and actions
    connect(m_backButton, &ClickableIconWidget::clicked, this,
            &AfcExplorerWidget::goBack);
    connect(m_forwardButton, &ClickableIconWidget::clicked, this,
            &AfcExplorerWidget::goForward);
    connect(m_enterButton, &ClickableIconWidget::clicked, this,
            &AfcExplorerWidget::onAddressBarReturnPressed);
    connect(m_addressBar, &QLineEdit::returnPressed, this,
            &AfcExplorerWidget::onAddressBarReturnPressed);
    connect(m_fileList, &QListWidget::itemDoubleClicked, this,
            &AfcExplorerWidget::onItemDoubleClicked);
    connect(m_exportBtn, &QPushButton::clicked, this,
            &AfcExplorerWidget::onExportClicked);
    connect(m_importBtn, &QPushButton::clicked, this,
            &AfcExplorerWidget::onImportClicked);
    connect(m_addToFavoritesBtn, &QPushButton::clicked, this,
            &AfcExplorerWidget::onAddToFavoritesClicked);

    updateNavigationButtons();
    updateNavStyles();
}

// todo: implement
void AfcExplorerWidget::onAddToFavoritesClicked()
{
    QString currentPath = "/";
    if (!m_history.isEmpty())
        currentPath = m_history.top();

    bool ok;
    QString alias = QInputDialog::getText(
        this, "Add to Favorites",
        "Enter alias for this location:", QLineEdit::Normal, currentPath, &ok);
    if (ok && !alias.isEmpty()) {
        saveFavoritePlace(currentPath, alias);
    }
}

void AfcExplorerWidget::saveFavoritePlace(const QString &path,
                                          const QString &alias)
{
    qDebug() << "Saving favorite place:" << alias << "->" << path;
    SettingsManager *settings = SettingsManager::sharedInstance();
    settings->saveFavoritePlace(path, alias);
}

void AfcExplorerWidget::updateNavStyles()
{
    QColor bgColor = isDarkMode() ? qApp->palette().color(QPalette::Light)
                                  : qApp->palette().color(QPalette::Dark);
    QColor borderColor = qApp->palette().color(QPalette::Mid);
    QColor accentColor = qApp->palette().color(QPalette::Highlight);

    QString navStyles = QString("QWidget#navWidget {"
                                "    background-color: %1;"
                                "    border: 1px solid %2;"
                                "    border-radius: 10px;"
                                "}"
                                "QWidget#navWidget {"
                                "    outline: 1px solid %3;"
                                "    outline-offset: 1px;"
                                "}")
                            .arg(bgColor.name())
                            .arg(bgColor.lighter().name())
                            .arg(accentColor.name());

    m_navWidget->setStyleSheet(navStyles);

    // Update address bar styles to complement the nav widget
    QString addressBarStyles =
        QString("QLineEdit { background-color: %1; border-radius: 10px; "
                "border: 1px solid %2; }")
            .arg(bgColor.name())
            .arg(borderColor.lighter().name());

    m_addressBar->setStyleSheet(addressBarStyles);
}