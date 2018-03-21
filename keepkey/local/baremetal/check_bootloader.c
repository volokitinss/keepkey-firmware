/*
 * This file is part of the KeepKey project.
 *
 * Copyright (C) 2018 keepkeyjon <jon@keepkey.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "keepkey_board.h"
#include "layout.h"
#include "memory.h"
#include "sha2.h"

#include <libopencm3/stm32/flash.h>

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

enum BL_Kind {
    BL_UNKNOWN       = 0x0,
    BL_HOTPATCHABLE  = 0xa1f35c78,
    BL_PATCH_APPLIED = 0x95c3a027,
};

static enum BL_Kind check_bootloader_kind(void) {
    static uint8_t bl_hash[SHA256_DIGEST_LENGTH];
    if (32 != memory_bootloader_hash(bl_hash))
        return BL_UNKNOWN;

    // Fixed bootloaders
    // ---------------------
    if (0 == memcmp(bl_hash, "\xf1\x3c\xe2\x28\xc0\xbb\x2b\xdb\xc5\x6b\xdc\xb5\xf4\x56\x93\x67\xf8\xe3\x01\x10\x74\xcc\xc6\x33\x31\x34\x8d\xeb\x49\x8f\x2d\x8f", 32))
        return BL_PATCH_APPLIED; // v1.0.0, fixed

    if (0 == memcmp(bl_hash, "\xec\x61\x88\x36\xf8\x64\x23\xdb\xd3\x11\x4c\x37\xd6\xe3\xe4\xff\xdf\xb8\x7d\x9e\x4c\x61\x99\xcf\x3e\x16\x3a\x67\xb2\x74\x98\xa2", 32))
        return BL_PATCH_APPLIED; // v1.0.1, fixed

    if (0 == memcmp(bl_hash, "\x4f\x9c\x38\xc1\xcd\x06\xf5\x9e\x8d\x4d\xe8\xe0\xd3\x1c\xdd\x34\xc8\x31\x44\xd2\xdf\x55\x0c\x41\x2e\x00\x2b\x4b\x35\xbd\x4f\xb3", 32))
        return BL_PATCH_APPLIED; // v1.0.3, fixed

    if (0 == memcmp(bl_hash, "\x91\x7d\x19\x52\x26\x0c\x9b\x89\xf3\xa9\x6b\xea\x07\xee\xa4\x07\x4a\xfd\xcc\x0e\x8c\xdd\x5d\x06\x4e\x36\x86\x8b\xdd\x68\xba\x7d", 32))
        return BL_PATCH_APPLIED; // v1.0.3_signed, fixed

    if (0 == memcmp(bl_hash, "\xfc\x4e\x5c\x4d\xc2\xe5\x12\x7b\x68\x14\xa3\xf6\x94\x24\xc9\x36\xf1\xdc\x24\x1d\x1d\xaf\x2c\x5a\x2d\x8f\x07\x28\xeb\x69\xd2\x0d", 32))
        return BL_PATCH_APPLIED; // v1.0.4, fixed - SALT whitelabel

    // Unpatched bootloaders
    // ---------------------
    if (0 == memcmp(bl_hash, "\x63\x97\xc4\x46\xf6\xb9\x00\x2a\x8b\x15\x0b\xf4\xb9\xb4\xe0\xbb\x66\x80\x0e\xd0\x99\xb8\x81\xca\x49\x70\x01\x39\xb0\x55\x9f\x10", 32))
        return BL_HOTPATCHABLE; // v1.0.0, unpatched

    if (0 == memcmp(bl_hash, "\xd5\x44\xb5\xe0\x6b\x0c\x35\x5d\x68\xb8\x68\xac\x75\x80\xe9\xba\xb2\xd2\x24\xa1\xe2\x44\x08\x81\xcc\x1b\xca\x2b\x81\x67\x52\xd5", 32))
        return BL_HOTPATCHABLE; // v1.0.1, unpatched

    if (0 == memcmp(bl_hash, "\x5a\xa5\x5e\x69\xf1\xd9\xaa\x50\x4d\xe6\x0f\xaf\x22\xbe\x93\xcb\xd0\x3b\x13\x73\x2d\xcb\x07\xbb\xc0\xb7\xf9\x1d\x42\xe1\x4c\xcc", 32))
        return BL_HOTPATCHABLE; // v1.0.3, unpatched

    if (0 == memcmp(bl_hash, "\xcb\x22\x25\x48\xa3\x9f\xf6\xcb\xe2\xae\x2f\x02\xc8\xd4\x31\xc9\xae\x0d\xf8\x50\xf8\x14\x44\x49\x11\xf5\x21\xb9\x5a\xb0\x2f\x4c", 32))
        return BL_HOTPATCHABLE; // v1.0.3_signed, unpatched

    if (0 == memcmp(bl_hash, "\x77\x0b\x30\xaa\xa0\xbe\x88\x4e\xe8\x62\x18\x59\xf5\xd0\x55\x43\x7f\x89\x4a\x5c\x9c\x7c\xa2\x26\x35\xe7\x02\x4e\x05\x98\x57\xb7", 32))
        return BL_HOTPATCHABLE; // v1.0.4, unpatched - SALT whitelabel

    return BL_UNKNOWN;
}

/*
 * Hot-patch old bootloaders to disallow executing unsigned firmwares.
 *
 * \returns true iff this bootloader has been hotpatched
 */
static bool apply_hotpatch(void)
{
    const uintptr_t hotpatch_addr = 0x802026c;

    static const char hotpatch[18] = {
        0x00, 0x00, // movs r0, r0
        0x00, 0x00, // movs r0, r0
        0x00, 0x00, // movs r0, r0
        0x00, 0x00, // movs r0, r0
        0x00, 0x00, // movs r0, r0
        0x00, 0x00, // movs r0, r0
        0x00, 0x00, // movs r0, r0
        0x00, 0x00, // movs r0, r0
    };

    // Enable writing to the read-only sectors
    memory_unlock();
    flash_unlock();

    // Write into the sector.
    flash_program((uint32_t)hotpatch_addr, (uint8_t*)hotpatch, sizeof(hotpatch));

    // Disallow writing to flash.
    flash_lock();

    // Ignore the reported error.
    flash_clear_status_flags();

    // Check for the hotpatch sequence
    return memcmp((void*)hotpatch_addr, hotpatch, sizeof(hotpatch)) == 0;
}

void check_bootloader(void) {
    enum BL_Kind kind = check_bootloader_kind();
    if (kind == BL_HOTPATCHABLE) {
        if (!apply_hotpatch()) {
            layout_warning_static("Hotpatch failed. Contact support.");
            system_halt();
        }
    } else if (kind == BL_UNKNOWN) {
        layout_warning_static("Unknown bootloader. Contact support.");
        system_halt();
    } else if (kind == BL_PATCH_APPLIED) {
        // do nothing, bootloader is already safe
    } else {
        layout_warning_static("B/L check failed. Reboot Device!");
        system_halt();
    }
}