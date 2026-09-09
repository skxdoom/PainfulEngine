#pragma once
#include "../Core/Vectors.h"
#include "../Core/Matrix.h"
#include "../Assets/Mpk.h"
#include "../Assets/ShaderScript.h"
#include "../World/Level.h"
#include "../World/Lighting.h"
#include "../World/Zones.h"
#include "../Core/Frustum.h"
#include "MaterialState.h"
#include "Camera.h"
#include "LightUniforms.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

class ShadowMap;

// Draws a level's static world mesh.
//
// One vertex/index buffer per MPK object, and one draw call per material, since
// materials tile the object's index array as contiguous triangle runs.
class WorldRenderer {
public:
	~WorldRenderer() { Shutdown(); }
	// Owns GPU handles that Shutdown destroys, so it is not copyable: a copy
	// would free them twice.
	WorldRenderer() = default;
	WorldRenderer(const WorldRenderer&) = delete;
	WorldRenderer& operator=(const WorldRenderer&) = delete;

	// shaderDir holds the compiled vs_world.bin / fs_world.bin.
	bool Init(const std::string& shaderDir);
	void Shutdown();
	// Drops the level's chunks and keeps the programs - a level switch.
	void Clear();

	// levelHint is the map name, used to find per-level textures.
	// shaders may be null; material state then falls back to built-in defaults.
	// skipActiveMeshes leaves out the "phys" objects, which the game host draws
	// through EntityRenderer where physics put them; the viewer keeps them.
	void Upload(const MapMesh& map, TextureCache& textures, const std::string& levelHint,
			const LevelInfo& info, ShaderLibrary* shaders = nullptr,
			bool skipActiveMeshes = false);

	// Hides or shows one map object (by its index in MapMesh::objects). What a
	// destructible's release does to its intact twin: World::RemoveEntity on
	// the "statdest" mesh (FUN_101B5010). Docs/Reference/Physics.md
	void SetObjectVisible(size_t object, bool visible);

	// The lights the scripts made this frame. Only the ones flagged
	// IsDynamic reach the world mesh: the placed lights are already in its
	// lightmap, and adding them again would double every torch alcove.
	// Docs/Reference/Lighting.md
	void SetDynamicLights(const std::vector<LightSource>& lights);

	// ambient/fogColor are 0-255 as stored in the level file.
	void Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
			const LevelInfo& info, float timeSeconds);

	// The flashlight's shadow map, read by Draw and written by DrawShadow.
	// Null for the viewer, which has no flashlight.
	void SetShadowMap(const ShadowMap* shadow) { shadow_ = shadow; }
	// The depth pass: every opaque chunk inside the beam. Called AFTER Draw,
	// whose zone set it reuses - the light sits at the camera, so the
	// camera's rooms are the beam's rooms.
	void DrawShadow(bgfx::ViewId view, float timeSeconds);

	size_t drawCalls() const { return drawCalls_; }
	size_t shadowDrawCalls() const { return shadowDrawCalls_; }
	// Chunks that took at least one dynamic light this frame. Zero while
	// lights exist means the per-chunk reach test is rejecting them, which is
	// a bounds problem, not a shading one.
	size_t litChunks() const { return litChunks_; }
	size_t zonesVisible() const { return zonesVisible_; }
	size_t zoneCount() const { return zoneGraph_.zoneCount(); }
	// Disables zone and frustum culling (the --novis flag).
	void SetVisibilityCulling(bool on) { visCulling_ = on; }

	// Diagnostic: 0 = CCW, 1 = CW, 2 = none.
	void SetCullMode(int mode) { cullMode_ = mode; }
	size_t trianglesUploaded() const { return triangles_; }

private:
	struct Batch { // one material of one object
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
		bgfx::TextureHandle diffuse = BGFX_INVALID_HANDLE;
		bgfx::TextureHandle lightmap = BGFX_INVALID_HANDLE;
		// Terrain blending: a material with all four slots filled mixes two
		// TILED terrain textures through a mask. Which slots are which is not
		// guesswork - the tiled pair carry large scale factors (Enclave's
		// ground is 30x30 and 20x20) while the lightmap and mask are 1x1 and
		// map once across the surface.
		bgfx::TextureHandle blend2 = BGFX_INVALID_HANDLE;
		bgfx::TextureHandle mask = BGFX_INVALID_HANDLE;
		bool blended = false;
		bool hasLightmap = false;
		// Per-slot UV transform from the .mpk material (offsetU/V, scaleU/V).
		// Stored as the shader wants it: scale in xy, offset in zw.
		float uvDiffuse[4] = {1, 1, 0, 0};
		float uvBlend[4] = {1, 1, 0, 0};
	};
	struct Chunk { // one MPK object
		bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
		bgfx::IndexBufferHandle ibo = BGFX_INVALID_HANDLE;
		uint32_t indexCount = 0; // the whole buffer, for the depth pass
		std::vector<Batch> batches;
		Mat4 transform;
		MaterialState material; // from the game's .shader scripts
		// Water surfaces take a separate program: a reflection sampled from a
		// cube map through a scrolling normal map. See Docs/Reference/Water.md.
		bool isWater = false;
		Vec3 aabbLo, aabbHi; // world-space bounds, for culling
		std::vector<uint16_t> zones; // every zone the chunk overlaps; empty = always drawn
		size_t object = 0; // index into MapMesh::objects
		bool hidden = false; // SetObjectVisible(false)
	};

	std::vector<Chunk> chunks_;
	bgfx::VertexLayout layout_;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDiffuse_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sLightmap_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uAmbient_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFogColor_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uFog_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uUvAnim_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uDetail_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDetail_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sBlend2_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sMask_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uUv0_ = BGFX_INVALID_HANDLE; // diffuse slot xform
	bgfx::UniformHandle uUv1_ = BGFX_INVALID_HANDLE; // blend slot xform
	bgfx::UniformHandle uTile_ = BGFX_INVALID_HANDLE; // tile[N] stage scaling
	// Water pass: its own program plus the two textures SetupMaterials
	// hardcodes (special/ripples_00 and special/cube_wenecja).
	bgfx::ProgramHandle waterProgram_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sNormal_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sCube_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uEye_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uWater_ = BGFX_INVALID_HANDLE; // bump, fresnel, reflection
	bgfx::UniformHandle uWaterDeep_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uWaterShallow_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle waterNormal_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle waterCube_ = BGFX_INVALID_HANDLE;
	size_t waterChunks_ = 0;
	// Dynamic lights: the same uniforms and the same packing the models use,
	// plus the list each chunk picks its own from.
	LightUniforms lightUniforms_;
	ProjectorMaps projector_;
	TextureCache* textures_ = nullptr; // for the projector maps, loaded on demand
	std::string levelHint_;
	std::vector<LightSource> dynamicLights_;
	std::vector<int> chunkLights_; // scratch: the picked slots, reused per chunk
	const ShadowMap* shadow_ = nullptr;
	size_t shadowDrawCalls_ = 0;
	bgfx::TextureHandle detailTex_ = BGFX_INVALID_HANDLE;
	float detailTile_[2] = {8.2f, 7.1f};
	bool detailOn_ = false;
	size_t drawCalls_ = 0, triangles_ = 0, zonesVisible_ = 0, litChunks_ = 0;
	ZoneGraph zoneGraph_;
	float worldScale_ = 1.f;
	bool visCulling_ = true;
	std::vector<int> cameraZones_; // scratch, reused every frame
	std::vector<bool> zoneVisible_; // scratch, reused every frame
	int cullMode_ = 0;
};

} // namespace painful
