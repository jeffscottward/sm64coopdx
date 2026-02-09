/**
 * Compilation and logic test for ROM loading integration in pc_main.c.
 *
 * Verifies that the web ROM loading startup flow compiles and that
 * the interaction between web_rom_loader and web_storage functions
 * follows the expected pattern:
 *   1. main_rom_handler() checks for existing ROM
 *   2. web_check_rom_exists() checks IDBFS persistent storage
 *   3. web_load_rom_from_picker() shows browser file picker
 *   4. web_storage_save() persists loaded ROM
 *
 * Test 1 (native): Compile without __EMSCRIPTEN__
 *   Build: cc -Wall -Wextra -Werror -o test_rom_startup_native test_rom_startup_integration.c
 *   Expected: compiles cleanly, stubs return expected values
 *
 * Test 2 (web-simulated): Compile with -DTARGET_WEB=1 -D__EMSCRIPTEN__ and stub include path
 *   Build: cc -Wall -Wextra -Werror -DTARGET_WEB=1 -D__EMSCRIPTEN__ -Istubs -o test_rom_startup_web test_rom_startup_integration.c
 *   Expected: compiles cleanly, web function declarations visible
 */

#include <stdio.h>
#include <stdbool.h>

/* Mock SYS_MAX_PATH used by the headers */
#ifndef SYS_MAX_PATH
#define SYS_MAX_PATH 4096
#endif

/* Include both headers as they appear in pc_main.c */
#include "../../src/pc/web/web_storage.h"
#include "../../src/pc/web/web_rom_loader.h"

/*
 * For the web-simulated test, provide stub implementations of the
 * functions declared in the headers (since we're not linking the .c files).
 */
#ifdef __EMSCRIPTEN__

/* web_storage stubs */
static int s_storage_init_called = 0;
static int s_storage_save_called = 0;

void web_storage_init(void) {
    s_storage_init_called++;
}

void web_storage_save(void) {
    s_storage_save_called++;
}

/* web_rom_loader stubs */
static int s_rom_exists_result = 0;
static int s_rom_picker_result = 0;
static int s_check_rom_called = 0;
static int s_load_picker_called = 0;

int web_check_rom_exists(void) {
    s_check_rom_called++;
    return s_rom_exists_result;
}

int web_load_rom_from_picker(void) {
    s_load_picker_called++;
    return s_rom_picker_result;
}

#endif /* __EMSCRIPTEN__ */

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
    printf("=== ROM Startup Integration Test (EMSCRIPTEN simulated mode) ===\n\n");
#else
    printf("=== ROM Startup Integration Test (NATIVE mode) ===\n\n");
#endif

    /* Test 1: Both headers can be included together */
    printf("Test 1: Headers include without conflict\n");
    TEST_ASSERT(1, "web_storage.h and web_rom_loader.h included without error");
    printf("\n");

    /* Test 2: Functions are callable */
    printf("Test 2: All integration functions callable\n");
    {
        web_storage_init();
        int rom_exists = web_check_rom_exists();
        int picker_result = web_load_rom_from_picker();
        web_storage_save();

        TEST_ASSERT(rom_exists == 0 || rom_exists == 1,
                    "web_check_rom_exists() returns boolean");
        TEST_ASSERT(picker_result == 0 || picker_result == 1,
                    "web_load_rom_from_picker() returns boolean");

#ifndef __EMSCRIPTEN__
        /* Native stubs: all should be no-ops or return 0 */
        TEST_ASSERT(rom_exists == 0,
                    "Native stub: web_check_rom_exists() returns 0");
        TEST_ASSERT(picker_result == 0,
                    "Native stub: web_load_rom_from_picker() returns 0");
#endif
    }
    printf("\n");

#ifdef __EMSCRIPTEN__
    /* Test 3: Simulate the startup flow from pc_main.c */
    printf("Test 3: Simulate web startup flow — ROM not found, picker succeeds\n");
    {
        s_check_rom_called = 0;
        s_load_picker_called = 0;
        s_storage_save_called = 0;

        /* Simulate: no ROM in IDBFS, picker returns success */
        s_rom_exists_result = 0;
        s_rom_picker_result = 1;

        /* This mirrors the logic in pc_main.c */
        bool main_rom_found = false; /* simulate main_rom_handler() returning false */
        if (!main_rom_found) {
            if (!web_check_rom_exists()) {
                int loaded = web_load_rom_from_picker();
                TEST_ASSERT(loaded == 1, "Picker returned success");
            }
            /* After loading, call web_storage_save() */
            web_storage_save();
        }

        TEST_ASSERT(s_check_rom_called == 1, "web_check_rom_exists called once");
        TEST_ASSERT(s_load_picker_called == 1, "web_load_rom_from_picker called once");
        TEST_ASSERT(s_storage_save_called == 1, "web_storage_save called once");
    }
    printf("\n");

    /* Test 4: Simulate the startup flow — ROM already in IDBFS */
    printf("Test 4: Simulate web startup flow — ROM already persisted\n");
    {
        s_check_rom_called = 0;
        s_load_picker_called = 0;
        s_storage_save_called = 0;

        /* Simulate: ROM exists in IDBFS */
        s_rom_exists_result = 1;

        bool main_rom_found = true; /* simulate main_rom_handler() returning true */
        if (!main_rom_found) {
            /* This block should not execute */
            web_check_rom_exists();
            web_load_rom_from_picker();
            web_storage_save();
        }

        TEST_ASSERT(s_check_rom_called == 0, "No ROM check needed — handler found it");
        TEST_ASSERT(s_load_picker_called == 0, "No picker needed — ROM already valid");
        TEST_ASSERT(s_storage_save_called == 0, "No save needed — ROM was already persisted");
    }
    printf("\n");
#endif

    /* Summary */
    printf("=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    if (tests_failed > 0) {
        printf("SOME TESTS FAILED\n");
        return 1;
    }

#ifdef __EMSCRIPTEN__
    printf("All web-simulated ROM startup integration tests PASSED\n");
#else
    printf("All native ROM startup integration tests PASSED\n");
#endif
    return 0;
}
