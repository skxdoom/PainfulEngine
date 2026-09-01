// The script layer, and the assets it drives: animation, sound, waypoints.
#include "Commands.h"

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
    // The player's own pusher: the widest of the four spheres the shape factory
    // builds for BodyTypes.Player at bodyScale 1.0 (Engine.dll 0x101b3e20).
    physics.SetPawnProbeRadius(0.4f);
    PlayerPawn pawn;
    Input input;
    ScriptEngine engine;
    engine.Bind(host);
    engine.AttachPhysics(&physics, dataRoot);
    engine.AttachPlayer(&pawn);
    engine.AttachInput(&input);
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
        }
        // The diagnostic hook: run an arbitrary chunk between OnPlay and the
        // ticks - teleport the player into a trigger, poke a template, ...
        if (exec && exec[0]) host.RunString(exec);
        for (int i = 0; i < frames && !host.quitRequested(); ++i) {
            input.BeginFrame();
            engine.SetFrameDelta(1.f / 60.f);

            engine.TickAnimations(1.f / 60.f);
            engine.TickMonsters(1.f / 60.f);
            engine.TickProjectiles(1.f / 60.f);
            host.FrameTick(1.0 / 60.0);
            physics.Update(1.f / 60.f);
            engine.SyncFromPhysics();
            engine.TickRagdolls();
            // The player's pusher follows its centre. SlideSphere is a query
            // and touches nothing, so without a body the pawn walks through
            // corpses and loose props without either noticing.
            {
                float centre[3];
                const float* h = pawn.headPos();
                for (int c = 0; c < 3; ++c) centre[c] = h[c];
                centre[1] -= 0.9f;      // head is centre + 0.9, per GetPawnHeadPos
                physics.MovePawnProbe(centre, true);
            }
            engine.TickTriggers();
            engine.TickLifetimes(1.f / 60.f);
        }
    }
    LogInfo("entities: %zu created, %zu released, %zu live; map \"%s\" scale %.2f",
            engine.created(), engine.released(), engine.entities().size(),
            engine.world().mapPath.c_str(), engine.world().scale);
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
    float prev[3] = {0, 0, 0};
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
    const float listener[3] = {0, 0, 0};
    const float fwd[3] = {0, 0, 1};
    const float right[3] = {1, 0, 0};
    audio.SetListener(listener, fwd, right);

    // 2D at full volume: no distance, no panning, nothing to get wrong.
    const int v = audio.Play2D(name, 100.f, false, true);
    if (!v) {
        LogInfo("could not play %s (missing %zu)", name, audio.samplesMissing());
        return 2;
    }
    LogInfo("playing %s ...", name);

    const double total = seconds ? std::atof(seconds) : 4.0;
    const Uint64 start = SDL_GetTicks();
    while ((SDL_GetTicks() - start) < Uint64(total * 1000.0)) {
        audio.Update();
        SDL_Delay(50);
    }
    LogInfo("done: %zu started, %zu playing at the end", audio.voicesStarted(),
            audio.voicesPlaying());
    return 0;
}


int MkLevelCmd(const char* dataRoot, const char* levelName, float extent,
                      float height, const char* texture) {
    const std::string root = dataRoot;
    const std::string name = levelName;

    // 64x64 cells keeps the index array inside the u16 the format stores, with
    // room to spare: 4225 vertices and 24576 indices against a 65535 ceiling.
    constexpr int kCells = 64;
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
    floor.uvChannels = 1;   // position, normal, uv inline - no lightmap

    floor.verts.reserve(size_t(kCells + 1) * (kCells + 1) * 8);
    for (int iz = 0; iz <= kCells; ++iz) {
        for (int ix = 0; ix <= kCells; ++ix) {
            const float x = -extent + float(ix) * step;
            const float z = -extent + float(iz) * step;
            floor.verts.push_back(x);
            floor.verts.push_back(height);
            floor.verts.push_back(z);
            floor.verts.push_back(0.f);          // normal
            floor.verts.push_back(1.f);          //   points up
            floor.verts.push_back(0.f);
            floor.verts.push_back(x * uvPerUnit);
            floor.verts.push_back(z * uvPerUnit);
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
    floor.bboxMax[0] =  extent; floor.bboxMax[1] = height; floor.bboxMax[2] =  extent;

    Material mat;
    mat.firstIndex = 0;
    mat.triangleCount = uint16_t(floor.indices.size() / 3);
    mat.slots[0].name = texture;    // slots 1-3 stay empty: no lightmap, no detail
    floor.materials.push_back(mat);

    MapMesh mesh;
    mesh.objects.push_back(std::move(floor));

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
    const std::string script = "-- Generated by `mklevel`. A floor and nothing else.\n";
    const std::string sTxt = settings;
    if (!WriteFile(levelDir + "/" + name + ".CLevel",
                   std::vector<uint8_t>(sTxt.begin(), sTxt.end())) ||
        !WriteFile(levelDir + "/" + name + ".lua",
                   std::vector<uint8_t>(script.begin(), script.end()))) {
        LogWarn("cannot write the level files under %s", levelDir.c_str());
        return 1;
    }

    LogInfo("wrote %s: %zu verts, %zu tris, %.0f x %.0f units at y=%.1f, texture \"%s\"",
            mapPath.c_str(), mesh.objects[0].vertexCount(),
            mesh.objects[0].triangleCount(), extent * 2, extent * 2, height, texture);
    LogInfo("wrote %s/%s.CLevel and %s.lua", levelDir.c_str(), name.c_str(), name.c_str());
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

    auto bounds = [](const std::vector<float>& v, size_t stride, float lo[3], float hi[3]) {
        for (int c = 0; c < 3; ++c) { lo[c] = 1e30f; hi[c] = -1e30f; }
        for (size_t i = 0; i + stride <= v.size(); i += stride)
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], v[i + c]);
                hi[c] = std::max(hi[c], v[i + c]);
            }
    };

    float bLo[3], bHi[3], pLo[3], pHi[3];
    for (int c = 0; c < 3; ++c) { bLo[c] = pLo[c] = 1e30f; bHi[c] = pHi[c] = -1e30f; }
    std::vector<float> posed;
    size_t skinnedMeshes = 0;
    for (const ModelMesh& mesh : model.meshes) {
        if (mesh.vertexCount() == 0) continue;
        float lo[3], hi[3];
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
