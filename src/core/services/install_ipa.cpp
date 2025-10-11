#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <libimobiledevice/afc.h>
#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

#include <plist/plist.h>

#include <zip.h>

#ifdef WIN32
#include <windows.h>
#define wait_ms(x) Sleep(x)
#else
#define wait_ms(x)                                                             \
    {                                                                          \
        struct timespec ts;                                                    \
        ts.tv_sec = 0;                                                         \
        ts.tv_nsec = x * 1000000;                                              \
        nanosleep(&ts, NULL);                                                  \
    }
#endif

#define ITUNES_METADATA_PLIST_FILENAME "iTunesMetadata.plist"

const char PKG_PATH[] = "PublicStaging";

struct install_status_data {
    int command_completed;
    int err_occurred;
    char *last_status;
};

static void status_cb(plist_t command, plist_t status, void *user_data)
{
    struct install_status_data *isd = (struct install_status_data *)user_data;
    if (command && status) {
        char *command_name = NULL;
        instproxy_command_get_name(command, &command_name);

        /* get status */
        char *status_name = NULL;
        instproxy_status_get_name(status, &status_name);

        if (status_name) {
            if (!strcmp(status_name, "Complete")) {
                isd->command_completed = 1;
            }
        }

        /* get error if any */
        char *error_name = NULL;
        char *error_description = NULL;
        uint64_t error_code = 0;
        instproxy_status_get_error(status, &error_name, &error_description,
                                   &error_code);

        /* output/handling */
        if (!error_name) {
            if (status_name) {
                /* get progress if any */
                int percent = -1;
                instproxy_status_get_percent_complete(status, &percent);

                if (isd->last_status &&
                    (strcmp(isd->last_status, status_name))) {
                    printf("\n");
                }

                if (percent >= 0) {
                    printf("\r%s: %s (%d%%)", command_name, status_name,
                           percent);
                } else {
                    printf("\r%s: %s", command_name, status_name);
                }
                if (isd->command_completed) {
                    printf("\n");
                }
            }
        } else {
            /* report error to the user */
            if (error_description)
                fprintf(stderr,
                        "ERROR: %s failed. Got error \"%s\" with code "
                        "0x%08" PRIx64 ": %s\n",
                        command_name, error_name, error_code,
                        error_description ? error_description : "N/A");
            else
                fprintf(stderr, "ERROR: %s failed. Got error \"%s\".\n",
                        command_name, error_name);
            isd->err_occurred = 1;
        }

        /* clean up */
        free(error_name);
        free(error_description);

        free(isd->last_status);
        isd->last_status = status_name;

        free(command_name);
        command_name = NULL;
    } else {
        fprintf(stderr, "ERROR: %s was called with invalid arguments!\n",
                __func__);
    }
}

static int zip_get_contents(struct zip *zf, const char *filename,
                            int locate_flags, char **buffer, uint32_t *len)
{
    struct zip_stat zs;
    struct zip_file *zfile;
    int zindex = zip_name_locate(zf, filename, locate_flags);

    *buffer = NULL;
    *len = 0;

    if (zindex < 0) {
        return -1;
    }

    zip_stat_init(&zs);

    if (zip_stat_index(zf, zindex, 0, &zs) != 0) {
        fprintf(stderr, "ERROR: zip_stat_index '%s' failed!\n", filename);
        return -2;
    }

    if (zs.size > 10485760) {
        fprintf(stderr, "ERROR: file '%s' is too large!\n", filename);
        return -3;
    }

    zfile = zip_fopen_index(zf, zindex, 0);
    if (!zfile) {
        fprintf(stderr, "ERROR: zip_fopen '%s' failed!\n", filename);
        return -4;
    }

    *buffer = (char *)malloc(zs.size);
    if (zs.size > LLONG_MAX ||
        zip_fread(zfile, *buffer, zs.size) != (zip_int64_t)zs.size) {
        fprintf(stderr, "ERROR: zip_fread %" PRIu64 " bytes from '%s'\n",
                (uint64_t)zs.size, filename);
        free(*buffer);
        *buffer = NULL;
        zip_fclose(zfile);
        return -5;
    }
    *len = zs.size;
    zip_fclose(zfile);
    return 0;
}

static int zip_get_app_directory(struct zip *zf, char **path)
{
    zip_int64_t i = 0;
    zip_int64_t c = (zip_int64_t)zip_get_num_entries(zf, 0);
    int len = 0;
    const char *name = NULL;

    /* look through all filenames in the archive */
    do {
        /* get filename at current index */
        name = zip_get_name(zf, i++, 0);
        if (name != NULL) {
            /* check if we have a "Payload/.../" name */
            len = strlen(name);
            if (!strncmp(name, "Payload/", 8) && (len > 8)) {
                /* skip hidden files */
                if (name[8] == '.')
                    continue;

                /* locate the second directory delimiter */
                const char *p = name + 8;
                do {
                    if (*p == '/') {
                        break;
                    }
                } while (p++ != NULL);

                /* try next entry if not found */
                if (p == NULL)
                    continue;

                len = p - name + 1;

                /* make sure app directory endwith .app */
                if (len < 12 || strncmp(p - 4, ".app", 4)) {
                    continue;
                }

                if (path != NULL) {
                    free(*path);
                    *path = NULL;
                }

                /* allocate and copy filename */
                *path = (char *)malloc(len + 1);
                strncpy(*path, name, len);

                /* add terminating null character */
                char *t = *path + len;
                *t = '\0';
                break;
            }
        }
    } while (i < c);

    if (*path == NULL) {
        return -1;
    }

    return 0;
}

static int afc_upload_file(afc_client_t afc, const char *filename,
                           const char *dstfn)
{
    FILE *f = NULL;
    uint64_t af = 0;
    char buf[1048576];

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "fopen: %s: %s\n", filename, strerror(errno));
        return -1;
    }

    if ((afc_file_open(afc, dstfn, AFC_FOPEN_WRONLY, &af) != AFC_E_SUCCESS) ||
        !af) {
        fclose(f);
        fprintf(stderr, "afc_file_open on '%s' failed!\n", dstfn);
        return -1;
    }

    size_t amount = 0;
    do {
        amount = fread(buf, 1, sizeof(buf), f);
        if (amount > 0) {
            uint32_t written, total = 0;
            while (total < amount) {
                written = 0;
                afc_error_t aerr =
                    afc_file_write(afc, af, buf, amount, &written);
                if (aerr != AFC_E_SUCCESS) {
                    fprintf(stderr, "AFC Write error: %d\n", aerr);
                    break;
                }
                total += written;
            }
            if (total != amount) {
                fprintf(stderr, "Error: wrote only %u of %u\n", total,
                        (uint32_t)amount);
                afc_file_close(afc, af);
                fclose(f);
                return -1;
            }
        }
    } while (amount > 0);

    afc_file_close(afc, af);
    fclose(f);

    return 0;
}

instproxy_error_t install_IPA(idevice_t device, afc_client_t afc,
                              const char *filePath)
{
    lockdownd_client_t client = NULL;
    instproxy_client_t ipc = NULL;
    lockdownd_service_descriptor_t service = NULL;
    instproxy_error_t err = INSTPROXY_E_UNKNOWN_ERROR;
    char *bundleidentifier = NULL;
    struct install_status_data status_data = {0, 0, NULL};
    plist_t sinf = NULL;
    plist_t meta = NULL;
    char *pkgname = NULL;
    struct stat fst;
    char **strs = NULL;
    plist_t client_opts = instproxy_client_options_new();
    char *zbuf = NULL;
    uint32_t len = 0;
    plist_t meta_dict = NULL;
    int errp = 0;
    struct zip *zf = zip_open(filePath, 0, &errp);
    plist_t info = NULL;
    char *filename = NULL;
    char *app_directory_name = NULL;
    char *bundleexecutable = NULL;
    plist_t bname = NULL;
    char *sinfname = NULL;

    if (!device || !filePath || !afc) {
        fprintf(stderr, "ERROR: Invalid arguments passed to install_IPA.\n");
        return INSTPROXY_E_INVALID_ARG;
    }

    lockdownd_error_t lerr = lockdownd_client_new_with_handshake(
        device, &client, "ideviceinstaller");
    if (lerr != LOCKDOWN_E_SUCCESS) {
        fprintf(stderr, "Could not connect to lockdownd: %s. Exiting.\n",
                lockdownd_strerror(lerr));
        return INSTPROXY_E_OP_FAILED;
    }

    lerr = lockdownd_start_service(
        client, "com.apple.mobile.installation_proxy", &service);
    if (lerr != LOCKDOWN_E_SUCCESS) {
        fprintf(stderr,
                "Could not start com.apple.mobile.installation_proxy: %s\n",
                lockdownd_strerror(lerr));
        lockdownd_client_free(client);
        return INSTPROXY_E_OP_FAILED;
    }

    err = instproxy_client_new(device, service, &ipc);
    if (service) {
        lockdownd_service_descriptor_free(service);
        service = NULL;
    }

    if (err != INSTPROXY_E_SUCCESS) {
        fprintf(stderr, "Could not connect to installation_proxy!\n");
        lockdownd_client_free(client);
        return err;
    }

    setbuf(stdout, NULL);

    if (stat(filePath, &fst) != 0) {
        fprintf(stderr, "ERROR: stat: %s: %s\n", filePath, strerror(errno));
        err = INSTPROXY_E_INVALID_ARG;
        goto leave_cleanup;
    }

    if (afc_get_file_info(afc, PKG_PATH, &strs) != AFC_E_SUCCESS) {
        if (afc_make_directory(afc, PKG_PATH) != AFC_E_SUCCESS) {
            fprintf(stderr,
                    "WARNING: Could not create directory '%s' on device!\n",
                    PKG_PATH);
        }
    }
    if (strs) {
        int i = 0;
        while (strs[i]) {
            free(strs[i]);
            i++;
        }
        free(strs);
    }

    if (!zf) {
        fprintf(stderr, "ERROR: zip_open: %s: %d\n", filePath, errp);
        err = INSTPROXY_E_INVALID_ARG;
        goto leave_cleanup;
    }

    /* extract iTunesMetadata.plist from package */
    if (zip_get_contents(zf, ITUNES_METADATA_PLIST_FILENAME, 0, &zbuf, &len) ==
        0) {
        meta = plist_new_data(zbuf, len);
        plist_from_memory(zbuf, len, &meta_dict, NULL);
    }
    if (!meta_dict) {
        plist_free(meta);
        meta = NULL;
        fprintf(stderr, "WARNING: could not locate %s in archive!\n",
                ITUNES_METADATA_PLIST_FILENAME);
    }
    free(zbuf);

    /* determine .app directory in archive */
    zbuf = NULL;
    len = 0;

    if (zip_get_app_directory(zf, &app_directory_name)) {
        fprintf(stderr, "ERROR: Unable to locate .app directory in archive. "
                        "Make sure it is inside a 'Payload' directory.\n");
        err = INSTPROXY_E_INVALID_ARG;
        goto zip_cleanup;
    }

    /* construct full filename to Info.plist */
    filename = (char *)malloc(strlen(app_directory_name) + 10 + 1);
    strcpy(filename, app_directory_name);
    free(app_directory_name);
    app_directory_name = NULL;
    strcat(filename, "Info.plist");

    if (zip_get_contents(zf, filename, 0, &zbuf, &len) < 0) {
        fprintf(stderr, "WARNING: could not locate %s in archive!\n", filename);
        free(filename);
        err = INSTPROXY_E_INVALID_ARG;
        goto zip_cleanup;
    }
    free(filename);
    plist_from_memory(zbuf, len, &info, NULL);
    free(zbuf);

    if (!info) {
        fprintf(stderr, "Could not parse Info.plist!\n");
        err = INSTPROXY_E_INVALID_ARG;
        goto zip_cleanup;
    }

    bname = plist_dict_get_item(info, "CFBundleExecutable");
    if (bname) {
        plist_get_string_val(bname, &bundleexecutable);
    }

    bname = plist_dict_get_item(info, "CFBundleIdentifier");
    if (bname) {
        plist_get_string_val(bname, &bundleidentifier);
    }
    plist_free(info);
    info = NULL;

    if (!bundleexecutable) {
        fprintf(stderr, "Could not determine value for CFBundleExecutable!\n");
        err = INSTPROXY_E_INVALID_ARG;
        goto zip_cleanup;
    }

    if (asprintf(&sinfname, "Payload/%s.app/SC_Info/%s.sinf", bundleexecutable,
                 bundleexecutable) < 0) {
        fprintf(stderr, "Out of memory!?\n");
        err = INSTPROXY_E_UNKNOWN_ERROR;
        goto zip_cleanup;
    }
    free(bundleexecutable);

    /* extract .sinf from package */
    zbuf = NULL;
    len = 0;
    if (zip_get_contents(zf, sinfname, 0, &zbuf, &len) == 0) {
        sinf = plist_new_data(zbuf, len);
    } else {
        fprintf(stderr, "WARNING: could not locate %s in archive!\n", sinfname);
    }
    free(sinfname);
    free(zbuf);

    /* copy archive to device */
    pkgname = NULL;
    if (asprintf(&pkgname, "%s/%s", PKG_PATH, bundleidentifier) < 0) {
        fprintf(stderr, "Out of memory!?\n");
        err = INSTPROXY_E_UNKNOWN_ERROR;
        goto zip_cleanup;
    }

    printf("Copying '%s' to device... ", filePath);

    if (afc_upload_file(afc, filePath, pkgname) < 0) {
        printf("FAILED\n");
        free(pkgname);
        err = INSTPROXY_E_OP_FAILED;
        goto zip_cleanup;
    }

    printf("DONE.\n");

    if (bundleidentifier) {
        instproxy_client_options_add(client_opts, "CFBundleIdentifier",
                                     bundleidentifier, NULL);
    }
    if (sinf) {
        instproxy_client_options_add(client_opts, "ApplicationSINF", sinf,
                                     NULL);
    }
    if (meta) {
        instproxy_client_options_add(client_opts, "iTunesMetadata", meta, NULL);
    }

zip_cleanup:
    if (zf) {
        zip_unchange_all(zf);
        zip_close(zf);
    }
    if (err != INSTPROXY_E_SUCCESS) {
        goto leave_cleanup;
    }

    /* perform installation */
    printf("Installing '%s'\n", bundleidentifier);
    instproxy_install(ipc, pkgname, client_opts, status_cb, &status_data);

    instproxy_client_options_free(client_opts);
    free(pkgname);

    while (!status_data.command_completed && !status_data.err_occurred) {
        wait_ms(50);
    }

    if (status_data.err_occurred) {
        err = INSTPROXY_E_OP_FAILED;
    } else {
        err = INSTPROXY_E_SUCCESS;
    }

leave_cleanup:
    instproxy_client_free(ipc);
    lockdownd_client_free(client);
    free(bundleidentifier);
    free(status_data.last_status);

    return err;
}