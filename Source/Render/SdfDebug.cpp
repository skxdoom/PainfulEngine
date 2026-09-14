#include "SdfDebug.h"
#include "SdfLighting.h"
#include "ShaderLoad.h"
#include "../World/Lighting.h"

#include <cmath>

namespace painful {

bool SdfDebug::Init(const std::string& shaderDir) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_sdfprobe");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_sdfprobe");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
		if (bgfx::isValid(vs)) bgfx::destroy(vs);
		if (bgfx::isValid(fs)) bgfx::destroy(fs);
		return false;
	}
	program_ = bgfx::createProgram(vs, fs, true);

	// A unit UV sphere, position and normal.
	constexpr int kRings = 8, kSegments = 16;
	std::vector<float> verts;
	std::vector<uint16_t> indices;
	for (int r = 0; r <= kRings; ++r) {
		const float phi = kPi * float(r) / float(kRings);
		for (int s = 0; s <= kSegments; ++s) {
			const float theta = 2.f * kPi * float(s) / float(kSegments);
			const float x = std::sin(phi) * std::cos(theta), y = std::cos(phi), z = std::sin(phi) * std::sin(theta);
			verts.insert(verts.end(), {x, y, z, x, y, z});
		}
	}
	for (int r = 0; r < kRings; ++r) {
		for (int s = 0; s < kSegments; ++s) {
			const uint16_t a = uint16_t(r * (kSegments + 1) + s), b = uint16_t(a + kSegments + 1);
			indices.insert(indices.end(), {a, b, uint16_t(a + 1), uint16_t(a + 1), b, uint16_t(b + 1)});
		}
	}
	bgfx::VertexLayout layout;
	layout.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
			.end();
	vbo_ = bgfx::createVertexBuffer(bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(float))), layout);
	ibo_ = bgfx::createIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));
	indexCount_ = uint32_t(indices.size());
	uSh_ = bgfx::createUniform("u_sh", bgfx::UniformType::Vec4, 9);
	uParams_ = bgfx::createUniform("u_sdfProbe", bgfx::UniformType::Vec4);
	return bgfx::isValid(program_) && bgfx::isValid(vbo_) && bgfx::isValid(ibo_);
}

void SdfDebug::Shutdown() {
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	if (bgfx::isValid(vbo_)) bgfx::destroy(vbo_);
	if (bgfx::isValid(ibo_)) bgfx::destroy(ibo_);
	if (bgfx::isValid(uSh_)) bgfx::destroy(uSh_);
	if (bgfx::isValid(uParams_)) bgfx::destroy(uParams_);
	program_ = BGFX_INVALID_HANDLE;
	vbo_ = BGFX_INVALID_HANDLE;
	ibo_ = BGFX_INVALID_HANDLE;
	uSh_ = uParams_ = BGFX_INVALID_HANDLE;
	probes_.clear();
}

void SdfDebug::Draw(bgfx::ViewId view, const Camera& camera, const SdfLighting& sdf,
		const EntityLighting& lighting, float gain) {
	if (!bgfx::isValid(program_) || !sdf.ready()) return;

	// The lattice follows the camera a whole cell at a time; a probe that stays
	// on it keeps its trace.
	const int cell[3] = {int(std::floor(camera.pos.x / kSpacing)),
			int(std::floor(camera.pos.y / kSpacing)), int(std::floor(camera.pos.z / kSpacing))};
	const int wide = 2 * kHalfWide + 1, high = 2 * kHalfHigh + 1;
	if (probes_.empty() || cell[0] != cell_[0] || cell[1] != cell_[1] || cell[2] != cell_[2]) {
		std::vector<Probe> next(size_t(wide) * size_t(high) * size_t(wide));
		const bool keep = !probes_.empty();
		for (int z = 0; z < wide; ++z)
		for (int y = 0; y < high; ++y)
		for (int x = 0; x < wide; ++x) {
			Probe& p = next[(size_t(z) * high + size_t(y)) * wide + size_t(x)];
			const int lx = cell[0] + x - kHalfWide, ly = cell[1] + y - kHalfHigh, lz = cell[2] + z - kHalfWide;
			p.pos = Vec3{float(lx) * kSpacing, float(ly) * kSpacing, float(lz) * kSpacing};
			const int ox = lx - cell_[0] + kHalfWide, oy = ly - cell_[1] + kHalfHigh, oz = lz - cell_[2] + kHalfWide;
			if (keep && ox >= 0 && oy >= 0 && oz >= 0 && ox < wide && oy < high && oz < wide)
				p = probes_[(size_t(oz) * high + size_t(oy)) * wide + size_t(ox)];
		}
		probes_ = std::move(next);
		for (int a = 0; a < 3; ++a) cell_[a] = cell[a];
	}

	int budget = kTracesPerFrame;
	const float params[4] = {gain, 0.f, 0.f, 0.f};
	float transform[16] = {kRadius, 0, 0, 0, 0, kRadius, 0, 0, 0, 0, kRadius, 0, 0, 0, 0, 1};
	const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
			BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
	for (Probe& p : probes_) {
		if ((!p.traced || p.generation != sdf.generation()) && budget > 0) {
			--budget;
			// Rays that leave the window take the box ambient here, as a model's do.
			EntityLightFade snap;
			EntityLightState lit;
			lighting.Evaluate(p.pos, 0.f, 0.f, snap, lit);
			float weight = 0.f;
			p.traced = sdf.Trace(p.pos, lit.ambient, p.sh, weight);
			p.generation = sdf.generation();
			const float d = sdf.Distance(p.pos);
			p.visible = p.traced && weight > 0.f &&
					(d < 0.f || d > kRadius + SdfLighting::kVoxel0 * 0.5f);
		}
		if (!p.visible) continue;
		float sh[36];
		for (int k = 0; k < 9; ++k) {
			for (int c = 0; c < 3; ++c) sh[k * 4 + c] = p.sh[k * 3 + c];
			sh[k * 4 + 3] = 0.f;
		}
		transform[12] = p.pos.x;
		transform[13] = p.pos.y;
		transform[14] = p.pos.z;
		bgfx::setUniform(uSh_, sh, 9);
		bgfx::setUniform(uParams_, params);
		bgfx::setTransform(transform);
		bgfx::setVertexBuffer(0, vbo_);
		bgfx::setIndexBuffer(ibo_, 0, indexCount_);
		bgfx::setState(state);
		bgfx::submit(view, program_);
	}
}

} // namespace painful
