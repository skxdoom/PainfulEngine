#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>

namespace painful {

// One triangle past the screen corners, in clip space, with uv oriented for
// the backend's render-target storage (top row first on D3D, bottom row
// first on GL). Shared by every full-screen pass (Bloom, DemonFx).
struct PostVertex {
	float x, y, z;
	float u, v;
};

inline bgfx::VertexLayout PostVertexLayout() {
	bgfx::VertexLayout layout;
	layout.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
	return layout;
}

// Sets the view's rect, clear and transform, then the triangle and a plain
// colour-write state; the caller binds textures and submits.
inline void FullScreenTriangle(bgfx::ViewId view, const bgfx::VertexLayout& layout, int width,
		int height) {
	bgfx::setViewRect(view, 0, 0, uint16_t(width), uint16_t(height));
	bgfx::setViewClear(view, BGFX_CLEAR_NONE);
	bgfx::setViewTransform(view, nullptr, nullptr);
	if (bgfx::getAvailTransientVertexBuffer(3, layout) < 3) return;
	bgfx::TransientVertexBuffer tvb;
	bgfx::allocTransientVertexBuffer(&tvb, 3, layout);
	const bool flip = bgfx::getCaps()->originBottomLeft;
	PostVertex* v = reinterpret_cast<PostVertex*>(tvb.data);
	v[0] = {-1.f, -1.f, 0.f, 0.f, flip ? 0.f : 1.f};
	v[1] = {3.f, -1.f, 0.f, 2.f, flip ? 0.f : 1.f};
	v[2] = {-1.f, 3.f, 0.f, 0.f, flip ? 2.f : -1.f};
	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

} // namespace painful
