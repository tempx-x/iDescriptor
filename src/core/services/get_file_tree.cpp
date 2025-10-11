#include "../../iDescriptor.h"
#include <QDebug>
#include <iostream>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/lockdown.h>
#include <string.h>

AFCFileTree get_file_tree(afc_client_t afcClient,
                          const std::string &path)
{

    AFCFileTree result;
    result.currentPath = path;

    if (afcClient == nullptr) {
        qDebug() << "AFC client is not initialized in get_file_tree";
        result.success = false;
        return result;
    }

    char **dirs = NULL;
    if (afc_read_directory(afcClient, path.c_str(), &dirs) !=
        AFC_E_SUCCESS) {
        result.success = false;
        return result;
    }

    for (int i = 0; dirs[i]; i++) {
        std::string entryName = dirs[i];
        if (entryName == "." || entryName == "..")
            continue;

        char **info = NULL;
        std::string fullPath = path;
        if (fullPath.back() != '/')
            fullPath += "/";
        fullPath += entryName;
        bool isDir = false;
        if (afc_get_file_info(afcClient, fullPath.c_str(), &info) ==
                AFC_E_SUCCESS &&
            info) {
            if (entryName == "var") {
                qDebug() << "File info for var:" << info[0] << info[1]
                         << info[2] << info[3] << info[4] << info[5];
            }
            for (int j = 0; info[j]; j += 2) {
                if (strcmp(info[j], "st_ifmt") == 0) {
                    if (strcmp(info[j + 1], "S_IFDIR") == 0) {
                        isDir = true;
                    } else if (strcmp(info[j + 1], "S_IFLNK") == 0) {
                        /*symlink*/
                        char **dir_contents = NULL;
                        if (afc_read_directory(afcClient, fullPath.c_str(),
                                               &dir_contents) ==
                            AFC_E_SUCCESS) {
                            isDir = true;
                            if (dir_contents) {
                                afc_dictionary_free(dir_contents);
                            }
                        }
                    }
                    break;
                }
            }
            afc_dictionary_free(info);
        }
        result.entries.push_back({entryName, isDir});
    }
    if (dirs) {
        afc_dictionary_free(dirs);
    }
    result.success = true;
    return result;
}