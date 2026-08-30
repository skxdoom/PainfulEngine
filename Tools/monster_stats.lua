-- Spawns a squad of monsters in front of the player and reports what they do.
-- Run it as the `lua` command's exec chunk:
--
--   CHUNK=$(grep -v "^\s*--" Tools/monster_stats.lua | tr '\n' ' ')
--   PainfulEngine lua <DataRoot> 1400 C1L1_Cathedral "$CHUNK"
--
-- Almost nothing the AI does happens away from a player, so the interesting
-- behaviour cannot be reached from a level's own spawn point. This wraps
-- Game_Tick, which is the general trick for reading live script state
-- headlessly.
--
-- Two things worth knowing before editing it:
--   * The chunk is handed to Lua as ONE LINE, so it must survive having its
--     newlines stripped - hence the grep above, and no trailing `--` comments.
--   * Per-actor flags have to be sampled EVERY tick, not at report time.
--     Visibility is recomputed probabilistically (aiParams.updateSpeed), so
--     `_seeEnemy` is true only in brief windows and a periodic sample reports
--     zero however often the actor actually saw the player.

local old = Game_Tick
local n = 0
local mob = nil
local rec = nil
local COUNT = 16

local function census()
  local withPO, without, names = 0, 0, ""
  for i,v in Actors do
    if v.AiParams then
      if ENTITY.PO_Exist(v._Entity) then withPO = withPO + 1
      else
        without = without + 1
        if without <= 4 then names = names.." "..tostring(v._Name) end
      end
    end
  end
  Log("census: actors with PO "..withPO.."  without "..without.." ->"..names)
end

Game_Tick = function(a,b,c,d)
  n = n + 1
  local r = old(a,b,c,d)

  if n == 60 then
    census()
    local px,py,pz = Player._groundx, Player._groundy, Player._groundz
    local ax,ay,az = CAM.GetAngRad()
    local fx,fy,fz = 0,0,1
    if ay then fx,fy,fz = VectorRotate(0,0,1, 0, ay, 0) end
    mob, rec = {}, {}
    for k = 1, COUNT do
      local row = math.floor((k-1)/4)
      local col = math.mod(k-1, 4)
      local o = GObjects:Add("Mob_"..k, CloneTemplate("EvilMonkV2.CActor"))
      o.Pos.X = px + fx*(6 + row*2.5) + (col - 1.5)*2.0
      o.Pos.Y = py + 0.3
      o.Pos.Z = pz + fz*(6 + row*2.5)
      o.angle = math.atan2(px - o.Pos.X, pz - o.Pos.Z)
      o.angleDest = o.angle
      o:Apply()
      if o.Synchronize then o:Synchronize() end
      table.insert(mob, o)
      table.insert(rec, {x = o.Pos.X, z = o.Pos.Z, saw = 0, walked = 0, path = 0,
                         lx = o.Pos.X, lz = o.Pos.Z})
    end
    Log("spawned "..COUNT.." EvilMonkV2 in front of the player")
  end

  if mob then
    for i,o in mob do
      local t = rec[i]
      local br = o._AIBrain
      if br and br._seeEnemy then t.saw = t.saw + 1 end
      if o._isWalking then t.walked = t.walked + 1 end
      local dx, dz = o._groundx - t.lx, o._groundz - t.lz
      t.path = t.path + math.sqrt(dx*dx + dz*dz)
      t.lx, t.lz = o._groundx, o._groundz
    end
  end

  if mob and math.mod(n, 200) == 0 then
    local saw, walked, moved, arrived, stuck = 0, 0, 0, 0, 0
    local net, path, nearest = 0, 0, 9999
    for i,o in mob do
      local t = rec[i]
      if t.saw > 0 then saw = saw + 1 end
      if t.walked > 0 then walked = walked + 1 end
      local dx, dz = o._groundx - t.x, o._groundz - t.z
      local straight = math.sqrt(dx*dx + dz*dz)
      net = net + straight
      path = path + t.path
      if straight > 0.5 then moved = moved + 1 end
      if t.walked > 0 and straight < 0.5 then stuck = stuck + 1 end
      local ex, ez = o._groundx - Player._groundx, o._groundz - Player._groundz
      local toPlayer = math.sqrt(ex*ex + ez*ez)
      if toPlayer < nearest then nearest = toPlayer end
      if toPlayer < 3.0 then arrived = arrived + 1 end
    end
    Log(string.format("t%4d of %d | saw %d, walked %d, moved %d, reached %d, STUCK %d | net %.1f path %.1f nearest %.1f",
      n, COUNT, saw, walked, moved, arrived, stuck, net/COUNT, path/COUNT, nearest))
  end
  return r
end
