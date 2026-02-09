-- hud_message.lua
-- Simple test mod: displays a custom HUD message
-- Used for testing web mod loading pipeline (minimal, no gameplay changes)

local MOD_NAME = "HUD Message Test"
local MOD_VERSION = "1.0"

local function on_hud_render()
    djui_hud_set_resolution(RESOLUTION_N64)
    djui_hud_set_font(FONT_NORMAL)

    -- Green text at top of screen
    djui_hud_set_color(0, 255, 0, 220)
    djui_hud_print_text("Web Mod Loaded!", 100, 20, 1.5)

    -- Smaller info text
    djui_hud_set_color(200, 200, 200, 180)
    djui_hud_print_text(MOD_NAME .. " v" .. MOD_VERSION, 100, 50, 0.8)
end

hook_event(HOOK_ON_HUD_RENDER, on_hud_render)

print("[" .. MOD_NAME .. " v" .. MOD_VERSION .. "] Loaded successfully!")
