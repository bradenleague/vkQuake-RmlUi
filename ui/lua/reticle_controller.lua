--
-- reticle_controller.lua
--
-- Per-weapon reticle animations driven by Lua.
-- Toggles CSS classes on the crosshair container so the RCSS transition
-- engine handles interpolation.  Idle state removes all firing/equipping
-- classes so hud.rcss base rules are the source of truth.
--
-- Signals:
--   weapon_firing  — true while weaponframe != 0 (weapon mid-animation).
--                    Stays true during continuous fire (nailgun, LG).
--                    Goes false when the weapon returns to idle frame.
--   weapon_show    — true during weapon raise animation.
--

Reticle = {}

-- Map weapon bitflags to CSS class names.
-- Classes defined in hud.rcss as .crosshair.firing-XX rules.
Reticle.fire_classes = {
	[1]  = "firing-sg",   -- IT_SHOTGUN
	[2]  = "firing-ssg",  -- IT_SUPER_SHOTGUN
	[4]  = "firing-ng",   -- IT_NAILGUN
	[8]  = "firing-sng",  -- IT_SUPER_NAILGUN
	[16] = "firing-gl",   -- IT_GRENADE_LAUNCHER
	[32] = "firing-rl",   -- IT_ROCKET_LAUNCHER
	[64] = "firing-lg",   -- IT_LIGHTNING
}

-- All firing class names (for bulk removal)
Reticle.all_fire_classes = {
	"firing-sg", "firing-ssg", "firing-ng", "firing-sng",
	"firing-gl", "firing-rl", "firing-lg",
}

-- State tracking
Reticle.prev_firing = false
Reticle.prev_equip  = false
Reticle.prev_weapon = 0
Reticle.active_class = nil  -- currently applied firing class

----------------------------------------------------------------
-- Helpers
----------------------------------------------------------------

local function get_container ()
	local ctx = rmlui.contexts["main"]
	if not ctx then return nil end
	local doc = ctx.documents["hud"]
	if not doc then return nil end
	return doc:GetElementById ("crosshair-container")
end

local function clear_fire_classes (container)
	for _, cls in ipairs (Reticle.all_fire_classes) do
		container:SetClass (cls, false)
	end
	Reticle.active_class = nil
end

----------------------------------------------------------------
-- Apply functions
----------------------------------------------------------------

function Reticle.ApplyFiring (container, weapon)
	-- Remove previous weapon's class if different
	if Reticle.active_class then
		container:SetClass (Reticle.active_class, false)
	end

	local cls = Reticle.fire_classes[weapon] or "firing-sg"
	container:SetClass (cls, true)
	container:SetClass ("equipping", false)
	Reticle.active_class = cls
end

function Reticle.ApplyEquipping (container)
	clear_fire_classes (container)
	container:SetClass ("equipping", true)
end

function Reticle.ApplyIdle (container)
	clear_fire_classes (container)
	container:SetClass ("equipping", false)
end

----------------------------------------------------------------
-- Frame callback
----------------------------------------------------------------

function Reticle.Think ()
	local g = game
	if not g then return end

	local firing = g.weapon_firing
	local equip  = g.weapon_show
	local weapon = g.active_weapon

	local firing_rising  = (firing and not Reticle.prev_firing)
	local firing_falling = (not firing and Reticle.prev_firing)
	local equip_changed  = (equip ~= Reticle.prev_equip)
	local weapon_changed = (weapon ~= Reticle.prev_weapon)

	-- Only query DOM on state transitions
	local need_update = firing_rising or firing_falling or equip_changed
		or (firing and weapon_changed)

	if need_update then
		local container = get_container ()
		if not container then goto done end

		-- Equip takes priority: collapse during weapon raise
		if equip and equip_changed then
			Reticle.ApplyEquipping (container)
		-- Weapon starts firing, or weapon switch while firing: expand
		elseif firing and (firing_rising or weapon_changed) then
			Reticle.ApplyFiring (container, weapon)
		-- Weapon stops firing: RCSS transition animates back to idle
		elseif firing_falling then
			Reticle.ApplyIdle (container)
		-- Equip ended without fire active: return to idle
		elseif not equip and equip_changed and not firing then
			Reticle.ApplyIdle (container)
		end
	end

	::done::
	Reticle.prev_firing = firing
	Reticle.prev_equip  = equip
	Reticle.prev_weapon = weapon
end

----------------------------------------------------------------
-- Self-test
----------------------------------------------------------------

function Reticle.SelfTest ()
	local expected_weapons = { 1, 2, 4, 8, 16, 32, 64 }
	local pass = true

	for _, w in ipairs (expected_weapons) do
		local cls = Reticle.fire_classes[w]
		if not cls then
			print ("FAIL: missing class for weapon " .. w)
			pass = false
		end
	end

	if pass then
		print ("Reticle.SelfTest: PASS — all 7 weapon classes valid")
	end
	return pass
end

----------------------------------------------------------------
-- Register frame callback
----------------------------------------------------------------

engine.on_frame ("reticle", Reticle.Think)
