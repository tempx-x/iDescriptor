#include "servicemanager.h"

afc_error_t ServiceManager::safeAfcReadDirectory(iDescriptorDevice *device,
                                                 const char *path, char ***dirs)
{
    return executeAfcOperation(device, [device, path, dirs]() {
        return afc_read_directory(device->afcClient, path, dirs);
    });
}

afc_error_t ServiceManager::safeAfcGetFileInfo(iDescriptorDevice *device,
                                               const char *path, char ***info)
{
    return executeAfcOperation(device, [device, path, info]() {
        return afc_get_file_info(device->afcClient, path, info);
    });
}

afc_error_t ServiceManager::safeAfcFileOpen(iDescriptorDevice *device,
                                            const char *path,
                                            afc_file_mode_t mode,
                                            uint64_t *handle)
{
    return executeAfcOperation(device, [device, path, mode, handle]() {
        return afc_file_open(device->afcClient, path, mode, handle);
    });
}

afc_error_t ServiceManager::safeAfcFileRead(iDescriptorDevice *device,
                                            uint64_t handle, char *data,
                                            uint32_t length,
                                            uint32_t *bytes_read)
{
    return executeAfcOperation(
        device, [device, handle, data, length, bytes_read]() {
            return afc_file_read(device->afcClient, handle, data, length,
                                 bytes_read);
        });
}

afc_error_t ServiceManager::safeAfcFileWrite(iDescriptorDevice *device,
                                             uint64_t handle, const char *data,
                                             uint32_t length,
                                             uint32_t *bytes_written)
{
    return executeAfcOperation(
        device, [device, handle, data, length, bytes_written]() {
            return afc_file_write(device->afcClient, handle, data, length,
                                  bytes_written);
        });
}

afc_error_t ServiceManager::safeAfcFileClose(iDescriptorDevice *device,
                                             uint64_t handle)
{
    return executeAfcOperation(device, [device, handle]() {
        return afc_file_close(device->afcClient, handle);
    });
}

afc_error_t ServiceManager::safeAfcFileSeek(iDescriptorDevice *device,
                                            uint64_t handle, int64_t offset,
                                            int whence)
{
    return executeAfcOperation(device, [device, handle, offset, whence]() {
        return afc_file_seek(device->afcClient, handle, offset, whence);
    });
}

QByteArray ServiceManager::safeReadAfcFileToByteArray(iDescriptorDevice *device,
                                                      const char *path)
{
    return executeOperation<QByteArray>(device, [device, path]() -> QByteArray {
        return read_afc_file_to_byte_array(device->afcClient, path);
    });
}

AFCFileTree ServiceManager::safeGetFileTree(iDescriptorDevice *device,
                                            const std::string &path)
{
    return executeOperation<AFCFileTree>(
        device, [device, path]() -> AFCFileTree {
            return get_file_tree(device->afcClient, path.c_str());
        });
}