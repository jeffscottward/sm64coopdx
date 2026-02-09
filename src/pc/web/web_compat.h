#ifndef WEB_COMPAT_H
#define WEB_COMPAT_H

/**
 * web_compat.h — Compatibility header for sm64coopdx WebAssembly/Emscripten builds.
 *
 * This header provides preprocessor guards, stubs, and configuration for
 * building sm64coopdx with Emscripten targeting WebAssembly. It should be
 * included early in compilation units that need web-specific behavior.
 *
 * Defined when building with Makefile.web: TARGET_WEB=1, __EMSCRIPTEN__
 */

#ifdef TARGET_WEB

/*
 * Disable the update checker on web builds.
 * The update checker depends on libcurl, which is unavailable in Emscripten.
 * This define causes update_checker.c functions to become no-ops.
 */
#ifndef NO_UPDATE_CHECKER
#define NO_UPDATE_CHECKER 1
#endif

/*
 * Disable the threaded loading screen on web builds.
 * The loading screen uses pthreads with mutex locking and a dedicated render
 * thread. While Emscripten supports pthreads (via SharedArrayBuffer), the
 * threaded loading screen requires careful synchronization that is not yet
 * ported. Defining WAPI_DUMMY suppresses LOADING_SCREEN_SUPPORTED in loading.h.
 *
 * NOTE: This is a build-phase workaround. A future phase should implement
 * a web-native loading screen or properly port the threaded version.
 */
#ifndef WEB_LOADING_SCREEN_DISABLED
#define WEB_LOADING_SCREEN_DISABLED 1
#endif

/*
 * Emscripten-specific includes.
 * These are only available when compiling with emcc/em++.
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif /* __EMSCRIPTEN__ */

/*
 * Web filesystem path stubs.
 *
 * On native platforms, sys_exe_path_dir() and sys_exe_path_file() resolve
 * the executable's location on disk. In Emscripten, the "executable" concept
 * doesn't apply — everything runs in a virtual filesystem (MEMFS/IDBFS).
 *
 * These inline stubs return fixed paths within the Emscripten VFS.
 * They are used when platform.c is compiled with TARGET_WEB, providing
 * overrides before the platform-specific implementations are reached.
 */
#define WEB_EXE_PATH_DIR  "/"
#define WEB_EXE_PATH_FILE "/sm64coopdx"
#define WEB_USER_PATH     "/save"
#define WEB_RESOURCE_PATH "/"

#endif /* TARGET_WEB */

#endif /* WEB_COMPAT_H */
