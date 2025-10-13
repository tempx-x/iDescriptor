#include "gallerywidget.h"
#include "fileexportdialog.h"
#include "iDescriptor.h"
#include "mediapreviewdialog.h"
#include "photoexportmanager.h"
#include "photomodel.h"
#include "servicemanager.h"
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

void GalleryWidget::load()
{
    if (m_loaded)
        return;

    m_loaded = true;

    setupUI();
}

GalleryWidget::GalleryWidget(iDescriptorDevice *device, QWidget *parent)
    : QWidget{parent}, m_device(device), m_model(nullptr),
      m_exportManager(nullptr), m_stackedWidget(nullptr),
      m_albumSelectionWidget(nullptr), m_albumListView(nullptr),
      m_photoGalleryWidget(nullptr), m_listView(nullptr), m_backButton(nullptr)
{
    // Initialize export manager
    m_exportManager = new PhotoExportManager(this);

    // Widget setup is done in load() method when gallery tab is activated
}

void GalleryWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    // Setup controls at the top (outside of stacked widget)
    setupControlsLayout();

    // Create stacked widget for different views
    m_stackedWidget = new QStackedWidget(this);

    // Setup album selection view
    setupAlbumSelectionView();

    // Setup photo gallery view
    setupPhotoGalleryView();

    // Add stacked widget to main layout
    m_mainLayout->addWidget(m_stackedWidget);
    setLayout(m_mainLayout);

    // Start with album selection view and load albums
    m_stackedWidget->setCurrentWidget(m_albumSelectionWidget);
    setControlsEnabled(false); // Disable controls until album is selected
    loadAlbumList();
}

void GalleryWidget::setupControlsLayout()
{
    m_controlsLayout = new QHBoxLayout();
    m_controlsLayout->setSpacing(5);
    m_controlsLayout->setContentsMargins(7, 7, 7, 7);

    // Sort order combo box
    QLabel *sortLabel = new QLabel("Sort:");
    sortLabel->setStyleSheet("font-weight: bold;");
    m_sortComboBox = new QComboBox();
    m_sortComboBox->addItem("Newest First",
                            static_cast<int>(PhotoModel::NewestFirst));
    m_sortComboBox->addItem("Oldest First",
                            static_cast<int>(PhotoModel::OldestFirst));
    m_sortComboBox->setCurrentIndex(0);   // Default to Newest First
    m_sortComboBox->setMinimumWidth(150); // Ensure text fits
    m_sortComboBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Filter combo box
    QLabel *filterLabel = new QLabel("Filter:");
    filterLabel->setStyleSheet("font-weight: bold;");
    m_filterComboBox = new QComboBox();
    m_filterComboBox->addItem("All Media", static_cast<int>(PhotoModel::All));
    m_filterComboBox->addItem("Images Only",
                              static_cast<int>(PhotoModel::ImagesOnly));
    m_filterComboBox->addItem("Videos Only",
                              static_cast<int>(PhotoModel::VideosOnly));
    m_filterComboBox->setCurrentIndex(0);   // Default to All
    m_filterComboBox->setMinimumWidth(150); // Ensure text fits
    m_filterComboBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Export buttons
    m_exportSelectedButton = new QPushButton("Export Selected");
    m_exportSelectedButton->setEnabled(false); // Initially disabled
    m_exportSelectedButton->setSizePolicy(QSizePolicy::Preferred,
                                          QSizePolicy::Fixed);
    m_exportSelectedButton->setStyleSheet("QPushButton { padding: 8px 16px; }");
    m_exportAllButton = new QPushButton("Export All");
    m_exportAllButton->setStyleSheet("QPushButton { padding: 8px 16px; }");

    // Back button
    m_backButton = new QPushButton("← Back to Albums");
    m_backButton->setVisible(false); // Hidden initially

    // Connect signals
    connect(m_sortComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GalleryWidget::onSortOrderChanged);
    connect(m_filterComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GalleryWidget::onFilterChanged);
    connect(m_exportSelectedButton, &QPushButton::clicked, this,
            &GalleryWidget::onExportSelected);
    connect(m_exportAllButton, &QPushButton::clicked, this,
            &GalleryWidget::onExportAll);
    connect(m_backButton, &QPushButton::clicked, this,
            &GalleryWidget::onBackToAlbums);

    // Add widgets to layout
    m_controlsLayout->addWidget(m_backButton);
    m_controlsLayout->addWidget(sortLabel);
    m_controlsLayout->addWidget(m_sortComboBox);
    m_controlsLayout->addWidget(filterLabel);
    m_controlsLayout->addWidget(m_filterComboBox);
    m_controlsLayout->addStretch(); // Push export buttons to the right
    m_controlsLayout->addWidget(m_exportSelectedButton);
    m_controlsLayout->addWidget(m_exportAllButton);

    // Create a frame to contain the controls
    QWidget *controlsWidget = new QWidget();
    controlsWidget->setLayout(m_controlsLayout);
    controlsWidget->setStyleSheet("QWidget { "
                                  "  padding: 2px; "
                                  "}");

    m_mainLayout->addWidget(controlsWidget);
}

void GalleryWidget::onSortOrderChanged()
{
    if (!m_model)
        return;

    int sortValue = m_sortComboBox->currentData().toInt();
    PhotoModel::SortOrder order = static_cast<PhotoModel::SortOrder>(sortValue);
    m_model->setSortOrder(order);

    qDebug() << "Sort order changed to:"
             << (order == PhotoModel::NewestFirst ? "Newest First"
                                                  : "Oldest First");
}

void GalleryWidget::onFilterChanged()
{
    if (!m_model)
        return;

    int filterValue = m_filterComboBox->currentData().toInt();
    PhotoModel::FilterType filter =
        static_cast<PhotoModel::FilterType>(filterValue);
    m_model->setFilterType(filter);

    QString filterName = m_filterComboBox->currentText();
    qDebug() << "Filter changed to:" << filterName;
}

void GalleryWidget::onExportSelected()
{
    if (!m_model || !m_listView->selectionModel()->hasSelection()) {
        QMessageBox::information(this, "No Selection",
                                 "Please select photos to export.");
        return;
    }

    if (m_exportManager->isExporting()) {
        QMessageBox::information(this, "Export in Progress",
                                 "An export is already in progress.");
        return;
    }

    QModelIndexList selectedIndexes =
        m_listView->selectionModel()->selectedIndexes();
    QStringList filePaths = m_model->getSelectedFilePaths(selectedIndexes);

    if (filePaths.isEmpty()) {
        QMessageBox::information(this, "No Items",
                                 "No valid items selected for export.");
        return;
    }

    QString exportDir = selectExportDirectory();
    if (exportDir.isEmpty()) {
        return;
    }

    qDebug() << "Starting export of selected files:" << filePaths.size()
             << "items to" << exportDir;

    // Create export dialog and connect signals
    // todo:cleanup
    auto *exportDialog = new FileExportDialog(exportDir, this);

    // Connect PhotoExportManager signals to FileExportDialog
    connect(m_exportManager, &PhotoExportManager::exportStarted, exportDialog,
            &FileExportDialog::onExportStarted);
    connect(m_exportManager, &PhotoExportManager::exportProgress, exportDialog,
            &FileExportDialog::onExportProgress);
    connect(m_exportManager, &PhotoExportManager::exportFinished, exportDialog,
            &FileExportDialog::onExportFinished);
    connect(m_exportManager, &PhotoExportManager::exportCancelled, exportDialog,
            &FileExportDialog::onExportCancelled);

    // Connect cancel signal from dialog to export manager
    connect(exportDialog, &FileExportDialog::cancelRequested, m_exportManager,
            &PhotoExportManager::cancelExport);

    // Start the export
    m_exportManager->exportFiles(m_device, filePaths, exportDir);
}

void GalleryWidget::onExportAll()
{
    if (!m_model)
        return;

    if (m_exportManager->isExporting()) {
        QMessageBox::information(this, "Export in Progress",
                                 "An export is already in progress.");
        return;
    }

    QStringList filePaths = m_model->getFilteredFilePaths();

    if (filePaths.isEmpty()) {
        QMessageBox::information(this, "No Items", "No items to export.");
        return;
    }

    QString message =
        QString("Export all %1 items currently shown?").arg(filePaths.size());
    int reply = QMessageBox::question(this, "Export All", message,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    QString exportDir = selectExportDirectory();
    if (exportDir.isEmpty()) {
        return;
    }

    qDebug() << "Starting export of all filtered files:" << filePaths.size()
             << "items to" << exportDir;

    // Create export dialog and connect signals
    // todo:cleanup
    auto *exportDialog = new FileExportDialog(exportDir, this);

    // Connect PhotoExportManager signals to FileExportDialog
    connect(m_exportManager, &PhotoExportManager::exportStarted, exportDialog,
            &FileExportDialog::onExportStarted);
    connect(m_exportManager, &PhotoExportManager::exportProgress, exportDialog,
            &FileExportDialog::onExportProgress);
    connect(m_exportManager, &PhotoExportManager::exportFinished, exportDialog,
            &FileExportDialog::onExportFinished);
    connect(m_exportManager, &PhotoExportManager::exportCancelled, exportDialog,
            &FileExportDialog::onExportCancelled);

    // Connect cancel signal from dialog to export manager
    connect(exportDialog, &FileExportDialog::cancelRequested, m_exportManager,
            &PhotoExportManager::cancelExport);

    // Start the export
    m_exportManager->exportFiles(m_device, filePaths, exportDir);
}

QString GalleryWidget::selectExportDirectory()
{
    QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

    QString selectedDir = QFileDialog::getExistingDirectory(
        this, "Select Export Directory", defaultDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    return selectedDir;
}

void GalleryWidget::setupAlbumSelectionView()
{
    m_albumSelectionWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(m_albumSelectionWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    // Add instructions label
    QLabel *instructionLabel = new QLabel("Select a photo album:");
    instructionLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(instructionLabel);

    // Create list view for albums
    m_albumListView = new QListView();
    // m_albumListView->setStyleSheet("QListView { "
    //                                "    border: 1px solid #c1c1c1ff; "
    //                                "    background-color: white; "
    //                                "    padding: 5px; "
    //                                "} "
    //                                "QListView::item { "
    //                                "    padding: 10px; "
    //                                "    border-bottom: 1px solid #e1e1e1; "
    //                                "} "
    //                                "QListView::item:hover { "
    //                                "    background-color: #f0f0f0; "
    //                                "} "
    //                                "QListView::item:selected { "
    //                                "    background-color: #0078d4; "
    //                                "    color: white; "
    //                                "}");

    layout->addWidget(m_albumListView);

    // Add the album selection widget to stacked widget
    m_stackedWidget->addWidget(m_albumSelectionWidget);

    // Connect double-click to select album
    connect(m_albumListView, &QListView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (!index.isValid())
                    return;
                QString albumPath = index.data(Qt::UserRole).toString();
                onAlbumSelected(albumPath);
            });
}

void GalleryWidget::setupPhotoGalleryView()
{
    m_photoGalleryWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(m_photoGalleryWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create list view for photos
    m_listView = new QListView();
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setFlow(QListView::LeftToRight);
    m_listView->setWrapping(true);
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setIconSize(QSize(120, 120));
    m_listView->setSpacing(10);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setUniformItemSizes(true);

    m_listView->setStyleSheet("QListView { "
                              "    border-top: 1px solid #c1c1c1ff; "
                              "    background-color: transparent; "
                              "    padding: 0px;"
                              "} "
                              "QListView::item { "
                              "    width: 150px; "
                              "    height: 150px; "
                              "    margin: 2px; "
                              "}");

    layout->addWidget(m_listView);

    // Add the photo gallery widget to stacked widget
    m_stackedWidget->addWidget(m_photoGalleryWidget);

    // Connect double-click to open preview dialog
    connect(m_listView, &QListView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (!index.isValid())
                    return;

                QString filePath =
                    m_model->data(index, Qt::UserRole).toString();
                if (filePath.isEmpty())
                    return;

                qDebug() << "Opening preview for" << filePath;
                auto *previewDialog = new MediaPreviewDialog(
                    m_device, m_device->afcClient, filePath, this);
                previewDialog->setAttribute(Qt::WA_DeleteOnClose);
                previewDialog->show();
            });
}

void GalleryWidget::loadAlbumList()
{
    // Get DCIM directory contents
    qDebug() << "Loading album list from /DCIM";
    AFCFileTree dcimTree = ServiceManager::safeGetFileTree(m_device, "/DCIM");

    if (!dcimTree.success) {
        qDebug() << "Failed to read DCIM directory";
        QMessageBox::warning(this, "Error",
                             "Could not access DCIM directory on device.");
        return;
    }

    qDebug() << "DCIM directory read successfully, found"
             << dcimTree.entries.size() << "entries";

    auto *albumModel = new QStandardItemModel(this);

    for (const MediaEntry &entry : dcimTree.entries) {
        QString albumName = QString::fromStdString(entry.name);
        qDebug() << "DCIM entry:" << albumName << "(isDir:" << entry.isDir
                 << ")";

        // Check if it's a directory and matches common iOS photo album patterns
        if (entry.isDir &&
            (albumName.contains("APPLE") ||
             QRegularExpression("^\\d{3}APPLE$").match(albumName).hasMatch() ||
             QRegularExpression("^\\d{4}\\d{2}\\d{2}$")
                 .match(albumName)
                 .hasMatch())) {
            qDebug() << "Found photo album:" << albumName;
            auto *item = new QStandardItem(albumName);
            QString fullPath = QString("/DCIM/%1").arg(albumName);
            item->setData(fullPath, Qt::UserRole); // Store full path
            item->setIcon(QIcon::fromTheme("folder"));
            albumModel->appendRow(item);
        }
    }

    m_albumListView->setModel(albumModel);

    if (albumModel->rowCount() == 0) {
        QMessageBox::information(this, "No Albums",
                                 "No photo albums found on device.");
    } else {
        qDebug() << "Found" << albumModel->rowCount() << "photo albums";
    }
}

void GalleryWidget::onAlbumSelected(const QString &albumPath)
{
    m_currentAlbumPath = albumPath;

    // Create model if not exists
    if (!m_model) {
        m_model = new PhotoModel(m_device, this);
        m_listView->setModel(m_model);

        // Update export button states based on selection
        connect(m_listView->selectionModel(),
                &QItemSelectionModel::selectionChanged, this, [this]() {
                    bool hasSelection =
                        m_listView->selectionModel()->hasSelection();
                    m_exportSelectedButton->setEnabled(hasSelection);
                });
    }

    // Set album path and load photos
    m_model->setAlbumPath(albumPath);

    // Switch to photo gallery view
    m_stackedWidget->setCurrentWidget(m_photoGalleryWidget);

    // Enable controls and show back button
    setControlsEnabled(true);
    m_backButton->setVisible(true);

    qDebug() << "Loaded album:" << albumPath;
}

void GalleryWidget::onBackToAlbums()
{
    // Switch back to album selection view
    m_stackedWidget->setCurrentWidget(m_albumSelectionWidget);

    // Disable controls and hide back button
    setControlsEnabled(false);
    m_backButton->setVisible(false);

    // Clear current album path
    m_currentAlbumPath.clear();
}

void GalleryWidget::setControlsEnabled(bool enabled)
{
    m_sortComboBox->setEnabled(enabled);
    m_filterComboBox->setEnabled(enabled);
    m_exportSelectedButton->setEnabled(
        enabled && m_listView && m_listView->selectionModel()->hasSelection());
    m_exportAllButton->setEnabled(enabled);
}
