#pragma once
#include "Camera.h"
#include "SdfLighting.h"
#include <bgfx/bgfx.h>
#include <string>

namespace painful {

// pfsdfdebugclipmaps: SdfLighting's cascades raymarched per pixel over the
// finished frame, with the trace's own rule - each step in the finest cascade
// holding it, never past its face - and the light bin facing the eye: what a
// ray from the camera would take at each surface.
// Docs/Reference/Lighting.md, "Distance field ambient".
class SdfClipmapDebug {
public:
	~SdfClipmapDebug() { Shutdown(); }
	SdfClipmapDebug() = default;
	SdfClipmapDebug(const SdfClipmapDebug&) = delete;
	SdfClipmapDebug& operator=(const SdfClipmapDebug&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	// A level went away: the textures go with it.
	void Clear();
	// Uploads each cascade the first time it is seen, then draws over `view`, a
	// backbuffer view of its own after the post chain.
	void Draw(bgfx::ViewId view, const Camera& camera, int width, int height, const SdfLighting& sdf);

private:
	struct Cascade {
		bgfx::TextureHandle distance = BGFX_INVALID_HANDLE; // 3D R8, quarter voxels
		bgfx::TextureHandle surface = BGFX_INVALID_HANDLE; // 3D R32F, the nearest surface's index
		bgfx::TextureHandle bins = BGFX_INVALID_HANDLE; // RGBA32F, six texels a surface
		Vec3 origin;
		float voxel = 0.f;
		uint32_t id = 0;
	};
	void Upload(Cascade& c, const SdfLighting::Volume& vol);
	void Release(Cascade& c);

	Cascade cascades_[SdfLighting::kCascades];
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::VertexLayout layout_;
	// Bound where a cascade is not built yet, so every sampler has a texture.
	bgfx::TextureHandle empty3D_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle empty2D_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDistance_[SdfLighting::kCascades] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle sSurface_[SdfLighting::kCascades] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle sBins_[SdfLighting::kCascades] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
			BGFX_INVALID_HANDLE};
	bgfx::UniformHandle uInvViewProj_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uEye_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uWindow_ = BGFX_INVALID_HANDLE; // one per cascade
	bgfx::UniformHandle uGrid_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uScreen_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
