/**
 * djui_panel_mod_browser.c -- Curated mod catalog browser for web builds.
 *
 * Displays a paginated list of popular/recommended mods. Each entry shows
 * the mod name, a short description, and a Download button. Mods already
 * present in the local cache show "Installed" status.
 *
 * The catalog is a static C array embedded at compile time. A future
 * enhancement could fetch the catalog from a remote JSON endpoint.
 */

#ifdef TARGET_WEB

#include <stdio.h>
#include <string.h>
#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_mod_browser.h"
#include "pc/web/web_mod_loader.h"
#include "pc/mods/mods.h"
#include "pc/mods/mods_utils.h"

/* ------------------------------------------------------------------ */
/* Curated mod catalog                                                 */
/* ------------------------------------------------------------------ */

/**
 * Static catalog of popular mods.
 *
 * These are example entries. In practice, the URLs would point to
 * real mod files hosted on a server with CORS headers enabled.
 * The catalog can be updated by editing this array.
 */
static const struct ModBrowserEntry sCatalog[] = {
    {
        "Extended Moveset",
        "Adds wall-slides, ground-pounds, and more advanced moves",
        "https://mods.sm64coopdx.com/mods/extended-moveset.lua",
        "moveset"
    },
    {
        "Character Select",
        "Choose from many custom characters with unique abilities",
        "https://mods.sm64coopdx.com/mods/char-select.lua",
        "cs"
    },
    {
        "Arena",
        "Competitive arena gamemode with multiple maps",
        "https://mods.sm64coopdx.com/mods/arena.lua",
        "gamemode"
    },
    {
        "Hide and Seek",
        "Classic hide and seek with timer and scoring",
        "https://mods.sm64coopdx.com/mods/hide-and-seek.lua",
        "gamemode"
    },
    {
        "Gun Mod",
        "Adds ranged weapons and projectiles to the game",
        "https://mods.sm64coopdx.com/mods/gun-mod.lua",
        "moveset"
    },
    {
        "Day Night Cycle",
        "Dynamic day/night cycle with lighting changes",
        "https://mods.sm64coopdx.com/mods/day-night-cycle.lua",
        "romhack"
    },
    {
        "Custom Music",
        "Replaces soundtrack with remixed versions",
        "https://mods.sm64coopdx.com/mods/custom-music.zip",
        "romhack"
    },
    {
        "Nametags+",
        "Enhanced nametags with health bars and distance display",
        "https://mods.sm64coopdx.com/mods/nametags-plus.lua",
        "misc"
    },
};

static const int sCatalogCount = sizeof(sCatalog) / sizeof(sCatalog[0]);

const struct ModBrowserEntry* mod_browser_get_catalog(int* count) {
    if (count) *count = sCatalogCount;
    return sCatalog;
}

/* ------------------------------------------------------------------ */
/* Panel state                                                         */
/* ------------------------------------------------------------------ */

static struct DjuiFlowLayout* sBrowserLayout = NULL;
static struct DjuiPaginated* sBrowserPaginated = NULL;
static struct DjuiText* sBrowserStatusText = NULL;
static int sBrowserDownloadingIndex = -1; /* -1 = no download in progress */

/* Forward declarations */
static void djui_panel_mod_browser_add_entries(struct DjuiBase* layoutBase);
static void djui_panel_mod_browser_destroy(struct DjuiBase* base);

/* ------------------------------------------------------------------ */
/* Download handling                                                   */
/* ------------------------------------------------------------------ */

static void browser_download_complete(int status) {
    int idx = sBrowserDownloadingIndex;
    sBrowserDownloadingIndex = -1;

    const char* modName = (idx >= 0 && idx < sCatalogCount) ? sCatalog[idx].name : "Mod";

    switch (status) {
        case WEB_MOD_OK:
            if (sBrowserStatusText) {
                djui_text_set_text(sBrowserStatusText, DLANG(MOD_BROWSER, INSTALL_SUCCESS));
                djui_base_set_color(&sBrowserStatusText->base, 100, 255, 100, 255);
            }
            djui_popup_create(DLANG(MOD_BROWSER, INSTALL_SUCCESS), 2);
            /* Refresh the mod system so the new mod is recognized */
            mods_refresh_local();
            mods_update_selectable();
            /* Rebuild the browser entries to update "Installed" status */
            if (sBrowserLayout) {
                djui_base_destroy_children(&sBrowserLayout->base);
                djui_panel_mod_browser_add_entries(&sBrowserLayout->base);
                if (sBrowserPaginated) {
                    djui_paginated_calculate_height(sBrowserPaginated);
                }
            }
            break;
        case WEB_MOD_ERR_BADURL:
            if (sBrowserStatusText) {
                djui_text_set_text(sBrowserStatusText, DLANG(MOD_BROWSER, INSTALL_BAD_URL));
                djui_base_set_color(&sBrowserStatusText->base, 255, 100, 100, 255);
            }
            djui_popup_create(DLANG(MOD_BROWSER, INSTALL_BAD_URL), 2);
            break;
        case WEB_MOD_ERR_TOOLARGE:
            if (sBrowserStatusText) {
                djui_text_set_text(sBrowserStatusText, DLANG(MOD_BROWSER, INSTALL_TOO_LARGE));
                djui_base_set_color(&sBrowserStatusText->base, 255, 100, 100, 255);
            }
            djui_popup_create(DLANG(MOD_BROWSER, INSTALL_TOO_LARGE), 2);
            break;
        default:
            if (sBrowserStatusText) {
                djui_text_set_text(sBrowserStatusText, DLANG(MOD_BROWSER, INSTALL_FAILED));
                djui_base_set_color(&sBrowserStatusText->base, 255, 100, 100, 255);
            }
            djui_popup_create(DLANG(MOD_BROWSER, INSTALL_FAILED), 2);
            break;
    }
    (void)modName;
}

static void browser_entry_download_click(struct DjuiBase* caller) {
    if (sBrowserDownloadingIndex >= 0) {
        /* A download is already in progress */
        djui_popup_create(DLANG(MOD_BROWSER, ALREADY_DOWNLOADING), 2);
        return;
    }

    int idx = (int)caller->tag;
    if (idx < 0 || idx >= sCatalogCount) return;

    const struct ModBrowserEntry* entry = &sCatalog[idx];

    /* Check if already cached */
    if (web_mod_is_cached(entry->url)) {
        djui_popup_create(DLANG(MOD_BROWSER, ALREADY_INSTALLED), 2);
        return;
    }

    sBrowserDownloadingIndex = idx;

    if (sBrowserStatusText) {
        djui_text_set_text(sBrowserStatusText, DLANG(MOD_BROWSER, INSTALLING));
        djui_base_set_color(&sBrowserStatusText->base, 220, 220, 220, 255);
    }

    /* Start async download */
    web_mod_download_async(entry->url, NULL, browser_download_complete);
}

/* ------------------------------------------------------------------ */
/* Entry rendering                                                     */
/* ------------------------------------------------------------------ */

static void djui_panel_mod_browser_add_entries(struct DjuiBase* layoutBase) {
    for (int i = 0; i < sCatalogCount; i++) {
        const struct ModBrowserEntry* entry = &sCatalog[i];

        /* Each entry is a rect container with name + description + button */
        struct DjuiRect* row = djui_rect_container_create(layoutBase, 64);
        {
            /* Mod name (top-left, bold-ish via color) */
            struct DjuiText* nameText = djui_text_create(&row->base, entry->name);
            djui_base_set_size_type(&nameText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&nameText->base, 0.65f, 28);
            djui_base_set_alignment(&nameText->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
            djui_base_set_color(&nameText->base, 255, 255, 200, 255);
            djui_text_set_alignment(nameText, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
            djui_text_set_drop_shadow(nameText, 64, 64, 64, 100);

            /* Description (bottom-left, smaller text) */
            struct DjuiText* descText = djui_text_create(&row->base, entry->description);
            djui_base_set_size_type(&descText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
            djui_base_set_size(&descText->base, 0.65f, 32);
            djui_base_set_alignment(&descText->base, DJUI_HALIGN_LEFT, DJUI_VALIGN_BOTTOM);
            djui_base_set_color(&descText->base, 180, 180, 180, 255);
            djui_text_set_alignment(descText, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
            djui_text_set_drop_shadow(descText, 64, 64, 64, 100);

            /* Download / Installed button (right side) */
            bool cached = web_mod_is_cached(entry->url) != 0;
            const char* btnLabel = cached
                ? DLANG(MOD_BROWSER, INSTALLED)
                : DLANG(MOD_BROWSER, INSTALL);

            struct DjuiButton* btn = djui_button_create(
                &row->base, btnLabel,
                DJUI_BUTTON_STYLE_NORMAL,
                browser_entry_download_click
            );
            djui_base_set_size(&btn->base, 0.30f, 32);
            djui_base_set_alignment(&btn->base, DJUI_HALIGN_RIGHT, DJUI_VALIGN_CENTER);
            btn->base.tag = i;

            if (cached) {
                djui_base_set_enabled(&btn->base, false);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Panel create / destroy                                              */
/* ------------------------------------------------------------------ */

static void djui_panel_mod_browser_destroy(struct DjuiBase* base) {
    struct DjuiThreePanel* threePanel = (struct DjuiThreePanel*)base;
    free(threePanel);
    sBrowserLayout = NULL;
    sBrowserPaginated = NULL;
    sBrowserStatusText = NULL;
    sBrowserDownloadingIndex = -1;
}

void djui_panel_mod_browser_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(
        DLANG(MOD_BROWSER, TITLE), false);

    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        /* Info text */
        struct DjuiText* info = djui_text_create(body, DLANG(MOD_BROWSER, DESCRIPTION));
        djui_base_set_size_type(&info->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&info->base, 1.0f, 32);
        djui_base_set_color(&info->base, 200, 200, 255, 255);
        djui_text_set_alignment(info, DJUI_HALIGN_LEFT, DJUI_VALIGN_TOP);
        djui_text_set_drop_shadow(info, 64, 64, 64, 100);

        /* Paginated list of mods */
        struct DjuiPaginated* paginated = djui_paginated_create(body, 4);
        paginated->showMaxCount = true;
        sBrowserLayout = paginated->layout;
        sBrowserPaginated = paginated;

        djui_panel_mod_browser_add_entries(&paginated->layout->base);
        djui_paginated_calculate_height(paginated);

        /* Status text for download feedback */
        sBrowserStatusText = djui_text_create(body, "");
        djui_base_set_size_type(&sBrowserStatusText->base, DJUI_SVT_RELATIVE, DJUI_SVT_ABSOLUTE);
        djui_base_set_size(&sBrowserStatusText->base, 1.0f, 20);
        djui_base_set_color(&sBrowserStatusText->base, 220, 220, 220, 255);
        djui_text_set_drop_shadow(sBrowserStatusText, 64, 64, 64, 100);

        /* Back button */
        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK,
            djui_panel_menu_back);

        panel->bodySize.value = paginated->base.height.value + 64 + 64 + 32;
    }

    panel->base.destroy = djui_panel_mod_browser_destroy;

    djui_panel_add(caller, panel, NULL);
}

#endif /* TARGET_WEB */
