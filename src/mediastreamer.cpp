#include "mediastreamer.h"
#include <QtGlobal>

#include "iDescriptor.h"
#include "servicemanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QHostAddress>
#include <QMutexLocker>
#include <QTcpSocket>
#include <QTimer>
#include <libimobiledevice/afc.h>
#include <memory>

MediaStreamer::MediaStreamer(iDescriptorDevice *device, afc_client_t afcClient,
                             const QString &filePath, QObject *parent)
    : QTcpServer(parent), m_device(device), m_afcClient(afcClient),
      m_filePath(filePath), m_cachedFileSize(-1), m_fileSizeCached(false)
{
    // Listen on localhost with automatic port assignment
    // todo: use qhostadress::localhost for jailbroken widget
    if (!listen(QHostAddress::LocalHost, 0)) {
        qWarning() << "MediaStreamer failed to start:" << errorString();
    } else {
        qDebug() << "MediaStreamer listening on" << getUrl().toString();
    }
}

MediaStreamer::~MediaStreamer()
{
    // Close all active connections
    QMutexLocker locker(&m_connectionsMutex);
    for (QTcpSocket *socket : m_activeConnections) {
        socket->disconnectFromHost();
        if (socket->state() != QAbstractSocket::UnconnectedState) {
            socket->waitForDisconnected(1000);
        }
        socket->deleteLater();
    }
    m_activeConnections.clear();
}

QUrl MediaStreamer::getUrl() const
{
    if (!isListening()) {
        return QUrl();
    }
    // todo pass folder/filename
    return QUrl(QString("http://127.0.0.1:%1/%2")
                    .arg(serverPort())
                    .arg(QFileInfo(m_filePath).fileName()));
}

bool MediaStreamer::isListening() const { return QTcpServer::isListening(); }

void MediaStreamer::incomingConnection(qintptr socketDescriptor)
{
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qWarning() << "Failed to set socket descriptor";
        socket->deleteLater();
        return;
    }

    // Add to active connections
    {
        QMutexLocker locker(&m_connectionsMutex);
        m_activeConnections.append(socket);
    }

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray requestData = socket->readAll();
        HttpRequest request = parseHttpRequest(requestData);
        handleRequest(socket, request);
    });

    connect(socket, &QTcpSocket::disconnected, this,
            &MediaStreamer::handleClientDisconnected);
    connect(socket,
            QOverload<QAbstractSocket::SocketError>::of(
                &QAbstractSocket::errorOccurred),
            this, [this, socket](QAbstractSocket::SocketError error) {
                qWarning() << "Socket error:" << error << socket->errorString();
                socket->deleteLater();
            });

    qDebug() << "MediaStreamer: Client connected from"
             << socket->peerAddress().toString();
}

void MediaStreamer::handleClientDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    {
        QMutexLocker locker(&m_connectionsMutex);
        m_activeConnections.removeAll(socket);
    }

    qDebug() << "MediaStreamer: Client disconnected";
    socket->deleteLater();
}

MediaStreamer::HttpRequest
MediaStreamer::parseHttpRequest(const QByteArray &requestData)
{
    HttpRequest request;

    const QString requestStr = QString::fromUtf8(requestData);
    const QStringList lines = requestStr.split("\r\n");

    if (lines.isEmpty()) {
        return request;
    }

    // Parse request line: "GET /path HTTP/1.1"
    const QStringList requestLine = lines[0].split(" ");
    if (requestLine.size() >= 3) {
        request.method = requestLine[0];
        request.path = requestLine[1];
        request.httpVersion = requestLine[2];
    }

    // Parse headers
    for (int i = 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.isEmpty())
            break; // End of headers

        const int colonPos = line.indexOf(':');
        if (colonPos > 0) {
            const QString key = line.left(colonPos).trimmed();
            const QString value = line.mid(colonPos + 1).trimmed();
            request.headers[key.toLower()] = value;
        }
    }

    // Parse Range header if present
    if (request.headers.contains("range")) {
        const QString rangeHeader = request.headers["range"];
        if (rangeHeader.startsWith("bytes=")) {
            const QString rangeValue = rangeHeader.mid(6); // Remove "bytes="
            const QStringList rangeParts = rangeValue.split('-');

            if (rangeParts.size() == 2) {
                request.hasRange = true;
                bool ok;
                request.rangeStart = rangeParts[0].toLongLong(&ok);
                if (!ok)
                    request.rangeStart = 0;

                if (!rangeParts[1].isEmpty()) {
                    request.rangeEnd = rangeParts[1].toLongLong(&ok);
                    if (!ok)
                        request.rangeEnd = -1;
                }
            }
        }
    }

    return request;
}

void MediaStreamer::handleRequest(QTcpSocket *socket,
                                  const HttpRequest &request)
{
    if (request.method != "GET" && request.method != "HEAD") {
        sendErrorResponse(socket, 405, "Method Not Allowed");
        return;
    }

    const qint64 fileSize = getFileSize();
    if (fileSize <= 0) {
        sendErrorResponse(socket, 404, "File Not Found");
        return;
    }

    qint64 rangeStart = 0;
    qint64 rangeEnd = fileSize - 1;

    if (request.hasRange) {
        rangeStart = request.rangeStart;
        if (request.rangeEnd >= 0 && request.rangeEnd < fileSize) {
            rangeEnd = request.rangeEnd;
        }

        // Validate range
        if (rangeStart < 0 || rangeStart >= fileSize || rangeStart > rangeEnd) {
            sendErrorResponse(socket, 416, "Range Not Satisfiable");
            return;
        }
    }

    const qint64 contentLength = rangeEnd - rangeStart + 1;
    const QString mimeType = getMimeType();

    // Send response headers
    QByteArray response;
    if (request.hasRange) {
        response += "HTTP/1.1 206 Partial Content\r\n";
        response += QString("Content-Range: bytes %1-%2/%3\r\n")
                        .arg(rangeStart)
                        .arg(rangeEnd)
                        .arg(fileSize)
                        .toUtf8();
    } else {
        response += "HTTP/1.1 200 OK\r\n";
    }

    response += "Accept-Ranges: bytes\r\n";
    response += QString("Content-Length: %1\r\n").arg(contentLength).toUtf8();
    response += QString("Content-Type: %1\r\n").arg(mimeType).toUtf8();
    response += "Connection: close\r\n";
    response += "Cache-Control: no-cache\r\n";
    response += "\r\n";

    socket->write(response);
    // Remove blocking call - let Qt handle when bytes are actually written

    // For HEAD requests, don't send body
    if (request.method == "HEAD") {
        socket->disconnectFromHost();
        return;
    }

    // Stream file content
    streamFileRange(socket, rangeStart, rangeEnd);
}

void MediaStreamer::sendErrorResponse(QTcpSocket *socket, int statusCode,
                                      const QString &statusText)
{
    const QByteArray response = QString("HTTP/1.1 %1 %2\r\n"
                                        "Content-Length: 0\r\n"
                                        "Connection: close\r\n"
                                        "\r\n")
                                    .arg(statusCode)
                                    .arg(statusText)
                                    .toUtf8();

    socket->write(response);
    // Remove blocking call
    socket->disconnectFromHost();
}

void MediaStreamer::streamFileRange(QTcpSocket *socket, qint64 startByte,
                                    qint64 endByte)
{
    // Create a new streaming context for this request
    StreamingContext *context = new StreamingContext();
    context->socket = socket;
    context->device = m_device;
    context->filePath = m_filePath;
    context->startByte = startByte;
    context->endByte = endByte;
    context->bytesRemaining = endByte - startByte + 1;
    context->afcHandle = 0;

    qDebug() << "m_filepath" << m_filePath;
    // Open file on device using ServiceManager
    const QByteArray pathBytes = m_filePath.toUtf8();
    afc_error_t openResult = ServiceManager::safeAfcFileOpen(
        m_device, pathBytes.constData(), AFC_FOPEN_RDONLY, &context->afcHandle);

    if (openResult != AFC_E_SUCCESS || context->afcHandle == 0) {
        qWarning() << "Failed to open file on device:" << m_filePath;
        delete context;
        socket->disconnectFromHost();
        return;
    }

    // Seek to start position if needed
    if (startByte > 0) {
        afc_error_t seekResult = ServiceManager::safeAfcFileSeek(
            m_device, context->afcHandle, startByte, SEEK_SET);
        if (seekResult != AFC_E_SUCCESS) {
            qWarning() << "Failed to seek in file:" << m_filePath;
            ServiceManager::safeAfcFileClose(m_device, context->afcHandle);
            delete context;
            socket->disconnectFromHost();
            return;
        }
    }

    qDebug() << "Starting non-blocking stream for range" << startByte << "-"
             << endByte << "(" << context->bytesRemaining << "bytes)";

    // Store context as socket property for cleanup
    socket->setProperty("streamingContext",
                        QVariant::fromValue(static_cast<void *>(context)));

    // Connect to socket signals for async streaming
    connect(socket, &QTcpSocket::bytesWritten, this,
            [this, context](qint64 bytes) {
                Q_UNUSED(bytes)
                // Check if context is still valid
                QTcpSocket *senderSocket = qobject_cast<QTcpSocket *>(sender());
                if (!senderSocket ||
                    senderSocket->property("streamingContext").isNull()) {
                    return; // Context already cleaned up
                }
                // Continue streaming when socket buffer has space
                if (context->socket->bytesToWrite() <
                    32768) { // Keep buffer below 32KB
                    streamNextChunk(context);
                }
            });

    connect(socket, &QTcpSocket::disconnected, this, [this, context]() {
        // Check if context is still valid before cleanup
        QTcpSocket *senderSocket = qobject_cast<QTcpSocket *>(sender());
        if (!senderSocket ||
            senderSocket->property("streamingContext").isNull()) {
            return; // Already cleaned up
        }
        cleanupStreamingContext(context);
    });

    // Start streaming the first chunk
    streamNextChunk(context);
}

qint64 MediaStreamer::getFileSize()
{
    QMutexLocker locker(&m_fileSizeMutex);

    if (m_fileSizeCached) {
        return m_cachedFileSize;
    }

    // Get file info from device using ServiceManager
    char **info = nullptr;
    const QByteArray pathBytes = m_filePath.toUtf8();
    afc_error_t result = ServiceManager::safeAfcGetFileInfo(
        m_device, pathBytes.constData(), &info);

    if (result != AFC_E_SUCCESS || !info) {
        qWarning() << "Failed to get file info for:" << m_filePath;
        return -1;
    }

    qint64 fileSize = -1;
    for (int i = 0; info[i]; i += 2) {
        if (strcmp(info[i], "st_size") == 0) {
            bool ok;
            fileSize = QString(info[i + 1]).toLongLong(&ok);
            if (!ok)
                fileSize = -1;
            break;
        }
    }

    afc_dictionary_free(info);

    if (fileSize > 0) {
        m_cachedFileSize = fileSize;
        m_fileSizeCached = true;
    }

    return fileSize;
}

QString MediaStreamer::getMimeType() const
{
    const QString lower = m_filePath.toLower();

    if (lower.endsWith(".mp4") || lower.endsWith(".m4v")) {
        return "video/mp4";
    } else if (lower.endsWith(".mov")) {
        return "video/quicktime";
    } else if (lower.endsWith(".avi")) {
        return "video/x-msvideo";
    } else if (lower.endsWith(".mkv")) {
        return "video/x-matroska";
    }

    return "application/octet-stream";
}

void MediaStreamer::streamNextChunk(StreamingContext *context)
{
    if (!context || !context->socket) {
        return; // Invalid context, don't cleanup here
    }

    // Check if context has been marked for cleanup
    if (context->socket->property("streamingContext").isNull()) {
        return; // Already cleaned up
    }

    if (context->bytesRemaining <= 0) {
        qDebug() << "Streaming completed for"
                 << QFileInfo(context->filePath).fileName();
        cleanupStreamingContext(context);
        return;
    }

    // Check if socket is still valid
    if (context->socket->state() != QAbstractSocket::ConnectedState) {
        cleanupStreamingContext(context);
        return;
    }

    const int CHUNK_SIZE = 64 * 1024; // 64KB chunks
    const uint32_t bytesToRead = static_cast<uint32_t>(
        qMin(static_cast<qint64>(CHUNK_SIZE), context->bytesRemaining));

    auto buffer = std::make_unique<char[]>(bytesToRead);
    uint32_t bytesRead = 0;

    afc_error_t readResult = ServiceManager::safeAfcFileRead(
        m_device, context->afcHandle, buffer.get(), bytesToRead, &bytesRead);

    if (readResult != AFC_E_SUCCESS || bytesRead == 0) {
        qWarning() << "AFC read error or EOF during streaming";
        cleanupStreamingContext(context);
        return;
    }

    const qint64 bytesWritten = context->socket->write(buffer.get(), bytesRead);
    if (bytesWritten == -1) {
        qWarning() << "Socket write error";
        cleanupStreamingContext(context);
        return;
    }

    context->bytesRemaining -= bytesWritten;

    // If we're done, clean up
    if (context->bytesRemaining <= 0) {
        qDebug() << "Streaming completed for"
                 << QFileInfo(context->filePath).fileName();
        cleanupStreamingContext(context);
        return;
    }

    // If socket buffer is getting full, let bytesWritten signal handle the next
    // chunk Otherwise, continue immediately for better performance
    if (context->socket->bytesToWrite() >= 32768) {
        // Wait for bytesWritten signal
        return;
    } else {
        // Continue immediately with safety check
        QTimer::singleShot(0, this, [this, context]() {
            // Double-check context is still valid when timer fires
            if (context && context->socket &&
                !context->socket->property("streamingContext").isNull()) {
                streamNextChunk(context);
            }
        });
    }
}

void MediaStreamer::cleanupStreamingContext(StreamingContext *context)
{
    if (!context)
        return;

    // Mark as cleaned up immediately to prevent double cleanup
    if (context->socket) {
        // Check if already cleaned up
        if (context->socket->property("streamingContext").isNull()) {
            return; // Already cleaned up
        }
        context->socket->setProperty("streamingContext", QVariant());
    }

    if (context->afcHandle != 0) {
        ServiceManager::safeAfcFileClose(context->device, context->afcHandle);
        context->afcHandle = 0;
    }

    if (context->socket) {
        // Disconnect all our custom signals to prevent further callbacks
        disconnect(context->socket, &QTcpSocket::bytesWritten, this, nullptr);
        disconnect(context->socket, &QTcpSocket::disconnected, this, nullptr);

        context->socket->disconnectFromHost();
        context->socket = nullptr; // Prevent further access
    }

    qDebug() << "Streaming context cleaned up for"
             << QFileInfo(context->filePath).fileName();
    delete context;
}
