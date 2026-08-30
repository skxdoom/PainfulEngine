// PainfulEngine - an open reimplementation of PainEngine (Painkiller, 2004).
//
// Requires your own copy of the game's data; no original assets are distributed.
// Formats and the porting plan are documented in Docs/Source_Port.md.

#include "Assets/Dat.h"
#include "Assets/ShaderScript.h"
#include "Assets/Ani.h"
#include "Assets/Mpk.h"
#include "Assets/Pkmdl.h"
#include "Assets/Skeleton.h"
#include "Assets/Waypoints.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Game/PlayerPawn.h"
#include "Audio/AudioEngine.h"
#include <SDL3/SDL.h>
#include "Game/ScriptEngine.h"
#include "Script/LuaHost.h"
#include "Render/Renderer.h"
#include "Render/SkyRenderer.h"
#include "Render/TextureCache.h"
#include <bimg/decode.h>
#include <bx/allocator.h>
namespace painful { extern bx::DefaultAllocator g_allocator; }
#include <algorithm>
#include "Render/EntityRenderer.h"
#include "Render/BillboardRenderer.h"
#include "Render/HudRenderer.h"
#include "Render/ParticleRenderer.h"
#include "Render/WorldRenderer.h"
#include "Render/DebugLines.h"
#include "Render/Window.h"
#include "World/Level.h"
#include "World/PhysicsWorld.h"
#include "World/Templates.h"
#include "World/Zones.h"
#include "Assets/Skeleton.h"
#include "Assets/Waypoints.h"

#include <chrono>
#include <algorithm>
#include <array>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>

using namespace painful;

static int Usage() {
    LogInfo("PainfulEngine\n");
    LogInfo("  PainfulEngine                                    find the game data and run");
    LogInfo("  PainfulEngine run   <Data/Levels/NAME> <DataRoot>  open a specific level");
    LogInfo("  PainfulEngine level <Data/Levels/NAME> <DataRoot>  headless report");
    LogInfo("  PainfulEngine physics <Data/Levels/NAME> <DataRoot> physics world probe");
    LogInfo("  PainfulEngine levels <DataRoot>                    list levels");
    LogInfo("  PainfulEngine lua   <DataRoot> [frames] [level]    boot the script layer");
    LogInfo("  PainfulEngine game  <DataRoot> [level] [--shot f]  script-driven windowed run");
    LogInfo("  PainfulEngine map   <file.mpk>");
    LogInfo("  PainfulEngine model <file.pkmdl>");
    LogInfo("  PainfulEngine pose  <file.pkmdl> <anim> [time]      skinning, checked numerically");
    LogInfo("  PainfulEngine bones <file.pkmdl> [anim] [time] [joint:ax,ay,az]");
    return 1;
}

// Totals worth showing while the renderer is still being built out.
struct LevelStats {
    size_t objects = 0, verts = 0, tris = 0, materials = 0, collidable = 0;
};

// The free camera collides as a sphere this wide.
//
// The player body's own widest sphere is 0.4 - EngineGame::CreatePlayer asks
// for BodyTypes.Player at bodyScale 1.0, and the shape factory builds that as a
// stack of four spheres in units of 0.2, the widest of them 2.0 units of that.
// The camera is deliberately fatter: it is not a player, it has no body to see
// clipping into a wall, and at player width it slides so close to surfaces that
// the near plane cuts through them.
static constexpr float kCameraRadius = 1.2f;

// How far around the camera the static world's wireframe is collected. The
// whole level is 300k triangles, so the debug view is local by necessity.
static constexpr float kPhysicsDebugRadius = 20.f;

static LevelStats Summarise(const Level& level) {
    LevelStats s;
    if (!level.mapLoaded()) return s;
    const MapMesh& m = level.map();
    s.objects = m.objects.size();
    for (const MapObject& o : m.objects) {
        s.verts += o.vertexCount();
        s.tris += o.triangleCount();
        s.materials += o.materials.size();
        if (o.isCollidable()) ++s.collidable;
    }
    return s;
}

// Shaders live next to the executable, so the engine can be launched from any
// working directory.
static std::string ShaderDirFor(const char* exePath) {
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::absolute(exePath, ec);
    if (!ec) {
        std::filesystem::path beside = exe.parent_path() / "Shaders";
        if (std::filesystem::exists(beside, ec)) return beside.string();
    }
    return "Shaders";   // fall back to the working directory
}

static std::string MapNameWithoutExtension(const std::string& mapFile) {
    size_t dot = mapFile.find_last_of('.');
    return dot == std::string::npos ? mapFile : mapFile.substr(0, dot);
}

static int RunCmd(const char* levelDir, const char* dataRoot,
                  const std::string& shotPath, const char* exePath,
                  const float* startPos, const float* startAngles,
                  int cullMode, int entityCull, float entityScale, bool skyOnly,
                  bool novis, bool noclip, bool physicsDebug) {
    const std::string root = dataRoot;
    const std::string shaderDir = ShaderDirFor(exePath);

    // Enumerate every level once so they can be cycled without restarting.
    std::vector<std::string> levelDirs;
    for (const DirEntry& entry : FileSystem::Get().List(root + "/Levels")) {
        if (entry.isDirectory) levelDirs.push_back(root + "/Levels/" + entry.name);
    }
    std::sort(levelDirs.begin(), levelDirs.end());
    if (levelDirs.empty()) {
        LogInfo("no levels found under %s/Levels", root.c_str());
        return 2;
    }

    int current = 0;
    {
        const std::filesystem::path wanted = std::filesystem::path(levelDir).filename();
        for (size_t i = 0; i < levelDirs.size(); ++i) {
            if (std::filesystem::path(levelDirs[i]).filename() == wanted) {
                current = static_cast<int>(i);
                break;
            }
        }
    }

    Window window;
    if (!window.Open("PainfulEngine", 1280, 720)) return 3;

    Renderer renderer;
    if (!renderer.Init(window)) return 3;
    LogInfo("renderer: %s", renderer.BackendName().c_str());

    // These are level-independent, so they are built once and reused.
    TextureCache textures;
    textures.Init(root + "/Textures");
    TemplateCache templates;
    templates.Init(root + "/LScripts/Templates");
    ShaderLibrary shaderScripts;
    if (!shaderScripts.LoadDirectory(root + "/Shaders/Scripts")) {
        for (const std::string& e : shaderScripts.errors()) LogWarn("%s", e.c_str());
    }
    LogInfo("%zu material definitions", shaderScripts.size());
    EmitterLibrary emitterScripts;
    emitterScripts.Init(root + "/Scripts");
    LogInfo("%zu emitters, %zu particle effects", emitterScripts.indexedEmitters(),
            emitterScripts.indexedEffects());

    float liveScale = entityScale;
    std::unique_ptr<Level> level;
    std::unique_ptr<WorldRenderer> world;
    std::unique_ptr<EntityRenderer> entities;
    std::unique_ptr<SkyRenderer> sky;
    std::unique_ptr<ParticleRenderer> particles;
    std::unique_ptr<BillboardRenderer> billboards;
    CollisionMesh collision;
    PhysicsWorld physics;
    physics.SetProbeRadius(kCameraRadius);
    DebugLines debugLines;
    const bool debugLinesReady = debugLines.Init(shaderDir);
    std::vector<DebugLine> physicsWireframe;
    std::vector<BodyPose> movedProps;
    Camera camera;
    LevelStats stats;

    auto loadLevel = [&](int index) {
        const int count = static_cast<int>(levelDirs.size());
        current = ((index % count) + count) % count;

        // Destroy the old level GPU resources before building the new ones.
        world.reset();
        entities.reset();
        sky.reset();
        particles.reset();
        billboards.reset();
        collision.Clear();
        physics.Clear();

        level = std::make_unique<Level>();
        if (!level->Load(levelDirs[current], root)) {
            LogWarn("cannot load %s", levelDirs[current].c_str());
            return;
        }
        // Level-local templates shadow the global ones for this level only.
        templates.SetLevelOverlay(levelDirs[current] + "/Templates");
        stats = Summarise(*level);

        world = std::make_unique<WorldRenderer>();
        world->SetCullMode(cullMode);
        world->SetVisibilityCulling(!novis);
        if (world->Init(shaderDir) && level->mapLoaded()) {
            world->Upload(level->map(), textures,
                          MapNameWithoutExtension(level->info().mapFile),
                          level->info(), &shaderScripts);
        }

        entities = std::make_unique<EntityRenderer>();
        entities->SetCullMode(entityCull);
        entities->SetVisibilityCulling(!novis);
        entities->SetScaleMultiplier(liveScale);
        if (entities->Init(shaderDir)) {
            entities->Build(*level, templates, textures, root, &shaderScripts);
            LogInfo("entities: %zu placed (%zu from packs), %zu hidden, %zu unresolved",
                    entities->placed(), entities->packed(), entities->hidden(),
                    entities->unresolved());
        }

        sky = std::make_unique<SkyRenderer>();
        if (sky->Init(shaderDir)) {
            sky->Load(root + "/Maps", level->info(), textures);
        }

        particles = std::make_unique<ParticleRenderer>();
        particles->SetScaleMultiplier(liveScale);
        if (particles->Init(shaderDir)) {
            particles->Build(*level, templates, emitterScripts, textures, root);
        }

        // Coronas trace the line of sight against solid world geometry, in the
        // same space the world mesh is drawn in.
        if (level->mapLoaded()) collision.Build(level->map(), level->info().scale);

        // The physics world takes the same geometry into Jolt, where it is one
        // static body the camera and anything simulated collide against.
        physics.Load(*level, templates, root);
        // Load settles the props, and settled means asleep - which the
        // per-frame sync deliberately skips. So take every prop's pose once
        // here, or the level is drawn with its furniture back where it was
        // authored and only the ones still moving ever catch up.
        if (entities) {
            physics.CollectPoses(movedProps, false);
            for (const BodyPose& pose : movedProps)
                entities->SetEntityPose(pose.entity, pose.pos, pose.rot);
        }

        billboards = std::make_unique<BillboardRenderer>();
        billboards->SetScaleMultiplier(liveScale);
        if (billboards->Init(shaderDir)) {
            billboards->Build(*level, templates, textures);
        }

        camera.pos[0] = level->info().startPos[0];
        camera.pos[1] = level->info().startPos[1];
        camera.pos[2] = level->info().startPos[2];

        // The original hard-clips the world at FarClipDist (Cfg.ClipPlane 100
        // makes the factor exactly 1) and paints the void in the fog colour.
        // --novis lifts the clip for free-flying level surveys.
        camera.farPlane = novis ? 5000.f : level->info().farClip;
        if (level->info().fogMode != 0) {
            renderer.SetClearColor(level->info().fogColor[0] / 255.f,
                                   level->info().fogColor[1] / 255.f,
                                   level->info().fogColor[2] / 255.f);
        }

        LogInfo("[%d/%zu] %s  map %s  %zu tris  %zu entities",
                current + 1, levelDirs.size(), level->name().c_str(),
                level->info().mapFile.c_str(), stats.tris, level->entities().size());
    };

    loadLevel(current);
    if (startPos) {
        camera.pos[0] = startPos[0];
        camera.pos[1] = startPos[1];
        camera.pos[2] = startPos[2];
    }
    if (startAngles) {
        camera.yaw = startAngles[0];
        camera.pitch = startAngles[1];
    }

    auto previous = std::chrono::steady_clock::now();
    const auto startTime = previous;
    float elapsed = 0.f;
    int frame = 0;

    while (window.PumpEvents()) {
        if (window.TakeResized()) renderer.Resize(window.width(), window.height());

        const int step = window.TakeLevelStep();
        if (step != 0) loadLevel(current + step);

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        elapsed = std::chrono::duration<float>(now - startTime).count();

        float dx = 0.f, dy = 0.f;
        window.TakeMouseDelta(dx, dy);
        camera.Look(dx * 0.003f, -dy * 0.003f);

        if (window.TakeNoclipToggle()) noclip = !noclip;
        if (window.TakePhysicsDebugToggle()) physicsDebug = !physicsDebug;

        const float speed = camera.moveSpeed * (window.IsDown(Key::Fast) ? 4.f : 1.f) * dt;
        float fwd = 0.f, right = 0.f, up = 0.f;
        if (window.IsDown(Key::Forward)) fwd += speed;
        if (window.IsDown(Key::Back))    fwd -= speed;
        if (window.IsDown(Key::Right))   right += speed;
        if (window.IsDown(Key::Left))    right -= speed;
        if (window.IsDown(Key::Up))      up += speed;
        if (window.IsDown(Key::Down))    up -= speed;

        if (noclip) {
            camera.Move(fwd, right, up);
        } else {
            // The camera keeps flying - it is not a player yet, and the player
            // controller belongs with the script host that creates it. It just
            // stops passing through walls: the same move, as a sphere, sliding
            // along whatever the physics world puts in the way.
            float f[3], r[3], delta[3];
            camera.Forward(f);
            camera.Right(r);
            for (int c = 0; c < 3; ++c) delta[c] = f[c] * fwd + r[c] * right;
            delta[1] += up;
            physics.SlideSphere(camera.pos, delta, kCameraRadius);
        }

        // The camera's own body follows it, so props are pushed rather than
        // passed through. It does not push while noclipping - that is the
        // point of noclip.
        physics.MoveProbe(camera.pos, !noclip);
        physics.Update(dt);

        // Anything the simulation moved is drawn where it moved to. Only awake
        // bodies are reported, so a level standing still costs nothing.
        if (entities) {
            physics.CollectPoses(movedProps);
            for (const BodyPose& pose : movedProps)
                entities->SetEntityPose(pose.entity, pose.pos, pose.rot);
        }

        renderer.BeginFrame();
        const LevelInfo& info = level->info();
        if (sky) sky->Draw(Renderer::kSkyView, camera, window.width(), window.height(), elapsed);
        if (world && !skyOnly) {
            world->Draw(Renderer::kWorldView, camera, window.width(), window.height(), info,
                        elapsed);
        }
        if (entities && !skyOnly) {
            entities->Draw(Renderer::kWorldView, camera, window.width(), window.height(), info,
                           elapsed);
        }
        // Particles last in the world view: they are blended and write no
        // depth, so everything solid has to be down first.
        if (particles && !skyOnly) {
            particles->Tick(dt);
            particles->Draw(Renderer::kWorldView, camera, window.width(), window.height());
        }
        // Coronas last of all: they ignore depth entirely, so anything drawn
        // after them would be wrong regardless of where it sits in the world.
        if (billboards && !skyOnly) {
            billboards->Update(camera, dt, collision);
            billboards->Draw(Renderer::kWorldView, camera);
        }
        // The collision wireframe goes over everything, since the whole point
        // of it is to be compared against what was drawn underneath.
        if (physicsDebug && debugLinesReady) {
            physics.CollectDebugLines(camera.pos, kPhysicsDebugRadius, physicsWireframe);
            debugLines.Draw(Renderer::kWorldView, physicsWireframe);
        }

        renderer.DebugText(1, "PainfulEngine  -  %s  -  %.1f fps",
                           renderer.BackendName().c_str(), dt > 0.f ? 1.f / dt : 0.f);
        renderer.DebugText(2, "[%d/%zu] %s   map %s", current + 1, levelDirs.size(),
                           level->name().c_str(), info.mapFile.c_str());
        renderer.DebugText(3, "%zu tris, %zu world draws, %zu entity draws, %zu placed models, zones %zu/%zu",
                           world ? world->trianglesUploaded() : 0,
                           world ? world->drawCalls() : 0,
                           entities ? entities->drawCalls() : 0,
                           entities ? entities->placed() : 0,
                           world ? world->zonesVisible() : 0,
                           world ? world->zoneCount() : 0);
        renderer.DebugText(5, "%zu particles in %zu emitters, %zu effects   |   "
                              "billboards %zu/%zu visible (%zu coronas, %zu traces)",
                           particles ? particles->liveParticles() : 0,
                           particles ? particles->emitters() : 0,
                           particles ? particles->effects() : 0,
                           billboards ? billboards->visible() : 0,
                           billboards ? billboards->placed() : 0,
                           billboards ? billboards->coronas() : 0,
                           billboards ? billboards->traces() : 0);
        // rot prints in the exact form --look takes, so a HUD screenshot can
        // be reproduced verbatim: --pos <pos> --look <rot>.
        renderer.DebugText(4, "pos %.1f %.1f %.1f   rot %.2f %.2f   sky %s",
                           camera.pos[0], camera.pos[1], camera.pos[2],
                           camera.yaw, camera.pitch,
                           (sky && sky->loaded())
                               ? (sky->layered() ? "layered" : "lowquality") : "none");
        renderer.DebugText(7, "physics: %s r%.1f, %zu static tris, %zu props, gravity %.2f%s",
                           noclip ? "camera NOCLIP" : "camera collides", kCameraRadius,
                           physics.staticTriangles(), physics.props(),
                           physics.settings().gravity,
                           physicsDebug ? "   |   hulls: green awake, yellow asleep, grey world"
                                        : "");
        if (physicsDebug) {
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (const DebugLine& line : physicsWireframe) {
                for (int c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], line.a[c]);
                    hi[c] = std::max(hi[c], line.a[c]);
                }
            }
            renderer.DebugText(8, "hulls: %zu segments, %zu drawn, bounds %.1f %.1f %.1f .. %.1f %.1f %.1f%s",
                               physicsWireframe.size(), debugLines.drawn(), lo[0], lo[1], lo[2],
                               hi[0], hi[1], hi[2],
                               debugLinesReady ? "" : "   (no vs_debug/fs_debug)");
        }
        renderer.DebugText(6, "%s - WASD move, shift fast, space/ctrl up-down, N noclip, P hulls, [ ] change level, esc release",
                           window.mouseCaptured() ? "mouse captured" : "click to capture mouse");
        renderer.EndFrame();

        if (!shotPath.empty()) {
            ++frame;
            // PAINFUL_SHOT_FRAME delays the capture - useful for verifying
            // time-driven effects like UV animation.
            int shotFrame = 30;
            if (const char* e = getenv("PAINFUL_SHOT_FRAME")) shotFrame = std::atoi(e);
            if (frame == shotFrame) renderer.RequestScreenshot(shotPath);
            if (frame >= shotFrame + 4) break;
        }
    }
    return 0;
}

static int LevelCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) {
        LogInfo("failed: %s", level.error().c_str());
        return 2;
    }
    const LevelInfo& i = level.info();

    LogInfo("level '%s'", level.name().c_str());
    LogInfo("  map          : %s%s", i.mapFile.c_str(), level.mapLoaded() ? "" : "  (not loaded)");
    LogInfo("  waypoints    : %s", i.waypointsFile.c_str());
    LogInfo("  scale        : %g", i.scale);
    LogInfo("  start pos    : (%.3f, %.3f, %.3f)", i.startPos[0], i.startPos[1], i.startPos[2]);
    LogInfo("  ambient      : (%.0f, %.0f, %.0f)", i.ambient[0], i.ambient[1], i.ambient[2]);
    LogInfo("  fog          : density %g, start %g, mode %d, colour (%.0f, %.0f, %.0f)",
            i.fogDensity, i.fogStart, i.fogMode, i.fogColor[0], i.fogColor[1], i.fogColor[2]);
    LogInfo("  mesh friction: %g", i.meshFriction);

    std::map<std::string, size_t> byType;
    for (const Entity& e : level.entities()) byType[e.type]++;
    LogInfo("  entities     : %zu", level.entities().size());
    for (const auto& kv : byType) LogInfo("      %-16s %zu", kv.first.c_str(), kv.second);

    if (level.mapLoaded()) {
        const LevelStats s = Summarise(level);
        LogInfo("  world mesh   : %zu objects, %zu verts, %zu tris, %zu materials",
                s.objects, s.verts, s.tris, s.materials);
        LogInfo("  collidable   : %zu objects (excludes noclip/portal/zone volumes)", s.collidable);
    }
    return 0;
}

// Diagnostic: where do placed entities sit relative to real world geometry?
static int EntitiesCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");

    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const MapObject& o : level.map().objects) {
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3];
            o.position(i, p);
            for (int c = 0; c < 3; ++c) { if (p[c] < lo[c]) lo[c] = p[c]; if (p[c] > hi[c]) hi[c] = p[c]; }
        }
    }
    LogInfo("world bounds x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]",
            lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    LogInfo("level start   (%.1f, %.1f, %.1f)",
            level.info().startPos[0], level.info().startPos[1], level.info().startPos[2]);
    LogInfo("");
    LogInfo("OUTLIERS ONLY (outside bounds, or >20 units from any geometry)");
    LogInfo("%-26s %-22s %9s %8s  %s", "entity", "position", "nearestGeo", "inBounds", "model");

    int shown = 0, outside = 0, far = 0;
    (void)shown;
    for (const Entity& e : level.entities()) {
        std::string model = templates.ResolveString(e.props, e.baseObj, "Model");
        if (model.empty()) continue;
        ++shown;

        // Distance to the closest world vertex, as a proxy for "is it in a room".
        double best = 1e30;
        float bx_ = 0, by_ = 0, bz_ = 0;
        for (const MapObject& o : level.map().objects) {
            for (size_t i = 0; i < o.vertexCount(); ++i) {
                float p[3];
                o.position(i, p);
                double dx = p[0] - e.pos[0], dy = p[1] - e.pos[1], dz = p[2] - e.pos[2];
                double d = dx * dx + dy * dy + dz * dz;
                if (d < best) { best = d; bx_ = p[0]; by_ = p[1]; bz_ = p[2]; }
            }
        }
        best = std::sqrt(best);
        const double tScale = templates.ResolveNumber(e.baseObj, "Scale", 1.0);
        const bool hasOwn = e.props.Has("Scale");
        const double fScale = hasOwn ? e.props.Number("Scale", tScale) : tScale;
        bool inBounds = e.pos[0] >= lo[0] && e.pos[0] <= hi[0] &&
                        e.pos[1] >= lo[1] && e.pos[1] <= hi[1] &&
                        e.pos[2] >= lo[2] && e.pos[2] <= hi[2];
        if (!inBounds) ++outside;
        if (best > 20.0) ++far;
        if (shown <= 14) {
            LogInfo("%-12s %-24s model=%-16s instScale=%-4s tmpl=%.3f final=%.3f",
                    e.type.c_str(), e.name.c_str(), model.c_str(),
                    hasOwn ? "yes" : "no", tScale, fScale);
        }
    }

    LogInfo("");
    LogInfo("placed with models: %d   outside world bounds: %d   further than 20 units from geometry: %d",
            shown, outside, far);
    return 0;
}

// Diagnostic: the .CLevel carries o.Pos and o.Ang. Do they place the world mesh?
// Try candidate transforms and see which seats entities closest to geometry.
static int FitCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");

    std::vector<std::array<float, 3>> targets;
    for (const Entity& e : level.entities()) {
        if (templates.ResolveString(e.props, e.baseObj, "Model").empty()) continue;
        targets.push_back({e.pos[0], e.pos[1], e.pos[2]});
    }
    // Subsample the world; we only need a ranking, not exact distances.
    std::vector<std::array<float, 3>> verts;
    for (const MapObject& o : level.map().objects) {
        for (size_t i = 0; i < o.vertexCount(); i += 8) {
            float p[3];
            o.position(i, p);
            verts.push_back({p[0], p[1], p[2]});
        }
    }
    const float* pos = level.info().startPos;
    const float* ang = level.info().angles;
    LogInfo("entities %zu, sampled world verts %zu", targets.size(), verts.size());
    LogInfo("o.Pos (%.2f, %.2f, %.2f)   o.Ang (%.3f, %.3f, %.3f)",
            pos[0], pos[1], pos[2], ang[0], ang[1], ang[2]);
    LogInfo("");

    struct Candidate { const char* name; int axis; float degrees; bool translate; };
    const Candidate candidates[] = {
        {"identity",                    0,  0.f,      false},
        {"translate by Pos",            0,  0.f,      true },
        {"rotX(+Ang.x)",                0,  ang[0],   false},
        {"rotX(+Ang.x) + Pos",          0,  ang[0],   true },
        {"rotX(-Ang.x)",                0, -ang[0],   false},
        {"rotX(-Ang.x) + Pos",          0, -ang[0],   true },
        {"rotY(+Ang.x) + Pos",          1,  ang[0],   true },
        {"rotY(-Ang.x) + Pos",          1, -ang[0],   true },
        {"translate by -Pos",           0,  0.f,      false},
    };

    for (const Candidate& c : candidates) {
        const float r = c.degrees * 3.14159265f / 180.f;
        const float cs = std::cos(r), sn = std::sin(r);
        double total = 0;
        for (const auto& t : targets) {
            double best = 1e30;
            for (const auto& v : verts) {
                float x = v[0], y = v[1], z = v[2];
                float tx, ty, tz;
                if (c.axis == 0) { tx = x; ty = y * cs - z * sn; tz = y * sn + z * cs; }
                else             { tx = x * cs + z * sn; ty = y; tz = -x * sn + z * cs; }
                if (c.translate) { tx += pos[0]; ty += pos[1]; tz += pos[2]; }
                if (std::string(c.name) == "translate by -Pos") { tx -= pos[0]; ty -= pos[1]; tz -= pos[2]; }
                double dx = tx - t[0], dy = ty - t[1], dz = tz - t[2];
                double d = dx * dx + dy * dy + dz * dz;
                if (d < best) best = d;
            }
            total += std::sqrt(best);
        }
        LogInfo("  %-24s mean nearest-geometry distance: %8.2f", c.name, total / targets.size());
    }
    return 0;
}

// Boots the Lua 5.0.2 script layer: runs LScripts/Loader.lua with the
// recovered native surface installed as instrumented stubs, then follows the
// engine's own sequence - Game:Init() and per-frame ticks - and prints what
// the scripts actually called. This is the recovery loop for the native API:
// boot, read the report, implement what the game hit, repeat.
//
// Given a level it goes all the way into gameplay: LoadLevel then
// Game:OnPlay, so the player exists and the whole tick chain runs - actors,
// weapons, pickup polling, triggers. That is what makes the report a
// measurement of the GAME rather than of the boot. `exec` then runs one
// chunk between OnPlay and the first tick, which is how a scripted situation
// gets set up headlessly (teleport into a trigger, poke a template).
static int LuaCmd(const char* dataRoot, int frames, const char* level,
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
            host.FrameTick(1.0 / 60.0);
            physics.Update(1.f / 60.f);
            engine.SyncFromPhysics();
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

// Script-driven windowed run: the game's own Lua loads the level and creates
// every entity through the native API; the C++ side supplies the window, the
// renderer and a free camera. The counterpart to `run`, which drives the
// same subsystems by hand - as natives grow real, this path takes over.
static int GameCmd(const char* dataRoot, const char* levelName, const char* exePath,
                   const std::string& shotPath, const char* exec) {
    const std::string root = dataRoot;
    const std::string shaderDir = ShaderDirFor(exePath);

    Window window;
    if (!window.Open("PainfulEngine", 1280, 720)) return 3;
    Renderer renderer;
    if (!renderer.Init(window)) return 3;
    LogInfo("renderer: %s", renderer.BackendName().c_str());

    TextureCache textures;
    textures.Init(root + "/Textures");
    ShaderLibrary shaderScripts;
    if (!shaderScripts.LoadDirectory(root + "/Shaders/Scripts")) {
        for (const std::string& e : shaderScripts.errors()) LogWarn("%s", e.c_str());
    }

    EntityRenderer entities;
    entities.SetCullMode(1);   // .pkmdl winding; pack meshes carry their own state
    if (!entities.Init(shaderDir)) return 3;

    EmitterLibrary emitterScripts;
    emitterScripts.Init(root + "/Scripts");
    ParticleRenderer particles;
    const bool particlesReady = particles.Init(shaderDir);
    BillboardRenderer billboards;
    const bool billboardsReady = billboards.Init(shaderDir);

    // Boot the scripts with the renderers and the simulation attached, then
    // let them load the level: every ENTITY.Create lands in the renderer,
    // every PO_Create in Jolt, every AddEmitter and SetupCorona in its
    // renderer, as they happen.
    PhysicsWorld physics;
    physics.SetProbeRadius(kCameraRadius);
    PlayerPawn pawn;
    Input input;
    LuaHost host;
    // Bare relative script paths - "config.ini" - resolve against the
    // original's working directory, which is where the executable sits.
    host.SetHomeDir(std::filesystem::absolute(exePath).parent_path().string());
    if (!host.Init(root)) return 3;
    ScriptEngine engine;
    engine.Bind(host);
    engine.AttachRenderer(&entities, &textures, root);
    engine.AttachPhysics(&physics, root);
    if (particlesReady) engine.AttachParticles(&particles, &emitterScripts);
    if (billboardsReady) engine.AttachBillboards(&billboards);

    // Sound. A machine with no output device still plays the game, silently.
    AudioEngine audio;
    if (audio.Init(root + "/Sounds")) engine.AttachAudio(&audio);

    // The 2D layer. The scripts draw the whole interface through it during
    // Game_Render, so it has to be attached before the level loads - the
    // loading screen is itself a HUD script.
    HudRenderer hud;
    const bool hudReady = hud.Init(shaderDir, root + "/Fonts");
    if (hudReady) engine.AttachHud(&hud, &textures);
    engine.SetScreenSize(window.width(), window.height());
    engine.SetResolutions(window.DisplayModes());
    engine.AttachPlayer(&pawn);
    engine.AttachInput(&input);

    // An item's `action` is a string of Lua the menu runs when it is chosen,
    // and its focus sound goes through the same mixer everything else uses.
    engine.menu().SetActionRunner([&host](const std::string& chunk) { host.RunString(chunk); });
    engine.menu().SetSoundPlayer(
        [&audio](const std::string& name) { audio.Play2D(name, 1.f, false, true); });
    // The words a widget draws for itself, out of the language table.
    engine.menu().SetTextReader([&host](const std::string& key) {
        return host.GetTextField("TXT", key);
    });

    // Escape belongs to the menu here, not to the window: see the game loop.
    window.SetEscapeQuits(false);

    if (!host.Boot()) return 3;
    host.CallGameInit();
    host.CallGameLoadLevel(levelName);
    // One chunk of Lua after the level is up, for turning on whatever a run is
    // meant to look at - Cfg.ShowFPS, a spawn, a camera placement.
    if (exec && exec[0]) host.RunString(exec);

    // Let the props settle before the first frame, the same fixed steps the
    // hand-driven path takes, and draw them where they came to rest - all of
    // them, because settled means asleep and asleep is what the per-frame
    // sync deliberately skips.
    physics.Settle(90);
    engine.SyncFromPhysics(false);

    // The transition into gameplay, in the engine's own order (Game.lua's
    // demo loader shows it plainly): the camera is seated from the level,
    // then the mouse locks, then play begins.
    //
    // The seating is already done by this point - the level loaded with the
    // mouse unlocked, so CLevel:Synchronize pushed Lev.Pos and Lev.Ang out
    // through CAM.SetPos/SetAng - so all that is left is to adopt the pose
    // into our own camera. This ORDER is load-bearing: CreatePlayerSP seats
    // the player at Lev.Pos, and locking any earlier inverts the
    // synchronise, so the level records our camera instead of pushing its
    // own pose and the player spawns at the world origin.
    float seatPos[3] = {0, 0, 0};
    float seatYaw = 0.f, seatPitch = 0.f;
    const bool seated = engine.TakeCameraPose(seatPos, seatYaw, seatPitch);
    if (seated)
        LogInfo("camera seated from the level at %.1f %.1f %.1f", seatPos[0], seatPos[1],
                seatPos[2]);
    engine.SetMouseLocked(true);
    host.CallGameOnPlay();

    // Turn the recorded WORLD.* state into renderer state.
    const ScriptEngine::WorldState& ws = engine.world();
    LevelInfo info;
    info.scale = ws.scale;
    info.overbright = ws.overbright;
    info.fogMode = ws.fogMode;
    info.fogStart = ws.fogStart;
    info.fogEnd = ws.fogEnd;
    info.fogDensity = ws.fogDensity;
    for (int i = 0; i < 3; ++i) {
        info.fogColor[i] = ws.fogColor[i];
        info.ambient[i] = ws.ambient[i];
    }
    info.farClip = ws.farClip;
    info.detailTex = ws.detailTex;
    info.detailTileU = ws.detailTileU;
    info.detailTileV = ws.detailTileV;
    info.skyDomeMap = ws.skyDomeMap;
    for (int i = 0; i < 4; ++i) info.skyLayers[i] = ws.skyLayers[i];
    info.skyMap = ws.skyMap;
    info.skyTexture = ws.skyTexture;
    info.skyAngle = ws.skyAngle;
    const size_t slash = ws.mapPath.find_last_of("/\\");
    info.mapFile = slash == std::string::npos ? ws.mapPath : ws.mapPath.substr(slash + 1);

    // The map mesh was loaded by the WORLD.LoadMap native (physics needed it
    // mid-level-load); the renderer reuses the same copy.
    MapMesh fallbackMap;
    const MapMesh* map = engine.map();
    WorldRenderer world;
    bool worldReady = false;
    if (!map && ws.loadRequested) {
        const std::string mapPath = host.ResolvePath(ws.mapPath);
        if (MapMesh::Load(mapPath, fallbackMap)) map = &fallbackMap;
        else LogWarn("map failed: %s (%s)", mapPath.c_str(), fallbackMap.error.c_str());
    }
    if (map) {
        if (world.Init(shaderDir)) {
            world.Upload(*map, textures, MapNameWithoutExtension(info.mapFile), info,
                         &shaderScripts);
            worldReady = true;
        }
    } else if (!ws.loadRequested) {
        LogWarn("the scripts never asked for a map - is '%s' a level?", levelName);
    }

    SkyRenderer sky;
    bool skyReady = false;
    if (sky.Init(shaderDir)) skyReady = sky.Load(root + "/Maps", info, textures);
    LogInfo("sky: %s (dome '%s', %d layers, lowq '%s')",
            skyReady ? (sky.layered() ? "layered" : "lowquality") : "none",
            ws.skyDomeMap.c_str(), ws.skyLayerCount, ws.skyMap.c_str());

    // Corona line-of-sight traces run against the same solid geometry the
    // world is drawn from.
    CollisionMesh collision;
    if (map) collision.Build(*map, ws.scale);

    // The pose the level pushed out through CAM.SetPos/SetAng during load,
    // captured at the play transition above. Reading Lev.Pos here instead
    // would be too late: the mouse is locked by now, so CLevel:Synchronize
    // has started writing the camera INTO Lev.Pos rather than out of it.
    Camera camera;
    if (seated) {
        for (int i = 0; i < 3; ++i) camera.pos[i] = seatPos[i];
        camera.yaw = seatYaw;
        camera.pitch = seatPitch;
    }
    camera.farPlane = info.farClip;
    if (info.fogMode != 0)
        renderer.SetClearColor(info.fogColor[0] / 255.f, info.fogColor[1] / 255.f,
                               info.fogColor[2] / 255.f);

    LogInfo("script world: map %s scale %.2f, %zu entities live",
            info.mapFile.c_str(), info.scale, engine.entities().size());

    auto previous = std::chrono::steady_clock::now();
    const auto startTime = previous;
    int frame = 0;
    bool noclip = false;
    while (window.PumpEvents() && !host.quitRequested()) {
        if (window.TakeResized()) {
            renderer.Resize(window.width(), window.height());
            engine.SetScreenSize(window.width(), window.height());
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        const float elapsed = std::chrono::duration<float>(now - startTime).count();

        // Hand the window's virtual-key state to the scripts' input. Edge
        // detection lives in Input, so this has to run once per frame,
        // before anything asks a question of it.
        input.BeginFrame();
        {
            const bool* keys = window.VirtualKeys();
            for (int vk = 1; vk < Input::kKeyCount; ++vk) input.SetKeyDown(vk, keys[vk]);
            // A wheel notch has no held state; it reads as pressed for the
            // one frame, under the codes Definitions.lua calls
            // MouseWheelForward / MouseWheelBack.
            const int wheel = window.TakeWheelSteps();
            if (wheel > 0) input.PulseKey(Input::kMouseWheelForward);
            if (wheel < 0) input.PulseKey(Input::kMouseWheelBack);
        }

        float dx = 0.f, dy = 0.f;
        window.TakeMouseDelta(dx, dy);
        // Dropped while the menu is up rather than accumulated: the tick that
        // consumes them is paused, so feeding them in would bank a frame's
        // worth of motion per menu frame and snap the view on the way out.
        if (engine.menu().active()) dx = dy = 0.f;
        input.AddMouseDelta(dx, dy);
        if (window.TakeNoclipToggle()) noclip = !noclip;
        // Who steers the view. While the player is walking it is the SCRIPTS:
        // Game:Tick2 calls UpdateViewFromPlayer, which reads MOUSE.GetDelta,
        // accumulates onto CAM.GetRawRotation and writes back through
        // CAM.SetPos/SetAng - so the mouse motion above is consumed there,
        // not here, and the camera adopts the result after the tick. The free
        // camera keeps its own look.
        const bool scriptView =
            engine.playerHandle() != 0 && engine.pawnEnabled() && !noclip;
        if (!scriptView) camera.Look(dx * 0.003f, -dy * 0.003f);
        const float speed = camera.moveSpeed * (window.IsDown(Key::Fast) ? 4.f : 1.f) * dt;
        float fwd = 0.f, right = 0.f, up = 0.f;
        if (window.IsDown(Key::Forward)) fwd += speed;
        if (window.IsDown(Key::Back))    fwd -= speed;
        if (window.IsDown(Key::Right))   right += speed;
        if (window.IsDown(Key::Left))    right -= speed;
        if (window.IsDown(Key::Up))      up += speed;
        if (window.IsDown(Key::Down))    up -= speed;
        const bool walking = scriptView;
        if (walking) {
            // Nothing to do here: the pawn moves inside the tick chain, when
            // CPlayer:Tick calls PLAYER.ExecAction with the action mask it
            // built from these keys. The eye follows the pawn's head once
            // that has run.
        } else if (noclip) {
            camera.Move(fwd, right, up);
            if (engine.playerHandle()) {
                // Keep the pawn with the flying camera, so dropping out of
                // noclip resumes from here rather than across the level.
                pawn.SetHeadPos(camera.pos);
                engine.SyncPlayerFromPawn();
            }
        } else {
            // No player yet: the free camera flies but slides along the
            // world, as in the hand-driven path.
            float f[3], r[3], delta[3];
            camera.Forward(f);
            camera.Right(r);
            for (int c = 0; c < 3; ++c) delta[c] = f[c] * fwd + r[c] * right;
            delta[1] += up;
            physics.SlideSphere(camera.pos, delta, kCameraRadius);
        }
        physics.MoveProbe(camera.pos, !noclip);

        // While the free camera owns the view, the CAM.* reads have to mirror
        // it - the scripts still derive the player's movement basis from them
        // (CPlayer:SetupAction off CAM.GetAngRad). Under the script view the
        // scripts hold that state themselves, and writing it here would fight
        // the accumulation they do in Tick2.
        if (!scriptView) engine.SetCameraPose(camera.pos, camera.yaw, camera.pitch);

        // A hard landing is fall damage, script-side: the same
        // PLAYER_HIT_GROUND PlayerAction queues, on the pawn's own test.
        const float impact = pawn.TakeGroundHit();
        if (impact > 0.f && engine.playerHandle()) {
            const double hitArgs[2] = {double(engine.playerHandle()), double(impact)};
            host.PostMsg("PLAYER_HIT_GROUND", hitArgs, 2);
        }

        // The engine's frame order, from Game.lua's own comments: Tick before
        // physics, Tick2 after physics, Tick3 after the world tick. The
        // player moves inside Game_Tick, when CPlayer:Tick reaches
        // PLAYER.ExecAction, so the mover needs this frame's delta first.
        const double d[1] = {dt};
        engine.SetFrameDelta(dt);

        // Paused freezes the SIMULATION and nothing else: no actor tick, no
        // physics step, no animation. Rendering and the render callbacks carry
        // on below, so the HUD still draws behind the menu and the world stays
        // on screen rather than going black.
        audio.Update();
        if (!engine.gamePaused()) {
            engine.TickAnimations(dt);
            engine.TickMonsters(dt);
            host.CallGlobal("Game_Tick", d, 1);
            physics.Update(dt);
            engine.SyncFromPhysics();
            host.CallGlobal("Game_Tick2", d, 1);
            // Tick2 is where the view is steered, so take the result: the eye
            // rides PO_GetPawnHeadPos less PLAYER.GetCameraFix, at the angles
            // the scripts accumulated from MOUSE.GetDelta.
            if (scriptView) engine.TakeCameraPose(camera.pos, camera.yaw, camera.pitch);
            host.CallGlobal("Game_Tick3", d, 1);
            // Region transitions feed the message pump the way the engine's
            // phantoms do.
            engine.TickTriggers();
            engine.TickLifetimes(dt);
        }
        // Opened before the render callbacks and closed after the world is
        // drawn: everything the scripts ask for lands in one batch, in the
        // order they asked, and is submitted over the finished 3D frame.
        if (hudReady) hud.Begin(Renderer::kHudView, window.width(), window.height());
        host.CallGlobal("Game_Render", d, 1);
        host.CallGlobal("Game_PostRender", d, 1);

        // The menu draws over the HUD, into the same batch, so it lands on top
        // in submission order. Its items were declared by the scripts during
        // whatever action last ran; nothing about it is drawn from Lua.
        // Escape toggles the menu. The original does this natively - nothing in
        // the shipped Lua ever calls PainMenu:OpenMenu, and Cfg.KeyPrimaryMenu
        // ships as "None" - so the key belongs to the engine, and the scripts
        // only observe the transition through those two hooks.
        if (window.TakeEscape()) {
            if (engine.menu().active()) engine.menu().Close();
            else engine.menu().Open();
        }

        // Who owns the mouse. Playing means captured, so the view steers the
        // moment the menu closes: waiting for a click to re-capture leaves the
        // player pointing a dead cursor at the world and wondering why nothing
        // turns. Only while the window has focus, or alt-tabbing away would
        // snatch the pointer straight back.
        const bool menuPointer = engine.menu().active() && engine.menu().mouseShown();
        window.SetAllowCapture(!menuPointer);
        window.SetMouseCaptured(!menuPointer && window.hasFocus());
        // The system cursor is never wanted here: in the menu we draw our own,
        // and in play the mouse is captured. Leaving it to follow the menu
        // state alone made it flash back on for the frame after a resume.
        window.SetSystemCursorVisible(false);
        if (engine.menu().active()) {
            // Keyboard navigation reads the raw virtual keys rather than a
            // UIAction: Definitions.lua's UIActions covers pause, scoreboard
            // and the like, and has no up/down/select - the shipped menu is
            // steered by the engine, not through a binding.
            //
            // Edge-triggered, or one key press walks the whole list.
            static bool navHeld[5] = {};
            const bool* vk = window.VirtualKeys();
            // up, down, enter, left, right
            const int navKeys[5] = {0x26, 0x28, 0x0D, 0x25, 0x27};
            for (int i = 0; i < 5; ++i) {
                const bool down = vk[navKeys[i]];
                if (down && !navHeld[i]) {
                    if (i == 0) engine.menu().NavUp();
                    else if (i == 1) engine.menu().NavDown();
                    else if (i == 2) engine.menu().NavActivate();
                    else engine.menu().NavAdjust(i == 3 ? -1 : 1);
                }
                navHeld[i] = down;
            }

            input.SetMousePos(window.mouseX(), window.mouseY());
            // A freshly built screen has no highlight until something moves;
            // seat it on the first row so the keyboard works immediately.
            engine.menu().FocusFirst();
            engine.menu().Update(window.mouseX(), window.mouseY(), window.TakeLeftClick());
            engine.menu().Draw(window.width(), window.height());
        }
        host.CallGlobal("Game_GC", nullptr, 0);
        // Entities the scripts spawned this frame get their renderer slots.
        engine.FlushToRenderer();

        renderer.BeginFrame();
        if (skyReady)
            sky.Draw(Renderer::kSkyView, camera, window.width(), window.height(), elapsed);
        if (worldReady)
            world.Draw(Renderer::kWorldView, camera, window.width(), window.height(),
                       info, elapsed);
        entities.Draw(Renderer::kWorldView, camera, window.width(), window.height(),
                      info, elapsed);
        // Particles then coronas last, exactly as in the hand-driven loop:
        // blended, no depth writes, and coronas ignore depth entirely.
        // Paused stops the SIMULATION but not the drawing, here as everywhere
        // else: the effects keep their last frame on screen rather than
        // vanishing behind the menu, but they stop advancing. Ticking these
        // from the render section is what let them keep running when the rest
        // of the world had already stopped.
        const float simDt = engine.gamePaused() ? 0.f : dt;
        if (particlesReady) {
            particles.Tick(simDt);
            particles.Draw(Renderer::kWorldView, camera, window.width(), window.height());
        }
        if (billboardsReady) {
            billboards.Update(camera, simDt, collision);
            billboards.Draw(Renderer::kWorldView, camera);
        }

        if (hudReady) hud.End();

        renderer.DebugText(1, "PainfulEngine (script-driven)  -  %s  -  %.1f fps",
                           renderer.BackendName().c_str(), dt > 0.f ? 1.f / dt : 0.f);
        renderer.DebugText(2, "%s   map %s   %zu script entities (%zu created, %zu released)",
                           levelName, info.mapFile.c_str(), engine.entities().size(),
                           engine.created(), engine.released());
        renderer.DebugText(7, "hud: %s, %zu quads in %zu draws, %zu fonts baked",
                           hudReady ? "on" : "OFF", hud.quadsThisFrame(), hud.drawCalls(),
                           hud.fonts().baked());
        renderer.DebugText(3, "%zu world draws, %zu entity draws (%zu skinned), zones %zu/%zu",
                           worldReady ? world.drawCalls() : 0, entities.drawCalls(),
                           entities.posedInstances(),
                           worldReady ? world.zonesVisible() : 0,
                           worldReady ? world.zoneCount() : 0);
        renderer.DebugText(4, "pos %.1f %.1f %.1f   rot %.2f %.2f   %s", camera.pos[0],
                           camera.pos[1], camera.pos[2], camera.yaw, camera.pitch,
                           walking ? (pawn.onGround() ? "walking" : "airborne")
                                   : (engine.playerHandle() ? "flying (N to walk)"
                                                            : "no player"));
        renderer.DebugText(5, "physics: %s, %zu static tris, %zu bodies, gravity %.2f   |   "
                              "%zu particles in %zu emitters, %zu/%zu billboards",
                           noclip ? "camera NOCLIP" : "camera collides",
                           physics.staticTriangles(), physics.bodyCount(),
                           physics.settings().gravity,
                           particles.liveParticles(), particles.emitters(),
                           billboards.visible(), billboards.placed());
        renderer.DebugText(6, "%s - %s, esc for the menu",
                           window.mouseCaptured() ? "mouse captured" : "menu",
                           walking ? "WASD walk, space jump, mouse look, N to fly"
                                   : "WASD move, shift fast, space/ctrl up-down, N to walk");
        renderer.EndFrame();

        if (!shotPath.empty()) {
            ++frame;
            int shotFrame = 30;
            if (const char* e = getenv("PAINFUL_SHOT_FRAME")) shotFrame = std::atoi(e);
            if (frame == shotFrame) {
                renderer.RequestScreenshot(shotPath);
                // The numbers behind the picture, so a shot can be judged
                // without opening it.
                std::string posed;
                for (const std::string& m : entities.posedModels())
                    posed += (posed.empty() ? "" : ", ") + m;
                LogInfo("frame %d: %zu entity draws, %zu of them CPU-skinned, "
                        "%zu script entities", frame, entities.drawCalls(),
                        entities.posedInstances(), engine.entities().size());
                LogInfo("  posing: %s", posed.empty() ? "(nothing)" : posed.c_str());
                LogInfo("  audio: %zu playing, %zu started, %zu reaped, %zu samples, %zu missing",
                        audio.voicesPlaying(), audio.voicesStarted(), audio.voicesReaped(),
                        audio.samplesLoaded(), audio.samplesMissing());
                LogInfo("  hud: %s, %zu quads in %zu draws, %zu fonts baked",
                        hudReady ? "on" : "OFF", hud.quadsThisFrame(), hud.drawCalls(),
                        hud.fonts().baked());
                LogInfo("  particles: %zu live in %zu emitters", particles.liveParticles(),
                        particles.emitters());
                LogInfo("  camera %.2f %.2f %.2f, player %s", camera.pos[0],
                        camera.pos[1], camera.pos[2],
                        walking ? (pawn.onGround() ? "on the ground" : "airborne")
                                : "flying");
            }
            if (frame >= shotFrame + 4) break;
        }
    }
    LogInfo("game loop ended: %s", host.quitRequested() ? "scripts called Exit()"
                                                        : "window closed");
    return 0;
}

// Lists the levels available for the in-engine selector.
static int LevelsCmd(const char* dataRoot) {
    std::vector<std::string> dirs;
    for (const DirEntry& entry : FileSystem::Get().List(std::string(dataRoot) + "/Levels")) {
        if (entry.isDirectory) dirs.push_back(entry.name);
    }
    std::sort(dirs.begin(), dirs.end());
    for (size_t i = 0; i < dirs.size(); ++i) {
        LogInfo("  [%2zu] %s", i + 1, dirs[i].c_str());
    }
    LogInfo("");
    LogInfo("%zu levels. Cycle them in-engine with the [ and ] keys.", dirs.size());
    return 0;
}

// Diagnostic: dump a sky dome mesh - object names and their materials.
// Diagnostic: parse a .dat item pack, or with a directory, validate every pack.
static int DatCmd(const char* path) {
    namespace fs = std::filesystem;
    if (FileSystem::Get().IsDirectory(path)) {
        size_t ok = 0, bad = 0;
        for (const std::string& rel : FileSystem::Get().ListRecursive(path)) {
            if (fs::path(rel).extension() != ".dat") continue;
            DatPack pack;
            if (DatPack::Load(std::string(path) + "/" + rel, pack)) {
                ++ok;
            } else {
                ++bad;
                LogInfo("FAIL %-40s %s", fs::path(rel).filename().string().c_str(),
                        pack.error.c_str());
            }
        }
        LogInfo("%zu parsed, %zu failed", ok, bad);
        return bad == 0 ? 0 : 3;
    }

    DatPack pack;
    if (!DatPack::Load(path, pack)) {
        LogInfo("%s: %s", path, pack.error.c_str());
        return 2;
    }
    LogInfo("%s: mesh %s, %zu objects", path, pack.meshName.c_str(), pack.objects.size());
    for (const MapObject& o : pack.objects) {
        const Mat4& t = o.transform;
        const bool ident = t.m[0] == 1.f && t.m[5] == 1.f && t.m[10] == 1.f &&
                           t.m[1] == 0.f && t.m[2] == 0.f && t.m[4] == 0.f;
        LogInfo("  %-28s %5zu verts %5zu tris  diffuse=%s  bbox %.2fx%.2fx%.2f  xform %s t=(%.2f %.2f %.2f)",
                o.name.c_str(), o.vertexCount(), o.triangleCount(),
                o.materials.empty() ? "" : o.materials[0].diffuse().c_str(),
                o.bboxMax[0] - o.bboxMin[0], o.bboxMax[1] - o.bboxMin[1],
                o.bboxMax[2] - o.bboxMin[2],
                ident ? "identity" : "ROTATED", t.m[12], t.m[13], t.m[14]);
        LogInfo("      y range [%.2f .. %.2f]  x [%.2f .. %.2f]  z [%.2f .. %.2f]",
                o.bboxMin[1], o.bboxMax[1], o.bboxMin[0], o.bboxMax[0],
                o.bboxMin[2], o.bboxMax[2]);
    }
    return 0;
}

// Diagnostic: parse the game's material scripts and dump what they define.
static int ShadersCmd(const char* dataRoot, const char* single) {
    ShaderLibrary lib;
    if (!lib.LoadDirectory(std::string(dataRoot) + "/Shaders/Scripts")) {
        for (const std::string& e : lib.errors()) LogInfo("  error: %s", e.c_str());
        if (lib.size() == 0) return 2;
    }
    LogInfo("%zu shader definitions parsed", lib.size());
    for (const std::string& e : lib.errors()) LogInfo("  error: %s", e.c_str());

    if (single && single[0]) {
        const ShaderDef* def = lib.Find(single);
        if (!def) { LogInfo("'%s' not found", single); return 2; }
        LogInfo("shader %s  variant=%s  (%s)", def->name.c_str(),
                def->variant.empty() ? "any" : def->variant.c_str(),
                def->sourceFile.c_str());
        for (size_t p = 0; p < def->passes.size(); ++p) {
            LogInfo("  pass %zu:", p);
            for (const auto& kv : def->passes[p].keys) {
                LogInfo("    %-14s %s", kv.first.c_str(), kv.second.c_str());
            }
        }
        return 0;
    }

    // Summary: names per variant, and every distinct vshader/fshader/fx used.
    std::map<std::string, int> variants;
    std::map<std::string, int> programs;
    for (const ShaderDef& d : lib.all()) {
        ++variants[d.variant.empty() ? "any" : d.variant];
        for (const ShaderPass& p : d.passes) {
            for (const char* key : {"vshader", "fshader", "fx"}) {
                std::string v = p.Get(key);
                if (!v.empty()) ++programs[std::string(key) + " " + v];
            }
        }
    }
    for (const auto& kv : variants) LogInfo("  %-6s %d", kv.first.c_str(), kv.second);
    LogInfo("programs referenced:");
    for (const auto& kv : programs) LogInfo("  %3dx  %s", kv.second, kv.first.c_str());
    return 0;
}

static int SkyDumpCmd(const char* path) {
    MapMesh m;
    MapMesh::Load(path, m);
    LogInfo("%s: %zu objects", path, m.objects.size());
    for (size_t i = 0; i < m.objects.size(); ++i) {
        const MapObject& o = m.objects[i];
        LogInfo("  [%zu] name=%-28s uv=%u verts=%-5zu tris=%-5zu mats=%zu bbox x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f]",
                i, o.name.c_str(), o.uvChannels, o.vertexCount(), o.triangleCount(),
                o.materials.size(), o.bboxMin[0], o.bboxMax[0], o.bboxMin[1], o.bboxMax[1],
                o.bboxMin[2], o.bboxMax[2]);
        // Float 3 of the 2-UV layout is unaccounted for; if it is a vertex
        // alpha the dome shells would use it to feather their rims.
        if (o.uvChannels == 2 && o.vertexCount() > 0) {
            float lo3 = 1e30f, hi3 = -1e30f;
            for (size_t vi = 0; vi < o.vertexCount(); ++vi) {
                const float f3 = o.verts[vi * 8 + 3];
                lo3 = std::min(lo3, f3);
                hi3 = std::max(hi3, f3);
            }
            LogInfo("        float3 range [%g .. %g]", lo3, hi3);
            float u0l = 1e30f, u0h = -1e30f, v0l = 1e30f, v0h = -1e30f;
            float u1l = 1e30f, u1h = -1e30f, v1l = 1e30f, v1h = -1e30f;
            for (size_t vi = 0; vi < o.vertexCount(); ++vi) {
                float a[2], b[2];
                o.uv(vi, a);
                o.uv1(vi, b);
                u0l = std::min(u0l, a[0]); u0h = std::max(u0h, a[0]);
                v0l = std::min(v0l, a[1]); v0h = std::max(v0h, a[1]);
                u1l = std::min(u1l, b[0]); u1h = std::max(u1h, b[0]);
                v1l = std::min(v1l, b[1]); v1h = std::max(v1h, b[1]);
            }
            LogInfo("        uv0 u[%.2f..%.2f] v[%.2f..%.2f]  uv1 u[%.2f..%.2f] v[%.2f..%.2f]",
                    u0l, u0h, v0l, v0h, u1l, u1h, v1l, v1h);
            // Correlate elevation with UV1 radius, to see which end of the
            // mask gradient lands on the shell apex.
            float apexY = -1e30f, rimY = 1e30f;
            size_t apexI = 0, rimI = 0;
            for (size_t vi = 0; vi < o.vertexCount(); ++vi) {
                const float y = o.verts[vi * 8 + 1];
                if (y > apexY) { apexY = y; apexI = vi; }
                if (y < rimY) { rimY = y; rimI = vi; }
            }
            float ua[2], ur[2];
            o.uv1(apexI, ua);
            o.uv1(rimI, ur);
            LogInfo("        apex y=%.1f uv1=(%.2f,%.2f) r=%.2f | rim y=%.1f uv1=(%.2f,%.2f) r=%.2f",
                    apexY, ua[0], ua[1],
                    std::sqrt((ua[0]-0.5f)*(ua[0]-0.5f) + (ua[1]-0.5f)*(ua[1]-0.5f)),
                    rimY, ur[0], ur[1],
                    std::sqrt((ur[0]-0.5f)*(ur[0]-0.5f) + (ur[1]-0.5f)*(ur[1]-0.5f)));
        }
        for (const Material& mat : o.materials) {
            LogInfo("        slot0=%-26s slot1=%-26s slot2=%-16s slot3=%s",
                    mat.slots[0].name.c_str(), mat.slots[1].name.c_str(),
                    mat.slots[2].name.c_str(), mat.slots[3].name.c_str());
            for (int s = 0; s < 4; ++s) {
                const TextureSlot& t = mat.slots[s];
                if (t.empty()) continue;
                LogInfo("          xform[%d] offset(%.3f %.3f) scale(%.3f %.3f)  %s",
                        s, t.offsetU, t.offsetV, t.scaleU, t.scaleV, t.name.c_str());
            }
        }
    }
    return 0;
}

// Diagnostic: bring up the physics world headlessly and probe it.
//
// Numbers, not a screenshot: what the level put into Jolt, and where a sphere
// pushed along each axis from the spawn actually ends up. A camera that
// collides is a camera whose sphere stops short of what it asked for.
static int PhysicsCmd(const char* levelDir, const char* dataRoot) {
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
static int ZonesCmd(const char* levelDir, const char* dataRoot,
                    const float* pos) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    const float ws = level.info().scale;
    ZoneGraph graph;
    graph.Build(level.map(), ws);
    LogInfo("%zu zones, %zu portals (world scale %.2f)", graph.zoneCount(),
            graph.portalCount(), ws);
    graph.Dump(ws);
    if (pos) {
        const float raw[3] = {pos[0] / ws, pos[1] / ws, pos[2] / ws};
        std::vector<int> zs;
        graph.ZonesAt(raw, zs);
        std::string s;
        for (int z : zs) s += std::to_string(z) + " ";
        LogInfo("point (%.1f %.1f %.1f) raw (%.1f %.1f %.1f) in zones: %s",
                pos[0], pos[1], pos[2], raw[0], raw[1], raw[2],
                s.empty() ? "(none)" : s.c_str());
    }
    return 0;
}

static int GroundCmd(const char* levelDir, const char* dataRoot,
                     float x, float y, float z, float radius) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    float best = -1e30f;
    const float r2 = radius * radius;
    // Compare in the same space the renderer draws: world mesh times o.Scale.
    const float ws = level.info().scale;
    for (const MapObject& o : level.map().objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3], w[3];
            o.position(i, p);
            o.transform.TransformPoint(p[0], p[1], p[2], w);
            w[0] *= ws; w[1] *= ws; w[2] *= ws;
            const float dx = w[0] - x, dz = w[2] - z;
            if (dx * dx + dz * dz > r2 || w[1] > y) continue;
            if (w[1] > best) best = w[1];
        }
    }
    if (best < -1e29f) LogInfo("(%.2f, %.2f, %.2f): no ground below", x, y, z);
    else LogInfo("(%.2f, %.2f, %.2f): ground y=%.2f, drop %.2f", x, y, z, best, y - best);
    return 0;
}

static int ScaleCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    const float* start = level.info().startPos;

    // Nearest vertex below the spawn, within a small horizontal radius.
    double bestDrop = 1e30;
    float floorY = 0.f;
    for (const MapObject& o : level.map().objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3];
            o.position(i, p);
            const double dx = p[0] - start[0], dz = p[2] - start[2];
            if (dx * dx + dz * dz > 25.0) continue;      // within 5 units horizontally
            if (p[1] > start[1]) continue;               // must be below
            const double drop = start[1] - p[1];
            if (drop < bestDrop) { bestDrop = drop; floorY = p[1]; }
        }
    }
    LogInfo("%s", level.name().c_str());
    LogInfo("  level Scale      : %g", level.info().scale);
    LogInfo("  spawn            : (%.2f, %.2f, %.2f)", start[0], start[1], start[2]);
    if (bestDrop > 1e29) {
        LogInfo("  no floor found within 5 units horizontally of the spawn");
        return 0;
    }
    LogInfo("  floor below      : y = %.2f", floorY);
    LogInfo("  camera height    : %.2f units", bestDrop);
    // A first-person camera sits around 1.65 m above the floor.
    LogInfo("  => units per metre: %.2f   (assuming a 1.65 m eye height)", bestDrop / 1.65);
    return 0;
}

// Diagnostic: do the bone bind matrices carry a scale the raw mesh lacks?
// Cross-fades one animation into another and reports where a bone ends up at
// each weight. This is how the blend MDL.SetAnim asks for gets checked: the
// intermediate poses must move monotonically from one animation to the other,
// and a bone must not change length on the way.
static int BlendCmd(const char* path, const char* animA, const char* animB,
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
static int WpsCmd(const char* path) {
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
static int SoundCmd(const char* root, const char* name, const char* seconds) {
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

static int BonesCmd(const char* path, const char* animName, const char* timeArg,
                    const char* rotArg) {
    Model m;
    if (!Model::Load(path, m)) { LogInfo("failed"); return 2; }
    LogInfo("%s: %zu bones", path, m.bones.size());
    for (size_t i = 0; i < m.bones.size() && i < 6; ++i) {
        const Bone& b = m.bones[i];
        const float* v = b.bind.m;
        double sx = std::sqrt(double(v[0])*v[0] + double(v[1])*v[1] + double(v[2])*v[2]);
        double sy = std::sqrt(double(v[4])*v[4] + double(v[5])*v[5] + double(v[6])*v[6]);
        double sz = std::sqrt(double(v[8])*v[8] + double(v[9])*v[9] + double(v[10])*v[10]);
        LogInfo("  [%zu] %-22s parent=%-3d scale(%.4f, %.4f, %.4f) trans(%.2f, %.2f, %.2f)",
                i, b.name.c_str(), b.parent, sx, sy, sz, v[12], v[13], v[14]);
    }
    // Compare the extent of the composed skeleton with the extent of the mesh.
    // These must agree; if they do not, the mesh is not in skeleton space.
    std::vector<Mat4> bindWorld, invBind;
    ComputeBindWorld(m.bones, bindWorld, invBind);
    double blo[3] = {1e30, 1e30, 1e30}, bhi[3] = {-1e30, -1e30, -1e30};
    for (const Mat4& w : bindWorld) {
        for (int c = 0; c < 3; ++c) {
            const double v = w.m[12 + c];
            if (v < blo[c]) blo[c] = v;
            if (v > bhi[c]) bhi[c] = v;
        }
    }
    double mlo[3] = {1e30, 1e30, 1e30}, mhi[3] = {-1e30, -1e30, -1e30};
    for (const ModelMesh& mesh : m.meshes) {
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            for (int c = 0; c < 3; ++c) {
                const double v = mesh.verts[i * 8 + c];
                if (v < mlo[c]) mlo[c] = v;
                if (v > mhi[c]) mhi[c] = v;
            }
        }
    }
    LogInfo("  skeleton extent : %.2f x %.2f x %.2f",
            bhi[0]-blo[0], bhi[1]-blo[1], bhi[2]-blo[2]);
    LogInfo("  mesh extent     : %.2f x %.2f x %.2f",
            mhi[0]-mlo[0], mhi[1]-mlo[1], mhi[2]-mlo[2]);
    // Named joints, posed. This is the joint natives' own arithmetic - what
    // MDL.GetJointPos answers before the entity transform is applied - so a
    // bone that ends up in the wrong place shows here rather than only as a
    // muzzle flash in the wrong spot.
    if (animName) {
        std::string dir = path, base = path;
        const size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) { dir = base.substr(0, slash); base = base.substr(slash + 1); }
        else dir = ".";
        const size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);

        AnimationCache cache;
        cache.SetRoot(dir);
        const Animation* anim = cache.Get(base, animName);
        if (!anim) {
            LogInfo("  no animation %s.%s.ani in %s", base.c_str(), animName, dir.c_str());
            return 0;
        }
        std::vector<Bone> bones = m.bones;
        BuildHierarchy(bones);
        std::vector<Mat4> bw, ib;
        ComputeBindWorld(bones, bw, ib);
        std::vector<const AnimTrack*> tracks;
        ResolveAnimTracks(bones, *anim, tracks);
        const float t = timeArg ? float(std::atof(timeArg)) : anim->duration() * 0.5f;
        std::vector<Mat4> posed;
        ComputeBoneWorldAtTime(bones, tracks, t, posed);

        // "<joint>:<ax>,<ay>,<az>" applies MDL.ApplyJointRotation's own
        // override and reports which bones moved. A rotation at one joint must
        // move that bone's descendants and NOTHING else - the check that it
        // turns where it sits rather than swinging about its parent.
        if (rotArg) {
            JointOverride ov;
            if (std::sscanf(rotArg, "%d:%f,%f,%f", &ov.bone, &ov.euler[0], &ov.euler[1],
                            &ov.euler[2]) == 4) {
                std::vector<Mat4> turned;
                ComputeBoneWorldAtTime(bones, tracks, t, turned, &ov, 1);
                LogInfo("  joint %d turned by (%.3f %.3f %.3f) rad:", ov.bone,
                        ov.euler[0], ov.euler[1], ov.euler[2]);
                for (size_t i = 0; i < bones.size(); ++i) {
                    float d = 0.f;
                    for (int c = 0; c < 3; ++c)
                        d = std::max(d, std::fabs(turned[i].m[12 + c] - posed[i].m[12 + c]));
                    if (d > 1e-4f)
                        LogInfo("    [%2zu] %-20s parent=%-3d moved %.3f",
                                i, bones[i].name.c_str(), bones[i].parent, d);
                }
                return 0;
            }
            LogInfo("  could not read \"%s\" as <joint>:<ax>,<ay>,<az>", rotArg);
        }

        LogInfo("  joints at t=%.3f of %.3f, model space (bind -> posed):", t, anim->duration());
        // The root chain is what a reader needs: it must rise monotonically in
        // both columns, because a spine is a spine in any pose. A 111-bone
        // weapon would bury that under its own hardware.
        const size_t kShown = 12;
        for (size_t i = 0; i < bones.size() && i < kShown; ++i)
            LogInfo("    [%2zu] %-20s (%7.2f %7.2f %7.2f) -> (%7.2f %7.2f %7.2f)",
                    i, bones[i].name.c_str(),
                    bw[i].m[12], bw[i].m[13], bw[i].m[14],
                    posed[i].m[12], posed[i].m[13], posed[i].m[14]);
        if (bones.size() > kShown)
            LogInfo("    ... and %zu more not shown", bones.size() - kShown);
    }

    if (bhi[1] - blo[1] > 1e-6) {
        LogInfo("  mesh / skeleton height ratio: %.3f", (mhi[1]-mlo[1]) / (bhi[1]-blo[1]));
    }
    return 0;
}


// Diagnostic: do all four textures of every sky layer resolve to a file?
static int SkyTexCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);

    const LevelInfo& info = level.info();
    int missing = 0, present = 0;
    LogInfo("%-22s dome=%s", level.name().c_str(), info.skyDomeMap.c_str());
    for (int i = 0; i < 4; ++i) {
        const SkyLayer& L = info.skyLayers[i];
        if (!L.valid()) continue;
        const std::string names[4] = {L.tex1.name, L.tex2.name, L.mask, L.lightmap};
        const char* kind[4] = {"tex1", "tex2", "mask", "lmap"};
        for (int t = 0; t < 4; ++t) {
            if (names[t].empty()) continue;
            const bool ok = !cache.Resolve(names[t], "").empty();
            if (ok) ++present; else ++missing;
            if (!ok) LogInfo("    layer%d %s MISSING: %s", i + 1, kind[t], names[t].c_str());
        }
    }
    LogInfo("    textures resolved %d, missing %d", present, missing);
    return missing == 0 ? 0 : 3;
}

// Diagnostic: what do the placeholder texture names resolve to?
// Diagnostic: decode any game texture to BGRA and report its border/centre
// values, or write it out as a TGA when an output path is given.
static int TexDumpCmd(const char* dataRoot, const char* name, const char* outPath) {
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);
    const std::string path = cache.Resolve(name, "");
    if (path.empty()) { LogInfo("unresolved: %s", name); return 2; }

    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) { LogInfo("unreadable: %s", path.c_str()); return 2; }
    bimg::ImageContainer* image =
        bimg::imageParse(&painful::g_allocator, data.data(), uint32_t(data.size()));
    if (!image) { LogInfo("parse failed"); return 2; }

    const uint32_t w = image->m_width, h = image->m_height;
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    bimg::imageDecodeToBgra8(&painful::g_allocator, rgba.data(), image->m_data, w, h, w * 4,
                             image->m_format);
    auto px = [&](uint32_t x, uint32_t y) {
        const uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
        LogInfo("  (%4u,%4u)  B %3u  G %3u  R %3u  A %3u", x, y, p[0], p[1], p[2], p[3]);
    };
    LogInfo("%s  %ux%u fmt %d", path.c_str(), w, h, int(image->m_format));
    px(0, 0); px(w / 2, 0); px(w - 1, 0);
    px(0, h / 2); px(w / 2, h / 2); px(w - 1, h / 2);
    px(0, h - 1); px(w / 2, h - 1); px(w - 1, h - 1);

    if (outPath && outPath[0]) {
        // Minimal uncompressed BGRA TGA.
        std::vector<uint8_t> tga(18, 0);
        tga[2] = 2; tga[12] = uint8_t(w); tga[13] = uint8_t(w >> 8);
        tga[14] = uint8_t(h); tga[15] = uint8_t(h >> 8); tga[16] = 32; tga[17] = 0x28;
        tga.insert(tga.end(), rgba.begin(), rgba.end());
        FILE* f = fopen(outPath, "wb");
        if (f) { fwrite(tga.data(), 1, tga.size(), f); fclose(f); LogInfo("wrote %s", outPath); }
    }
    bimg::imageFree(image);
    return 0;
}

static int ResolveCmd(const char* dataRoot, const char* name) {
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);
    const std::string hit = cache.Resolve(name, "");
    LogInfo("  %-16s -> %s", name, hit.empty() ? "(UNRESOLVED)" : hit.c_str());
    return 0;
}

// Writes a complete, loadable level from nothing but numbers.
//
// This is the smallest thing that proves a level is authorable from code. A
// level is almost entirely plain Lua text - the settings file, the class
// script and every placed light, item and spawn point - so the only binary in
// the way was the world mesh, and MapMesh::Write closes that.
//
// The floor is a grid rather than two big triangles: physics does not care,
// but a single quad gives the renderer nothing to interpolate across and the
// result reads as a flat card rather than a surface.
static int MkLevelCmd(const char* dataRoot, const char* levelName, float extent,
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

static int MapCmd(const char* path) {
    MapMesh m;
    MapMesh::Load(path, m);
    size_t verts = 0, tris = 0;
    for (const MapObject& o : m.objects) { verts += o.vertexCount(); tris += o.triangleCount(); }
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const MapObject& o : m.objects) {
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3];
            o.position(i, p);
            for (int c = 0; c < 3; ++c) { if (p[c] < lo[c]) lo[c] = p[c]; if (p[c] > hi[c]) hi[c] = p[c]; }
        }
    }
    LogInfo("%s: %zu objects, %zu verts, %zu tris, terminator %s",
            path, m.objects.size(), verts, tris, m.terminated ? "OK" : "MISSING");
    LogInfo("  bounds x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]  (size %.1f x %.1f x %.1f)",
            lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
            hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]);
    LogInfo("  parse stopped at 0x%zx of 0x%zx (%zu bytes unread)", m.parseEnd, m.size, m.size - m.parseEnd);
    LogInfo("  parser skipped %zu bytes across %zu regions", m.skippedBytes, m.skippedRegions);

    // Which way the exporter winds its triangles, measured rather than assumed:
    // for each triangle, does cross(b-a, c-a) agree with the vertex normals or
    // oppose them? PhysicsWorld reverses the winding on the strength of this,
    // and anything that WRITES a .mpk has to match it or the floor comes out
    // one-sided the wrong way.
    size_t agree = 0, oppose = 0;
    for (const MapObject& o : m.objects) {
        for (size_t t = 0; t + 2 < o.indices.size(); t += 3) {
            const uint32_t ia = o.indices[t], ib = o.indices[t + 1], ic = o.indices[t + 2];
            if (ia >= o.vertexCount() || ib >= o.vertexCount() || ic >= o.vertexCount()) continue;
            float a[3], b[3], c[3], n[3];
            o.position(ia, a); o.position(ib, b); o.position(ic, c);
            o.normal(ia, n);
            const float u[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
            const float v[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
            const float g[3] = {u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2],
                                u[0]*v[1] - u[1]*v[0]};
            const float d = g[0]*n[0] + g[1]*n[1] + g[2]*n[2];
            if (d > 0.f) ++agree; else if (d < 0.f) ++oppose;
        }
    }
    LogInfo("  winding: %zu triangles agree with their vertex normals, %zu oppose",
            agree, oppose);
    size_t nonIdentity = 0;
    std::map<std::string, size_t> translations;
    for (const MapObject& o : m.objects) {
        const Mat4& t = o.transform;
        const bool ident = t.m[0] == 1.f && t.m[5] == 1.f && t.m[10] == 1.f &&
                           t.m[12] == 0.f && t.m[13] == 0.f && t.m[14] == 0.f;
        if (ident) continue;
        ++nonIdentity;
        char buf[96];
        snprintf(buf, sizeof buf, "(%.1f, %.1f, %.1f) diag(%.2f %.2f %.2f)",
                 t.m[12], t.m[13], t.m[14], t.m[0], t.m[5], t.m[10]);
        ++translations[buf];
    }
    LogInfo("  %zu objects with non-identity transform", nonIdentity);
    size_t noMats = 0, noMatTris = 0, totalTris = 0;
    for (const MapObject& o : m.objects) {
        totalTris += o.triangleCount();
        if (o.materials.empty()) { ++noMats; noMatTris += o.triangleCount(); }
    }
    LogInfo("  %zu objects have NO parsed materials (%zu of %zu tris, %.1f%%)",
            noMats, noMatTris, totalTris, totalTris ? 100.0 * noMatTris / totalTris : 0.0);
    for (const auto& kv : translations) {
        if (kv.second > 2) LogInfo("    %zux %s", kv.second, kv.first.c_str());
    }
    std::vector<std::pair<size_t, size_t>> bySize(m.skips);
    std::sort(bySize.begin(), bySize.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < bySize.size() && i < 8; ++i) {
        LogInfo("    skip %zu bytes at offset 0x%zx", bySize[i].second, bySize[i].first);
    }
    return m.terminated ? 0 : 3;
}

// Diagnostic: how are the four texture slots actually populated?
static int MatsCmd(const char* path, const char* nameFilter) {
    MapMesh m;
    MapMesh::Load(path, m);

    // With a name, report that object in full instead of the whole-map summary:
    // every slot with its UV transform, plus the raw UV span of the geometry.
    // Those two together are what decides how many times a texture repeats.
    if (nameFilter && *nameFilter) {
        std::string want = nameFilter;
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        size_t found = 0;
        for (const MapObject& o : m.objects) {
            std::string lower = o.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (lower.find(want) == std::string::npos) continue;
            ++found;
            float lo0[2] = {1e30f, 1e30f}, hi0[2] = {-1e30f, -1e30f};
            float lo1[2] = {1e30f, 1e30f}, hi1[2] = {-1e30f, -1e30f};
            for (size_t i = 0; i < o.vertexCount(); ++i) {
                float uv[2];
                o.uv(i, uv);
                for (int c = 0; c < 2; ++c) {
                    lo0[c] = std::min(lo0[c], uv[c]);
                    hi0[c] = std::max(hi0[c], uv[c]);
                }
                o.uv1(i, uv);
                for (int c = 0; c < 2; ++c) {
                    lo1[c] = std::min(lo1[c], uv[c]);
                    hi1[c] = std::max(hi1[c], uv[c]);
                }
            }
            LogInfo("%s  (%zu verts, %zu tris, uvChannels %u)", o.name.c_str(),
                    o.vertexCount(), o.triangleCount(), o.uvChannels);
            LogInfo("  bounds raw x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]",
                    o.bboxMin[0], o.bboxMax[0], o.bboxMin[1], o.bboxMax[1],
                    o.bboxMin[2], o.bboxMax[2]);
            LogInfo("  texcoord0 u[%.3f..%.3f] v[%.3f..%.3f]   span %.2f x %.2f",
                    lo0[0], hi0[0], lo0[1], hi0[1], hi0[0] - lo0[0], hi0[1] - lo0[1]);
            LogInfo("  texcoord1 u[%.3f..%.3f] v[%.3f..%.3f]   span %.2f x %.2f",
                    lo1[0], hi1[0], lo1[1], hi1[1], hi1[0] - lo1[0], hi1[1] - lo1[1]);
            // The raw 8 floats, so a suspected channel mix-up can be judged
            // rather than argued about: index 3 has no documented meaning on
            // 2-UV objects.
            LogInfo("  raw vertex floats (first 4 verts):");
            for (size_t i = 0; i < o.vertexCount() && i < 4; ++i) {
                LogInfo("    [%zu] %8.3f %8.3f %8.3f | %8.3f | %8.3f %8.3f | %8.3f %8.3f",
                        i, o.verts[i * 8 + 0], o.verts[i * 8 + 1], o.verts[i * 8 + 2],
                        o.verts[i * 8 + 3], o.verts[i * 8 + 4], o.verts[i * 8 + 5],
                        o.verts[i * 8 + 6], o.verts[i * 8 + 7]);
            }
            for (const Material& mat : o.materials) {
                for (int s = 0; s < 4; ++s) {
                    const TextureSlot& t = mat.slots[s];
                    if (t.name.empty()) continue;
                    LogInfo("  slot%d %-22s off(%.3f,%.3f) scale(%.3f,%.3f)  -> repeats %.1f x %.1f",
                            s, t.name.c_str(), t.offsetU, t.offsetV, t.scaleU, t.scaleV,
                            (hi0[0] - lo0[0]) * t.scaleU, (hi0[1] - lo0[1]) * t.scaleV);
                }
            }
        }
        if (!found) LogInfo("no object matching '%s'", nameFilter);
        return 0;
    }
    size_t total = 0, slot0empty = 0, slot1empty = 0, bothFilled = 0, slot0LooksLightmap = 0;
    size_t nonIdentity = 0;
    int shown = 0;
    LogInfo("materials with a non-identity slot0 UV transform:");
    for (const MapObject& o : m.objects) {
        for (const Material& mat : o.materials) {
            ++total;
            const std::string& s0 = mat.slots[0].name;
            const std::string& s1 = mat.slots[1].name;
            if (s0.empty()) ++slot0empty;
            if (s1.empty()) ++slot1empty;
            if (!s0.empty() && !s1.empty()) ++bothFilled;
            // Lightmaps are named after the mesh plus a lightmap suffix.
            if (s0.find("_L_") != std::string::npos) ++slot0LooksLightmap;
            const TextureSlot& t0 = mat.slots[0];
            bool identity = (t0.offsetU == 0.f && t0.offsetV == 0.f &&
                             t0.scaleU == 1.f && t0.scaleV == 1.f);
            if (!identity) {
                ++nonIdentity;
                if (shown++ < 10)
                    LogInfo("%-30s %-18s off(%.3f,%.3f) scale(%.3f,%.3f)",
                            o.name.c_str(), t0.name.c_str(),
                            t0.offsetU, t0.offsetV, t0.scaleU, t0.scaleV);
            }
        }
    }
    LogInfo("");
    LogInfo("materials %zu | slot0 empty %zu | slot1 empty %zu | both filled %zu",
            total, slot0empty, slot1empty, bothFilled);
    LogInfo("slot0 contains a lightmap-looking name (_L_): %zu", slot0LooksLightmap);
    LogInfo("materials whose slot0 UV transform is NOT identity: %zu", nonIdentity);
    size_t uv1WithLightmap = 0, uv1Total = 0, uv2Total = 0;
    for (const MapObject& o : m.objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (const Material& mat : o.materials) {
            if (o.uvChannels == 1) {
                ++uv1Total;
                if (!mat.slots[1].name.empty()) ++uv1WithLightmap;
            } else ++uv2Total;
        }
    }
    LogInfo("materials on 1-UV objects: %zu (of which %zu still name a lightmap)", uv1Total, uv1WithLightmap);
    LogInfo("materials on 2-UV objects: %zu", uv2Total);
    return 0;
}

// Diagnostic: where does every diffuse reference in a map actually resolve?
static int TexturesCmd(const char* mapPath, const char* dataRoot, const char* hint) {
    MapMesh m;
    MapMesh::Load(mapPath, m);
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);

    std::map<std::string, std::string> seen;   // reference -> resolved path
    for (const MapObject& o : m.objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (const Material& mat : o.materials) {
            const std::string& d = mat.slots[0].name;
            if (!d.empty() && !seen.count(d)) seen[d] = cache.Resolve(d, hint);
        }
    }
    size_t unresolved = 0, wrongLevel = 0, looksLightmap = 0;
    std::string hintDir = std::string("levels\\") + hint;
    for (const auto& kv : seen) {
        std::string lower = kv.second;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool bad = false;
        if (kv.second.empty()) { ++unresolved; bad = true; }
        else if (lower.find("_l_") != std::string::npos) { ++looksLightmap; bad = true; }
        else if (lower.find("levels/") != std::string::npos &&
                 lower.find(hintDir) == std::string::npos) { ++wrongLevel; bad = true; }
        if (bad) LogInfo("  %-28s -> %s", kv.first.c_str(),
                         kv.second.empty() ? "(UNRESOLVED)" : kv.second.c_str());
    }
    LogInfo("");
    LogInfo("distinct diffuse refs %zu | unresolved %zu | resolved to a LIGHTMAP %zu | resolved to another level %zu",
            seen.size(), unresolved, looksLightmap, wrongLevel);
    return 0;
}

// Walks the four-file chain a placed CParticleFX resolves through and prints
// what each step produced, so the data path can be checked without a window:
//   instance -> template o.Effect -> Effects/<name>.pfx -> Emitters/<file>.ini
static int ParticlesCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");
    EmitterLibrary library;
    library.Init(std::string(dataRoot) + "/Scripts");
    LogInfo("library: %zu emitters, %zu effects", library.indexedEmitters(),
            library.indexedEffects());

    static const char* kBlendName[12] = {"none", "alpha", "add", "modulate", "filter",
                                         "translucent", "invmodulate", "subtract",
                                         "revsubtract", "desttranslucent", "destalpha",
                                         "modulate2x"};
    std::map<std::string, size_t> byEffect;
    size_t placed = 0, unresolved = 0, budget = 0;
    for (const Entity& e : level.entities()) {
        if (e.type != "CParticleFX") continue;
        ++placed;
        std::string effect = e.props.String("Effect", "");
        if (effect.empty()) effect = templates.ResolveString(e.baseObj, "Effect");
        if (effect.empty()) effect = "Default";
        byEffect[effect]++;
        if (!library.Effect(effect)) ++unresolved;
    }
    LogInfo("%zu CParticleFX placed, %zu distinct effects, %zu unresolved", placed,
            byEffect.size(), unresolved);

    for (const auto& kv : byEffect) {
        const ParticleFxDef* fx = library.Effect(kv.first);
        LogInfo("  %-28s x%-4zu %s", kv.first.c_str(), kv.second,
                fx ? "" : "(UNRESOLVED)");
        if (!fx) continue;
        for (const ParticleFxDef::Ref& ref : fx->emitters) {
            const EmitterParams* p = library.Emitter(ref.file);
            if (!p) { LogInfo("      %-24s (UNRESOLVED)", ref.file.c_str()); continue; }
            budget += static_cast<size_t>(p->maxParticles) * kv.second;
            LogInfo("      %-24s scale %.2f  type %d  blend %-11s  max %4d  rate %6.1f/s  "
                    "life %.2f-%.2f  tex %s",
                    ref.file.c_str(), ref.scale, p->type,
                    (p->blendMode >= 0 && p->blendMode < 12) ? kBlendName[p->blendMode] : "?",
                    p->maxParticles,
                    p->spawnInterval > 0.f ? 1.f / p->spawnInterval : 0.f,
                    p->lifeMin, p->lifeMax, p->texture.c_str());
        }
    }
    LogInfo("worst-case particle budget for this level: %zu", budget);
    for (const std::string& err : library.errors()) LogWarn("  %s", err.c_str());
    return 0;
}

// Resolves every CBillboard the level places through its template chain and
// reports what came out, so the corona parameters can be checked without a
// window. Also builds the collision BVH and times it, since coronas are the
// first thing to depend on it.
static int BillboardsCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");
    TextureCache textures;
    textures.Init(std::string(dataRoot) + "/Textures", false);

    static const char* kBlendName[5] = {"none", "alpha", "add", "filter", "translucent"};
    struct Row {
        size_t count = 0, coronas = 0;
        std::string texture, resolved;
        float size = 0, minSize = 0, alpha = 0, off = 0;
        int blend = 0;
    };
    std::map<std::string, Row> byBase;
    size_t total = 0, coronas = 0, missingTex = 0;

    for (const Entity& e : level.entities()) {
        if (e.type != "CBillboard") continue;
        ++total;

        // Mirrors BillboardRenderer::Build's walk: instance first, then the
        // BaseObj chain.
        std::vector<const Properties*> chain{&e.props};
        std::string current = e.baseObj;
        for (int d = 0; d < 16 && !current.empty(); ++d) {
            const Properties* p = templates.Find(current);
            if (!p) break;
            chain.push_back(p);
            current = p->String("BaseObj");
        }
        auto find = [&](const char* key) -> const Value* {
            for (const Properties* p : chain) if (const Value* v = p->Find(key)) return v;
            return nullptr;
        };
        auto num = [&](const char* key, float fallback) {
            const Value* v = find(key);
            return v && v->kind == Value::Kind::Number ? float(v->number) : fallback;
        };

        Row& row = byBase[e.baseObj.empty() ? "(none)" : e.baseObj];
        ++row.count;
        const Value* en = find("Corona.Enabled");
        const bool isCorona = en && en->kind == Value::Kind::Bool && en->boolean;
        if (isCorona) { ++row.coronas; ++coronas; }

        const Value* t = find("Texture");
        row.texture = t && t->kind == Value::Kind::String ? t->text : "banka";
        row.size = num("Size", 5.f);
        row.minSize = num("Corona.MinSize", 0.8f);
        row.alpha = num("Alpha", 0.5f);
        row.off = num("Corona.OffDistance", 70.f);
        row.blend = int(num("BlendMode", 1.f));
        row.resolved = textures.Resolve("Particles/" + row.texture, level.name());
        if (row.resolved.empty()) ++missingTex;
    }

    LogInfo("%zu CBillboard placed (%zu coronas), %zu distinct templates", total, coronas,
            byBase.size());
    for (const auto& kv : byBase) {
        const Row& r = kv.second;
        LogInfo("  %-32s x%-4zu %s  tex %-18s %s  size %.1f-%.1f  alpha %.2f  off %.0f  blend %s",
                kv.first.c_str(), r.count, r.coronas ? "corona " : "sprite ", r.texture.c_str(),
                r.resolved.empty() ? "(MISSING)" : "ok", r.minSize, r.size, r.alpha, r.off,
                (r.blend >= 0 && r.blend < 5) ? kBlendName[r.blend] : "?");
    }
    if (missingTex) LogWarn("%zu templates reference a texture that does not resolve", missingTex);

    if (level.mapLoaded()) {
        const auto t0 = std::chrono::steady_clock::now();
        CollisionMesh collision;
        collision.Build(level.map(), level.info().scale);
        const float ms = std::chrono::duration<float, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();
        LogInfo("collision BVH built in %.0f ms", ms);
    }
    return 0;
}

// Poses a model by one of its animations and reports what moved. This is the
// check that CPU skinning is right, without a window and without a screenshot:
// a bind pose and a posed pose have very different bounds, and a model that
// silently failed to skin reports them identical.
//
// It is also the oracle the GPU skinning path will be diffed against, one
// bone at a time - which is the reason CPU skinning was built first at all.
static int PoseCmd(const char* modelPath, const char* animName, const char* timeArg) {
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

static int ModelCmd(const char* path) {
    Model model;
    if (!Model::Load(path, model)) { LogInfo("failed to load %s", path); return 2; }
    size_t verts = 0, tris = 0;
    for (const ModelMesh& mesh : model.meshes) {
        verts += mesh.vertexCount();
        tris += mesh.triangleCount();
    }
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const ModelMesh& mesh : model.meshes) {
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            for (int c = 0; c < 3; ++c) {
                float v = mesh.verts[i * 8 + c];
                if (v < lo[c]) lo[c] = v;
                if (v > hi[c]) hi[c] = v;
            }
        }
    }
    LogInfo("%s: %zu bones, %zu meshes, %zu verts, %zu tris",
            path, model.bones.size(), model.meshes.size(), verts, tris);
    LogInfo("  bind-pose bounds x[%.2f..%.2f] y[%.2f..%.2f] z[%.2f..%.2f]  (size %.2f x %.2f x %.2f)",
            lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
            hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]);
    // Per-mesh names and material texture references. The mesh name is what a
    // .shader script keys off for a per-object material override, so it has to
    // be visible to tell why a model did or did not pick one up.
    for (const ModelMesh& mesh : model.meshes) {
        LogInfo("  mesh %-28s %6zu verts %6zu tris  skin %s  materials%s: %s",
                mesh.name.c_str(), mesh.vertexCount(), mesh.triangleCount(),
                mesh.hasSkin() ? "yes" : "no ", mesh.materialsExact ? "" : " (INEXACT)",
                [&] {
                    std::string s;
                    for (const std::string& m : mesh.materials) {
                        if (!s.empty()) s += ", ";
                        s += m;
                    }
                    return s.empty() ? std::string("(none)") : s;
                }().c_str());
    }
    return 0;
}

// Locates the game data next to the executable. The engine is meant to sit in
// the game's Bin folder, like the original Painkiller.exe, so the data root is
// a sibling of the exe's directory. The shipped Data folder (read straight
// from its .pak archives) is preferred; a loose Data_Extracted tree still
// works as a fallback.
static std::string FindDataRoot(const char* exePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(exePath, ec).parent_path();
    for (int depth = 0; depth < 2 && !dir.empty(); ++depth, dir = dir.parent_path()) {
        for (const char* name : {"Data", "Data_Extracted"}) {
            fs::path candidate = dir / name;
            if (fs::exists(candidate / "Levels.pak", ec) ||
                fs::exists(candidate / "Levels", ec))
                return candidate.string();
        }
    }
    return {};
}

// Mounts the .pak archives in a data root before a command touches it. A root
// of loose extracted files has none, and every lookup then goes to disk.
static const char* MountRoot(const char* dataRoot) {
    const size_t n = FileSystem::Get().MountData(dataRoot);
    if (n) LogInfo("mounted %zu .pak archives from %s", n, dataRoot);
    return dataRoot;
}

// For commands that take bare file paths rather than a data root: mount the
// data directory the path points into (the nearest ancestor holding .pak
// archives), falling back to the root auto-detected next to the executable,
// so arguments that point inside the archives resolve too.
static void MountForPath(const char* anyPath, const char* exePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(anyPath, ec).parent_path();
    for (int depth = 0; depth < 8 && dir.has_relative_path(); ++depth) {
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".pak") {
                MountRoot(dir.string().c_str());
                return;
            }
        }
        dir = dir.parent_path();
    }
    const std::string root = FindDataRoot(exePath);
    if (!root.empty()) MountRoot(root.c_str());
}

// Double-click launch: no arguments, so find the data ourselves and open the
// first campaign level. The [ ] keys cycle through every level from there.
static int DefaultRun(const char* exePath) {
    const std::string root = FindDataRoot(exePath);
    if (root.empty()) {
        LogInfo("no game data found. Place PainfulEngine.exe in the game's Bin");
        LogInfo("folder, next to the Data directory with the .pak archives (or");
        LogInfo("a Data_Extracted directory with the unpacked assets).");
        return 2;
    }
    MountRoot(root.c_str());
    std::string level = "C1L1_Cathedral";
    if (!FileSystem::Get().IsDirectory(root + "/Levels/" + level)) {
        for (const DirEntry& entry : FileSystem::Get().List(root + "/Levels")) {
            if (entry.isDirectory) { level = entry.name; break; }
        }
    }
    // Launching the exe means playing, so this takes the script-driven path:
    // the game's own Lua loads the level, Game:OnPlay creates the player, and
    // you walk. `run` is the older hand-driven loader, which builds the same
    // world without a Lua host - so it has no player and only ever gives a
    // free camera. It stays as a diagnostic, not as the way in.
    return GameCmd(root.c_str(), level.c_str(), exePath, "", nullptr);
}

int main(int argc, char** argv) {
    if (argc < 2) return DefaultRun(argv[0]);
    if (argc < 3) return Usage();
    std::string cmd = argv[1];

    // Mount any .pak archives before dispatch. Most commands name their data
    // root explicitly; the bare file-path diagnostics (map, dat, model, ...)
    // get the auto-detected root so arguments that point inside the archives
    // resolve as well.
    {
        static const std::set<std::string> rootAt3 = {
            "run",   "level",  "entities", "fit",       "skytex",     "scale",
            "zones", "ground", "textures", "particles", "billboards", "physics"};
        static const std::set<std::string> rootAt2 = {"levels", "resolve", "texdump", "sound",
                                                      "shaders", "lua", "game", "mklevel"};
        if (rootAt3.count(cmd) && argc >= 4) MountRoot(argv[3]);
        else if (rootAt2.count(cmd)) MountRoot(argv[2]);
        else MountForPath(argv[2], argv[0]);
    }
    if (cmd == "run"   && argc >= 4) {
        std::string shot;
        int cullMode = 0, entityCull = 1;
        bool skyOnly = false;
        bool novis = false;
        bool noclip = false;
        bool physicsDebug = false;
        float entityScale = 1.f;
        float pos[3], angles[2];
        bool hasPos = false, hasAngles = false;
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--shot" && i + 1 < argc) shot = argv[++i];
            else if (arg == "--skyview") skyOnly = true;
            else if (arg == "--novis") novis = true;
            else if (arg == "--noclip") noclip = true;
            else if (arg == "--physdebug") physicsDebug = true;
            else if (arg == "--pos" && i + 3 < argc) {
                for (int k = 0; k < 3; ++k) pos[k] = float(std::atof(argv[i + 1 + k]));
                i += 3;
                hasPos = true;
            } else if (arg == "--escale" && i + 1 < argc) {
                entityScale = float(std::atof(argv[++i]));
            } else if (arg == "--ecull" && i + 1 < argc) {
                std::string mode = argv[++i];
                entityCull = (mode == "cw") ? 1 : (mode == "none") ? 2 : 0;
            } else if (arg == "--cull" && i + 1 < argc) {
                std::string mode = argv[++i];
                cullMode = (mode == "cw") ? 1 : (mode == "none") ? 2 : 0;
            } else if (arg == "--look" && i + 2 < argc) {
                angles[0] = float(std::atof(argv[i + 1]));
                angles[1] = float(std::atof(argv[i + 2]));
                i += 2;
                hasAngles = true;
            }
        }
        return RunCmd(argv[2], argv[3], shot, argv[0],
                      hasPos ? pos : nullptr, hasAngles ? angles : nullptr, cullMode,
                      entityCull, entityScale, skyOnly, novis, noclip, physicsDebug);
    }
    if (cmd == "level" && argc >= 4) return LevelCmd(argv[2], argv[3]);
    if (cmd == "entities" && argc >= 4) return EntitiesCmd(argv[2], argv[3]);
    if (cmd == "fit" && argc >= 4) return FitCmd(argv[2], argv[3]);
    if (cmd == "levels") return LevelsCmd(argv[2]);
    if (cmd == "lua")
        return LuaCmd(argv[2], argc >= 4 ? std::atoi(argv[3]) : 10,
                      argc >= 5 ? argv[4] : nullptr, argc >= 6 ? argv[5] : nullptr);
    if (cmd == "game") {
        std::string shot;
        for (int i = 4; i < argc; ++i)
            if (std::string(argv[i]) == "--shot" && i + 1 < argc) shot = argv[++i];
        const char* exec = nullptr;
        for (int i = 4; i < argc; ++i)
            if (std::string(argv[i]) == "--exec" && i + 1 < argc) exec = argv[++i];
        return GameCmd(argv[2], argc >= 4 ? argv[3] : "C1L1_Cathedral", argv[0], shot, exec);
    }
    if (cmd == "mklevel")
        return MkLevelCmd(argv[2], argc >= 4 ? argv[3] : "TestFloor",
                          argc >= 5 ? float(std::atof(argv[4])) : 200.f,
                          argc >= 6 ? float(std::atof(argv[5])) : 0.f,
                          argc >= 7 ? argv[6] : "beton_tile_all");
    if (cmd == "map")   return MapCmd(argv[2]);
    if (cmd == "mats")  return MatsCmd(argv[2], argc >= 4 ? argv[3] : "");
    if (cmd == "resolve" && argc >= 4) return ResolveCmd(argv[2], argv[3]);
    if (cmd == "texdump" && argc >= 4) return TexDumpCmd(argv[2], argv[3], argc >= 5 ? argv[4] : "");
    if (cmd == "skytex" && argc >= 4) return SkyTexCmd(argv[2], argv[3]);
    if (cmd == "blend" && argc >= 5)
        return BlendCmd(argv[2], argv[3], argv[4], argc >= 6 ? argv[5] : nullptr);
    if (cmd == "wps") return WpsCmd(argv[2]);
    if (cmd == "sound" && argc >= 4)
        return SoundCmd(argv[2], argv[3], argc >= 5 ? argv[4] : nullptr);
    if (cmd == "bones")
        return BonesCmd(argv[2], argc >= 4 ? argv[3] : nullptr,
                        argc >= 5 ? argv[4] : nullptr, argc >= 6 ? argv[5] : nullptr);
    if (cmd == "scale" && argc >= 4) return ScaleCmd(argv[2], argv[3]);
    if (cmd == "zones" && argc >= 4) {
        float zp[3];
        const bool hasP = argc >= 7;
        if (hasP) for (int k = 0; k < 3; ++k) zp[k] = float(std::atof(argv[4 + k]));
        return ZonesCmd(argv[2], argv[3], hasP ? zp : nullptr);
    }
    if (cmd == "ground" && argc >= 8) return GroundCmd(argv[2], argv[3], float(atof(argv[4])), float(atof(argv[5])), float(atof(argv[6])), float(atof(argv[7])));
    if (cmd == "skydump") return SkyDumpCmd(argv[2]);
    if (cmd == "shaders") return ShadersCmd(argv[2], argc >= 4 ? argv[3] : "");
    if (cmd == "dat") return DatCmd(argv[2]);
    if (cmd == "textures" && argc >= 5) return TexturesCmd(argv[2], argv[3], argv[4]);
    if (cmd == "model") return ModelCmd(argv[2]);
    if (cmd == "pose" && argc >= 4)
        return PoseCmd(argv[2], argv[3], argc >= 5 ? argv[4] : nullptr);
    if (cmd == "particles" && argc >= 4) return ParticlesCmd(argv[2], argv[3]);
    if (cmd == "billboards" && argc >= 4) return BillboardsCmd(argv[2], argv[3]);
    if (cmd == "physics" && argc >= 4) return PhysicsCmd(argv[2], argv[3]);
    return Usage();
}

#ifdef _WIN32
// The executable builds for the GUI subsystem so double-clicking it opens no
// console window. When launched FROM a console, attach to it so the CLI
// commands still print. __argc/__argv are populated by the CRT.
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Keep stdout/stderr as-is when the parent already redirected them (a
    // pipe or a file); rebinding to CONOUT$ would steal that output.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool redirected = out != nullptr && out != INVALID_HANDLE_VALUE;
    if (AttachConsole(ATTACH_PARENT_PROCESS) && !redirected) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
    return main(__argc, __argv);
}
#endif
