#include "appdownloadbasedialog.h"
#include "appstoremanager.h"
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

void AppDownloadBaseDialog::updateProgressBar(int percentage)
{

    if (m_progressBar) {
        m_progressBar->setValue(percentage);
    }
}

void AppDownloadBaseDialog::addProgressBar(int index)
{
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedHeight(25);
    m_progressBar->setStyleSheet(
        "QProgressBar { border-radius: 6px; background: #eee; } "
        "QProgressBar::chunk { background: #34C759; }");
    m_layout->insertWidget(index, m_progressBar);
}

AppDownloadBaseDialog::AppDownloadBaseDialog(const QString &appName,
                                             const QString &bundleId,
                                             QWidget *parent)
    : QDialog(parent), m_appName(appName), m_downloadProcess(nullptr),
      m_progressTimer(nullptr)
{
    // Common UI: progress bar and action button
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(20);
    m_layout->setContentsMargins(30, 30, 30, 30);

    QLabel *nameLabel = new QLabel(appName);
    nameLabel->setStyleSheet("font-size: 20px; font-weight: bold;");
    m_layout->addWidget(nameLabel);

    m_actionButton = nullptr; // Derived classes set this
}

void AppDownloadBaseDialog::startDownloadProcess(const QString &bundleId,
                                                 const QString &outputDir,
                                                 int index,
                                                 bool promptToOpenDir)
{
    bool acquireLicense = true;

    if (bundleId.isEmpty()) {
        QMessageBox::critical(this, "Error", "Bundle ID not provided.");
        reject();
        return;
    }

    addProgressBar(index);
    if (m_actionButton)
        m_actionButton->setEnabled(false);

    m_operationInProgress = true;

    AppStoreManager *manager = AppStoreManager::sharedInstance();
    if (!manager) {
        QMessageBox::critical(this, "Error",
                              "Failed to initialize App Store manager.");
        reject();
        return;
    }

    auto progressCallback = [this](long long current, long long total) {
        int percentage = 0;
        if (total > 0) {
            percentage = static_cast<int>((current * 100) / total);
        }
        updateProgressBar(percentage);
    };

    manager->downloadApp(
        bundleId, outputDir, "", acquireLicense,
        [this, outputDir, promptToOpenDir](int result) {
            if (result == 0) { // Success
                emit downloadFinished(true, "Success");
                m_progressBar->setValue(100);
                if (promptToOpenDir) {

                    if (QMessageBox::Yes ==
                        QMessageBox::question(
                            this, "Download Successful",
                            QString("Successfully downloaded. Would you like "
                                    "to open the output directory: %1?")
                                .arg(outputDir))) {
                        QDir dir(outputDir);
                        if (!dir.exists()) {
                            QMessageBox::warning(
                                this, "Directory Not Found",
                                QString("The directory %1 does not exist.")
                                    .arg(outputDir));
                        } else {
                            QDesktopServices::openUrl(
                                QUrl::fromLocalFile(outputDir));
                        }
                    }
                    accept();
                }
            } else { // Failure
                emit downloadFinished(false, "Failed");
                // if (promptToOpenDir)
                QMessageBox::critical(
                    this, "Download Failed",
                    QString("Failed to download %1. Error code: %2")
                        .arg(m_appName)
                        .arg(result));
                reject();
            }
        },
        progressCallback);
}

void AppDownloadBaseDialog::reject()
{
    // FIXME: we need to cancel download if it gets closed
    // if (m_operationInProgress) {
    //     AppStoreManager *manager = AppStoreManager::sharedInstance();
    //     m_operationInProgress = false;
    // }

    cleanup();
    QDialog::reject();
}

void AppDownloadBaseDialog::cleanup()
{
    if (m_progressTimer && m_progressTimer->isActive()) {
        m_progressTimer->stop();
    }
}