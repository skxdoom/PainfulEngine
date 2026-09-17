// ScriptEngine: WORLD.LoadGame for a world file the original engine wrote.
//
// Assets/WorldSave.h reads it. Each record becomes the Entity fields our own
// save carries - what made the entity, then its live state - and goes through
// RebuildEntity, so the two formats share one rebuild. Docs/Reference/LuaHost.md,
// "Loading an original save".

#include "ScriptEngineInternal.h"
#include "PlayerPawn.h"
#include "../Assets/Hke.h"
#include "../Assets/WorldSave.h"
#include "../Assets/Pkmdl.h"
#include "../Core/Check.h"
#include "../Core/Matrix.h"
#include "../World/PhysicsWorld.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace painful {

namespace {

// "../Data/Sounds/weapons/painkiller/pain-rotor-loop.wav" -> the name under Sounds.
std::string SoundNameOf(const std::string& file) {
	// Only the prefix goes: a script's own backslash is part of the cache key.
	std::string name = WsText(file);
	const size_t at = name.find("Sounds/");
	if (at != std::string::npos) name.erase(0, at + 7);
	const size_t dot = name.rfind('.');
	if (dot != std::string::npos) name.erase(dot);
	return name;
}

// The records as the AudioEngine keeps them. A sound filed in the save's pause set
// resumes, and its forget bit sits in that entry rather than in its flags.
std::vector<AudioEngine::VoiceState> SavedVoices(const WsAudio& audio) {
	std::vector<AudioEngine::VoiceState> out;
	const auto fill = [&](AudioEngine::VoiceState& s, uint32_t id, const std::string& file, uint8_t flags,
			uint32_t loopCount, uint32_t position, float volume, float speed, const WsPauseEntry& pause) {
		const bool filed = pause.present && pause.set == audio.pauseSet;
		s.id = int(id);
		s.name = SoundNameOf(file);
		s.forget = (flags & 1) != 0 || (filed && pause.forget);
		s.resumes = filed || (flags & 2) != 0;
		s.sameSpeed = (flags & 8) != 0;
		s.loopCount = int(loopCount);
		s.offset = position;
		s.volume = volume;
		s.speed = speed;
	};
	for (const WsSound2D& r : audio.sounds2D) {
		AudioEngine::VoiceState s;
		fill(s, r.id, r.file, r.flags, r.loopCount, r.position, r.volume, r.speed, r.pause);
		out.push_back(s);
	}
	for (const WsSound3D& r : audio.sounds3D) {
		AudioEngine::VoiceState s;
		s.positional = true;
		fill(s, r.id, r.file, r.flags, r.loopCount, r.position, r.volume, r.speed, r.pause);
		s.pos = r.pos;
		s.dist1 = r.minDistance;
		s.dist2 = r.maxDistance;
		out.push_back(s);
	}
	return out;
}

float Bits(uint32_t v) {
	float f;
	std::memcpy(&f, &v, 4);
	return f;
}

// The file keeps x, y, z then w (Model::SaveEntity 0x101df639); the engine w first.
Quat FileQuat(const float xyzw[4]) { return Quat(xyzw[3], xyzw[0], xyzw[1], xyzw[2]); }
Quat FileQuat(const Vec3& xyz, float w) { return Quat(w, xyz[0], xyz[1], xyz[2]); }

// CBillboard's editor blend index back from the material blend SetupCorona stored
// (0x10137b70 maps 1, 2, 3, 4 to 1, 2, 4, 5).
int CoronaBlendIndex(uint32_t stored) {
	switch (stored) {
	case 1: return 1;
	case 2: return 2;
	case 4: return 3;
	case 5: return 4;
	default: return 0;
	}
}

constexpr uint32_t kFlagDrawOff = 0x40; // Entity::EnableDraw
constexpr uint32_t kFlagDynamicLight = 0x400000; // Light::EnableDynamic
constexpr uint32_t kPoMonster = 0x2, kPoGrenade = 0x20, kPoFlying = 0x800;

} // namespace

bool ScriptEngine::LoadWorldSave(const std::vector<uint8_t>& buf, const std::string& path) {
	// A model's mesh names: the resolver's count, and what the visibility bytes name.
	std::map<std::string, std::pair<bool, std::vector<std::string>>> meshNames;
	const auto meshesOf = [&](const std::string& model) -> const std::vector<std::string>* {
		auto it = meshNames.find(model);
		if (it == meshNames.end()) {
			Model m;
			std::pair<bool, std::vector<std::string>> entry;
			entry.first = Model::Load(dataRoot_ + "/Models/" + model + ".pkmdl", m);
			for (const ModelMesh& mesh : m.meshes) entry.second.push_back(mesh.name);
			it = meshNames.emplace(model, std::move(entry)).first;
		}
		return it->second.first ? &it->second.second : nullptr;
	};
	WorldSave::Resolver models;
	models.meshCount = [&](const std::string& model) {
		const std::vector<std::string>* names = meshesOf(model);
		return names ? int(names->size()) : -1;
	};
	models.ragdoll = [&](const std::string& model, int& constraints, int& actions) {
		const Hke* def = RagdollDef(model);
		if (!def) return false;
		constraints = int(def->constraints.size());
		actions = int(def->springs.size() + def->dashpots.size());
		return true;
	};

	WorldSave save;
	if (!WorldSave::Read(buf, save, models)) {
		LogWarn("WORLD.LoadGame: %s: %s at 0x%zx", path.c_str(), save.error.c_str(), save.errorAt);
		return false;
	}

	// The level's own objects stay, as they do in the original: World::SaveEntities
	// never writes a map active mesh, and the saved decals hang off their handles.
	// Everything the scripts made goes, and so does anything on a handle the save owns.
	std::set<int> owned;
	for (const WsEntity& os : save.entities) owned.insert(os.base.handle);
	std::vector<int> release;
	size_t displaced = 0;
	for (const auto& kv : entities_) {
		const bool taken = owned.count(kv.first) != 0;
		if (!kv.second.worldObject || taken) release.push_back(kv.first);
		if (kv.second.worldObject && taken) ++displaced;
	}
	for (int h : release) ReleaseEntity(h);
	playerHandle_ = 0;
	PAINFUL_CHECK(displaced == 0, "WORLD.LoadGame: %zu level objects sat on handles the save owns",
			displaced);

	// LoadAudio comes before the entities: every sound record at its ID, so the handles
	// in the scripts' tables address sounds again, and a Sound entity then takes a new one.
	WsAudio audio;
	const bool audioRead = WsAudioRead(save.audio.body, audio);
	PAINFUL_CHECK(!audioRead || audio.soundsRead || save.audio.body.empty(),
			"WORLD.LoadGame: the audio chunk's sounds do not parse: %s", audio.error.c_str());
	if (audio_ && audioRead)
		audio_->RestoreVoices(SavedVoices(audio), int(audio.next2D), int(audio.next3D));

	std::map<std::string, size_t> skipped;
	std::vector<std::pair<int, const WsRagdoll*>> ragdolls;
	size_t made = 0;
	int maxHandle = 0;
	for (const WsEntity& os : save.entities) {
		const int handle = os.base.handle;
		Entity e;
		e.name = WsText(os.name);
		e.visible = (os.base.flags & kFlagDrawOff) == 0;
		e.inWorld = os.inWorld != 0;
		e.deathZoneTest = os.base.deathZoneTest != 0;
		e.scale = Bits(os.headerValue);
		const WsPhysicsObject* body = nullptr;

		switch (os.type) {
		case kWsModel: {
			const WsModel& m = os.model;
			e.type = kModel;
			e.source = WsText(os.resource);
			e.pos = m.pos;
			e.rot = FileQuat(m.rot);
			e.characterShadow = m.shadow != 0;
			// ASSUMED: the visibility bytes follow the .pkmdl's mesh order; the counts agree.
			if (const std::vector<std::string>* names = meshesOf(e.source))
				for (size_t i = 0; i < m.meshVisible.size() && i < names->size(); ++i)
					if (!m.meshVisible[i]) e.hiddenMeshes[(*names)[i]] = false;
			// The scripts hold these indices: slot 0 is the model's own, never written.
			e.animSlots.assign(std::max<size_t>(m.slotCount, 1), Entity::AnimSlot());
			for (size_t s = 0; s < m.slots.size(); ++s) {
				if (!m.slots[s].present) continue;
				Entity::AnimSlot& slot = e.animSlots[s + 1];
				slot.name = WsText(m.slots[s].anim);
				slot.loop = m.slots[s].loop != 0;
				// Model::LoadEntity gives the mask to the animation on the bone "ROOOT".
				slot.curveMask = m.slots[s].curveMask;
				if (slot.curveMask) slot.curveBone = "ROOOT";
			}
			for (const WsModel::Channel& c : m.channels) {
				if (c.slot == 0 || c.slot >= e.animSlots.size()) continue;
				e.animIndex = c.slot;
				e.animTime = c.time;
				e.animScale = c.speed > 0.f ? c.speed : 1.f;
				e.animLoop = c.loop != 0;
			}
			if (m.hasBody) {
				// The player's mover is the pawn, not a script body.
				if (m.body.hasController) playerHandle_ = handle;
				else body = &m.body;
			}
			if (m.ragdollOn) ragdolls.emplace_back(handle, &m.ragdoll);
			break;
		}
		case kWsWorldMesh: {
			std::string pack;
			if (!SplitPackSource(WsText(os.resource), pack)) {
				++skipped["WorldMesh outside Items"];
				continue;
			}
			e.type = kMesh;
			e.source = WsText(os.resource);
			e.mesh = e.name;
			e.meshCentred = (os.headerFlag & 0x80) != 0; // CreateEntity's last argument
			e.pos = os.mesh.pos;
			e.rot = FileQuat(os.mesh.rot);
			if (os.mesh.hasBody) body = &os.mesh.body;
			break;
		}
		case kWsLight: {
			const WsLight& l = os.light;
			e.type = kLight;
			e.pos = l.pos;
			e.hasLight = true;
			LightSource& s = e.light;
			s.type = int(l.type);
			s.color[0] = float((l.argb >> 16) & 0xff) / 255.f;
			s.color[1] = float((l.argb >> 8) & 0xff) / 255.f;
			s.color[2] = float(l.argb & 0xff) / 255.f;
			s.dir = l.dir;
			if (s.dir.LengthSq() > 1e-12f) s.dir /= s.dir.Length();
			s.intensity = l.intensity;
			s.range = l.range;
			s.startFalloff = l.startFalloff;
			s.coneOuterCos = l.coneCos;
			s.coneCos = l.coneInnerCos;
			s.dynamic = (os.base.flags & kFlagDynamicLight) != 0;
			s.projector = WsText(l.projector);
			break;
		}
		case kWsParticle: {
			const WsParticle& p = os.particle;
			e.type = kParticleFX;
			e.pos = p.pos;
			e.rot = FileQuat(p.v614, p.f610);
			e.parentOffset = p.parentOffset;
			for (const WsParticle::Emitter& em : p.emitters) {
				Entity::EmitterRec r;
				r.file = WsText(em.file);
				r.setup = true;
				r.scale = em.scale;
				r.offset = em.offset;
				r.rotDeg = em.rotDeg;
				e.emitterRecs.push_back(r);
			}
			break;
		}
		case kWsBillboard: {
			const WsBillboard& b = os.billboard;
			e.type = kBillboard;
			e.pos = b.pos;
			e.rot = FileQuat(b.v614, b.f610);
			// SetupCorona's nine floats, from where 0x10137b70 stores them (WsBillboard::f).
			static constexpr int kField[9] = {5, 2, 3, 10, 8, 9, 7, 11, 12};
			e.hasCorona = true;
			for (int i = 0; i < 9; ++i) e.coronaArgs[i] = b.f[kField[i]];
			e.coronaTex = WsText(b.texture);
			std::memcpy(&e.coronaColor, &b.f[0], 4);
			e.coronaBlend = CoronaBlendIndex(b.blend);
			e.coronaSpriteOnly = b.corona == 0;
			break;
		}
		default:
			++skipped[WsTypeName(os.type)];
			continue;
		}

		if (body) {
			const WsPhysicsObject& p = *body;
			e.bodyType = int(p.type);
			// A pack's mesh body (CreatePhysicsObjectFromMesh: types 4-8, 11-13) takes the
			// mesh at the entity's scale; the 1.0 its PhysicsObject carries is not one.
			const bool fromMesh = os.type == kWsWorldMesh &&
					((p.type >= 4 && p.type <= 8) || (p.type >= 11 && p.type <= 13));
			e.bodyArgScale = fromMesh ? -1.f : p.scaleArg;
			e.bodyMass = p.mass;
			// CObject:PO_Create's PO_SetFreedomOfRotation. Without it a bench keeps the
			// inertia of its default mass under a mass of 1000 and sinks through the floor.
			// Mode 1 is CreatePhysicsObject's own default, i.e. never set.
			if (p.freedom != 1) {
				e.bodyFreedomMode = p.freedom;
				e.bodyFreedomSoft = 1.f;
			}
			e.bodyFriction = p.friction;
			e.bodyRestitution = p.restitution;
			e.bodyLinDamp = p.linearDamping;
			e.bodyAngDamp = p.angularDamping;
			e.collisionGroup = p.collisionGroup;
			// ENTITY.EnableCollisions' minimum time and strength ride on the ^Q header.
			if (p.constraints.hasHeader) {
				e.collisionsOn = true;
				e.collisionMinTime = p.constraints.header[0];
				e.collisionMinStrength = p.constraints.header[1];
			}
			e.isGrenade = (p.flags & kPoGrenade) != 0;
			e.velocity = p.velocity;
			e.angVel = p.angularVelocity;
			if (p.flags & kPoMonster) {
				e.isMonster = true;
				// PO_SetSightParams' storage; the pitch is kept at degrees * pi/180, and
				// ours is the half angle our native derives.
				e.sightRange = p.sight[0];
				e.sightRange360 = p.sight[1];
				e.sightHalfYaw = p.sight[2];
				e.sightHalfPitch = p.sight[3] * 0.5f;
				e.moveWish = p.moveWish;
				e.monsterMoveConst = p.moveConst;
				e.monsterMoveFlag = p.moveFlag != 0;
				e.monsterFlying = (p.flags & kPoFlying) != 0;
			}
		}
		e.physicsBody = body ? 0 : -1; // RebuildEntity reads "had a body" here
		e.ragdollSlot = -1;
		RebuildEntity(handle, e);
		maxHandle = std::max(maxHandle, handle);
		++made;
	}

	// Parents, once every entity exists. A bound effect rides its joint; the other
	// children keep the pose they were saved in.
	size_t bound = 0;
	for (const WsEntity& os : save.entities) {
		if (!os.base.hasParent) continue;
		Entity* child = Find(os.base.handle);
		if (!child) continue;
		child->parent = os.base.parent;
		child->dieWithParent = os.base.dieWithParent != 0;
		if (Entity* parent = Find(os.base.parent)) {
			std::vector<int>& kids = parent->children;
			if (std::find(kids.begin(), kids.end(), os.base.handle) == kids.end())
				kids.push_back(os.base.handle);
		}
		if (os.base.follows && child->type == kParticleFX) {
			child->parentBound = true;
			child->parentJoint.clear();
			child->parentJointIndex = int(int8_t(os.base.joint));
			PlaceAttached(*child);
			++bound;
		}
	}

	// Corpses and ragdoll props, from the limb poses the solver held. Parts are
	// matched by bone name; the file's bodies follow the .hke's order. A saved
	// position is the Havok body's, which sits DISPLACEMENT (times the scale) short
	// of our part frame: C3L1_LampA's joint6 is 124.1 * 0.72 = 89.4 below it. A
	// fixed part (.hke mass 0) keeps the seed; the original never moves one.
	size_t ragdollsPosed = 0;
	float worstSeedGap = 0.f;
	std::string worstSeedModel;
	for (const auto& rd : ragdolls) {
		Entity* e = Find(rd.first);
		if (!e || !physics_) continue;
		// The damping pair and the flag byte (Ragdoll +0x2c, +0x30; 0x10 moved by
		// explosions), which EnableRagdoll applies.
		e->ragdollLinearDamping = rd.second->tail[0];
		e->ragdollAngularDamping = rd.second->tail[1];
		e->ragdollMovedByExplosions = (rd.second->flag & 0x10) != 0;
		if (!EnableRagdoll(*e, true)) continue;
		const Hke* def = RagdollDef(e->source);
		const std::vector<std::string>& parts = physics_->RagdollBones(e->ragdollSlot);
		const std::vector<WsRagdoll::Body>& saved = rd.second->bodies;
		if (!def || def->bodies.size() != saved.size()) continue;
		// A limb's collision callback (EnableCollisionsToRagdoll): minimum time, strength.
		for (size_t b = 0; b < saved.size(); ++b) {
			if (!saved[b].hasExtra) continue;
			const int joint = JointIndexByName(*e, def->bodies[b].bone);
			if (joint < 0) continue;
			Entity::RagdollCallback& cb = e->ragdollCallbacks[joint];
			cb.minTime = saved[b].extra[0];
			cb.minStrength = saved[b].extra[1];
		}
		std::vector<float> seed(parts.size() * 16, 0.f);
		physics_->GetRagdollPose(e->ragdollSlot, seed.data());
		std::vector<float> pose(parts.size() * 16, 0.f);
		size_t placed = 0;
		for (size_t p = 0; p < parts.size(); ++p) {
			for (size_t b = 0; b < def->bodies.size(); ++b) {
				if (def->bodies[b].bone != parts[p]) continue;
				float* m = &pose[p * 16];
				++placed;
				if (def->bodies[b].mass <= 0.f) {
					std::memcpy(m, &seed[p * 16], 16 * sizeof(float));
					break;
				}
				float rot9[9];
				EngineQuatToRot9(FileQuat(saved[b].rot), rot9);
				for (int r = 0; r < 3; ++r)
					for (int c = 0; c < 3; ++c) m[r * 4 + c] = rot9[r * 3 + c];
				const Vec3& d = def->bodies[b].displacement;
				for (int c = 0; c < 3; ++c)
					m[12 + c] = saved[b].pos[c] +
							e->scale * (d[0] * rot9[c] + d[1] * rot9[3 + c] + d[2] * rot9[6 + c]);
				m[15] = 1.f;
				float d2 = 0.f;
				for (int c = 0; c < 3; ++c) {
					const float k = m[12 + c] - seed[p * 16 + 12 + c];
					d2 += k * k;
				}
				if (std::sqrt(d2) > worstSeedGap) {
					worstSeedGap = std::sqrt(d2);
					worstSeedModel = e->source + "/" + parts[p];
				}
				break;
			}
		}
		if (placed != parts.size()) continue;
		physics_->SetRagdollPose(e->ragdollSlot, pose.data(), /*kinematic=*/false);
		++ragdollsPosed;
	}

	// PATH handles are Pathfinder2 slots from 0 (0x1013aa20). A live one keeps its
	// next point and destination, and WaypointGPath2::Load routes between them again.
	paths_.assign(save.paths.size(), Route{});
	size_t livePaths = 0;
	for (size_t i = 0; i < save.paths.size(); ++i) {
		if (!save.paths[i].active) continue;
		paths_[i].live = true;
		RoutePath(paths_[i], save.paths[i].from, save.paths[i].to, 0.f);
		++livePaths;
	}

	if (playerHandle_) {
		pawnEnabled_ = true;
		if (pawn_)
			if (Entity* p = Find(playerHandle_)) {
				Vec3 head = p->pos;
				head[1] += PlayerPawn::EyeAboveFloor();
				pawn_->Spawn(head);
			}
	}
	nextHandle_ = std::max(nextHandle_, maxHandle + 1);
	loadedFromSave_ = true;

	std::string skippedText;
	for (const auto& kv : skipped) skippedText += " " + kv.first + " " + std::to_string(kv.second);
	LogInfo("WORLD.LoadGame: original save %s: %zu of %zu entities restored, %zu effects bound, "
			"%zu of %zu ragdolls posed (worst moving part %.2f from its seed, %s), %zu live paths, "
			"player #%d; not restored:%s",
			path.c_str(), made, save.entities.size(), bound, ragdollsPosed, ragdolls.size(), worstSeedGap,
			worstSeedModel.c_str(), livePaths, playerHandle_,
			skippedText.empty() ? " nothing" : skippedText.c_str());

	// The music (MilesEngine::LoadAudio): each stream reopened at its saved byte. The
	// scripts deleted theirs before this load and only start one on a change.
	const std::vector<WsStream>& streams = audio.streams;
	size_t musicSlots = 0;
	if (audio_ && audioRead) {
		for (size_t slot = 0; slot < streams.size(); ++slot) {
			if (!streams[slot].present) continue;
			std::string name = WsText(streams[slot].file);
			const size_t cut = name.find_last_of("/\\");
			if (cut != std::string::npos) name.erase(0, cut + 1);
			const size_t dot = name.rfind('.');
			if (dot != std::string::npos) name.erase(dot);
			AudioEngine::StreamState state;
			state.name = name;
			state.offset = streams[slot].position;
			state.volume = streams[slot].volume;
			state.playing = true;
			state.paused = !streams[slot].resumes; // SaveGame_ResumeSounds starts the rest
			state.loop = streams[slot].loopCount == 0;
			if (audio_->RestoreStream(int(slot), state)) ++musicSlots;
		}
	}
	LogInfo("WORLD.LoadGame: %zu music streams reopened, %zu 2D and %zu 3D sounds restored",
			musicSlots, audio.sounds2D.size(), audio.sounds3D.size());

	if (host_) host_->RunString("SaveGame:AfterLoadEntities()");
	// LoadPortalState: the antiportal flags by index, once the Slabs have remade theirs.
	for (size_t i = 0; i < save.antiportals.size() && i < antiportals_.size(); ++i)
		antiportals_[i].enabled = save.antiportals[i] != 0;
	return true;
}

} // namespace painful
