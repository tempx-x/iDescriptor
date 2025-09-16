// https://github.com/libimobiledevice/libimobiledevice/blob/master/tools/ideviceinfo.c
/*
 * ideviceinfo.c
 * Simple utility to show information about an attached device
 *
 * Copyright (c) 2010-2019 Nikias Bassen, All Rights Reserved.
 * Copyright (c) 2009 Martin Szulecki All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#endif

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <plist/plist.h>
#include <pugixml.hpp>

#define FORMAT_KEY_VALUE 1
#define FORMAT_XML 2

static const char *domains[] = {
    "com.apple.disk_usage", "com.apple.disk_usage.factory",
    "com.apple.mobile.battery",
    /* FIXME: For some reason lockdownd segfaults on this, works sometimes
       though "com.apple.mobile.debug",. */
    "com.apple.iqagent", "com.apple.purplebuddy", "com.apple.PurpleBuddy",
    "com.apple.mobile.chaperone", "com.apple.mobile.third_party_termination",
    "com.apple.mobile.lockdownd", "com.apple.mobile.lockdown_cache",
    "com.apple.xcode.developerdomain", "com.apple.international",
    "com.apple.mobile.data_sync", "com.apple.mobile.tethered_sync",
    "com.apple.mobile.mobile_application_usage", "com.apple.mobile.backup",
    "com.apple.mobile.nikita", "com.apple.mobile.restriction",
    "com.apple.mobile.user_preferences", "com.apple.mobile.sync_data_class",
    "com.apple.mobile.software_behavior",
    "com.apple.mobile.iTunes.SQLMusicLibraryPostProcessCommands",
    "com.apple.mobile.iTunes.accessories",
    "com.apple.mobile.internal",          /**< iOS 4.0+ */
    "com.apple.mobile.wireless_lockdown", /**< iOS 4.0+ */
    "com.apple.fairplay", "com.apple.iTunes", "com.apple.mobile.iTunes.store",
    "com.apple.mobile.iTunes", "com.apple.fmip", "com.apple.Accessibility",
    NULL};

plist_t get_device_info(const char *udid, int use_network, int simple,
                        lockdownd_client_t client, idevice_t device)
{
    lockdownd_error_t ldret = LOCKDOWN_E_UNKNOWN_ERROR;
    idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
    plist_t node = NULL;

    /* run query and output information */
    if (lockdownd_get_value(client, NULL, NULL, &node) != LOCKDOWN_E_SUCCESS) {
        fprintf(stderr, "ERROR: Could not get value\n");
    }

    plist_t disk_info = nullptr;
    uint64_t total_space = 0;
    uint64_t free_space = 0;
    /* {
    "AmountDataAvailable": 6663077888,
    "AmountDataReserved": 209715200,
    "AmountRestoreAvailable": 11524079616,
    "CalculateDiskUsage": "OkilyDokily",
    "NANDInfo": <01000000 01000000 01000000 00000080 ... 00 00000000 000000>,
    "TotalDataAvailable": 6872793088,
    "TotalDataCapacity": 11306721280,
    "TotalDiskCapacity": 16000000000,
    "TotalSystemAvailable": 0,
    "TotalSystemCapacity": 4693204992
    }*/
    /* trying to set DiskInfo as key results in
    xplist.c:365: node_to_xml: Assertion `(node->children->count % 2) == 0'
    failed. so lets do merge it*/
    if (lockdownd_get_value(client, "com.apple.disk_usage", nullptr,
                            &disk_info) == LOCKDOWN_E_SUCCESS) {
        // merge dict
        plist_dict_merge(&node, disk_info);
        plist_free(disk_info);
    }

    return node;
}

void get_device_info_xml(const char *udid, int use_network, int simple,
                         pugi::xml_document &infoXml, lockdownd_client_t client,
                         idevice_t device)
{
    plist_t node = get_device_info(udid, use_network, simple, client, device);
    if (!node)
        return;

    char *xml_string = nullptr;
    uint32_t xml_length = 0;
    plist_to_xml(node, &xml_string, &xml_length);
    plist_free(node);

    if (xml_string) {
        infoXml.load_string(xml_string);
        free(xml_string);
    }
}