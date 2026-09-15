#pragma once
#include "Camera.h"
#include "../Core/Vectors.h"
#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

class EntityLighting;
class SdfProbes;

// pfsdfdebuggrid: a world-aligned lattice of small spheres about the camera, each
// shaded per pixel from SdfProbes' grids at its centre, as a model's pixel there
// would be. A sphere buried in a surface or outside the grids is not drawn, so
// the lattice also shows where they end.
// Docs/Reference/Lighting.md, "Distance field ambient".
class SdfDebug {
public:
	static constexpr float kSpacing = 2.f; // world units between probes
	static constexpr int kHalfWide = 5; // probes either side of the camera's cell in x and z
	static constexpr int kHalfHigh = 2; // and in y
	static constexpr float kRadius = 0.2f;

	~SdfDebug() { Shutdown(); }
	SdfDebug() = default;
	SdfDebug(const SdfDebug&) = delete;
	SdfDebug& operator=(const SdfDebug&) = delete;

	bool Init(const std::string& shaderDir);
	void Shutdown();
	// A level went away: every box ambient is looked up again.
	void Clear() { probes_.clear(); }
	// Into `view` with the camera's transform already set (the world view).
	void Draw(bgfx::ViewId view, const Camera& camera, const SdfProbes& probes,
			const EntityLighting& lighting, float gain);

private:
	struct Probe {
		Vec3 pos;
		Vec3 ambient; // the box ambient, for the grids' unresolved share
	};

	std::vector<Probe> probes_;
	int cell_[3] = {0, 0, 0};

	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::VertexBufferHandle vbo_ = BGFX_INVALID_HANDLE;
	bgfx::IndexBufferHandle ibo_ = BGFX_INVALID_HANDLE;
	uint32_t indexCount_ = 0;
	bgfx::UniformHandle uProbe_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uAmbient_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
