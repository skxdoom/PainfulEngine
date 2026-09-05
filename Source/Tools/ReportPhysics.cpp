// The Jolt world, and what it does to a body dropped into it.
#include "Commands.h"

int RagdollDropCmd(const char* levelDir, const char* dataRoot, const char* modelName) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) {
        LogInfo("cannot load level: %s", level.error().c_str());
        return 2;
    }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");
    PhysicsWorld physics;
    physics.Load(level, templates, dataRoot);
    if (!physics.loaded()) {
        LogInfo("level has no collidable geometry");
        return 2;
    }

    Hke def;
    const std::string hkePath = std::string(dataRoot) + "/Models/" + modelName + ".hke";
    if (!Hke::Load(hkePath, def)) {
        LogInfo("%s: %s", hkePath.c_str(), def.error.c_str());
        return 2;
    }

    // Models are built at Scale * 0.1 and the .hke is authored in that same
    // ten-times space, so this is the conversion the renderer already applies.
    const float scale = 0.1f;
    const int slot = physics.CreateRagdoll(modelName, def, scale);
    if (slot < 0) {
        LogInfo("could not create a ragdoll for %s", modelName);
        return 2;
    }
    const std::vector<std::string>& bones = physics.RagdollBones(slot);
    const size_t n = bones.size();

    // Somewhere with floor under it. A spawn point is the level's own answer
    // to that question; failing one, the first placed entity will do.
    float at[3] = {0, 0, 0};
    bool found = false;
    for (const Entity& e : level.entities())
        if (e.type == "CSpawnPoint") {
            for (int c = 0; c < 3; ++c) at[c] = e.pos[c];
            found = true;
            break;
        }
    if (!found && !level.entities().empty())
        for (int c = 0; c < 3; ++c) at[c] = level.entities().front().pos[c];
    at[1] += 3.f;

    // SEED IT AT ITS OWN REST POSE, ROTATIONS AND ALL. The constraints were
    // built from the authored transforms, so handing the solver the authored
    // translations with identity rotations violates every one of them at t=0
    // and the ragdoll tears itself apart before gravity gets a say.
    //
    // Our Mat4 is row-vector with the basis in its ROWS, so row i is the image
    // of basis vector i: m[i*4+j] = R(j,i) for the column-vector R that
    // Rodrigues gives.
    std::vector<float> pose(n * 16, 0.f);
    for (size_t i = 0; i < n; ++i) {
        const HkeBody* b = def.Body(bones[i]);
        float* m = &pose[i * 16];
        m[0] = m[5] = m[10] = m[15] = 1.f;
        if (!b) {
            for (int c = 0; c < 3; ++c) m[12 + c] = at[c];
            continue;
        }
        float k[3] = {b->rotAxis[0], b->rotAxis[1], b->rotAxis[2]};
        const float len = std::sqrt(k[0]*k[0] + k[1]*k[1] + k[2]*k[2]);
        if (len > 1e-6f && std::fabs(b->rotAngle) > 1e-9f) {
            for (int c = 0; c < 3; ++c) k[c] /= len;
            const float s = std::sin(b->rotAngle), co = std::cos(b->rotAngle), t = 1.f - co;
            const float R[3][3] = {
                {co + k[0]*k[0]*t,        k[0]*k[1]*t - k[2]*s,  k[0]*k[2]*t + k[1]*s},
                {k[1]*k[0]*t + k[2]*s,    co + k[1]*k[1]*t,      k[1]*k[2]*t - k[0]*s},
                {k[2]*k[0]*t - k[1]*s,    k[2]*k[1]*t + k[0]*s,  co + k[2]*k[2]*t}};
            for (int i2 = 0; i2 < 3; ++i2)
                for (int j = 0; j < 3; ++j) m[i2 * 4 + j] = R[j][i2];
        }
        for (int c = 0; c < 3; ++c) m[12 + c] = b->translation[c] * scale + at[c];
    }
    physics.SetRagdollPose(slot, pose.data(), /*kinematic=*/false);

    // A BODY THAT HELD TOGETHER KEEPS ITS OWN SIZE, WHATEVER WAY UP IT LANDS.
    // Per-axis extents cannot say that: a figure that starts standing and ends
    // lying down has swapped its height for its depth without anything having
    // gone wrong. So compare the widest distance between any two parts, which
    // is orientation-free.
    //
    // And measure only the parts the constraints actually hold. The weapons
    // are detached bodies by design - they fall out of the ragdoll and roll
    // away, which is correct and would otherwise read as the ragdoll flying
    // apart.
    std::vector<size_t> held;
    for (size_t i = 0; i < n; ++i) {
        bool linked = false;
        for (const HkeConstraint& c : def.constraints)
            if (c.bodyA == bones[i] || c.bodyB == bones[i]) { linked = true; break; }
        if (linked) held.push_back(i);
    }
    auto widest = [&](const std::vector<float>& p) {
        float worst = 0.f;
        for (size_t a = 0; a < held.size(); ++a)
            for (size_t b = a + 1; b < held.size(); ++b) {
                float d2 = 0.f;
                for (int c = 0; c < 3; ++c) {
                    const float k = p[held[a] * 16 + 12 + c] - p[held[b] * 16 + 12 + c];
                    d2 += k * k;
                }
                worst = std::max(worst, d2);
            }
        return std::sqrt(worst);
    };

    // How big is it, and how far apart are the parts, at t = 0?
    auto measure = [&](std::vector<float>& p, float lo[3], float hi[3], float centre[3]) {
        for (int c = 0; c < 3; ++c) { lo[c] = 1e30f; hi[c] = -1e30f; centre[c] = 0.f; }
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) {
                const float v = p[i * 16 + 12 + c];
                lo[c] = std::min(lo[c], v);
                hi[c] = std::max(hi[c], v);
                centre[c] += v / float(n);
            }
    };

    std::vector<float> readback(n * 16, 0.f);
    physics.GetRagdollPose(slot, readback.data());
    float lo0[3], hi0[3], c0[3];
    measure(readback, lo0, hi0, c0);

    const float span0 = widest(readback);
    LogInfo("%s: %zu parts (%zu held by constraints, %zu detached), dropped at %.1f %.1f %.1f",
            modelName, n, held.size(), n - held.size(), at[0], at[1], at[2]);
    LogInfo("  authored: extent %.2f x %.2f x %.2f, widest part gap %.2f",
            hi0[0] - lo0[0], hi0[1] - lo0[1], hi0[2] - lo0[2], span0);

    for (int second = 1; second <= 5; ++second) {
        for (int step = 0; step < 60; ++step) physics.Update(1.f / 60.f);
        physics.GetRagdollPose(slot, readback.data());
        float lo[3], hi[3], c[3];
        measure(readback, lo, hi, c);
        bool finite = true;
        for (size_t i = 0; i < n * 16 && finite; ++i)
            if (!(readback[i] == readback[i])) finite = false;   // false for NaN
        LogInfo("  t=%ds  widest gap %.2f (%.2fx)   centre %.2f %.2f %.2f   %s",
                second, widest(readback), span0 > 1e-3f ? widest(readback) / span0 : 0.f,
                c[0], c[1], c[2], finite ? "" : "  *** NaN ***");
    }

    const float span = widest(readback);
    float lo[3], hi[3], c[3];
    measure(readback, lo, hi, c);
    LogInfo("  held together: %.2f -> %.2f  (%.2fx; 1.0-1.5x is a body, 10x is a firework)",
            span0, span, span0 > 1e-3f ? span / span0 : 0.f);
    LogInfo("  settled extent: %.2f x %.2f x %.2f, fell %.2f units",
            hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], c0[1] - c[1]);
    physics.RemoveRagdoll(slot);
    return 0;
}

int PhysicsCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) {
        LogInfo("cannot load level: %s", level.error().c_str());
        return 2;
    }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");

    PhysicsWorld physics;
    physics.Load(level, templates, dataRoot);
    if (!physics.loaded()) {
        LogInfo("level has no collidable geometry");
        return 2;
    }

    const Tweaks& t = physics.tweaks();
    LogInfo("Tweak.lua      : %s, %zu values", t.loaded() ? "loaded" : "MISSING", t.size());
    LogInfo("gravity        : %.2f units/s2  (Tweak.GlobalData.Gravity)",
            physics.settings().gravity);
    LogInfo("mesh friction  : %.2f           (o.Physics.DefaultMeshFriction)",
            physics.settings().meshFriction);
    LogInfo("static         : %zu triangles", physics.staticTriangles());
    LogInfo("props          : %zu bodies, %zu unresolved", physics.props(),
            physics.unresolvedProps());
    LogInfo("bodies total   : %zu", physics.bodyCount());

    // The camera's body exists from the start in the real thing, and it sits
    // exactly where the camera does - so every probe below has to be run with
    // it present or it is not testing what the engine does.
    physics.SetProbeRadius(kCameraRadius);
    // The player's own pusher: the widest of the four spheres the shape factory
    // builds for BodyTypes.Player at bodyScale 1.0 (Engine.dll 0x101b3e20).
    physics.SetPawnProbeRadius(0.4f);

    float spawn[3] = {level.info().startPos[0], level.info().startPos[1],
                      level.info().startPos[2]};
    physics.MoveProbe(spawn, false);
    LogInfo("spawn          : %.2f %.2f %.2f%s", spawn[0], spawn[1], spawn[2],
            physics.SphereOverlaps(spawn, kCameraRadius) ? "  (inside geometry)" : "");
    {
        float freed[3] = {spawn[0], spawn[1], spawn[2]};
        const int resolved = physics.Depenetrate(freed, kCameraRadius);
        LogInfo("depenetrate    : %d overlaps, moved %.2f %.2f %.2f", resolved,
                freed[0] - spawn[0], freed[1] - spawn[1], freed[2] - spawn[2]);
    }

    // Push the camera sphere 50 units along each axis. Anything that comes
    // back short hit something; in a closed level, most of them should.
    const float reach = 50.f;
    const char* names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    const float dirs[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                              {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    for (int d = 0; d < 6; ++d) {
        float at[3] = {spawn[0], spawn[1], spawn[2]};
        const float delta[3] = {dirs[d][0] * reach, dirs[d][1] * reach, dirs[d][2] * reach};
        physics.SlideSphere(at, delta, kCameraRadius);
        const float moved = std::sqrt((at[0] - spawn[0]) * (at[0] - spawn[0]) +
                                      (at[1] - spawn[1]) * (at[1] - spawn[1]) +
                                      (at[2] - spawn[2]) * (at[2] - spawn[2]));
        LogInfo("  slide %s     : %6.2f of %.0f units%s", names[d], moved, reach,
                moved < reach - 0.05f ? "  (blocked)" : "");
    }

    {
        // The debug wireframe, counted from a point known to have geometry
        // under it: with a zero radius the static world's box is degenerate, so
        // the difference between the two is what the world contributed.
        std::vector<BodyPose> placed;
        physics.CollectPoses(placed, false);
        float at[3] = {spawn[0], spawn[1], spawn[2]};
        if (!placed.empty()) {
            const Entity& e = level.entities()[placed.front().entity];
            for (int c = 0; c < 3; ++c) at[c] = e.pos[c];
        }
        std::vector<DebugLine> lines;
        physics.CollectDebugLines(at, 0.f, lines);
        const size_t propsOnly = lines.size();
        physics.CollectDebugLines(at, 20.f, lines);
        LogInfo("wireframe      : %zu segments from props, %zu from the world within 20 units",
                propsOnly, lines.size() - propsOnly);
    }

    // Where do the props end up? They are created awake, so five seconds is
    // the level settling exactly as it does on load. Anything that travels a
    // long way is a shape or a placement this port has wrong.
    physics.ActivateProps();
    for (int i = 0; i < 300; ++i) physics.Update(1.f / 60.f);

    std::vector<BodyPose> poses;
    physics.CollectPoses(poses, false);
    size_t stirred = 0, far = 0;
    float worst = 0.f;
    std::string worstName;
    for (const BodyPose& pose : poses) {
        const Entity& e = level.entities()[pose.entity];
        const float d = std::sqrt((pose.pos[0] - e.pos[0]) * (pose.pos[0] - e.pos[0]) +
                                  (pose.pos[1] - e.pos[1]) * (pose.pos[1] - e.pos[1]) +
                                  (pose.pos[2] - e.pos[2]) * (pose.pos[2] - e.pos[2]));
        if (d > 0.05f) ++stirred;
        if (d > 1.f) ++far;
        if (d > worst) { worst = d; worstName = e.name + " (" + e.baseObj + ")"; }
    }
    std::vector<BodyPose> awake;
    physics.CollectPoses(awake, true);
    LogInfo("settle 5 s     : %zu of %zu props moved > 0.05, %zu moved > 1.00 units, "
            "%zu still awake",
            stirred, poses.size(), far, awake.size());
    if (!worstName.empty())
        LogInfo("  furthest     : %.2f units, %s", worst, worstName.c_str());
    {
        // Which templates drift, rather than which instances - a shape that is
        // wrong is wrong for every copy of it.
        std::map<std::string, int> drifters;
        for (const BodyPose& pose : poses) {
            const Entity& e = level.entities()[pose.entity];
            const float d = std::sqrt((pose.pos[0] - e.pos[0]) * (pose.pos[0] - e.pos[0]) +
                                      (pose.pos[1] - e.pos[1]) * (pose.pos[1] - e.pos[1]) +
                                      (pose.pos[2] - e.pos[2]) * (pose.pos[2] - e.pos[2]));
            if (d > 1.f) ++drifters[e.baseObj];
        }
        for (const auto& [name, count] : drifters)
            LogInfo("  drifted      : %3d x %s", count, name.c_str());
    }
    for (size_t i = 0; i < poses.size() && i < 8; ++i) {
        const Entity& e = level.entities()[poses[i].entity];
        // What a query says is under it, for comparison with what the
        // simulation did: the two disagreeing means the body is wrong, not the
        // geometry.
        float probe[3] = {e.pos[0], e.pos[1], e.pos[2]};
        const float down[3] = {0.f, -250.f, 0.f};
        physics.SlideSphere(probe, down, kCameraRadius);
        LogInfo("  %-24s %8.2f %8.2f %8.2f -> delta %6.2f %6.2f %6.2f   query drop %.2f",
                e.name.c_str(), e.pos[0], e.pos[1], e.pos[2], poses[i].pos[0] - e.pos[0],
                poses[i].pos[1] - e.pos[1], poses[i].pos[2] - e.pos[2], e.pos[1] - probe[1]);
    }

    // Last, because it disturbs props: does the camera push what it runs into,
    // and does it do so at every frame rate?
    //
    // A query cannot push anything - only the kinematic body can - so this
    // checks both halves of camera collision are wired up. It runs the same
    // traverse at three frame rates because the failure that prompted it was
    // exactly this: the body was driven with the frame's own delta while the
    // simulation advances in fixed steps, so how far it actually moved
    // depended on the frame rate, and the push landed or missed at random.
    // The three numbers should agree.
    // The last case is shift speed: 8 units in a tenth of a second is 2 units
    // of travel per simulation step against a body 1.2 wide, which a stepped
    // body would jump clean over.
    struct PushCase { float frame; float seconds; };
    const PushCase cases[4] = {{1.f / 120.f, 2.f}, {1.f / 60.f, 2.f},
                               {1.f / 30.f, 2.f},  {1.f / 30.f, 0.1f}};
    for (const PushCase& probeCase : cases) {
        const float frame = probeCase.frame;
        physics.Load(level, templates, dataRoot);
        physics.SetProbeRadius(kCameraRadius);
        if (physics.props() == 0) break;

        std::vector<BodyPose> placed;
        physics.CollectPoses(placed, false);
        const size_t entity = placed.front().entity;
        const Entity& target = level.entities()[entity];
        const float before[3] = {placed.front().pos[0], placed.front().pos[1],
                                 placed.front().pos[2]};

        // Drive the body straight through it. This is the body's half of
        // camera collision on its own - the camera's own slide is a separate
        // question and depends on where in a level the prop happens to sit.
        const float from[3] = {before[0] - 4.f, before[1] + kCameraRadius, before[2]};
        physics.MoveProbe(from, false);
        const int frames = std::max(1, static_cast<int>(probeCase.seconds / frame));
        const float speed = 8.f / probeCase.seconds;
        for (int i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i + 1) / static_cast<float>(frames);
            const float at[3] = {from[0] + 8.f * t, from[1], from[2]};
            physics.MoveProbe(at, true);
            physics.Update(frame);
        }

        physics.CollectPoses(placed, false);
        float after[3] = {before[0], before[1], before[2]};
        for (const BodyPose& pose : placed)
            if (pose.entity == entity)
                for (int c = 0; c < 3; ++c) after[c] = pose.pos[c];

        LogInfo("camera push    : %3.0f fps at %5.1f units/s, %s moved %.2f %.2f %.2f",
                1.f / frame, speed, target.name.c_str(), after[0] - before[0],
                after[1] - before[1], after[2] - before[2]);
    }
    return 0;
}

// Diagnostic: how many world units is the player camera above the floor?
// The level start position is the player spawn, so the drop from it to the
// geometry directly below gives a real-world anchor for the unit scale.
// Diagnostic: the highest world vertex below a point, within a radius.
// Diagnostic: dump the zone/portal graph, and which zones contain a point.

int HkeTextCmd(const char* path) {
    std::string text, error;
    if (!Hke::DecodeToText(path, text, error)) {
        LogInfo("%s: %s", path, error.c_str());
        if (text.empty()) return 1;
        LogInfo("--- decoded up to the failure ---");
    }
    fwrite(text.data(), 1, text.size(), stdout);
    return error.empty() ? 0 : 1;
}

int RagdollCmd(const char* path, const char* modelsRoot) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::is_directory(path, ec)) {
        size_t ascii = 0, binary = 0, failed = 0;
        size_t bodies = 0, constraints = 0, hulls = 0, hullVerts = 0;
        size_t hinges = 0, ragdolls = 0, breakable = 0, limited = 0;
        size_t danglingGeom = 0, detached = 0, withDetached = 0;
        size_t withModel = 0, bodiesNoBone = 0, withRde = 0, rdeNoBody = 0;
        std::vector<std::string> noBoneNames;
        std::vector<std::string> unknown;
        std::vector<std::string> detachedNames;

        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(path, ec))
            if (e.path().extension() == ".hke") files.push_back(e.path());
        std::sort(files.begin(), files.end());

        for (const fs::path& f : files) {
            Hke hke;
            if (!Hke::Load(f.string(), hke)) {
                ++failed;
                LogInfo("  FAILED %s%s: %s", f.filename().string().c_str(),
                        hke.binary ? " (binary)" : "", hke.error.c_str());
                continue;
            }
            if (hke.binary) ++binary;
            ++ascii;
            bodies += hke.bodies.size();
            constraints += hke.constraints.size();
            hulls += hke.geometries.size();
            for (const HkeGeometry& g : hke.geometries) hullVerts += g.vertexCount();
            for (const HkeConstraint& c : hke.constraints) {
                if (c.kind == HkeConstraint::kHinge) { ++hinges; if (c.limited) ++limited; }
                else ++ragdolls;
                if (c.breakable) ++breakable;
            }
            // Every primitive must resolve its hull by name, or the shape it
            // is meant to carry is not there.
            for (const HkeBody& b : hke.bodies)
                if (!b.geometry.empty() && !hke.Find(b.geometry)) ++danglingGeom;
            for (const std::string& u : hke.unknown)
                if (std::find(unknown.begin(), unknown.end(), u) == unknown.end())
                    unknown.push_back(u);

            // Against the model, which is the check that matters for building
            // bodies: a ragdoll body whose bone the rig does not have cannot
            // be posed by anything.
            Model model;
            if (Model::Load(f.parent_path().string() + "/" + f.stem().string() + ".pkmdl",
                            model)) {
                ++withModel;
                for (const HkeBody& b : hke.bodies) {
                    bool found = false;
                    for (const Bone& bone : model.bones)
                        if (bone.name == b.bone) { found = true; break; }
                    if (!found) {
                        ++bodiesNoBone;
                        if (noBoneNames.size() < 12)
                            noBoneNames.push_back(f.stem().string() + ":" + b.bone);
                    }
                }
            }
            // And against the .rde, which names a subset and only overrides
            // material.
            Ragdoll rde;
            if (Ragdoll::Load(f.parent_path().string() + "/" + f.stem().string() + ".rde",
                              rde)) {
                ++withRde;
                for (const RagdollLimb& limb : rde.limbs)
                    if (!hke.Body(limb.bone)) ++rdeNoBody;
            }
            // THE WEAPON RULE, counted across the whole set: a body that no
            // constraint touches is a limb you can hit which is not part of
            // the body.
            bool any = false;
            for (const HkeBody& b : hke.bodies) {
                bool referenced = false;
                for (const HkeConstraint& c : hke.constraints)
                    if (c.bodyA == b.bone || c.bodyB == b.bone) { referenced = true; break; }
                if (referenced) continue;
                ++detached;
                any = true;
                if (detachedNames.size() < 24)
                    detachedNames.push_back(f.stem().string() + ":" + b.bone);
            }
            if (any) ++withDetached;
        }

        LogInfo("%zu .hke: %zu parsed (%zu of them binary, decoded), %zu failed",
                files.size(), ascii, binary, failed);
        LogInfo("  %zu rigid bodies, %zu hulls (%zu vertices), %zu dangling hull refs",
                bodies, hulls, hullVerts, danglingGeom);
        LogInfo("  %zu constraints: %zu ragdoll (cone/twist), %zu hinge (%zu limited), "
                "%zu breakable", constraints, ragdolls, hinges, limited, breakable);
        LogInfo("  %zu detached bodies in %zu models - the weapons", detached, withDetached);
        for (const std::string& n : detachedNames) LogInfo("    %s", n.c_str());
        LogInfo("  cross-check: %zu have a model, %zu ragdoll bodies with no bone; "
                "%zu have an .rde, %zu .rde limbs with no body",
                withModel, bodiesNoBone, withRde, rdeNoBody);
        for (const std::string& n : noBoneNames) LogInfo("    %s", n.c_str());
        LogInfo("  unknown keywords: %zu", unknown.size());
        for (const std::string& u : unknown) LogInfo("    %s", u.c_str());
        return failed == 0 ? 0 : 3;
    }

    std::string base = path;
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    Hke hke;
    if (!Hke::Load(base + ".hke", hke)) {
        LogInfo("%s.hke: %s", base.c_str(), hke.error.c_str());
        return 2;
    }
    LogInfo("%s.hke: version %d, world scale %.3f, gravity %.2f %.2f %.2f",
            base.c_str(), hke.version, hke.worldScale,
            hke.gravity[0], hke.gravity[1], hke.gravity[2]);
    LogInfo("  drag %.3f linear / %.3f angular, deactivation threshold %.2f",
            hke.linearDrag, hke.angularDrag, hke.deactivationThreshold);

    LogInfo("  %zu bodies:", hke.bodies.size());
    for (const HkeBody& b : hke.bodies) {
        const HkeGeometry* g = hke.Find(b.geometry);
        LogInfo("    %-16s mass %-8.2f rest %.2f fric %.2f/%.2f  hull %s (%zu verts, %zu tris)"
                "  at %.2f %.2f %.2f",
                b.bone.c_str(), b.mass, b.elasticity, b.staticFriction, b.dynamicFriction,
                b.geometry.c_str(), g ? g->vertexCount() : 0, g ? g->triangleCount() : 0,
                b.translation[0], b.translation[1], b.translation[2]);
    }

    LogInfo("  %zu constraints:", hke.constraints.size());
    for (const HkeConstraint& c : hke.constraints) {
        if (c.kind == HkeConstraint::kHinge) {
            LogInfo("    hinge   %-14s -> %-14s  limit %7.2f .. %7.2f deg%s%s",
                    c.bodyA.c_str(), c.bodyB.c_str(),
                    c.limitMinAngle * 180.f / 3.14159265f,
                    c.limitMaxAngle * 180.f / 3.14159265f,
                    c.limited ? "" : "  (unlimited)", c.breakable ? "  BREAKABLE" : "");
        } else {
            LogInfo("    ragdoll %-14s -> %-14s  twist %6.1f..%6.1f  cone %6.1f..%6.1f  "
                    "plane %6.1f..%6.1f%s",
                    c.bodyA.c_str(), c.bodyB.c_str(),
                    c.twistMin * 180.f / 3.14159265f, c.twistMax * 180.f / 3.14159265f,
                    c.coneMin * 180.f / 3.14159265f, c.coneMax * 180.f / 3.14159265f,
                    c.planeMin * 180.f / 3.14159265f, c.planeMax * 180.f / 3.14159265f,
                    c.breakable ? "  BREAKABLE" : "");
        }
    }

    // Do the constraint anchors coincide at the pose the file was AUTHORED in?
    // Each two-bodied constraint names a point in each body; if those two do
    // not land on the same spot at rest, the ragdoll is torn before anything
    // has touched it, and every joint inherits that error.
    {
        float worst = 0.f;
        std::string worstPair;
        size_t checked = 0;
        for (const HkeConstraint& c : hke.constraints) {
            if (c.worldSpace) continue;
            const HkeBody* ba = hke.Body(c.bodyA);
            const HkeBody* bb = hke.Body(c.bodyB);
            if (ba == nullptr || bb == nullptr) continue;
            const bool spring = c.kind == HkeConstraint::kStiffSpring;
            const float* la = spring ? c.localPointA
                            : (c.kind == HkeConstraint::kHinge) ? c.hingePosA : c.csToRef[3];
            const float* lb = spring ? c.localPointB
                            : (c.kind == HkeConstraint::kHinge) ? c.hingePosB : c.csToAtt[3];
            Mat4 ra, rb;
            ba->RestMatrix(ra.m);
            bb->RestMatrix(rb.m);
            float wa[3], wb[3];
            ra.TransformPoint(la[0], la[1], la[2], wa);
            rb.TransformPoint(lb[0], lb[1], lb[2], wb);
            float d2 = 0.f;
            for (int k = 0; k < 3; ++k) d2 += (wa[k] - wb[k]) * (wa[k] - wb[k]);
            // A stiff spring holds its two points a LENGTH apart, not together.
            if (spring) {
                LogInfo("  stiff spring %s: %s->%s authored %.3f, SPRING_LENGTH %.3f",
                        c.name.c_str(), c.bodyA.c_str(), c.bodyB.c_str(), std::sqrt(d2),
                        c.springLength);
                continue;
            }
            ++checked;
            if (std::sqrt(d2) > worst) { worst = std::sqrt(d2); worstPair = c.bodyA + "->" + c.bodyB; }
        }
        LogInfo("  anchor gap at the authored rest pose: worst %.4f model units over %zu "
                "constraints (%s)", worst, checked, worstPair.c_str());
    }

    // Which limbs are NOT part of the body - the answer Ragdoll::Joint_AreLinked
    // gives the stake, and the reason a thrown weapon is not a hit.
    std::string root = hke.Body("root") ? "root" : (hke.Body("ROOOT") ? "ROOOT" : "");
    if (root.empty() && !hke.bodies.empty()) root = hke.bodies.front().bone;
    LogInfo("  linkage to \"%s\":", root.c_str());
    for (const HkeBody& b : hke.bodies)
        if (!hke.Linked(b.bone, root))
            LogInfo("    DETACHED  %s   (a stake passes through this)", b.bone.c_str());

    // And what the .rde has to say, which is only ever material - the shapes
    // and the mass are here, which is why every .rde mass is -1.
    Ragdoll rde;
    if (Ragdoll::Load(base + ".rde", rde)) {
        size_t matched = 0, missing = 0;
        for (const RagdollLimb& limb : rde.limbs)
            (hke.Body(limb.bone) ? matched : missing)++;
        LogInfo("  .rde: %zu limbs, %zu matched to a body, %zu with no body",
                rde.limbs.size(), matched, missing);
        for (const RagdollLimb& limb : rde.limbs)
            if (!hke.Body(limb.bone)) LogInfo("    no body for .rde limb %s", limb.bone.c_str());
    } else {
        LogInfo("  .rde: %s", rde.error.c_str());
    }

    // And whether the model has a bone for every body the ragdoll names.
    Model model;
    if (Model::Load(base + ".pkmdl", model)) {
        size_t absent = 0;
        for (const HkeBody& b : hke.bodies) {
            bool found = false;
            for (const Bone& bone : model.bones)
                if (bone.name == b.bone) { found = true; break; }
            if (!found) { ++absent; LogInfo("    no BONE for body %s", b.bone.c_str()); }
        }
        LogInfo("  model: %zu bones, %zu ragdoll bodies with no bone",
                model.bones.size(), absent);
    }
    if (!hke.unknown.empty()) {
        LogInfo("  unknown keywords:");
        for (const std::string& u : hke.unknown) LogInfo("    %s", u.c_str());
    }
    (void)modelsRoot;
    return 0;
}

