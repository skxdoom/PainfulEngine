#include "LightUniforms.h"
#include "ShadowMap.h"
#include "TextureCache.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace painful {

void PackLight(LightBlock& block, int slot, const LightSource& l,
		const std::string& projName) {
	// The world pass's vertex colour: colour x min(intensity x 0.5, 1).
	// WorldMesh::RenderLightPass (0x101d9740) computes exactly this, and the
	// 0.5 is _DAT_102ae5b0. The pixel shaders then double it (point) or
	// quadruple it (spot), so the effective ceiling is intensity 2.
	const float gain = std::min(l.intensity * 0.5f, 1.f);
	for (int a = 0; a < 3; ++a) {
		block.pos[slot][a] = l.pos[a];
		block.color[slot][a] = l.color[a] * gain;
		block.axis[slot][a] = l.dir[a];
	}
	block.pos[slot][3] = l.range;
	block.color[slot][3] = float(l.type);
	block.axis[slot][3] = l.coneOuterCos;
	block.cone[slot][0] = l.coneCos;
	// The cookie spans the outer cone: Light::UpdateProj's half-fov is
	// acos(coneAngleCos), so its half-width at unit axial distance is the tan
	// of that angle.
	const float cosOuter = std::min(std::max(l.coneOuterCos, -0.999f), 0.999f);
	block.cone[slot][1] = l.coneOuterCos > -1.f
			? std::sqrt(1.f - cosOuter * cosOuter) / cosOuter
			: 1.f;
	block.count[0] = std::max(block.count[0], float(slot + 1));

	if (l.projector.empty() || l.projector != projName) return;
	block.count[1] = float(slot);
	// A basis for the cookie. The axis alone does not fix the roll, any stable
	// pair does, and a flashlight beam has no preferred up.
	const Vec3 up = std::abs(l.dir[1]) > 0.9f ? Vec3{1.f, 0.f, 0.f} : Vec3{0.f, 1.f, 0.f};
	Vec3 right = Cross(l.dir, up);
	if (right.LengthSq() > 1e-12f) right /= right.Length();
	Vec3 realUp = Cross(right, l.dir);
	if (realUp.LengthSq() > 1e-12f) realUp /= realUp.Length();
	right.Store(block.projX);
	realUp.Store(block.projY);
}

void PackShadow(LightBlock& block, const ShadowMap* shadow) {
	if (!shadow || !shadow->active()) return;
	std::memcpy(block.shadowMtx, shadow->matrix(), sizeof(block.shadowMtx));
	block.shadowParams[0] = 1.f;
	block.shadowParams[1] = ShadowMap::kNormalOffset;
	block.shadowParams[2] = ShadowMap::kLightOffset;
	block.shadowParams[3] = 1.f / float(shadow->size());
}

void LightUniforms::Init() {
	if (bgfx::isValid(count_)) return;
	count_ = bgfx::createUniform("u_dynCount", bgfx::UniformType::Vec4);
	pos_ = bgfx::createUniform("u_dynPos", bgfx::UniformType::Vec4, kMaxDynamicLights);
	color_ = bgfx::createUniform("u_dynColor", bgfx::UniformType::Vec4, kMaxDynamicLights);
	axis_ = bgfx::createUniform("u_dynAxis", bgfx::UniformType::Vec4, kMaxDynamicLights);
	cone_ = bgfx::createUniform("u_dynCone", bgfx::UniformType::Vec4, kMaxDynamicLights);
	projX_ = bgfx::createUniform("u_dynProjX", bgfx::UniformType::Vec4);
	projY_ = bgfx::createUniform("u_dynProjY", bgfx::UniformType::Vec4);
	sProj_ = bgfx::createUniform("s_proj", bgfx::UniformType::Sampler);
	sProjFall_ = bgfx::createUniform("s_projfall", bgfx::UniformType::Sampler);
	shadowMtx_ = bgfx::createUniform("u_shadowMtx", bgfx::UniformType::Mat4);
	shadowParams_ = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
	sShadow_ = bgfx::createUniform("s_shadow", bgfx::UniformType::Sampler);
}

void LightUniforms::Shutdown() {
	const auto drop = [](bgfx::UniformHandle& h) {
		if (bgfx::isValid(h)) bgfx::destroy(h);
		h = BGFX_INVALID_HANDLE;
	};
	drop(count_);
	drop(pos_);
	drop(color_);
	drop(axis_);
	drop(cone_);
	drop(projX_);
	drop(projY_);
	drop(sProj_);
	drop(sProjFall_);
	drop(shadowMtx_);
	drop(shadowParams_);
	drop(sShadow_);
}

void LightUniforms::Submit(const LightBlock& block, int projStage, int projFallStage,
		bgfx::TextureHandle proj, bgfx::TextureHandle projFall,
		int shadowStage, bgfx::TextureHandle shadow) const {
	if (!bgfx::isValid(count_)) return;
	bgfx::setUniform(count_, block.count);
	bgfx::setUniform(pos_, block.pos, kMaxDynamicLights);
	bgfx::setUniform(color_, block.color, kMaxDynamicLights);
	bgfx::setUniform(axis_, block.axis, kMaxDynamicLights);
	bgfx::setUniform(cone_, block.cone, kMaxDynamicLights);
	bgfx::setUniform(projX_, block.projX);
	bgfx::setUniform(projY_, block.projY);
	bgfx::setTexture(uint8_t(projStage), sProj_, proj, BGFX_SAMPLER_UVW_CLAMP);
	bgfx::setTexture(uint8_t(projFallStage), sProjFall_, projFall, BGFX_SAMPLER_UVW_CLAMP);
	bgfx::setUniform(shadowMtx_, block.shadowMtx);
	bgfx::setUniform(shadowParams_, block.shadowParams);
	// Default flags: the compare mode is baked into the depth texture.
	bgfx::setTexture(uint8_t(shadowStage), sShadow_, shadow);
}

bool ProjectorMaps::Resolve(const std::string& name, TextureCache& textures,
		const std::string& levelHint) {
	if (name.empty()) return false;
	if (name == name_) return true;
	name_ = name;
	cookie_ = textures.Get(name, levelHint);
	// light.shader's tu2_spotpass pairs every projector with this one ramp.
	falloff_ = textures.Get("special/flashfalloff", levelHint);
	return true;
}

} // namespace painful
