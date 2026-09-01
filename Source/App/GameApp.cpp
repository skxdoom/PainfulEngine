#include "GameApp.h"

#include "Assets/Ani.h"
#include "Assets/Dat.h"
#include "Assets/Hke.h"
#include "Assets/Mpk.h"
#include "Assets/Pkmdl.h"
#include "Assets/Rde.h"
#include "Assets/ShaderScript.h"
#include "Assets/Skeleton.h"
#include "Assets/Waypoints.h"
#include "Audio/AudioEngine.h"
#include "Core/AppPaths.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Game/PlayerPawn.h"
#include "Game/ScriptEngine.h"
#include "Render/BillboardRenderer.h"
#include "Render/DebugLines.h"
#include "Render/EntityRenderer.h"
#include "Render/HudRenderer.h"
#include "Render/ParticleRenderer.h"
#include "Render/Renderer.h"
#include "Render/SkyRenderer.h"
#include "Render/TextureCache.h"
#include "Render/Window.h"
#include "Render/WorldRenderer.h"
#include "Script/LuaHost.h"
#include "World/Level.h"
#include "World/Lighting.h"
#include "World/PhysicsWorld.h"
#include "World/Templates.h"
#include "World/Zones.h"

#include <SDL3/SDL.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace painful { extern bx::DefaultAllocator g_allocator; }

using namespace painful;

namespace painful {

// The camera's view-projection, built exactly as WorldRenderer builds it -
// right-handed, because PainEngine's data is a Maya export and bx defaults to
// left-handed. The debug overlays have to agree with what was drawn or the
// nameplates sit next to the things they name.
static void BuildViewProj(const Camera& camera, int width, int height, float out[16]) {
    float forward[3];
    camera.Forward(forward);
    const bx::Vec3 eye = {camera.pos[0], camera.pos[1], camera.pos[2]};
    const bx::Vec3 at = {camera.pos[0] + forward[0], camera.pos[1] + forward[1],
                         camera.pos[2] + forward[2]};
    float viewMtx[16], projMtx[16];
    bx::mtxLookAt(viewMtx, eye, at, {0.f, 1.f, 0.f}, bx::Handedness::Right);
    bx::mtxProj(projMtx, camera.fovDegrees, float(width) / float(height),
                camera.nearPlane, camera.farPlane, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    bx::mtxMul(out, viewMtx, projMtx);
}

// World point -> screen pixels. False when the point is behind the eye, where
// the perspective divide would mirror it back into view and hang a label on
// the wrong side of the screen.
static bool ProjectToScreen(const float world[3], const float viewProj[16],
                            int width, int height, float out[2]) {
    float clip[4];
    for (int c = 0; c < 4; ++c)
        clip[c] = world[0] * viewProj[c] + world[1] * viewProj[4 + c] +
                  world[2] * viewProj[8 + c] + viewProj[12 + c];
    if (clip[3] <= 1e-4f) return false;
    out[0] = (clip[0] / clip[3] * 0.5f + 0.5f) * float(width);
    out[1] = (1.f - (clip[1] / clip[3] * 0.5f + 0.5f)) * float(height);
    return true;
}

// Script-driven windowed run: the game's own Lua loads the level and creates
// every entity through the native API; the C++ side supplies the window, the
// renderer and a free camera. The counterpart to `run`, which drives the
// same subsystems by hand - as natives grow real, this path takes over.
int GameCmd(const char* dataRoot, const char* levelName, const char* exePath,
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
    // .pkmdl winding; pack meshes carry their own state. PAINFUL_ECULL is a
    // diagnostic override - 2 disables culling entirely, which is how to tell
    // a hole made by wrong winding from a hole made by missing geometry.
    entities.SetCullMode(getenv("PAINFUL_ECULL") ? atoi(getenv("PAINFUL_ECULL")) : 1);
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
    // The player's own pusher: the widest of the four spheres the shape factory
    // builds for BodyTypes.Player at bodyScale 1.0 (Engine.dll 0x101b3e20).
    physics.SetPawnProbeRadius(0.4f);
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
    //
    // A hidden run stays silent too. PAINFUL_HIDDEN means nobody is watching,
    // and an automated capture that nonetheless fills the room with gunfire is
    // just noise - several a minute, from a window that is not even on screen.
    AudioEngine audio;
    const bool wantAudio = std::getenv("PAINFUL_HIDDEN") == nullptr;
    if (wantAudio && audio.Init(root + "/Sounds")) engine.AttachAudio(&audio);

    // The 2D layer. The scripts draw the whole interface through it during
    // Game_Render, so it has to be attached before the level loads - the
    // loading screen is itself a HUD script.
    HudRenderer hud;
    const bool hudReady = hud.Init(shaderDir, root + "/Fonts");
    if (hudReady) engine.AttachHud(&hud, &textures);

    // The debug overlays: F1 collision wireframe, F2 the same without the
    // level, F3 nameplates. Off unless asked for, and each independent - the
    // wireframe and the labels answer different questions.
    DebugLines debugLines;
    const bool debugLinesReady = debugLines.Init(shaderDir);
    std::vector<DebugLine> debugWireframe;
    // Two different questions, so two independent overlays rather than one
    // mode with three positions:
    //
    //   F1  the GEOMETRY - every triangle the renderer draws, world and
    //       entities, in wireframe. What is actually on screen.
    //   F2  the dynamic COLLISION - what physics thinks is there, level left
    //       out. What the world can actually be hit by.
    //
    // Having them on together is the useful state: where the two disagree is
    // where the bug is.
    //
    // PAINFUL_WIRE=1|2 and PAINFUL_NAMEPLATES=1 start one already on, which is
    // how an automated capture can see it - a keypress is not available there.
    bool geoWire = false;
    bool collisionWire = false;
    if (const char* w = std::getenv("PAINFUL_WIRE")) {
        const int mode = std::atoi(w);
        geoWire = mode == 1;
        collisionWire = mode == 2;
    }
    bool nameplates = std::getenv("PAINFUL_NAMEPLATES") != nullptr;
    constexpr float kNameplateRadius = 20.f;

    // F4 stops the monsters THINKING, which is not the same as stopping them
    // ticking. CAiBrain:PreUpdate and OnUpdate are the deciding - target,
    // approach, attack - while CActor:Tick carries animation, damage
    // reactions and the ragdoll. Stubbing the brain alone leaves an enemy
    // standing there alive and fully hittable, which is exactly the bench you
    // want for hitboxes and ragdolls.
    //
    // Every actor holds its OWN brain: CActor makes it with Clone(CAiBrain),
    // and Clone is a shallow copy, so the functions are copied by value. The
    // class has to be stubbed for actors spawned later AND every live brain
    // for the ones already standing. Re-enabling walks the same two places.
    bool aiDisabled = std::getenv("PAINFUL_NOAI") != nullptr;
    bool aiApplied = false;

    // F6: the developers' own debug tooling, still in the shipped scripts and
    // gated on two switches of theirs.
    //
    //   debugMarek     449 uses, 251 of them Game:Print. Actor state, boss
    //                  phases, animation decisions - narrated by the people who
    //                  wrote them. Main/SaveGame.lua sets it, commented out.
    //   IsFinalBuild() 24 gates across 7 files, from the key that hides the
    //                  viewmodel to actor readouts.
    //
    // Both move together, because in the original they distinguished one BUILD
    // from another rather than being two independent options.
    bool devMode = std::getenv("PAINFUL_DEV") != nullptr;
    bool devApplied = false;
    static const char* const kAiOff =
        "do local function off(b)"
        "  if b and not b.__aiOff then"
        "    b.__aiOff = true"
        "    b.__aiOnUpdate = b.OnUpdate  b.__aiPreUpdate = b.PreUpdate"
        "    b.OnUpdate = function() end  b.PreUpdate = function() end"
        "  end end"
        " off(CAiBrain)"
        " if Actors then for i,o in Actors do off(o._AIBrain) end end end";
    static const char* const kAiOn =
        "do local function on(b)"
        "  if b and b.__aiOff then"
        "    b.OnUpdate = b.__aiOnUpdate  b.PreUpdate = b.__aiPreUpdate"
        "    b.__aiOnUpdate = nil  b.__aiPreUpdate = nil  b.__aiOff = nil"
        "  end end"
        " on(CAiBrain)"
        " if Actors then for i,o in Actors do on(o._AIBrain) end end end";
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

    // Model lighting. In this path there is no Level object at all - the script
    // layer creates the entities and hands the renderer state over through the
    // WORLD.* natives - but the CLight and CEnvironment placements it works
    // from are the same files on disk, so they are read directly.
    //
    // The scripts also create lights of their own at runtime (LIGHT.Setup,
    // LIGHT.SetFalloff, ENVIRONMENT.AddLight are all still stubs), so muzzle
    // flashes and fireballs do not light anything yet. Level lighting does.
    {
        Level lightingLevel;
        const std::string levelDir = std::string(dataRoot) + "/Levels/" + levelName;
        if (lightingLevel.Load(levelDir, dataRoot)) {
            TemplateCache lightingTemplates;
            lightingTemplates.Init(std::string(dataRoot) + "/LScripts/Templates");
            lightingTemplates.SetLevelOverlay(levelDir + "/Templates");
            entities.BuildLighting(lightingLevel, lightingTemplates);
            LogInfo("entity lighting: %zu lights, %zu environment boxes",
                    entities.lightCount(), entities.environmentCount());
        } else {
            LogWarn("no entity lighting: %s", lightingLevel.error().c_str());
        }
    }

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
    if (const char* n = getenv("PAINFUL_NEAR")) camera.nearPlane = float(atof(n));
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
        // F1 cycles the full wireframe on and off; F2 does the same for the
        // dynamic-only view. Pressing either while the other is up switches
        // straight to it, so the two are one mode rather than two flags that
        // can disagree.
        if (window.TakeDebugToggle(0)) geoWire = !geoWire;
        if (window.TakeDebugToggle(1)) collisionWire = !collisionWire;
        if (window.TakeDebugToggle(2)) nameplates = !nameplates;
        if (window.TakeDebugToggle(3)) aiDisabled = !aiDisabled;
        // Applied from the state rather than from the keypress, so that
        // PAINFUL_NOAI takes effect on the first frame - the scripts have to
        // exist before there is a CAiBrain to stub, so it cannot happen at
        // startup. Actors created later inherit whatever CAiBrain is now and
        // are born stubbed; ones already standing keep their own brain, which
        // is why the switch walks the live ones too.
        if (aiDisabled != aiApplied) {
            host.RunString(aiDisabled ? kAiOff : kAiOn);
            aiApplied = aiDisabled;
            LogInfo("AI %s", aiDisabled ? "disabled" : "enabled");
        }
        if (window.TakeDebugToggle(5)) devMode = !devMode;
        if (devMode != devApplied) {
            // debugMarek is a plain global the scripts read directly, so
            // setting it is the whole of that half. IsFinalBuild is a native
            // and answers from the engine's own flag.
            engine.SetDevMode(devMode);
            host.RunString(devMode ? "debugMarek = true" : "debugMarek = nil");
            devApplied = devMode;
            LogInfo("developer mode %s (debugMarek, IsFinalBuild -> %s)",
                    devMode ? "ON" : "off", devMode ? "false" : "true");
        }
        renderer.SetWireframe(geoWire);
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
            engine.TickProjectiles(dt);
            host.CallGlobal("Game_Tick", d, 1);
            physics.Update(dt);
            engine.SyncFromPhysics();
            engine.TickRagdolls();
            host.CallGlobal("Game_Tick2", d, 1);
            // Tick2 is where the view is steered, so take the result: the eye
            // rides PO_GetPawnHeadPos less PLAYER.GetCameraFix, at the angles
            // the scripts accumulated from MOUSE.GetDelta.
            if (scriptView) engine.TakeCameraPose(camera.pos, camera.yaw, camera.pitch);
            host.CallGlobal("Game_Tick3", d, 1);
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
            // Region transitions feed the message pump the way the engine's
            // phantoms do.
            engine.TickTriggers();
            engine.TickLifetimes(dt);
            // Bound effects follow their parents here, after the actors have
            // finished moving and their joints are posed for the frame. Placed
            // any earlier and every effect trails its owner by a frame.
            engine.UpdateAttached();
            engine.TickSounds(dt);
            engine.TickCollisions(dt);
            // Last, once the camera has settled: a view-attached weapon is
            // re-placed from the eye that will actually be rendered. Baked
            // during the tick it lags the shake by a frame, and the weapon
            // visibly jitters against the view by exactly the shake amount.
            engine.UpdateViewAttached();
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

        // The collision wireframe, drawn over the finished world so it reads
        // against what the renderer actually put on screen - the whole point
        // is to see where the two DISAGREE.
        if (collisionWire && debugLinesReady) {
            // Dynamic only. The static level is a few hundred thousand
            // triangles of blue that buries whatever you are looking at, and
            // F1 already draws the world - as the geometry it really is.
            physics.CollectDebugLines(camera.pos, kPhysicsDebugRadius, debugWireframe,
                                      false);

            // The per-limb hitboxes, in orange, over the same view. This is the
            // comparison that matters: a monster's collision body is one sphere
            // at its feet, while the boxes are where a shot should actually
            // land, and seeing both at once is what makes the gap obvious.
            engine.CollectHitboxLines(camera.pos, kPhysicsDebugRadius, debugWireframe);

            // A green box on every script entity that has NO physics body.
            //
            // Without this the overlay silently omits most of the level: of
            // Cathedral's 38 actors only 6 carry a body, and of its 68 items
            // only 3. Those are drawn by the renderer and can be shot at, but
            // the physics world has never heard of them - so the honest
            // wireframe of them is a marker at the entity, not a shape, and
            // seeing a sea of green IS the finding.
            for (const auto& kv : engine.entities()) {
                const auto& e = kv.second;
                if (e.physicsBody >= 0) continue;      // already drawn, in its own colour
                if (e.type != ScriptEngine::kMesh && e.type != ScriptEngine::kModel) continue;
                float d[3];
                for (int c = 0; c < 3; ++c) d[c] = e.pos[c] - camera.pos[c];
                if (d[0]*d[0] + d[1]*d[1] + d[2]*d[2] >
                    kPhysicsDebugRadius * kPhysicsDebugRadius) continue;

                constexpr float kHalf = 0.35f;
                const float lo[3] = {e.pos[0] - kHalf, e.pos[1] - kHalf, e.pos[2] - kHalf};
                const float hi[3] = {e.pos[0] + kHalf, e.pos[1] + kHalf, e.pos[2] + kHalf};
                // The twelve edges of the box, as pairs of corner indices.
                static const int kEdges[12][2] = {{0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},
                                                  {7,6},{6,4}, {0,4},{1,5},{2,6},{3,7}};
                float corner[8][3];
                for (int i = 0; i < 8; ++i) {
                    corner[i][0] = (i & 1) ? hi[0] : lo[0];
                    corner[i][1] = (i & 2) ? hi[1] : lo[1];
                    corner[i][2] = (i & 4) ? hi[2] : lo[2];
                }
                for (const auto& edge : kEdges) {
                    DebugLine line;
                    for (int c = 0; c < 3; ++c) {
                        line.a[c] = corner[edge[0]][c];
                        line.b[c] = corner[edge[1]][c];
                    }
                    line.abgr = 0xff00ff00u;
                    debugWireframe.push_back(line);
                }
            }
            debugLines.Draw(Renderer::kWorldView, debugWireframe);
        }

        // Nameplates. Anything within 20m gets its handle and what it is, which
        // is the pair you need to go from "that one is wrong" to a probe: the
        // handle is what every native takes, and the name is what the script
        // asked for. Sorted so the labels stack far-to-near and the closest
        // thing ends up on top.
        if (nameplates && hudReady) {
            float viewProj[16];
            BuildViewProj(camera, window.width(), window.height(), viewProj);

            struct Plate { float depth; float x, y; std::string text; };
            std::vector<Plate> plates;
            for (const auto& kv : engine.entities()) {
                const auto& e = kv.second;
                float d[3];
                for (int c = 0; c < 3; ++c) d[c] = e.pos[c] - camera.pos[c];
                const float distSq = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
                if (distSq > kNameplateRadius * kNameplateRadius) continue;

                float screen[2];
                if (!ProjectToScreen(e.pos, viewProj, window.width(), window.height(), screen))
                    continue;

                // Whichever of the three the entity actually carries: a model
                // has a name, a mesh has its object, and the source is the
                // fallback that is always there.
                std::string what = !e.name.empty() ? e.name
                                 : !e.mesh.empty() ? e.mesh
                                                   : e.source;
                const size_t slash = what.find_last_of("/\\");
                if (slash != std::string::npos) what = what.substr(slash + 1);

                char label[192];
                snprintf(label, sizeof label, "#%d %s  %.1fm", kv.first, what.c_str(),
                         std::sqrt(distSq));
                plates.push_back({distSq, screen[0], screen[1], label});
            }
            std::sort(plates.begin(), plates.end(),
                      [](const Plate& a, const Plate& b) { return a.depth > b.depth; });
            for (const Plate& p : plates) {
                const float w = hud.TextWidth("arial", 14, p.text);
                const float x = p.x - w * 0.5f;
                // Drawn four times in black, one pixel out in each direction,
                // before the label itself. A nameplate lands on whatever the
                // world happens to be behind it - pale stone, a lit doorway -
                // and plain coloured text on that is unreadable exactly when
                // it matters. An outline costs four more quads and works over
                // anything.
                for (int oy = -1; oy <= 1; oy += 2)
                    for (int ox = -1; ox <= 1; ox += 2)
                        hud.Text("arial", 14, x + float(ox), p.y + float(oy), p.text,
                                 0xff000000u);
                hud.Text("arial", 14, x, p.y, p.text, 0xff40ffffu);
            }
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
        renderer.DebugText(8,
                           "F1 geometry: %s   F2 collision: %s   F3 nameplates: %s   "
                           "F4 AI: %s   F6 dev: %s%s",
                           geoWire ? "wireframe" : "off",
                           collisionWire ? "dynamic" : "off",
                           nameplates ? "on (20m)" : "off",
                           aiDisabled ? "DISABLED" : "on",
                           devMode ? "ON" : "off",
                           collisionWire
                               ? "   |   green awake, yellow asleep, magenta script, "
                                 "red non-colliding, GREEN BOX = no physics body"
                               : "");
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

}  // namespace painful
