#include "airplaywindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

// V4L2 includes
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Include the rpiplay server functions
extern "C" {
int start_server_qt(const char *name);
int stop_server_qt();
}

// Global callback for video renderer
std::function<void(uint8_t *, int, int)> qt_video_callback;

AirPlayWindow::AirPlayWindow(QWidget *parent)
    : QMainWindow(parent), m_videoLabel(nullptr), m_statusLabel(nullptr),
      m_serverThread(nullptr), m_serverRunning(false), m_v4l2_fd(-1),
      m_v4l2_width(0), m_v4l2_height(0), m_v4l2_enabled(false)
{
    setupUI();

    // Setup video callback
    qt_video_callback = [this](uint8_t *data, int width, int height) {
        // V4L2 output if enabled
        if (m_v4l2_enabled) {
            writeFrameToV4L2(data, width, height);
        }

        QByteArray frameData((const char *)data, width * height * 3);
        QMetaObject::invokeMethod(this, "updateVideoFrame",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, frameData),
                                  Q_ARG(int, width), Q_ARG(int, height));
    };
}

AirPlayWindow::~AirPlayWindow()
{
    stopAirPlayServer();
    closeV4L2();
    qt_video_callback = nullptr;
}

void AirPlayWindow::setupUI()
{
    setWindowTitle("AirPlay Receiver");
    resize(800, 600);

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Status area
    QHBoxLayout *statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Server: Stopped");
    m_statusLabel->setStyleSheet("QLabel { padding: 5px; background-color: "
                                 "#f0f0f0; border: 1px solid #ccc; }");

    QPushButton *startBtn = new QPushButton("Start Server");
    QPushButton *stopBtn = new QPushButton("Stop Server");

    connect(startBtn, &QPushButton::clicked, this,
            &AirPlayWindow::startAirPlayServer);
    connect(stopBtn, &QPushButton::clicked, this,
            &AirPlayWindow::stopAirPlayServer);

    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();

    // V4L2 controls
    QCheckBox *v4l2CheckBox = new QCheckBox("Enable V4L2 Output");
    connect(v4l2CheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        m_v4l2_enabled = enabled;
        if (!enabled) {
            closeV4L2();
        }
        qDebug() << "V4L2 output" << (enabled ? "enabled" : "disabled");
    });

    QPushButton *testV4L2Btn = new QPushButton("Test V4L2");
    testV4L2Btn->setToolTip("Test V4L2 loopback device availability");
    connect(testV4L2Btn, &QPushButton::clicked, this,
            [this]() { testV4L2Device(); });

    statusLayout->addWidget(v4l2CheckBox);
    statusLayout->addWidget(testV4L2Btn);
    statusLayout->addWidget(startBtn);
    statusLayout->addWidget(stopBtn);

    // Video display area
    m_videoLabel = new QLabel("Waiting for AirPlay connection...");
    m_videoLabel->setMinimumSize(640, 480);
    m_videoLabel->setStyleSheet(
        "QLabel { background-color: black; color: white; }");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setScaledContents(true);

    mainLayout->addLayout(statusLayout);
    mainLayout->addWidget(m_videoLabel, 1);

    // Auto-start server
    startAirPlayServer();
}

void AirPlayWindow::startAirPlayServer()
{
    if (m_serverRunning)
        return;

    m_serverThread = new AirPlayServerThread(this);
    connect(m_serverThread, &AirPlayServerThread::statusChanged, this,
            &AirPlayWindow::onServerStatusChanged);
    connect(m_serverThread, &AirPlayServerThread::videoFrameReady, this,
            &AirPlayWindow::updateVideoFrame);

    m_serverThread->start();
}

void AirPlayWindow::stopAirPlayServer()
{
    if (m_serverThread) {
        m_serverThread->stopServer();
        m_serverThread->wait(3000);
        m_serverThread->deleteLater();
        m_serverThread = nullptr;
    }
    m_serverRunning = false;
    m_statusLabel->setText("Server: Stopped");
}

void AirPlayWindow::updateVideoFrame(QByteArray frameData, int width,
                                     int height)
{
    if (frameData.size() != width * height * 3)
        return;

    QImage image((const uchar *)frameData.data(), width, height,
                 QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(image);
    m_videoLabel->setPixmap(pixmap);
}

void AirPlayWindow::onServerStatusChanged(bool running)
{
    m_serverRunning = running;
    m_statusLabel->setText(running ? "Server: Running" : "Server: Stopped");
}

// AirPlayServerThread implementation
AirPlayServerThread::AirPlayServerThread(QObject *parent)
    : QThread(parent), m_shouldStop(false)
{
}

AirPlayServerThread::~AirPlayServerThread()
{
    stopServer();
    wait();
}

void AirPlayServerThread::stopServer() { m_shouldStop = true; }

void AirPlayServerThread::run()
{
    emit statusChanged(true);

    // Start the server (you'll need to adapt the rpiplay server code)
    start_server_qt("iDescriptor");

    while (!m_shouldStop) {
        msleep(100);
    }

    stop_server_qt();
    emit statusChanged(false);
}

// V4L2 Implementation
void AirPlayWindow::initV4L2(int width, int height, const char *device)
{
    closeV4L2(); // Close previous device if any

    m_v4l2_fd = open(device, O_WRONLY);
    if (m_v4l2_fd < 0) {
        qWarning("Failed to open V4L2 device %s: %s", device, strerror(errno));
        QMessageBox::warning(
            this, "V4L2 Error",
            QString("Failed to open V4L2 device %1.\n"
                    "Make sure v4l2loopback module is loaded:\n"
                    "sudo modprobe v4l2loopback")
                .arg(device));
        return;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = width * 3;
    fmt.fmt.pix.sizeimage = (unsigned int)width * height * 3;

    if (ioctl(m_v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning("Failed to set V4L2 format: %s", strerror(errno));
        ::close(m_v4l2_fd);
        m_v4l2_fd = -1;
        QMessageBox::warning(
            this, "V4L2 Error",
            "Failed to set V4L2 video format. Device may not support RGB24.");
        return;
    }

    m_v4l2_width = width;
    m_v4l2_height = height;
    qDebug("V4L2 device %s initialized to %dx%d", device, width, height);
}

void AirPlayWindow::closeV4L2()
{
    if (m_v4l2_fd >= 0) {
        ::close(m_v4l2_fd);
        m_v4l2_fd = -1;
        qDebug("V4L2 device closed.");
    }
}

void AirPlayWindow::writeFrameToV4L2(uint8_t *data, int width, int height)
{
    // Check if V4L2 device needs to be initialized or re-initialized
    if (m_v4l2_fd < 0 || m_v4l2_width != width || m_v4l2_height != height) {
        initV4L2(width, height, "/dev/video0"); // Use your v4l2loopback device
    }

    // Write frame to V4L2 device if it's open
    if (m_v4l2_fd >= 0) {
        ssize_t bytes_written =
            write(m_v4l2_fd, data, (size_t)width * height * 3);
        if (bytes_written < 0) {
            qWarning("Failed to write frame to V4L2 device: %s",
                     strerror(errno));
            closeV4L2(); // Close on error to retry initialization
        }
    }
}

void AirPlayWindow::testV4L2Device()
{
    const char *device = "/dev/video0";
    int fd = open(device, O_WRONLY);

    if (fd < 0) {
        QMessageBox::critical(this, "V4L2 Test",
                              QString("Failed to open V4L2 device %1.\n"
                                      "Error: %2\n\n"
                                      "To fix this, run:\n"
                                      "sudo modprobe v4l2loopback video_nr=0")
                                  .arg(device)
                                  .arg(strerror(errno)));
        return;
    }

    // Test if device supports V4L2_PIX_FMT_RGB24
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = 1280;
    fmt.fmt.pix.height = 720;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = 1280 * 3;
    fmt.fmt.pix.sizeimage = 1280 * 720 * 3;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        ::close(fd);
        QMessageBox::warning(
            this, "V4L2 Test",
            QString("V4L2 device %1 exists but doesn't support RGB24 format.\n"
                    "Error: %2")
                .arg(device)
                .arg(strerror(errno)));
        return;
    }

    ::close(fd);
    QMessageBox::information(
        this, "V4L2 Test",
        QString("✓ V4L2 device %1 is working correctly!\n\n"
                "You can now:\n"
                "• View output with: ffplay %1\n"
                "• Use in OBS as Video Capture Device\n"
                "• Record with: ffmpeg -f v4l2 -i %1 output.mp4")
            .arg(device));
}
