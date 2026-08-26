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
#include "Core/Log.h"
#include "Render/Renderer.h"
#include "Render/SkyRenderer.h"
#include "Render/TextureCache.h"
#include <bimg/decode.h>
#include <bx/allocator.h>
namespace painful { extern bx::DefaultAllocator g_allocator; }
#include <algorithm>
#include "Render/EntityRenderer.h"
#include "Render/ParticleRenderer.h"
#include "Render/WorldRenderer.h"
#include "Render/Window.h"
#include "World/Level.h"
#include "World/Templates.h"
#include "World/Zones.h"
#include "Assets/Skeleton.h"

#include <chrono>
#include <algorithm>
#include <array>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

using namespace painful;

static int Usage() {
    LogInfo("PainfulEngine\n");
    LogInfo("  PainfulEngine                                    find the game data and run");
    LogInfo("  PainfulEngine run   <Data/Levels/NAME> <DataRoot>  open a specific level");
    LogInfo("  PainfulEngine level <Data/Levels/NAME> <DataRoot>  headless report");
    LogInfo("  PainfulEngine levels <DataRoot>                    list levels");
    LogInfo("  PainfulEngine map   <file.mpk>");
    LogInfo("  PainfulEngine model <file.pkmdl>");
    return 1;
}

// Totals worth showing while the renderer is still being built out.
struct LevelStats {
    size_t objects = 0, verts = 0, tris = 0, materials = 0, collidable = 0;
};

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
                  bool novis) {
    const std::string root = dataRoot;
    const std::string shaderDir = ShaderDirFor(exePath);

    // Enumerate every level once so they can be cycled without restarting.
    std::vector<std::string> levelDirs;
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(root + "/Levels", ec)) {
            if (entry.is_directory()) levelDirs.push_back(entry.path().string());
        }
        std::sort(levelDirs.begin(), levelDirs.end());
    }
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

        const float speed = camera.moveSpeed * (window.IsDown(Key::Fast) ? 4.f : 1.f) * dt;
        float fwd = 0.f, right = 0.f, up = 0.f;
        if (window.IsDown(Key::Forward)) fwd += speed;
        if (window.IsDown(Key::Back))    fwd -= speed;
        if (window.IsDown(Key::Right))   right += speed;
        if (window.IsDown(Key::Left))    right -= speed;
        if (window.IsDown(Key::Up))      up += speed;
        if (window.IsDown(Key::Down))    up -= speed;
        camera.Move(fwd, right, up);

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
        renderer.DebugText(5, "%zu particles in %zu emitters, %zu effects, %zu particle draws",
                           particles ? particles->liveParticles() : 0,
                           particles ? particles->emitters() : 0,
                           particles ? particles->effects() : 0,
                           particles ? particles->drawCalls() : 0);
        // rot prints in the exact form --look takes, so a HUD screenshot can
        // be reproduced verbatim: --pos <pos> --look <rot>.
        renderer.DebugText(4, "pos %.1f %.1f %.1f   rot %.2f %.2f   sky %s",
                           camera.pos[0], camera.pos[1], camera.pos[2],
                           camera.yaw, camera.pitch,
                           (sky && sky->loaded())
                               ? (sky->layered() ? "layered" : "lowquality") : "none");
        renderer.DebugText(6, "%s - WASD move, shift fast, space/ctrl up-down, [ ] change level, esc release",
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
        std::string model = templates.ResolveString(e.baseObj, "Model");
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
        if (templates.ResolveString(e.baseObj, "Model").empty()) continue;
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

// Lists the levels available for the in-engine selector.
static int LevelsCmd(const char* dataRoot) {
    std::vector<std::string> dirs;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::string(dataRoot) + "/Levels", ec)) {
        if (entry.is_directory()) dirs.push_back(entry.path().filename().string());
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
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        size_t ok = 0, bad = 0;
        for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
            if (entry.path().extension() != ".dat") continue;
            DatPack pack;
            if (DatPack::Load(entry.path().string(), pack)) {
                ++ok;
            } else {
                ++bad;
                LogInfo("FAIL %-40s %s", entry.path().filename().string().c_str(),
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
static int BonesCmd(const char* path) {
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
static int MatsCmd(const char* path) {
    MapMesh m;
    MapMesh::Load(path, m);
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
    return 0;
}

// Locates the game data next to the executable. The engine is meant to sit in
// the game's Bin folder, like the original Painkiller.exe, so the data root is
// a sibling of the exe's directory. Extracted data is preferred; the .pak
// archives in Data are not readable yet.
static std::string FindDataRoot(const char* exePath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::absolute(exePath, ec).parent_path();
    for (int depth = 0; depth < 2 && !dir.empty(); ++depth, dir = dir.parent_path()) {
        for (const char* name : {"Data_Extracted", "Data"}) {
            fs::path candidate = dir / name;
            if (!fs::exists(candidate / "Levels", ec)) continue;
            if (std::string(name) == "Data") {
                LogInfo("found %s, but .pak archives are not supported yet - "
                        "extract them to Data_Extracted", candidate.string().c_str());
                continue;
            }
            return candidate.string();
        }
    }
    return {};
}

// Double-click launch: no arguments, so find the data ourselves and open the
// first campaign level. The [ ] keys cycle through every level from there.
static int DefaultRun(const char* exePath) {
    const std::string root = FindDataRoot(exePath);
    if (root.empty()) {
        LogInfo("no game data found. Place PainfulEngine.exe in the game's Bin");
        LogInfo("folder, next to a Data_Extracted directory with the unpacked");
        LogInfo("assets (Levels/, Maps/, Textures/, LScripts/, Models/, Items/).");
        return 2;
    }
    std::string level = root + "/Levels/C1L1_Cathedral";
    std::error_code ec;
    if (!std::filesystem::exists(level, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(root + "/Levels", ec)) {
            if (entry.is_directory()) { level = entry.path().string(); break; }
        }
    }
    return RunCmd(level.c_str(), root.c_str(), "", exePath,
                  nullptr, nullptr, 0, 1, 1.f, false, false);
}

int main(int argc, char** argv) {
    if (argc < 2) return DefaultRun(argv[0]);
    if (argc < 3) return Usage();
    std::string cmd = argv[1];
    if (cmd == "run"   && argc >= 4) {
        std::string shot;
        int cullMode = 0, entityCull = 1;
        bool skyOnly = false;
        bool novis = false;
        float entityScale = 1.f;
        float pos[3], angles[2];
        bool hasPos = false, hasAngles = false;
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--shot" && i + 1 < argc) shot = argv[++i];
            else if (arg == "--skyview") skyOnly = true;
            else if (arg == "--novis") novis = true;
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
                      entityCull, entityScale, skyOnly, novis);
    }
    if (cmd == "level" && argc >= 4) return LevelCmd(argv[2], argv[3]);
    if (cmd == "entities" && argc >= 4) return EntitiesCmd(argv[2], argv[3]);
    if (cmd == "fit" && argc >= 4) return FitCmd(argv[2], argv[3]);
    if (cmd == "levels") return LevelsCmd(argv[2]);
    if (cmd == "map")   return MapCmd(argv[2]);
    if (cmd == "mats")  return MatsCmd(argv[2]);
    if (cmd == "resolve" && argc >= 4) return ResolveCmd(argv[2], argv[3]);
    if (cmd == "texdump" && argc >= 4) return TexDumpCmd(argv[2], argv[3], argc >= 5 ? argv[4] : "");
    if (cmd == "skytex" && argc >= 4) return SkyTexCmd(argv[2], argv[3]);
    if (cmd == "bones") return BonesCmd(argv[2]);
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
    if (cmd == "particles" && argc >= 4) return ParticlesCmd(argv[2], argv[3]);
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
