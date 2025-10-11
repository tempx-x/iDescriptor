#ifndef APPDOWNLOADBASEDIALOG_H
#define APPDOWNLOADBASEDIALOG_H

#include <QDialog>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

class AppDownloadBaseDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AppDownloadBaseDialog(const QString &appName,
                                   const QString &bundleId,
                                   QWidget *parent = nullptr);

public slots:
    void updateProgressBar(int percentage);

signals:
    void downloadFinished(bool success, const QString &message);

protected:
    void reject() override;
    void startDownloadProcess(const QString &bundleId,
                              const QString &workingDir, int index,
                              bool promptToOpenDir = true);
    void checkDownloadProgress(const QString &logFilePath,
                               const QString &appName,
                               const QString &outputDir);
    void addProgressBar(int index);
    QProgressBar *m_progressBar;
    QTimer *m_progressTimer;
    QProcess *m_downloadProcess;
    QString m_appName;
    QPushButton *m_actionButton;
    QVBoxLayout *m_layout;
    bool m_operationInProgress = false;

private slots:
    void cleanup();
};

#endif // APPDOWNLOADBASEDIALOG_H
