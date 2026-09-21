// ScriptEngine: the WORLD natives - map loading, fog, ambient and the sky.

#include "ScriptEngineInternal.h"
#include "../Core/Check.h"
#include "../Core/Vectors.h"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

namespace painful {

// The World natives. The struct is declared here rather than in
// ScriptEngine.h so that adding one touches only this file.
struct WorldNatives : ScriptNativesBase {
	static int L_WORLD_AddEntity(lua_State* L);
	static int L_WORLD_FindEntityByName(lua_State* L);
	static int L_WORLD_Release(lua_State* L);
	static int L_WORLD_CreateEnabledAntiPortalFromClosedConvexMesh(lua_State* L);
	static int L_WORLD_DeleteAntiPortal(lua_State* L);
	static int L_WORLD_EnableAntiPortal(lua_State* L);
	static int L_WORLD_IsAntiPortalEnabled(lua_State* L);
	static ScriptEngine::AntiPortal* AntiPortalArg(lua_State* L);
	static int L_PHYSICS_ActiveMeshGroupActivate(lua_State* L);
	static int L_PHYSICS_ActiveMeshGroupEnable(lua_State* L);
	static int L_PHYSICS_ActiveMeshGroupStaticMeshEnable(lua_State* L);
	static int L_PHYSICS_ActiveMeshGroupSetActivationParams(lua_State* L);
	static int L_WORLD_LoadMap(lua_State* L);
	static int L_WORLD_Init(lua_State* L);
	static int L_WORLD_SetupFog(lua_State* L);
	static int L_WORLD_BloomFXParams(lua_State* L);
	static int L_WORLD_EnableDemonFX(lua_State* L);
	static int L_WORLD_EnableSuperDemonFX(lua_State* L);
	static int L_WORLD_DemonFXParams(lua_State* L);
	static int L_WORLD_DemonFXWarp(lua_State* L);
	static int L_WORLD_SetFarClipDist(lua_State* L);
	static int L_WORLD_AmbientColor(lua_State* L);
	static int L_WORLD_GetAmbientColor(lua_State* L);
	static int L_WORLD_RemoveEntity(lua_State* L);
	static int L_WORLD_AdvanceFrameCounter(lua_State* L);
	static int L_WORLD_GetFrameCounter(lua_State* L);
	static int L_WORLD_DeleteDyingEntities(lua_State* L);
	static int L_WORLD_MakeUnderwater(lua_State* L);
	static int L_WORLD_IsUnderwater(lua_State* L);
	static int L_MESH_GetRandomPoint(lua_State* L);
	static int L_PHYSICS_GetHavokBodyActiveGroup(lua_State* L);
	static int L_WORLD_LoadSky(lua_State* L);
	static int L_WORLD_LoadLowQualitySky(lua_State* L);
	static int L_WORLD_SetupSkyLayer(lua_State* L);
	static int L_MESH_SetDefaultDetailMaps(lua_State* L);
	static int L_MESH_SetDefaultMaterial(lua_State* L);
	static int L_WORLD_SetupWater(lua_State* L);
	static int L_MESH_SetDefaultCubeMaps(lua_State* L);
	static int L_MESH_SetCubeMap(lua_State* L);
	static int L_MESH_SetNormalMap(lua_State* L);
	static int L_MESH_SetSpecular(lua_State* L);
	static int L_MESH_AddSpecularLight(lua_State* L);
	static int L_MESH_ResetSpecularLights(lua_State* L);
	static ScriptEngine::MeshOverride* MeshOverrideFor(ScriptEngine* self, lua_State* L);
	static int L_FOGVOL_Setup(lua_State* L);
	static int L_FOGVOL_GetProperties(lua_State* L);
	static int L_ENTITY_EnableDeathZoneTest(lua_State* L);
	static int L_WORLD_EnableDeathZone(lua_State* L);
	static int L_WORLD_CheckStartGlass(lua_State* L);
	static int L_WORLD_EnableDrawMeshGroup(lua_State* L);
	static int L_PHYSICS_StaticMeshGroupEnable(lua_State* L);
	static int L_WORLD_SetCollisionGroupMeshGroup(lua_State* L);
	static int L_WORLD_SetTimeToDeleteMeshGroup(lua_State* L);
	static int L_MESH_SetMeshGroup(lua_State* L);
};

// ------------------------------------------------------------ death zones

// The zones are map objects named `deathzone*`, the same name-only rule that
// identifies water, and their bounds scale with the level like the rest of the
// mesh. Docs/Reference/Physics.md, "Death zones".
void ScriptEngine::BuildDeathZones() {
	deathZones_.clear();
	if (!mapLoaded_) return;
	const float scale = world_.scale > 0.f ? world_.scale : 1.f;
	for (const MapObject& o : map_.objects) {
		if (o.name.find("deathzone") == std::string::npos) continue;
		DeathZone z;
		z.name = o.name;
		for (int a = 0; a < 3; ++a) {
			z.lo[a] = o.bboxMin[a] * scale;
			z.hi[a] = o.bboxMax[a] * scale;
		}
		deathZones_.push_back(z);
	}
	if (!deathZones_.empty())
		LogInfo("death zones: %zu", deathZones_.size());
}

// One IN_DEATH_ZONE per entity that asks to be tested, carrying the zone's
// NAME as a fifth argument - Game_GetMsg turns the test off again and hands
// the object x, y, z and that name, which it matches "wat" against.
void ScriptEngine::TickDeathZones() {
	if (!host_ || deathZones_.empty()) return;
	for (auto& kv : entities_) {
		Entity& e = kv.second;
		if (!e.deathZoneTest) continue;
		Vec3 p = e.pos;
		if (kv.first == playerHandle_ && pawn_) pawn_->FloorPos(p);
		for (const DeathZone& z : deathZones_) {
			if (!z.enabled) continue;
			bool inside = true;
			for (int a = 0; a < 3 && inside; ++a) inside = p[a] >= z.lo[a] && p[a] <= z.hi[a];
			if (!inside) continue;
			const double args[4] = {double(kv.first), p[0], p[1], p[2]};
			host_->PostMsg("IN_DEATH_ZONE", args, 4, z.name.c_str());
			break;
		}
	}
}

// ------------------------------------------------------------------ glass

// One static body per pane, kept out of the static mesh by the same rule that
// keeps a destructible's twin out, so breaking one can remove it.
void ScriptEngine::BuildGlass() {
	glass_.clear();
	if (!mapLoaded_ || !physics_) return;
	const float scale = world_.scale > 0.f ? world_.scale : 1.f;
	for (size_t i = 0; i < map_.objects.size(); ++i) {
		const MapObject& o = map_.objects[i];
		if (!o.isGlass() || o.vertexCount() == 0) continue;
		GlassPane g;
		g.object = i;
		Vec3 origin;
		g.body = physics_->CreateStaticTwinBody(o, scale, -1, origin);
		if (g.body < 0) continue;
		for (int a = 0; a < 3; ++a) {
			g.lo[a] = o.bboxMin[a] * scale;
			g.hi[a] = o.bboxMax[a] * scale;
		}
		glass_.push_back(g);
	}
	if (!glass_.empty()) LogInfo("glass: %zu panes", glass_.size());
}

// The original identifies the pane from the BODY the caller hit and spends the
// radius on which shards to start. Ours has one static world body, so the
// point and radius pick the pane instead - the deviation is in Physics.md.
bool ScriptEngine::BreakGlassAt(const Vec3& at, float radius) {
	const float r = radius > 0.f ? radius : 0.5f;
	for (GlassPane& g : glass_) {
		if (g.broken) continue;
		bool inside = true;
		for (int a = 0; a < 3 && inside; ++a)
			inside = at[a] >= g.lo[a] - r && at[a] <= g.hi[a] + r;
		if (!inside) continue;
		g.broken = true;
		if (physics_ && g.body >= 0) physics_->SetScriptBodyEnabled(g.body, false);
		if (worldObjectVisible_) worldObjectVisible_(g.object, false);
		// The bullet holes were cut from the pane's own triangles, so they
		// would hang in the air once it is gone.
		int cleared = 0;
		for (auto& kv : entities_) {
			Entity& d = kv.second;
			if (d.decalSlot >= 0 && d.decalObject == int(g.object)) {
				decals_.ClearGeometry(d.decalSlot);
				++cleared;
			}
		}
		static const bool kTrace = DebugFlag("PAINFUL_DECAL_TRACE");
		if (kTrace) LogInfo("glass: pane %zu broken, %d decals cleared", g.object, cleared);
		return true;
	}
	return false;
}

// WORLD.CheckStartGlass(he, x, y, z, radius = 0.5, vx, vy, vz) -> was it glass.
// The return value is gameplay, not decoration: BoltStick passes THROUGH what
// it breaks and ElectroDisk does not bounce off it.
int WorldNatives::L_WORLD_CheckStartGlass(lua_State* L) {
	ScriptEngine* self = From(L);
	const Vec3 at{float(luaL_optnumber(L, 2, 0)), float(luaL_optnumber(L, 3, 0)),
					float(luaL_optnumber(L, 4, 0))};
	lua_pushboolean(L, self->BreakGlassAt(at, float(luaL_optnumber(L, 5, 0.5))) ? 1 : 0);
	return 1;
}

// ------------------------------------------------------------ mesh groups

// The `actgrp<N>` an active mesh was authored into, and the four natives that
// act on every member at once. Both boss arenas are built this way: C4L4_Alastor
// carries 1,990 grouped objects and shows a few groups at a time.
// Docs/Reference/Physics.md, "Mesh groups".
void ScriptEngine::ForEachInMeshGroup(int group, const std::function<void(Entity&)>& fn) {
	if (group < 0) return;
	for (auto& kv : entities_)
		if (kv.second.meshGroup == group) fn(kv.second);
}

// WORLD.EnableDrawMeshGroup(group, on = true) -> World::MeshesActiveGroupEnableDraw.
int WorldNatives::L_WORLD_EnableDrawMeshGroup(lua_State* L) {
	ScriptEngine* self = From(L);
	const bool on = lua_isnone(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	self->ForEachInMeshGroup(int(luaL_optnumber(L, 1, -1)), [&](Entity& e) {
		e.visible = on;
		if (self->renderer_ && e.rendererInstance >= 0)
			self->renderer_->SetScriptVisible(e.rendererInstance, on);
	});
	return 0;
}

// PHYSICS.StaticMeshGroupEnable(group, on = true) ->
// PhysicsWorld::StaticMeshesEnableByGroup. The collision half of the pair above;
// Alastor calls them together for every group it shows or hides.
int WorldNatives::L_PHYSICS_StaticMeshGroupEnable(lua_State* L) {
	ScriptEngine* self = From(L);
	const bool on = lua_isnone(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	self->ForEachInMeshGroup(int(luaL_optnumber(L, 1, -1)), [&](Entity& e) {
		if (self->physics_ && e.physicsBody >= 0)
			self->physics_->SetScriptBodyEnabled(e.physicsBody, on);
	});
	return 0;
}

// WORLD.SetCollisionGroupMeshGroup(group, collisionGroup) ->
// World::MeshesActiveGroupSetCollisionGroup. Alastor drops the floors out of
// the player's way by regrouping them rather than removing them.
int WorldNatives::L_WORLD_SetCollisionGroupMeshGroup(lua_State* L) {
	ScriptEngine* self = From(L);
	const int cg = int(luaL_optnumber(L, 2, 0));
	self->ForEachInMeshGroup(int(luaL_optnumber(L, 1, -1)), [&](Entity& e) {
		e.collisionGroup = cg;
		if (self->physics_ && e.physicsBody >= 0)
			self->physics_->SetScriptBodyCollisionGroup(e.physicsBody, cg);
	});
	return 0;
}

// WORLD.SetTimeToDeleteMeshGroup(group, time, randomize) - time 0 removes the
// group NOW (World::MeshesActiveGroupRemove; the sentinel at 0x103a74ac reads
// 0.0, and Alastor's WallsTimeToDelete is exactly 0.0), otherwise it schedules
// the removal at time + rand(0..randomize). The script's 4th argument is not
// read by the engine.
int WorldNatives::L_WORLD_SetTimeToDeleteMeshGroup(lua_State* L) {
	ScriptEngine* self = From(L);
	const int group = int(luaL_optnumber(L, 1, -1));
	const float time = float(luaL_optnumber(L, 2, 0));
	const float randomize = float(luaL_optnumber(L, 3, 0));
	if (time != 0.f) {
		self->ForEachInMeshGroup(group, [&](Entity& e) {
			const float jitter = randomize > 0.f
					? randomize * (float(std::rand()) / float(RAND_MAX)) : 0.f;
			e.timeToDie = time + jitter;
		});
		return 0;
	}
	// Removing walks the map, so collect first: erasing inside the walk would
	// invalidate it.
	std::vector<int> doomed;
	for (const auto& kv : self->entities_)
		if (kv.second.meshGroup == group) doomed.push_back(kv.first);
	for (int h : doomed) self->ReleaseEntity(h);
	return 0;
}

// MESH.SetMeshGroup(e, group) - Entity+0x7e2, and only on a Mesh entity
// (0x1012ED30 checks the type). C2L1's antennas and C5L2's crane put
// themselves into group 70 so the level can switch them as one.
int WorldNatives::L_MESH_SetMeshGroup(lua_State* L) {
	ScriptEngine* self = From(L);
	Entity* e = self->Find(HandleArg(L, 1));
	if (e && e->type == kMesh) e->meshGroup = int(luaL_optnumber(L, 2, 0));
	return 0;
}

// ENTITY.EnableDeathZoneTest(e, on = true) - the byte at Entity+0x11b.
int WorldNatives::L_ENTITY_EnableDeathZoneTest(lua_State* L) {
	ScriptEngine* self = From(L);
	if (Entity* e = self->Find(HandleArg(L, 1)))
		e->deathZoneTest = lua_isnone(L, 2) ? true : (lua_toboolean(L, 2) != 0);
	return 0;
}

// WORLD.EnableDeathZone(name, on = FALSE) - note the default, which is why
// `EnableDeathZone:'x'` with no argument turns one OFF (0x1013E180).
int WorldNatives::L_WORLD_EnableDeathZone(lua_State* L) {
	ScriptEngine* self = From(L);
	const char* name = luaL_optstring(L, 1, "");
	const bool on = lua_toboolean(L, 2) != 0;
	for (ScriptEngine::DeathZone& z : self->deathZones_)
		if (z.name == name) { z.enabled = on; break; }
	return 0;
}

// ---------------------------------------------------------------- WORLD

// WORLD.AddEntity(handle, hidden) - enters the entity into the drawn world;
// CActor passes `not self.Visible` as the second argument.
int WorldNatives::L_WORLD_AddEntity(lua_State* L) {
	ScriptEngine* self = From(L);
	if (Entity* e = self->Find(HandleArg(L, 1))) {
		e->inWorld = true;
		if (lua_isboolean(L, 2)) e->visible = !lua_toboolean(L, 2);
		self->SyncPose(*e);
	}
	return 0;
}

namespace {

bool SameNameNoCase(const std::string& a, const std::string& b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
			[](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

} // namespace

// WORLD.FindEntityByName(name) -> handle, or 0 (0x1013dd70: the world's entities
// newest first, names compared without case). A map object's entity is made on
// first ask at the handle LoadMap reserved; its mesh natives are still stubs.
int WorldNatives::L_WORLD_FindEntityByName(lua_State* L) {
	ScriptEngine* self = From(L);
	const std::string name = luaL_optstring(L, 1, "");
	int found = 0;
	if (!name.empty()) {
		for (const auto& kv : self->entities_)
			if (kv.second.inWorld && kv.first > found && SameNameNoCase(kv.second.name, name))
				found = kv.first;
		for (size_t i = 0; !found && i < self->objectHandles_.size(); ++i) {
			if (!self->objectHandles_[i] || !SameNameNoCase(self->map_.objects[i].name, name))
				continue;
			// Taken only when a save from before the reservation put something there.
			found = self->Find(self->objectHandles_[i]) ? self->nextHandle_++ : self->objectHandles_[i];
			Entity e;
			e.type = kMesh;
			e.name = self->map_.objects[i].name;
			e.worldObject = true;
			e.inWorld = true;
			self->entities_.emplace(found, e);
			++self->created_;
		}
	}
	lua_pushnumber(L, found);
	return 1;
}

// WORLD.Release(withMap = true, mapName) - 0x10120d50.
int WorldNatives::L_WORLD_Release(lua_State* L) {
	From(L)->ReleaseWorld(lua_isnoneornil(L, 1) || lua_toboolean(L, 1));
	return 0;
}

// The antiportal natives keep the list and its flags; nothing is culled by them
// here. The names Create hands out are ours: Slab only passes them back.
ScriptEngine::AntiPortal* WorldNatives::AntiPortalArg(lua_State* L) {
	const std::string name = luaL_optstring(L, 1, "");
	for (ScriptEngine::AntiPortal& a : From(L)->antiportals_)
		if (SameNameNoCase(a.name, name)) return &a;
	return nullptr;
}

int WorldNatives::L_WORLD_CreateEnabledAntiPortalFromClosedConvexMesh(lua_State* L) {
	ScriptEngine* self = From(L);
	ScriptEngine::AntiPortal a;
	a.name = "antiportal" + std::to_string(++self->antiportalSerial_);
	a.enabled = true;
	self->antiportals_.push_back(a);
	lua_pushstring(L, a.name.c_str());
	return 1;
}

int WorldNatives::L_WORLD_DeleteAntiPortal(lua_State* L) {
	ScriptEngine* self = From(L);
	if (ScriptEngine::AntiPortal* a = AntiPortalArg(L))
		self->antiportals_.erase(self->antiportals_.begin() + (a - self->antiportals_.data()));
	return 0;
}

int WorldNatives::L_WORLD_EnableAntiPortal(lua_State* L) {
	if (ScriptEngine::AntiPortal* a = AntiPortalArg(L)) a->enabled = lua_toboolean(L, 2) != 0;
	return 0;
}

int WorldNatives::L_WORLD_IsAntiPortalEnabled(lua_State* L) {
	const ScriptEngine::AntiPortal* a = AntiPortalArg(L);
	lua_pushboolean(L, a && a->enabled);
	return 1;
}

// WORLD.LoadMap(mapPath, levelName, scale, overbright, rtCubeMap,
// shadowMapSize, shadowMapCount) - recorded; the game loop owns the actual
// renderer upload.
// Every "phys" object of the map becomes a rigid body and an entity, as
// World::LoadMeshPakFile + PhysicsWorld::AddMesh make them. The level's mass
// factor comes from the Lua global the ENGINE calls, Level_GetActiveMeshesData
// (CLevel.lua:970), substring-matched on the lowercased name; 1 means "use
// ActiveMeshesMassScale", which WORLD.Init brings a moment later.
float ScriptEngine::ActiveMeshMassScale(const std::string& objectName) {
	lua_State* L = host_ ? host_->state() : nullptr;
	float massScale = 1.f;
	if (L) {
		const int top = lua_gettop(L);
		lua_pushstring(L, "Level_GetActiveMeshesData");
		lua_gettable(L, LUA_GLOBALSINDEX);
		if (lua_isfunction(L, -1)) {
			lua_pushstring(L, objectName.c_str());
			if (lua_pcall(L, 1, 1, 0) == 0 && lua_isnumber(L, -1))
				massScale = float(lua_tonumber(L, -1));
		}
		lua_settop(L, top);
	}
	return massScale;
}

void ScriptEngine::CreateActiveMeshes() {
	if (!physics_ || !mapLoaded_) return;
	destructibles_.clear();
	size_t made = 0, pinned = 0, held = 0;
	std::vector<std::pair<size_t, int>> pieces; // (object, handle) of every "physdest"
	for (size_t i = 0; i < map_.objects.size(); ++i) {
		const MapObject& o = map_.objects[i];
		if (!o.isActiveMesh() || o.vertexCount() == 0) continue;
		const float massScale = ActiveMeshMassScale(o.name);
		Vec3 origin;
		const int slot = physics_->CreateActiveMeshBody(
				o, world_.scale, massScale, o.isPinned(), o.nameHas("concave"),
				o.activeGroup(), origin);
		if (slot < 0) continue;
		Entity e;
		e.type = kMesh;
		e.name = o.name;
		e.worldObject = true;
		e.inWorld = true;
		e.activeMesh = int(i);
		e.meshGroup = o.activeGroup();
		e.physicsBody = slot;
		e.collisionGroup = 3; // AddMesh creates every one in group 3
		for (int c = 0; c < 3; ++c) e.pos[c] = e.activeOrigin[c] = origin[c];
		// A piece waits for its twin's release: out of the simulation and
		// unseen (AddMesh's physdest branch ends in World::RemoveEntity).
		const bool piece = o.isDestructiblePiece();
		if (piece) e.visible = false;
		const int handle = i < objectHandles_.size() && objectHandles_[i] ? objectHandles_[i] : nextHandle_++;
		PAINFUL_CHECK(!Find(handle), "active mesh %s: handle %d is taken", o.name.c_str(), handle);
		entities_.emplace(handle, e);
		bodyToEntity_[slot] = handle;
		++created_;
		++made;
		if (o.isPinned()) ++pinned;
		CreateRendererInstance(entities_[handle]);
		if (piece) {
			physics_->SetScriptBodyEnabled(slot, false);
			if (renderer_ && entities_[handle].rendererInstance >= 0)
				renderer_->SetScriptVisible(entities_[handle].rendererInstance, false);
			pieces.emplace_back(i, handle);
			++held;
		}
	}
	// The intact twins, paired with their pieces by name (FUN_101BA530's
	// prefix). Each piece goes to the LONGEST matching prefix: Enclave's
	// grob2 would otherwise take grob22's pieces. ASSUMED - the original's
	// matcher is not located yet. Docs/Reference/Physics.md, "Destructibles".
	std::vector<std::string> prefixes;
	for (size_t i = 0; i < map_.objects.size(); ++i) {
		const MapObject& o = map_.objects[i];
		if (!o.isStaticTwin() || o.vertexCount() == 0) continue;
		Destructible d;
		d.object = i;
		d.group = o.activeGroup();
		Vec3 origin;
		d.twinBody = physics_->CreateStaticTwinBody(o, world_.scale, d.group, origin);
		if (d.twinBody < 0) continue;
		destructibles_.push_back(std::move(d));
		prefixes.push_back(o.piecePrefix());
	}
	size_t orphans = 0;
	for (const auto& p : pieces) {
		const std::string& name = map_.objects[p.first].name;
		size_t best = SIZE_MAX, bestLen = 0;
		for (size_t k = 0; k < prefixes.size(); ++k) {
			const std::string& pre = prefixes[k];
			if (pre.empty() || pre.size() <= bestLen) continue;
			if (name.compare(0, pre.size(), pre) == 0) { best = k; bestLen = pre.size(); }
		}
		if (best == SIZE_MAX) ++orphans;
		else destructibles_[best].pieces.push_back(p.second);
	}
	if (made)
		LogInfo("active meshes: %zu bodies, %zu pinned, %zu pieces held for %zu destructibles"
				" (%zu unpaired)",
				made, pinned, held, destructibles_.size(), orphans);
}

void ScriptEngine::ReleaseDestructible(size_t index, const float* blast) {
	if (index >= destructibles_.size()) return;
	Destructible& d = destructibles_[index];
	if (d.released) return;
	d.released = true;
	LogInfo("destructible: %s -> %zu pieces%s", map_.objects[d.object].name.c_str(),
			d.pieces.size(), blast ? " (blast)" : "");
	Vec3 at;
	if (physics_ && d.twinBody >= 0) {
		physics_->GetScriptBodyPosition(d.twinBody, at);
		physics_->RemoveScriptBody(d.twinBody);
	}
	if (worldObjectVisible_) worldObjectVisible_(d.object, false);
	for (int handle : d.pieces) {
		auto it = entities_.find(handle);
		if (it == entities_.end()) continue;
		Entity& e = it->second;
		e.visible = true;
		if (renderer_ && e.rendererInstance >= 0)
			renderer_->SetScriptVisible(e.rendererInstance, true);
		if (physics_ && e.physicsBody >= 0) {
			physics_->SetScriptBodyEnabled(e.physicsBody, true);
			const Vec3 still;
			physics_->SetScriptBodyVelocity(e.physicsBody, still);
		}
	}
	// Lev:OnExplodeMesh(actgrp, x, y, z) - Cemetery plays the collapse and
	// shakes the camera off it.
	const double args[4] = {double(d.group), blast ? blast[0] : at[0],
							blast ? blast[1] : at[1], blast ? blast[2] : at[2]};
	host_->PostMsg("EXPLODEMESH", args, 4);
}

void ScriptEngine::ReleaseTwins(const std::vector<int>& twinSlots, const float* blast) {
	for (int slot : twinSlots)
		for (size_t i = 0; i < destructibles_.size(); ++i)
			if (destructibles_[i].twinBody == slot && !destructibles_[i].released)
				ReleaseDestructible(i, blast);
}

int WorldNatives::L_PHYSICS_ActiveMeshGroupActivate(lua_State* L) {
	ScriptEngine* self = From(L);
	if (!self->physics_) return 0;
	std::vector<int> twins;
	self->physics_->ActivateActiveMeshGroup(int(luaL_optnumber(L, 1, -1)), twins);
	self->ReleaseTwins(twins, nullptr);
	return 0;
}

int WorldNatives::L_PHYSICS_ActiveMeshGroupEnable(lua_State* L) {
	ScriptEngine* self = From(L);
	if (self->physics_)
		self->physics_->EnableActiveMeshGroup(int(luaL_optnumber(L, 1, -1)),
				lua_toboolean(L, 2) != 0);
	return 0;
}

// The static twins of a group, on or off (FUN_101B25D0): the intact
// "statdest" bodies leave or rejoin the simulation. Drawing is the scripts'
// own WORLD.EnableDrawMeshGroup call beside it, so only the body moves here.
int WorldNatives::L_PHYSICS_ActiveMeshGroupStaticMeshEnable(lua_State* L) {
	ScriptEngine* self = From(L);
	const int group = int(luaL_optnumber(L, 1, -1));
	const bool on = lua_toboolean(L, 2) != 0;
	if (!self->physics_) return 0;
	for (const Destructible& d : self->destructibles_)
		if (d.group == group && !d.released && d.twinBody >= 0)
			self->physics_->SetScriptBodyEnabled(d.twinBody, on);
	return 0;
}

// Collision reporting and time-to-live per group (FUN_101B9E60). The
// collision callbacks arrive through ENTITY.EnableCollisionsToAll instead;
// the autodelete timers are not ported.
int WorldNatives::L_PHYSICS_ActiveMeshGroupSetActivationParams(lua_State*) { return 0; }

int WorldNatives::L_WORLD_LoadMap(lua_State* L) {
	ScriptEngine* self = From(L);
	self->world_.mapPath = luaL_optstring(L, 1, "");
	self->world_.levelName = luaL_optstring(L, 2, "");
	self->world_.scale = float(luaL_optnumber(L, 3, 1.0));
	self->world_.overbright = lua_toboolean(L, 4) != 0;
	self->world_.rtCubeMap = lua_toboolean(L, 5) != 0;
	// The empty "NoName" level passes "../Data/Maps/" with no file - a level
	// without a world, not an error.
	self->world_.loadRequested =
		!self->world_.mapPath.empty() && self->world_.mapPath.back() != '/';
	// The map's .EVolumetric entities apply after this, to its own objects.
	self->volumeParams_.clear();

	// With physics attached the static world is built HERE, synchronously:
	// the entity bodies follow through PO_Create later in this same level
	// load, and they need something to rest on.
	// A map was already up: this is a level switch, and the scripts have just
	// released their own entities in Game:Clear. Drop what is ours.
	if (self->mapLoaded_) self->ResetLevelState();
	self->mapLoaded_ = false;
	self->objectHandles_.clear();
	// A level is going up (or the empty one): the app rebuilds its renderer
	// on the next TakeLevelChange, and a LoadWorld after this marks itself.
	++self->levelChangeSerial_;
	self->loadedFromSave_ = false;
	if (self->physics_ && self->world_.loadRequested) {
		const std::string path = self->host_->ResolvePath(self->world_.mapPath);
		// Load reports success as "no error recorded", so the reused mesh
		// must start clean or a previous failure poisons this one.
		self->map_ = MapMesh();
		if (MapMesh::Load(path, self->map_)) {
			self->mapLoaded_ = true;
			// World::LoadMeshPak makes the objects entities before any script does.
			self->objectHandles_.assign(self->map_.objects.size(), 0);
			for (size_t i = 0; i < self->map_.objects.size(); ++i)
				if (self->map_.objects[i].makesEntity())
					self->objectHandles_[i] = self->nextHandle_++;
			// The map's antiportals come first in World+0x78, off until a Slab opens them.
			self->antiportals_.clear();
			for (const MapObject& o : self->map_.objects)
				if (o.nameHas("antyp")) self->antiportals_.push_back({o.name, false});
			self->physics_->LoadWorldMesh(self->map_, self->world_.scale,
					self->dataRoot_);
			self->CreateActiveMeshes();
			// Water is not in that mesh - every shipped water object is also
			// named `noclip` - so it is registered separately here.
			self->BuildWaterSurfaces();
			self->BuildDeathZones();
			self->BuildGlass();
		} else {
			LogWarn("WORLD.LoadMap: %s failed: %s", path.c_str(),
					self->map_.error.c_str());
		}
	}
	return 0;
}

void ScriptEngine::ResetLevelState() {
	// The active meshes are entities the ENGINE made from the map's `phys`
	// objects (CreateActiveMeshes); GObjects:Clear never sees them.
	std::vector<int> engineOwned;
	for (const auto& kv : entities_)
		if (kv.second.worldObject || kv.second.decalSlot >= 0) engineOwned.push_back(kv.first);
	for (int handle : engineOwned) ReleaseEntity(handle);
	decals_.Clear();
	water_.clear();
	deathZones_.clear();
	glass_.clear();
	lastExploded_.clear();
	contactVelocity_.clear();
	excludedSlots_.clear();
	// Limb handles name (entity, joint), and ReleaseWorld(true) restarts
	// handles at 1 - so anything left here would hide a joint of a NEW
	// entity for the rest of the session. An unbalanced Remove bracket (a
	// script error between Stake.lua's Add/Remove pair) is how that happens.
	suppressedLimbs_.clear();
	limbShadowed_.clear();
	limbHandles_.clear();
	limbHandleIndex_.clear();
	if (playerHandle_ && Find(playerHandle_) == nullptr) playerHandle_ = 0;
	LogInfo("level switch: %zu engine entities dropped, %zu script entities still live",
			engineOwned.size(), entities_.size());
}

void ScriptEngine::ReleaseWorld(bool withMap) {
	if (withMap) {
		// World::Release (0x1005f160) deletes them all and sets the array count back to 1.
		ReleaseAllEntities();
		ResetLevelState();
		nextHandle_ = 1;
		objectHandles_.clear();
		antiportals_.clear();
		return;
	}
	// ReleaseWithoutMap (0x1005dc80): what CreateEntity flagged (+0x19 & 2) goes, the
	// map objects stay, and the handles keep counting.
	std::vector<int> made;
	for (const auto& kv : entities_)
		if (!kv.second.worldObject) made.push_back(kv.first);
	for (int handle : made) ReleaseEntity(handle);
}

// WORLD.Init(activeMeshesMassScale, defaultMeshFriction,
// defaultMeshRestitution, deactivatorDelay, deactivatorMaxPosDiff) - CLevel
// calls it right after LoadMap. The deactivator pair maps onto Jolt's own
// sleep thresholds, which are close enough to leave alone for now.
int WorldNatives::L_WORLD_Init(lua_State* L) {
	ScriptEngine* self = From(L);
	if (self->physics_)
		self->physics_->SetWorldSurface(float(luaL_optnumber(L, 1, 1.0)),
				float(luaL_optnumber(L, 2, 0.5)),
				float(luaL_optnumber(L, 3, 0.5)));
	return 0;
}

// WORLD.SetupFog(mode [, start, end, density, packedColor]). CLevel scales
// start/end by the user's clip-plane setting before the call, so the values
// arrive ready to use.
int WorldNatives::L_WORLD_SetupFog(lua_State* L) {
	ScriptEngine* self = From(L);
	WorldState& w = self->world_;
	w.fogMode = int(luaL_optnumber(L, 1, 0));
	w.fogStart = float(luaL_optnumber(L, 2, 0));
	w.fogEnd = float(luaL_optnumber(L, 3, 90));
	w.fogDensity = float(luaL_optnumber(L, 4, 0));
	const uint32_t c = uint32_t(int64_t(luaL_optnumber(L, 5, 0)));
	w.fogColor[0] = float((c >> 16) & 0xFF);
	w.fogColor[1] = float((c >> 8) & 0xFF);
	w.fogColor[2] = float(c & 0xFF);
	return 0;
}

// WORLD.BloomFXParams(threshold, multiplier, overlayColor, dimScale) - the
// CLevel.BloomFX block (World+0x6cc..0x6d8). DimScale is what the sprite
// packers (FUN_101e4080, Billboard::Draw) multiply RGB by when bloom is on.
int WorldNatives::L_WORLD_BloomFXParams(lua_State* L) {
	WorldState& w = From(L)->world_;
	w.bloomThreshold = float(luaL_optnumber(L, 1, 0.25));
	w.bloomMultiplier = float(luaL_optnumber(L, 2, 1.0));
	w.bloomOverlay = uint32_t(int64_t(luaL_optnumber(L, 3, 0x808080)));
	w.bloomDimScale = float(luaL_optnumber(L, 4, 0.8));
	return 0;
}

// Demon Morph. WORLD.EnableDemonFX(on) is World+0x6dc (0x10120720), the
// gate of View::RenderDemonFXWorld; EnableSuperDemonFX(on) is +0x6dd
// (0x101207A0), which no shipped script sets. DemonFx.md.
int WorldNatives::L_WORLD_EnableDemonFX(lua_State* L) {
	From(L)->world_.demonFx = lua_toboolean(L, 1) != 0;
	return 0;
}

int WorldNatives::L_WORLD_EnableSuperDemonFX(lua_State* L) {
	From(L)->world_.superDemonFx = lua_toboolean(L, 1) != 0;
	return 0;
}

// WORLD.DemonFXParams(Scale, Bias, 1 - MBlur, MBlur) - CLevel:ReloadFX, into
// World+0x6e0/+0x6e4/+0x6ec/+0x6f0 (0x10120820), defaults 1, 0, 0.3, 0.7.
int WorldNatives::L_WORLD_DemonFXParams(lua_State* L) {
	WorldState& w = From(L)->world_;
	w.demonScale = float(luaL_optnumber(L, 1, 1.0));
	w.demonBias = float(luaL_optnumber(L, 2, 0.0));
	w.demonKeep = float(luaL_optnumber(L, 3, 0.3));
	w.demonMBlur = float(luaL_optnumber(L, 4, 0.7));
	return 0;
}

// WORLD.DemonFXWarp(amount) - World+0x6e8 (0x101209E0), the DemonFXWarp
// process every tick: 0..MaxWarp 0.1, in over 0.2 s, out as a damped cosine.
int WorldNatives::L_WORLD_DemonFXWarp(lua_State* L) {
	From(L)->world_.demonWarp = float(luaL_optnumber(L, 1, 0.0));
	return 0;
}

int WorldNatives::L_WORLD_SetFarClipDist(lua_State* L) {
	From(L)->world_.farClip = float(luaL_optnumber(L, 1, 1024));
	return 0;
}

// WORLD.AmbientColor(r, g, b, gunAmbientMultiplier), components 0-255.
int WorldNatives::L_WORLD_AmbientColor(lua_State* L) {
	ScriptEngine* self = From(L);
	for (int i = 0; i < 3; ++i)
		self->world_.ambient[i] = float(luaL_optnumber(L, 1 + i, 128));
	// The gun ambient multiplier, the fourth argument, default 1 (0x1011FB70
	// writes World+0x17CC). Nothing here lights the view model from it yet;
	// it is kept so the menu's save-and-restore round-trips.
	self->world_.gunAmbient = float(luaL_optnumber(L, 4, 1));
	return 0;
}

// WORLD.GetAmbientColor() -> r, g, b, gun multiplier (0x1011FC80). PainMenu
// stores the four, overrides them to light the character preview, and puts
// them back.
int WorldNatives::L_WORLD_GetAmbientColor(lua_State* L) {
	ScriptEngine* self = From(L);
	for (int i = 0; i < 3; ++i) lua_pushnumber(L, self->world_.ambient[i]);
	lua_pushnumber(L, self->world_.gunAmbient);
	return 4;
}

// WORLD.RemoveEntity(e) - unlinks the entity from the world without freeing it
// (0x10136D40 -> World::RemoveEntity). ENTITY.Release is the separate call that
// frees it; the Delete paths make both. A deactivated weapon and the
// multiplayer model's head and muzzle leave the world this way and come back
// through WORLD.AddEntity.
int WorldNatives::L_WORLD_RemoveEntity(lua_State* L) {
	ScriptEngine* self = From(L);
	if (Entity* e = self->Find(HandleArg(L, 1))) {
		// Only the world membership: EnableDraw owns e->visible, and a
		// holstered weapon that came back through AddEntity would stay
		// invisible if this touched it too.
		e->inWorld = false;
		self->SyncPose(*e);
	}
	return 0;
}

// WORLD.AdvanceFrameCounter() / WORLD.GetFrameCounter() - World+0x0
// (0x1011E4F0, 0x1011E480). The menu steps it by hand so the character
// preview's animation advances while the game itself is not running.
int WorldNatives::L_WORLD_AdvanceFrameCounter(lua_State* L) {
	++From(L)->worldFrame_;
	return 0;
}

int WorldNatives::L_WORLD_GetFrameCounter(lua_State* L) {
	lua_pushnumber(L, From(L)->worldFrame_);
	return 1;
}

// WORLD.MakeUnderwater(on) / WORLD.IsUnderwater() - 0x101200B0 and 0x10120030,
// PhysicsWorld::EnableUnderwaterWorld and its reader. CLevel pushes its
// IsUnderwater out on every apply, and no shipped level sets it, so the flag is
// kept and read back but the physics behind it is unexercised and not built.
int WorldNatives::L_WORLD_MakeUnderwater(lua_State* L) {
	From(L)->world_.underwater = lua_toboolean(L, 1) != 0;
	return 0;
}

int WorldNatives::L_WORLD_IsUnderwater(lua_State* L) {
	lua_pushboolean(L, From(L)->world_.underwater);
	return 1;
}

// WORLD.DeleteDyingEntities() - reaps whatever SetTimeToDie has run out on,
// now (0x101210E0). TickLifetimes already does this every frame, so this only
// matters to a caller that wants it before the next tick; GameMP's round reset
// is the one. WORLD.DeleteDelayedEntities is its twin over the engine's
// deferred-delete queue, which this port has no equivalent of - ReleaseEntity
// frees immediately - so that one binds to the same sweep.
int WorldNatives::L_WORLD_DeleteDyingEntities(lua_State* L) {
	From(L)->ReapExpiredEntities();
	return 0;
}

// MESH.GetRandomPoint(e) -> a point on the mesh, in the entity's own space
// (0x1012F250 -> WorldMesh::GetRandomPoint, which picks a random vertex and
// scales it by the mesh scale - not a point on a face, and not in world
// space). CItem hands it straight to PARTICLE.SetParentOffset to sit a flame
// somewhere on a burning piece of wreckage.
int WorldNatives::L_MESH_GetRandomPoint(lua_State* L) {
	ScriptEngine* self = From(L);
	const Entity* e = self->Find(HandleArg(L, 1));
	Vec3 out{0.f, 0.f, 0.f};
	const MapObject* object = e ? self->MeshGeometry(*e) : nullptr;
	if (object && object->vertexCount() > 0) {
		object->position(size_t(std::rand()) % object->vertexCount(), out);
		for (int c = 0; c < 3; ++c) out[c] *= e->scale;
	}
	for (int c = 0; c < 3; ++c) lua_pushnumber(L, out[c]);
	return 3;
}

// PHYSICS.GetHavokBodyActiveGroup(h) -> the collision group of the body behind
// a contact handle (0x101298F0). CLevel:OnCollision is the caller: a world
// mesh with no script object of its own reports a contact, and the group is
// what picks the impact sound set (SoundsDefsGroups, 20..31 -
// WORLD.SetCollisionGroupMeshGroup is what put them there).
int WorldNatives::L_PHYSICS_GetHavokBodyActiveGroup(lua_State* L) {
	ScriptEngine* self = From(L);
	const int slot = lua_isnumber(L, 1) ? int(lua_tonumber(L, 1)) : -1;
	// CollisionGroups.Fixed, what a world body that never got a group reports.
	int group = 1;
	const auto it = self->bodyToEntity_.find(slot);
	if (it != self->bodyToEntity_.end()) {
		if (const Entity* e = self->Find(it->second)) group = e->collisionGroup;
	} else {
		int owner = 0, joint = -1;
		if (self->LimbFromHandle(slot, owner, joint))
			if (const Entity* e = self->Find(owner)) group = e->collisionGroup;
	}
	lua_pushnumber(L, group);
	return 1;
}

// WORLD.LoadSky("../Data/Maps/<dome>") -> layer count, read out of the dome
// mesh itself: objects name their layer ("layer01shape",
// "_trans_layer03shape"), and the count is how many carry one. An empty path
// (Cfg.RenderSky < 2) or an unreadable mesh returns 0, which sends
// CLevel:ReloadSky down the low-quality path - the same fallback the
// original uses for DX7-class hardware.
int WorldNatives::L_WORLD_LoadSky(lua_State* L) {
	ScriptEngine* self = From(L);
	const std::string mapPath = luaL_optstring(L, 1, "");
	self->world_.skyDomeMap.clear();
	self->world_.skyLayerCount = 0;

	if (!mapPath.empty() && mapPath.back() != '/') {
		MapMesh dome;
		if (MapMesh::Load(self->host_->ResolvePath(mapPath), dome)) {
			int count = 0;
			for (const MapObject& o : dome.objects) {
				std::string low = o.name;
				for (char& c : low)
					c = char(std::tolower(static_cast<unsigned char>(c)));
				if (low.find("layer") != std::string::npos) ++count;
			}
			if (count > 4) count = 4;
			if (count > 0) {
				const size_t slash = mapPath.find_last_of("/\\");
				self->world_.skyDomeMap =
					slash == std::string::npos ? mapPath : mapPath.substr(slash + 1);
				self->world_.skyLayerCount = count;
			}
		}
	}
	lua_pushnumber(L, self->world_.skyLayerCount);
	return 1;
}

// WORLD.LoadLowQualitySky("../Data/Maps/<dome>", height, angle) -> layer
// count (one: the single-texture dome).
int WorldNatives::L_WORLD_LoadLowQualitySky(lua_State* L) {
	ScriptEngine* self = From(L);
	const std::string mapPath = luaL_optstring(L, 1, "");
	self->world_.skyMap.clear();
	if (mapPath.empty() || mapPath.back() == '/' ||
			!FileSystem::Get().Exists(self->host_->ResolvePath(mapPath))) {
		lua_pushnumber(L, 0);
		return 1;
	}
	const size_t slash = mapPath.find_last_of("/\\");
	self->world_.skyMap = slash == std::string::npos ? mapPath : mapPath.substr(slash + 1);
	self->world_.skyAngle = float(luaL_optnumber(L, 3, 0));
	lua_pushnumber(L, 1);
	return 1;
}

// WORLD.SetupSkyLayer(i, texMask, texLMap,
//     tex1, rot, panU, panV, tileU, tileV,
//     tex2, rot, panU, panV, tileU, tileV) - argument order straight from
// CLevel:ReloadSky. On the low-quality path (no layered dome) the only
// meaningful argument is tex1: the dome's single texture.
int WorldNatives::L_WORLD_SetupSkyLayer(lua_State* L) {
	ScriptEngine* self = From(L);
	const int i = int(luaL_optnumber(L, 1, 0));
	if (self->world_.skyLayerCount == 0) {
		self->world_.skyTexture = luaL_optstring(L, 4, "");
		return 0;
	}
	if (i < 0 || i >= 4) return 0;
	SkyLayer& layer = self->world_.skyLayers[i];
	layer.mask = luaL_optstring(L, 2, "");
	layer.lightmap = luaL_optstring(L, 3, "");
	SkyTexture* tex[2] = {&layer.tex1, &layer.tex2};
	for (int t = 0; t < 2; ++t) {
		const int base = 4 + t * 6;
		tex[t]->name = luaL_optstring(L, base, "");
		tex[t]->rotSpeed = float(luaL_optnumber(L, base + 1, 0));
		tex[t]->panU = float(luaL_optnumber(L, base + 2, 0));
		tex[t]->panV = float(luaL_optnumber(L, base + 3, 0));
		tex[t]->tileU = float(luaL_optnumber(L, base + 4, 1));
		tex[t]->tileV = float(luaL_optnumber(L, base + 5, 1));
	}
	return 0;
}

int WorldNatives::L_MESH_SetDefaultDetailMaps(lua_State* L) {
	ScriptEngine* self = From(L);
	self->world_.detailTex = luaL_optstring(L, 1, "");
	self->world_.detailTileU = float(luaL_optnumber(L, 2, 8.2));
	self->world_.detailTileV = float(luaL_optnumber(L, 3, 7.1));
	return 0;
}



// WORLD.SetupWater (FUN_1011f3b0): CLevel:Apply hands the level's o.Water
// over as 21 numbers, in this order. The level-wide block - a surface in
// a CEnvironment water box takes that one instead. Water.md.
int WorldNatives::L_WORLD_SetupWater(lua_State* L) {
	ScriptEngine* self = From(L);
	WaterInfo& w = self->world_.water;
	w.fresnelBias = float(luaL_optnumber(L, 1, 0));
	w.fresnelExponent = float(luaL_optnumber(L, 2, 2));
	w.bumpHeight = float(luaL_optnumber(L, 3, 0.05));
	w.waveAmplitude = float(luaL_optnumber(L, 4, 1));
	w.waveFrequency = float(luaL_optnumber(L, 5, 1));
	for (int c = 0; c < 3; ++c) {
		w.deepColor[c] = float(luaL_optnumber(L, 6 + c, 150));
		w.shallowColor[c] = float(luaL_optnumber(L, 9 + c, 100));
	}
	w.waveSpeed = float(luaL_optnumber(L, 12, 1));
	w.waterAmount = float(luaL_optnumber(L, 13, 1));
	w.reflectionAmount = float(luaL_optnumber(L, 14, 1));
	w.waterLevel = float(luaL_optnumber(L, 15, 0));
	w.reflectScene = lua_toboolean(L, 16) != 0;
	w.refractScene = lua_toboolean(L, 17) != 0;
	w.pan[0] = float(luaL_optnumber(L, 18, 0.00172));
	w.pan[1] = float(luaL_optnumber(L, 19, 0.003));
	w.tile[0] = float(luaL_optnumber(L, 20, 17.5));
	w.tile[1] = float(luaL_optnumber(L, 21, 10));
	return 0;
}

// MESH.SetDefaultCubeMaps(texture): o.CubeMap.Tex, every model's $envcubemap.
int WorldNatives::L_MESH_SetDefaultCubeMaps(lua_State* L) {
	ScriptEngine* self = From(L);
	self->world_.cubeMap = luaL_optstring(L, 1, "");
	return 0;
}

// MESH.SetDefaultMaterial / SetCubeMap / SetNormalMap on a world-mesh object,
// which is how a map's MapEntities .EMesh picks its water: Orphanage's
// water_noclipshape is "water_ntu_refl" with special/ripples_00, Docks' keeps
// the "water" family but swaps in skies/wenecja_sky4. Recorded by object name
// for the renderer. Water.md, "Which water a surface gets".
ScriptEngine::MeshOverride* WorldNatives::MeshOverrideFor(ScriptEngine* self, lua_State* L) {
	const auto it = self->entities_.find(int(luaL_optnumber(L, 1, 0)));
	if (it == self->entities_.end() || !it->second.worldObject) return nullptr;
	return &self->meshOverrides_[it->second.name];
}

int WorldNatives::L_MESH_SetDefaultMaterial(lua_State* L) {
	if (ScriptEngine::MeshOverride* o = MeshOverrideFor(From(L), L))
		o->material = luaL_optstring(L, 2, "");
	return 0;
}

int WorldNatives::L_MESH_SetCubeMap(lua_State* L) {
	if (ScriptEngine::MeshOverride* o = MeshOverrideFor(From(L), L))
		o->cube = luaL_optstring(L, 2, "");
	return 0;
}

int WorldNatives::L_MESH_SetNormalMap(lua_State* L) {
	if (ScriptEngine::MeshOverride* o = MeshOverrideFor(From(L), L))
		o->normal = luaL_optstring(L, 2, "");
	return 0;
}

// MESH.SetSpecular(e, power = 8) - WorldMesh::SetSpecular (0x101dc520): the gloss
// power, and the gloss maps (<texture>_s, else the diffuse) loaded. EMesh:Apply runs
// it on every map mesh after AddSpecularLight. Lighting.md, "World specular"
int WorldNatives::L_MESH_SetSpecular(lua_State* L) {
	if (ScriptEngine::MeshOverride* o = MeshOverrideFor(From(L), L)) {
		o->specular = true;
		o->specPower = float(luaL_optnumber(L, 2, 8));
	}
	return 0;
}

// MESH.AddSpecularLight(e, light) - 0x101d6d90: fills the first free of two slots
// and ignores the rest.
int WorldNatives::L_MESH_AddSpecularLight(lua_State* L) {
	ScriptEngine::MeshOverride* o = MeshOverrideFor(From(L), L);
	const int light = int(luaL_optnumber(L, 2, 0));
	if (!o || light <= 0) return 0;
	for (int& slot : o->specLights)
		if (slot == 0) { slot = light; break; }
	return 0;
}

int WorldNatives::L_MESH_ResetSpecularLights(lua_State* L) {
	if (ScriptEngine::MeshOverride* o = MeshOverrideFor(From(L), L))
		o->specLights[0] = o->specLights[1] = 0;
	return 0;
}

// FOGVOL.Setup(e, color, end) - 0x1013b4b0: Volume+0x864 the composed colour,
// +0x86c End. Recorded by object name for the renderer. FogVolumes.md
int WorldNatives::L_FOGVOL_Setup(lua_State* L) {
	ScriptEngine* self = From(L);
	const auto it = self->entities_.find(int(luaL_optnumber(L, 1, 0)));
	if (it == self->entities_.end() || !it->second.worldObject) return 0;
	ScriptEngine::VolumeParams& v = self->volumeParams_[it->second.name];
	v.color = uint32_t(int64_t(luaL_optnumber(L, 2, 0)));
	v.end = float(luaL_optnumber(L, 3, 0));
	return 0;
}

// FOGVOL.GetProperties(e) -> r, g, b, end - 0x1013b560.
int WorldNatives::L_FOGVOL_GetProperties(lua_State* L) {
	ScriptEngine* self = From(L);
	const auto it = self->entities_.find(int(luaL_optnumber(L, 1, 0)));
	if (it == self->entities_.end() || !it->second.worldObject) return 0;
	const auto found = self->volumeParams_.find(it->second.name);
	const ScriptEngine::VolumeParams v = found != self->volumeParams_.end() ? found->second
			: ScriptEngine::VolumeParams();
	lua_pushnumber(L, (v.color >> 16) & 0xff);
	lua_pushnumber(L, (v.color >> 8) & 0xff);
	lua_pushnumber(L, v.color & 0xff);
	lua_pushnumber(L, v.end);
	return 4;
}

void BindWorld(ScriptEngine& engine, LuaHost& host) {
	const ScriptNative natives[] = {
		{"WORLD", "Init", WorldNatives::L_WORLD_Init},
		{"WORLD", "AddEntity", WorldNatives::L_WORLD_AddEntity},
		{"WORLD", "FindEntityByName", WorldNatives::L_WORLD_FindEntityByName},
		{"WORLD", "Release", WorldNatives::L_WORLD_Release},
		{"WORLD", "CreateEnabledAntiPortalFromClosedConvexMesh",
				WorldNatives::L_WORLD_CreateEnabledAntiPortalFromClosedConvexMesh},
		{"WORLD", "DeleteAntiPortal", WorldNatives::L_WORLD_DeleteAntiPortal},
		{"WORLD", "EnableAntiPortal", WorldNatives::L_WORLD_EnableAntiPortal},
		{"WORLD", "IsAntiPortalEnabled", WorldNatives::L_WORLD_IsAntiPortalEnabled},
		{"WORLD", "LoadMap", WorldNatives::L_WORLD_LoadMap},
		{"PHYSICS", "ActiveMeshGroupActivate", WorldNatives::L_PHYSICS_ActiveMeshGroupActivate},
		{"PHYSICS", "ActiveMeshGroupEnable", WorldNatives::L_PHYSICS_ActiveMeshGroupEnable},
		{"PHYSICS", "ActiveMeshGroupStaticMeshEnable", WorldNatives::L_PHYSICS_ActiveMeshGroupStaticMeshEnable},
		{"PHYSICS", "ActiveMeshGroupSetActivationParams", WorldNatives::L_PHYSICS_ActiveMeshGroupSetActivationParams},
		{"WORLD", "SetupFog", WorldNatives::L_WORLD_SetupFog},
		{"WORLD", "BloomFXParams", WorldNatives::L_WORLD_BloomFXParams},
		{"WORLD", "EnableDemonFX", WorldNatives::L_WORLD_EnableDemonFX},
		{"WORLD", "EnableSuperDemonFX", WorldNatives::L_WORLD_EnableSuperDemonFX},
		{"WORLD", "DemonFXParams", WorldNatives::L_WORLD_DemonFXParams},
		{"WORLD", "DemonFXWarp", WorldNatives::L_WORLD_DemonFXWarp},
		{"WORLD", "SetFarClipDist", WorldNatives::L_WORLD_SetFarClipDist},
		{"WORLD", "AmbientColor", WorldNatives::L_WORLD_AmbientColor},
		{"WORLD", "GetAmbientColor", WorldNatives::L_WORLD_GetAmbientColor},
		{"WORLD", "RemoveEntity", WorldNatives::L_WORLD_RemoveEntity},
		{"WORLD", "AdvanceFrameCounter", WorldNatives::L_WORLD_AdvanceFrameCounter},
		{"WORLD", "GetFrameCounter", WorldNatives::L_WORLD_GetFrameCounter},
		{"WORLD", "DeleteDyingEntities", WorldNatives::L_WORLD_DeleteDyingEntities},
		{"WORLD", "DeleteDelayedEntities", WorldNatives::L_WORLD_DeleteDyingEntities},
		{"WORLD", "MakeUnderwater", WorldNatives::L_WORLD_MakeUnderwater},
		{"WORLD", "IsUnderwater", WorldNatives::L_WORLD_IsUnderwater},
		{"MESH", "GetRandomPoint", WorldNatives::L_MESH_GetRandomPoint},
		{"PHYSICS", "GetHavokBodyActiveGroup", WorldNatives::L_PHYSICS_GetHavokBodyActiveGroup},
		{"WORLD", "LoadSky", WorldNatives::L_WORLD_LoadSky},
		{"WORLD", "LoadLowQualitySky", WorldNatives::L_WORLD_LoadLowQualitySky},
		{"WORLD", "SetupSkyLayer", WorldNatives::L_WORLD_SetupSkyLayer},
		{"MESH", "SetDefaultDetailMaps", WorldNatives::L_MESH_SetDefaultDetailMaps},
		{"MESH", "SetDefaultMaterial", WorldNatives::L_MESH_SetDefaultMaterial},
		{"WORLD", "SetupWater", WorldNatives::L_WORLD_SetupWater},
		{"MESH", "SetDefaultCubeMaps", WorldNatives::L_MESH_SetDefaultCubeMaps},
		{"MESH", "SetCubeMap", WorldNatives::L_MESH_SetCubeMap},
		{"MESH", "SetNormalMap", WorldNatives::L_MESH_SetNormalMap},
		{"MESH", "SetSpecular", WorldNatives::L_MESH_SetSpecular},
		{"MESH", "AddSpecularLight", WorldNatives::L_MESH_AddSpecularLight},
		{"MESH", "ResetSpecularLights", WorldNatives::L_MESH_ResetSpecularLights},
		{"FOGVOL", "Setup", WorldNatives::L_FOGVOL_Setup},
		{"FOGVOL", "GetProperties", WorldNatives::L_FOGVOL_GetProperties},
		{"ENTITY", "EnableDeathZoneTest", WorldNatives::L_ENTITY_EnableDeathZoneTest},
		{"WORLD", "EnableDeathZone", WorldNatives::L_WORLD_EnableDeathZone},
		{"WORLD", "CheckStartGlass", WorldNatives::L_WORLD_CheckStartGlass},
		{"WORLD", "EnableDrawMeshGroup", WorldNatives::L_WORLD_EnableDrawMeshGroup},
		{"PHYSICS", "StaticMeshGroupEnable", WorldNatives::L_PHYSICS_StaticMeshGroupEnable},
		{"WORLD", "SetCollisionGroupMeshGroup",
				WorldNatives::L_WORLD_SetCollisionGroupMeshGroup},
		{"WORLD", "SetTimeToDeleteMeshGroup",
				WorldNatives::L_WORLD_SetTimeToDeleteMeshGroup},
		{"MESH", "SetMeshGroup", WorldNatives::L_MESH_SetMeshGroup},
	};
	RegisterFamily(engine, host, natives);
}

} // namespace painful
