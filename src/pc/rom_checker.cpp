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

// Expected SM64 US ROM size: 8 MB
#define SM64_US_ROM_SIZE 8388608

// ROM format detection from magic bytes:
//   z64 (big-endian):    80 37 12 40
//   v64 (byteswapped):   37 80 40 12
//   n64 (little-endian): 40 12 37 80
enum RomFormat { ROM_Z64, ROM_V64, ROM_N64, ROM_UNKNOWN };

static enum RomFormat detect_rom_format(const u8 magic[4]) {
    if (magic[0] == 0x80 && magic[1] == 0x37) return ROM_Z64;
    if (magic[0] == 0x37 && magic[1] == 0x80) return ROM_V64;
    if (magic[0] == 0x40 && magic[1] == 0x12) return ROM_N64;
    return ROM_UNKNOWN;
}

// Convert ROM data in-place to z64 (big-endian) format.
static void convert_rom_to_z64(u8 *data, long size, enum RomFormat fmt) {
    if (fmt == ROM_V64) {
        // v64: swap every pair of adjacent bytes
        for (long i = 0; i < size - 1; i += 2) {
            u8 tmp = data[i];
            data[i] = data[i + 1];
            data[i + 1] = tmp;
        }
    } else if (fmt == ROM_N64) {
        // n64: reverse every group of 4 bytes
        for (long i = 0; i < size - 3; i += 4) {
            u8 tmp0 = data[i], tmp1 = data[i + 1];
            data[i] = data[i + 3];
            data[i + 1] = data[i + 2];
            data[i + 2] = tmp1;
            data[i + 3] = tmp0;
        }
    }
}

static bool is_rom_valid(const std::string romPath) {
    // Web builds: validate by file size and magic bytes instead of strict MD5.
    // Accepts z64, v64, and n64 ROM formats — converts to z64 (big-endian) if
    // needed since rom_assets_load() reads data with BSWAP macros expecting BE.
    FILE *f = fopen(romPath.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);

    u8 magic[4] = { 0 };
    fseek(f, 0, SEEK_SET);
    fread(magic, 1, 4, f);
    fclose(f);

    if (size != SM64_US_ROM_SIZE) {
        printf("[Web] ROM rejected: size %ld != expected %d\n", size, SM64_US_ROM_SIZE);
        return false;
    }

    enum RomFormat fmt = detect_rom_format(magic);
    if (fmt == ROM_UNKNOWN) {
        printf("[Web] ROM rejected: unrecognized magic %02x%02x%02x%02x\n",
               magic[0], magic[1], magic[2], magic[3]);
        return false;
    }

    std::string destPath = fs_get_write_path("") + std::string("baserom.us.z64");

    if (fmt != ROM_Z64) {
        // ROM needs byte-order conversion — read, convert, write as z64
        printf("[Web] ROM is %s format, converting to z64...\n",
               fmt == ROM_V64 ? "v64" : "n64");
        u8 *romData = (u8 *)malloc(size);
        if (!romData) return false;

        FILE *src = fopen(romPath.c_str(), "rb");
        if (!src) { free(romData); return false; }
        fread(romData, 1, size, src);
        fclose(src);

        convert_rom_to_z64(romData, size, fmt);

        // Write converted ROM
        FILE *dst = fopen(destPath.c_str(), "wb");
        if (dst) {
            fwrite(romData, 1, size, dst);
            fclose(dst);
        }
        free(romData);

        // Also overwrite the source file so future loads use z64 directly
        if (romPath != destPath) {
            // Already written to destPath above
        }
    } else if (romPath != destPath && !web_file_exists(destPath.c_str())) {
        // z64 format — just copy if needed
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
    printf("[Web] ROM accepted: %s (%ld bytes, format=%s)\n", romPath.c_str(), size,
           fmt == ROM_Z64 ? "z64" : fmt == ROM_V64 ? "v64->z64" : "n64->z64");
    return true;
}

// Web build: check known ROM filenames directly instead of directory iteration.
// std::filesystem::directory_iterator may not work reliably on Emscripten's VFS.
inline static bool scan_path_for_rom(const char *dir) {
    for (VanillaMD5 *md5 = sVanillaMD5; md5->localizationName != NULL; md5++) {
        std::string path = std::string(dir) + "baserom." + md5->localizationName + ".z64";
        if (web_file_exists(path.c_str())) {
            if (is_rom_valid(path)) {
                return true;
            }
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
