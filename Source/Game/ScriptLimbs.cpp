// ScriptEngine: per-limb hit traces against the posed skeleton.

#include "ScriptEngineInternal.h"

namespace painful {

// ---------------------------------------------------------------- limb traces
//
// A monster has two shapes and they answer two different questions.
//
// The MOVEMENT shape - three stacked spheres sized off the rig's ROOOT joint -
// is what you walk into, shove and cannot stand inside. The SHOOTING shape is
// the set of limb boxes the .rde names, one per bone, derived from the
// vertices that bone drives. Testing a shot against the movement shape makes a
// headshot and a shot at the ankle the same event, which is what this replaces.
//
// The test runs in BONE SPACE. The boxes are already held there - that is the
// whole point of BuildLimbBounds - so transforming the ray into a box's own
// frame turns an oriented-box intersection into a plain slab test, and no box
// ever has to be rebuilt or re-cornered for a pose.

namespace {

// Ray vs axis-aligned box, both in the same space. `dir` spans the WHOLE
// segment, so t comes back in 0..1 and needs no length anywhere.
//
// `axis` names the face the segment entered through, or stays -1 when the
// segment STARTS INSIDE the box - a point-blank shot, which has no entry face
// to take a normal from and which the caller has to answer for separately.
bool SlabTest(const float o[3], const float dir[3], const float lo[3], const float hi[3],
              float& tHit, int& axis, float& sign) {
    float tmin = 0.f, tmax = 1.f;
    axis = -1;
    sign = -1.f;
    for (int c = 0; c < 3; ++c) {
        if (std::fabs(dir[c]) < 1e-9f) {
            // Parallel to this pair of planes: either between them for the
            // whole segment or outside them for all of it.
            if (o[c] < lo[c] || o[c] > hi[c]) return false;
            continue;
        }
        const float inv = 1.f / dir[c];
        float t1 = (lo[c] - o[c]) * inv;
        float t2 = (hi[c] - o[c]) * inv;
        float faceSign = -1.f;                  // entered through the low face
        if (t1 > t2) { std::swap(t1, t2); faceSign = 1.f; }
        if (t1 > tmin) { tmin = t1; axis = c; sign = faceSign; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    tHit = tmin;
    return true;
}

// A direction through an affine matrix: the 3x3 alone, so the translation does
// not apply. TransformPoint would move the ray's direction by the entity's
// position, which points every shot at the world origin.
void TransformDir(const Mat4& m, const float v[3], float out[3]) {
    out[0] = v[0] * m.m[0] + v[1] * m.m[4] + v[2] * m.m[8];
    out[1] = v[0] * m.m[1] + v[1] * m.m[5] + v[2] * m.m[9];
    out[2] = v[0] * m.m[2] + v[1] * m.m[6] + v[2] * m.m[10];
}

// Does the segment from + t*span (t in 0..1) pass within `radius` of `p`? The
// broad phase, so the matrix work only happens for actors near the shot.
bool SegmentNearPoint(const float from[3], const float span[3], const float p[3],
                      float radius) {
    float d[3];
    for (int c = 0; c < 3; ++c) d[c] = p[c] - from[c];
    const float len2 = span[0] * span[0] + span[1] * span[1] + span[2] * span[2];
    float t = (len2 > 1e-12f) ? (d[0] * span[0] + d[1] * span[1] + d[2] * span[2]) / len2 : 0.f;
    t = std::max(0.f, std::min(1.f, t));
    float away = 0.f;
    for (int c = 0; c < 3; ++c) {
        const float k = d[c] - t * span[c];
        away += k * k;
    }
    return away <= radius * radius;
}

} // namespace

bool ScriptEngine::TraceLimbs(const float from[3], const float to[3], float maxDistance,
                              LimbHit& out) {
    float span[3];
    for (int c = 0; c < 3; ++c) span[c] = to[c] - from[c];
    const float length =
        std::sqrt(span[0] * span[0] + span[1] * span[1] + span[2] * span[2]);
    if (length < 1e-6f) return false;

    // NOTHING BEYOND WHAT THE WORLD TRACE ALREADY FOUND. A shot that stops at
    // a wall must not reach through it to the monster standing behind, so the
    // search is clamped to the distance already established rather than run
    // over the whole segment and reconciled afterwards.
    float bestT = (maxDistance >= 0.f && maxDistance < length) ? maxDistance / length : 1.f;
    bool got = false;

    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (e.type != kModel || !e.visible) continue;
        // WHAT IS SHOOTABLE BY LIMB IS NOT THE SAME SET AS WHAT HAS A BODY.
        //
        // The original keeps them at different offsets on the entity - the
        // PhysicsObject at +0xac, the Ragdoll at +0x7b8 - each with its own
        // EnableLineTraceCollision, and AddRagdollToIntersectionSolver
        // switches only the second. So a thing can be shootable through its
        // ragdoll while having no physics object at all, and Cathedral's 32
        // bats are exactly that: bat.rde exists, PO_Exist is false, and
        // gating on the monster flag would leave a swarm of enemies that
        // shots pass straight through.
        //
        // A monster, then, or anything with no body of its own - where limbs
        // can only ADD, because there is nothing for them to shadow. A PROP
        // with a working script body is deliberately left on it: that path
        // answers today, and routing it through limbs would change what `he`
        // means for something whose PO_Hit and IsFixedMesh handling reads it
        // as a body slot. Breakable props are their own question.
        if (!e.isMonster && e.physicsBody >= 0) continue;
        // The RAGDOLL's trace switch, not the body's. The scripts bracket a
        // shot with AddRagdollToIntersectionSolver / Remove... precisely to
        // say which limbs are shootable this instant, and that is a different
        // question from whether the walking shape is in the traces.
        if (!e.ragdollInSolver) continue;

        const std::vector<LimbBounds>* limbs = Hitboxes(e.source);
        if (!limbs || limbs->empty()) continue;

        // Broad phase off the model's own bounds. An animated pose is not
        // guaranteed to stay inside its bind-pose bounds - an arm swings wide
        // of them - so the radius is deliberately generous. It only has to
        // save the matrix work for actors nowhere near the shot; being loose
        // costs a slab test, being tight would lose a hit.
        const SkeletonCache::Entry* skel = skeletons_.Get(e.source);
        if (!skel) continue;
        float reach = 0.f;
        for (int c = 0; c < 3; ++c)
            reach = std::max(reach,
                             std::max(std::fabs(skel->lo[c]), std::fabs(skel->hi[c])));
        if (!SegmentNearPoint(from, span, e.pos, reach * e.scale * 1.5f + 0.5f)) continue;

        const std::vector<Mat4>* bones = PosedBones(e);
        if (!bones) continue;

        // The entity's own model -> world, built EXACTLY as JointToWorld
        // builds it. If these two ever disagree, the box a shot tests is not
        // the box F2 draws, and no amount of looking at the picture would
        // show it.
        float rot[9];
        EngineQuatToRot9(e.rotWXYZ, rot);
        Mat4 world;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) world.m[r * 4 + c] = e.scale * rot[r * 3 + c];
            world.m[r * 4 + 3] = 0.f;
        }
        for (int c = 0; c < 3; ++c) world.m[12 + c] = e.pos[c];
        world.m[15] = 1.f;

        for (const LimbBounds& limb : *limbs) {
            if (!limb.valid()) continue;
            if (limb.bone < 0 || size_t(limb.bone) >= bones->size()) continue;
            // A joint EnableJoint has switched off is out of the ragdoll, so
            // there is no body there to hit.
            if (!e.disabledJoints.empty() &&
                std::find(e.disabledJoints.begin(), e.disabledJoints.end(), limb.bone) !=
                    e.disabledJoints.end())
                continue;
            // ...and one PHYSICS.RemoveHavokBodyFromIS has taken out, which is
            // how the stake looks BEHIND the weapon it just hit. Only consulted
            // when something is actually suppressed, which is almost never.
            if (!suppressedLimbs_.empty()) {
                const auto key = limbHandleIndex_.find((long long(kv.first) << 32) |
                                                       (unsigned int)(limb.bone));
                if (key != limbHandleIndex_.end() &&
                    std::find(suppressedLimbs_.begin(), suppressedLimbs_.end(),
                              kLimbHandleBase + key->second) != suppressedLimbs_.end())
                    continue;
            }

            const Mat4 toWorld = Mat4::Mul((*bones)[size_t(limb.bone)], world);
            const Mat4 toLimb = Mat4::InvertAffine(toWorld);

            float o[3], dir[3];
            toLimb.TransformPoint(from[0], from[1], from[2], o);
            TransformDir(toLimb, span, dir);

            float t = 0.f;
            int axis = -1;
            float sign = -1.f;
            if (!SlabTest(o, dir, limb.min, limb.max, t, axis, sign)) continue;
            if (t >= bestT) continue;

            bestT = t;
            got = true;
            out.entity = kv.first;
            out.joint = limb.bone;
            out.distance = t * length;
            for (int c = 0; c < 3; ++c) out.point[c] = from[c] + t * span[c];

            if (axis < 0) {
                // The segment started inside this limb - a muzzle pressed
                // against a chest. There is no entry face, so face back down
                // the ray, which is the same answer PhysicsWorld::RayCast
                // gives for a degenerate contact. Anything else here is a NaN
                // waiting to spread through every decal and effect the hit
                // spawns.
                for (int c = 0; c < 3; ++c) out.normal[c] = -span[c] / length;
            } else {
                float n[3] = {0, 0, 0};
                n[axis] = sign;
                TransformDir(toWorld, n, out.normal);
                const float n2 = out.normal[0] * out.normal[0] +
                                 out.normal[1] * out.normal[1] +
                                 out.normal[2] * out.normal[2];
                if (n2 > 1e-12f) {
                    const float inv = 1.f / std::sqrt(n2);
                    for (int c = 0; c < 3; ++c) out.normal[c] *= inv;
                } else {
                    for (int c = 0; c < 3; ++c) out.normal[c] = -span[c] / length;
                }
            }
        }
    }
    return got;
}

// The handle for one limb of one actor, stable for as long as the process
// runs. Stability matters: the scripts do not treat `he` as a value that
// expires - CActor stores it, passes it into OnDamage, and a monster like the
// Tank keeps what it learned from it (_hitGasTank) until it dies.
int ScriptEngine::LimbHandle(int entity, int joint) {
    const long long key = (long long(entity) << 32) | (unsigned int)(joint);
    const auto it = limbHandleIndex_.find(key);
    if (it != limbHandleIndex_.end()) return kLimbHandleBase + it->second;
    const int index = int(limbHandles_.size());
    limbHandles_.push_back({entity, joint});
    limbHandleIndex_[key] = index;
    return kLimbHandleBase + index;
}

bool ScriptEngine::LimbFromHandle(int handle, int& entity, int& joint) const {
    if (handle < kLimbHandleBase) return false;
    const size_t index = size_t(handle - kLimbHandleBase);
    if (index >= limbHandles_.size()) return false;
    entity = limbHandles_[index].first;
    joint = limbHandles_[index].second;
    return true;
}

void ScriptEngine::CollectHitboxLines(const float around[3], float radius,
                                      std::vector<DebugLine>& out) {
    // The twelve edges of a box, as pairs of corner indices.
    static const int kEdges[12][2] = {{0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4},
                                      {0,4},{1,5},{2,6},{3,7}};
    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (e.type != kModel || !e.visible) continue;

        float d[3];
        for (int c = 0; c < 3; ++c) d[c] = e.pos[c] - around[c];
        if (d[0]*d[0] + d[1]*d[1] + d[2]*d[2] > radius * radius) continue;

        const std::vector<LimbBounds>* limbs = Hitboxes(e.source);
        if (!limbs) continue;

        for (const LimbBounds& limb : *limbs) {
            // Each corner goes bone-local -> world through the POSED bone, so
            // the box follows the animation without being rebuilt.
            float corner[8][3];
            bool posed = true;
            for (int i = 0; i < 8 && posed; ++i) {
                const float local[3] = {(i & 1) ? limb.max[0] : limb.min[0],
                                        (i & 2) ? limb.max[1] : limb.min[1],
                                        (i & 4) ? limb.max[2] : limb.min[2]};
                posed = JointToWorld(e, limb.bone, local, corner[i]);
            }
            if (!posed) continue;

            for (const auto& edge : kEdges) {
                DebugLine line;
                for (int c = 0; c < 3; ++c) {
                    line.a[c] = corner[edge[0]][c];
                    line.b[c] = corner[edge[1]][c];
                }
                line.abgr = 0xff00a5ffu;      // orange: neither collision nor geometry
                out.push_back(line);
            }
        }
    }
}

// Returns the child handle, or 0 for "no such child" - the value the scripts
// actually compare against.
int ScriptEngine::L_ENTITY_GetChildByName(lua_State* L) {
    ScriptEngine* self = From(L);
    const Entity* parent = self->Find(HandleArg(L, 1));
    const char* name = luaL_optstring(L, 2, "");
    int found = 0;
    if (parent && name && *name) {
        for (int handle : parent->children) {
            const Entity* c = self->Find(handle);
            if (c && (c->soundName == name || c->name == name)) {
                found = handle;
                break;
            }
        }
    }
    lua_pushnumber(L, found);
    return 1;
}

int ScriptEngine::L_ENTITY_KillAllChildrenByName(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* parent = self->Find(HandleArg(L, 1));
    const char* name = luaL_optstring(L, 2, "");
    if (!parent || !name || !*name) return 0;
    // Collected first, then released. ReleaseEntity unlinks the child from
    // THIS list itself - that is what its back-link cleanup does - so erasing
    // here as well walked off the end of a vector that had already shrunk.
    std::vector<int> doomed;
    for (int handle : parent->children) {
        const Entity* c = self->Find(handle);
        if (c && (c->soundName == name || c->name == name)) doomed.push_back(handle);
    }
    for (int handle : doomed) self->ReleaseEntity(handle);
    // Stake:Tick asks whether anything went - `if KillAllChildrenByName(se,
    // "stakeflame") then` is how it decides the stake was burning and owes a
    // puff of smoke. Returning nothing made that test read false every time.
    lua_pushboolean(L, doomed.empty() ? 0 : 1);
    return 1;
}

int ScriptEngine::L_ENTITY_KillAllChildren(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* parent = self->Find(HandleArg(L, 1));
    if (!parent) return 0;
    const std::vector<int> kids = parent->children;
    parent->children.clear();
    // Only the ones that asked to die with their parent, which is what
    // Entity::KillAllChildren checks (child+0x11a at 0x1d2bc0).
    for (int handle : kids) {
        Entity* kid = self->Find(handle);
        if (kid == nullptr) continue;
        kid->parent = 0;
        if (kid->dieWithParent) self->ReleaseEntity(handle);
    }
    return 0;
}

// ENTITY.UnregisterAllChildren(parent, [type])
//
// Forgets the children without destroying them, which is what the scripts want
// when an owner dies but its effects should finish on their own.
//
// The second argument is an ETypes FILTER, and dropping it took every child
// rather than the named kind. Stake:Tick unregisters its TRAIL on impact and
// then, on the very next line, kills its flight loop by name:
//
//     ENTITY.UnregisterAllChildren(se, ETypes.Trail)
//     ENTITY.KillAllChildrenByName(se, "weapons/stake/stake_onfly-loop")
//
// With the filter ignored the first call emptied the list, so the second found
// nothing to kill and the stake screamed from the wall for the rest of the
// level. Harmless while SND.Play was a stub; audible the moment it was not.
int ScriptEngine::L_ENTITY_UnregisterAllChildren(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* parent = self->Find(HandleArg(L, 1));
    if (!parent) return 0;
    const int type = lua_isnumber(L, 2) ? int(lua_tonumber(L, 2)) : -1;
    for (size_t i = parent->children.size(); i-- > 0;) {
        Entity* c = self->Find(parent->children[i]);
        if (type >= 0 && (!c || c->type != type)) continue;
        if (c) c->parent = 0;               // forgotten, not killed
        parent->children.erase(parent->children.begin() + long(i));
    }
    return 0;
}



// ENTITY.PO_EnableGravity(entity, on)
//
// The original does not make a projectile a different KIND of object - it
// turns that body's gravity off. PhysicsObject::EnableGravity (0x1018c4e0)
// sets the body's own gravity to the world vector when on and to zero when
// off, which is Jolt's gravity factor.
//
// This is what a stake, a rocket and a shuriken all rely on: the weapon
// scripts set a velocity and then call this with false, and the thing flies
// straight. Without it they arc to the floor and behave like dropped props,
// which is exactly how they looked.
int ScriptEngine::L_PO_EnableGravity(lua_State* L) {
    ScriptEngine* self = From(L);
    Entity* e = self->Find(HandleArg(L, 1));
    if (!e) return 0;
    const bool on = lua_isnoneornil(L, 2) ? true : (lua_toboolean(L, 2) != 0);
    // A projectile never reaches the solver, so setting only the body's
    // gravity factor was a value nothing read. Stake:Tick turns gravity ON
    // 0.2s after the shot and that has to reach TickProjectiles, or the stake
    // flies dead flat until it times out.
    e->gravityOn = on;
    if (self->physics_ && e->physicsBody >= 0)
        self->physics_->SetScriptBodyGravityFactor(e->physicsBody, on ? 1.f : 0.f);
    return 0;
}


// Projectiles are MOVED, not simulated - the same division the engine already
// makes for monsters. Stake:OnCreateEntity asks PO_Create for
// ECollisionGroups.Noncolliding, sets a velocity, turns gravity off and then
// looks for its own hits with Stake:Trace. Nothing about that wants a solver,
// and handing it to one is what made every shot behave differently.
//
// Constant speed along a straight line is exactly why the original's shots are
// identical every time.
// Straight is the DEFAULT, not the whole story. A stake leaves the barrel at
// 70 m/s with gravity off and flies flat; 0.2s later Stake:Tick turns gravity
// back on and gives it a spin, and it noses over into the floor. Driven still
// means driven - the arc is one accumulator here, not a solver - but "moved
// along a constant velocity" was only ever the first two tenths of a second.
void ScriptEngine::TickProjectiles(float dt) {
    if (dt <= 0.f) return;
    const float gravity = physics_ ? physics_->settings().gravity : 2.f * 9.81f;
    for (auto& kv : entities_) {
        Entity& e = kv.second;
        if (!e.isProjectile) continue;
        // A stake that has struck something turns its physics object off and
        // expects to stay exactly where it was put. Carrying it on regardless
        // is what sent it through the floor and out of the level after a hit
        // it had already registered.
        if (!e.poEnabled) continue;

        if (e.gravityOn) e.velocity[1] -= gravity * dt;

        const float speedSq = e.velocity[0] * e.velocity[0] +
                              e.velocity[1] * e.velocity[1] +
                              e.velocity[2] * e.velocity[2];
        const float spinSq = e.angVel[0] * e.angVel[0] +
                             e.angVel[1] * e.angVel[1] +
                             e.angVel[2] * e.angVel[2];
        if (speedSq <= 1e-6f && spinSq <= 1e-12f) continue;

        for (int c = 0; c < 3; ++c) e.pos[c] += e.velocity[c] * dt;

        // The tumble. The axis is in WORLD space, so the step composes on the
        // side that applies it after the current orientation - which under the
        // engine's q^-1*v*q convention is the right-hand side, the opposite of
        // the textbook order. Renormalised because this integrates every frame
        // for the whole flight and a drifting quaternion shears the model.
        if (spinSq > 1e-12f) {
            const float w = std::sqrt(spinSq);
            const float half = 0.5f * w * dt;
            const float s = std::sin(half) / w;
            const float step[4] = {std::cos(half), e.angVel[0] * s,
                                   e.angVel[1] * s, e.angVel[2] * s};
            float out[4];
            EngineQuatMul(e.rotWXYZ, step, out);
            const float len = std::sqrt(out[0]*out[0] + out[1]*out[1] +
                                        out[2]*out[2] + out[3]*out[3]);
            if (len > 1e-8f)
                for (int c = 0; c < 4; ++c) e.rotWXYZ[c] = out[c] / len;
        }

        // The body follows so the model draws in the right place and any query
        // against it answers truthfully; it is a carrier, not a simulation.
        if (physics_ && e.physicsBody >= 0)
            physics_->SetScriptBodyPose(e.physicsBody, e.pos, e.rotWXYZ);
        SyncPose(e);
    }
}

// R3D.DrawSprite(x, y, z, size, rot, colour, texture)
//
// every frame, picking a random texture ("Items/1".."Items/3") and a random
// angle each time. It is also how the item glows are drawn.
//
// The colour arrives packed the way R3D.RGBA builds it, and the rotation is in
// radians about the view axis.
int ScriptEngine::L_R3D_DrawSprite(lua_State* L) {
    ScriptEngine* self = From(L);
    if (!self->billboards_ || !self->hudTextures_) return 0;
    const float pos[3] = {float(luaL_optnumber(L, 1, 0)), float(luaL_optnumber(L, 2, 0)),
                          float(luaL_optnumber(L, 3, 0))};
    const float size = float(luaL_optnumber(L, 4, 1.0));
    const float rot = float(luaL_optnumber(L, 5, 0.0));
    const uint32_t argb = uint32_t(int64_t(luaL_optnumber(L, 6, -1)));
    const char* texture = luaL_optstring(L, 7, "");
    if (!texture || !*texture || size <= 0.f) return 0;

    const uint32_t a = (argb >> 24) & 0xFF, r = (argb >> 16) & 0xFF;
    const uint32_t g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    const uint32_t abgr = (a << 24) | (b << 16) | (g << 8) | r;

    self->billboards_->DrawImmediate(pos, size, rot, abgr,
                                     self->hudTextures_->Get(texture, ""));
    return 0;
}


}  // namespace painful
