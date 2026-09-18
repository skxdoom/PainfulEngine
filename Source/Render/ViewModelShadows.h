#pragma once
#include "../Core/Frustum.h"
#include "../Core/Vectors.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace painful {

// The view model's shadow maps: one atlas, one view, each cell drawn with its
// own matrix and scissor. Cell 0 is orthographic down the environment box's
// directional; cells 1..kLights are perspective from the placed and dynamic
// lights nearest the weapon. All are fitted to the weapon's bounding sphere,
// and the weapon alone casts into them - its self-shadowing is what they are
// for. Docs/Reference/Lighting.md, "Shadows on the view model"
class ViewModelShadows {
public:
	static constexpr int kLights = 3;
	static constexpr int kCols = 2, kRows = 2;

	struct Cell {
		bool active = false;
		int lightId = 0; // LightSource::id; 0 for the directional
		Vec3 lightPos; // a light cell's eye
		float drawMatrix[16] = {}; // world -> the cell's clip space
		float receiverMatrix[16] = {}; // world -> atlas uv, depth (divide by w)
		float rect[4] = {}; // the cell in atlas uv, inset past the filter
		// World units per texel: flat for the directional, per unit of
		// distance from the light for a light cell.
		float texel = 0.f;
		uint16_t scissor[4] = {};
		Frustum frustum = {};
	};

	~ViewModelShadows() { Shutdown(); }
	ViewModelShadows() = default;
	ViewModelShadows(const ViewModelShadows&) = delete;
	ViewModelShadows& operator=(const ViewModelShadows&) = delete;

	// cellSize texels a side, kCols x kRows cells.
	bool Init(const std::string& shaderDir, int cellSize);
	void Shutdown();
	bool ready() const { return bgfx::isValid(fb_); }
	int size() const { return cellSize_; }
	void SetView(bgfx::ViewId view) { view_ = view; }
	bgfx::ViewId viewId() const { return view_; }

	// Forgets last frame's cells and clears the atlas.
	void BeginFrame();
	// Cell 0: an orthographic box about the sphere, looking down toLight.
	void Begin(const Vec3& toLight, const Vec3& centre, float radius);
	// The next light cell: a perspective view from lightPos fitted to the
	// sphere. False when the cells are spent or the light sits inside it.
	bool AddLight(int lightId, const Vec3& lightPos, const Vec3& centre, float radius);
	bool active() const { return cells_[0].active; }
	const Cell& cell(int i) const { return cells_[i]; }
	int lightCount() const { return lights_; }

	const float* texelUv() const { return texelUv_; } // one atlas texel, u and v
	bgfx::TextureHandle texture() const { return depth_; }
	bgfx::ProgramHandle program() const { return program_; }
	static constexpr uint64_t kState = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

private:
	void Place(Cell& cell, int index, const float view[16], const float proj[16]);

	bgfx::FrameBufferHandle fb_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle depth_ = BGFX_INVALID_HANDLE;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::ViewId view_ = 0;
	int cellSize_ = 0;
	int lights_ = 0;
	float texelUv_[2] = {};
	Cell cells_[1 + kLights];
};

} // namespace painful
