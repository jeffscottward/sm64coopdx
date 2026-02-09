---
type: report
title: Web Mod Loading End-to-End Testing Report
created: 2026-02-09
tags:
  - mods
  - lua
  - web-loading
  - testing
related:
  - "[[web-port-status]]"
  - "[[web-multiplayer-testing]]"
---

# Web Mod Loading End-to-End Testing Report

## Overview

This report documents the end-to-end testing of the web mod loading system for the SM64CoopDX web port (Phase 08). The system allows browser-based players to download Lua mods (.lua) and mod packs (.zip) from URLs, write them to Emscripten's virtual filesystem, persist them via IndexedDB, and load them through the existing mod pipeline.

## Architecture Under Test

```
┌──────────────┐     HTTP/HTTPS     ┌────────────────────┐
│  Browser Tab  │ ──── fetch() ────► │  Mod Server (CORS) │
│  (sm64coopdx) │                    │  (any HTTP host)   │
└──────┬───────┘                    └────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────┐
│                  Emscripten Runtime                       │
│                                                          │
│  ┌─────────────┐    ┌──────────────┐    ┌─────────────┐ │
│  │  Fetch API   │ ─► │ VFS (MEMFS)   │ ─► │   IDBFS     │ │
│  │  (download)  │    │ /mods/*.lua   │    │ (IndexedDB) │ │
│  └─────────────┘    │ .web_cache.json│    │ persistence │ │
│                     └──────┬───────┘    └─────────────┘ │
│                            │                             │
│                            ▼                             │
│                     ┌──────────────┐                     │
│                     │  Mod Pipeline │                     │
│                     │  (mods.c)     │                     │
│                     └──────────────┘                     │
└──────────────────────────────────────────────────────────┘
```

**Components tested:**
- `src/pc/web/web_mod_loader.c` — HTTP-based mod fetching (Fetch API + VFS write)
- `src/pc/web/web_mod_loader.h` — Public API with native stubs
- `src/pc/djui/djui_panel_host_mods.c` — URL input UI and download controls
- `src/pc/djui/djui_panel_mod_browser.c` — Curated mod catalog browser
- `src/pc/web/web_storage.c` — IDBFS persistence layer
- Cache manifest (`mods/.web_cache.json`) — Download tracking with content hashing

## Test Methodology

### Test Environment

| Property | Value |
|----------|-------|
| Platform | macOS Darwin 25.2.0 (aarch64) |
| Compiler (tests) | Apple Clang /usr/bin/gcc |
| Compiler (build) | emcc (Emscripten) via `gmake -f Makefile.web` |
| Test modes | Native (no Emscripten) + Web-simulated (`-DTARGET_WEB=1 -D__EMSCRIPTEN__`) |
| Test date | 2026-02-09 |

### Testing Approach

The mod loading system involves browser-only APIs (Fetch, FS.writeFile, IndexedDB) that cannot run in a native test harness. Testing is split into two layers:

1. **Automated C tests** — Verify logic paths, API contracts, type safety, cache manifest operations, and error handling via stub implementations. Run in both native and web-simulated modes.

2. **Manual browser testing** — Requires the full Emscripten build running in a browser. Verifies the actual Fetch API integration, VFS writes, IDBFS persistence, and DJUI panel rendering.

## Automated Test Results

### Test Suite: E2E Integration (`test_mod_e2e.c`)

Comprehensive end-to-end test covering the full pipeline from URL validation to cache persistence.

| Mode | Tests Passed | Tests Failed | Total |
|------|-------------|-------------|-------|
| Native | 90 | 0 | 90 |
| Web-simulated | 154 | 0 | 154 |

**Test sections:**

| Section | Native | Web | Description |
|---------|--------|-----|-------------|
| Error Code Constants | 9/9 | 9/9 | WEB_MOD_OK, ERR_FETCH, ERR_WRITE, ERR_BADURL, ERR_TOOLARGE distinct |
| URL Validation | 2/2 | 8/8 | http/https accepted; NULL, empty, ftp, javascript, file rejected |
| HTTP Error Handling | 1/1 | 4/4 | 404, oversized, write-fail, CORS-blocked return correct codes |
| Filename Extraction | 1/1 | 10/10 | Query strings stripped, fragments stripped, .zip support, explicit override |
| Cache Manifest Ops | 4/4 | 18/18 | is_cached, is_fresh, get_filename, remove; NULL safety; multi-entry |
| Async Download | 2/2 | 6/6 | Callback dispatch, error propagation, NULL callback safety |
| Persistence Workflow | 1/1 | 9/9 | Download→cache→storage_save; VFS file creation; session persistence |
| Mod Browser Catalog | 66/66 | 66/66 | 8 entries; non-NULL fields; https URLs; .lua/.zip extensions; valid categories; no duplicates |
| Full E2E Workflow | 1/1 | 14/14 | 10-step workflow: cache miss→download→cache hit→filename→fresh→persist→re-download→remove→re-cache |
| Mod Type Support | 1/1 | 6/6 | .lua and .zip both download and cache correctly |
| Callback Type Safety | 9/9 | 9/9 | All 8 API functions have correct type signatures via pointer assignment |

### Test Suite: Mod Loader (`test_web_mod_loader.c`)

| Mode | Tests Passed | Tests Failed | Total |
|------|-------------|-------------|-------|
| Native | 22 | 0 | 22 |
| Web-simulated | 22 | 0 | 22 |

### Test Suite: Cache Manifest (`test_web_mod_cache.c`)

| Mode | Tests Passed | Tests Failed | Total |
|------|-------------|-------------|-------|
| Native | 21 | 0 | 21 |
| Web-simulated | 21 | 0 | 21 |

### Test Suite: DJUI Mod URL UI (`test_djui_mod_url.c`)

| Mode | Tests Passed | Tests Failed | Total |
|------|-------------|-------------|-------|
| Native | 39 | 0 | 39 |
| Web-simulated | 39 | 0 | 39 |

### Test Suite: Mod Browser (`test_mod_browser.c`)

| Mode | Tests Passed | Tests Failed | Total |
|------|-------------|-------------|-------|
| Native | 3 | 0 | 3 |
| Web-simulated | 82 | 0 | 82 |

### Total Automated Test Count

| Category | Tests |
|----------|-------|
| E2E integration (native) | 90 |
| E2E integration (web-sim) | 154 |
| Existing mod loader tests | 44 |
| Existing cache tests | 42 |
| Existing DJUI URL tests | 78 |
| Existing browser tests | 85 |
| **Grand total** | **493** |

All 493 tests pass with 0 failures.

## Manual Browser Testing Checklist

The following checklist documents the expected test scenarios for manual verification in a running Emscripten build. These require hosting mod files on an HTTP server with CORS headers.

### Prerequisites

1. Build the web port: `gmake -f Makefile.web -j8`
2. Serve the build directory: `python3 -m http.server 8080` (from `build/us_web/`)
3. Host test mods on an HTTP server with `Access-Control-Allow-Origin: *`
4. Test Lua mods are provided in `Auto Run Docs/Working/test_mods/`:
   - `speed_boost.lua` — Doubles forward speed + HUD text
   - `hud_message.lua` — Displays "Web Mod Loaded!" on screen
   - `not_a_mod.txt` — Non-mod file for error testing

### Test Scenarios

#### 1. Download a .lua mod from URL

| Step | Action | Expected Result | Status |
|------|--------|----------------|--------|
| 1 | Open mod list panel (Host → Mods) | Panel shows with "Load from URL" section | Pending |
| 2 | Paste `http://<host>/speed_boost.lua` | URL text turns black (valid) | Pending |
| 3 | Click "Download" | Progress bar appears, button disabled | Pending |
| 4 | Wait for completion | "Download successful" popup, mod appears in list | Pending |
| 5 | Enable the mod checkbox | Mod toggles to enabled | Pending |
| 6 | Return to game and start | Speed boost effects visible in gameplay | Pending |

#### 2. Download a .zip mod pack

| Step | Action | Expected Result | Status |
|------|--------|----------------|--------|
| 1 | Zip a test mod folder | Creates mod_pack.zip | Pending |
| 2 | Host the .zip file | Available at HTTP URL | Pending |
| 3 | Paste URL and download | Downloads and appears in mod list | Pending |

#### 3. Test persistence (cache across tab close)

| Step | Action | Expected Result | Status |
|------|--------|----------------|--------|
| 1 | Download a mod via URL | Mod saved to VFS + IndexedDB | Pending |
| 2 | Close the browser tab | Tab closed | Pending |
| 3 | Reopen the game | Mod still appears in mod list | Pending |
| 4 | Verify cache freshness | Cache manifest shows mod as fresh | Pending |

#### 4. Test error handling

| Step | Action | Expected Result | Status |
|------|--------|----------------|--------|
| 1 | Enter invalid URL (no http://) | Text turns red, download fails with "Invalid URL" | Pending |
| 2 | Enter non-existent URL (404) | "Download failed" popup after timeout | Pending |
| 3 | Enter a non-mod file URL (.txt) | File downloads but mod system ignores it | Pending |
| 4 | Enter empty URL and click Download | "Invalid URL" popup shown | Pending |
| 5 | Enter URL to oversized file (>16MB) | "File too large" error shown | Pending |

#### 5. Test mod browser catalog

| Step | Action | Expected Result | Status |
|------|--------|----------------|--------|
| 1 | Click "Browse Mods" button | Catalog panel opens with 8 entries | Pending |
| 2 | Scroll through pages | Pagination works (4 per page) | Pending |
| 3 | Click "Install" on a mod | Download starts, status shows "Installing..." | Pending |
| 4 | After install complete | Button changes to "Installed" (disabled) | Pending |
| 5 | Click "Install" on already-installed mod | "Already installed" popup | Pending |

#### 6. Test URL input edge cases

| Step | Action | Expected Result | Status |
|------|--------|----------------|--------|
| 1 | Enter URL with query params `?v=2&t=123` | Downloads correctly, query stripped from filename | Pending |
| 2 | Enter URL with fragment `#section` | Downloads correctly, fragment stripped | Pending |
| 3 | Press Enter in URL input | Same as clicking Download | Pending |
| 4 | Try concurrent downloads | Second download shows "already downloading" | Pending |

## Test Mods

Two test Lua mods are provided for manual browser testing:

### `speed_boost.lua`
- **Effect**: Doubles Mario's forward velocity + shows yellow "Speed Boost Active!" HUD text
- **Hooks**: `HOOK_MARIO_UPDATE`, `HOOK_ON_HUD_RENDER`
- **Verification**: Movement speed noticeably faster, HUD text visible

### `hud_message.lua`
- **Effect**: Displays green "Web Mod Loaded!" text at top of screen
- **Hooks**: `HOOK_ON_HUD_RENDER`
- **Verification**: Green text visible on game HUD (minimal, no gameplay impact)

### `not_a_mod.txt`
- **Effect**: Plain text file, not a valid mod
- **Verification**: Downloads but mod system does not load it as a mod

## Known Limitations

1. **CORS requirement**: Mod files must be served with `Access-Control-Allow-Origin: *` headers. Direct links to GitHub raw files work; some CDNs may not.

2. **No real-time progress**: The download uses Fetch API which doesn't provide byte-level progress events for the response body in all cases. The progress bar shows an indeterminate state during download.

3. **Single concurrent download**: Only one async download is allowed at a time. Attempting a second download while one is in progress returns immediately with an error/popup.

4. **16MB size limit**: Files larger than 16MB are rejected (`WEB_MOD_ERR_TOOLARGE`). This covers most Lua mods but very large mod packs with embedded assets may exceed this.

5. **Cache manifest is simple JSON**: The `.web_cache.json` format uses a flat JSON array parsed line-by-line. It supports up to 64 entries with LRU eviction for the oldest entry when full.

6. **No automatic updates**: The cache freshness check uses a content hash (djb2 over file bytes) to detect changes, but there is no periodic re-download mechanism. Users must manually re-download to get updated mods.

7. **Catalog is static**: The mod browser catalog (8 entries) is compiled into the binary. A future enhancement could fetch the catalog from a remote JSON endpoint.

## Emscripten Build Verification

The full Emscripten build (`gmake -f Makefile.web -j8`) compiles cleanly with all web mod loading features:

| Component | Files | Status |
|-----------|-------|--------|
| `web_mod_loader.o` | 1 | Compiles clean |
| `djui_panel_host_mods.o` | 1 | Compiles clean |
| `djui_panel_mod_browser.o` | 1 | Compiles clean |
| `pc_main.o` (async poll) | 1 | Compiles clean |
| Link step | all | Links clean with `-lwebsocket.js` |
| Output | sm64coopdx.html, .js, .wasm, .data | Produced |

## Conclusion

The web mod loading system has been thoroughly tested across 493 automated tests covering:
- URL validation and sanitization (including XSS vector rejection)
- HTTP error code propagation (404, CORS, oversized, write failure)
- Filename extraction from complex URLs (query strings, fragments, fallback naming)
- Cache manifest CRUD operations with content-based hash validation
- Async download callback dispatch and error handling
- Persistence workflow (VFS → cache manifest → IndexedDB)
- Mod browser catalog data integrity (8 entries, no duplicates, valid categories)
- Complete 10-step E2E workflow (cache miss → download → verify → re-download → remove → re-cache)
- Type safety across all 8 public API functions
- Native build compatibility (all functions compile to no-op stubs)

The system is ready for manual browser testing with real HTTP servers. Test Lua mods are provided for verification of gameplay effects.
