/**
 * Isolated syntax test for gfx_web_util.h under simulated __EMSCRIPTEN__.
 *
 * Uses stub Emscripten headers to verify the C syntax and type correctness
 * of the Emscripten code path without requiring the actual Emscripten SDK.
 *
 * Build: /usr/bin/cc -Wall -Wextra -Werror -D__EMSCRIPTEN__ -Istubs \
 *            -o test_emscripten test_gfx_web_util_emscripten.c
 */

#include <stdio.h>
#include <stdbool.h>

/* Include the actual header under test (stubs dir provides emscripten headers) */
#include "../../src/pc/gfx/gfx_web_util.h"

int main(void) {
    /* Exercise all functions to verify they compile and link */
    double dpr = gfx_web_get_device_pixel_ratio();
    printf("  gfx_web_get_device_pixel_ratio() = %f\n", dpr);

    bool resized = gfx_web_sync_canvas_size("#canvas");
    printf("  gfx_web_sync_canvas_size(\"#canvas\") = %d\n", resized);

    bool lost = gfx_web_is_context_lost();
    printf("  gfx_web_is_context_lost() = %d\n", lost);

    gfx_web_register_context_handlers("#canvas");
    printf("  gfx_web_register_context_handlers() OK\n");

    /* Verify context loss flag transitions */
    gfx_web_on_context_lost(0, NULL, NULL);
    if (!gfx_web_is_context_lost()) {
        printf("FAILED: context should be lost\n");
        return 1;
    }

    gfx_web_on_context_restored(0, NULL, NULL);
    if (gfx_web_is_context_lost()) {
        printf("FAILED: context should be restored\n");
        return 1;
    }

    printf("All Emscripten path tests PASSED\n");
    return 0;
}
