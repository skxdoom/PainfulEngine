// The `run` viewer: the hand-driven level loader, without a Lua host.
//
// It builds the same world PainfulEngine does but creates no player and only
// ever gives a free camera, which is what makes it useful for surveying a level
// - and what makes it a diagnostic rather than the way in.
#include "LevelStats.h"
#include "Commands.h"

int RunCmd(const char* levelDir, const char* dataRoot,
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
    // The player's own pusher: the widest of the four spheres the shape factory
    // builds for BodyTypes.Player at bodyScale 1.0 (Engine.dll 0x101b3e20).
    physics.SetPawnProbeRadius(0.4f);
    DebugLines debugLines;
    const bool debugLinesReady = debugLines.Init(shaderDir);
    std::vector<DebugLine> physicsWireframe;
    std::vector<BodyPose> movedProps;
    Camera camera;
    if (const char* n = getenv("PAINFUL_NEAR")) camera.nearPlane = float(atof(n));
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
        // depth, so everything solid has to be down first. Cfg.Bloom defaults
        // on, so sprites take the level's BloomFX.DimScale here as in the game.
        if (level) {
            const float dim = level->info().bloomMultiplier > 0.f ? level->info().bloomDimScale : 1.f;
            if (particles) particles->SetColorScale(dim);
            if (billboards) billboards->SetColorScale(dim);
            if (particles) particles->SetFog(info.fogMode, info.fogStart, info.fogEnd, info.fogDensity, info.fogColor);
            if (billboards) billboards->SetFog(info.fogMode, info.fogStart, info.fogEnd, info.fogDensity, info.fogColor);
        }
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

