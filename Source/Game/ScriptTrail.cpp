// ScriptEngine: trails - ENTITY.AttachTrailToBones and the ribbons it starts.
// The entity is an ordinary ETypes.Trail made by ENTITY.Create; the ribbon is
// World/Trails' TrailRibbon. Docs/Reference/Trails.md.

#include "ScriptEngineInternal.h"
#include <algorithm>
#include <string>

namespace painful {

struct TrailNatives : ScriptNativesBase {
	static int L_ENTITY_AttachTrailToBones(lua_State* L);
};

// ENTITY.AttachTrailToBones(owner, trail [, joint names...]) - Trail::
// AttachToBones (0x101EA930): the trail becomes the owner's child, following
// it, and its ribbon is strung between the named joints - none for an owner
// that is not a model, which is every projectile.
int TrailNatives::L_ENTITY_AttachTrailToBones(lua_State* L) {
	ScriptEngine* self = From(L);
	const int ownerHandle = HandleArg(L, 1);
	const int trailHandle = HandleArg(L, 2);
	Entity* owner = self->Find(ownerHandle);
	Entity* trail = self->Find(trailHandle);
	if (!owner || !trail || trail->type != kTrail || ownerHandle == trailHandle) return 0;

	if (std::find(owner->children.begin(), owner->children.end(), trailHandle) ==
			owner->children.end())
		owner->children.push_back(trailHandle);
	trail->parent = ownerHandle;
	// It outlives the owner and fades out: CutTail deletes an empty trail once
	// its owner is gone, so it must not be released with it.
	trail->dieWithParent = false;
	trail->parentOffset = trail->pos;
	trail->parentRot = trail->rot;
	trail->parentRotBound = true;
	trail->localScale = trail->scale;
	trail->localPose = true;
	trail->parentBound = true;
	self->PlaceAttached(*trail);

	std::vector<int> joints;
	if (owner->type == kModel)
		for (int a = 3; a <= lua_gettop(L); ++a)
			if (lua_isstring(L, a)) joints.push_back(self->JointIndexByName(*owner, lua_tostring(L, a)));
	auto ribbon = std::make_unique<TrailRibbon>(self->trailLib_.Get(trail->source));
	ribbon->Attach(int(joints.size()));
	self->trails_[trailHandle] = {std::move(ribbon), std::move(joints)};
	return 0;
}

// Trail::Tick for every ribbon, after the attached entities are placed:
// UpdateTransform's points from the owner (the trail's own placed position,
// one joint, or every joint), then the clock, AddNode and CutTail. A ribbon
// whose owner is gone stops sampling, fades, and takes its entity with it once
// it is empty.
void ScriptEngine::TickTrails(float dt) {
	std::vector<int> dead;
	for (auto& kv : trails_) {
		Entity* trail = Find(kv.first);
		if (!trail) {
			dead.push_back(kv.first);
			continue;
		}
		TrailState& t = kv.second;
		Entity* owner = trail->parent ? Find(trail->parent) : nullptr;
		Vec3 points[16];
		int count = 0;
		if (owner && trail->parentBound) {
			const int rails = t.ribbon->rails();
			if (rails == 0) {
				points[count++] = trail->pos;
			} else if (rails == 1) {
				Vec3 p;
				if (JointToWorld(*owner, t.joints[0], Vec3(), p))
					points[count++] = p + owner->rot.Rotate(trail->parentOffset * owner->scale);
			} else {
				for (int j = 0; j < rails && j < 16; ++j) {
					Vec3 p;
					if (!JointToWorld(*owner, t.joints[size_t(j)], Vec3(), p)) break;
					points[count++] = p;
				}
				if (count < rails) count = 0;
			}
		}
		// The trail's own scale (Trail+0x680, SetScale): Trail::UpdateTransform
		// replaces the entity one, so the owner's scale never reaches it.
		t.ribbon->SetScale(trail->localPose ? trail->localScale : trail->scale);
		t.ribbon->Tick(dt, points, count);
		if (!owner && t.ribbon->Empty()) dead.push_back(kv.first);
	}
	for (int handle : dead) {
		trails_.erase(handle);
		ReleaseEntity(handle);
	}
}

std::vector<const TrailRibbon*> ScriptEngine::TrailRibbons() const {
	std::vector<const TrailRibbon*> out;
	out.reserve(trails_.size());
	for (const auto& kv : trails_)
		if (kv.second.ribbon) out.push_back(kv.second.ribbon.get());
	return out;
}

void BindTrail(ScriptEngine& engine, LuaHost& host) {
	const ScriptNative natives[] = {
		{"ENTITY", "AttachTrailToBones", TrailNatives::L_ENTITY_AttachTrailToBones},
	};
	RegisterFamily(engine, host, natives);
}

} // namespace painful
