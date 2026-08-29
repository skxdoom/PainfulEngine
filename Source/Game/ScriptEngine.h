#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../Script/LuaHost.h"

namespace painful {

class EntityRenderer;
class TextureCache;

// The engine-side state behind the script natives: the entity registry that
// ENTITY.* manipulates and the world state WORLD.* accumulates. This is the
// seam between the Lua layer and the engine subsystems - the same seam the
// original engine has, where every native takes an entity handle first.
//
// Headless-safe by design: with no renderer attached, Create still hands out
// real handles and the registry tracks transforms - which is what the `lua`
// diagnostic exercises. The windowed game loop attaches the renderer and the
// same natives start putting things on screen.
class ScriptEngine {
public:
    // ETypes, verbatim from Definitions.lua.
    enum EType {
        kNone = 0, kMesh = 1, kLight = 2, kParticleFX = 3, kModel = 4,
        kRegion = 5, kDecal = 6, kBillboard = 8, kEnvironment = 9,
        kTrail = 10, kSound = 11,
    };

    struct Entity {
        int type = kNone;
        std::string source;         // model name (Model) or pack path (Mesh)
        std::string mesh;           // pack object name (Mesh only)
        std::string name;           // the "Name:Tag" the script passed
        float scale = 1.f;
        float pos[3] = {0, 0, 0};
        float rotWXYZ[4] = {1, 0, 0, 0};
        bool visible = true;
        bool inWorld = false;       // WORLD.AddEntity was called
        bool worldObject = false;   // WORLD.FindEntityByName pseudo-entity
        int rendererInstance = -1;  // EntityRenderer slot, -1 when headless/unresolved
    };

    // What the scripts told WORLD.* to set up; the game loop turns this into
    // the actual renderer state. Colours arrive packed by our own R3D.RGBA
    // (Color:Compose routes through it), so the layout is ours on both ends.
    struct WorldState {
        std::string mapPath;        // engine-style, "../Data/Maps/<map>"
        std::string levelName;
        float scale = 1.f;
        bool overbright = false;
        bool loadRequested = false;

        int fogMode = 0;
        float fogStart = 0.f, fogEnd = 90.f, fogDensity = 0.f;
        float fogColor[3] = {0, 0, 0};          // 0-255
        float farClip = 1024.f;
        float ambient[3] = {128, 128, 128};     // 0-255
        std::string detailTex;
        float detailTileU = 8.2f, detailTileV = 7.1f;
    };

    // Installs the real natives over their stubs. Call after LuaHost::Init.
    void Bind(LuaHost& host);

    // Attaches the render side; entities created from then on (and existing
    // ones, on Flush) become renderer instances. dataRoot is the mounted
    // data directory.
    void AttachRenderer(EntityRenderer* entities, TextureCache* textures,
                        const std::string& dataRoot);

    const WorldState& world() const { return world_; }
    void ClearLoadRequest() { world_.loadRequested = false; }
    const std::unordered_map<int, Entity>& entities() const { return entities_; }
    size_t created() const { return created_; }
    size_t released() const { return released_; }

    // Pushes every registry entity that has none into the attached renderer.
    void FlushToRenderer();

private:
    static ScriptEngine* From(lua_State* L);

    Entity* Find(int handle);
    void SyncPose(Entity& e);
    void CreateRendererInstance(Entity& e);

    // --- natives ---
    static int L_Create(lua_State* L);
    static int L_Release(lua_State* L);
    static int L_SetPosition(lua_State* L);
    static int L_GetPosition(lua_State* L);
    static int L_SetRotationQ(lua_State* L);
    static int L_GetRotationQ(lua_State* L);
    static int L_SetOrientation(lua_State* L);
    static int L_GetOrientation(lua_State* L);
    static int L_EnableDraw(lua_State* L);
    static int L_GetVelocity(lua_State* L);
    static int L_PO_Exist(lua_State* L);
    static int L_PO_GetMaxSphereRay(lua_State* L);
    static int L_WORLD_AddEntity(lua_State* L);
    static int L_WORLD_FindEntityByName(lua_State* L);
    static int L_WORLD_LoadMap(lua_State* L);
    static int L_WORLD_SetupFog(lua_State* L);
    static int L_WORLD_SetFarClipDist(lua_State* L);
    static int L_WORLD_AmbientColor(lua_State* L);
    static int L_MESH_SetDefaultDetailMaps(lua_State* L);

    LuaHost* host_ = nullptr;
    std::unordered_map<int, Entity> entities_;
    int nextHandle_ = 1;
    size_t created_ = 0, released_ = 0;
    WorldState world_;

    EntityRenderer* renderer_ = nullptr;
    TextureCache* textures_ = nullptr;
    std::string dataRoot_;
};

} // namespace painful
