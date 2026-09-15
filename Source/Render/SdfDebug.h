#pragma once
#include "Camera.h"
#include "../Core/Vectors.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>
#include <vector>

namespace painful {

class SdfField;
class SdfVertexLight;

// pfsdfdebuggrid: a world-aligned lattice of small spheres about the camera,
// each lit by the light traced through the distance field at its own vertices
// (SdfVertexLight), as a model's vertices there would be. The spheres take the
// trace budget the models leave, in turn; one buried in a surface, outside the
// fields or not traced yet is not drawn. Docs/Reference/Lighting.md,
// "Per-vertex tracing".
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
	// A level went away: the lattice is laid and traced again.
	void Clear() { probes_.clear(); }
	// Queues the lattice's vertices into `light`, then draws the traced spheres
	// into `view` (the world view). Before light.Dispatch.
	void Draw(bgfx::ViewId view, const Camera& camera, const SdfField& field, SdfVertexLight& light,
			float gain);

private:
	struct Probe {
		Vec3 pos;
		uint32_t queuedFrame = 0;
		bool queued = false;
		bool traced = false;
	};

	std::vector<Probe> probes_;
	std::vector<Vec3> sphere_; // the unit sphere's vertices, which are also its normals
	int cell_[3] = {0, 0, 0};
	int slots_ = -1; // the lattice's first history slot, kSphere vertices a probe
	int slotCount_ = 0;
	uint32_t generation_ = 0; // SdfVertexLight's when the slots were taken
	size_t cursor_ = 0; // the next probe to trace

	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::VertexBufferHandle vbo_ = BGFX_INVALID_HANDLE;
	bgfx::IndexBufferHandle ibo_ = BGFX_INVALID_HANDLE;
	uint32_t indexCount_ = 0;
	bgfx::UniformHandle uProbe_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uAmbient_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
