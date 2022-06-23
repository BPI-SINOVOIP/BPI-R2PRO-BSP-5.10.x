/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "bootloader.h"
#include "common.h"
//#include "cutils/properties.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"
#include "encryptedfs_provisioning.h"
#include "rktools.h"
#include "sdboot.h"
#include "mtdutils/mtdutils.h"

static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 's' },
  { "update_package", required_argument, NULL, 'u' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_all", no_argument, NULL, 'a' },
  { "set_encrypted_filesystems", required_argument, NULL, 'e' },
  { "show_text", no_argument, NULL, 't' },
  { "factory_pcba_test", no_argument, NULL, 'f' },
  { NULL, 0, NULL, 0 },
};

static const char *COMMAND_FILE = "/userdata/recovery/command";
static const char *INTENT_FILE = "/userdata/recovery/intent";
static const char *LOG_FILE = "/userdata/recovery/log";
static const char *LAST_LOG_FILE = "/userdata/recovery/last_log";
static const char *SDCARD_ROOT = "/sdcard";
static const char *SDCARD_ROOT2 = "/mnt/external_sd";
static const char *USERDATA_ROOT = "/userdata";
static const char *UDISK_ROOT = "/udisk";
static const char *UDISK_ROOT2 = "/mnt/usb_storage";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *SIDELOAD_TEMP_DIR = "/tmp/sideload";
static const char *coldboot_done = "/dev/.coldboot_done";
char systemFlag[252];
bool bSDBootUpdate = false;

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /userdata
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_volume() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 *
 * SECURE FILE SYSTEMS ENABLE/DISABLE
 * 1. user selects "enable encrypted file systems"
 * 2. main system writes "--set_encrypted_filesystems=on|off" to
 *    /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and
 *    "--set_encrypted_filesystems=on|off"
 *    -- after this, rebooting will restart the transition --
 * 5. read_encrypted_fs_info() retrieves encrypted file systems settings from /userdata
 *    Settings include: property to specify the Encrypted FS istatus and
 *    FS encryption key if enabled (not yet implemented)
 * 6. erase_volume() reformats /userdata
 * 7. erase_volume() reformats /cache
 * 8. restore_encrypted_fs_info() writes required encrypted file systems settings to /userdata
 *    Settings include: property to specify the Encrypted FS status and
 *    FS encryption key if enabled (not yet implemented)
 * 9. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 10. main() calls reboot() to boot main system
 */

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;
extern size_t strlcpy(char *dst, const char *src, size_t dsize);
extern size_t strlcat(char *dst, const char *src, size_t dsize);
extern int do_rk_updateEngine(const char *binary, const char *path);


int read_encrypted_fs_info(encrypted_fs_info *encrypted_fs_data) {
    return ENCRYPTED_FS_ERROR;
}

int restore_encrypted_fs_info(encrypted_fs_info *encrypted_fs_data) {
    return ENCRYPTED_FS_ERROR;
}
// open a given path, mounting partitions as necessary
static FILE*
fopen_path(const char *path, const char *mode) {
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return NULL;
    }

    // When writing, try to create the containing directory, if necessary.
    // Use generous permissions, the system (init.rc) will reset them.
    if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1);

    FILE *fp = fopen(path, mode);
    return fp;
}

// close a file, log an error if the error indicator is set
static void
check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    set_bootloader_message(&boot);
}

static void
set_sdcard_update_bootloader_message() {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
}

// How much of the temp log we have copied to the copy in cache.
static long tmplog_offset = 0;

static void
copy_log_file(const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(TEMPORARY_LOG_FILE, "r");
        if (tmplog == NULL) {
            LOGE("Can't open %s\n", TEMPORARY_LOG_FILE);
        } else {
            if (append) {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            if (append) {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, TEMPORARY_LOG_FILE);
        }
        check_and_fclose(log, destination);
    }
}


// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    printf("finish_recovery Enter.....\n");

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(LOG_FILE, true);
    copy_log_file(LAST_LOG_FILE, false);
    chmod(LAST_LOG_FILE, 0640);

    // Reset to mormal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.systemFlag, systemFlag, sizeof(boot.systemFlag));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (ensure_path_mounted(COMMAND_FILE) != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

    sync();  // For good measure.
}

static int
erase_volume(const char *volume) {
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    ui_print("Formatting %s...\n", volume);

    if (strcmp(volume, "/userdata") == 0) {
        // Any part of the log we'd copied to data is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
    }

    return format_volume(volume);
}

static char*
copy_sideloaded_package(const char* original_path) {
  if (ensure_path_mounted(original_path) != 0) {
    LOGE("Can't mount %s\n", original_path);
    return NULL;
  }

  if (ensure_path_mounted(SIDELOAD_TEMP_DIR) != 0) {
    LOGE("Can't mount %s\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }

  if (mkdir(SIDELOAD_TEMP_DIR, 0700) != 0) {
    if (errno != EEXIST) {
      LOGE("Can't mkdir %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
      return NULL;
    }
  }

  // verify that SIDELOAD_TEMP_DIR is exactly what we expect: a
  // directory, owned by root, readable and writable only by root.
  struct stat st;
  if (stat(SIDELOAD_TEMP_DIR, &st) != 0) {
    LOGE("failed to stat %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
    return NULL;
  }
  if (!S_ISDIR(st.st_mode)) {
    LOGE("%s isn't a directory\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }
  if ((st.st_mode & 0777) != 0700) {
    LOGE("%s has perms %o\n", SIDELOAD_TEMP_DIR, st.st_mode);
    return NULL;
  }
  if (st.st_uid != 0) {
    LOGE("%s owned by %lu; not root\n", SIDELOAD_TEMP_DIR, st.st_uid);
    return NULL;
  }

  char copy_path[PATH_MAX];
  strcpy(copy_path, SIDELOAD_TEMP_DIR);
  strcat(copy_path, "/package.zip");

  char* buffer = malloc(BUFSIZ);
  if (buffer == NULL) {
    LOGE("Failed to allocate buffer\n");
    return NULL;
  }

  size_t read;
  FILE* fin = fopen(original_path, "rb");
  if (fin == NULL) {
    LOGE("Failed to open %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }
  FILE* fout = fopen(copy_path, "wb");
  if (fout == NULL) {
    LOGE("Failed to open %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  while ((read = fread(buffer, 1, BUFSIZ, fin)) > 0) {
    if (fwrite(buffer, 1, read, fout) != read) {
      LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
      return NULL;
    }
  }

  free(buffer);

  if (fclose(fout) != 0) {
    LOGE("Failed to close %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  if (fclose(fin) != 0) {
    LOGE("Failed to close %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }

  // "adb push" is happy to overwrite read-only files when it's
  // running as root, but we'll try anyway.
  if (chmod(copy_path, 0400) != 0) {
    LOGE("Failed to chmod %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  return strdup(copy_path);
}

static char**
prepend_title(const char** headers) {
    char* title[] = { "Linux system recovery <"
                          EXPAND(RECOVERY_API_VERSION) "e>",
                      "",
                      NULL };

    // count the number of lines in our title, plus the
    // caller-provided headers.
    int count = 0;
    char** p;
    for (p = title; *p; ++p, ++count);
    for (p = (char** )headers; *p; ++p, ++count);

    char** new_headers = malloc((count+1) * sizeof(char*));
    char** h = new_headers;
    for (p = title; *p; ++p, ++h) *h = *p;
    for (p = (char** )headers; *p; ++p, ++h) *h = *p;
    *h = NULL;

    return new_headers;
}

static int
get_menu_selection(char** headers, char** items, int menu_only,
                   int initial_selection) {
    // throw away keys pressed previously, so user doesn't
    // accidentally trigger menu items.
    ui_clear_key_queue();

    ui_start_menu(headers, items, initial_selection);
    int selected = initial_selection;
    int chosen_item = -1;

    while (chosen_item < 0) {
        int key = ui_wait_key();
        int visible = ui_text_visible();

        int action = device_handle_key(key, visible);

        if (action < 0) {
            switch (action) {
                case HIGHLIGHT_UP:
                    --selected;
                    selected = ui_menu_select(selected);
                    break;
                case HIGHLIGHT_DOWN:
                    ++selected;
                    selected = ui_menu_select(selected);
                    break;
                case SELECT_ITEM:
                    chosen_item = selected;
                    break;
                case NO_ACTION:
                    break;
            }
        } else if (!menu_only) {
            chosen_item = action;
        }
    }

    ui_end_menu();
    return chosen_item;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static int
sdcard_directory(const char* path) {
    ensure_path_mounted(SDCARD_ROOT);

    const char* MENU_HEADERS[] = { "Choose a package to install:",
                                   path,
                                   "",
                                   NULL };
    DIR* d;
    struct dirent* de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        ensure_path_unmounted(SDCARD_ROOT);
        return 0;
    }

    char** headers = prepend_title(MENU_HEADERS);

    int d_size = 0;
    int d_alloc = 10;
    char** dirs = malloc(d_alloc * sizeof(char*));
    int z_size = 1;
    int z_alloc = 10;
    char** zips = malloc(z_alloc * sizeof(char*));
    zips[0] = strdup("../");

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') continue;
            if (name_len == 2 && de->d_name[0] == '.' &&
                de->d_name[1] == '.') continue;

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 4 &&
                   strncasecmp(de->d_name + (name_len-4), ".zip", 4) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char*), compare_string);
    qsort(zips, z_size, sizeof(char*), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = realloc(zips, z_alloc * sizeof(char*));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char*));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is sdcard_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = sdcard_directory(new_path);
            if (result >= 0) break;
        } else {
            // selected a zip file:  attempt to install it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);

            ui_print("\n-- Install %s ...\n", path);
            set_sdcard_update_bootloader_message();
            char* copy = copy_sideloaded_package(new_path);
            ensure_path_unmounted(SDCARD_ROOT);
            if (copy) {
                //result = install_package(copy);
                free(copy);
            } else {
                result = INSTALL_ERROR;
            }
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < z_size; ++i) free(zips[i]);
    free(zips);
    free(headers);

    ensure_path_unmounted(SDCARD_ROOT);
    return result;
}

static void
wipe_data(int confirm) {
    if (confirm) {
        static char** title_headers = NULL;

        if (title_headers == NULL) {
            char* headers[] = { "Confirm wipe of all user data?",
                                "  THIS CAN NOT BE UNDONE.",
                                "",
                                NULL };
            title_headers = prepend_title((const char**)headers);
        }

        char* items[] = { " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " Yes -- delete all user data",   // [7]
                          " No",
                          " No",
                          " No",
                          NULL };

        int chosen_item = get_menu_selection(title_headers, items, 1, 0);
        if (chosen_item != 7) {
            return;
        }
    }

    ui_print("\n-- Wiping data...\n");
    device_wipe_data();
    erase_volume("/userdata");
    //erase_volume("/cache");
    ui_print("Data wipe complete.\n");
}

static int ui_update(const char * fw_package) {
    const char* binary = "/usr/bin/rkupdate";
    int i, ret = 0;
    int status = INSTALL_SUCCESS;

    for(i = 0; i < 5; i++) {
    ret = ensure_path_mounted(fw_package);
    if(ret == 0)
        break;
        sleep(1);
        LOGD("mounted %s failed.\n", fw_package);
    }

    if(ret == 0 && access(fw_package, F_OK) == 0) {
        LOGD(">>>rkflash will update from %s\n", fw_package);
#ifdef USE_RKUPDATE
        status = do_rk_update(binary, fw_package);
#endif

#ifdef USE_UPDATEENGINE
        const char* updateEnginebin = "/usr/bin/updateEngine";
        status = do_rk_updateEngine(updateEnginebin, fw_package);
#endif
        if(status == INSTALL_SUCCESS){
            strcpy(systemFlag, fw_package);

            if(strncmp(fw_package,"/userdata", 9) != 0) {
                if (resize_volume("/userdata"))
                    LOGE("\n ---resize_volume userdata error ---\n");
            } else {
                //update success, delete userdata/update.img and write result to file.
                if(access(fw_package, F_OK) == 0)
                    remove(fw_package);
            }
        }
    } else {
        status = INSTALL_ERROR;
    }

    if (status != INSTALL_SUCCESS) {
        ui_print("Installation aborted.\n");
        while (1)
        {
            /* code */
        }

        return -1;
    }
    ui_print("update.img Installation done.\n");
    ui_show_text(0);

    return 0;
}

static void
prompt_and_wait() {
    char** headers = prepend_title((const char**)MENU_HEADERS);

    for (;;) {
        finish_recovery(NULL);
        ui_reset_progress();

        int chosen_item = get_menu_selection(headers, MENU_ITEMS, 0, 0);

        // device-specific code may take some action here.  It may
        // return one of the core actions handled in the switch
        // statement below.
        chosen_item = device_perform_action(chosen_item);

        switch (chosen_item) {
            case ITEM_REBOOT:
                return;

            case ITEM_WIPE_DATA:
                wipe_data(ui_text_visible());
                if (!ui_text_visible()) return;
                break;
#if 0
            case ITEM_WIPE_CACHE:
                ui_print("\n-- Wiping cache...\n");
                erase_volume("/cache");
                ui_print("Cache wipe complete.\n");
                if (!ui_text_visible()) return;
                break;
#endif
            case ITEM_APPLY_SDCARD:
                {
                    int status = sdcard_directory(SDCARD_ROOT);
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui_set_background(BACKGROUND_ICON_ERROR);
                            ui_print("Installation aborted.\n");
                        } else if (!ui_text_visible()) {
                            return;  // reboot if logs aren't visible
                        } else {
                            ui_print("\nInstall from sdcard complete.\n");
                        }
                    }
                }
                break;

            case ITEM_APPLY_USERDATA:
            {
                //update firmware from local userdata;
                const char* fw_package = "/userdata/update.img";
                // LOGD("%s:%d:-------->>>>> USERDATA update\n",__func__, __LINE__);
                int status = ui_update(fw_package);
                if (status < 0) {
                    ui_set_background(BACKGROUND_ICON_ERROR);
                } else if (!ui_text_visible()) {
                    return;  // reboot if logs aren't visible
                } else {
                    ui_print("\nInstall from sdcard complete.\n");
                }
            }
                break;

            case ITEM_APPLY_UDISK:
            {
                //update firmware from udisk;
                const char* fw_package = "/udisk/update.img";
                // LOGD("%s:%d:-------->>>> UDISK update\n",__func__, __LINE__);
                int status = ui_update(fw_package);
                if (status < 0) {
                    ui_set_background(BACKGROUND_ICON_ERROR);
                } else if (!ui_text_visible()) {
                    ui_print("\nInstall from sdcard complete.\n");
                    return;  // reboot if logs aren't visible
                } else {
                    ui_print("\nInstall from sdcard complete.\n");
                }
            }
               break;
        }
    }
}

static void
print_property(const char *key, const char *name, void *cookie) {
    printf("%s=%s\n", key, name);
}


int
main(int argc, char **argv) {
    bool bSDBoot    = false;
    const char *sdupdate_package = NULL;

    while(access(coldboot_done, F_OK) != 0){
        printf("coldboot not done, wait...\n");
        sleep(1);
    }

    time_t start = time(NULL);
    if(access("/.rkdebug", F_OK) != 0){
        // If these fail, there's not really anywhere to complain...
        freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
        freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);
    } else {
        printf("\n");
        printf("*********************************************************\n");
        printf("            ROCKCHIP recovery system                     \n");
        printf("*********************************************************\n");
        printf("**** version : v1.0.1 ****\n");
    }
    printf("Starting recovery on %s\n", ctime(&start));

#ifndef RecoveryNoUi
    printf("Recovery System have UI defined.\n");
#endif

    ui_init();
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    load_volume_table();
    setFlashPoint();

    bSDBoot = is_boot_from_SD();
    if(!bSDBoot) {
        get_args(&argc, &argv);
    } else {
        if (is_sdcard_update()) {
            char imageFile[64] = {0};
            strlcpy(imageFile, EX_SDCARD_ROOT, sizeof(imageFile));
            strlcat(imageFile, "/sdupdate.img", sizeof(imageFile));
            if (access(imageFile, F_OK) == 0) {
                sdupdate_package = strdup(imageFile);
                bSDBootUpdate = true;
                ui_show_text(1);
                printf("sdupdate_package = %s \n",sdupdate_package);
            }
        }
    }

    int previous_runs = 0;
    const char *send_intent = NULL;
    const char *update_package = NULL;
    const char *encrypted_fs_mode = NULL;
    int wipe_data = 0;
    int wipe_all = 0;
    int pcba_test = 0;  //chad.ma add for pcba test
    int toggle_secure_fs = 0;
    encrypted_fs_info encrypted_fs_data;

    strcpy(systemFlag, "false");
    int arg;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
        switch (arg) {
        case 'p': previous_runs = atoi(optarg); break;
        case 's': send_intent = optarg; break;
        case 'u': update_package = optarg; break;
        case 'w': wipe_data = 1; break;
        case 'a': wipe_all = 1; break;
        case 'e': encrypted_fs_mode = optarg; toggle_secure_fs = 1; break;
        case 't': ui_show_text(1); break;
        case 'f': pcba_test = 1; break;  //chad.ma add for pcba test
        case '?':
            LOGE("Invalid command argument\n");
            continue;
        }
    }

    device_recovery_start();

    printf("Command:");
    for (arg = 0; arg < argc; arg++) {
        printf(" \"%s\"", argv[arg]);
    }
    printf("\n");

    if (update_package) {
        // For backwards compatibility on the cache partition only, if
        // we're given an old 'root' path "CACHE:foo", change it to
        // "/cache/foo".
        if (strncmp(update_package, "CACHE:", 6) == 0) {
            int len = strlen(update_package) + 10;
            char* modified_path = malloc(len);
            strlcpy(modified_path, "/cache/", len);
            strlcat(modified_path, update_package+6, len);
            printf("(replacing path \"%s\" with \"%s\")\n",
                   update_package, modified_path);
            update_package = modified_path;
        }
    }
    printf("\n");

    int status = INSTALL_SUCCESS;

    if (toggle_secure_fs) {
        if (strcmp(encrypted_fs_mode,"on") == 0) {
            encrypted_fs_data.mode = MODE_ENCRYPTED_FS_ENABLED;
            ui_print("Enabling Encrypted FS.\n");
        } else if (strcmp(encrypted_fs_mode,"off") == 0) {
            encrypted_fs_data.mode = MODE_ENCRYPTED_FS_DISABLED;
            ui_print("Disabling Encrypted FS.\n");
        } else {
            ui_print("Error: invalid Encrypted FS setting.\n");
            status = INSTALL_ERROR;
        }

        // Recovery strategy: if the data partition is damaged, disable encrypted file systems.
        // This preventsthe device recycling endlessly in recovery mode.
        if ((encrypted_fs_data.mode == MODE_ENCRYPTED_FS_ENABLED) &&
                (read_encrypted_fs_info(&encrypted_fs_data))) {
            ui_print("Encrypted FS change aborted, resetting to disabled state.\n");
            encrypted_fs_data.mode = MODE_ENCRYPTED_FS_DISABLED;
        }

        if (status != INSTALL_ERROR) {
            if (erase_volume("/userdata")) {
                ui_print("Data wipe failed.\n");
                status = INSTALL_ERROR;
#if 0
            } else if (erase_volume("/cache")) {
                ui_print("Cache wipe failed.\n");
                status = INSTALL_ERROR;
#endif
            } else if ((encrypted_fs_data.mode == MODE_ENCRYPTED_FS_ENABLED) &&
                      (restore_encrypted_fs_info(&encrypted_fs_data))) {
                ui_print("Encrypted FS change aborted.\n");
                status = INSTALL_ERROR;
            } else {
                ui_print("Successfully updated Encrypted FS.\n");
                status = INSTALL_SUCCESS;
            }
        }
    } else if (update_package != NULL) {
        int fd = -1;
        char* resutl_file = "/userdata/update_rst.txt";
        char text[128] ;
        memset(text, 0, sizeof(text));

        fd = open(resutl_file, O_CREAT | O_WRONLY | O_TRUNC, 0777);
        if (fd < 0) {
            LOGE("open /userdata/update_rst.txt fail, errno = %d\n", errno);
        }

        if (ensure_path_unmounted("/oem") != 0)
            LOGE("\n === umount oem fail === \n");

        if (ensure_path_unmounted("/userdata") != 0)
            LOGE("\n === umount userdata fail === \n");

        const char* binary = "/usr/bin/rkupdate";
        int i, ret = 0;
        for (i = 0; i < 5; i++) {
            ret = ensure_path_mounted(update_package);
            if(ret == 0)
                break;
            sleep(1);
            printf("mounted %s failed.\n", update_package);
        }
        if (ret == 0) {
            printf(">>>rkflash will update from %s\n", update_package);
            #ifdef USE_RKUPDATE
            status = do_rk_update(binary, update_package);
            #endif
            #ifdef USE_UPDATEENGINE
            const char* updateEnginebin = "/usr/bin/updateEngine";
            status = do_rk_updateEngine(updateEnginebin, update_package);
            #endif
            if (status == INSTALL_SUCCESS) {
                strcpy(systemFlag, update_package);

                if (strncmp(update_package,"/userdata", 9) != 0) {
                    if (resize_volume("/userdata"))
                        LOGE("\n ---resize_volume userdata error ---\n");
                } else {
                    //update success, delete userdata/update.img and write result to file.
                    if(access(update_package, F_OK) == 0)
                        remove(update_package);
                }
                strlcpy(text, "update images success!", 127);
            } else {
                strlcpy(text, "update images failed!", 127);
            }
        } else {
            strlcpy(text, "update images failed!", 127);
        }

        int w_len = write(fd, text, strlen(text));
        if (w_len <= 0) {
            LOGE("Write %s fail, errno = %d\n", resutl_file, errno);
        }
        close(fd);

        if (status != INSTALL_SUCCESS) ui_print("Installation aborted.\n");
        ui_print("update.img Installation done.\n");
        //ui_show_text(0);
    }else if (sdupdate_package != NULL) {
        // update image from sdcard
#ifdef USE_RKUPDATE
        const char* binary = "/usr/bin/rkupdate";
        printf(">>>sdboot update will update from %s\n", sdupdate_package);
        status = do_rk_update(binary, sdupdate_package);
#endif

        #ifdef USE_UPDATEENGINE
#define	FACTORY_FIRMWARE_IMAGE "/mnt/sdcard/out_image.img"
#define	CMD4RECOVERY_FILENAME "/mnt/sdcard/cmd4recovery"
        if ((access(FACTORY_FIRMWARE_IMAGE, F_OK)) && access(CMD4RECOVERY_FILENAME, F_OK)) {
            int tmp_fd = creat(CMD4RECOVERY_FILENAME, 0777);
            if (tmp_fd < 0) {
                printf("creat % error.\n", CMD4RECOVERY_FILENAME);
                status = INSTALL_ERROR;
            } else {
                close(tmp_fd);
                const char* updateEnginebin = "/usr/bin/updateEngine";
                status = do_rk_updateEngine(updateEnginebin, sdupdate_package);
            }
        }

        if (isMtdDevice()) {
            printf("start flash write to /dev/mtd0.\n");
            size_t total_size;
            size_t erase_size;
            mtd_scan_partitions();
            const MtdPartition *part = mtd_find_partition_by_name("rk-nand");
            if ( part == NULL ) {
                part = mtd_find_partition_by_name("spi-nand0");
            }
            if (part == NULL || mtd_partition_info(part, &total_size, &erase_size, NULL)) {
                if ((!access(FACTORY_FIRMWARE_IMAGE, F_OK)) && mtd_find_partition_by_name("sfc_nor") != NULL) {
                    printf("Info: start flash out_image.img to spi nor.\n");
                    system("flashcp -v " FACTORY_FIRMWARE_IMAGE " /dev/mtd0");
                } else
                printf("Error: Can't find rk-nand or spi-nand0.\n");
            } else {
                system("flash_erase /dev/mtd0 0x0 0");
                system("sh "CMD4RECOVERY_FILENAME);
            }
        } else {
            printf("Start to dd data to emmc partition.\n");
            system("sh "CMD4RECOVERY_FILENAME);
        }

#endif

        if (status == INSTALL_SUCCESS) {
            printf("update.img Installation success.\n");
            ui_print("update.img Installation success.\n");
            //ui_show_text(0);
        }

    } else if (wipe_data) {
        if (device_wipe_data()) status = INSTALL_ERROR;
        if (erase_volume("/userdata")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) ui_print("Data wipe failed.\n");
    } else if (wipe_all) {
        if (device_wipe_data()) status = INSTALL_ERROR;
        if (erase_volume("/userdata")) status = INSTALL_ERROR;
        if (status != INSTALL_SUCCESS) {
            ui_print("Data wipe failed.\n");
            printf("userdata wipe failed.\n");
        } else {
            ui_print("Data wipe done.\n");
            printf("userdata wipe done.\n");
        }

        if (access("/dev/block/by-name/oem", F_OK) == 0) {
            if (resize_volume("/oem")) status = INSTALL_ERROR;
            if (status != INSTALL_SUCCESS) ui_print("resize failed.\n");
        }

        ui_print("resize oem done.\n");
        //ui_show_text(0);
    }
    else if (pcba_test)
    {
         //pcba test todo...
         printf("------------------ pcba test start -------------\n");
         exit(EXIT_SUCCESS); //exit recovery bin directly, not start pcba here, in rkLanuch.sh
         return 0;
    }
    else {
        status = INSTALL_ERROR;  // No command specified
    }

    if (status != INSTALL_SUCCESS) ui_set_background(BACKGROUND_ICON_ERROR);
    if (status != INSTALL_SUCCESS) {
        printf("\n Install fail! \n");
        if (!bSDBootUpdate && ui_text_visible())
            prompt_and_wait();
    }

    if (sdupdate_package != NULL && bSDBootUpdate) {
        if (status == INSTALL_SUCCESS){
            char *SDDdevice =
                     strdup(get_mounted_device_from_path(EX_SDCARD_ROOT));

            ensure_ex_path_unmounted(EX_SDCARD_ROOT);
            /* Updating is finished here, we must print this message
             * in console, it shows user a specific message that
             * updating is completely, remove SD CARD and reboot */
            fflush(stdout);
            freopen("/dev/console", "w", stdout);
            printf("\nPlease remove SD CARD!!!, wait for reboot.\n");
            ui_print("Please remove SD CARD!!!, wait for reboot.");

            while (access(SDDdevice, F_OK) == 0) { sleep(1); }
            free(SDDdevice);
        }
    }

    // Otherwise, get ready to boot the main system...
    finish_recovery(send_intent);
    ui_print("Rebooting...\n");
    printf("Reboot...\n");
    ui_show_text(0);
    fflush(stdout);
    sync();
    reboot(RB_AUTOBOOT);
    return EXIT_SUCCESS;
}
