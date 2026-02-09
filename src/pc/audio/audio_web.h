#ifndef AUDIO_WEB_H
#define AUDIO_WEB_H

/**
 * audio_web.h — Browser autoplay policy workaround for Emscripten builds.
 *
 * Modern browsers require a user gesture (click, keypress, touch) before
 * audio playback can start. Emscripten's SDL2 audio port uses the Web Audio
 * API internally, which is subject to this policy. Even though
 * SDL_PauseAudioDevice(dev, 0) is called during init, the AudioContext
 * starts in a "suspended" state until the user interacts with the page.
 *
 * This header provides a one-shot mechanism that resumes the AudioContext
 * after the first user interaction. It uses EM_ASM to inject a JavaScript
 * event listener that calls audioCtx.resume() on the first click, keydown,
 * or touchstart event.
 *
 * On native builds this header compiles to nothing.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <stdbool.h>

/* Tracks whether the audio resume listener has been installed. */
static bool audio_web_resume_installed = false;

/**
 * Installs a one-time JavaScript event listener that resumes the Web Audio
 * AudioContext on the first user interaction (click, keydown, or touchstart).
 *
 * SDL2's Emscripten port creates a global AudioContext accessible as
 * SDL2.audioContext (or Module.SDL2.audioContext). This function targets
 * that context.
 *
 * Call this once after SDL audio initialization (audio_sdl.init()).
 * Subsequent calls are no-ops.
 */
static inline void audio_web_setup_resume(void) {
    if (audio_web_resume_installed) return;
    audio_web_resume_installed = true;

    EM_ASM({
        // Emscripten SDL2 audio stores the AudioContext in SDL2.audioContext
        // Try multiple known locations for the AudioContext
        function tryResumeAudio() {
            var ctx = null;
            if (typeof SDL2 !== 'undefined' && SDL2.audioContext) {
                ctx = SDL2.audioContext;
            } else if (typeof Module !== 'undefined' && Module.SDL2 && Module.SDL2.audioContext) {
                ctx = Module.SDL2.audioContext;
            }
            if (ctx && ctx.state === 'suspended') {
                ctx.resume();
            }
        }

        var events = ['click', 'keydown', 'touchstart'];
        function onInteraction() {
            tryResumeAudio();
            // Remove all listeners after first interaction
            for (var i = 0; i < events.length; i++) {
                document.removeEventListener(events[i], onInteraction, true);
            }
        }
        for (var i = 0; i < events.length; i++) {
            document.addEventListener(events[i], onInteraction, true);
        }
    });
}

/**
 * Returns true if the Web Audio AudioContext is currently running.
 * Returns false if still suspended (waiting for user interaction).
 * Useful for UI hints like "Click to enable audio".
 */
static inline bool audio_web_is_running(void) {
    return (bool)EM_ASM_INT({
        var ctx = null;
        if (typeof SDL2 !== 'undefined' && SDL2.audioContext) {
            ctx = SDL2.audioContext;
        } else if (typeof Module !== 'undefined' && Module.SDL2 && Module.SDL2.audioContext) {
            ctx = Module.SDL2.audioContext;
        }
        return (ctx && ctx.state === 'running') ? 1 : 0;
    });
}

#else /* !__EMSCRIPTEN__ */

/* Native builds: these compile to nothing. */
static inline void audio_web_setup_resume(void) { (void)0; }
static inline bool audio_web_is_running(void) { return true; }

#endif /* __EMSCRIPTEN__ */

#endif /* AUDIO_WEB_H */
