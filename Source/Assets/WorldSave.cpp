// The original world save. One serializer reads and writes, so each field is
// listed once and a decoded file writes back byte for byte. The write sizes are
// the disassembly's (each Save* function); Docs/Reference/Formats.md.
#include "WorldSave.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace painful {

namespace {

constexpr uint32_t kMaxCount = 1u << 20;

constexpr uint16_t kTagWorld = 0x5e43;
constexpr uint16_t kTagPath = 0x5e44;
constexpr uint16_t kTagEntities = 0x5e45;
constexpr uint16_t kTagBody = 0x5e46;
constexpr uint16_t kTagPhysics = 0x5e47;
constexpr uint16_t kTagRagdoll = 0x5e49;
constexpr uint16_t kTagPhysicsWorld = 0x5e50;
constexpr uint16_t kTagConstraints = 0x5e51;
constexpr uint16_t kTagEntity = 0x5e53;
constexpr uint16_t kTagGlass = 0x5e70;

uint16_t ClassTag(uint32_t type) {
	switch (type) {
	case kWsWorldMesh: return 0x5e54;
	case kWsModel: return 0x5e55;
	case kWsSound: return 0x5e56;
	case kWsDecal: return 0x5e80;
	case kWsTrail: return 0x5e85;
	case kWsBillboard: return 0x5e90;
	case kWsParticle: return 0x5e93;
	case kWsLight: return 0x5ea0; // Light::LoadEntity's check; SaveEntity computes it
	default: return 0;
	}
}

class Archive {
public:
	Archive(std::vector<uint8_t>& buf, bool writing, const WorldSave::Resolver* models) : buf_(buf), writing_(writing), models_(models) {}

	bool writing() const { return writing_; }
	bool reading() const { return !writing_; }
	bool ok() const { return error_.empty(); }
	size_t pos() const { return writing_ ? buf_.size() : pos_; }
	size_t size() const { return buf_.size(); }
	const std::string& error() const { return error_; }
	size_t errorAt() const { return errorAt_; }

	void Context(const char* fmt, ...) {
		char text[256];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(text, sizeof text, fmt, ap);
		va_end(ap);
		context_ = text;
	}

	void Fail(const char* fmt, ...) {
		if (!error_.empty()) return;
		char text[512];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(text, sizeof text, fmt, ap);
		va_end(ap);
		error_ = context_.empty() ? text : context_ + ": " + text;
		errorAt_ = pos();
	}

	void Raw(void* p, size_t n) {
		if (n == 0) return;
		if (writing_) {
			const uint8_t* b = static_cast<const uint8_t*>(p);
			buf_.insert(buf_.end(), b, b + n);
			return;
		}
		if (!ok() || n > buf_.size() - pos_) {
			Fail("%zu bytes past the end", n);
			std::memset(p, 0, n);
			return;
		}
		std::memcpy(p, buf_.data() + pos_, n);
		pos_ += n;
	}
	void U8(uint8_t& v) { Raw(&v, 1); }
	void U16(uint16_t& v) { Raw(&v, 2); }
	void U32(uint32_t& v) { Raw(&v, 4); }
	void I32(int32_t& v) { Raw(&v, 4); }
	void F32(float& v) { Raw(&v, 4); }
	void F(float* p, size_t n) { Raw(p, 4 * n); }
	void U(uint32_t* p, size_t n) { Raw(p, 4 * n); }
	void V(Vec3& v) { Raw(v.p(), 12); }

	void Tag(uint16_t expected, const char* what) {
		uint16_t t = expected;
		U16(t);
		if (reading() && ok() && t != expected) Fail("%s: tag %04x, expected %04x", what, t, expected);
	}
	// A tag whose value the code computes rather than pushes.
	void AnyTag(uint16_t& t, const char* what) {
		U16(t);
		if (reading() && ok() && (t & 0xff00) != 0x5e00) Fail("%s: %04x is not a tag", what, t);
	}

	// u32 length, then that many bytes.
	void Str(std::string& s) {
		uint32_t n = uint32_t(s.size());
		U32(n);
		if (writing_) {
			buf_.insert(buf_.end(), s.begin(), s.end());
			return;
		}
		if (!ok()) return;
		if (n > buf_.size() - pos_) {
			Fail("string of %u bytes past the end", n);
			return;
		}
		s.assign(reinterpret_cast<const char*>(buf_.data() + pos_), n);
		pos_ += n;
	}

	// Written from the container's size; read, checked, and the container sized.
	template <class T> bool Count(std::vector<T>& v, const char* what) {
		uint32_t n = uint32_t(v.size());
		U32(n);
		if (reading()) {
			if (!ok()) return false;
			if (n > kMaxCount) {
				Fail("%s: count %u", what, n);
				return false;
			}
			v.assign(n, T());
		}
		return ok();
	}

	int MeshCount(const std::string& model) {
		const int n = models_ && models_->meshCount ? models_->meshCount(model) : -1;
		if (n < 0) Fail("model %s: mesh count unknown", model.c_str());
		return n;
	}
	bool RagdollCounts(const std::string& model, int& constraints, int& actions) {
		if (models_ && models_->ragdoll && models_->ragdoll(model, constraints, actions)) return true;
		Fail("ragdoll %s: .hke counts unknown", model.c_str());
		return false;
	}

private:
	std::vector<uint8_t>& buf_;
	bool writing_;
	const WorldSave::Resolver* models_;
	size_t pos_ = 0;
	std::string error_, context_;
	size_t errorAt_ = 0;
};

void Chunk(Archive& ar, WsChunk& c) {
	const size_t at = ar.pos();
	ar.Str(c.name);
	if (ar.writing()) {
		uint32_t start = uint32_t(at);
		uint32_t size = uint32_t(4 + c.name.size() + 8 + c.body.size());
		ar.U32(start);
		ar.U32(size);
		ar.Raw(c.body.data(), c.body.size());
		return;
	}
	ar.U32(c.start);
	ar.U32(c.size);
	if (!ar.ok()) return;
	if (c.start != at || at + c.size < ar.pos()) {
		ar.Fail("chunk at %zu states start %u, size %u", at, c.start, c.size);
		return;
	}
	c.body.resize(at + c.size - ar.pos());
	ar.Raw(c.body.data(), c.body.size());
}

// Bytes after the type byte, from FUN_101c4390's write runs.
size_t ConstraintSize(uint8_t type) {
	switch (type) {
	case 0: case 1: return 12;
	case 2: return 25;
	case 3: case 4: case 5: case 7: return 16;
	default: return 0;
	}
}

void Constraints(Archive& ar, WsConstraints& c) {
	ar.Tag(kTagConstraints, "constraints");
	ar.U8(c.hasHeader);
	if (c.hasHeader) ar.F(c.header, 3);
	if (!ar.Count(c.records, "constraints")) return;
	for (WsConstraints::Record& r : c.records) {
		ar.U8(r.type);
		const size_t size = ConstraintSize(r.type);
		if (!size) {
			ar.Fail("constraint type %u", r.type);
			return;
		}
		if (ar.reading()) r.data.resize(size);
		ar.Raw(r.data.data(), r.data.size());
	}
}

void PhysicsObject(Archive& ar, WsPhysicsObject& p) {
	ar.U32(p.type);
	ar.F32(p.scaleArg);
	ar.Tag(kTagBody, "physics object");
	ar.U32(p.flags);
	ar.U8(p.freedom);
	ar.V(p.velocity);
	ar.V(p.angularVelocity);
	ar.U8(p.collisionGroup);
	ar.U8(p.b2);
	ar.U8(p.b3);
	ar.U8(p.lineTrace);
	ar.F32(p.mass);
	ar.F32(p.friction);
	ar.F32(p.restitution);
	ar.F32(p.linearDamping);
	ar.F32(p.angularDamping);
	ar.U8(p.b5);
	ar.U32(p.u18);
	ar.F(p.inertia, 9);
	if (p.flags & 2) {
		ar.F(p.sight, 4);
		ar.V(p.moveWish);
		ar.V(p.ext60);
		ar.F32(p.moveConst);
		ar.U8(p.moveFlag);
		ar.U8(p.ext71);
	}
	ar.U8(p.hasController);
	if (p.hasController) ar.Raw(p.controller, sizeof p.controller);
	if (ar.ok()) Constraints(ar, p.constraints);
}

void Ragdoll(Archive& ar, WsRagdoll& r, const std::string& model) {
	ar.Tag(kTagRagdoll, "ragdoll");
	if (!ar.Count(r.bodies, "ragdoll bodies")) return;
	// Column by column, then one row per body.
	for (WsRagdoll::Body& b : r.bodies) ar.V(b.pos);
	for (WsRagdoll::Body& b : r.bodies) ar.F(b.rot, 4);
	for (WsRagdoll::Body& b : r.bodies) ar.V(b.velocity);
	for (WsRagdoll::Body& b : r.bodies) ar.V(b.angularVelocity);
	for (WsRagdoll::Body& b : r.bodies) ar.U8(b.flag);
	for (WsRagdoll::Body& b : r.bodies) {
		ar.U8(b.hasExtra);
		if (b.hasExtra) ar.F(b.extra, 3);
		ar.F32(b.mass);
		ar.U8(b.b);
		ar.F32(b.c);
		ar.F32(b.d);
		ar.F32(b.e);
		ar.F(b.f, 8);
		ar.U8(b.g);
	}
	ar.F(r.tail, 3);
	if (!ar.ok()) return;
	if (ar.reading()) {
		int constraints = 0, actions = 0;
		if (!ar.RagdollCounts(model, constraints, actions)) return;
		r.constraints.assign(size_t(constraints), WsRagdoll::Constraint());
		r.actions.assign(size_t(actions), WsRagdoll::Action());
	}
	for (WsRagdoll::Constraint& c : r.constraints) {
		ar.U8(c.type);
		if (c.type >= 10) {
			ar.U8(c.breakable);
			ar.F32(c.strength);
		}
		ar.F(c.data, 8);
		ar.U8(c.after);
	}
	for (WsRagdoll::Action& a : r.actions) {
		ar.U8(a.skip);
		if (!a.skip) {
			ar.U8(a.kind);
			if (a.kind >= 2) ar.F(a.data, 8);
		}
		ar.U8(a.after);
	}
	ar.U8(r.flag);
	ar.V(r.gravity);
}

void RagdollGroups(Archive& ar, WsRagdollGroups& g) {
	ar.AnyTag(g.tag, "ragdoll groups");
	if (!ar.Count(g.records, "ragdoll groups")) return;
	for (WsRagdollGroups::Record& r : g.records) {
		ar.U8(r.has);
		if (r.has) ar.F(r.v, 3);
		ar.U8(r.after);
	}
}

void EntityBase(Archive& ar, WsEntityBase& e) {
	ar.Tag(kTagEntity, "entity base");
	ar.I32(e.handle);
	ar.F32(e.fb0);
	ar.F32(e.fb4);
	ar.U8(e.deathZoneTest);
	ar.U32(e.u24);
	ar.U8(e.b28);
	ar.U32(e.flags);
	ar.U32(e.u2c);
	ar.U32(e.u30);
	ar.U32(e.u34);
	ar.U8(e.hasParent);
	if (e.hasParent) {
		ar.I32(e.parent);
		ar.U8(e.joint);
		ar.U8(e.follows);
		ar.U8(e.dieWithParent);
	}
}

void ModelBody(Archive& ar, WsModel& m, const std::string& model) {
	ar.V(m.pos);
	ar.F(m.rot, 4);
	ar.F32(m.f700);
	ar.Str(m.material);
	ar.U8(m.shadow);
	if (!ar.ok()) return;
	if (ar.reading()) {
		const int meshes = ar.MeshCount(model);
		if (meshes < 0) return;
		m.meshVisible.assign(size_t(meshes), 0);
	}
	ar.Raw(m.meshVisible.data(), m.meshVisible.size());

	ar.U32(m.slotCount);
	if (!ar.ok()) return;
	if (m.slotCount > 64) {
		ar.Fail("%u animation slots", m.slotCount);
		return;
	}
	const size_t written = m.slotCount > 1 ? m.slotCount - 1 : 0;
	if (ar.reading()) m.slots.assign(written, WsModel::Slot());
	else if (m.slots.size() != written) ar.Fail("%zu slots for a count of %u", m.slots.size(), m.slotCount);
	for (WsModel::Slot& s : m.slots) {
		ar.U8(s.present);
		if (!s.present) continue;
		ar.Str(s.anim);
		ar.U8(s.loop);
		ar.U32(s.curveMask);
	}

	if (!ar.Count(m.channels, "animation channels")) return;
	for (WsModel::Channel& c : m.channels) {
		ar.U8(c.slot);
		ar.F32(c.time);
		ar.F32(c.weight);
		ar.F32(c.speed);
		ar.U8(c.loop);
		ar.U8(c.e);
		ar.F32(c.blend);
		ar.F32(c.blendLeft);
	}

	ar.U8(m.hasBody);
	if (m.hasBody) PhysicsObject(ar, m.body);
	if (!ar.ok()) return;
	ar.U8(m.ragdollOn);
	if (m.ragdollOn) {
		Ragdoll(ar, m.ragdoll, model);
		return;
	}
	ar.U8(m.hasGroups);
	if (m.hasGroups) RagdollGroups(ar, m.groups);
}

void WorldMeshBody(Archive& ar, WsWorldMesh& w) {
	ar.V(w.pos);
	ar.F(w.rot, 4);
	ar.U8(w.hasBody);
	ar.Str(w.material);
	ar.U(w.materialData, 4);
	ar.Str(w.s684);
	ar.Str(w.s690);
	if (w.hasBody && ar.ok()) PhysicsObject(ar, w.body);
}

void LightBody(Archive& ar, WsLight& l) {
	ar.U32(l.flags);
	ar.V(l.pos);
	ar.U32(l.type);
	ar.U32(l.argb);
	ar.F32(l.intensity);
	ar.V(l.dir);
	ar.F32(l.coneCos);
	ar.F32(l.coneInnerCos);
	ar.F32(l.range);
	ar.F32(l.startFalloff);
	ar.Str(l.projector);
}

void ParticleBody(Archive& ar, WsParticle& p) {
	ar.U32(p.u18);
	ar.V(p.pos);
	ar.V(p.v614);
	ar.F32(p.f610);
	ar.V(p.parentOffset);
	ar.U8(p.bc88);
	ar.V(p.vc90);
	ar.F32(p.fc8c);
	ar.V(p.vc9c);
	ar.U8(p.bca8);
	ar.U8(p.bcaa);
	ar.U8(p.bca9);
	if (!ar.Count(p.emitters, "emitters")) return;
	for (WsParticle::Emitter& e : p.emitters) {
		ar.Str(e.file);
		ar.V(e.a);
		ar.F32(e.b);
		ar.V(e.c);
		ar.V(e.d);
		ar.Raw(e.flags, sizeof e.flags);
		ar.V(e.offset);
		ar.V(e.rotDeg);
		ar.F32(e.scale);
	}
}

void SoundBody(Archive& ar, WsSound& s) {
	ar.U(s.v, 10);
	ar.Str(s.file);
	ar.U8(s.b);
	if (ar.ok()) Chunk(ar, s.sample);
}

void TrailBody(Archive& ar, WsTrail& t) {
	ar.Str(t.texture);
	ar.V(t.pos);
	ar.F32(t.f680);
	ar.U32(t.unknown);
	ar.F32(t.f868);
	ar.U8(t.b86c);
	ar.U32(t.capacity);
	ar.U32(t.segments);
	ar.U32(t.end);
	ar.U32(t.start);
	if (!ar.ok()) return;
	const size_t n = t.segments < 2 ? 2 : t.segments;
	if (n > 4096 || (t.start != t.end && (t.capacity == 0 || t.start >= t.capacity || t.end >= t.capacity))) {
		ar.Fail("trail: %u segments, ring %u..%u of %u", t.segments, t.start, t.end, t.capacity);
		return;
	}
	if (ar.reading()) {
		t.segA.assign(n, 0.f);
		t.segB.assign(n, Vec3());
		t.segC.assign(n, Vec3());
		t.frames.clear();
		for (uint32_t i = t.start; i != t.end; i = (i + 1) % t.capacity) {
			t.frames.emplace_back();
			t.frames.back().points.assign(n, Vec3());
		}
	}
	for (float& f : t.segA) ar.F32(f);
	for (Vec3& v : t.segB) ar.V(v);
	for (Vec3& v : t.segC) ar.V(v);
	for (WsTrail::Frame& f : t.frames) {
		ar.U32(f.a);
		for (Vec3& v : f.points) ar.V(v);
	}
}

void DecalBody(Archive& ar, WsDecal& d) {
	ar.Str(d.texture);
	ar.F32(d.f678);
	ar.F32(d.f67c);
	ar.F32(d.f684);
	ar.U8(d.b688);
	ar.F(d.matrix, 16);
	if (!ar.Count(d.verts, "decal vertices")) return;
	for (WsDecal::Vert& v : d.verts) {
		ar.V(v.p);
		ar.F32(v.a);
		ar.F32(v.b);
		ar.F32(v.c);
	}
}

void BillboardBody(Archive& ar, WsBillboard& b) {
	ar.V(b.pos);
	ar.V(b.v614);
	ar.F32(b.f610);
	ar.U32(b.corona);
	ar.U32(b.blend);
	ar.Str(b.texture);
	ar.F(b.f, 17);
}

// The record a class's SaveEntity writes. World::SaveEntities follows it with the
// in-world byte; a glass shard does not.
void EntityRecord(Archive& ar, WsEntity& e) {
	ar.U32(e.type);
	ar.Str(e.resource);
	ar.Str(e.name);
	ar.U32(e.headerValue);
	ar.U8(e.headerFlag);
	if (!ar.ok()) return;
	const uint16_t tag = ClassTag(e.type);
	if (tag) ar.Tag(tag, WsTypeName(e.type));
	else {
		ar.Fail("entity type %u", e.type);
		return;
	}
	EntityBase(ar, e.base);
	if (!ar.ok()) return;
	const std::string model = WsText(e.resource);
	switch (e.type) {
	case kWsWorldMesh: WorldMeshBody(ar, e.mesh); break;
	case kWsLight: LightBody(ar, e.light); break;
	case kWsParticle: ParticleBody(ar, e.particle); break;
	case kWsModel: ModelBody(ar, e.model, model); break;
	case kWsDecal: DecalBody(ar, e.decal); break;
	case kWsBillboard: BillboardBody(ar, e.billboard); break;
	case kWsTrail: TrailBody(ar, e.trail); break;
	case kWsSound: SoundBody(ar, e.sound); break;
	}
}

void Glass(Archive& ar, WsGlass& g) {
	ar.Tag(kTagGlass, "glass");
	ar.Str(g.name);
	ar.U8(g.b3);
	ar.U8(g.bd);
	ar.U32(g.u1);
	ar.U8(g.b8);
	ar.V(g.v24);
	ar.U32(g.u2);
	ar.V(g.v34);
	if (!ar.Count(g.points, "glass points")) return;
	for (WsGlass::Point& p : g.points) {
		ar.V(p.p);
		ar.U32(p.v);
	}
	if (ar.reading()) g.pieces.assign(g.points.size() + 1, WsGlass::Piece());
	else if (g.pieces.size() != g.points.size() + 1) {
		ar.Fail("%zu glass pieces for %zu points", g.pieces.size(), g.points.size());
		return;
	}
	for (WsGlass::Piece& p : g.pieces) {
		ar.U8(p.present);
		if (!p.present) continue;
		EntityRecord(ar, p.entity);
		ar.U8(p.bc0);
		ar.I32(p.index10);
		ar.I32(p.index5);
		if (p.index5 != -1) ar.U32(p.value);
		if (!ar.ok()) return;
	}
}

void PhysicsWorld(Archive& ar, WsPhysicsWorld& w) {
	ar.Tag(kTagPhysicsWorld, "physics world");
	if (!ar.Count(w.layers, "collision layers")) return;
	for (WsPhysicsWorld::Layer& l : w.layers) {
		ar.U32(l.index);
		ar.U8(l.flag);
		ar.F(l.v, 7);
	}
	if (!ar.Count(w.groups, "collision groups")) return;
	for (WsPhysicsWorld::Group& g : w.groups) {
		ar.U8(g.a);
		ar.U32(g.b);
		ar.U8(g.c);
		ar.U8(g.d);
		ar.F32(g.e);
	}
	if (!ar.Count(w.bodies, "world bodies")) return;
	for (WsPhysicsWorld::Body& b : w.bodies) {
		ar.U8(b.present);
		if (!b.present) continue;
		ar.F(b.a, 3);
		ar.F(b.b, 3);
		ar.F32(b.c);
		PhysicsObject(ar, b.po);
		ar.U32(b.entity18);
		ar.U32(b.entityB0);
		if (!ar.ok()) return;
	}
	if (!ar.Count(w.pairs, "group pairs")) return;
	for (WsPhysicsWorld::Pair& p : w.pairs) {
		ar.U8(p.present);
		if (!p.present) continue;
		ar.U32(p.a);
		ar.U8(p.b);
		ar.U8(p.c);
		ar.U32(p.d);
	}
}

void ByteList(Archive& ar, std::vector<uint8_t>& v, const char* what) {
	if (ar.Count(v, what)) ar.Raw(v.data(), v.size());
}

void Serialize(Archive& ar, WorldSave& s) {
	ar.Tag(kTagWorld, "world save");
	ar.U32(s.version);
	if (ar.reading() && ar.ok() && s.version != 3) ar.Fail("version %u", s.version);
	if (!ar.Count(s.glass, "glass panes")) return;
	for (size_t i = 0; i < s.glass.size(); ++i) {
		ar.Context("glass pane %zu of %zu", i, s.glass.size());
		Glass(ar, s.glass[i]);
		if (!ar.ok()) return;
	}
	ar.Context("%s", "");
	Chunk(ar, s.audio);

	if (ar.reading()) s.physicsAt = ar.pos();
	ar.Tag(kTagPhysics, "physics");
	if (!ar.Count(s.worlds, "physics worlds")) return;
	for (WsPhysicsWorld& w : s.worlds) {
		PhysicsWorld(ar, w);
		if (!ar.ok()) return;
	}
	ar.U32(s.physicsC);
	ar.U32(s.physics14);

	if (ar.reading()) s.pathsAt = ar.pos();
	if (!ar.Count(s.paths, "paths")) return;
	for (WorldSave::Path& p : s.paths) {
		ar.U8(p.active);
		if (!p.active) continue;
		ar.Tag(kTagPath, "path");
		ar.V(p.from);
		ar.V(p.to);
	}

	if (ar.reading()) s.entitiesAt = ar.pos();
	ar.Tag(kTagEntities, "entities");
	ar.U8(s.e6dc);
	ar.U8(s.e6dd);
	ar.U8(s.e6bb);
	if (!ar.Count(s.entities, "entities")) return;
	for (size_t i = 0; i < s.entities.size(); ++i) {
		if (ar.reading()) s.entityAt.push_back(ar.pos());
		ar.Context("entity %zu of %zu", i, s.entities.size());
		EntityRecord(ar, s.entities[i]);
		ar.U8(s.entities[i].inWorld);
		if (!ar.ok()) return;
	}
	ar.Context("%s", "");

	if (ar.reading()) s.portalsAt = ar.pos();
	ByteList(ar, s.portals, "portals");
	ByteList(ar, s.antiportals, "antiportals");
	ByteList(ar, s.zones, "zones");
	if (ar.reading() && ar.ok() && ar.pos() != ar.size())
		ar.Fail("%zu bytes after the zone state", ar.size() - ar.pos());
}

} // namespace

std::string WsText(const std::string& s) {
	size_t n = s.size();
	while (n > 0 && s[n - 1] == '\0') --n;
	return s.substr(0, n);
}

const char* WsTypeName(uint32_t type) {
	switch (type) {
	case kWsWorldMesh: return "WorldMesh";
	case kWsLight: return "Light";
	case kWsParticle: return "ParticleEffect";
	case kWsModel: return "Model";
	case kWsDecal: return "Decal";
	case kWsBillboard: return "Billboard";
	case kWsTrail: return "Trail";
	case kWsSound: return "Sound";
	default: return "?";
	}
}

bool WsAudioStreams(const std::vector<uint8_t>& body, std::vector<WsStream>& out) {
	out.clear();
	if (body.empty()) return true;
	// The archive only reads from the buffer in this mode.
	Archive ar(const_cast<std::vector<uint8_t>&>(body), false, nullptr);
	ar.Tag(0x5e60, "audio");
	uint32_t header[21];
	ar.U(header, 21);
	// The sample cache (FUN_101f8080): a file, six u32, a loaded byte.
	uint32_t samples = 0;
	ar.U32(samples);
	if (samples > kMaxCount) ar.Fail("%u cached samples", samples);
	for (uint32_t i = 0; i < samples && ar.ok(); ++i) {
		std::string file;
		ar.Str(file);
		uint32_t v[6];
		ar.U(v, 6);
		uint8_t loaded = 0;
		ar.U8(loaded);
	}
	// The pause sets (PauseCurrentlyPlayingSounds): a present byte, u32 n and n stream
	// slots, u32 m and m "was playing" bytes.
	uint32_t groups = 0;
	ar.U32(groups);
	if (groups > kMaxCount) ar.Fail("%u pause sets", groups);
	std::vector<std::vector<std::pair<uint32_t, uint8_t>>> sets;
	for (uint32_t i = 0; i < groups && ar.ok(); ++i) {
		sets.emplace_back();
		uint8_t present = 0;
		ar.U8(present);
		if (!present) continue;
		uint32_t n = 0;
		ar.U32(n);
		if (n > kMaxCount) {
			ar.Fail("pause set of %u", n);
			break;
		}
		std::vector<uint32_t> slots(n, 0);
		for (uint32_t& slot : slots) ar.U32(slot);
		ar.U32(n);
		if (n > kMaxCount) {
			ar.Fail("pause set flags %u", n);
			break;
		}
		std::vector<uint8_t> flags(n, 0);
		ar.Raw(flags.data(), flags.size());
		for (size_t k = 0; k < slots.size(); ++k)
			sets.back().emplace_back(slots[k], k < flags.size() ? flags[k] : uint8_t(0));
	}
	ar.Tag(0x5f00, "music");
	uint32_t count = 0;
	ar.U32(count);
	if (!ar.ok() || count > 64) return false;
	out.assign(count, WsStream());
	for (WsStream& s : out) {
		ar.U8(s.present);
		if (!s.present) continue;
		ar.Str(s.file);
		ar.F32(s.volume);
		ar.U32(s.rate);
		ar.U32(s.u1c);
		ar.U32(s.loopCount);
		ar.U32(s.position);
	}
	uint32_t globals[4];
	ar.U(globals, 4);
	ar.Tag(0x5f01, "2D sounds");
	if (header[20] < sets.size())
		for (const std::pair<uint32_t, uint8_t>& entry : sets[header[20]])
			if (entry.second && entry.first < out.size()) out[entry.first].resumes = true;
	return ar.ok();
}

std::vector<uint8_t> WsAudioBody(const std::vector<WsStream>& streams) {
	const auto bits = [](float f) {
		uint32_t v = 0;
		std::memcpy(&v, &f, 4);
		return v;
	};
	std::vector<uint8_t> out;
	Archive ar(out, true, nullptr);
	ar.Tag(0x5e60, "audio");
	// The values every original save holds. +0x3c and +0x40 are GetTickCount stamps
	// and +0x220 and +0x2a4 counters, written as zero.
	uint32_t header[21] = {bits(2.f), 0x81, bits(1.f), bits(1.f), bits(1.f), bits(1.f), 0, 0, bits(1.f),
			bits(4.f), bits(0.2f), 0, bits(1.f), bits(1.f), bits(1000.f), 0x447a00a4, 0, 0, 0xffffffffu,
			0xffffffffu, 0};
	ar.U(header, 21);
	uint32_t zero = 0;
	ar.U32(zero); // cached samples
	// One pause set, number 0 as header[20] says: the streams SaveGame_ResumeSounds
	// restarts once the load is done.
	std::vector<uint32_t> resume;
	for (size_t slot = 0; slot < streams.size(); ++slot)
		if (streams[slot].present && streams[slot].resumes) resume.push_back(uint32_t(slot));
	uint32_t sets = 1, n = uint32_t(resume.size());
	uint8_t present = 1, playing = 1;
	ar.U32(sets);
	ar.U8(present);
	ar.U32(n);
	for (uint32_t slot : resume) ar.U32(slot);
	ar.U32(n);
	for (size_t i = 0; i < resume.size(); ++i) ar.U8(playing);
	ar.Tag(0x5f00, "music");
	uint32_t count = uint32_t(streams.size());
	ar.U32(count);
	for (WsStream s : streams) {
		ar.U8(s.present);
		if (!s.present) continue;
		ar.Str(s.file);
		ar.F32(s.volume);
		ar.U32(s.rate);
		ar.U32(s.u1c);
		ar.U32(s.loopCount);
		ar.U32(s.position);
	}
	uint32_t globals[4] = {0, 0xffffffffu, 0xffffffffu, 0xffffffffu};
	ar.U(globals, 4);
	ar.Tag(0x5f01, "2D sounds");
	ar.U32(zero);
	ar.Tag(0x5f02, "3D sounds");
	ar.U32(zero);
	ar.Tag(0x5f03, "audio end");
	return out;
}

bool WorldSave::Read(const std::vector<uint8_t>& data, WorldSave& out, const Resolver& models) {
	out = WorldSave();
	// The archive only reads from the buffer in this mode.
	Archive ar(const_cast<std::vector<uint8_t>&>(data), false, &models);
	Serialize(ar, out);
	out.error = ar.error();
	out.errorAt = ar.errorAt();
	return ar.ok();
}

bool WorldSave::Write(std::vector<uint8_t>& out) const {
	out.clear();
	// The archive only reads from the object in this mode.
	Archive ar(out, true, nullptr);
	Serialize(ar, const_cast<WorldSave&>(*this));
	return ar.ok();
}

} // namespace painful
