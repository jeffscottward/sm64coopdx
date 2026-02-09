# Phase 08: Mod Loading from URLs

This phase implements the ability to load Lua mods directly from URLs in the browser, eliminating the need for users to download mod files manually. The existing mod system loads `.lua` files and mod packs from the local `mods/` directory. For the web port, we extend this to fetch mods via HTTP from URLs (including from mods.sm64coopdx.com), write them to Emscripten's virtual filesystem, and load them through the existing mod pipeline. A simple in-game UI lets users paste a mod URL or browse a curated list. Loaded mods are cached in IndexedDB for persistence.

## Tasks

- [x] Create `src/pc/web/web_mod_loader.c` — HTTP-based mod fetching for the browser:
  - Use Emscripten's `emscripten_async_wget2_data()` or the Fetch API via `EM_JS` to download mod files from URLs
  - Create a C function `int web_mod_download(const char* url, const char* dest_path)` that:
    - Fetches the file at `url` via HTTP GET
    - Writes the response data to `dest_path` in the virtual filesystem (the game's mods directory)
    - Returns 0 on success, error code on failure
    - Handles both single `.lua` files and `.zip` mod packs
  - Create `void web_mod_download_async(const char* url, const char* dest_path, void (*callback)(int status))` for non-blocking downloads
  - Handle CORS: mods.sm64coopdx.com must serve files with `Access-Control-Allow-Origin: *` headers, or the mod files must be proxied
  - Create corresponding header `src/pc/web/web_mod_loader.h`
  - Guard with `#ifdef TARGET_WEB`

  **Completed:** Created `src/pc/web/web_mod_loader.h` and `src/pc/web/web_mod_loader.c`. Implementation uses browser Fetch API via EM_ASM for HTTP downloads, writes to Emscripten VFS mods directory, and persists via web_storage_save(). Both blocking (web_mod_download with ASYNCIFY polling) and non-blocking (web_mod_download_async with callback) APIs provided. Error codes: WEB_MOD_OK(0), WEB_MOD_ERR_FETCH(-1), WEB_MOD_ERR_WRITE(-2), WEB_MOD_ERR_BADURL(-3), WEB_MOD_ERR_TOOLARGE(-4). 16MB size limit. URL filename extraction for auto-naming. Native stubs compile to error-returning no-ops. 22 tests pass in both native and web-simulated modes. Full Emscripten build verified.

- [ ] Implement mod caching in IndexedDB via the existing IDBFS storage:
  - When a mod is downloaded, it's written to the mods directory in Emscripten's virtual filesystem
  - After writing, call `web_storage_save()` (from Phase 04) to persist the mod to IndexedDB
  - On subsequent visits, mods persist — the user doesn't need to re-download
  - Create a mod cache manifest file (`mods/.web_cache.json`) that tracks:
    - URL the mod was downloaded from
    - Download timestamp
    - File hash (for cache invalidation)
    - File size
  - Add a function to check if a cached mod is up-to-date before re-downloading

- [ ] Add a mod URL input to the DJUI mod management UI:
  - Read `src/pc/djui/djui_panel_modlist.c` to understand the existing mod list UI
  - For web builds, add a "Load from URL" button to the mod list panel
  - When clicked, show a text input field where users can paste a mod URL
  - On submit, download the mod, add it to the mods directory, and refresh the mod list
  - Show a download progress indicator
  - Show success/error feedback
  - Guard with `#ifdef TARGET_WEB`

- [ ] Create a curated mod browser (optional enhancement):
  - If `mods.sm64coopdx.com` provides an API or structured listing, create a browsable mod catalog
  - Add a "Browse Mods" button to the DJUI mod panel
  - Show a list of available mods with names, descriptions, and download buttons
  - This is lower priority — the URL input from the previous task provides the core functionality
  - If no API exists, this can be a static list of popular mod URLs hardcoded or fetched from a JSON file

- [ ] Test mod loading end-to-end:
  - Upload a simple test Lua mod (e.g., one that changes Mario's speed or adds a HUD message) to a web server or use a known mod URL
  - In the web build:
    - Open the mod list panel
    - Paste the mod URL
    - Verify it downloads and appears in the mod list
    - Enable the mod and restart/reload
    - Verify the mod's effects are active in-game
  - Test with a `.zip` mod pack if the game supports them
  - Test persistence: close the tab, reopen, verify the mod is still loaded from cache
  - Test error handling: try an invalid URL, a 404, a non-mod file
  - Document results in `docs/research/web-mod-loading.md` with front matter:
    - type: report, tags: [mods, lua, web-loading, testing]
