-- Boss harness for `PainfulTools lua`, run after Combat.lua with
-- BOSS = '<template>' set. God mode, and the boss worn down in steps so every
-- phase of its script runs. Prints one [boss] line per change of AI goal,
-- state or animation, a position line once a second, and what hit the player.
-- Single quotes only: PowerShell 5.1 strips double quotes from native arguments.
GOD = true
local f = 0

local function R(v) return math.floor(v * 10 + 0.5) / 10 end

local boss
for _, o in Actors do
	if o.BaseObj == BOSS .. '.CActor' or o._Name == BOSS then boss = o end
end
if not boss then
	print('[boss] ' .. BOSS .. ': not found among ' .. table.getn(Actors) .. ' actors')
	return
end
local hp0 = boss.Health
print(string.format('[boss] %s found: health %s, at %s %s %s', BOSS, tostring(hp0),
		R(boss.Pos.X), R(boss.Pos.Y), R(boss.Pos.Z)))

local hits = 0
local realDamage = Player.OnDamage
Player.OnDamage = function(self, damage, owner, attack, ...)
	hits = hits + 1
	if hits <= 20 then
		print(string.format('[boss] f%d player hit: %s by %s, attack %s', f, tostring(damage),
				tostring(owner and (owner.BaseObj or owner._Name)), tostring(attack)))
	end
	return realDamage(self, damage, owner, attack, unpack(arg))
end

local last = ''
local step = 0
local realTick = Game_Tick3
Game_Tick3 = function(d)
	realTick(d)
	f = f + 1
	if not boss._Entity then
		if last ~= 'gone' then print(string.format('[boss] f%d entity gone', f)) end
		last = 'gone'
		return
	end
	local brain = boss._AIBrain
	local goal = brain and brain._currentGoal and brain._goals[brain._currentGoal]
	local now = string.format('goal=%s sub=%s state=%s anim=%s',
			tostring(goal and goal.name), tostring(brain and brain._submode),
			tostring(boss._state), tostring(boss.Animation))
	if now ~= last then
		print(string.format('[boss] f%d %s hp=%s', f, now, tostring(boss.Health)))
		last = now
	end
	if math.mod(f, 60) == 0 then
		local x, y, z = ENTITY.GetPosition(boss._Entity)
		local px, py, pz = ENTITY.GetPosition(Player._Entity)
		print(string.format('[boss] f%d pos %s %s %s  player %s %s %s hp=%s', f, R(x), R(y), R(z),
				R(px), R(py), R(pz), tostring(Player.Health)))
	end
	-- A fifth of the starting health every ten seconds, through the same
	-- OnDamage a weapon calls, so health-gated phases are reached.
	if math.mod(f, 600) == 0 and boss.Health > 0 and hp0 and hp0 > 0 then
		step = step + 1
		local x, y, z = ENTITY.GetPosition(boss._Entity)
		boss:OnDamage(hp0 * 0.2, Player, AttackTypes.Rocket, x, y + 1, z, 0, 1, 0)
		print(string.format('[boss] f%d damage step %d: hp=%s', f, step, tostring(boss.Health)))
	end
end
