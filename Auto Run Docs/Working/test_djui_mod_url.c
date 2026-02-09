/**
 * test_djui_mod_url.c -- Unit test for DJUI mod URL integration.
 *
 * Tests the web mod URL download feature added to djui_panel_host_mods.c.
 * Validates URL validation logic, callback type signatures, error code
 * handling, and state management.
 *
 * Compile (native mode - tests native stubs):
 *   cc -o test_djui_mod_url test_djui_mod_url.c -I../../ -I../../src -I../../include
 *
 * Compile (web-simulated mode):
 *   cc -D__EMSCRIPTEN__ -I../../Auto\ Run\ Docs/Working/stubs -o test_djui_mod_url_web \
 *      test_djui_mod_url.c -I../../ -I../../src -I../../include
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Web mod loader header — provides API and native stubs */
#include "pc/web/web_mod_loader.h"

/*
 * When building in web-simulated mode (__EMSCRIPTEN__ defined),
 * the header declares extern functions. Provide stub implementations
 * for linking the test binary.
 */
#ifdef __EMSCRIPTEN__
int web_mod_download(const char* url, const char* dest_filename) {
    (void)url; (void)dest_filename;
    return WEB_MOD_ERR_FETCH;
}
void web_mod_download_async(const char* url, const char* dest_filename,
                            web_mod_download_callback callback) {
    (void)url; (void)dest_filename;
    if (callback) callback(WEB_MOD_ERR_FETCH);
}
int web_mod_is_cached(const char* url) { (void)url; return 0; }
void web_mod_cache_update(const char* url, const char* filename, int size) {
    (void)url; (void)filename; (void)size;
}
void web_mod_check_async_complete(void) { }
int web_mod_cache_get_filename(const char* url, char* buf, int bufsize) {
    (void)url; (void)buf; (void)bufsize; return 0;
}
int web_mod_cache_is_fresh(const char* url) { (void)url; return 0; }
void web_mod_cache_remove(const char* url) { (void)url; }
#endif

/* Test infrastructure */
static int sTestsPassed = 0;
static int sTestsFailed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        sTestsPassed++; \
        printf("  PASS: %s\n", msg); \
    } else { \
        sTestsFailed++; \
        printf("  FAIL: %s\n", msg); \
    } \
} while(0)

/* ===== URL validation tests (mirrors djui_mod_url_text_change logic) ===== */

static int validate_mod_url(const char* url) {
    if (!url || url[0] == '\0') return 0;
    if (strncmp(url, "http://", 7) == 0) return 1;
    if (strncmp(url, "https://", 8) == 0) return 1;
    return 0;
}

static void test_url_validation(void) {
    printf("\n=== URL Validation Tests ===\n");

    TEST_ASSERT(validate_mod_url("https://example.com/mod.lua") == 1,
                "Valid HTTPS URL accepted");
    TEST_ASSERT(validate_mod_url("http://example.com/mod.lua") == 1,
                "Valid HTTP URL accepted");
    TEST_ASSERT(validate_mod_url("https://mods.sm64coopdx.com/mods/test.lua") == 1,
                "Valid mods site URL accepted");
    TEST_ASSERT(validate_mod_url("ftp://example.com/mod.lua") == 0,
                "FTP URL rejected");
    TEST_ASSERT(validate_mod_url("ws://example.com/mod.lua") == 0,
                "WebSocket URL rejected");
    TEST_ASSERT(validate_mod_url("") == 0,
                "Empty string rejected");
    TEST_ASSERT(validate_mod_url(NULL) == 0,
                "NULL rejected");
    TEST_ASSERT(validate_mod_url("not-a-url") == 0,
                "Plain text rejected");
    TEST_ASSERT(validate_mod_url("httpx://bad.com/mod.lua") == 0,
                "Invalid scheme rejected");
    TEST_ASSERT(validate_mod_url("https://example.com/mods/pack.zip") == 1,
                "ZIP mod pack URL accepted");
}

/* ===== Error code tests ===== */

static void test_error_codes(void) {
    printf("\n=== Error Code Tests ===\n");

    TEST_ASSERT(WEB_MOD_OK == 0, "WEB_MOD_OK is 0");
    TEST_ASSERT(WEB_MOD_ERR_FETCH == -1, "WEB_MOD_ERR_FETCH is -1");
    TEST_ASSERT(WEB_MOD_ERR_WRITE == -2, "WEB_MOD_ERR_WRITE is -2");
    TEST_ASSERT(WEB_MOD_ERR_BADURL == -3, "WEB_MOD_ERR_BADURL is -3");
    TEST_ASSERT(WEB_MOD_ERR_TOOLARGE == -4, "WEB_MOD_ERR_TOOLARGE is -4");
}

/* ===== Callback type signature tests ===== */

static int sCallbackStatus = -999;
static void test_callback_fn(int status) {
    sCallbackStatus = status;
}

static void test_callback_types(void) {
    printf("\n=== Callback Type Tests ===\n");

    /* Verify callback typedef is usable */
    web_mod_download_callback cb = test_callback_fn;
    TEST_ASSERT(cb != NULL, "Callback function pointer assigned");

    /* Invoke callback with each error code */
    cb(WEB_MOD_OK);
    TEST_ASSERT(sCallbackStatus == WEB_MOD_OK, "Callback received OK status");

    cb(WEB_MOD_ERR_FETCH);
    TEST_ASSERT(sCallbackStatus == WEB_MOD_ERR_FETCH, "Callback received FETCH error");

    cb(WEB_MOD_ERR_WRITE);
    TEST_ASSERT(sCallbackStatus == WEB_MOD_ERR_WRITE, "Callback received WRITE error");

    cb(WEB_MOD_ERR_BADURL);
    TEST_ASSERT(sCallbackStatus == WEB_MOD_ERR_BADURL, "Callback received BADURL error");

    cb(WEB_MOD_ERR_TOOLARGE);
    TEST_ASSERT(sCallbackStatus == WEB_MOD_ERR_TOOLARGE, "Callback received TOOLARGE error");
}

/* ===== Native stub behavior tests ===== */

static void test_native_stubs(void) {
    printf("\n=== Native Stub Behavior Tests ===\n");

    /* web_mod_download should return error on native */
    int result = web_mod_download("https://example.com/mod.lua", "mod.lua");
    TEST_ASSERT(result == WEB_MOD_ERR_FETCH, "Native web_mod_download returns FETCH error");

    result = web_mod_download(NULL, NULL);
    TEST_ASSERT(result == WEB_MOD_ERR_FETCH, "Native web_mod_download with NULL returns FETCH error");

    /* web_mod_is_cached should return 0 on native */
    result = web_mod_is_cached("https://example.com/mod.lua");
    TEST_ASSERT(result == 0, "Native web_mod_is_cached returns 0");

    /* web_mod_download_async should invoke callback with error */
    sCallbackStatus = -999;
    web_mod_download_async("https://example.com/mod.lua", "mod.lua", test_callback_fn);
    TEST_ASSERT(sCallbackStatus == WEB_MOD_ERR_FETCH,
                "Native async download invokes callback with FETCH error");

    /* web_mod_download_async with NULL callback should not crash */
    web_mod_download_async("https://example.com/mod.lua", "mod.lua", NULL);
    TEST_ASSERT(1, "Native async download with NULL callback does not crash");

    /* web_mod_check_async_complete should be a no-op */
    web_mod_check_async_complete();
    TEST_ASSERT(1, "Native web_mod_check_async_complete is safe no-op");

    /* Cache functions should return 0 / no-op */
    char buf[256] = {0};
    result = web_mod_cache_get_filename("https://example.com/mod.lua", buf, sizeof(buf));
    TEST_ASSERT(result == 0, "Native cache_get_filename returns 0");

    result = web_mod_cache_is_fresh("https://example.com/mod.lua");
    TEST_ASSERT(result == 0, "Native cache_is_fresh returns 0");

    /* These should not crash */
    web_mod_cache_update("url", "file", 100);
    web_mod_cache_remove("url");
    TEST_ASSERT(1, "Native cache_update and cache_remove are safe no-ops");
}

/* ===== Download state management tests ===== */

/* Simulate the state machine used in djui_panel_host_mods.c */
static int sSimDownloading = 0;
static float sSimProgress = 0.0f;

static void sim_download_start(const char* url) {
    if (sSimDownloading) return;
    if (!validate_mod_url(url)) return;
    sSimDownloading = 1;
    sSimProgress = 0.5f;
}

static void sim_download_complete(int status) {
    sSimDownloading = 0;
    sSimProgress = 0.0f;
    (void)status;
}

static void test_state_management(void) {
    printf("\n=== State Management Tests ===\n");

    TEST_ASSERT(sSimDownloading == 0, "Initial state: not downloading");
    TEST_ASSERT(sSimProgress == 0.0f, "Initial progress is 0");

    /* Start a download */
    sim_download_start("https://example.com/mod.lua");
    TEST_ASSERT(sSimDownloading == 1, "After start: downloading = true");
    TEST_ASSERT(sSimProgress == 0.5f, "After start: progress is 0.5 (indeterminate)");

    /* Try to start another download while one is in progress */
    sim_download_start("https://example.com/other.lua");
    TEST_ASSERT(sSimDownloading == 1, "Double-start blocked: still downloading");

    /* Complete the download */
    sim_download_complete(WEB_MOD_OK);
    TEST_ASSERT(sSimDownloading == 0, "After complete: not downloading");
    TEST_ASSERT(sSimProgress == 0.0f, "After complete: progress reset to 0");

    /* Invalid URL should not start download */
    sim_download_start("ftp://bad.com/mod.lua");
    TEST_ASSERT(sSimDownloading == 0, "Invalid URL does not start download");

    sim_download_start("");
    TEST_ASSERT(sSimDownloading == 0, "Empty URL does not start download");
}

int main(void) {
    printf("=== DJUI Mod URL Integration Tests ===\n");

#ifdef __EMSCRIPTEN__
    printf("Mode: Web-simulated (__EMSCRIPTEN__ defined)\n");
#else
    printf("Mode: Native\n");
#endif

    test_url_validation();
    test_error_codes();
    test_callback_types();
    test_native_stubs();
    test_state_management();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", sTestsPassed);
    printf("Failed: %d\n", sTestsFailed);
    printf("Total:  %d\n", sTestsPassed + sTestsFailed);

    return sTestsFailed > 0 ? 1 : 0;
}
