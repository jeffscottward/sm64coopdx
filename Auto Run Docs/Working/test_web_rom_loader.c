/**
 * Compilation and logic test for web_rom_loader.h header guards.
 *
 * Verifies that the web ROM loader header provides correct function
 * declarations/stubs depending on build target:
 *
 * Test 1 (native): Compile without __EMSCRIPTEN__
 *   Build: cc -Wall -Wextra -Werror -o test_rom_loader_native test_web_rom_loader.c
 *   Expected: compiles cleanly, stubs return 0
 *
 * Test 2 (web-simulated): Compile with -DTARGET_WEB=1 -D__EMSCRIPTEN__ and stub include path
 *   Build: cc -Wall -Wextra -Werror -DTARGET_WEB=1 -D__EMSCRIPTEN__ -Istubs -o test_rom_loader_web test_web_rom_loader.c
 *   Expected: compiles cleanly, declarations visible (uses stub emscripten.h)
 *
 * NOTE: The actual Emscripten-dependent implementation (EM_ASM, emscripten_sleep)
 * cannot be tested without the Emscripten SDK. This test verifies:
 *   - Header include guards work
 *   - Native stubs compile and return expected values
 *   - Web declarations are syntactically correct
 *   - The header can be included multiple times (include guard)
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

/*
 * For web-simulated builds (-D__EMSCRIPTEN__), use the stub emscripten.h
 * from stubs/emscripten/ via -Istubs on the compile command line.
 * No inline mocking needed — the stub header provides all required symbols.
 *
 * Mock the platform.h SYS_MAX_PATH define used by the implementation.
 */
#ifndef SYS_MAX_PATH
#define SYS_MAX_PATH 4096
#endif

/* Include the header under test */
#include "../../src/pc/web/web_rom_loader.h"

/* Include it again to verify include guard works */
#include "../../src/pc/web/web_rom_loader.h"

/*
 * For the web-simulated test, provide stub implementations of the
 * functions declared in the header (since we're not linking the .c file).
 */
#ifdef __EMSCRIPTEN__
int web_check_rom_exists(void) {
    /* Stub: no ROM in test environment */
    return 0;
}

int web_load_rom_from_picker(void) {
    /* Stub: simulate user cancel */
    return 0;
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
    printf("=== Web ROM Loader Test (EMSCRIPTEN simulated mode) ===\n\n");
#else
    printf("=== Web ROM Loader Test (NATIVE mode) ===\n\n");
#endif

    /* Test 1: Header compiles without errors */
    printf("Test 1: Header include guard\n");
    TEST_ASSERT(1, "web_rom_loader.h included twice without error");
    printf("\n");

    /* Test 2: web_check_rom_exists() is callable */
    printf("Test 2: web_check_rom_exists() callable\n");
    {
        int result = web_check_rom_exists();
        TEST_ASSERT(result == 0 || result == 1,
                    "web_check_rom_exists() returns 0 or 1");
#ifndef __EMSCRIPTEN__
        TEST_ASSERT(result == 0,
                    "Native stub returns 0 (no ROM exists)");
#endif
    }
    printf("\n");

    /* Test 3: web_load_rom_from_picker() is callable */
    printf("Test 3: web_load_rom_from_picker() callable\n");
    {
        int result = web_load_rom_from_picker();
        TEST_ASSERT(result == 0 || result == 1,
                    "web_load_rom_from_picker() returns 0 or 1");
#ifndef __EMSCRIPTEN__
        TEST_ASSERT(result == 0,
                    "Native stub returns 0 (always fails on native)");
#endif
    }
    printf("\n");

    /* Test 4: Function signatures match expected types */
    printf("Test 4: Function pointer assignment (type check)\n");
    {
        int (*check_fn)(void) = web_check_rom_exists;
        int (*load_fn)(void) = web_load_rom_from_picker;
        TEST_ASSERT(check_fn != NULL, "web_check_rom_exists has valid address");
        TEST_ASSERT(load_fn != NULL, "web_load_rom_from_picker has valid address");
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
    printf("All web-simulated ROM loader tests PASSED\n");
#else
    printf("All native ROM loader tests PASSED\n");
#endif
    return 0;
}
