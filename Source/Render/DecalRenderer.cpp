// DecalRenderer: Decal::Draw / Decal::SetState (0x101CF5C0 / 0x101CF310) in
// bgfx terms. The geometry is the DecalSystem's; this binds a texture, packs
// the fade factor and submits. Docs/Reference/Decals.md.

#include "DecalRenderer.h"
#include "../Core/Log.h"
#include "MaterialState.h"
#include "ShaderLoad.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace painful {

namespace {

struct DecalGpuVertex {
	float x, y, z;
	uint32_t abgr;
	float u, v;
};

// World units the surface triangles are lifted along the decal normal, so
// they win the depth test against the wall they were cut from.
constexpr float kDepthNudge = 0.004f;

} // namespace

bool DecalRenderer::Init(const std::string& shaderDir) {
	// Same vertex shape as a sprite: position, one colour, one UV.
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_particle");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_particle");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
		LogWarn("decals: missing vs_particle/fs_particle in %s", shaderDir.c_str());
		return false;
	}
	program_ = bgfx::createProgram(vs, fs, true);
	sDiffuse_ = bgfx::createUniform("s_diffuse", bgfx::UniformType::Sampler);
	uFog_ = bgfx::createUniform("u_fog", bgfx::UniformType::Vec4);
	uFogColor_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
	layout_.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.end();
	return bgfx::isValid(program_);
}

void DecalRenderer::Shutdown() {
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	if (bgfx::isValid(sDiffuse_)) bgfx::destroy(sDiffuse_);
	if (bgfx::isValid(uFog_)) bgfx::destroy(uFog_);
	if (bgfx::isValid(uFogColor_)) bgfx::destroy(uFogColor_);
	program_ = BGFX_INVALID_HANDLE;
	sDiffuse_ = BGFX_INVALID_HANDLE;
	uFog_ = BGFX_INVALID_HANDLE;
	uFogColor_ = BGFX_INVALID_HANDLE;
	textures_.clear();
}

// "Decals/<name>", or for an animated decal the "<base>_00".."<base>_NN"
// sequence the FPS plays through once. The texture cache owns the handles.
const DecalRenderer::Frames& DecalRenderer::Resolve(const std::string& texture, bool animated,
		TextureCache& textures) {
	const std::string key = texture + (animated ? "|anim" : "");
	auto it = textures_.find(key);
	if (it != textures_.end()) return it->second;
	Frames f;
	if (texture.empty()) {
		f.frames.push_back(textures.White());
	} else if (animated && texture.size() > 3 && texture[texture.size() - 3] == '_' &&
			std::isdigit(static_cast<unsigned char>(texture[texture.size() - 2])) &&
			std::isdigit(static_cast<unsigned char>(texture[texture.size() - 1]))) {
		const std::string base = texture.substr(0, texture.size() - 2);
		for (int i = 0; i < 64; ++i) {
			char num[4];
			std::snprintf(num, sizeof num, "%02d", i);
			const std::string ref = "Decals/" + base + num;
			if (textures.Resolve(ref, "").empty()) break;
			f.frames.push_back(textures.Get(ref, ""));
		}
	}
	if (f.frames.empty()) f.frames.push_back(textures.Get("Decals/" + texture, ""));
	return textures_.emplace(key, f).first->second;
}

void DecalRenderer::Draw(bgfx::ViewId view, const Camera& camera, const DecalSystem& decals,
		TextureCache& textures) {
	(void)camera;
	drawCalls_ = 0;
	triangles_ = 0;
	if (!bgfx::isValid(program_)) return;

	for (const DecalInstance& d : decals.decals()) {
		if (!d.alive || d.finished || d.verts.empty() || d.alpha == 0) continue;
		const uint32_t count = uint32_t(d.verts.size());
		if (bgfx::getAvailTransientVertexBuffer(count, layout_) < count) break;

		const std::string& tex = d.textureOverride.empty() ? d.def.texture : d.textureOverride;
		const Frames& frames = Resolve(tex, d.def.fps > 0.f, textures);
		size_t frame = 0;
		if (d.def.fps > 0.f && frames.frames.size() > 1) {
			const float age = d.def.lifeTime - d.life;
			frame = size_t(std::max(0.f, age * d.def.fps));
			frame = std::min(frame, frames.frames.size() - 1);
		}

		bgfx::TransientVertexBuffer tvb;
		bgfx::allocTransientVertexBuffer(&tvb, count, layout_);
		DecalGpuVertex* v = reinterpret_cast<DecalGpuVertex*>(tvb.data);
		// Decal::Tick's 0x01010101 * alpha: the fade scales every channel.
		const uint32_t a = d.alpha;
		const uint32_t abgr = (a << 24) | (a << 16) | (a << 8) | a;
		for (uint32_t i = 0; i < count; ++i) {
			const DecalVertex& s = d.verts[i];
			v[i] = {s.pos[0] + d.normal[0] * kDepthNudge, s.pos[1] + d.normal[1] * kDepthNudge,
					s.pos[2] + d.normal[2] * kDepthNudge, abgr, s.u, s.v};
		}

		const int blend = d.def.blendMode < 0 ? 5 : d.def.blendMode;
		uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_LEQUAL |
				BlendModeState(blend);
		if (blend == 0) state |= BGFX_STATE_WRITE_Z;
		bgfx::setState(state);
		bgfx::setVertexBuffer(0, &tvb, 0, count);
		float fogColor[4];
		FogColorForBlend(blend, fogColor_, fogColor);
		bgfx::setUniform(uFog_, fog_);
		bgfx::setUniform(uFogColor_, fogColor);
		bgfx::setTexture(0, sDiffuse_, frames.frames[frame]);
		bgfx::submit(view, program_);
		++drawCalls_;
		triangles_ += count / 3;
	}
}

} // namespace painful
