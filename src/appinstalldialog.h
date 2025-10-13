#ifndef APPINSTALLDIALOG_H
#define APPINSTALLDIALOG_H

#include "appdownloadbasedialog.h"
#include <QComboBox>
#include <QDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QTemporaryDir>

class AppInstallDialog : public AppDownloadBaseDialog
{
    Q_OBJECT
public:
    explicit AppInstallDialog(const QString &appName,
                              const QString &description,
                              const QString &bundleId,
                              QWidget *parent = nullptr);
    ~AppInstallDialog();

protected:
    void reject() override;

private slots:
    void onInstallClicked();

private:
    QComboBox *m_deviceCombo;
    QString m_bundleId;
    QLabel *m_statusLabel;
    QFutureWatcher<int> *m_installWatcher;
    QTemporaryDir *m_tempDir;
    void updateDeviceList();
    void performInstallation(const QString &ipaPath, const QString &deviceUdid);
};

#endif // APPINSTALLDIALOG_H
