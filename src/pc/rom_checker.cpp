#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>

#ifndef TARGET_WEB
#include <filesystem>
#endif

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

#ifdef TARGET_WEB
#include <cstdio>
#include <cstring>
#endif

extern "C" {
#include "platform.h"
#include "mods/mods_utils.h" // for path_ends_with
#include "mods/mod_cache.h"  // for md5 hashing
#include "mods/mods.h"
#include "loading.h"
#include "fs/fs.h"
}

#ifndef TARGET_WEB
namespace fs = std::filesystem;
#endif

bool gRomIsValid = false;
char gRomFilename[SYS_MAX_PATH] = "";

struct VanillaMD5 {
    const char *localizationName;
    const char *md5;
};

// lookup table for vanilla sm64 roms
static struct VanillaMD5 sVanillaMD5[] = {
    // { "eu", "45676429ef6b90e65b517129b700308e" },
    // { "jp", "85d61f5525af708c9f1e84dce6dc10e9" },
    // { "sh", "2d727c3278aa232d94f2fb45aec4d303" },
    { "us", "20b854b239203baf6c961b850a4a51a2" },
    { NULL, NULL },
};

#ifdef TARGET_WEB

// Web build: simplified ROM validation without std::filesystem
// The ROM is loaded into Emscripten's virtual filesystem via browser file picker.

static bool web_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static bool is_rom_valid(const std::string romPath) {
    u8 dataHash[16] = { 0 };
    mod_cache_md5(romPath.c_str(), dataHash);

    std::stringstream ss;
    for (int i = 0; i < 16; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(dataHash[i]);
    }

    for (VanillaMD5 *md5 = sVanillaMD5; md5->localizationName != NULL; md5++) {
        if (md5->md5 == ss.str()) {
            std::string destPath = fs_get_write_path("") + std::string("baserom.") + md5->localizationName + ".z64";

            // Copy the rom to the user path using C file I/O (no std::filesystem)
            if (romPath != destPath && !web_file_exists(destPath.c_str())) {
                FILE *src = fopen(romPath.c_str(), "rb");
                if (src) {
                    FILE *dst = fopen(destPath.c_str(), "wb");
                    if (dst) {
                        char buf[4096];
                        size_t n;
                        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                            fwrite(buf, 1, n, dst);
                        }
                        fclose(dst);
                    }
                    fclose(src);
                }
            }

            snprintf(gRomFilename, SYS_MAX_PATH, "%s", destPath.c_str());
            gRomIsValid = true;
            return true;
        }
    }

    return false;
}

// Web build: check known ROM filenames directly instead of directory iteration.
// std::filesystem::directory_iterator may not work reliably on Emscripten's VFS.
inline static bool scan_path_for_rom(const char *dir) {
    for (VanillaMD5 *md5 = sVanillaMD5; md5->localizationName != NULL; md5++) {
        std::string path = std::string(dir) + "baserom." + md5->localizationName + ".z64";
        if (web_file_exists(path.c_str())) {
            if (is_rom_valid(path)) { return true; }
        }
    }
    return false;
}

#else /* !TARGET_WEB */

inline static void rename_tmp_folder() {
    std::string userPath = fs_get_write_path("");
    std::string oldPath = userPath + "tmp";
    std::string newPath = userPath + TMP_DIRECTORY;
    if (fs::exists(oldPath) && !fs::exists(newPath)) {
#if defined(_WIN32) || defined(_WIN64)
        SetFileAttributesA(oldPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
        fs::rename(oldPath, newPath);
    }
}

static bool is_rom_valid(const std::string romPath) {
    u8 dataHash[16] = { 0 };
    mod_cache_md5(romPath.c_str(), dataHash);

    std::stringstream ss;
    for (int i = 0; i < 16; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(dataHash[i]);
    }

    for (VanillaMD5 *md5 = sVanillaMD5; md5->localizationName != NULL; md5++) {
        if (md5->md5 == ss.str()) {
            std::string destPath = fs_get_write_path("") + std::string("baserom.") + md5->localizationName + ".z64";

            // Copy the rom to the user path
            if (romPath != destPath && !std::filesystem::exists(std::filesystem::path(destPath))) {
                std::filesystem::copy_file(
                    std::filesystem::path(romPath),
                    std::filesystem::path(destPath)
                );
            }

            snprintf(gRomFilename, SYS_MAX_PATH, "%s", destPath.c_str()); // Load the copied rom
            gRomIsValid = true;
            return true;
        }
    }

    return false;
}

inline static bool scan_path_for_rom(const char *dir) {
    for (const auto &entry: std::filesystem::directory_iterator(dir)) {
        std::string path = entry.path().generic_string();
        if (path_ends_with(path.c_str(), ".z64")) {
            if (is_rom_valid(path)) { return true; }
        }
    }
    return false;
}

#endif /* TARGET_WEB */

extern "C" {
void legacy_folder_handler(void) {
#ifndef TARGET_WEB
    rename_tmp_folder();
#endif
}

bool main_rom_handler(void) {
    if (scan_path_for_rom(fs_get_write_path(""))) { return true; }
    scan_path_for_rom(sys_exe_path_dir());
    return gRomIsValid;
}

#ifdef LOADING_SCREEN_SUPPORTED
void rom_on_drop_file(const char *path) {
    static bool hasDroppedInvalidFile = false;
    if (strlen(path) > 0 && !is_rom_valid(path) && !hasDroppedInvalidFile) {
        hasDroppedInvalidFile = true;
        strcat(gCurrLoadingSegment.str, "\n\\#ffc000\\The file you last dropped was not a valid, vanilla SM64 rom.");
    }
}
#endif
}
