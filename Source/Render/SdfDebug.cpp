#include "SdfDebug.h"
#include "SdfField.h"
#include "SdfVertexLight.h"
#include "ShaderLoad.h"

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

	// A unit UV sphere: position, normal, and the vertex's index in its second UV.
	constexpr int kRings = 8, kSegments = 16;
	std::vector<float> verts;
	std::vector<uint16_t> indices;
	sphere_.clear();
	for (int r = 0; r <= kRings; ++r) {
		const float phi = kPi * float(r) / float(kRings);
		for (int s = 0; s <= kSegments; ++s) {
			const float theta = 2.f * kPi * float(s) / float(kSegments);
			const float x = std::sin(phi) * std::cos(theta), y = std::cos(phi), z = std::sin(phi) * std::sin(theta);
			verts.insert(verts.end(), {x, y, z, x, y, z, float(sphere_.size()), 0.f});
			sphere_.push_back(Vec3{x, y, z});
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
			.add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
			.end();
	vbo_ = bgfx::createVertexBuffer(bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(float))), layout);
	ibo_ = bgfx::createIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));
	indexCount_ = uint32_t(indices.size());
	uProbe_ = bgfx::createUniform("u_sdfProbe", bgfx::UniformType::Vec4);
	uAmbient_ = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
	return bgfx::isValid(program_) && bgfx::isValid(vbo_) && bgfx::isValid(ibo_);
}

void SdfDebug::Shutdown() {
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	if (bgfx::isValid(vbo_)) bgfx::destroy(vbo_);
	if (bgfx::isValid(ibo_)) bgfx::destroy(ibo_);
	if (bgfx::isValid(uProbe_)) bgfx::destroy(uProbe_);
	if (bgfx::isValid(uAmbient_)) bgfx::destroy(uAmbient_);
	program_ = BGFX_INVALID_HANDLE;
	vbo_ = BGFX_INVALID_HANDLE;
	ibo_ = BGFX_INVALID_HANDLE;
	uProbe_ = uAmbient_ = BGFX_INVALID_HANDLE;
	probes_.clear();
}

void SdfDebug::Draw(bgfx::ViewId view, const Camera& camera, const SdfField& field, SdfVertexLight& light,
		float gain) {
	light.Unreserve();
	if (!bgfx::isValid(program_) || !field.ready()) return;
	const int perProbe = int(sphere_.size());

	// The lattice follows the camera a whole cell at a time, and is traced afresh.
	const int cell[3] = {int(std::floor(camera.pos.x / kSpacing)),
			int(std::floor(camera.pos.y / kSpacing)), int(std::floor(camera.pos.z / kSpacing))};
	const int wide = 2 * kHalfWide + 1, high = 2 * kHalfHigh + 1;
	if (probes_.empty() || cell[0] != cell_[0] || cell[1] != cell_[1] || cell[2] != cell_[2] ||
			generation_ != light.generation()) {
		if (slots_ >= 0 && generation_ == light.generation()) light.Release(slots_, slotCount_);
		probes_.assign(size_t(wide) * size_t(high) * size_t(wide), Probe());
		for (int z = 0; z < wide; ++z)
		for (int y = 0; y < high; ++y)
		for (int x = 0; x < wide; ++x) {
			Probe& p = probes_[(size_t(z) * high + size_t(y)) * wide + size_t(x)];
			p.pos = Vec3{float(cell[0] + x - kHalfWide) * kSpacing, float(cell[1] + y - kHalfHigh) * kSpacing,
					float(cell[2] + z - kHalfWide) * kSpacing};
		}
		slotCount_ = int(probes_.size()) * perProbe;
		slots_ = light.Allocate(slotCount_);
		generation_ = light.generation();
		cursor_ = 0;
		for (int a = 0; a < 3; ++a) cell_[a] = cell[a];
	}
	if (slots_ < 0) return;

	for (Probe& p : probes_)
		if (p.queued && !p.traced && light.WasTraced(p.queuedFrame)) p.traced = true;
	// The probes in turn, as far as the models left the budget.
	for (size_t n = 0; n < probes_.size() && light.budget() >= perProbe; ++n) {
		const size_t i = cursor_;
		cursor_ = (cursor_ + 1) % probes_.size();
		Probe& p = probes_[i];
		const int first = slots_ + int(i) * perProbe;
		for (int v = 0; v < perProbe; ++v)
			light.Queue(p.pos + sphere_[size_t(v)] * kRadius, sphere_[size_t(v)], first + v, !p.traced);
		p.queuedFrame = light.frame();
		p.queued = true;
	}

	float transform[16] = {kRadius, 0, 0, 0, 0, kRadius, 0, 0, 0, 0, kRadius, 0, 0, 0, 0, 1};
	const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
			BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
	const float ambient[4] = {0.f, 0.f, 0.f, kRadius};
	for (size_t i = 0; i < probes_.size(); ++i) {
		const Probe& p = probes_[i];
		if (!p.traced) continue;
		const float centre[4] = {p.pos.x, p.pos.y, p.pos.z, gain};
		transform[12] = p.pos.x;
		transform[13] = p.pos.y;
		transform[14] = p.pos.z;
		bgfx::setUniform(uProbe_, centre);
		bgfx::setUniform(uAmbient_, ambient);
		field.BindSurfaces(0);
		light.Bind(4, slots_ + int(i) * perProbe);
		bgfx::setTransform(transform);
		bgfx::setVertexBuffer(0, vbo_);
		bgfx::setIndexBuffer(ibo_, 0, indexCount_);
		bgfx::setState(state);
		bgfx::submit(view, program_);
	}
}

} // namespace painful
