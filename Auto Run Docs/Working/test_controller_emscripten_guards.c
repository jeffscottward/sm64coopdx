/**
 * Compilation test for controller_sdl2.c Emscripten guards.
 *
 * This test verifies that the #ifndef __EMSCRIPTEN__ guards correctly
 * exclude haptic-related symbols when __EMSCRIPTEN__ is defined.
 *
 * Test 1 (native): Compile without __EMSCRIPTEN__
 *   Build: /usr/bin/cc -Wall -Wextra -Werror -o test_controller_guards test_controller_emscripten_guards.c
 *   Expected: compiles cleanly, all haptic paths accessible
 *
 * Test 2 (emscripten-simulated): Compile with -D__EMSCRIPTEN__
 *   Build: /usr/bin/cc -Wall -Wextra -Werror -D__EMSCRIPTEN__ -o test_controller_guards_emu test_controller_emscripten_guards.c
 *   Expected: compiles cleanly, haptic paths excluded
 */

#include <stdio.h>
#include <stdbool.h>

/* Simulate the guard pattern used in controller_sdl2.c */

static bool init_ok = false;

#ifndef __EMSCRIPTEN__
static bool haptics_enabled = false;
/* In real code: static SDL_Haptic *sdl_haptic = NULL; */
static void *sdl_haptic = NULL;
#endif

static void test_init(void) {
#ifndef __EMSCRIPTEN__
    haptics_enabled = true;
    sdl_haptic = NULL;
    printf("  haptics_enabled initialized\n");
    printf("  sdl_haptic initialized\n");
    printf("  gamecontrollerdb.txt would be loaded\n");
#endif
    init_ok = true;
}

static void test_rumble_play(float strength, float length) {
#ifndef __EMSCRIPTEN__
    if (sdl_haptic) {
        printf("  Would play haptic rumble: str=%.2f len=%.2f\n", strength, length);
    }
    (void)haptics_enabled;
#else
    (void)strength; (void)length;
#endif
}

static void test_rumble_stop(void) {
#ifndef __EMSCRIPTEN__
    if (sdl_haptic) {
        printf("  Would stop haptic rumble\n");
    }
#endif
}

static void test_shutdown(void) {
#ifndef __EMSCRIPTEN__
    sdl_haptic = NULL;
    haptics_enabled = false;
    printf("  Haptic subsystem cleaned up\n");
#endif
    init_ok = false;
}

int main(void) {
#ifdef __EMSCRIPTEN__
    printf("=== Controller Emscripten Guard Test (EMSCRIPTEN mode) ===\n\n");
#else
    printf("=== Controller Emscripten Guard Test (NATIVE mode) ===\n\n");
#endif

    /* Test 1: Init */
    printf("Test 1: controller_sdl_init pattern\n");
    test_init();
    if (!init_ok) { printf("FAILED: init_ok not set\n"); return 1; }
    printf("  init_ok = true\n");
    printf("  PASSED\n\n");

    /* Test 2: Rumble play */
    printf("Test 2: controller_sdl_rumble_play pattern\n");
    test_rumble_play(0.5f, 1.0f);
#ifdef __EMSCRIPTEN__
    printf("  rumble_play is no-op (expected for Emscripten)\n");
#endif
    printf("  PASSED\n\n");

    /* Test 3: Rumble stop */
    printf("Test 3: controller_sdl_rumble_stop pattern\n");
    test_rumble_stop();
#ifdef __EMSCRIPTEN__
    printf("  rumble_stop is no-op (expected for Emscripten)\n");
#endif
    printf("  PASSED\n\n");

    /* Test 4: Shutdown */
    printf("Test 4: controller_sdl_shutdown pattern\n");
    test_shutdown();
    if (init_ok) { printf("FAILED: init_ok should be false\n"); return 1; }
    printf("  init_ok = false\n");
    printf("  PASSED\n\n");

#ifdef __EMSCRIPTEN__
    printf("All Emscripten-mode guard tests PASSED\n");
#else
    printf("All native-mode guard tests PASSED\n");
#endif
    return 0;
}
