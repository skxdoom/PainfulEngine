#include "EnvCubeMap.h"
#include "SkyRenderer.h"
#include "WorldRenderer.h"
#include "../Core/Log.h"
#include <cmath>

namespace painful {

void EnvCubeMap::Shutdown() {
	for (bgfx::FrameBufferHandle& f : faces_) {
		if (bgfx::isValid(f)) bgfx::destroy(f);
		f = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(cube_)) bgfx::destroy(cube_);
	if (bgfx::isValid(depth_)) bgfx::destroy(depth_);
	cube_ = BGFX_INVALID_HANDLE;
	depth_ = BGFX_INVALID_HANDLE;
	size_ = 0;
}

bool EnvCubeMap::Build(int size) {
	Shutdown();
	const uint16_t s = uint16_t(size);
	cube_ = bgfx::createTextureCube(s, false, 1, bgfx::TextureFormat::RGBA8,
			BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
	const bgfx::TextureFormat::Enum depthFormats[] = {bgfx::TextureFormat::D24S8,
			bgfx::TextureFormat::D32F, bgfx::TextureFormat::D16};
	for (bgfx::TextureFormat::Enum f : depthFormats) {
		if (bgfx::isValid(depth_)) break;
		if (!bgfx::isTextureValid(0, false, 1, f, BGFX_TEXTURE_RT_WRITE_ONLY)) continue;
		// A cube depth too: bgfx wants every attachment of one framebuffer
		// to be the same kind, so each face pairs a colour and a depth layer.
		depth_ = bgfx::createTextureCube(s, false, 1, f, BGFX_TEXTURE_RT_WRITE_ONLY);
	}
	if (!bgfx::isValid(cube_) || !bgfx::isValid(depth_)) {
		LogWarn("env cube map: no %d target", size);
		Shutdown();
		return false;
	}
	for (int face = 0; face < 6; ++face) {
		bgfx::Attachment att[2];
		// No resolve step: Attachment::init defaults to auto-generated mips,
		// which a depth attachment is refused for.
		att[0].init(cube_, bgfx::Access::Write, uint16_t(face), 1, 0, BGFX_RESOLVE_NONE);
		att[1].init(depth_, bgfx::Access::Write, uint16_t(face), 1, 0, BGFX_RESOLVE_NONE);
		faces_[face] = bgfx::createFrameBuffer(2, att, false);
		if (!bgfx::isValid(faces_[face])) {
			LogWarn("env cube map: no framebuffer for face %d", face);
			Shutdown();
			return false;
		}
	}
	size_ = size;
	LogInfo("env cube map: %d per face", size);
	return true;
}

bool EnvCubeMap::Render(bgfx::ViewId base, SkyRenderer* sky, WorldRenderer& world,
		const Camera& camera, float waterLevel, const LevelInfo& info, float timeSeconds,
		int size) {
	if (size < 8) return false;
	if (size != size_ || !ready()) {
		if (failedSize_ == size) return false; // said so once
		if (!Build(size)) { failedSize_ = size; return false; }
	}

	// The eye mirrored about WaterLevel when the level sets one: the cube then
	// reads as a planar reflection from any point on that plane.
	Camera eye = camera;
	eye.fovDegrees = 90.f;
	eye.mirrored = false;
	eye.clipped = false;
	if (waterLevel != 0.f) eye.pos[1] = 2.f * waterLevel - camera.pos[1];

	// bgfx samples cube maps the D3D way, which is left-handed: a right-handed
	// capture cannot match it by rotation, so the engine's own recipe is
	// followed (View::RenderCubemap's face table plus the z it negates in
	// every cube lookup): the +X -X +Y -Y slots looked along their axes, the
	// +Z slot looked along -Z and the -Z slot along +Z, +Y up on the side
	// faces, +Z / -Z up on the poles, and the shaders sample (x, y, -z).
	const float half = 1.5707963f;
	const float yaw[6] = {0.f, 3.1415927f, 0.f, 0.f, -half, half};
	const float pitch[6] = {0.f, 0.f, half, -half, 0.f, 0.f};
	const Vec3 ups[6] = {Vec3{0, 1, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1},
			Vec3{0, 1, 0}, Vec3{0, 1, 0}};
	const uint16_t s = uint16_t(size_);
	for (int face = 0; face < 6; ++face) {
		const bgfx::ViewId skyView = bgfx::ViewId(base + face * 2);
		const bgfx::ViewId worldView = bgfx::ViewId(skyView + 1);
		bgfx::setViewFrameBuffer(skyView, faces_[face]);
		bgfx::setViewFrameBuffer(worldView, faces_[face]);
		bgfx::setViewRect(skyView, 0, 0, s, s);
		bgfx::setViewRect(worldView, 0, 0, s, s);
		bgfx::setViewClear(skyView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
		bgfx::setViewClear(worldView, BGFX_CLEAR_NONE);
		bgfx::setViewMode(skyView, bgfx::ViewMode::Sequential);
		bgfx::touch(skyView);
		eye.yaw = yaw[face];
		eye.pitch = pitch[face];
		eye.up = ups[face];
		if (sky) sky->Draw(skyView, eye, size_, size_, timeSeconds);
		world.Draw(worldView, eye, size_, size_, info, timeSeconds);
	}
	return true;
}

} // namespace painful
