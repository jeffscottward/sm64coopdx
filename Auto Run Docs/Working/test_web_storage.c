/**
 * Compilation and logic test for web_storage.h header guards.
 *
 * Verifies that the web storage header provides correct function
 * declarations/stubs depending on build target:
 *
 * Test 1 (native): Compile without __EMSCRIPTEN__
 *   Build: /usr/bin/gcc -Wall -Wextra -Werror -o test_storage_native test_web_storage.c
 *   Expected: compiles cleanly, stubs are no-ops
 *
 * Test 2 (web-simulated): Compile with -DTARGET_WEB=1 -D__EMSCRIPTEN__ and stub include path
 *   Build: /usr/bin/gcc -Wall -Wextra -Werror -DTARGET_WEB=1 -D__EMSCRIPTEN__ -Istubs -o test_storage_web test_web_storage.c
 *   Expected: compiles cleanly, declarations visible (uses stub emscripten.h)
 *
 * NOTE: The actual Emscripten-dependent implementation (EM_ASM, IDBFS) cannot
 * be tested without the Emscripten SDK. This test verifies:
 *   - Header include guards work
 *   - Native stubs compile cleanly as no-ops
 *   - Web declarations are syntactically correct
 *   - The header can be included multiple times (include guard)
 *   - Function pointer assignment verifies type signatures
 */

#include <stdio.h>
#include <stdbool.h>

/* Include the header under test */
#include "../../src/pc/web/web_storage.h"

/* Include it again to verify include guard works */
#include "../../src/pc/web/web_storage.h"

/*
 * For the web-simulated test, provide stub implementations of the
 * functions declared in the header (since we're not linking the .c file).
 */
#ifdef __EMSCRIPTEN__
void web_storage_init(void) {
    /* Stub: no IDBFS in test environment */
}

void web_storage_save(void) {
    /* Stub: no IDBFS in test environment */
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
    printf("=== Web Storage Test (EMSCRIPTEN simulated mode) ===\n\n");
#else
    printf("=== Web Storage Test (NATIVE mode) ===\n\n");
#endif

    /* Test 1: Header compiles without errors */
    printf("Test 1: Header include guard\n");
    TEST_ASSERT(1, "web_storage.h included twice without error");
    printf("\n");

    /* Test 2: web_storage_init() is callable */
    printf("Test 2: web_storage_init() callable\n");
    {
        web_storage_init();
        TEST_ASSERT(1, "web_storage_init() called without error");
    }
    printf("\n");

    /* Test 3: web_storage_save() is callable */
    printf("Test 3: web_storage_save() callable\n");
    {
        web_storage_save();
        TEST_ASSERT(1, "web_storage_save() called without error");
    }
    printf("\n");

    /* Test 4: Function pointer assignment (type check) */
    printf("Test 4: Function pointer assignment (type check)\n");
    {
        void (*init_fn)(void) = web_storage_init;
        void (*save_fn)(void) = web_storage_save;
        TEST_ASSERT(init_fn != NULL, "web_storage_init has valid address");
        TEST_ASSERT(save_fn != NULL, "web_storage_save has valid address");
    }
    printf("\n");

    /* Test 5: Multiple calls are safe (idempotent) */
    printf("Test 5: Multiple calls are safe\n");
    {
        web_storage_init();
        web_storage_init();
        web_storage_save();
        web_storage_save();
        TEST_ASSERT(1, "Multiple init/save calls complete without error");
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
    printf("All web-simulated storage tests PASSED\n");
#else
    printf("All native storage tests PASSED\n");
#endif
    return 0;
}
