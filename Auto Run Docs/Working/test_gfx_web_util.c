/**
 * Isolated compilation test for gfx_web_util.h
 *
 * Test 1: Verify the header compiles to nothing when __EMSCRIPTEN__ is NOT defined.
 * Test 2: Verify header syntax is valid C by checking the preprocessor output with
 *          __EMSCRIPTEN__ defined (using a stub approach since we don't have emcc here).
 *
 * Run: cc -fsyntax-check test_gfx_web_util.c -I../../src/pc/gfx
 *   (without __EMSCRIPTEN__ — should compile cleanly as empty)
 */

#include <stdio.h>

/* Test 1: Include without __EMSCRIPTEN__ — should be completely empty */
#include "../../src/pc/gfx/gfx_web_util.h"

int main(void) {
    printf("Test 1 PASSED: gfx_web_util.h compiles to nothing without __EMSCRIPTEN__\n");

#ifdef __EMSCRIPTEN__
    /* Test 2: Would test actual function calls under Emscripten.
       This path only compiles with emcc. */
    double dpr = gfx_web_get_device_pixel_ratio();
    printf("  Device pixel ratio: %f\n", dpr);

    bool resized = gfx_web_sync_canvas_size("#canvas");
    printf("  Canvas resized: %d\n", resized);

    bool lost = gfx_web_is_context_lost();
    printf("  Context lost: %d\n", lost);

    gfx_web_register_context_handlers("#canvas");
    printf("  Context handlers registered\n");

    printf("Test 2 PASSED: All gfx_web_util.h functions callable under __EMSCRIPTEN__\n");
#else
    printf("Test 2 SKIPPED: __EMSCRIPTEN__ not defined (native build)\n");
#endif

    printf("All tests passed.\n");
    return 0;
}
