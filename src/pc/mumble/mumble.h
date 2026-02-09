#ifndef MUMBLE_H
#define MUMBLE_H

#include <stdbool.h>

#ifdef TARGET_WEB

/*
 * Mumble positional audio uses shared memory (shm_open, mmap) which is
 * unavailable in browsers.  Provide no-op stubs so call sites in pc_main.c
 * compile to nothing without needing #ifdef guards at every call.
 */
static inline void mumble_init(void) { (void)0; }
static inline void mumble_update(void) { (void)0; }
static inline void mumble_update_menu(void) { (void)0; }
static inline bool should_update_context(void) { return false; }

#else /* !TARGET_WEB */

#include <stdint.h>
#include <wchar.h>

struct LinkedMem {

	uint32_t uiVersion;
	uint32_t uiTick;

	float	fAvatarPosition[3];
	float	fAvatarFront[3];
	float	fAvatarTop[3];
	wchar_t	name[256];
	float	fCameraPosition[3];
	float	fCameraFront[3];
	float	fCameraTop[3];
	wchar_t	identity[256];

	uint32_t	context_len;

	unsigned char context[256];
	wchar_t description[2048];
};

void mumble_init(void);
void mumble_update(void);
void mumble_update_menu(void);

bool should_update_context(void);

#endif /* !TARGET_WEB */

#endif /* MUMBLE_H */