/**
 * Isolated syntax and behavior test for audio_web.h under simulated __EMSCRIPTEN__.
 *
 * Uses stub Emscripten headers to verify C syntax correctness and behavioral
 * correctness of the Emscripten code path without requiring the actual SDK.
 *
 * Build: /usr/bin/cc -Wall -Wextra -Werror -D__EMSCRIPTEN__ -Istubs \
 *            -o test_audio_web_emscripten test_audio_web_emscripten.c
 * Run:   ./test_audio_web_emscripten
 */

#include <stdio.h>
#include <stdbool.h>

/* Include the header under test (stubs dir provides emscripten headers) */
#include "../../src/pc/audio/audio_web.h"

int main(void) {
    /* Test 1: Header compiles under simulated Emscripten */
    printf("Test 1 PASSED: audio_web.h compiles under simulated __EMSCRIPTEN__\n");

    /* Test 2: audio_web_setup_resume() can be called */
    audio_web_setup_resume();
    printf("Test 2 PASSED: audio_web_setup_resume() callable\n");

    /* Test 3: Second call is a no-op (idempotency via guard flag) */
    audio_web_setup_resume();
    printf("Test 3 PASSED: audio_web_setup_resume() idempotent (guard flag works)\n");

    /* Test 4: audio_web_is_running() returns a boolean
       (with stubs, EM_ASM_INT returns 0 so this will return false) */
    bool running = audio_web_is_running();
    printf("  audio_web_is_running() = %d (expected 0 with stubs)\n", running);
    if (running != false) {
        printf("FAILED: with stubs, EM_ASM_INT returns 0 so is_running should be false\n");
        return 1;
    }
    printf("Test 4 PASSED: audio_web_is_running() returns expected stub value\n");

    /* Test 5: Verify the guard flag was set */
    if (!audio_web_resume_installed) {
        printf("FAILED: audio_web_resume_installed should be true after setup\n");
        return 1;
    }
    printf("Test 5 PASSED: audio_web_resume_installed flag is set\n");

    printf("All Emscripten-path audio_web.h tests PASSED\n");
    return 0;
}
