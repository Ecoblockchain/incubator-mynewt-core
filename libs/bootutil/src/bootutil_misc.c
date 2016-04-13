/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include <inttypes.h>
#include <hal/hal_flash.h>
#include <config/config.h>
#include <os/os.h>
#include "bootutil/image.h"
#include "bootutil_priv.h"

static int boot_conf_set(int argc, char **argv, char *val);

static struct image_version boot_main;
static struct image_version boot_test;
static uint8_t boot_st_loaded;

static struct conf_handler boot_conf_handler = {
    .ch_name = "boot",
    .ch_get = NULL,
    .ch_set = boot_conf_set,
    .ch_commit = NULL,
    .ch_export = NULL,
};

static int
boot_conf_set(int argc, char **argv, char *val)
{
    int rc;
    int len;

    if (argc == 1) {
        if (!strcmp(argv[0], "main")) {
            len = sizeof(boot_main);
            rc = conf_bytes_from_str(val, &boot_main, &len);
        } else if (!strcmp(argv[0], "test")) {
            len = sizeof(boot_test);
            rc = conf_bytes_from_str(val, &boot_test, &len);
        } else if (!strcmp(argv[0], "status")) {
            len = boot_st_sz;
            rc = conf_bytes_from_str(val, boot_st, &len);
            if (rc == 0 && len > 0) {
                boot_st_loaded = 1;
            } else {
                boot_st_loaded = 0;
            }
        } else {
            rc = OS_ENOENT;
        }
    } else {
        rc = OS_ENOENT;
    }
    return rc;
}

static int
boot_vect_read_one(struct image_version *dst, struct image_version *src)
{
    if (src->iv_major == 0 && src->iv_minor == 0 &&
      src->iv_revision == 0 && src->iv_build_num == 0) {
        return BOOT_EBADVECT;
    }
    memcpy(dst, src, sizeof(*dst));
    return 0;
}

/**
 * Retrieves from the boot vector the version number of the test image (i.e.,
 * the image that has not been proven stable, and which will only run once).
 *
 * @param out_ver           On success, the test version gets written here.
 *
 * @return                  0 on success; nonzero on failure.
 */
int
boot_vect_read_test(struct image_version *out_ver)
{
    return boot_vect_read_one(out_ver, &boot_test);
}

/**
 * Retrieves from the boot vector the version number of the main image.
 *
 * @param out_ver           On success, the main version gets written here.
 *
 * @return                  0 on success; nonzero on failure.
 */
int
boot_vect_read_main(struct image_version *out_ver)
{
    return boot_vect_read_one(out_ver, &boot_main);
}

int
boot_vect_write_one(const char *name, struct image_version *ver)
{
    char str[CONF_STR_FROM_BYTES_LEN(sizeof(struct image_version))];
    char *to_store;

    if (!ver) {
        to_store = NULL;
    } else {
        if (!conf_str_from_bytes(ver, sizeof(*ver), str, sizeof(str))) {
            return -1;
        }
        to_store = str;
    }
    return conf_save_one(&boot_conf_handler, name, to_store);
}

/**
 * Write the test image version number from the boot vector.
 *
 * @return                  0 on success; nonzero on failure.
 */
int
boot_vect_write_test(struct image_version *ver)
{
    if (!ver) {
        memset(&boot_test, 0, sizeof(boot_test));
        return boot_vect_write_one("test", NULL);
    } else {
        memcpy(&boot_test, ver, sizeof(boot_test));
        return boot_vect_write_one("test", &boot_test);
    }
}

/**
 * Deletes the main image version number from the boot vector.
 *
 * @return                  0 on success; nonzero on failure.
 */
int
boot_vect_write_main(struct image_version *ver)
{
    if (!ver) {
        memset(&boot_main, 0, sizeof(boot_main));
        return boot_vect_write_one("main", NULL);
    } else {
        memcpy(&boot_main, ver, sizeof(boot_main));
        return boot_vect_write_one("main", &boot_main);
    }
}

static int
boot_read_image_header(struct image_header *out_hdr,
                       const struct boot_image_location *loc)
{
    int rc;

    rc = hal_flash_read(loc->bil_flash_id, loc->bil_address, out_hdr,
                        sizeof *out_hdr);
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    if (out_hdr->ih_magic != IMAGE_MAGIC) {
        return BOOT_EBADIMAGE;
    }

    return 0;
}

/**
 * Reads the header of each image present in flash.  Headers corresponding to
 * empty image slots are filled with 0xff bytes.
 *
 * @param out_headers           Points to an array of image headers.  Each
 *                                  element is filled with the header of the
 *                                  corresponding image in flash.
 * @param addresses             An array containing the flash addresses of each
 *                                  image slot.
 * @param num_addresses         The number of headers to read.  This should
 *                                  also be equal to the lengths of the
 *                                  out_headers and addresses arrays.
 */
void
boot_read_image_headers(struct image_header *out_headers,
                        const struct boot_image_location *addresses,
                        int num_addresses)
{
    struct image_header *hdr;
    int rc;
    int i;

    for (i = 0; i < num_addresses; i++) {
        hdr = out_headers + i;
        rc = boot_read_image_header(hdr, &addresses[i]);
        if (rc != 0 || hdr->ih_magic != IMAGE_MAGIC) {
            memset(hdr, 0xff, sizeof *hdr);
        }
    }
}

void
bootutil_cfg_register(void)
{
    conf_register(&boot_conf_handler);
}

int
boot_read_status(void)
{
    conf_load();

    return boot_st_loaded == 1;
}

/**
 * Writes the supplied boot status to the flash file system.  The boot status
 * contains the current state of an in-progress image copy operation.
 *
 * @param status                The boot status base to write.
 * @param entries               The array of boot status entries to write.
 * @param num_areas             The number of flash areas capable of storing
 *                                  image data.  This is equal to the length of
 *                                  the entries array.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
boot_write_status(void)
{
    char *val_str;
    char *rstr;
    int rc = 0;
    int len;

    len = CONF_STR_FROM_BYTES_LEN(boot_st_sz);
    val_str = malloc(len);
    if (!val_str) {
        return BOOT_ENOMEM;
    }
    rstr = conf_str_from_bytes(boot_st, boot_st_sz, val_str, len);
    if (!rstr) {
        rc = BOOT_EFILE;
    } else {
        if (conf_save_one(&boot_conf_handler, "status", val_str)) {
            rc = BOOT_EFLASH;
        }
    }
    free(val_str);

    return rc;
}

/**
 * Erases the boot status from the flash file system.  The boot status
 * contains the current state of an in-progress image copy operation.  By
 * erasing the boot status, it is implied that there is no copy operation in
 * progress.
 */
void
boot_clear_status(void)
{
    conf_save_one(&boot_conf_handler, "status", NULL);
}
