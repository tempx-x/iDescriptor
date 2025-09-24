#ifndef AIRPLAYWINDOW_H
#define AIRPLAYWINDOW_H

#include <QLabel>
#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <cstdint>
#include <functional>

class AirPlayServerThread;

class AirPlayWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit AirPlayWindow(QWidget *parent = nullptr);
    ~AirPlayWindow();

public slots:
    void updateVideoFrame(QByteArray frameData, int width, int height);

private slots:
    void onServerStatusChanged(bool running);

private:
    void setupUI();
    void startAirPlayServer();
    void stopAirPlayServer();

    QLabel *m_videoLabel;
    QLabel *m_statusLabel;
    AirPlayServerThread *m_serverThread;
    bool m_serverRunning;

    // V4L2 members
    int m_v4l2_fd;
    int m_v4l2_width;
    int m_v4l2_height;
    bool m_v4l2_enabled;

    // V4L2 methods
    void initV4L2(int width, int height, const char *device);
    void closeV4L2();
    void writeFrameToV4L2(uint8_t *data, int width, int height);
    void testV4L2Device();
};

class AirPlayServerThread : public QThread
{
    Q_OBJECT

public:
    explicit AirPlayServerThread(QObject *parent = nullptr);
    ~AirPlayServerThread();

    void stopServer();

signals:
    void statusChanged(bool running);
    void videoFrameReady(QByteArray frameData, int width, int height);

protected:
    void run() override;

private:
    bool m_shouldStop;
};

// Global callback for video renderer
extern std::function<void(uint8_t *, int, int)> qt_video_callback;

#endif // AIRPLAYWINDOW_H
