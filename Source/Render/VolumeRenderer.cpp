#include "VolumeRenderer.h"
#include "Camera.h"
#include "GpuBuffers.h"
#include "Renderer.h"
#include "SceneTargets.h"
#include "ShaderLoad.h"
#include "WorldRenderer.h"
#include "../Assets/Mpk.h"
#include "../Core/Frustum.h"
#include "../Core/Log.h"

#include <algorithm>

namespace painful {

namespace {

constexpr uint64_t kPointClamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

bgfx::ProgramHandle Program(const std::string& dir, const char* vs, const char* fs) {
	bgfx::ShaderHandle v = LoadShader(dir, vs);
	bgfx::ShaderHandle f = LoadShader(dir, fs);
	if (!bgfx::isValid(v) || !bgfx::isValid(f)) {
		if (bgfx::isValid(v)) bgfx::destroy(v);
		if (bgfx::isValid(f)) bgfx::destroy(f);
		return BGFX_INVALID_HANDLE;
	}
	return bgfx::createProgram(v, f, true);
}

} // namespace

bool VolumeRenderer::Init(const std::string& shaderDir) {
	layout_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
	faces_ = Program(shaderDir, "vs_volume", "fs_volume_faces");
	composite_ = Program(shaderDir, "vs_volume", "fs_volume");
	compositeMs_ = Program(shaderDir, "vs_volume", "fs_volume_ms");
	uVolume_ = bgfx::createUniform("u_volume", bgfx::UniformType::Vec4);
	uColor_ = bgfx::createUniform("u_volumeColor", bgfx::UniformType::Vec4);
	uDepth_ = bgfx::createUniform("u_volumeDepth", bgfx::UniformType::Vec4);
	sDepth_ = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
	sFaces_ = bgfx::createUniform("s_volumeFaces", bgfx::UniformType::Sampler);
	const bool ok = bgfx::isValid(faces_) && bgfx::isValid(composite_);
	if (!ok) {
		LogWarn("volumes: shaders missing, fog and light volumes off");
		Shutdown();
	}
	return ok;
}

void VolumeRenderer::Shutdown() {
	Clear();
	ReleaseTarget();
	for (bgfx::ProgramHandle* p : {&faces_, &composite_, &compositeMs_}) {
		if (bgfx::isValid(*p)) bgfx::destroy(*p);
		*p = BGFX_INVALID_HANDLE;
	}
	for (bgfx::UniformHandle* u : {&uVolume_, &uColor_, &uDepth_, &sDepth_, &sFaces_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
}

void VolumeRenderer::Clear() {
	for (Volume& v : volumes_) {
		if (bgfx::isValid(v.vbo)) bgfx::destroy(v.vbo);
		if (bgfx::isValid(v.ibo)) bgfx::destroy(v.ibo);
	}
	volumes_.clear();
	drawn_ = 0;
}

// World::LoadMeshPakFile tests "vollight" before "volfog" (type 0, then 1).
void VolumeRenderer::Upload(const MapMesh& map, float worldScale, const WorldRenderer& world) {
	Clear();
	if (!ready()) return;
	for (const MapObject& o : map.objects) {
		const bool light = o.nameHas("vollight");
		if ((!light && !o.nameHas("volfog")) || o.vertexCount() == 0 || o.indices.empty()) continue;
		Mat4 transform = o.transform;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 3; ++c) transform.m[r * 4 + c] *= worldScale;
		std::vector<float> positions(o.vertexCount() * 3);
		Volume v;
		v.name = o.name;
		v.light = light;
		v.lo = Vec3(1e30f);
		v.hi = Vec3(-1e30f);
		for (size_t i = 0; i < o.vertexCount(); ++i) {
			Vec3 raw, w;
			o.position(i, raw);
			transform.TransformPoint(raw.x, raw.y, raw.z, w);
			positions[i * 3] = w.x;
			positions[i * 3 + 1] = w.y;
			positions[i * 3 + 2] = w.z;
			v.lo = Vec3(std::min(v.lo.x, w.x), std::min(v.lo.y, w.y), std::min(v.lo.z, w.z));
			v.hi = Vec3(std::max(v.hi.x, w.x), std::max(v.hi.y, w.y), std::max(v.hi.z, w.z));
		}
		v.triangles.reserve(o.indices.size());
		for (uint16_t index : o.indices)
			v.triangles.emplace_back(positions[index * 3], positions[index * 3 + 1], positions[index * 3 + 2]);
		v.vbo = MakeVertexBuffer(positions.data(), uint32_t(positions.size() * sizeof(float)), layout_);
		v.ibo = MakeIndexBuffer(o.indices.data(), uint32_t(o.indices.size()));
		world.ZonesForBox(o.bboxMin, o.bboxMax, v.zones);
		volumes_.push_back(std::move(v));
	}
	if (!volumes_.empty()) LogInfo("volumes: %zu", volumes_.size());
}

void VolumeRenderer::SetParams(const std::string& name, uint32_t color, float end) {
	for (Volume& v : volumes_) {
		if (v.name != name) continue;
		v.color = color;
		v.end = end;
	}
}

bool VolumeRenderer::Contains(const Volume& v, const Vec3& p) {
	if (p.x < v.lo.x || p.y < v.lo.y || p.z < v.lo.z || p.x > v.hi.x || p.y > v.hi.y || p.z > v.hi.z)
		return false;
	// Skewed off the axes so the ray does not run along an authored edge.
	const Vec3 dir(0.9986f, 0.0431f, 0.0297f);
	int crossings = 0;
	for (size_t t = 0; t + 2 < v.triangles.size(); t += 3) {
		const Vec3& a = v.triangles[t];
		const Vec3 e1 = v.triangles[t + 1] - a, e2 = v.triangles[t + 2] - a;
		const Vec3 h = Cross(dir, e2);
		const float det = Dot(e1, h);
		if (det > -1e-8f && det < 1e-8f) continue;
		const Vec3 s = p - a;
		const float u = Dot(s, h) / det;
		if (u < 0.f || u > 1.f) continue;
		const Vec3 q = Cross(s, e1);
		const float w = Dot(dir, q) / det;
		if (w < 0.f || u + w > 1.f) continue;
		if (Dot(e2, q) / det > 0.f) ++crossings;
	}
	return (crossings & 1) != 0;
}

bool VolumeRenderer::AnyInView(const Camera& camera, int width, int height) const {
	if (volumes_.empty()) return false;
	float view[16], proj[16];
	camera.ViewProj(width, height, camera.farPlane, view, proj);
	const Frustum frustum = Frustum::FromViewProj(view, proj);
	for (const Volume& v : volumes_)
		if (frustum.VisibleAabb(v.lo, v.hi)) return true;
	return false;
}

void VolumeRenderer::ReleaseTarget() {
	if (bgfx::isValid(facesFb_)) bgfx::destroy(facesFb_);
	facesFb_ = BGFX_INVALID_HANDLE;
	facesTex_ = BGFX_INVALID_HANDLE;
	targetW_ = targetH_ = 0;
}

bool VolumeRenderer::BuildTarget(int width, int height) {
	ReleaseTarget();
	facesTex_ = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1, bgfx::TextureFormat::RGBA16F,
			BGFX_TEXTURE_RT | kPointClamp);
	if (bgfx::isValid(facesTex_)) facesFb_ = bgfx::createFrameBuffer(1, &facesTex_, true);
	if (!bgfx::isValid(facesFb_)) {
		if (bgfx::isValid(facesTex_)) bgfx::destroy(facesTex_);
		facesTex_ = BGFX_INVALID_HANDLE;
		LogWarn("volumes: no target at %dx%d, fog and light volumes off", width, height);
		return false;
	}
	targetW_ = width;
	targetH_ = height;
	return true;
}

void VolumeRenderer::Draw(const SceneTargets& scene, const Camera& camera, const WorldRenderer& world,
		bgfx::ViewId viewBase, int viewCount, float colorScale, float farClip) {
	drawn_ = 0;
	if (!ready() || volumes_.empty() || !scene.active() || !scene.depthReadable()) return;
	const bool multisampled = scene.msaa() >= 2;
	if (multisampled && !bgfx::isValid(compositeMs_)) return;
	const int w = scene.width(), h = scene.height();
	if ((w != targetW_ || h != targetH_) && !BuildTarget(w, h)) return;

	float view[16], proj[16];
	camera.ViewProj(w, h, camera.farPlane, view, proj);
	const Frustum frustum = Frustum::FromViewProj(view, proj);
	order_.clear();
	for (size_t i = 0; i < volumes_.size(); ++i) {
		const Volume& v = volumes_[i];
		if (v.end <= 0.f || !frustum.VisibleAabb(v.lo, v.hi) || !world.ZonesVisible(v.zones)) continue;
		const Vec3 centre((v.lo.x + v.hi.x) * 0.5f, (v.lo.y + v.hi.y) * 0.5f, (v.lo.z + v.hi.z) * 0.5f);
		const Vec3 d(centre.x - camera.pos[0], centre.y - camera.pos[1], centre.z - camera.pos[2]);
		order_.emplace_back(d.x * d.x + d.y * d.y + d.z * d.z, i);
	}
	std::sort(order_.begin(), order_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

	const bgfx::Caps* caps = bgfx::getCaps();
	const float depthParams[4] = {proj[10], proj[14], caps->homogeneousDepth ? 1.f : 0.f, 0.f};
	const size_t cap = std::min(order_.size(), size_t(std::max(viewCount / 2, 0)));
	// Farthest first, and past the view budget the farthest are the ones left out.
	const size_t skip = order_.size() - cap;
	for (size_t k = skip; k < order_.size(); ++k) {
		const Volume& v = volumes_[order_[k].second];
		const bgfx::ViewId facesView = bgfx::ViewId(viewBase + 2 * (k - skip));
		const bgfx::ViewId colorView = bgfx::ViewId(facesView + 1);
		// RenderWorld caps End just short of the far clip (0x100b3dfc).
		const float end = std::min(v.end, std::max(farClip - 1.f, 0.001f));
		const bool inside = Contains(v, Vec3(camera.pos[0], camera.pos[1], camera.pos[2]));
		const float params[4] = {1.f / end, v.light ? 1.f : 0.f, inside ? 1.f : 0.f, 0.f};

		// The faces: nothing tested against the scene, the composite does that.
		bgfx::setViewFrameBuffer(facesView, facesFb_);
		Renderer::SetViewCamera(facesView, camera, w, h);
		bgfx::setViewClear(facesView, BGFX_CLEAR_COLOR, 0xffffff00);
		bgfx::setVertexBuffer(0, v.vbo);
		bgfx::setIndexBuffer(v.ibo);
		bgfx::setUniform(uVolume_, params);
		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
				BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) |
				BGFX_STATE_BLEND_EQUATION_SEPARATE(BGFX_STATE_BLEND_EQUATION_MIN, BGFX_STATE_BLEND_EQUATION_MAX));
		bgfx::submit(facesView, faces_);

		// The colour, over the volume's back faces as the original's colour pass draws.
		const float rgba[4] = {float((v.color >> 16) & 0xff) / 255.f * colorScale,
				float((v.color >> 8) & 0xff) / 255.f * colorScale, float(v.color & 0xff) / 255.f * colorScale,
				float((v.color >> 24) & 0xff) / 255.f};
		bgfx::setViewFrameBuffer(colorView, scene.colorFramebuffer());
		Renderer::SetViewCamera(colorView, camera, w, h);
		bgfx::setVertexBuffer(0, v.vbo);
		bgfx::setIndexBuffer(v.ibo);
		bgfx::setUniform(uVolume_, params);
		bgfx::setUniform(uColor_, rgba);
		bgfx::setUniform(uDepth_, depthParams);
		bgfx::setTexture(0, sDepth_, scene.depth(), kPointClamp);
		bgfx::setTexture(1, sFaces_, facesTex_, kPointClamp);
		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_CULL_CW |
				(v.light ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) : BGFX_STATE_BLEND_ALPHA));
		bgfx::submit(colorView, multisampled ? compositeMs_ : composite_);
		++drawn_;
	}
}

} // namespace painful
