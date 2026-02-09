-- speed_boost.lua
-- Simple test mod: doubles Mario's forward speed
-- Used for testing web mod loading pipeline

local MOD_NAME = "Speed Boost Test"
local MOD_VERSION = "1.0"
local SPEED_MULTIPLIER = 2.0

local function on_mario_update(m)
    if m.playerIndex == 0 then
        m.forwardVel = m.forwardVel * SPEED_MULTIPLIER
    end
end

local function on_hud_render()
    djui_hud_set_resolution(RESOLUTION_N64)
    djui_hud_set_font(FONT_NORMAL)
    djui_hud_set_color(255, 255, 0, 200)
    djui_hud_print_text("Speed Boost Active!", 10, 10, 1.0)
end

hook_event(HOOK_MARIO_UPDATE, on_mario_update)
hook_event(HOOK_ON_HUD_RENDER, on_hud_render)

print("[" .. MOD_NAME .. " v" .. MOD_VERSION .. "] Loaded successfully!")
