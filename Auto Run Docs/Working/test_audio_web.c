/**
 * Isolated compilation test for audio_web.h
 *
 * Test 1: Verify the header compiles on native builds (non-Emscripten).
 *         The native stubs should provide no-op functions.
 * Test 2: Verify audio_web_setup_resume() is idempotent (multiple calls are safe).
 * Test 3: Verify audio_web_is_running() returns true on native builds.
 *
 * Build: /usr/bin/cc -Wall -Wextra -Werror -o test_audio_web test_audio_web.c
 * Run:   ./test_audio_web
 */

#include <stdio.h>
#include <stdbool.h>

/* Include the header under test — without __EMSCRIPTEN__, native stubs apply */
#include "../../src/pc/audio/audio_web.h"

int main(void) {
    /* Test 1: Header compiles on native builds */
    printf("Test 1 PASSED: audio_web.h compiles on native build\n");

    /* Test 2: audio_web_setup_resume() is a no-op on native, multiple calls safe */
    audio_web_setup_resume();
    audio_web_setup_resume();
    printf("Test 2 PASSED: audio_web_setup_resume() is idempotent no-op on native\n");

    /* Test 3: audio_web_is_running() returns true on native */
    bool running = audio_web_is_running();
    if (!running) {
        printf("FAILED: audio_web_is_running() should return true on native\n");
        return 1;
    }
    printf("Test 3 PASSED: audio_web_is_running() returns true on native\n");

    printf("All native audio_web.h tests PASSED\n");
    return 0;
}
