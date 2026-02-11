--
-- postprocess_controller.lua
--
-- Dynamic post-process effects driven by game state.
-- Modifies r_ui_warp and r_ui_chromatic cvars in real-time
-- to create screen distortion on damage, weapon fire, quad,
-- and low health.
--
-- Effects are additive on top of base cvar values captured at init.
-- Only calls cvar_set when the computed value actually changes.
--

PostProcess = {}

-- Base cvar values (captured on first frame)
PostProcess.base_warp     = nil
PostProcess.base_chromatic = nil
PostProcess.base_echo      = nil
PostProcess.base_echo_scale = nil

-- Previous game state for rising-edge detection
PostProcess.prev_pain  = false
PostProcess.prev_flash = false

-- Effect timers (engine time when effect started, nil = inactive)
PostProcess.damage_time = nil
PostProcess.fire_time   = nil
PostProcess.quad_expire_time = nil
PostProcess.prev_quad   = false
PostProcess.face_pain_rearm_frames = 0

-- Last values written to cvars (dirty tracking)
PostProcess.last_warp     = nil
PostProcess.last_chromatic = nil
PostProcess.last_echo      = nil

-- Effect durations (seconds)
PostProcess.DAMAGE_DURATION = 0.4
PostProcess.FIRE_DURATION   = 0.15
PostProcess.QUAD_FADE_DURATION = 0.4

-- Effect intensities
PostProcess.DAMAGE_WARP     = -0.15
PostProcess.DAMAGE_CHROMATIC =  0.012
PostProcess.FIRE_CHROMATIC   =  0.003
PostProcess.QUAD_CHROMATIC   =  0.004
PostProcess.LOW_HP_WARP      = -0.1   -- at 0 hp, scales linearly 0..25

----------------------------------------------------------------
-- Helpers
----------------------------------------------------------------

local function lerp_decay (start_time, duration, now)
	local elapsed = now - start_time
	if elapsed >= duration then return 0 end
	local t = elapsed / duration
	return 1.0 - t
end

local function format_val (v)
	return string.format ("%.6f", v)
end

local function get_hud_document ()
	local ctx = rmlui.contexts["main"]
	if not ctx then return nil end
	return ctx.documents["hud"]
end

----------------------------------------------------------------
-- Frame callback
----------------------------------------------------------------

function PostProcess.Think ()
	local g = game
	if not g then return end

	local now = engine.time ()

	-- Init base values on first frame
	if PostProcess.base_warp == nil then
		PostProcess.base_warp       = engine.cvar_get_number ("r_ui_warp")
		PostProcess.base_chromatic  = engine.cvar_get_number ("r_ui_chromatic")
		PostProcess.base_echo       = engine.cvar_get_number ("r_ui_echo")
		PostProcess.base_echo_scale = engine.cvar_get_number ("r_ui_echo_scale")
		PostProcess.last_warp       = PostProcess.base_warp
		PostProcess.last_chromatic  = PostProcess.base_chromatic
		PostProcess.last_echo       = PostProcess.base_echo
		return
	end

	-- Echo off when HUD is not visible (menus, console)
	local hud_up = engine.hud_visible ()
	if not hud_up then
		if PostProcess.last_echo ~= 0 then
			engine.cvar_set ("r_ui_echo", "0")
			PostProcess.last_echo = 0
		end
	elseif PostProcess.last_echo == 0 and PostProcess.base_echo > 0 then
		engine.cvar_set ("r_ui_echo", format_val (PostProcess.base_echo))
		PostProcess.last_echo = PostProcess.base_echo
	end

	-- Skip during intermission or when dead
	if g.intermission or g.health <= 0 then
		-- Restore base values if we were mid-effect
		if PostProcess.last_warp ~= PostProcess.base_warp
			or PostProcess.last_chromatic ~= PostProcess.base_chromatic then
			engine.cvar_set ("r_ui_warp", format_val (PostProcess.base_warp))
			engine.cvar_set ("r_ui_chromatic", format_val (PostProcess.base_chromatic))
			PostProcess.last_warp     = PostProcess.base_warp
			PostProcess.last_chromatic = PostProcess.base_chromatic
		end
		PostProcess.damage_time = nil
		PostProcess.fire_time   = nil
		return
	end

	-- Detect rising edges
	local pain = g.face_pain
	if pain and not PostProcess.prev_pain then
		PostProcess.damage_time = now

		-- Retrigger lab_hud.rcss .face-pain animation.
		local hud_doc = get_hud_document ()
		if hud_doc then
			hud_doc:SetClass ("face-pain", false)
			PostProcess.face_pain_rearm_frames = 2
		end
	end
	PostProcess.prev_pain = pain

	if PostProcess.face_pain_rearm_frames > 0 then
		PostProcess.face_pain_rearm_frames = PostProcess.face_pain_rearm_frames - 1
		if PostProcess.face_pain_rearm_frames == 0 then
			local hud_doc = get_hud_document ()
			if hud_doc then
				hud_doc:SetClass ("face-pain", true)
			end
		end
	end

	local flash = g.fire_flash
	if flash and not PostProcess.prev_flash then
		PostProcess.fire_time = now
	end
	PostProcess.prev_flash = flash

	-- Compute additive deltas
	local warp_delta     = 0
	local chromatic_delta = 0

	-- Damage hit: decaying warp + chromatic
	if PostProcess.damage_time then
		local intensity = lerp_decay (PostProcess.damage_time, PostProcess.DAMAGE_DURATION, now)
		if intensity > 0 then
			warp_delta     = warp_delta     + PostProcess.DAMAGE_WARP     * intensity
			chromatic_delta = chromatic_delta + PostProcess.DAMAGE_CHROMATIC * intensity
		else
			PostProcess.damage_time = nil
		end
	end

	-- Weapon fire: decaying chromatic
	if PostProcess.fire_time then
		local intensity = lerp_decay (PostProcess.fire_time, PostProcess.FIRE_DURATION, now)
		if intensity > 0 then
			chromatic_delta = chromatic_delta + PostProcess.FIRE_CHROMATIC * intensity
		else
			PostProcess.fire_time = nil
		end
	end

	-- Quad damage: continuous sine pulse on chromatic, with fade-out on expiry
	if g.has_quad then
		local pulse = (math.sin (now * 6.0) + 1.0) * 0.5  -- 0..1 at ~1Hz
		chromatic_delta = chromatic_delta + PostProcess.QUAD_CHROMATIC * pulse
		PostProcess.quad_expire_time = nil
	elseif PostProcess.prev_quad then
		-- Quad just expired — start fade-out
		PostProcess.quad_expire_time = now
	end
	PostProcess.prev_quad = g.has_quad

	if PostProcess.quad_expire_time then
		local intensity = lerp_decay (PostProcess.quad_expire_time, PostProcess.QUAD_FADE_DURATION, now)
		if intensity > 0 then
			chromatic_delta = chromatic_delta + PostProcess.QUAD_CHROMATIC * intensity
		else
			PostProcess.quad_expire_time = nil
		end
	end

	-- Low health: continuous warp scaling linearly below 25 hp
	if g.health < 25 then
		local factor = 1.0 - (g.health / 25.0)
		warp_delta = warp_delta + PostProcess.LOW_HP_WARP * factor
	end

	-- Compute final values
	local final_warp     = PostProcess.base_warp     + warp_delta
	local final_chromatic = PostProcess.base_chromatic + chromatic_delta

	-- Only write cvars when value actually changed
	local str_warp     = format_val (final_warp)
	local str_chromatic = format_val (final_chromatic)

	if str_warp ~= format_val (PostProcess.last_warp) then
		engine.cvar_set ("r_ui_warp", str_warp)
		PostProcess.last_warp = final_warp
	end

	if str_chromatic ~= format_val (PostProcess.last_chromatic) then
		engine.cvar_set ("r_ui_chromatic", str_chromatic)
		PostProcess.last_chromatic = final_chromatic
	end
end

----------------------------------------------------------------
-- Self-test
----------------------------------------------------------------

function PostProcess.SelfTest ()
	local pass = true

	-- Verify decay function
	local v = lerp_decay (0, 1.0, 0.5)
	if math.abs (v - 0.5) > 0.001 then
		print ("FAIL: lerp_decay(0, 1.0, 0.5) = " .. v .. " (expected 0.5)")
		pass = false
	end

	v = lerp_decay (0, 1.0, 1.0)
	if v ~= 0 then
		print ("FAIL: lerp_decay at duration should be 0, got " .. v)
		pass = false
	end

	v = lerp_decay (0, 1.0, 0)
	if math.abs (v - 1.0) > 0.001 then
		print ("FAIL: lerp_decay at start should be 1.0, got " .. v)
		pass = false
	end

	-- Verify format precision
	local s = format_val (0.123456789)
	if s ~= "0.123457" then
		print ("FAIL: format_val precision, got " .. s)
		pass = false
	end

	if pass then
		print ("PostProcess.SelfTest: PASS — decay and format functions valid")
	end
	return pass
end

----------------------------------------------------------------
-- Register frame callback
----------------------------------------------------------------

engine.on_frame ("postprocess", PostProcess.Think)
