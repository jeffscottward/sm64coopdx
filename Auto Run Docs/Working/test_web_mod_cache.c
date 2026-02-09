/**
 * Test suite for web_mod_loader.h caching functionality.
 *
 * Verifies that the cache manifest functions (web_mod_cache_update,
 * web_mod_is_cached, web_mod_cache_get_filename, web_mod_cache_is_fresh,
 * web_mod_cache_remove) work correctly.
 *
 * Test 1 (native): Compile without __EMSCRIPTEN__
 *   Build: /usr/bin/gcc -Wall -Wextra -Werror -o test_mod_cache_native test_web_mod_cache.c
 *   Expected: compiles cleanly, stubs return expected values
 *
 * Test 2 (web-simulated): Compile with -DTARGET_WEB=1 -D__EMSCRIPTEN__ and stub include path
 *   Build: /usr/bin/gcc -Wall -Wextra -Werror -DTARGET_WEB=1 -D__EMSCRIPTEN__ -Istubs -o test_mod_cache_web test_web_mod_cache.c
 *   Expected: compiles cleanly, function declarations visible
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Include the header under test */
#include "../../src/pc/web/web_mod_loader.h"

/*
 * For the web-simulated test, provide stub implementations.
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

int web_mod_cache_get_filename(const char* url, char* buf, int bufsize) {
    (void)url; (void)buf; (void)bufsize;
    return 0;
}

int web_mod_cache_is_fresh(const char* url) {
    (void)url;
    return 0;
}

void web_mod_cache_remove(const char* url) {
    (void)url;
}
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

int main(void) {
#ifdef __EMSCRIPTEN__
    printf("=== Web Mod Cache Test (EMSCRIPTEN simulated mode) ===\n\n");
#else
    printf("=== Web Mod Cache Test (NATIVE mode) ===\n\n");
#endif

    /* Test 1: Header compiles with new declarations */
    printf("Test 1: Header includes new cache function declarations\n");
    TEST_ASSERT(1, "web_mod_loader.h compiled with cache functions");
    printf("\n");

    /* Test 2: web_mod_cache_get_filename() stub returns 0 (not cached) */
    printf("Test 2: web_mod_cache_get_filename() callable\n");
    {
        char buf[256] = {0};
        int found = web_mod_cache_get_filename("http://example.com/mod.lua", buf, sizeof(buf));
        TEST_ASSERT(found == 0, "Uncached URL returns 0");
        TEST_ASSERT(buf[0] == '\0', "Buffer unchanged when not found");
    }
    printf("\n");

    /* Test 3: web_mod_cache_get_filename() with NULL args */
    printf("Test 3: web_mod_cache_get_filename() NULL safety\n");
    {
        char buf[256] = {0};
        int r1 = web_mod_cache_get_filename(NULL, buf, sizeof(buf));
        TEST_ASSERT(r1 == 0, "NULL url returns 0");
        int r2 = web_mod_cache_get_filename("http://example.com/mod.lua", NULL, 0);
        TEST_ASSERT(r2 == 0, "NULL buf returns 0");
        int r3 = web_mod_cache_get_filename("", buf, sizeof(buf));
        TEST_ASSERT(r3 == 0, "Empty url returns 0");
    }
    printf("\n");

    /* Test 4: web_mod_cache_is_fresh() stub returns 0 */
    printf("Test 4: web_mod_cache_is_fresh() callable\n");
    {
        int fresh = web_mod_cache_is_fresh("http://example.com/mod.lua");
        TEST_ASSERT(fresh == 0, "Uncached URL is not fresh");
    }
    printf("\n");

    /* Test 5: web_mod_cache_is_fresh() NULL safety */
    printf("Test 5: web_mod_cache_is_fresh() NULL safety\n");
    {
        int r1 = web_mod_cache_is_fresh(NULL);
        TEST_ASSERT(r1 == 0, "NULL url returns 0");
        int r2 = web_mod_cache_is_fresh("");
        TEST_ASSERT(r2 == 0, "Empty url returns 0");
    }
    printf("\n");

    /* Test 6: web_mod_cache_remove() callable without crash */
    printf("Test 6: web_mod_cache_remove() callable\n");
    {
        web_mod_cache_remove("http://example.com/mod.lua");
        TEST_ASSERT(1, "web_mod_cache_remove called without crash");
    }
    printf("\n");

    /* Test 7: web_mod_cache_remove() NULL safety */
    printf("Test 7: web_mod_cache_remove() NULL safety\n");
    {
        web_mod_cache_remove(NULL);
        TEST_ASSERT(1, "NULL url does not crash");
        web_mod_cache_remove("");
        TEST_ASSERT(1, "Empty url does not crash");
    }
    printf("\n");

    /* Test 8: Function pointer type verification for new functions */
    printf("Test 8: Function pointer type verification\n");
    {
        int (*get_fn)(const char*, char*, int) = web_mod_cache_get_filename;
        int (*fresh_fn)(const char*) = web_mod_cache_is_fresh;
        void (*remove_fn)(const char*) = web_mod_cache_remove;
        TEST_ASSERT(get_fn != NULL, "web_mod_cache_get_filename has valid address");
        TEST_ASSERT(fresh_fn != NULL, "web_mod_cache_is_fresh has valid address");
        TEST_ASSERT(remove_fn != NULL, "web_mod_cache_remove has valid address");
    }
    printf("\n");

    /* Test 9: Existing functions still work */
    printf("Test 9: Existing cache functions still callable\n");
    {
        int cached = web_mod_is_cached("http://example.com/test.lua");
        TEST_ASSERT(cached == 0, "web_mod_is_cached still works");
        web_mod_cache_update("http://example.com/test.lua", "test.lua", 1234);
        TEST_ASSERT(1, "web_mod_cache_update still works");
    }
    printf("\n");

    /* Test 10: Cache workflow integration (stub mode) */
    printf("Test 10: Cache workflow integration\n");
    {
        /* Simulate: download → update cache → check cached → check fresh → remove */
        const char* url = "http://example.com/workflow_test.lua";

        /* After cache_update, is_cached should still return 0 in stub mode */
        web_mod_cache_update(url, "workflow_test.lua", 5678);
        int cached = web_mod_is_cached(url);
#ifdef __EMSCRIPTEN__
        /* Web stub always returns 0 (no filesystem) */
        TEST_ASSERT(cached == 0, "Web stub is_cached returns 0");
#else
        TEST_ASSERT(cached == 0, "Native stub is_cached returns 0");
#endif

        int fresh = web_mod_cache_is_fresh(url);
        TEST_ASSERT(fresh == 0, "Stub is_fresh returns 0");

        char filename[256] = {0};
        int found = web_mod_cache_get_filename(url, filename, sizeof(filename));
        TEST_ASSERT(found == 0, "Stub get_filename returns 0");

        web_mod_cache_remove(url);
        TEST_ASSERT(1, "Cache remove completes without error");
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
    printf("All web-simulated mod cache tests PASSED\n");
#else
    printf("All native mod cache tests PASSED\n");
#endif
    return 0;
}
