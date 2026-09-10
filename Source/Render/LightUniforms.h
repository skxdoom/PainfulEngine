#pragma once
#include "../World/Lighting.h"
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

class TextureCache;
class ShadowMap;

// The dynamic lights as both shaders read them.
//
// The world mesh and the models run the SAME per-pixel evaluation
// (Shaders/shared_lights.sh), so they share one packing and one set of uniforms:
// a monster and the wall behind it are lit by identical arithmetic, which is
// the whole point - the original lit models from four per-entity constants and
// the world from a projected cookie, and the two never matched.
// Docs/Reference/Lighting.md
struct LightBlock {
	// x: how many slots are filled. y: which slot carries the projector, -1
	// for none.
	float count[4] = {0.f, -1.f, 0.f, 0.f};
	float pos[kMaxDynamicLights][4] = {}; // xyz world, w range
	// rgb: colour x min(intensity x 0.5, 1) - the value the engine's vertex
	// stage hands the light pass (WorldMesh::RenderLightPass). The x2 / x4 the
	// pixel shader applies on top is in shared_lights.sh.
	// w: type, 2 point, 3 spot.
	float color[kMaxDynamicLights][4] = {};
	float axis[kMaxDynamicLights][4] = {}; // xyz spot axis, w cos(outer), -1 none
	float cone[kMaxDynamicLights][4] = {}; // x cos(inner), y tan(outer half-angle)
	float projX[4] = {1.f, 0.f, 0.f, 0.f}; // the projector's right vector
	float projY[4] = {0.f, 1.f, 0.f, 0.f}; // and its up vector
	// The projector's shadow map: world -> shadow uv/depth, and (on, normal
	// offset, light offset, 1/size) with the offsets in texels. Off by default.
	float shadowMtx[16] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
	float shadowParams[4] = {0.f, 0.f, 0.f, 0.f};
	// The model shadows' orthographic map: matrix, (strength, normal offset,
	// light offset, 1/size), (to the light, the PAINFUL_SHADOWVIEW flag).
	float dirShadowMtx[16] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
	float dirShadowParams[4] = {0.f, 0.f, 0.f, 0.f};
	float dirShadowDir[4] = {0.f, 1.f, 0.f, 0.f};
	float dirShadowFade[4] = {0.f, 0.f, 0.f, 0.f}; // x: edge fade width (uv)
};

// Writes one light into a slot. projName is the projector texture the renderer
// has actually loaded; a light asking for a different one gets no cookie.
void PackLight(LightBlock& block, int slot, const LightSource& light,
		const std::string& projName);
// Writes the shadow map's matrix and offsets; null or inactive leaves the
// block reading "no shadow".
void PackShadow(LightBlock& block, const ShadowMap* shadow);
// Same for the model shadows' orthographic map.
void PackDirShadow(LightBlock& block, const ShadowMap* shadow);

// The bgfx handles behind LightBlock. Both renderers own one; bgfx refcounts
// uniforms by name, so creating the same names twice is a share, not a clash -
// which is also why the array lengths have to agree, and they do, through
// kMaxDynamicLights.
class LightUniforms {
public:
	~LightUniforms() { Shutdown(); }
	LightUniforms() = default;
	LightUniforms(const LightUniforms&) = delete;
	LightUniforms& operator=(const LightUniforms&) = delete;

	void Init();
	void Shutdown();
	// Sets the uniforms and binds the two projector maps and the shadow map.
	// Every sampler is declared in the shader, so each is bound whether a
	// projector light exists or not; an invalid shadow handle binds nothing,
	// which the shader never reads while the block says off.
	void Submit(const LightBlock& block, int projStage, int projFallStage,
			bgfx::TextureHandle proj, bgfx::TextureHandle projFall,
			int shadowStage, bgfx::TextureHandle shadow,
			int dirShadowStage, bgfx::TextureHandle dirShadow) const;

private:
	bgfx::UniformHandle count_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle pos_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle color_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle axis_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle cone_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle projX_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle projY_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sProj_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sProjFall_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle shadowMtx_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle shadowParams_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sShadow_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle dirShadowMtx_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle dirShadowParams_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle dirShadowDir_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle dirShadowFade_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDirShadow_ = BGFX_INVALID_HANDLE;
};

// The projector maps, loaded once for whichever light asks for one. Only
// PlayerLight does, with "special/flashlight".
class ProjectorMaps {
public:
	// Returns true when `name` is the one now loaded.
	bool Resolve(const std::string& name, TextureCache& textures,
			const std::string& levelHint);
	void Clear() {
		name_.clear();
		cookie_ = BGFX_INVALID_HANDLE;
		falloff_ = BGFX_INVALID_HANDLE;
	}
	const std::string& name() const { return name_; }
	bgfx::TextureHandle cookie() const { return cookie_; }
	bgfx::TextureHandle falloff() const { return falloff_; }

private:
	std::string name_;
	bgfx::TextureHandle cookie_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle falloff_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
