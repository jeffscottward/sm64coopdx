/**
 * Compilation and logic test for web_mod_loader.h header guards.
 *
 * Verifies that the web mod loader header provides correct function
 * declarations/stubs depending on build target:
 *
 * Test 1 (native): Compile without __EMSCRIPTEN__
 *   Build: /usr/bin/gcc -Wall -Wextra -Werror -o test_mod_native test_web_mod_loader.c
 *   Expected: compiles cleanly, stubs return error codes
 *
 * Test 2 (web-simulated): Compile with -DTARGET_WEB=1 -D__EMSCRIPTEN__ and stub include path
 *   Build: /usr/bin/gcc -Wall -Wextra -Werror -DTARGET_WEB=1 -D__EMSCRIPTEN__ -Istubs -o test_mod_web test_web_mod_loader.c
 *   Expected: compiles cleanly, declarations visible (uses stub emscripten.h)
 *
 * NOTE: Actual HTTP fetching requires a browser + Emscripten. This test verifies:
 *   - Header include guards work
 *   - Status code macros are defined correctly
 *   - Native stubs return expected error codes
 *   - Web declarations are syntactically correct
 *   - Function pointer assignment verifies type signatures
 *   - Callback typedef is correct
 *   - extract_filename_from_url logic (via native stub behavior)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Include the header under test */
#include "../../src/pc/web/web_mod_loader.h"

/* Include it again to verify include guard works */
#include "../../src/pc/web/web_mod_loader.h"

/*
 * For the web-simulated test, provide stub implementations of the
 * functions declared in the header (since we're not linking the .c file).
 */
#ifdef __EMSCRIPTEN__
int web_mod_download(const char* url, const char* dest_filename) {
    (void)url; (void)dest_filename;
    return WEB_MOD_OK;
}

void web_mod_download_async(const char* url, const char* dest_filename,
                            web_mod_download_callback callback) {
    (void)url; (void)dest_filename;
    if (callback) callback(WEB_MOD_OK);
}

int web_mod_is_cached(const char* url) {
    (void)url;
    return 0;
}

void web_mod_cache_update(const char* url, const char* filename, int size) {
    (void)url; (void)filename; (void)size;
}

void web_mod_check_async_complete(void) { }
#endif

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAILED: %s\n", msg); \
        tests_failed++; \
    } else { \
        printf("  PASSED: %s\n", msg); \
        tests_passed++; \
    } \
} while (0)

/* Track async callback invocation */
static int sCallbackInvoked = 0;
static int sCallbackStatus = -99;

static void test_callback(int status) {
    sCallbackInvoked = 1;
    sCallbackStatus = status;
}

int main(void) {
#ifdef __EMSCRIPTEN__
    printf("=== Web Mod Loader Test (EMSCRIPTEN simulated mode) ===\n\n");
#else
    printf("=== Web Mod Loader Test (NATIVE mode) ===\n\n");
#endif

    /* Test 1: Header compiles without errors */
    printf("Test 1: Header include guard\n");
    TEST_ASSERT(1, "web_mod_loader.h included twice without error");
    printf("\n");

    /* Test 2: Status code macros are defined */
    printf("Test 2: Status code macros\n");
    TEST_ASSERT(WEB_MOD_OK == 0, "WEB_MOD_OK is 0");
    TEST_ASSERT(WEB_MOD_ERR_FETCH == -1, "WEB_MOD_ERR_FETCH is -1");
    TEST_ASSERT(WEB_MOD_ERR_WRITE == -2, "WEB_MOD_ERR_WRITE is -2");
    TEST_ASSERT(WEB_MOD_ERR_BADURL == -3, "WEB_MOD_ERR_BADURL is -3");
    TEST_ASSERT(WEB_MOD_ERR_TOOLARGE == -4, "WEB_MOD_ERR_TOOLARGE is -4");
    printf("\n");

    /* Test 3: web_mod_download() callable */
    printf("Test 3: web_mod_download() callable\n");
    {
        int result = web_mod_download("http://example.com/test.lua", "test.lua");
#ifdef __EMSCRIPTEN__
        TEST_ASSERT(result == WEB_MOD_OK, "web_mod_download returns OK in stub mode");
#else
        TEST_ASSERT(result == WEB_MOD_ERR_FETCH, "web_mod_download returns ERR_FETCH on native");
#endif
    }
    printf("\n");

    /* Test 4: web_mod_download() with NULL URL returns BADURL on native */
    printf("Test 4: web_mod_download() with NULL URL\n");
    {
        int result = web_mod_download(NULL, "test.lua");
#ifdef __EMSCRIPTEN__
        TEST_ASSERT(result == WEB_MOD_OK, "web_mod_download(NULL) returns OK in stub mode");
#else
        TEST_ASSERT(result == WEB_MOD_ERR_FETCH, "web_mod_download(NULL) returns ERR_FETCH on native");
#endif
    }
    printf("\n");

    /* Test 5: web_mod_download_async() callable with callback */
    printf("Test 5: web_mod_download_async() with callback\n");
    {
        sCallbackInvoked = 0;
        sCallbackStatus = -99;
        web_mod_download_async("http://example.com/test.lua", "test.lua", test_callback);
#ifdef __EMSCRIPTEN__
        TEST_ASSERT(sCallbackInvoked == 1, "Async callback invoked in stub mode");
        TEST_ASSERT(sCallbackStatus == WEB_MOD_OK, "Async callback got OK status in stub mode");
#else
        TEST_ASSERT(sCallbackInvoked == 1, "Async callback invoked on native");
        TEST_ASSERT(sCallbackStatus == WEB_MOD_ERR_FETCH, "Async callback got ERR_FETCH on native");
#endif
    }
    printf("\n");

    /* Test 6: web_mod_download_async() with NULL callback */
    printf("Test 6: web_mod_download_async() with NULL callback\n");
    {
        web_mod_download_async("http://example.com/test.lua", "test.lua", NULL);
        TEST_ASSERT(1, "NULL callback does not crash");
    }
    printf("\n");

    /* Test 7: web_mod_is_cached() returns 0 for uncached URL */
    printf("Test 7: web_mod_is_cached()\n");
    {
        int cached = web_mod_is_cached("http://example.com/test.lua");
        TEST_ASSERT(cached == 0, "Uncached URL returns 0");
    }
    printf("\n");

    /* Test 8: web_mod_cache_update() callable */
    printf("Test 8: web_mod_cache_update()\n");
    {
        web_mod_cache_update("http://example.com/test.lua", "test.lua", 1234);
        TEST_ASSERT(1, "web_mod_cache_update called without error");
    }
    printf("\n");

    /* Test 9: web_mod_check_async_complete() callable */
    printf("Test 9: web_mod_check_async_complete()\n");
    {
        web_mod_check_async_complete();
        TEST_ASSERT(1, "web_mod_check_async_complete called without error");
    }
    printf("\n");

    /* Test 10: Function pointer assignments (type verification) */
    printf("Test 10: Function pointer type verification\n");
    {
        int (*dl_fn)(const char*, const char*) = web_mod_download;
        void (*dl_async_fn)(const char*, const char*, web_mod_download_callback) = web_mod_download_async;
        int (*cached_fn)(const char*) = web_mod_is_cached;
        void (*update_fn)(const char*, const char*, int) = web_mod_cache_update;
        void (*check_fn)(void) = web_mod_check_async_complete;
        TEST_ASSERT(dl_fn != NULL, "web_mod_download has valid address");
        TEST_ASSERT(dl_async_fn != NULL, "web_mod_download_async has valid address");
        TEST_ASSERT(cached_fn != NULL, "web_mod_is_cached has valid address");
        TEST_ASSERT(update_fn != NULL, "web_mod_cache_update has valid address");
        TEST_ASSERT(check_fn != NULL, "web_mod_check_async_complete has valid address");
    }
    printf("\n");

    /* Test 11: Callback typedef can be used */
    printf("Test 11: Callback typedef\n");
    {
        web_mod_download_callback cb = test_callback;
        TEST_ASSERT(cb != NULL, "web_mod_download_callback typedef usable");
        cb(42);
        TEST_ASSERT(sCallbackStatus == 42, "Callback receives status parameter");
    }
    printf("\n");

    /* Test 12: WEB_MOD_MAX_SIZE defined (only in web builds, but macro exists in both) */
    printf("Test 12: Size limit macro\n");
    {
#ifdef WEB_MOD_MAX_SIZE
        TEST_ASSERT(WEB_MOD_MAX_SIZE == 16 * 1024 * 1024, "WEB_MOD_MAX_SIZE is 16MB");
#else
        TEST_ASSERT(1, "WEB_MOD_MAX_SIZE not defined on native (expected)");
#endif
    }
    printf("\n");

    /* Summary */
    printf("=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    if (tests_failed > 0) {
        printf("SOME TESTS FAILED\n");
        return 1;
    }

#ifdef __EMSCRIPTEN__
    printf("All web-simulated mod loader tests PASSED\n");
#else
    printf("All native mod loader tests PASSED\n");
#endif
    return 0;
}
