#ifndef SERVICEMANAGER_H
#define SERVICEMANAGER_H

#include "iDescriptor.h"
#include <QDebug>
#include <functional>
#include <libimobiledevice/afc.h>
#include <mutex>

/**
 * @brief Centralized manager for device service operations with thread safety
 *
 * This class provides thread-safe wrappers for all device operations to prevent
 * crashes when devices are unplugged during active operations. It uses a
 * per-device recursive mutex to ensure that device cleanup waits for all
 * operations to complete.
 */
class ServiceManager
{
public:
    /**
     * @brief Execute an AFC operation safely with device locking
     * @param device The device to operate on
     * @param operation Function that performs the AFC operation
     * @return Result of the operation
     */
    template <typename T>
    static T executeOperation(iDescriptorDevice *device,
                              std::function<T()> operation)
    {
        if (!device || !device->mutex) {
            return T{}; // Return default-constructed value for the type
        }

        std::lock_guard<std::recursive_mutex> lock(*device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            return T{};
        }

        return operation();
    }

    template <typename T>
    static T executeOperation(iDescriptorDevice *device,
                              std::function<T()> operation, T failureValue)
    {
        if (!device || !device->mutex) {
            return failureValue;
        }

        std::lock_guard<std::recursive_mutex> lock(*device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            return failureValue;
        }

        return operation();
    }

    static afc_error_t
    executeAfcOperation(iDescriptorDevice *device,
                        std::function<afc_error_t()> operation)
    {
        try {
            if (!device || !device->mutex) {
                return AFC_E_UNKNOWN_ERROR;
            }

            std::lock_guard<std::recursive_mutex> lock(*device->mutex);

            // Double-check device is still valid after acquiring lock
            if (!device->afcClient) {
                return AFC_E_UNKNOWN_ERROR;
            }

            return operation();
        } catch (const std::exception &e) {
            qDebug() << "Exception in executeAfcOperation:" << e.what();
            return AFC_E_UNKNOWN_ERROR;
        }
    }

    /**
     * @brief Execute an AFC operation safely (void return version)
     * @param device The device to operate on
     * @param operation Function that performs the AFC operation
     */
    static void executeOperation(iDescriptorDevice *device,
                                 std::function<void()> operation)
    {
        if (!device || !device->mutex) {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(*device->mutex);

        // Double-check device is still valid after acquiring lock
        if (!device->afcClient) {
            return;
        }

        operation();
    }

    // Specific AFC operation wrappers
    static afc_error_t safeAfcReadDirectory(iDescriptorDevice *device,
                                            const char *path, char ***dirs);
    static afc_error_t safeAfcGetFileInfo(iDescriptorDevice *device,
                                          const char *path, char ***info);
    static afc_error_t safeAfcFileOpen(iDescriptorDevice *device,
                                       const char *path, afc_file_mode_t mode,
                                       uint64_t *handle);
    static afc_error_t safeAfcFileRead(iDescriptorDevice *device,
                                       uint64_t handle, char *data,
                                       uint32_t length, uint32_t *bytes_read);
    static afc_error_t safeAfcFileWrite(iDescriptorDevice *device,
                                        uint64_t handle, const char *data,
                                        uint32_t length,
                                        uint32_t *bytes_written);
    static afc_error_t safeAfcFileClose(iDescriptorDevice *device,
                                        uint64_t handle);
    static afc_error_t safeAfcFileSeek(iDescriptorDevice *device,
                                       uint64_t handle, int64_t offset,
                                       int whence);

    // Utility functions
    static QByteArray safeReadAfcFileToByteArray(iDescriptorDevice *device,
                                                 const char *path);
    static AFCFileTree safeGetFileTree(iDescriptorDevice *device,
                                       const std::string &path = "/");
};

#endif // SERVICEMANAGER_H
