#pragma once

// Private to Tools/ - the shared preamble and every command's declaration.
//
// PainfulTools is a set of reports over the shipped data: each one resolves what
// it needs and prints what it found, so a change can be checked without opening
// a window. The commands keep their own natural signatures; ToolsMain.cpp adapts
// them to the dispatch table with a captureless lambda apiece.
//
// This header is not installed and is included only by the Tools translation
// units, which is why the using-directive is acceptable here.

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

// -------------------------------------------------------------- the viewer
int RunCmd(const char* levelDir, const char* dataRoot,
           const std::string& shotPath, const char* exePath,
           const float* startPos, const float* startAngles,
           int cullMode, int entityCull, float entityScale, bool skyOnly,
           bool novis, bool startNoclip, bool startPhysicsDebug);

// --------------------------------------------------------------- the level
int LevelCmd(const char* levelDir, const char* dataRoot);
int EntitiesCmd(const char* levelDir, const char* dataRoot, const char* type);
int FitCmd(const char* levelDir, const char* dataRoot);
int LevelsCmd(const char* dataRoot);
int FilesCmd(const char* dataRoot, const char* dir);
int LightingCmd(const char* levelDir, const char* dataRoot,
                const float* at, const float* eye);
int ZonesCmd(const char* levelDir, const char* dataRoot, const float* pos);
int GroundCmd(const char* levelDir, const char* dataRoot,
              float x, float y, float z, float radius);
int ScaleCmd(const char* levelDir, const char* dataRoot);

// ---------------------------------------------------------------- geometry
int DatCmd(const char* path);
int BonesCmd(const char* path, const char* animName, const char* timeArg,
             const char* rotArg);
int MapCmd(const char* path);
int MatsCmd(const char* path, const char* nameFilter);
int HitboxesCmd(const char* modelPath);
int ModelCmd(const char* path);

// ---------------------------------------------------------------- textures
int SkyDumpCmd(const char* path);
int SkyTexCmd(const char* levelDir, const char* dataRoot);
int TexDumpCmd(const char* dataRoot, const char* name, const char* outPath);
int ResolveCmd(const char* dataRoot, const char* name);
int TexturesCmd(const char* mapPath, const char* dataRoot, const char* hint);

// ------------------------------------------------------------------ render
int ShadersCmd(const char* dataRoot, const char* single);
int ParticlesCmd(const char* levelDir, const char* dataRoot);
int BillboardsCmd(const char* levelDir, const char* dataRoot);

// ----------------------------------------------------------------- physics
int RagdollDropCmd(const char* levelDir, const char* dataRoot, const char* modelName);
int PhysicsCmd(const char* levelDir, const char* dataRoot);
int RagdollCmd(const char* path, const char* modelsRoot);

// ------------------------------------------------------------------ script
int LuaCmd(const char* dataRoot, int frames, const char* level, const char* exec);
int BlendCmd(const char* path, const char* animA, const char* animB,
             const char* timeArg);
int WpsCmd(const char* path);
int SoundCmd(const char* root, const char* name, const char* seconds);
int MkLevelCmd(const char* dataRoot, const char* levelName, float extent,
               float height, const char* texture, const char* steps,
               const char* lightmap);
int PoseCmd(const char* modelPath, const char* animName, const char* timeArg);
