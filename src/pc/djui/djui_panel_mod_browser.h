#pragma once
#include "djui.h"

/**
 * Curated mod browser panel for web builds.
 *
 * Displays a list of popular/recommended mods that users can download
 * directly from URLs. Each entry has a name, description, and download
 * button. Mods that are already cached show an "Installed" indicator.
 *
 * The catalog is a static list compiled into the binary. Future versions
 * could fetch the catalog from a remote JSON endpoint.
 */

#ifdef TARGET_WEB

/** A single entry in the curated mod catalog. */
struct ModBrowserEntry {
    const char* name;        /** Display name of the mod */
    const char* description; /** Short description */
    const char* url;         /** Download URL */
    const char* category;    /** Category tag (e.g., "gamemode", "moveset") */
};

/** Get the built-in mod catalog. Sets *count to the number of entries. */
const struct ModBrowserEntry* mod_browser_get_catalog(int* count);

/** Create the mod browser panel. */
void djui_panel_mod_browser_create(struct DjuiBase* caller);

#endif /* TARGET_WEB */
