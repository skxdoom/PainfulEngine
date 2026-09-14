// The script layer, and the assets it drives: animation, sound, waypoints.
#include "Commands.h"
#include "../Core/FileSystem.h"
#include "../Core/Vectors.h"
#include "../Core/Matrix.h"
#include "Core/Debug.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iterator>

namespace {

// --census: the values each field takes over the records that carry it, most
// common first. A writer fills the fields no loader or native names from these.
class Census {
public:
	void Add(const std::string& key, const std::string& value) { ++seen_[key][value]; }
	void U(const std::string& key, uint32_t v) { Add(key, std::to_string(v)); }
	void X(const std::string& key, uint32_t v) {
		char t[16];
		std::snprintf(t, sizeof t, "%08x", v);
		Add(key, t);
	}
	void F(const std::string& key, const float* v, size_t n = 1) {
		std::string s;
		for (size_t i = 0; i < n; ++i) {
			char t[32];
			std::snprintf(t, sizeof t, "%s%.4g", i ? " " : "", v[i]);
			s += t;
		}
		Add(key, s);
	}
	void V(const std::string& key, const Vec3& v) {
		const float t[3] = {v[0], v[1], v[2]};
		F(key, t, 3);
	}
	void S(const std::string& key, const std::string& v) { Add(key, "'" + WsText(v) + "'"); }
	void Print() const {
		for (const auto& kv : seen_) {
			std::vector<std::pair<size_t, std::string>> order;
			size_t total = 0;
			for (const auto& v : kv.second) {
				order.emplace_back(v.second, v.first);
				total += v.second;
			}
			std::stable_sort(order.begin(), order.end(),
					[](const auto& a, const auto& b) { return a.first > b.first; });
			std::string line;
			for (size_t i = 0; i < order.size() && i < 6; ++i)
				line += "  " + order[i].second + " x" + std::to_string(order[i].first);
			if (order.size() > 6) line += "  (+" + std::to_string(order.size() - 6) + " more)";
			LogInfo("  census %s [%zu]:%s", kv.first.c_str(), total, line.c_str());
		}
	}

private:
	std::map<std::string, std::map<std::string, size_t>> seen_;
};

void CensusBody(Census& c, const std::string& owner, const WsPhysicsObject& p) {
	const std::string k = owner + ".po.";
	c.U(k + "type", p.type);
	c.F(k + "scaleArg", &p.scaleArg);
	c.X(k + "flags", p.flags);
	c.U(k + "freedom", p.freedom);
	c.U(k + "group", p.collisionGroup);
	c.U(k + "b2", p.b2);
	c.U(k + "b3", p.b3);
	c.U(k + "lineTrace", p.lineTrace);
	c.F(k + "mass", &p.mass);
	c.F(k + "friction", &p.friction);
	c.F(k + "restitution", &p.restitution);
	c.F(k + "linDamp", &p.linearDamping);
	c.F(k + "angDamp", &p.angularDamping);
	c.U(k + "b5", p.b5);
	c.X(k + "u18", p.u18);
	c.F(k + "inertia", p.inertia, 9);
	float scaled[9];
	for (int i = 0; i < 9; ++i) scaled[i] = p.inertia[i] * p.mass;
	c.F(k + "inertia*mass t" + std::to_string(p.type), scaled, 9);
	c.U(k + "hasController", p.hasController);
	if (p.hasController) {
		// FUN_1018aeb0's fields: u32 +0x1c.., one row per 4 bytes so the census shows each.
		for (size_t i = 0; i + 4 <= sizeof p.controller; i += 4) {
			uint32_t v = 0;
			std::memcpy(&v, p.controller + i, 4);
			char name[32];
			std::snprintf(name, sizeof name, "ctrl@%03zu", i);
			c.X(k + name, v);
		}
	}
	c.U(k + "cons.hasHeader", p.constraints.hasHeader);
	if (p.constraints.hasHeader) c.F(k + "cons.header", p.constraints.header, 3);
	c.U(k + "cons.records", uint32_t(p.constraints.records.size()));
	if (p.flags & 2) {
		c.F(k + "sight", p.sight, 4);
		c.V(k + "ext60", p.ext60);
		c.F(k + "moveConst", &p.moveConst);
		c.U(k + "moveFlag", p.moveFlag);
		c.U(k + "ext71", p.ext71);
	}
}

void CensusEntity(Census& c, const WsEntity& e) {
	std::string t = WsTypeName(e.type);
	if (e.type == kWsModel && e.model.hasBody)
		t += e.model.body.hasController ? "(player)" : (e.model.body.flags & 2) ? "(monster)" : "(body)";
	if (e.type == kWsModel && e.model.ragdollOn) t += "(ragdoll)";
	const WsEntityBase& b = e.base;
	c.U(t + ".headerFlag", e.headerFlag);
	if (e.type != kWsModel) c.X(t + ".headerValue", e.headerValue);
	c.F(t + ".fb0", &b.fb0);
	c.F(t + ".fb4", &b.fb4);
	c.U(t + ".dz", b.deathZoneTest);
	c.U(t + ".u24", b.u24);
	c.U(t + ".b28", b.b28);
	c.X(t + ".flags", b.flags);
	c.X(t + ".u2c", b.u2c);
	c.X(t + ".u30", b.u30);
	c.X(t + ".u34", b.u34);
	c.U(t + ".hasParent", b.hasParent);
	if (b.hasParent) {
		c.Add(t + ".joint", std::to_string(int(int8_t(b.joint))));
		c.U(t + ".follows", b.follows);
		c.U(t + ".die", b.dieWithParent);
	}
	c.U(t + ".inWorld", e.inWorld);
	switch (e.type) {
	case kWsModel: {
		const WsModel& m = e.model;
		float scale = 0.f;
		std::memcpy(&scale, &e.headerValue, 4);
		const float ratio = scale != 0.f ? m.f700 / scale : m.f700;
		c.F(t + ".f700/scale", &ratio);
		c.S(t + ".material", m.material);
		c.U(t + ".shadow", m.shadow);
		uint32_t hidden = 0;
		for (uint8_t v : m.meshVisible) hidden += v ? 0 : 1;
		c.U(t + ".hiddenMeshes", hidden);
		c.U(t + ".slotCount", m.slotCount);
		for (const WsModel::Slot& s : m.slots) {
			c.U(t + ".slot.present", s.present);
			if (!s.present) continue;
			c.U(t + ".slot.loop", s.loop);
			c.U(t + ".slot.curveMask", s.curveMask);
		}
		c.U(t + ".channels", uint32_t(m.channels.size()));
		for (const WsModel::Channel& ch : m.channels) {
			c.U(t + ".ch.slotIsZero", ch.slot == 0);
			c.F(t + ".ch.weight", &ch.weight);
			c.F(t + ".ch.speed", &ch.speed);
			c.U(t + ".ch.loop", ch.loop);
			c.U(t + ".ch.e", ch.e);
			c.F(t + ".ch.blend", &ch.blend);
			c.F(t + ".ch.blendLeft", &ch.blendLeft);
		}
		c.U(t + ".hasBody", m.hasBody);
		c.U(t + ".ragdollOn", m.ragdollOn);
		c.U(t + ".hasGroups", m.hasGroups);
		if (m.hasBody) CensusBody(c, t, m.body);
		if (m.hasGroups) {
			c.X(t + ".groups.tag", m.groups.tag);
			c.U(t + ".groups.n", uint32_t(m.groups.records.size()));
			for (const WsRagdollGroups::Record& r : m.groups.records) {
				c.U(t + ".groups.has", r.has);
				if (r.has) c.F(t + ".groups.v", r.v, 3);
				c.U(t + ".groups.after", r.after);
			}
		}
		if (m.ragdollOn) {
			const WsRagdoll& r = m.ragdoll;
			for (const WsRagdoll::Body& rb : r.bodies) {
				c.U(t + ".rb.flag", rb.flag);
				c.U(t + ".rb.hasExtra", rb.hasExtra);
				if (rb.hasExtra) c.F(t + ".rb.extra", rb.extra, 3);
				c.F(t + ".rb.mass", &rb.mass);
				c.U(t + ".rb.b", rb.b);
				c.F(t + ".rb.c", &rb.c);
				c.F(t + ".rb.d", &rb.d);
				c.F(t + ".rb.e", &rb.e);
				c.F(t + ".rb.f", rb.f, 8);
				c.U(t + ".rb.g", rb.g);
			}
			c.F(t + ".rd.tail", r.tail, 3);
			for (const WsRagdoll::Constraint& k : r.constraints) {
				c.U(t + ".rc.type", k.type);
				if (k.type >= 10) {
					c.U(t + ".rc.breakable", k.breakable);
					c.F(t + ".rc.strength", &k.strength);
				}
				c.F(t + ".rc.data", k.data, 8);
				c.U(t + ".rc.after", k.after);
			}
			for (const WsRagdoll::Action& a : r.actions) {
				c.U(t + ".ra.skip", a.skip);
				c.U(t + ".ra.kind", a.kind);
				c.U(t + ".ra.after", a.after);
			}
			c.U(t + ".rd.flag", r.flag);
			c.V(t + ".rd.gravity", r.gravity);
		}
		break;
	}
	case kWsWorldMesh: {
		const WsWorldMesh& w = e.mesh;
		c.U(t + ".hasBody", w.hasBody);
		c.S(t + ".material", w.material);
		for (int i = 0; i < 4; ++i) c.X(t + ".materialData" + std::to_string(i), w.materialData[i]);
		c.S(t + ".s684", w.s684);
		c.S(t + ".s690", w.s690);
		if (w.hasBody) CensusBody(c, t, w.body);
		break;
	}
	case kWsLight:
		c.X(t + ".lightFlags", e.light.flags);
		c.U(t + ".type", e.light.type);
		c.S(t + ".projector", e.light.projector);
		break;
	case kWsParticle: {
		const WsParticle& p = e.particle;
		c.X(t + ".u18", p.u18);
		c.U(t + ".bc88", p.bc88);
		c.V(t + ".vc90", p.vc90);
		c.F(t + ".fc8c", &p.fc8c);
		c.V(t + ".vc9c", p.vc9c);
		c.U(t + ".bca8", p.bca8);
		c.U(t + ".bcaa", p.bcaa);
		c.U(t + ".bca9", p.bca9);
		c.U(t + ".emitters", uint32_t(p.emitters.size()));
		for (const WsParticle::Emitter& em : p.emitters) {
			c.V(t + ".em.a", em.a);
			c.F(t + ".em.b", &em.b);
			c.V(t + ".em.c", em.c);
			c.V(t + ".em.d", em.d);
			c.Add(t + ".em.flags", std::to_string(em.flags[0]) + std::to_string(em.flags[1]) +
					std::to_string(em.flags[2]) + std::to_string(em.flags[3]) + std::to_string(em.flags[4]));
		}
		break;
	}
	case kWsBillboard: {
		const WsBillboard& bb = e.billboard;
		c.U(t + ".corona", bb.corona);
		c.U(t + ".blend", bb.blend);
		for (int i : {1, 4, 6, 13, 14, 15, 16}) c.F(t + ".f" + std::to_string(i), &bb.f[i]);
		break;
	}
	case kWsDecal:
		c.S(t + ".texture", e.decal.texture);
		c.F(t + ".f678", &e.decal.f678);
		c.F(t + ".f67c", &e.decal.f67c);
		c.F(t + ".f684", &e.decal.f684);
		c.U(t + ".b688", e.decal.b688);
		c.U(t + ".verts", uint32_t(e.decal.verts.size()));
		break;
	case kWsTrail:
		c.F(t + ".f680", &e.trail.f680);
		c.X(t + ".unknown", e.trail.unknown);
		c.F(t + ".f868", &e.trail.f868);
		c.U(t + ".b86c", e.trail.b86c);
		c.U(t + ".capacity", e.trail.capacity);
		c.U(t + ".segments", e.trail.segments);
		break;
	case kWsSound:
		for (int i = 0; i < 10; ++i) c.X(t + ".v" + std::to_string(i), e.sound.v[i]);
		c.U(t + ".b", e.sound.b);
		break;
	}
}

} // namespace

// The original engine's world save, decoded against the mounted data (a model's
// mesh count and its .hke counts are not in the file), then written back and
// compared byte for byte. `option` is "--list" or "--census".
int WorldSaveCmd(const char* path, const char* dataRoot, const char* option) {
	const bool list = !std::strcmp(option, "--list");
	const bool census = !std::strcmp(option, "--census");
	Census cs;
	std::vector<uint8_t> data;
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			LogInfo("%s: cannot open", path);
			return 2;
		}
		data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	const std::string root = dataRoot;
	std::map<std::string, int> meshCounts;
	std::map<std::string, std::pair<int, int>> ragdollCounts;
	WorldSave::Resolver models;
	models.meshCount = [&](const std::string& model) {
		auto it = meshCounts.find(model);
		if (it != meshCounts.end()) return it->second;
		Model m;
		const int n = Model::Load(root + "/Models/" + model + ".pkmdl", m) ? int(m.meshes.size()) : -1;
		meshCounts[model] = n;
		return n;
	};
	models.ragdoll = [&](const std::string& model, int& constraints, int& actions) {
		auto it = ragdollCounts.find(model);
		if (it == ragdollCounts.end()) {
			Hke hke;
			std::pair<int, int> counts(-1, -1);
			if (Hke::Load(root + "/Models/" + model + ".hke", hke))
				counts = {int(hke.constraints.size()), int(hke.springs.size() + hke.dashpots.size())};
			it = ragdollCounts.emplace(model, counts).first;
		}
		constraints = it->second.first;
		actions = it->second.second;
		return constraints >= 0;
	};

	WorldSave save;
	const bool ok = WorldSave::Read(data, save, models);
	LogInfo("%s: %zu bytes, version %u", path, data.size(), save.version);
	LogInfo("  audio chunk %zu bytes; physics @0x%zx, paths @0x%zx, entities @0x%zx, portals @0x%zx",
			save.audio.body.size(), save.physicsAt, save.pathsAt, save.entitiesAt, save.portalsAt);
	size_t layers = 0, groups = 0, bodies = 0, pairs = 0;
	for (const WsPhysicsWorld& w : save.worlds) {
		layers += w.layers.size();
		groups += w.groups.size();
		bodies += w.bodies.size();
		pairs += w.pairs.size();
	}
	size_t activePaths = 0;
	for (const WorldSave::Path& p : save.paths) activePaths += p.active ? 1 : 0;
	LogInfo("  physics: %zu worlds, %zu layers, %zu groups, %zu bodies, %zu pairs; %zu paths, %zu active",
			save.worlds.size(), layers, groups, bodies, pairs, save.paths.size(), activePaths);
	size_t shards = 0;
	for (const WsGlass& g : save.glass)
		for (const WsGlass::Piece& p : g.pieces) shards += p.present ? 1 : 0;
	LogInfo("  glass: %zu panes, %zu shards", save.glass.size(), shards);
	WsAudio audio;
	if (!WsAudioRead(save.audio.body, audio)) LogInfo("  music: the audio chunk does not parse");
	else if (!audio.soundsRead && !save.audio.body.empty()) LogInfo("  sounds: %s", audio.error.c_str());
	const std::vector<WsStream>& streams = audio.streams;
	for (size_t i = 0; i < streams.size(); ++i)
		if (streams[i].present)
			LogInfo("  music slot %zu: '%s' volume %.2f rate %u %u loop %u at byte %u%s", i,
					WsText(streams[i].file).c_str(), streams[i].volume, streams[i].rate, streams[i].u1c,
					streams[i].loopCount, streams[i].position, streams[i].resumes ? ", resumes" : ", paused");
	LogInfo("  sounds: next IDs %u / %u, %zu cached samples, %zu 2D, %zu 3D", audio.next2D, audio.next3D,
			audio.samples.size(), audio.sounds2D.size(), audio.sounds3D.size());
	if (list) {
		for (const WsSound2D& s : audio.sounds2D)
			LogInfo("    2D #%u '%s' flags %02x loop %u at byte %u volume %.2f speed %.2f%s", s.id,
					WsText(s.file).c_str(), s.flags, s.loopCount, s.position, s.volume, s.speed,
					s.pause.present ? ", in the pause set" : "");
		for (const WsSound3D& s : audio.sounds3D)
			LogInfo("    3D #%u '%s' flags %02x loop %u at byte %u volume %.2f at %.1f %.1f %.1f, %.1f..%.1f%s",
					s.id, WsText(s.file).c_str(), s.flags, s.loopCount, s.position, s.volume, s.pos[0],
					s.pos[1], s.pos[2], s.minDistance, s.maxDistance, s.pause.present ? ", in the pause set" : "");
	}

	// Entities read so far: all of them, or up to and including the failing one.
	const size_t parsed = ok ? save.entities.size() : save.entityAt.size();
	std::map<std::string, size_t> byType;
	size_t withBody = 0, ragdolls = 0, parented = 0;
	for (size_t i = 0; i < parsed; ++i) {
		const WsEntity& e = save.entities[i];
		++byType[WsTypeName(e.type)];
		if ((e.type == kWsModel && e.model.hasBody) || (e.type == kWsWorldMesh && e.mesh.hasBody)) ++withBody;
		if (e.type == kWsModel && e.model.ragdollOn) ++ragdolls;
		if (e.base.hasParent) ++parented;
		if (census) CensusEntity(cs, e);
		if (!list) continue;
		const Vec3* pos = e.type == kWsModel ? &e.model.pos
				: e.type == kWsWorldMesh ? &e.mesh.pos
				: e.type == kWsLight ? &e.light.pos
				: e.type == kWsParticle ? &e.particle.pos
				: e.type == kWsBillboard ? &e.billboard.pos
				: e.type == kWsTrail ? &e.trail.pos : nullptr;
		LogInfo("  #%zu @0x%zx %-14s h%-5d %s / %s%s  (%.2f %.2f %.2f)", i, save.entityAt[i],
				WsTypeName(e.type), e.base.handle, WsText(e.resource).c_str(), WsText(e.name).c_str(),
				e.base.hasParent ? " [child]" : "", pos ? (*pos)[0] : 0.f, pos ? (*pos)[1] : 0.f,
				pos ? (*pos)[2] : 0.f);
		const WsEntityBase& b = e.base;
		LogInfo("      base: hdr %08x/%u fb0 %.3f fb4 %.3f dz %u u24 %u b28 %u flags %08x %08x %08x %08x in %u%s",
				e.headerValue, e.headerFlag, b.fb0, b.fb4, b.deathZoneTest, b.u24, b.b28, b.flags, b.u2c,
				b.u30, b.u34, e.inWorld,
				b.hasParent ? (" parent h" + std::to_string(b.parent) + " j" + std::to_string(int(int8_t(b.joint))) +
						" follow " + std::to_string(b.follows) + " die " + std::to_string(b.dieWithParent)).c_str() : "");
		if (e.type == kWsModel) {
			const WsModel& m = e.model;
			std::string slots, vis, ch;
			for (size_t s = 0; s < m.slots.size(); ++s)
				if (m.slots[s].present)
					slots += " " + std::to_string(s + 1) + ":" + WsText(m.slots[s].anim) + "/" +
							std::to_string(m.slots[s].loop) + "/" + std::to_string(m.slots[s].curveMask);
			for (uint8_t v : m.meshVisible) vis += char('0' + v);
			for (const WsModel::Channel& c : m.channels) {
				char t[128];
				std::snprintf(t, sizeof t, " [s%u t %.3f w %.3f speed %.3f loop %u %u blend %.3f %.3f]", c.slot,
						c.time, c.weight, c.speed, c.loop, c.e, c.blend, c.blendLeft);
				ch += t;
			}
			LogInfo("      model: rot %.3f %.3f %.3f %.3f f700 %.3f mat '%s' shadow %u meshes %s slots %u%s",
					m.rot[0], m.rot[1], m.rot[2], m.rot[3], m.f700, WsText(m.material).c_str(), m.shadow,
					vis.c_str(), m.slotCount, slots.c_str());
			LogInfo("      anims:%s", ch.c_str());
			if (m.hasBody) {
				const WsPhysicsObject& p = m.body;
				LogInfo("      body: type %u scale %.3f flags %08x b0 %u vel %.2f %.2f %.2f av %.2f %.2f %.2f "
						"group %u %u %u %u mass %.3f fr %.3f rest %.3f damp %.3f %.3f b5 %u u18 %08x ctrl %u cons %zu",
						p.type, p.scaleArg, p.flags, p.freedom, p.velocity[0], p.velocity[1], p.velocity[2],
						p.angularVelocity[0], p.angularVelocity[1], p.angularVelocity[2], p.collisionGroup,
						p.b2, p.b3, p.lineTrace, p.mass, p.friction, p.restitution, p.linearDamping,
						p.angularDamping, p.b5, p.u18, p.hasController, p.constraints.records.size());
				if (p.flags & 2)
					LogInfo("      monster: sight %.3f %.3f %.3f %.3f wish %.2f %.2f %.2f e60 %.2f %.2f %.2f const %.3f flag %u %u",
							p.sight[0], p.sight[1], p.sight[2], p.sight[3], p.moveWish[0], p.moveWish[1],
							p.moveWish[2], p.ext60[0], p.ext60[1], p.ext60[2], p.moveConst, p.moveFlag, p.ext71);
			}
			if (m.ragdollOn)
				LogInfo("      ragdoll: %zu bodies, body0 (%.2f %.2f %.2f) tail %.3f %.3f %.3f flag %u",
						m.ragdoll.bodies.size(), m.ragdoll.bodies.empty() ? 0.f : m.ragdoll.bodies[0].pos[0],
						m.ragdoll.bodies.empty() ? 0.f : m.ragdoll.bodies[0].pos[1],
						m.ragdoll.bodies.empty() ? 0.f : m.ragdoll.bodies[0].pos[2], m.ragdoll.tail[0],
						m.ragdoll.tail[1], m.ragdoll.tail[2], m.ragdoll.flag);
			for (const WsRagdoll::Body& rb : m.ragdoll.bodies)
				LogInfo("        body (%.2f %.2f %.2f) rot %.3f %.3f %.3f %.3f v %.2f %.2f %.2f flag %u extra %u "
						"mass %.2f b %u c %.3f d %.3f e %.3f g %u",
						rb.pos[0], rb.pos[1], rb.pos[2], rb.rot[0], rb.rot[1], rb.rot[2], rb.rot[3],
						rb.velocity[0], rb.velocity[1], rb.velocity[2], rb.flag, rb.hasExtra, rb.mass, rb.b,
						rb.c, rb.d, rb.e, rb.g);
			// The saved limbs and constraints beside the .hke's, for the writer's mapping.
			Hke hke;
			if (m.ragdollOn && Hke::Load(root + "/Models/" + WsText(e.resource) + ".hke", hke)) {
				for (size_t k = 0; k < hke.bodies.size() && k < m.ragdoll.bodies.size(); ++k) {
					const HkeBody& hb = hke.bodies[k];
					const WsRagdoll::Body& rb = m.ragdoll.bodies[k];
					LogInfo("        hke body %zu %s mass %.2f mask %d active %d nocoll %d | saved b %u mass %.2f "
							"f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f e %.3f extra %u",
							k, hb.bone.c_str(), hb.mass, hb.collisionMask, hb.active, hb.collisionsDisabled, rb.b,
							rb.mass, rb.f[0], rb.f[1], rb.f[2], rb.f[3], rb.f[4], rb.f[5], rb.f[6], rb.f[7], rb.e,
							rb.hasExtra);
				}
				for (size_t k = 0; k < m.ragdoll.constraints.size(); ++k) {
					const WsRagdoll::Constraint& c = m.ragdoll.constraints[k];
					LogInfo("        cons %zu: type %u brk %u str %.1f data %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f after %u",
							k, c.type, c.breakable, c.strength, c.data[0], c.data[1], c.data[2], c.data[3], c.data[4],
							c.data[5], c.data[6], c.data[7], c.after);
					if (k >= hke.constraints.size()) continue;
					const HkeConstraint& h = hke.constraints[k];
					LogInfo("          hke %s %s-%s lim %d brk %d str %.1f ws %d ref %.3f %.3f %.3f att %.3f %.3f %.3f "
							"hA %.3f %.3f %.3f hB %.3f %.3f %.3f pA %.3f %.3f %.3f pB %.3f %.3f %.3f len %.3f "
							"wp %.3f %.3f %.3f lin %.1f ang %.1f min %.3f max %.3f",
							h.kind == HkeConstraint::kHinge ? "hinge" : h.kind == HkeConstraint::kRagdoll ? "ragdoll" : "spring",
							h.bodyA.c_str(), h.bodyB.c_str(), h.limited, h.breakable, h.strength, h.worldSpace,
							h.csToRef[3][0], h.csToRef[3][1], h.csToRef[3][2], h.csToAtt[3][0], h.csToAtt[3][1],
							h.csToAtt[3][2], h.hingePosA[0], h.hingePosA[1], h.hingePosA[2], h.hingePosB[0],
							h.hingePosB[1], h.hingePosB[2], h.localPointA[0], h.localPointA[1], h.localPointA[2],
							h.localPointB[0], h.localPointB[1], h.localPointB[2], h.springLength, h.worldPivot[0],
							h.worldPivot[1], h.worldPivot[2], h.linearStrength, h.angularStrength, h.limitMinAngle,
							h.limitMaxAngle);
					const auto bodyInfo = [&](const std::string& bone) {
						for (const HkeBody& hb : hke.bodies) {
							if (hb.bone != bone) continue;
							char t[256];
							std::snprintf(t, sizeof t, "%s disp %.3f %.3f %.3f tr %.3f %.3f %.3f rot %.3f (%.3f %.3f %.3f)",
									bone.c_str(), hb.displacement[0], hb.displacement[1], hb.displacement[2],
									hb.translation[0], hb.translation[1], hb.translation[2], hb.rotAngle,
									hb.rotAxis[0], hb.rotAxis[1], hb.rotAxis[2]);
							return std::string(t);
						}
						return bone + " ?";
					};
					float modelScale = 0.f;
					std::memcpy(&modelScale, &e.headerValue, 4);
					LogInfo("          bodies: A %s | B %s | scale %.4f | hingeA %.3f %.3f %.3f",
							bodyInfo(h.bodyA).c_str(), bodyInfo(h.bodyB).c_str(), modelScale,
							h.worldHingePos[0], h.worldHingePos[1], h.worldHingePos[2]);
				}
			}
		} else if (e.type == kWsLight) {
			const WsLight& l = e.light;
			LogInfo("      light: type %u argb %08x intensity %.3f dir %.3f %.3f %.3f cone %.3f %.3f range %.3f "
					"start %.3f proj '%s'",
					l.type, l.argb, l.intensity, l.dir[0], l.dir[1], l.dir[2], l.coneCos, l.coneInnerCos,
					l.range, l.startFalloff, WsText(l.projector).c_str());
		} else if (e.type == kWsBillboard) {
			const WsBillboard& bb = e.billboard;
			std::string f;
			for (float v : bb.f) f += " " + std::to_string(v);
			LogInfo("      corona: '%s' corona %u blend %u f610 %.3f:%s", WsText(bb.texture).c_str(), bb.corona,
					bb.blend, bb.f610, f.c_str());
		} else if (e.type == kWsParticle) {
			for (const WsParticle::Emitter& em : e.particle.emitters)
				LogInfo("      emitter '%s' a %.2f %.2f %.2f b %.2f c %.2f %.2f %.2f d %.2f %.2f %.2f "
						"flags %u%u%u%u%u offset %.2f %.2f %.2f rot %.2f %.2f %.2f scale %.3f",
						WsText(em.file).c_str(), em.a[0], em.a[1], em.a[2], em.b, em.c[0], em.c[1], em.c[2],
						em.d[0], em.d[1], em.d[2], em.flags[0], em.flags[1], em.flags[2], em.flags[3],
						em.flags[4], em.offset[0], em.offset[1], em.offset[2], em.rotDeg[0], em.rotDeg[1],
						em.rotDeg[2], em.scale);
		} else if (e.type == kWsWorldMesh) {
			const WsWorldMesh& w = e.mesh;
			LogInfo("      mesh: rot %.3f %.3f %.3f %.3f body %u '%s' %08x %08x %08x %08x '%s' '%s'", w.rot[0],
					w.rot[1], w.rot[2], w.rot[3], w.hasBody, WsText(w.material).c_str(), w.materialData[0],
					w.materialData[1], w.materialData[2], w.materialData[3], WsText(w.s684).c_str(),
					WsText(w.s690).c_str());
			if (w.hasBody)
				LogInfo("      body: type %u scale %.3f flags %08x b0 %u group %u %u %u %u b5 %u mass %.3f fr %.3f rest %.3f damp %.3f %.3f",
						w.body.type, w.body.scaleArg, w.body.flags, w.body.freedom, w.body.collisionGroup, w.body.b2,
						w.body.b3, w.body.lineTrace, w.body.b5, w.body.mass, w.body.friction, w.body.restitution,
						w.body.linearDamping, w.body.angularDamping);
		}
	}
	std::string types;
	for (const auto& kv : byType) types += " " + kv.first + " " + std::to_string(kv.second);
	LogInfo("  entities: %zu of %zu read:%s", parsed, save.entities.size(), types.c_str());
	LogInfo("  %zu with a physics object, %zu ragdolls simulating, %zu children", withBody, ragdolls, parented);
	LogInfo("  portals %zu, antiportals %zu, zones %zu", save.portals.size(), save.antiportals.size(),
			save.zones.size());
	if (census) {
		cs.U("save.e6dc", save.e6dc);
		cs.U("save.e6dd", save.e6dd);
		cs.U("save.e6bb", save.e6bb);
		cs.X("save.physicsC", save.physicsC);
		cs.X("save.physics14", save.physics14);
		for (uint8_t v : save.portals) cs.U("save.portal", v);
		for (uint8_t v : save.antiportals) cs.U("save.antiportal", v);
		for (uint8_t v : save.zones) cs.U("save.zone", v);
		cs.Print();
	}

	// Which branches of the format these bytes exercised: a round trip proves
	// only the paths a file takes.
	size_t ext = 0, controllers = 0, constraintHeaders = 0, overrides = 0, groupsOff = 0;
	size_t ragdollConstraints = 0, breakable = 0, ragdollActions = 0, sounds = 0, samples = 0, frames = 0;
	std::map<int, size_t> constraintTypes;
	auto countBody = [&](const WsPhysicsObject& p) {
		ext += (p.flags & 2) ? 1 : 0;
		controllers += p.hasController ? 1 : 0;
		constraintHeaders += p.constraints.hasHeader ? 1 : 0;
		for (const WsConstraints::Record& r : p.constraints.records) ++constraintTypes[r.type];
	};
	for (size_t i = 0; i < parsed; ++i) {
		const WsEntity& e = save.entities[i];
		if (e.type == kWsModel) {
			if (e.model.hasBody) countBody(e.model.body);
			overrides += e.model.channels.size();
			groupsOff += e.model.hasGroups ? 1 : 0;
			ragdollConstraints += e.model.ragdoll.constraints.size();
			for (const WsRagdoll::Constraint& c : e.model.ragdoll.constraints) breakable += c.type >= 10 ? 1 : 0;
			ragdollActions += e.model.ragdoll.actions.size();
		}
		if (e.type == kWsWorldMesh && e.mesh.hasBody) countBody(e.mesh.body);
		if (e.type == kWsSound) {
			++sounds;
			samples += e.sound.sample.body.size() > 1 ? 1 : 0;
		}
		if (e.type == kWsTrail) frames += e.trail.frames.size();
	}
	std::string typeList;
	for (const auto& kv : constraintTypes) typeList += " t" + std::to_string(kv.first) + " " + std::to_string(kv.second);
	LogInfo("  coverage: %zu ext blocks, %zu controllers, %zu constraint headers, constraints:%s", ext,
			controllers, constraintHeaders, typeList.empty() ? " none" : typeList.c_str());
	LogInfo("  coverage: %zu overrides, %zu idle ragdoll groups, ragdoll constraints %zu (%zu breakable), actions %zu, sounds %zu (%zu samples), trail frames %zu",
			overrides, groupsOff, ragdollConstraints, breakable, ragdollActions, sounds, samples, frames);

	if (!ok) {
		LogInfo("  FAILED at 0x%zx: %s", save.errorAt, save.error.c_str());
		const size_t from = save.errorAt > 16 ? save.errorAt - 16 : 0;
		std::string hex;
		for (size_t i = from; i < from + 48 && i < data.size(); ++i) {
			char b[4];
			std::snprintf(b, sizeof b, "%02x ", data[i]);
			hex += b;
		}
		LogInfo("  bytes from 0x%zx: %s", from, hex.c_str());
		for (const auto& kv : meshCounts)
			if (kv.second < 0) LogInfo("  no model file for %s", kv.first.c_str());
		return 1;
	}

	std::vector<uint8_t> again;
	save.Write(again);
	size_t same = 0;
	while (same < again.size() && same < data.size() && again[same] == data[same]) ++same;
	if (again.size() == data.size() && same == data.size()) {
		LogInfo("  written back: identical");
		return 0;
	}
	LogInfo("  written back: %zu bytes, first difference at 0x%zx", again.size(), same);
	return 1;
}

int LuaCmd(const char* dataRoot, int frames, const char* level,
		const char* exec) {
	// Unbuffered: this command exists to find out what the scripts did, and a
	// block-buffered stdout throws away the last few KB - which is precisely
	// the part that matters when a run dies rather than finishes.
	setvbuf(stdout, nullptr, _IONBF, 0);

	LuaHost host;
	if (!host.Init(dataRoot)) {
		LogInfo("failed to create the Lua state");
		return 2;
	}
	// Headless means no renderer, not a hollow game: physics, the pawn and
	// the input all attach, so the tick chain here is the one the windowed
	// run takes and the call report measures the real thing. Only the
	// drawing is missing.
	PhysicsWorld physics;
	physics.SetProbeRadius(kCameraRadius);
	physics.SetProbeEnabled(false); // no free camera here; it would sit on the player
	// The player's own pusher: the widest of the four spheres the shape factory
	// builds for BodyTypes.Player at bodyScale 1.0 (Engine.dll 0x101b3e20).
	physics.SetPawnProbeRadius(0.4f); // the four-sphere player body as a sensor
	PlayerPawn pawn;
	Input input;
	ScriptEngine engine;
	engine.Bind(host);
	engine.AttachPhysics(&physics, dataRoot);
	engine.AttachPlayer(&pawn);
	engine.AttachInput(&input);
	// The texture INDEX only (createWhite=false, so no graphics device). It
	// gives MATERIAL.Size real dimensions, which is what the HUD scripts lay
	// themselves out from - without it Hud:Render aborts on its own
	// "material not found" diagnostic and takes the rest of PostRender with it.
	TextureCache hudTextures;
	hudTextures.Init(std::string(dataRoot) + "/Textures", false);
	engine.AttachHudTextures(&hudTextures);
	// PAINFUL_AUDIO=1 opens a real device so the SOUND natives can be exercised
	// headlessly. Without it audio_ is null, every SOUND call is a silent no-op,
	// and the mixer - including its voice cap - cannot be measured at all.
	AudioEngine audio;
	if (DebugFlag("PAINFUL_AUDIO") && audio.Init(std::string(dataRoot) + "/Sounds"))
		engine.AttachAudio(&audio);
	const bool realtime = DebugFlag("PAINFUL_REALTIME");
	const bool ok = host.Boot();
	if (ok) {
		host.CallGameInit();
		if (level && level[0]) {
			host.CallGameLoadLevel(level);
			physics.Settle(90);
			engine.SyncFromPhysics(false);
			// Same play transition the windowed run makes: the level has
			// seated the camera through CAM.SetPos while unlocked, and the
			// lock goes on before OnPlay so CreatePlayerSP finds Lev.Pos
			// still holding the level's own spawn.
			engine.SetMouseLocked(true);
			host.CallGameOnPlay();
			// As the game host: the level start's last step seeds Player.Pos.
			host.RunString("Game:SwitchPlayerToPhysics(true)");
		}
		// The diagnostic hook: run an arbitrary chunk between OnPlay and the
		// ticks - teleport the player into a trigger, poke a template, ...
		if (exec && exec[0]) host.RunString(exec);
		for (int i = 0; i < frames && !host.quitRequested(); ++i) {
			input.BeginFrame();
			// The game tick runs on world-speed time, as in GameApp.
			const float sim = (1.f / 60.f) * engine.timeMultiplier();
			engine.SetFrameDelta(1.f / 60.f); // the mover keeps real time

			engine.TickAnimations(sim);
			engine.TickMonsters(sim);
			engine.TickProjectiles(sim);
			host.FrameTick(double(sim));
			physics.Update(sim);
			engine.TickGrenades();
			engine.SyncFromPhysics();
			engine.TickRagdolls();
			// The player's pusher follows its centre. SlideSphere is a query
			// and touches nothing, so without a body the pawn walks through
			// corpses and loose props without either noticing.
			{
				Vec3 centre;
				const float* h = pawn.headPos();
				for (int c = 0; c < 3; ++c) centre[c] = h[c];
				centre[1] -= 0.9f; // head is centre + 0.9, per GetPawnHeadPos
				physics.MovePawnProbe(centre, true);
			}
			// The landing message the game loop posts, negated as PlayerAction
			// does (Physics.md, "The player takes hits"), so falls hurt here too.
			{
				const float impact = pawn.TakeGroundHit();
				if (impact > 0.f && engine.playerHandle()) {
					const double hitArgs[2] = {double(engine.playerHandle()), double(-impact)};
					host.PostMsg("PLAYER_HIT_GROUND", hitArgs, 2);
				}
			}
			engine.TickTriggers();
			engine.TickLifetimes(sim);
			// The same tail the game loop runs. Without these the headless path
			// is not the game minus a window: bound entities never follow what
			// they hang off, and CONTACTS ARE NEVER REPORTED - so a destructible
			// could not break here even though the physics under it is real.
			engine.UpdateAttached();
			engine.TickSounds(sim);
			engine.TickCollisions(sim);
			// The view model follows the camera here too, as in GameApp.
			engine.UpdateViewAttached();
			// The mixer's own tick, on the simulated clock: the voice policy
			// promotes and expires by it, and a headless run outpaces the
			// wall clock a hundredfold.
			audio.Advance(1.f / 60.f);
			audio.Update();
			// PAINFUL_REALTIME=1 paces the loop at 60 Hz wall time so the
			// mixer, which runs on the device clock, keeps up with the policy
			// clock - the only way to see a voice actually END.
			if (realtime) std::this_thread::sleep_for(std::chrono::microseconds(16667));
		}
	}
	LogInfo("entities: %zu created, %zu released, %zu live; map \"%s\" scale %.2f",
			engine.created(), engine.released(), engine.entities().size(),
			engine.world().mapPath.c_str(), engine.world().scale);
	if (audio.ready()) audio.LogRealVoices();
	host.PrintCallReport(60);
	return ok && host.scriptErrors() == 0 ? 0 : 3;
}

// Lists the levels available for the in-engine selector.

int BlendCmd(const char* path, const char* animA, const char* animB,
		const char* timeArg) {
	Model model;
	if (!Model::Load(path, model) || model.bones.empty()) {
		LogInfo("failed to load %s, or it has no skeleton", path);
		return 2;
	}

	std::string dir = path, base = path;
	const size_t slash = base.find_last_of("/\\");
	if (slash != std::string::npos) { dir = base.substr(0, slash); base = base.substr(slash + 1); }
	else dir = ".";
	const size_t dot = base.find_last_of('.');
	if (dot != std::string::npos) base = base.substr(0, dot);

	AnimationCache cache;
	cache.SetRoot(dir);
	const Animation* a = cache.Get(base, animA);
	const Animation* b = cache.Get(base, animB);
	if (!a || !b) {
		LogInfo("missing %s.%s.ani or %s.%s.ani", base.c_str(), animA, base.c_str(), animB);
		return 2;
	}

	std::vector<Bone> bones = model.bones;
	BuildHierarchy(bones);
	std::vector<Mat4> bw, ib;
	ComputeBindWorld(bones, bw, ib);
	std::vector<const AnimTrack*> ta, tb;
	ResolveAnimTracks(bones, *a, ta);
	ResolveAnimTracks(bones, *b, tb);

	const float t = timeArg ? float(std::atof(timeArg)) : 0.f;
	// A bone deep enough in the chain that a bad blend shows: the head.
	const size_t probe = bones.size() > 6 ? 6 : bones.size() - 1;
	LogInfo("%s: %s -> %s at t=%.3f, bone [%zu] %s", path, animA, animB, t,
			probe, bones[probe].name.c_str());

	std::vector<Mat4> world;
	Vec3 prev = {0, 0, 0};
	for (int step = 0; step <= 4; ++step) {
		const float u = float(step) * 0.25f;
		ComputeBoneWorldBlended(bones, ta, t, tb, t, u, world);
		const float* m = world[probe].m;
		// Bone length from its parent: a blend that lerped matrices instead of
		// rotations would shorten this in the middle.
		const int par = bones[probe].parent;
		float len = 0.f;
		if (par >= 0) {
			const float* p = world[size_t(par)].m;
			for (int c = 0; c < 3; ++c) {
				const float d = m[12 + c] - p[12 + c];
				len += d * d;
			}
			len = std::sqrt(len);
		}
		LogInfo("  u=%.2f  pos %7.3f %7.3f %7.3f   from parent %.4f%s", u,
				m[12], m[13], m[14], len,
				step == 0 ? "" : (std::fabs(m[12]-prev[0]) + std::fabs(m[13]-prev[1]) +
				std::fabs(m[14]-prev[2]) > 1e-5f ? "  (moved)" : "  (STUCK)"));
		for (int c = 0; c < 3; ++c) prev[c] = m[12 + c];
	}
	return 0;
}

// Parses a .wps waypoint set and reports whether it parsed exactly. The
// adjacency invariant (see Waypoints.h) is what confirms the record stride, so
// a set that loads at all is a set whose format is understood.
int WpsCmd(const char* path) {
	WaypointSet wps;
	if (!WaypointSet::Load(path, wps)) {
		LogInfo("%s: %s", path, wps.error.c_str());
		return 2;
	}

	size_t minLinks = SIZE_MAX, maxLinks = 0;
	double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
	size_t isolated = 0;
	for (const WaypointSet::Node& n : wps.nodes) {
		minLinks = std::min<size_t>(minLinks, n.linkCount);
		maxLinks = std::max<size_t>(maxLinks, n.linkCount);
		if (n.linkCount == 0) ++isolated;
		for (int c = 0; c < 3; ++c) {
			lo[c] = std::min(lo[c], double(n.pos[c]));
			hi[c] = std::max(hi[c], double(n.pos[c]));
		}
	}

	// The floor each waypoint belongs to.
	std::map<uint32_t, size_t> floorHist;
	for (const WaypointSet::Node& n : wps.nodes) ++floorHist[n.floor];

	LogInfo("%s", path);
	LogInfo("  %zu waypoints, %zu links, %zu bytes of floors, %zu of %zu bytes consumed",
			wps.nodes.size(), wps.links.size(), wps.floorBytes, wps.consumed, wps.size);
	LogInfo("  links per waypoint: min %zu, max %zu, mean %.1f, %zu isolated",
			minLinks, maxLinks, double(wps.links.size()) / double(wps.nodes.size()),
			isolated);
	LogInfo("  bounds x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]",
			lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
	LogInfo("  %zu floors, %u to %u", floorHist.size(),
			floorHist.empty() ? 0 : floorHist.begin()->first,
			floorHist.empty() ? 0 : floorHist.rbegin()->first);

	// Connectivity first. Routing between the bounding box's two CORNERS
	// reports "no path" on most levels, and that is the test being wrong
	// rather than the graph: a corner is exactly where a sealed pocket, an
	// out-of-bounds marker or a separate island tends to sit. What matters is
	// how much of the graph hangs together.
	std::vector<int> component(wps.nodes.size(), -1);
	size_t components = 0, largest = 0;
	int largestSeed = -1;
	std::vector<int> queue;
	for (size_t s = 0; s < wps.nodes.size(); ++s) {
		if (component[s] >= 0) continue;
		const int id = int(components++);
		size_t seen = 0;
		queue.assign(1, int(s));
		component[s] = id;
		while (!queue.empty()) {
			const size_t at = size_t(queue.back());
			queue.pop_back();
			++seen;
			const WaypointSet::Node& node = wps.nodes[at];
			for (uint32_t k = 0; k < node.linkCount; ++k) {
				const size_t edge = size_t(node.linkStart) + k;
				if (edge >= wps.links.size()) break;
				const size_t next = size_t(wps.links[edge]);
				if (component[next] >= 0) continue;
				component[next] = id;
				queue.push_back(int(next));
			}
		}
		if (seen > largest) { largest = seen; largestSeed = int(s); }
	}
	LogInfo("  %zu components, largest holds %zu of %zu waypoints (%.0f%%)",
			components, largest, wps.nodes.size(),
			100.0 * double(largest) / double(wps.nodes.size()));

	// Then the farthest apart pair WITHIN that component, which is a route
	// that must exist. Walked distance over straight-line distance is how much
	// the level makes an actor bend - the number that was 1.00 before any of
	// this, because a straight line was all there was.
	if (largestSeed >= 0) {
		const int id = component[size_t(largestSeed)];
		int na = -1, nb = -1;
		double best = -1;
		for (size_t i = 0; i < wps.nodes.size(); ++i) {
			if (component[i] != id) continue;
			if (na < 0) na = int(i);
			double d = 0;
			for (int c = 0; c < 3; ++c) {
				const double e = wps.nodes[i].pos[c] - wps.nodes[size_t(na)].pos[c];
				d += e * e;
			}
			if (d > best) { best = d; nb = int(i); }
		}
		std::vector<int> route;
		if (na >= 0 && nb >= 0 && wps.FindPath(na, nb, route)) {
			double walked = 0;
			for (size_t i = 1; i < route.size(); ++i) {
				double d = 0;
				for (int c = 0; c < 3; ++c) {
					const double e = wps.nodes[size_t(route[i])].pos[c] -
							wps.nodes[size_t(route[i - 1])].pos[c];
					d += e * e;
				}
				walked += std::sqrt(d);
			}
			const double straight = std::sqrt(best);
			LogInfo("  route %d -> %d: %zu hops, %.1f walked vs %.1f straight (x%.2f)",
					na, nb, route.size(), walked, straight,
					straight > 0 ? walked / straight : 0.0);
		} else {
			LogInfo("  route %d -> %d: NO PATH inside one component - that is a bug",
					na, nb);
		}
	}
	return 0;
}

// Plays one sample through the engine and waits, so the audio path can be
// tested without the game around it. `painful sound Sounds/misc/gas-outflow-5sec`
// - if this is silent but the mixer reports signal, the problem is SDL's
// delivery rather than anything the engine computes.
int SoundCmd(const char* root, const char* name, const char* seconds) {
	AudioEngine audio;
	if (!audio.Init(std::string(root) + "/Sounds")) {
		LogInfo("no audio device");
		return 2;
	}
	const Vec3 listener{0, 0, 0};
	const Vec3 fwd{0, 0, 1};
	const Vec3 right{1, 0, 0};
	audio.SetListener(listener, fwd, right);

	// 2D at full volume: no distance, no panning, nothing to get wrong.
	const int v = audio.Play2D(name, 100.f, false, true);
	if (v < 0) {
		LogInfo("could not play %s (missing %zu)", name, audio.samplesMissing());
		return 2;
	}
	LogInfo("playing %s ...", name);

	const double total = seconds ? std::atof(seconds) : 4.0;
	const Uint64 start = SDL_GetTicks();
	while ((SDL_GetTicks() - start) < Uint64(total * 1000.0)) {
		audio.Advance(0.05f);
		audio.Update();
		SDL_Delay(50);
	}
	LogInfo("done: %zu started, %zu playing at the end", audio.voicesStarted(),
			audio.voicesPlaying());
	return 0;
}


// One axis-aligned box of STATIC world geometry.
//
// Six faces, four vertices each so every face carries its own normal. The
// winding is the exporter's, not the intuitive one: the geometric normal of
// each triangle must OPPOSE its vertex normal (see MapMesh::Write). Corners
// are handed in counter-clockwise order as seen from OUTSIDE the box, which
// would give a geometric normal along +n, so the indices are emitted reversed.
// Wound the other way a step is invisible from the front and the player walks
// through it.
static void AddBoxFace(MapObject& o, const Vec3& c0, const Vec3& c1,
		const Vec3& c2, const Vec3& c3, const Vec3& n,
		float uvPerUnit) {
	const uint16_t base = uint16_t(o.vertexCount());
	const float* corner[4] = {c0, c1, c2, c3};
	// Planar UVs off the two axes the face does NOT point along, so the
	// texture keeps its world scale on every face.
	const int axis = (std::fabs(n[0]) > 0.5f) ? 0 : (std::fabs(n[1]) > 0.5f ? 1 : 2);
	const int u = (axis == 0) ? 2 : 0;
	const int v = (axis == 1) ? 2 : 1;
	// uvChannels 2: the normal moves to its own array and the second UV set
	// takes the freed floats. Lightmap UVs run 0..1 across each face, which is
	// all a flat lightmap needs.
	static const float lmUV[4][2] = {{0.f,0.f},{1.f,0.f},{1.f,1.f},{0.f,1.f}};
	for (int i = 0; i < 4; ++i) {
		o.verts.push_back(corner[i][0]);
		o.verts.push_back(corner[i][1]);
		o.verts.push_back(corner[i][2]);
		o.verts.push_back(0.f); // pad
		o.verts.push_back(corner[i][u] * uvPerUnit); // uv0
		o.verts.push_back(corner[i][v] * uvPerUnit);
		o.verts.push_back(lmUV[i][0]); // uv1
		o.verts.push_back(lmUV[i][1]);
		o.normals.push_back(n[0]);
		o.normals.push_back(n[1]);
		o.normals.push_back(n[2]);
	}
	// Reversed: (0,2,1) and (0,3,2) rather than (0,1,2) and (0,2,3).
	o.indices.push_back(base); o.indices.push_back(uint16_t(base + 2));
	o.indices.push_back(uint16_t(base + 1));
	o.indices.push_back(base); o.indices.push_back(uint16_t(base + 3));
	o.indices.push_back(uint16_t(base + 2));
}

// A step: a solid box standing on the floor, `h` tall.
static MapObject MakeStepBox(const std::string& name, float cx, float cz, float floorY,
		float h, float halfX, float halfZ, float uvPerUnit,
		const std::string& texture, const std::string& lightmap) {
	MapObject box;
	// A plain name is plain solid geometry - no portal, zone, barrier or
	// physics substring - which is what MapObject::isCollidable answers true
	// for. These are STATIC world mesh, not props: nothing can shove them and
	// they need no body of their own.
	box.name = name;
	box.uvChannels = 2;

	const float x0 = cx - halfX, x1 = cx + halfX;
	const float z0 = cz - halfZ, z1 = cz + halfZ;
	const float y0 = floorY, y1 = floorY + h;

	// Each face's corners counter-clockwise seen from outside.
	const Vec3 top[4] = {{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}};
	const Vec3 bottom[4] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}};
	const Vec3 xneg[4] = {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}};
	const Vec3 xpos[4] = {{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}};
	const Vec3 zneg[4] = {{x0,y0,z0},{x0,y1,z0},{x1,y1,z0},{x1,y0,z0}};
	const Vec3 zpos[4] = {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
	const Vec3 nUp{0,1,0}, nDown{0,-1,0};
	const Vec3 nXneg{-1,0,0}, nXpos{1,0,0};
	const Vec3 nZneg{0,0,-1}, nZpos{0,0,1};

	AddBoxFace(box, top[0], top[1], top[2], top[3], nUp, uvPerUnit);
	AddBoxFace(box, bottom[0], bottom[1], bottom[2], bottom[3], nDown, uvPerUnit);
	AddBoxFace(box, xneg[0], xneg[1], xneg[2], xneg[3], nXneg, uvPerUnit);
	AddBoxFace(box, xpos[0], xpos[1], xpos[2], xpos[3], nXpos, uvPerUnit);
	AddBoxFace(box, zneg[0], zneg[1], zneg[2], zneg[3], nZneg, uvPerUnit);
	AddBoxFace(box, zpos[0], zpos[1], zpos[2], zpos[3], nZpos, uvPerUnit);

	box.bboxMin[0] = x0; box.bboxMin[1] = y0; box.bboxMin[2] = z0;
	box.bboxMax[0] = x1; box.bboxMax[1] = y1; box.bboxMax[2] = z1;

	Material mat;
	mat.firstIndex = 0;
	mat.triangleCount = uint16_t(box.indices.size() / 3);
	mat.slots[0].name = texture;
	mat.slots[1].name = lightmap; // slot 1 is the lightmap on shipped geometry
	box.materials.push_back(mat);
	return box;
}

// A ramp: a wedge rising toward +X at `degrees`, `length` long, with a
// vertical back. The slope test bed for the pawn's slide and rungs.
static MapObject MakeRamp(const std::string& name, float cx, float cz, float floorY,
		float degrees, float length, float halfZ, float uvPerUnit,
		const std::string& texture, const std::string& lightmap) {
	MapObject ramp;
	ramp.name = name;
	ramp.uvChannels = 2;
	const float x0 = cx - length * 0.5f, x1 = cx + length * 0.5f;
	const float z0 = cz - halfZ, z1 = cz + halfZ;
	const float y0 = floorY, y1 = floorY + length * std::tan(degrees * 3.14159265f / 180.f);
	const float s = std::sin(degrees * 3.14159265f / 180.f);
	const float c = std::cos(degrees * 3.14159265f / 180.f);

	const Vec3 top[4] = {{x0,y0,z0},{x0,y0,z1},{x1,y1,z1},{x1,y1,z0}};
	const Vec3 bottom[4] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}};
	const Vec3 back[4] = {{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}};
	// The triangular sides as quads with a doubled corner.
	const Vec3 zneg[4] = {{x0,y0,z0},{x1,y1,z0},{x1,y0,z0},{x1,y0,z0}};
	const Vec3 zpos[4] = {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x1,y1,z1}};
	const Vec3 nTop{-s, c, 0}, nDown{0,-1,0}, nXpos{1,0,0};
	const Vec3 nZneg{0,0,-1}, nZpos{0,0,1};
	AddBoxFace(ramp, top[0], top[1], top[2], top[3], nTop, uvPerUnit);
	AddBoxFace(ramp, bottom[0], bottom[1], bottom[2], bottom[3], nDown, uvPerUnit);
	AddBoxFace(ramp, back[0], back[1], back[2], back[3], nXpos, uvPerUnit);
	AddBoxFace(ramp, zneg[0], zneg[1], zneg[2], zneg[3], nZneg, uvPerUnit);
	AddBoxFace(ramp, zpos[0], zpos[1], zpos[2], zpos[3], nZpos, uvPerUnit);

	ramp.bboxMin[0] = x0; ramp.bboxMin[1] = y0; ramp.bboxMin[2] = z0;
	ramp.bboxMax[0] = x1; ramp.bboxMax[1] = y1; ramp.bboxMax[2] = z1;
	Material mat;
	mat.firstIndex = 0;
	mat.triangleCount = uint16_t(ramp.indices.size() / 3);
	mat.slots[0].name = texture;
	mat.slots[1].name = lightmap;
	ramp.materials.push_back(mat);
	return ramp;
}

int MkLevelCmd(const char* dataRoot, const char* levelName, float extent,
		float height, const char* texture, const char* steps,
		const char* lightmapArg) {
	const std::string root = dataRoot;
	const std::string name = levelName;
	// Slot 1 on every object. A flat white bitmap is a neutral lightmap: the
	// surface renders unlit-looking but the RECORD matches shipped geometry.
	const std::string lightmap = (lightmapArg && *lightmapArg) ? lightmapArg : "LM";
	// ONE QUAD, NOT A GRID.
	//
	// A flat floor needs two triangles; the texture tiles through UVs greater
	// than 1, not through geometry. It used to be a 64x64 grid, which put 8192
	// COPLANAR triangles in a single object - something no shipped map does,
	// and the classic pathological input for a spatial-partition builder. The
	// original engine hangs rather than crashes on this level, which is what
	// that looks like from outside.
	constexpr int kCells = 1;
	const float step = (extent * 2.f) / float(kCells);
	// One texture repeat every 8 world units, so a 400-unit floor tiles 50
	// times rather than stretching one image across the whole thing.
	const float uvPerUnit = 1.f / 8.f;

	MapObject floor;
	// The name carries engine semantics: portals, zones, barriers and noclip
	// are all encoded as substrings. A plain name means plain solid geometry,
	// which is what MapObject::isCollidable answers for anything without one
	// of those tokens.
	floor.name = "floor_generated";
	// LIGHTMAPPED, like every shipped world object that carries one.
	//
	// uvChannels 2 changes the vertex record: the normal leaves the inline
	// slot for its own array and the freed floats become the second UV set.
	// Both layouts are 32 bytes (Docs/Reference/Formats.md). Emitting 1 here
	// gave geometry no shipped map looks like, and slot 1 had nothing to
	// sample.
	floor.uvChannels = 2;

	floor.verts.reserve(size_t(kCells + 1) * (kCells + 1) * 8);
	for (int iz = 0; iz <= kCells; ++iz) {
		for (int ix = 0; ix <= kCells; ++ix) {
			const float x = -extent + float(ix) * step;
			const float z = -extent + float(iz) * step;
			floor.verts.push_back(x);
			floor.verts.push_back(height);
			floor.verts.push_back(z);
			floor.verts.push_back(0.f); // pad
			floor.verts.push_back(x * uvPerUnit); // uv0: tiles
			floor.verts.push_back(z * uvPerUnit);
			floor.verts.push_back(float(ix) / float(kCells)); // uv1: 0..1
			floor.verts.push_back(float(iz) / float(kCells)); //   across the object
			floor.normals.push_back(0.f);
			floor.normals.push_back(1.f);
			floor.normals.push_back(0.f);
		}
	}

	// Wound to match the shipped exporter, which is measured rather than
	// assumed: 283457 of the 283501 triangles in 1x01_Chaos have a geometric
	// normal OPPOSING their vertex normal. Going round the quad the obvious
	// way - (a, b, c) then (a, c, d) with the grid laid out +x, +z - happens
	// to give exactly that, because cross(+x, +x+z) points -y.
	//
	// Which is the whole point of matching it. The renderer culls CCW and
	// PhysicsWorld feeds Jolt the reverse of each triangle; a floor wound the
	// intuitive way would be invisible from above AND let bodies through.
	const int stride = kCells + 1;
	floor.indices.reserve(size_t(kCells) * kCells * 6);
	for (int iz = 0; iz < kCells; ++iz) {
		for (int ix = 0; ix < kCells; ++ix) {
			const uint16_t a = uint16_t(iz * stride + ix);
			const uint16_t b = uint16_t(a + 1);
			const uint16_t c = uint16_t(a + 1 + stride);
			const uint16_t d = uint16_t(a + stride);
			floor.indices.push_back(a); floor.indices.push_back(b); floor.indices.push_back(c);
			floor.indices.push_back(a); floor.indices.push_back(c); floor.indices.push_back(d);
		}
	}

	floor.bboxMin[0] = -extent; floor.bboxMin[1] = height; floor.bboxMin[2] = -extent;
	floor.bboxMax[0] = extent; floor.bboxMax[1] = height; floor.bboxMax[2] = extent;

	Material mat;
	mat.firstIndex = 0;
	mat.triangleCount = uint16_t(floor.indices.size() / 3);
	mat.slots[0].name = texture;
	mat.slots[1].name = lightmap;
	floor.materials.push_back(mat);

	MapMesh mesh;
	mesh.objects.push_back(std::move(floor));

	// A row of steps, when asked for: comma-separated heights in world units.
	// Static geometry, not props - the point is to compare the step ladder
	// (Docs/Reference/PlayerMovement.md) against the original at heights that
	// straddle its rungs, and a pushable box would measure something else.
	int stepCount = 0;
	if (steps && *steps) {
		const float spacing = 5.f;
		float cx = spacing;
		for (const char* p = steps; *p;) {
			// "r<degrees>" is a 4-unit ramp at that angle instead of a box.
			const bool isRamp = *p == 'r' || *p == 'R';
			char* end = nullptr;
			const float h = std::strtof(isRamp ? p + 1 : p, &end);
			if (end == (isRamp ? p + 1 : p)) break;
			if (h > 0.f) {
				char nm[64];
				std::snprintf(nm, sizeof nm, isRamp ? "ramp_%02d_%03d" : "step_%02d_%03d",
						stepCount + 1, int(h * (isRamp ? 1.f : 100.f) + 0.5f));
				mesh.objects.push_back(
						isRamp ? MakeRamp(nm, cx, 0.f, height, h, 4.f, 2.f, uvPerUnit, texture, lightmap)
						: MakeStepBox(nm, cx, 0.f, height, h, 1.5f, 2.f, uvPerUnit, texture,
						lightmap));
				++stepCount;
				cx += spacing;
			}
			p = (*end == ',') ? end + 1 : end;
			while (*p == ' ') ++p;
		}
	}

	const std::string mapFile = name + ".mpk";
	const std::string mapPath = root + "/Maps/" + mapFile;
	if (!MapMesh::Write(mapPath, mesh)) {
		LogWarn("cannot write %s", mapPath.c_str());
		return 1;
	}

	// The rest of a level is text. o.Scale multiplies the WORLD MESH and
	// nothing else, so 1 keeps mesh units and world units the same and makes
	// the numbers above mean what they say.
	char settings[1024];
	snprintf(settings, sizeof settings,
			"o.BaseObj = \"CLevel\"\n"
			"o.Map = \"%s\"\n"
			"o.Scale = 1\n"
			"o.Pos = Vector:New(0,%.3f,0)\n"
			"o.Ang = Vector:New(90,0,0)\n"
			"o.Ambient = Color:New(150,150,150,0)\n"
			"o.DirLight.Color = Color:New(170,170,170,0)\n"
			"o.FarClipDist = %.0f\n"
			"o.Fog.Mode = 0\n"
			"o.Physics.DefaultMeshFriction = 0.7\n",
			mapFile.c_str(), height + 2.f, extent * 3.f);

	const std::string levelDir = root + "/Levels/" + name;
	const std::string script = stepCount > 0
		? "-- Generated by `mklevel`. A floor and a row of static step boxes.\n"
		: "-- Generated by `mklevel`. A floor and nothing else.\n";
	const std::string sTxt = settings;
	if (!WriteFile(levelDir + "/" + name + ".CLevel",
			std::vector<uint8_t>(sTxt.begin(), sTxt.end())) ||
			!WriteFile(levelDir + "/" + name + ".lua",
			std::vector<uint8_t>(script.begin(), script.end()))) {
		LogWarn("cannot write the level files under %s", levelDir.c_str());
		return 1;
	}

	// A monster spawn point. NOT the player start - that is the level's own
	// o.Pos, which CLevel:Synchronize pushes out through CAM.SetPos and
	// Game:CreatePlayerSP then seats the player at. This is CSpawnPoint, which
	// every shipped level carries and which spawns ACTORS; it is written here
	// because a level with no entity directories at all is unlike anything the
	// original tools have seen.
	//
	// Placed behind the spawn so it stays clear of the step row on +X.
	char spawn[512];
	std::snprintf(spawn, sizeof spawn,
			"o.BaseObj = \"MonstersSpawnPoint.CSpawnPoint\"\n"
			"o.Pos = Vector:New(-10,%.3f,0)\n"
			"o.GroupCount = 1\n"
			"o.GroupDelay = 1\n"
			"o.EachDelay = 0.5\n"
			"o.SpawnTemplate = \"EvilMonkV2_WalkOnlyNoThrow.CActor\"\n",
			height);
	const std::string sp = spawn;
	if (!WriteFile(levelDir + "/CSpawnPoint/MonstersSpawnPoint_001.CSpawnPoint",
			std::vector<uint8_t>(sp.begin(), sp.end()))) {
		LogWarn("cannot write the spawn point under %s", levelDir.c_str());
		return 1;
	}

	LogInfo("wrote %s: %zu verts, %zu tris, %.0f x %.0f units at y=%.1f, texture \"%s\"",
			mapPath.c_str(), mesh.objects[0].vertexCount(),
			mesh.objects[0].triangleCount(), extent * 2, extent * 2, height, texture);
	LogInfo("wrote %s/%s.CLevel and %s.lua", levelDir.c_str(), name.c_str(), name.c_str());
	if (stepCount > 0)
		LogInfo("wrote %d static step boxes along +X at z=0, 5 units apart", stepCount);
	LogInfo("load it with:  PainfulEngine game %s %s", dataRoot, name.c_str());
	return 0;
}


int PoseCmd(const char* modelPath, const char* animName, const char* timeArg) {
	Model model;
	if (!Model::Load(modelPath, model) || model.meshes.empty()) {
		LogInfo("failed to load %s", modelPath);
		return 2;
	}
	if (model.bones.empty()) {
		LogInfo("%s has no skeleton", modelPath);
		return 2;
	}

	// <Model>.<anim>.ani beside the model, the engine's own naming.
	std::string dir = modelPath, base = modelPath;
	const size_t slash = base.find_last_of("/\\");
	if (slash != std::string::npos) { dir = base.substr(0, slash); base = base.substr(slash + 1); }
	else dir = ".";
	const size_t dot = base.find_last_of('.');
	if (dot != std::string::npos) base = base.substr(0, dot);

	AnimationCache cache;
	cache.SetRoot(dir);
	const Animation* anim = cache.Get(base, animName);
	if (!anim) {
		LogInfo("no animation %s.%s.ani in %s", base.c_str(), animName, dir.c_str());
		return 2;
	}

	std::vector<Bone> bones = model.bones;
	BuildHierarchy(bones);
	std::vector<Mat4> bindWorld, inverseBind;
	ComputeBindWorld(bones, bindWorld, inverseBind);

	std::vector<const AnimTrack*> tracks;
	ResolveAnimTracks(bones, *anim, tracks);
	size_t driven = 0;
	for (const AnimTrack* t : tracks) if (t) ++driven;

	const float duration = anim->duration();
	const float time = timeArg ? float(std::atof(timeArg)) : duration * 0.5f;

	std::vector<Mat4> skin;

	// Self-test, and the reason the numbers below can be trusted: with no
	// track bound to any bone, every skinning matrix is inverseBind*bindWorld,
	// which must come out exactly identity. A reversed multiply order shows up
	// here and nowhere else - the animated bounds would still look plausible.
	const std::vector<const AnimTrack*> noTracks(bones.size(), nullptr);
	ComputeSkinningMatricesAtTime(bones, inverseBind, noTracks, 0.f, skin);
	float identityError = 0.f;
	static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
										0, 0, 1, 0, 0, 0, 0, 1};
	for (const Mat4& m : skin)
		for (int c = 0; c < 16; ++c)
			identityError = std::max(identityError, std::fabs(m[c] - kIdentity[c]));

	ComputeSkinningMatricesAtTime(bones, inverseBind, tracks, time, skin);

	auto bounds = [](const std::vector<float>& v, size_t stride, Vec3& lo, Vec3& hi) {
		for (int c = 0; c < 3; ++c) { lo[c] = 1e30f; hi[c] = -1e30f; }
		for (size_t i = 0; i + stride <= v.size(); i += stride)
			for (int c = 0; c < 3; ++c) {
				lo[c] = std::min(lo[c], v[i + c]);
				hi[c] = std::max(hi[c], v[i + c]);
			}
	};

	Vec3 bLo, bHi, pLo, pHi;
	for (int c = 0; c < 3; ++c) { bLo[c] = pLo[c] = 1e30f; bHi[c] = pHi[c] = -1e30f; }
	std::vector<float> posed;
	size_t skinnedMeshes = 0;
	for (const ModelMesh& mesh : model.meshes) {
		if (mesh.vertexCount() == 0) continue;
		Vec3 lo, hi;
		bounds(mesh.verts, 8, lo, hi);
		for (int c = 0; c < 3; ++c) { bLo[c] = std::min(bLo[c], lo[c]); bHi[c] = std::max(bHi[c], hi[c]); }
		if (!mesh.hasSkin()) continue;
		++skinnedMeshes;
		SkinMeshVertices(mesh, skin, posed);
		bounds(posed, 8, lo, hi);
		for (int c = 0; c < 3; ++c) { pLo[c] = std::min(pLo[c], lo[c]); pHi[c] = std::max(pHi[c], hi[c]); }
	}

	LogInfo("%s posed by \"%s\" at t=%.3f of %.3f", modelPath, animName, time, duration);
	size_t maxKeys = 0;
	for (const AnimTrack& t : anim->tracks) maxKeys = std::max(maxKeys, t.keys.size());
	LogInfo("  %zu keys on the densest track (%.1f per second)", maxKeys,
			duration > 0.f ? float(maxKeys) / duration : 0.f);
	LogInfo("  %zu bones, %zu driven by this animation, %zu skinned meshes",
			bones.size(), driven, skinnedMeshes);
	LogInfo("  bind  %6.2f x %6.2f x %6.2f   (unanimated identity error %.6f)",
			bHi[0]-bLo[0], bHi[1]-bLo[1], bHi[2]-bLo[2], identityError);
	if (skinnedMeshes == 0) {
		LogInfo("  posed: nothing is skinned - the model draws in bind pose");
		return 0;
	}
	LogInfo("  posed %6.2f x %6.2f x %6.2f", pHi[0]-pLo[0], pHi[1]-pLo[1], pHi[2]-pLo[2]);
	return 0;
}

// What the ragdoll definition names, and how big each limb actually is.
//
// The .rde carries no shape at all - only mass and material - so this is the
// check that the shapes CAN be derived from the model, and that the bone names
// in the two files agree. A limb the .rde names but the model never weights a
// vertex to would come back with no box, and that is worth seeing.
// What the .hke actually says, and whether it agrees with everything else.
//
// Given one model it dumps the ragdoll; given a DIRECTORY it sweeps every .hke
// beside it and reports the totals. The sweep is the real test: a parser that
// reads one file proves nothing about a format nobody has documented, and the
// `unknown keywords` count is what says the coverage is complete rather than
// merely tolerant.
