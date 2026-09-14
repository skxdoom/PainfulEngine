// ScriptEngine: the "C^" world file WORLD.SaveGame writes, built from our entities
// so that Painkiller itself loads a save made here. ScriptWorldSave.cpp reads the
// same format. Where each value comes from - a loader, a native, or the census of
// six original saves: Docs/Reference/Formats.md, "Writing the original world save".

#include "ScriptEngineInternal.h"
#include "../Assets/Hke.h"
#include "../Assets/WorldSave.h"
#include "../Core/Matrix.h"
#include "../World/PhysicsWorld.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>

namespace painful {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

uint32_t Bits(float f) {
	uint32_t v = 0;
	std::memcpy(&v, &f, 4);
	return v;
}

// The engine's strings count their NUL.
std::string Z(const std::string& s) { return s + '\0'; }

// x, y, z then w, where our Quat is w first.
void FileRot(const Quat& q, float out[4]) {
	const float* p = q;
	out[0] = p[1];
	out[1] = p[2];
	out[2] = p[3];
	out[3] = p[0];
}

// SetupCorona's material blend from CBillboard's editor index (0x10137b70).
uint32_t CoronaBlendStored(int index) {
	switch (index) {
	case 2: return 2;
	case 3: return 4;
	case 4: return 5;
	default: return 1;
	}
}

// A point in a .hke body's own frame: the inverse of its translation and ROTATION.
Vec3 LocalFromWorld(const HkeBody& b, const Vec3& point) {
	Vec3 v;
	for (int i = 0; i < 3; ++i) v[i] = point[i] - b.translation[i];
	const float len = std::sqrt(b.rotAxis[0] * b.rotAxis[0] + b.rotAxis[1] * b.rotAxis[1] +
			b.rotAxis[2] * b.rotAxis[2]);
	if (len < 1e-6f || std::fabs(b.rotAngle) < 1e-9f) return v;
	const float k[3] = {b.rotAxis[0] / len, b.rotAxis[1] / len, b.rotAxis[2] / len};
	const float c = std::cos(-b.rotAngle), s = std::sin(-b.rotAngle);
	const float kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
	const float kx[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
	Vec3 out;
	for (int i = 0; i < 3; ++i) out[i] = v[i] * c + kx[i] * s + k[i] * kv * (1.f - c);
	return out;
}

// A constraint pivot in the Havok body's space: the .hke's, plus the body's
// DISPLACEMENT, times the scale (C3L1_LampA joint6: (7.474 + 124.109) * 0.72 = 94.74).
void HavokPivot(const HkeBody& b, const Vec3& local, float scale, float out[4]) {
	for (int i = 0; i < 3; ++i) out[i] = (local[i] + b.displacement[i]) * scale;
	out[3] = 0.f;
}

float InertiaOf(float inverse) { return inverse > 0.f ? 1.f / inverse : kInf; }

// FUN_1018aeb0's 120 bytes for a player standing still: the constants every original
// save holds, and the last position, which LoadPO overwrites with the body's anyway.
void PlayerController(uint8_t out[120], const Vec3& feet) {
	std::memset(out, 0, 120);
	const auto f = [&](size_t at, float v) { std::memcpy(out + at, &v, 4); };
	const uint32_t two = 2;
	std::memcpy(out + 30, &two, 4); // +0x34
	std::memcpy(out + 34, &two, 4); // +0x38
	f(42, 0.5f); // +0x40
	f(62, 8.f); // +0x6c
	f(67, 1.f); // +0x74
	f(71, -100.f); // +0x7c
	for (size_t i = 0; i < 3; ++i) f(103 + 4 * i, feet[i]); // +0x54
}

} // namespace

void ScriptEngine::BuildWorldSave(WorldSave& out) {
	out = WorldSave();
	out.version = 3;
	// An empty audio chunk: LoadGame resets Miles for it.
	out.audio.name = Z("AUDIOv01");
	// PhysicsEngine+0xc and +0x14, 8 and 1 in every original save; no world state.
	out.physicsC = Bits(8.f);
	out.physics14 = Bits(1.f);
	out.e6dc = world_.demonFx ? 1 : 0;

	out.paths.resize(paths_.size());
	for (size_t i = 0; i < paths_.size(); ++i) {
		out.paths[i].active = paths_[i].live ? 1 : 0;
		out.paths[i].from = paths_[i].from;
		out.paths[i].to = paths_[i].to;
	}
	// LoadPortalState wants exactly the level's antiportal count; portal and zone
	// states may be fewer, so they are left as the level loads them.
	for (const AntiPortal& a : antiportals_) out.antiportals.push_back(a.enabled ? 1 : 0);

	// The audio. The scripts delete their streams on a load and start one only on a
	// change, so LoadAudio reopens them here, and SaveGame_ResumeSounds restarts the
	// ones in the pause set: those that were playing. The sounds keep the IDs the
	// scripts' tables hold, each file in the sample cache the loader looks it up in.
	if (audio_) {
		WsAudio audio;
		const std::vector<AudioEngine::StreamState> states = audio_->StreamStates();
		std::vector<WsStream>& streams = audio.streams;
		streams.resize(states.size());
		bool any = false;
		for (size_t slot = 0; slot < states.size(); ++slot) {
			const AudioEngine::StreamState& st = states[slot];
			if (st.name.empty()) continue;
			WsStream& s = streams[slot];
			s.present = 1;
			s.file = Z("../Data/Music/" + st.name + ".mp3");
			s.volume = st.volume;
			s.loopCount = st.loop ? 0 : 1;
			s.position = uint32_t(st.offset);
			s.resumes = st.playing && !st.paused;
			any = true;
		}
		audio.next2D = uint32_t(audio_->NextId(false));
		audio.next3D = uint32_t(audio_->NextId(true));
		std::map<std::string, size_t> sampleOf;
		for (const AudioEngine::VoiceState& v : audio_->VoiceStates()) {
			const std::string file = Z("../Data/Sounds/" + v.name + ".wav");
			if (!sampleOf.count(v.name)) {
				WsSample sample;
				sample.file = file;
				int maxInstances = 0, intervalMs = 0;
				audio_->GetSoundProperties(v.name, maxInstances, intervalMs);
				sample.maxInstances = uint32_t(maxInstances);
				sample.minIntervalMs = uint32_t(intervalMs);
				// LoadAudio takes now minus this and clamps at 0: started long ago.
				sample.sinceLastStart = 0xffffffffu;
				sampleOf[v.name] = audio.samples.size();
				audio.samples.push_back(sample);
			}
			const uint32_t maxInstances = audio.samples[sampleOf[v.name]].maxInstances;
			// SaveGame_PauseSounds files what plays in set 0, its forget bit with it.
			WsPauseEntry pause;
			pause.present = v.resumes ? 1 : 0;
			pause.forget = v.resumes && v.forget ? 1 : 0;
			const uint8_t flags = uint8_t((v.forget && !v.resumes ? 1 : 0) | (v.sameSpeed ? 8 : 0));
			if (!v.positional) {
				WsSound2D r;
				r.id = uint32_t(v.id);
				r.position = v.offset;
				r.volume = v.volume;
				r.targetVolume = v.volume;
				r.file = file;
				r.loopCount = uint32_t(v.loopCount);
				r.flags = flags;
				r.maxInstances = maxInstances;
				r.speed = v.speed;
				r.pause = pause;
				audio.sounds2D.push_back(r);
			} else {
				WsSound3D r;
				r.id = uint32_t(v.id);
				r.position = v.offset;
				r.file = file;
				r.flags = flags;
				r.loopCount = uint32_t(v.loopCount);
				r.minDistance = v.dist1;
				r.maxDistance = v.dist2;
				r.pos = v.pos;
				r.volume = v.volume;
				r.targetVolume = v.volume;
				r.maxInstances = maxInstances;
				r.speed = v.speed;
				r.pause = pause;
				audio.sounds3D.push_back(r);
			}
			any = true;
		}
		if (any) out.audio.body = WsAudioWrite(audio);
	}

	std::map<std::string, std::vector<std::string>> meshNames;
	const auto meshesOf = [&](const std::string& model) -> const std::vector<std::string>& {
		auto it = meshNames.find(model);
		if (it == meshNames.end()) {
			Model m;
			std::vector<std::string> names;
			if (Model::Load(dataRoot_ + "/Models/" + model + ".pkmdl", m))
				for (const ModelMesh& mesh : m.meshes) names.push_back(mesh.name);
			it = meshNames.emplace(model, std::move(names)).first;
		}
		return it->second;
	};

	std::vector<int> handles;
	handles.reserve(entities_.size());
	for (const auto& kv : entities_) handles.push_back(kv.first);
	std::sort(handles.begin(), handles.end());

	std::map<std::string, size_t> skipped;
	size_t ragdollOrdinal = 0, ragdollsWritten = 0, ragdollsHeld = 0;
	for (int handle : handles) {
		Entity& e = entities_[handle];
		if (e.worldObject) continue; // the level's own; SaveEntities skips them too
		const bool player = handle == playerHandle_;
		WsEntity w;

		const auto base = [&](uint32_t flags, uint32_t pass) {
			WsEntityBase& b = w.base;
			b.handle = handle;
			b.fb0 = e.timeToDie >= 0.f ? e.timeToDie : -1.f; // SetTimeToDie, +0xb0
			b.fb4 = 0.5f;
			b.deathZoneTest = e.deathZoneTest ? 1 : 0;
			b.u24 = pass; // +0x24: EnableGunPass writes 7
			b.b28 = e.demonic ? 1 : 0; // EnableDemonic
			b.flags = flags;
			if (e.parent != 0 && Find(e.parent)) {
				b.hasParent = 1;
				b.parent = e.parent;
				b.joint = uint8_t(int8_t(e.parentJointIndex >= 0 ? e.parentJointIndex : -1));
				b.follows = 1;
				b.dieWithParent = e.dieWithParent ? 1 : 0;
			}
			w.inWorld = e.inWorld ? 1 : 0;
		};

		// PhysicsObject::SavePO's record. Flags, damping defaults and the inertia rule
		// follow the original saves: a FullFree item's tensor is 0.4 * mass, a locked
		// axis infinite, a pinned body's zero.
		const auto bodyOf = [&](WsPhysicsObject& p, bool monster) {
			const int slot = e.physicsBody;
			const bool have = physics_ && slot >= 0;
			const bool pinned = have && physics_->IsScriptBodyPinned(slot);
			p.type = player ? 100u : uint32_t(e.bodyType);
			p.scaleArg = monster ? -1.f : player ? 1.f : e.bodyArgScale > 0.f ? e.bodyArgScale : 1.f;
			if (player) p.flags = 0x1001;
			else if (monster) p.flags = 0x1012 | (e.monsterFlying ? 0x800 : 0);
			else p.flags = e.collisionsOn ? 0x8 : p.type == 7 ? 0x2000 : 0x1000;
			if (e.isGrenade) p.flags |= 0x20;
			p.freedom = uint8_t(player || monster ? 1 : have ? physics_->ScriptBodyFreedomMode(slot)
					: std::max(e.bodyFreedomMode, 1));
			if (player && pawn_) {
				pawn_->Velocity(p.velocity);
			} else if (have) {
				physics_->GetScriptBodyVelocity(slot, p.velocity);
				physics_->GetScriptBodyAngularVelocity(slot, p.angularVelocity);
			}
			p.collisionGroup = uint8_t(player ? 23 : e.collisionGroup ? e.collisionGroup : monster ? 4 : 3);
			p.b2 = uint8_t(player || (have && physics_->ScriptBodyInWorld(slot)) ? 1 : 0);
			p.b3 = uint8_t(player || (have && physics_->ScriptBodyAwake(slot)) ? 1 : 0);
			// A monster's own body is out of the traces; its ragdoll's limbs are what shots hit.
			p.lineTrace = uint8_t(player || (!monster && e.inSolver) ? 1 : 0);
			p.mass = player ? PlayerPawn::Mass() : e.bodyMass > 0.f ? e.bodyMass
					: have ? physics_->ScriptBodyMass(slot) : 1.f;
			if (p.mass <= 0.f) p.mass = 1.f;
			p.friction = player ? 0.f : e.bodyFriction;
			p.restitution = player ? 0.f : e.bodyRestitution;
			p.linearDamping = e.bodyLinDamp >= 0.f ? e.bodyLinDamp : player || monster || pinned ? 0.f : 0.05f;
			p.angularDamping = e.bodyAngDamp >= 0.f ? e.bodyAngDamp : 0.f;
			p.b5 = pinned ? 1 : 0;
			const float radius = have ? physics_->ScriptBodyRadius(slot) : 0.f;
			p.u18 = Bits(player || monster || radius <= 0.f ? 1.f : radius);
			float diag[3] = {0.f, 0.f, 0.f};
			Vec3 inv;
			if (player || monster) {
				diag[0] = diag[1] = diag[2] = kInf;
			} else if (pinned) {
				// zero
			} else if (p.freedom == 3) {
				diag[0] = diag[1] = diag[2] = 0.4f * p.mass;
			} else if (have && physics_->ScriptBodyInverseInertia(slot, inv)) {
				for (int c = 0; c < 3; ++c) diag[c] = InertiaOf(inv[c]);
			} else {
				diag[0] = diag[1] = diag[2] = 0.4f * p.mass;
			}
			p.inertia[0] = diag[0];
			p.inertia[4] = diag[1];
			p.inertia[8] = diag[2];
			if (monster) {
				p.sight[0] = e.sightRange;
				p.sight[1] = e.sightRange360;
				p.sight[2] = e.sightHalfYaw;
				p.sight[3] = e.sightHalfPitch * 2.f;
				p.moveWish = e.moveWish;
				p.ext60 = Vec3{0.f, 1.f, 0.f}; // the floor normal
				p.moveConst = e.monsterMoveConst;
				p.moveFlag = e.monsterMoveFlag ? 1 : 0;
				p.ext71 = 1; // on the floor
			}
			if (player) {
				p.hasController = 1;
				Vec3 feet = e.pos;
				if (pawn_) pawn_->FloorPos(feet);
				PlayerController(p.controller, feet);
			} else if (e.collisionsOn && !monster) {
				// ENTITY.EnableCollisions' minimum time and strength.
				p.constraints.hasHeader = 1;
				p.constraints.header[0] = e.collisionMinTime;
				p.constraints.header[1] = e.collisionMinStrength;
			}
		};

		// A simulating ragdoll, limb by limb in .hke order. Only the joint kinds the
		// original saves showed (ragdoll, hinge); a ragdoll with springs or dashpots is
		// written as idle instead, since a wrong kind byte fails the whole load.
		const auto ragdollOf = [&](WsRagdoll& r, const Hke& def) -> bool {
			if (!physics_ || !physics_->RagdollActive(e.ragdollSlot)) return false;
			if (!def.springs.empty() || !def.dashpots.empty()) return false;
			const std::vector<std::string>& parts = physics_->RagdollBones(e.ragdollSlot);
			const float s = e.scale;
			const uint8_t layer = uint8_t(10 + ragdollOrdinal % 7);
			const auto named = [&](const std::string& bone) -> const HkeBody* {
				for (const HkeBody& hb : def.bodies)
					if (hb.bone == bone) return &hb;
				return nullptr;
			};
			r.bodies.assign(def.bodies.size(), WsRagdoll::Body());
			for (size_t b = 0; b < def.bodies.size(); ++b) {
				const HkeBody& hb = def.bodies[b];
				int part = -1;
				for (size_t p = 0; p < parts.size(); ++p)
					if (parts[p] == hb.bone) part = int(p);
				if (part < 0) return false;
				WsRagdoll::Body& rb = r.bodies[b];
				Vec3 pos;
				Quat rot;
				physics_->GetRagdollPartPosition(e.ragdollSlot, part, pos);
				physics_->GetRagdollPartRotation(e.ragdollSlot, part, rot);
				float rot9[9];
				EngineQuatToRot9(rot, rot9);
				const Vec3& d = hb.displacement;
				for (int c = 0; c < 3; ++c)
					rb.pos[c] = pos[c] - s * (d[0] * rot9[c] + d[1] * rot9[3 + c] + d[2] * rot9[6 + c]);
				FileRot(rot, rb.rot);
				physics_->GetRagdollPartVelocity(e.ragdollSlot, part, rb.velocity, rb.angularVelocity);
				const bool fixed = hb.mass <= 0.f;
				rb.flag = fixed ? 1 : 0;
				for (const auto& cb : e.ragdollCallbacks) {
					if (RagdollPartOfJoint(e, cb.first) != part) continue;
					rb.hasExtra = 1;
					rb.extra[0] = cb.second.minTime;
					rb.extra[1] = cb.second.minStrength;
					break;
				}
				rb.mass = fixed ? 0.f : hb.mass * s * s * s;
				rb.b = layer;
				rb.c = 2.5f;
				rb.d = 0.4f;
				Vec3 inv;
				if (!fixed && physics_->RagdollPartInverseInertia(e.ragdollSlot, part, inv)) {
					rb.e = InertiaOf(inv[0]);
					rb.f[3] = InertiaOf(inv[1]);
					rb.f[7] = InertiaOf(inv[2]);
				}
				rb.g = physics_->RagdollPartAwake(e.ragdollSlot, part) ? 1 : 0;
			}
			r.tail[0] = e.ragdollLinearDamping >= 0.f ? e.ragdollLinearDamping : def.linearDrag;
			r.tail[1] = e.ragdollAngularDamping >= 0.f ? e.ragdollAngularDamping : def.angularDrag;
			r.tail[2] = 1.f;
			r.constraints.assign(def.constraints.size(), WsRagdoll::Constraint());
			for (size_t k = 0; k < def.constraints.size(); ++k) {
				const HkeConstraint& hc = def.constraints[k];
				const HkeBody* a = named(hc.bodyA);
				const HkeBody* b = named(hc.bodyB);
				if (!a || !b || hc.kind == HkeConstraint::kStiffSpring) return false;
				WsRagdoll::Constraint& rc = r.constraints[k];
				const uint8_t brk = hc.breakable ? 10 : 0;
				if (hc.kind == HkeConstraint::kRagdoll) {
					rc.type = brk;
					HavokPivot(*a, hc.worldSpace ? LocalFromWorld(*a, hc.worldPivot) : hc.csToRef[3], s, rc.data);
					HavokPivot(*b, hc.worldSpace ? LocalFromWorld(*b, hc.worldPivot) : hc.csToAtt[3], s, rc.data + 4);
				} else {
					rc.type = uint8_t((hc.limited ? 1 : 2) + brk);
					HavokPivot(*a, hc.worldSpace ? LocalFromWorld(*a, hc.worldHingePos) : hc.hingePosA, s, rc.data);
					HavokPivot(*b, hc.worldSpace ? LocalFromWorld(*b, hc.worldHingePos) : hc.hingePosB, s, rc.data + 4);
				}
				if (hc.breakable) rc.strength = hc.linearStrength; // LINEAR_STRENGTH, not STRENGTH
				rc.after = 1;
			}
			r.flag = e.ragdollMovedByExplosions ? 0x11 : 0x01;
			r.gravity = Vec3{0.f, -19.62f, 0.f};
			return true;
		};

		switch (e.type) {
		case kModel: {
			if (e.source.empty()) {
				++skipped["Model without a model"];
				continue;
			}
			const bool monster = e.isMonster && !player;
			w.type = kWsModel;
			w.resource = Z(e.source);
			w.name = Z(e.name);
			w.headerValue = Bits(e.scale);
			base(player ? 0x260u : e.visible ? 0u : 0x40u, e.collisionGroup == 7 ? 7u : 1u);
			WsModel& m = w.model;
			m.pos = e.pos;
			if (player && pawn_) pawn_->FloorPos(m.pos);
			FileRot(e.rot, m.rot);
			m.f700 = 1.f;
			m.material = Z(e.materialName);
			m.shadow = monster ? 1 : 0;
			for (const std::string& mesh : meshesOf(e.source)) {
				const auto it = e.hiddenMeshes.find(mesh);
				m.meshVisible.push_back(it == e.hiddenMeshes.end() || it->second ? 1 : 0);
			}
			m.slotCount = uint32_t(std::max<size_t>(e.animSlots.size(), 1));
			for (size_t s = 1; s < e.animSlots.size(); ++s) {
				const Entity::AnimSlot& a = e.animSlots[s];
				WsModel::Slot slot;
				slot.present = a.name.empty() ? 0 : 1;
				slot.anim = Z(a.name);
				slot.loop = a.loop ? 1 : 0;
				slot.curveMask = a.curveMask;
				m.slots.push_back(slot);
			}
			WsModel::Channel channel;
			channel.weight = 1.f;
			channel.speed = 1.f;
			if (e.animIndex > 0 && size_t(e.animIndex) < e.animSlots.size()) {
				channel.slot = uint8_t(e.animIndex);
				channel.time = e.animTime;
				channel.speed = e.animScale;
				channel.loop = e.animLoop ? 1 : 0;
				// SetAnim's blend time, in both fields as every original save has it; 0.201 is
				// CActor's default and 0.2 the items'.
				channel.blend = e.blendTotal > 0.f ? e.blendTotal : monster ? 0.201f : 0.2f;
				channel.blendLeft = channel.blend;
				m.channels.push_back(channel);
			} else if (e.ragdollSlot >= 0) {
				m.channels.push_back(channel); // a corpse's: slot 0 at full weight
			}
			if (player || monster || e.physicsBody >= 0) {
				m.hasBody = 1;
				bodyOf(m.body, monster);
			}
			if (const Hke* def = RagdollDef(e.source)) {
				if (e.ragdollSlot >= 0 && ragdollOf(m.ragdoll, *def)) {
					m.ragdollOn = 1;
					++ragdollOrdinal;
					++ragdollsWritten;
				} else {
					if (e.ragdollSlot >= 0) ++ragdollsHeld;
					m.hasGroups = 1;
					m.groups.tag = 0x5e49;
					m.groups.records.assign(def->bodies.size(), WsRagdollGroups::Record());
					for (WsRagdollGroups::Record& rec : m.groups.records) rec.after = 6;
				}
			}
			break;
		}
		case kMesh: {
			std::string pack;
			if (!SplitPackSource(e.source, pack)) {
				++skipped["Mesh outside Items"];
				continue;
			}
			w.type = kWsWorldMesh;
			w.resource = Z(e.source);
			w.name = Z(e.mesh.empty() ? e.name : e.mesh);
			w.headerValue = Bits(e.scale);
			w.headerFlag = e.meshCentred ? 0x80 : 0;
			base(0x01000604u | (e.meshCentred ? 0x80u : 0u) | (e.visible ? 0u : 0x40u), 1);
			WsWorldMesh& mesh = w.mesh;
			mesh.pos = e.pos;
			FileRot(e.rot, mesh.rot);
			mesh.hasBody = e.physicsBody >= 0 ? 1 : 0;
			mesh.material = Z("special/detail");
			mesh.materialData[2] = Bits(8.2f);
			mesh.materialData[3] = Bits(7.1f);
			mesh.s684 = Z("");
			mesh.s690 = Z("");
			if (mesh.hasBody) bodyOf(mesh.body, false);
			break;
		}
		case kLight: {
			w.type = kWsLight;
			w.resource = Z(e.source);
			w.name = Z(e.name);
			w.headerValue = Bits(1.f);
			const uint32_t flags = 0x240u | (e.light.dynamic ? 0x400000u : 0u);
			base(flags, 4);
			WsLight& l = w.light;
			const auto byte = [](float v) {
				return uint32_t(std::max(0L, std::min(255L, std::lround(v * 255.f))));
			};
			l.flags = flags;
			l.pos = e.pos;
			l.type = uint32_t(e.light.type);
			l.argb = (byte(e.light.color[0]) << 16) | (byte(e.light.color[1]) << 8) | byte(e.light.color[2]);
			l.intensity = e.light.intensity;
			l.dir = e.light.dir;
			l.coneCos = e.light.coneOuterCos;
			l.coneInnerCos = e.light.coneCos;
			l.range = e.light.range;
			l.startFalloff = e.light.startFalloff;
			l.projector = Z(e.light.projector);
			break;
		}
		case kParticleFX: {
			w.type = kWsParticle;
			w.resource = Z(e.source);
			w.name = Z(e.name);
			w.headerValue = Bits(e.scale);
			const uint32_t flags = 0x200u | (e.visible ? 0u : 0x40u);
			base(flags, 4);
			WsParticle& p = w.particle;
			p.u18 = flags;
			p.pos = e.pos;
			float r[4];
			FileRot(e.rot, r);
			p.v614 = Vec3{r[0], r[1], r[2]};
			p.f610 = r[3];
			p.parentOffset = e.parentOffset;
			p.bc88 = e.parentRotBound ? 1 : 0;
			FileRot(e.parentRot, r);
			p.vc90 = Vec3{r[0], r[1], r[2]};
			p.fc8c = r[3];
			p.bca8 = 1;
			for (const Entity::EmitterRec& rec : e.emitterRecs) {
				WsParticle::Emitter em;
				em.file = Z(rec.file);
				em.b = 1.f;
				// ASSUMED: which of the five emitter bits is which (ParticleEffect::LoadEntity
				// stores them unnamed); the census's 10100 / 10110 / 10001 read as below.
				em.flags[0] = 1;
				em.flags[2] = rec.setup ? 1 : 0;
				em.flags[3] = rec.evolveSet && rec.evolve ? 1 : 0;
				em.flags[4] = rec.stopped ? 1 : 0;
				em.offset = rec.offset;
				em.rotDeg = rec.rotDeg;
				em.scale = rec.scale;
				p.emitters.push_back(em);
			}
			break;
		}
		case kBillboard: {
			w.type = kWsBillboard;
			w.resource = Z(e.source);
			w.name = Z(e.name);
			w.headerValue = Bits(1.f);
			base(0x200u | (e.visible ? 0u : 0x40u), e.coronaSpriteOnly ? 4u : 6u);
			WsBillboard& b = w.billboard;
			b.pos = e.pos;
			float r[4];
			FileRot(e.rot, r);
			b.v614 = Vec3{r[0], r[1], r[2]};
			b.f610 = r[3];
			b.corona = e.coronaSpriteOnly ? 0 : 1;
			b.blend = CoronaBlendStored(e.coronaBlend);
			b.texture = Z(e.coronaTex);
			std::memcpy(&b.f[0], &e.coronaColor, 4);
			static constexpr int kField[9] = {5, 2, 3, 10, 8, 9, 7, 11, 12};
			for (int i = 0; i < 9; ++i) b.f[kField[i]] = e.coronaArgs[i];
			const uint32_t one = 1;
			b.f[6] = 1.f;
			std::memcpy(&b.f[14], &one, 4);
			b.f[16] = 0.1f;
			break;
		}
		case kDecal:
		case kTrail:
		case kSound:
			++skipped[e.type == kDecal ? "Decal" : e.type == kTrail ? "Trail" : "Sound"];
			continue;
		default:
			continue; // regions and environments: SaveEntities writes neither
		}
		out.entities.push_back(std::move(w));
	}

	std::string skippedText;
	for (const auto& kv : skipped) skippedText += " " + kv.first + " " + std::to_string(kv.second);
	LogInfo("WORLD.SaveGame: world save: %zu entities, %zu ragdolls simulating, %zu written idle, "
			"%zu paths, %zu antiportals; not written:%s",
			out.entities.size(), ragdollsWritten, ragdollsHeld, out.paths.size(), out.antiportals.size(),
			skippedText.empty() ? " nothing" : skippedText.c_str());
}

} // namespace painful
