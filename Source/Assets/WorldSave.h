#pragma once
#include "../Core/Vectors.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace painful {

// PainEngine's own world save: <level>.World inside Save.dat, written by
// PCFSystem::SaveGame (Engine.dll 0x100518a0). "C^", version 3, then glass, audio,
// physics, pathfinding, entities, portal and zone state; blocks open with a u16
// tag 0x5eXX. Docs/Reference/Formats.md, "The original world save".
//
// A field is named by what it is where that is recovered and by its offset in the
// engine object otherwise (fb4 = Entity+0xb4). Strings keep their bytes, the NUL
// included, so a decoded file writes back byte for byte. A rotation is stored
// x, y, z then w; the engine's own order is w first.

// A block that states its own extent: start is the file offset of this header,
// size runs from there to the end of the body.
struct WsChunk {
	std::string name;
	uint32_t start = 0, size = 0;
	std::vector<uint8_t> body;
};

// ^Q, FUN_101c4390. A record's size follows from its type byte.
struct WsConstraints {
	uint8_t hasHeader = 0;
	float header[3] = {};
	struct Record {
		uint8_t type = 0;
		std::vector<uint8_t> data;
	};
	std::vector<Record> records;
};

// ^F, PhysicsObject::SavePO 0x1018ca10.
struct WsPhysicsObject {
	// Before the tag (Model::LoadEntity reads them itself): PO_Create's body type
	// and scale argument, -1 when flag 0x10 is set.
	uint32_t type = 0;
	float scaleArg = 0.f;
	uint32_t flags = 0; // +0x74: 2 monster (adds the monster block), 0x20 grenade, 0x800 flying
	// +0x10, EFreedomsOfRotation: 1 (CreatePhysicsObject's default), 3 a CObject
	// item, 2 broken debris. LoadPO re-applies it when it differs.
	uint8_t freedom = 0;
	Vec3 velocity, angularVelocity;
	uint8_t collisionGroup = 0; // ECollisionGroups
	uint8_t b2 = 0, b3 = 0, lineTrace = 0;
	float mass = 0.f, friction = 0.f, restitution = 0.f;
	float linearDamping = 0.f, angularDamping = 0.f;
	uint8_t b5 = 0;
	uint32_t u18 = 0;
	float inertia[9] = {}; // inverse, infinite for the player
	// The monster block: PO_SetSightParams (+0x24..+0x30), PO_Move's wish
	// (+0x34), PO_SetMonsterMovementConst (+0x6c, +0x70).
	// 0x10131210: range, 360 range, yaw (degrees * pi/360), pitch (degrees * pi/180).
	float sight[4] = {};
	Vec3 moveWish, ext60;
	float moveConst = 0.f;
	uint8_t moveFlag = 0, ext71 = 0;
	uint8_t hasController = 0; // the player's mover
	uint8_t controller[120] = {}; // FUN_1018b150
	WsConstraints constraints;
};

// ^I, FUN_101b0330. The constraint and action counts are not in the file: they
// are the model's .hke (Resolver::ragdoll).
struct WsRagdoll {
	struct Body {
		Vec3 pos;
		float rot[4] = {};
		Vec3 velocity, angularVelocity;
		uint8_t flag = 0;
		uint8_t hasExtra = 0;
		float extra[3] = {};
		float mass = 0.f; // 0 = fixed
		uint8_t b = 0;
		float c = 0.f, d = 0.f, e = 0.f;
		float f[8] = {};
		uint8_t g = 0;
	};
	std::vector<Body> bodies;
	float tail[3] = {};
	struct Constraint {
		uint8_t type = 0; // 10 and up carry the breakable pair, then type - 10
		uint8_t breakable = 0;
		float strength = 0.f;
		float data[8] = {};
		uint8_t after = 0;
	};
	std::vector<Constraint> constraints;
	struct Action {
		uint8_t skip = 0; // non-zero: nothing more is written
		uint8_t kind = 0; // 2 and up carry data
		float data[8] = {};
		uint8_t after = 0;
	};
	std::vector<Action> actions;
	uint8_t flag = 0;
	Vec3 gravity;
};

// FUN_101ae330: the collision groups of a ragdoll that is not simulating.
struct WsRagdollGroups {
	uint16_t tag = 0;
	struct Record {
		uint8_t has = 0;
		float v[3] = {};
		uint8_t after = 0;
	};
	std::vector<Record> records;
};

// ^S, Entity::SaveEntity 0x101d1240.
struct WsEntityBase {
	int32_t handle = 0; // +0x14, the index scripts keep in _Entity
	float fb0 = 0.f, fb4 = 0.f;
	uint8_t deathZoneTest = 0; // +0x11b
	uint32_t u24 = 0;
	uint8_t b28 = 0;
	uint32_t flags = 0; // +0x18: 0x40 draw off (Entity::EnableDraw), 0x400000 dynamic light
	uint32_t u2c = 0, u30 = 0, u34 = 0;
	uint8_t hasParent = 0;
	// ENTITY.RegisterChild: the parent's handle, the joint (+0x118, a char, -1
	// none), follows (+0x119) and dies with the parent (+0x11a).
	int32_t parent = 0;
	uint8_t joint = 0, follows = 0, dieWithParent = 0;
};

// ^U, Model::SaveEntity 0x101df560.
struct WsModel {
	Vec3 pos;
	float rot[4] = {};
	float f700 = 0.f;
	std::string material;
	uint8_t shadow = 0;
	std::vector<uint8_t> meshVisible; // one per .pkmdl mesh, in file order
	uint32_t slotCount = 0; // slot 0 is the model's own and not written
	struct Slot {
		uint8_t present = 0;
		std::string anim;
		uint8_t loop = 0;
		uint32_t curveMask = 0; // Model::LoadEntity: the movement curve on "ROOOT"
	};
	std::vector<Slot> slots;
	// What is playing (+0x7a8), from Model::ActivateAnimation.
	struct Channel {
		uint8_t slot = 0; // 0 = nothing
		float time = 0.f, weight = 0.f, speed = 0.f;
		uint8_t loop = 0, e = 0;
		float blend = 0.f, blendLeft = 0.f;
	};
	std::vector<Channel> channels;
	uint8_t hasBody = 0;
	WsPhysicsObject body;
	uint8_t ragdollOn = 0;
	WsRagdoll ragdoll;
	uint8_t hasGroups = 0;
	WsRagdollGroups groups;
};

// ^T, WorldMesh::SaveEntity 0x101db190. The level's own active meshes are not
// saved (World::SaveEntities skips +0x7dc != -1); these are the scripts' packs.
struct WsWorldMesh {
	Vec3 pos;
	float rot[4] = {};
	uint8_t hasBody = 0;
	std::string material;
	uint32_t materialData[4] = {};
	std::string s684, s690;
	WsPhysicsObject body;
};

// 0x5ea0, Light::SaveEntity 0x101d5650. LIGHT.Setup / SetFalloff / SetIntensity
// (0x101375f0, 0x10137720, 0x10137820) name the fields.
struct WsLight {
	uint32_t flags = 0; // Entity+0x18 again
	Vec3 pos;
	uint32_t type = 0; // +0x7f4
	uint32_t argb = 0; // +0x678
	float intensity = 0.f; // +0x67c
	Vec3 dir; // +0x7e4
	float coneCos = 0.f; // +0x7f0, cos(angle)
	float coneInnerCos = 0.f; // +0x680, cos(0.8 angle)
	float range = 0.f; // +0x7f8
	float startFalloff = 0.f; // +0x7fc
	std::string projector;
};

// 0x5e93, ParticleEffect::SaveEntity 0x101e4c10.
struct WsParticle {
	uint32_t u18 = 0;
	Vec3 pos, v614;
	float f610 = 0.f;
	Vec3 parentOffset; // +0xc7c
	uint8_t bc88 = 0;
	Vec3 vc90;
	float fc8c = 0.f;
	Vec3 vc9c;
	uint8_t bca8 = 0, bcaa = 0, bca9 = 0;
	struct Emitter {
		std::string file;
		Vec3 a;
		float b = 0.f;
		Vec3 c, d;
		uint8_t flags[5] = {};
		// PARTICLE.SetupEmitter's arguments (0x10139cb0).
		Vec3 offset, rotDeg;
		float scale = 0.f;
	};
	std::vector<Emitter> emitters;
};

// ^V, Sound::SaveEntity 0x101e8380. The sample is a chunk (Sound2D/3D_Save).
struct WsSound {
	uint32_t v[10] = {};
	std::string file;
	uint8_t b = 0;
	WsChunk sample;
};

// 0x5e85, Trail::SaveEntity 0x101eaa00.
struct WsTrail {
	std::string texture;
	Vec3 pos;
	float f680 = 0.f;
	uint32_t unknown = 0;
	float f868 = 0.f;
	uint8_t b86c = 0;
	uint32_t capacity = 0, segments = 0, end = 0, start = 0;
	std::vector<float> segA; // max(segments, 2) each
	std::vector<Vec3> segB, segC;
	struct Frame {
		uint32_t a = 0;
		std::vector<Vec3> points;
	};
	std::vector<Frame> frames; // start .. end, wrapping at capacity
};

// 0x5e80, Decal::SaveEntity 0x101cfec0.
struct WsDecal {
	std::string texture;
	float f678 = 0.f, f67c = 0.f, f684 = 0.f;
	uint8_t b688 = 0;
	float matrix[16] = {};
	struct Vert {
		Vec3 p;
		float a = 0.f, b = 0.f, c = 0.f;
	};
	std::vector<Vert> verts;
};

// 0x5e90, Billboard::SaveEntity 0x101cc5c0. BILLBOARD.SetupCorona (0x10137b70)
// stores its arguments in f: colour 0 (an int), fadeIn 2, fadeOut 3, alpha 5,
// maxDistance 7, minDistance 8, size 9, minSize 10, offDistance 11, traceMargin 12.
struct WsBillboard {
	Vec3 pos, v614;
	float f610 = 0.f;
	uint32_t corona = 0; // +0x674, SetCorona(!spriteOnly)
	uint32_t blend = 0; // +0x678, the material blend (1, 2, 4, 5)
	std::string texture;
	float f[17] = {}; // +0x68c..0x6c4, 0x6f8, 0x6fc
};

enum WsType : uint32_t {
	kWsWorldMesh = 1, kWsLight = 2, kWsParticle = 3, kWsModel = 4,
	kWsDecal = 6, kWsBillboard = 8, kWsTrail = 10, kWsSound = 11,
};

// One record of World::SaveEntities: a header, the ^S base, the class body,
// and the in-world flag SaveEntities writes after it. Only the member of `type`
// is meaningful. The header is World::CreateEntity's arguments.
struct WsEntity {
	uint32_t type = 0;
	std::string resource; // model, pack path, texture or effect: CreateEntity's 2nd
	std::string name; // the script's "Name:Tag", a pack's mesh: its 3rd
	uint32_t headerValue = 0; // the scale, as float bits
	uint8_t headerFlag = 0; // Entity+0x18 & 0x80: CreateEntity's last, a pack mesh centred
	uint16_t tag = 0;
	WsEntityBase base;
	WsModel model;
	WsWorldMesh mesh;
	WsLight light;
	WsParticle particle;
	WsSound sound;
	WsTrail trail;
	WsDecal decal;
	WsBillboard billboard;
	uint8_t inWorld = 0;
};

// ^p, FUN_10037020: one breakable pane. Its shards are entity records written by
// the same virtual World::SaveEntities calls, without the in-world byte.
struct WsGlass {
	std::string name;
	uint8_t b3 = 0, bd = 0;
	uint32_t u1 = 0;
	uint8_t b8 = 0;
	Vec3 v24; // 0xfeedface until the pane breaks
	uint32_t u2 = 0;
	Vec3 v34;
	struct Point {
		Vec3 p;
		uint32_t v = 0;
	};
	std::vector<Point> points;
	struct Piece {
		uint8_t present = 0;
		WsEntity entity;
		uint8_t bc0 = 0;
		int32_t index10 = -1, index5 = -1;
		uint32_t value = 0; // written only when index5 != -1
	};
	std::vector<Piece> pieces; // one more than points
};

// ^P, FUN_101b30c0.
struct WsPhysicsWorld {
	struct Layer {
		uint32_t index = 0;
		uint8_t flag = 0;
		float v[7] = {};
	};
	std::vector<Layer> layers;
	struct Group {
		uint8_t a = 0;
		uint32_t b = 0;
		uint8_t c = 0, d = 0;
		float e = 0.f;
	};
	std::vector<Group> groups;
	struct Body {
		uint8_t present = 0;
		float a[3] = {}, b[3] = {};
		float c = 0.f;
		WsPhysicsObject po;
		uint32_t entity18 = 0, entityB0 = 0; // the owning entity's +0x18 and +0xb0
	};
	std::vector<Body> bodies;
	struct Pair {
		uint8_t present = 0;
		uint32_t a = 0;
		uint8_t b = 0, c = 0;
		uint32_t d = 0;
	};
	std::vector<Pair> pairs;
};

std::string WsText(const std::string& s); // without the trailing NUL
const char* WsTypeName(uint32_t type);

// A music stream in the audio chunk (MilesEngine::SaveAudio 0x101f22a0, section
// 0x5f00): what LoadAudio reopens at its saved byte. The scripts delete their
// streams on a load and restart one only on a change, so the music lives here.
struct WsStream {
	uint8_t present = 0;
	std::string file; // "../Data/Music/<name>.mp3", NUL included
	float volume = 1.f; // +0x14
	uint32_t rate = 44100; // +0x18, 44100 in every save
	uint32_t u1c = 0; // +0x1c
	uint32_t loopCount = 0; // +0x20, StreamingSound_SetLoopCount; 0 plays forever
	uint32_t position = 0; // AIL_stream_position, a byte of the file
	// In the pause set header[20] names (PauseCurrentlyPlayingSounds), which
	// SaveGame_ResumeSounds lifts after the load: playing, not paused, at the save.
	bool resumes = false;
};
// The streams in an audio chunk's body; false when it does not parse that far.
bool WsAudioStreams(const std::vector<uint8_t>& body, std::vector<WsStream>& out);
// A body holding only these streams, with the header every original save has and
// no cached samples, groups, or 2D and 3D sounds.
std::vector<uint8_t> WsAudioBody(const std::vector<WsStream>& streams);

struct WorldSave {
	uint32_t version = 3;
	std::vector<WsGlass> glass;
	WsChunk audio; // "AUDIOv01", MilesEngine::SaveAudio
	std::vector<WsPhysicsWorld> worlds;
	uint32_t physicsC = 0, physics14 = 0;
	struct Path {
		uint8_t active = 0;
		Vec3 from, to; // ^D, WaypointGPath2::Save
	};
	std::vector<Path> paths;
	uint8_t e6dc = 0, e6dd = 0, e6bb = 0;
	std::vector<WsEntity> entities;
	std::vector<uint8_t> portals, antiportals, zones;

	// What the file leaves to the model: its mesh count, and its .hke's
	// constraint and action counts. A negative or false answer fails the read.
	struct Resolver {
		std::function<int(const std::string& model)> meshCount;
		std::function<bool(const std::string& model, int& constraints, int& actions)> ragdoll;
	};

	// Filled by Read, for reports.
	std::string error;
	size_t errorAt = 0;
	size_t physicsAt = 0, pathsAt = 0, entitiesAt = 0, portalsAt = 0;
	std::vector<size_t> entityAt;

	static bool Read(const std::vector<uint8_t>& data, WorldSave& out, const Resolver& models);
	bool Write(std::vector<uint8_t>& out) const;
};

} // namespace painful
