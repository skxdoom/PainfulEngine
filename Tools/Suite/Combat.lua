-- Combat harness for `PainfulTools lua`: every weapon, full ammo, and the fire
-- bits PULSED. CPlayer's _waitingForFirstFire needs one frame with Fire clear
-- before it will shoot, so holding the bit would never fire at all.
for k, v in Player.Ammo do Player.Ammo[k] = 999 end
for i = 1, 7 do Player:AddWeapon(i) end
local f = 0
INP.GetActionStatus = function(e)
	f = f + 1
	local a = 0
	if math.mod(f, 20) < 10 then a = a + Actions.Fire end
	if math.mod(f, 240) < 2 then a = a + Actions.NextWeapon end
	if math.mod(f, 20) >= 15 then a = a + Actions.AltFire end
	return a
end
