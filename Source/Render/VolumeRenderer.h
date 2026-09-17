#pragma once
#include "../Core/Vectors.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>
#include <vector>

namespace painful {

struct Camera;
struct MapMesh;
class SceneTargets;
class WorldRenderer;

// The map's fog and light volumes: every "vollight" object adds its colour over
// the scene, every "volfog" object blends it in, by how much of the volume lies
// between the camera and the scene, ramped over the volume's End (FOGVOL.Setup).
// The original's stencil and destination-alpha passes are read here from the
// scene's depth instead. Docs/Reference/FogVolumes.md
class VolumeRenderer {
public:
	~VolumeRenderer() { Shutdown(); }
	VolumeRenderer() = default;
	VolumeRenderer(const VolumeRenderer&) = delete;
	VolumeRenderer& operator=(const VolumeRenderer&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	bool ready() const { return bgfx::isValid(composite_); }

	// Takes the volume objects out of the map, placed as the world's chunks are.
	void Upload(const MapMesh& map, float worldScale, const WorldRenderer& world);
	void Clear();
	// FOGVOL.Setup on the object named: Color:Compose (0xAARRGGBB) and End.
	void SetParams(const std::string& name, uint32_t color, float end);
	size_t count() const { return volumes_.size(); }
	size_t drawn() const { return drawn_; }

	// Whether any volume is in the camera's frustum: the frame then keeps the
	// scene in its target, which the volumes read the depth of.
	bool AnyInView(const Camera& camera, int width, int height) const;
	// After the scene's draws: each volume in a visible zone, farthest first, in
	// two views of its own from viewBase. `colorScale` dims the colours while the
	// bloom is on (BloomFX's fourth value); `farClip` caps End as the original does.
	void Draw(const SceneTargets& scene, const Camera& camera, const WorldRenderer& world,
			bgfx::ViewId viewBase, int viewCount, float colorScale, float farClip);

private:
	struct Volume {
		std::string name;
		bool light = false; // "vollight": added; "volfog": blended
		uint32_t color = 0xffffff; // Volume's constructor defaults
		float end = 20.f;
		bgfx::VertexBufferHandle vbo = BGFX_INVALID_HANDLE;
		bgfx::IndexBufferHandle ibo = BGFX_INVALID_HANDLE;
		Vec3 lo, hi; // world space
		std::vector<uint16_t> zones;
		std::vector<Vec3> triangles; // world space, three a triangle: the eye-inside test
	};
	// Ray parity against the volume's triangles; a box miss is outside.
	static bool Contains(const Volume& v, const Vec3& p);
	bool BuildTarget(int width, int height);
	void ReleaseTarget();

	std::vector<Volume> volumes_;
	std::vector<std::pair<float, size_t>> order_; // scratch: distance, volume
	bgfx::VertexLayout layout_;
	bgfx::ProgramHandle faces_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle composite_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle compositeMs_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uVolume_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uColor_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uDepth_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDepth_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sFaces_ = BGFX_INVALID_HANDLE;
	// Full size, RGBA16F: the nearest front and back face over End, and front coverage.
	bgfx::FrameBufferHandle facesFb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle facesTex_ = BGFX_INVALID_HANDLE;
	int targetW_ = 0, targetH_ = 0;
	size_t drawn_ = 0;
};

} // namespace painful
