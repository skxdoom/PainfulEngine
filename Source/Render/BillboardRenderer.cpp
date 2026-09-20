#include "BillboardRenderer.h"
#include "ShaderLoad.h"
#include "../Core/Check.h"
#include "../Core/Vectors.h"
#include "../Core/Log.h"
#include "MaterialState.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace painful {

namespace {

struct BillboardVertex {
	float x, y, z;
	uint32_t abgr;
	float u, v;
};

// How far in front of the camera the occlusion trace starts, so the segment
// does not begin inside whatever the camera is standing in.
constexpr float kTraceStartOffset = 0.3f;
// The trace runs at most ten times a second per billboard, which is what the
// original's countdown at +0x6fc amounts to.
constexpr float kTraceInterval = 0.1f;

// CBillboard.BlendMode is an editor index, not the material system's enum;
// SetupCorona maps it before storing. Note 3 and 4 are NOT 3 and 4.
int RemapBlendMode(int editorIndex) {
	switch (editorIndex) {
		case 1: return kBlendAlpha;
		case 2: return kBlendAdd;
		case 3: return kBlendFilter;
		case 4: return kBlendTranslucent;
		default: return kBlendNone;
	}
}

// Walks instance properties then the BaseObj template chain, so a value can be
// declared at any level. TemplateCache resolves scalars but not ctor values
// like Color, and billboards need both, so the walk is done once here.
class Chain {
public:
	Chain(const Entity& entity, TemplateCache& templates) {
		levels_.push_back(&entity.props);
		std::string current = entity.baseObj;
		for (int depth = 0; depth < 16 && !current.empty(); ++depth) {
			const Properties* props = templates.Find(current);
			if (!props) break;
			levels_.push_back(props);
			current = props->String("BaseObj");
		}
	}

	const Value* Find(const std::string& key) const {
		for (const Properties* p : levels_)
			if (const Value* v = p->Find(key)) return v;
		return nullptr;
	}

	float Number(const std::string& key, float fallback) const {
		const Value* v = Find(key);
		return v && v->kind == Value::Kind::Number ? static_cast<float>(v->number) : fallback;
	}

	std::string String(const std::string& key, const std::string& fallback = "") const {
		const Value* v = Find(key);
		return v && v->kind == Value::Kind::String ? v->text : fallback;
	}

	bool Bool(const std::string& key, bool fallback) const {
		const Value* v = Find(key);
		return v && v->kind == Value::Kind::Bool ? v->boolean : fallback;
	}

private:
	std::vector<const Properties*> levels_;
};

} // namespace

bool BillboardRenderer::Init(const std::string& shaderDir) {
	// Billboards are the same geometry as particles - a camera-facing textured
	// quad with one vertex colour - so they share the shader pair.
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_particle");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_particle");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
		LogWarn("billboards: missing vs_particle/fs_particle in %s", shaderDir.c_str());
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

void BillboardRenderer::Shutdown() {
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	if (bgfx::isValid(sDiffuse_)) bgfx::destroy(sDiffuse_);
	if (bgfx::isValid(uFog_)) { bgfx::destroy(uFog_); uFog_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uFogColor_)) { bgfx::destroy(uFogColor_); uFogColor_ = BGFX_INVALID_HANDLE; }
	program_ = BGFX_INVALID_HANDLE;
	sDiffuse_ = BGFX_INVALID_HANDLE;
	sprites_.clear();
}

void BillboardRenderer::Build(const Level& level, TemplateCache& templates,
		TextureCache& textures) {
	sprites_.clear();
	coronas_ = 0;

	for (const Entity& entity : level.entities()) {
		if (entity.type != "CBillboard") continue;
		const Chain chain(entity, templates);

		Sprite s;
		// Class defaults from CBillboard.lua. Note the class default Alpha is
		// 0.5 and Size 5, which differ from the C++ constructor's - the script
		// always passes every value through SetupCorona, so the script wins.
		s.alpha = chain.Number("Alpha", 0.5f);
		s.size = chain.Number("Size", 5.f);
		s.minSize = chain.Number("Corona.MinSize", 0.8f);
		s.minDistance = chain.Number("Corona.MinDistance", 5.f);
		s.maxDistance = chain.Number("Corona.MaxDistance", 20.f);
		s.offDistance = chain.Number("Corona.OffDistance", 70.f);
		s.fadeInTime = chain.Number("Corona.FadeInTime", 0.5f);
		s.fadeOutTime = chain.Number("Corona.FadeOutTime", 0.5f);
		s.traceMargin = chain.Number("Corona.TraceMargin", 1.f);
		s.corona = chain.Bool("Corona.Enabled", false);

		std::string texture = chain.String("Texture", "banka");
		int blendMode = static_cast<int>(chain.Number("BlendMode", 1.f));

		// CBillboard:Apply converts the older light-corona spelling of these
		// fields before calling SetupCorona; a handful of templates still use
		// them ("tryb konwersji ze swiatel" in the script).
		if (const Value* v = chain.Find("Corona.Texture")) {
			if (v->kind == Value::Kind::String) texture = v->text;
			s.corona = !chain.Bool("Corona.Billboard", false);
		}
		if (const Value* v = chain.Find("Corona.BlendMode"))
			if (v->kind == Value::Kind::Number) blendMode = static_cast<int>(v->number);
		if (const Value* v = chain.Find("Corona.AlphaMax"))
			if (v->kind == Value::Kind::Number) s.alpha = static_cast<float>(v->number);
		if (const Value* v = chain.Find("Corona.MaxRadius"))
			if (v->kind == Value::Kind::Number) s.size = static_cast<float>(v->number);
		if (const Value* v = chain.Find("Corona.MinRadius"))
			if (v->kind == Value::Kind::Number) s.minSize = static_cast<float>(v->number);

		// Color:Compose is R3D.RGBA, which packs 0xAARRGGBB. The alpha channel
		// is not used: the corona's alpha comes from the fade, which is why
		// every shipped template writes Color:New(r,g,b,0).
		if (const Value* v = chain.Find("Color")) {
			if (v->kind == Value::Kind::Ctor && v->args.size() >= 3) {
				auto byteOf = [](float f) {
					const int i = static_cast<int>(f);
					return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
				};
				s.r = byteOf(v->Arg(0, 255.f));
				s.g = byteOf(v->Arg(1, 255.f));
				s.b = byteOf(v->Arg(2, 255.f));
			}
		}

		s.blendMode = RemapBlendMode(blendMode);
	s.blendState = BlendModeState(s.blendMode);
		s.texture = textures.Get("Particles/" + texture, level.name());

		for (int i = 0; i < 3; ++i) s.pos[i] = entity.pos[i] * scaleMultiplier_;
		s.size *= scaleMultiplier_;
		s.minSize *= scaleMultiplier_;
		s.curSize = s.size;

		// A plain billboard never traces and never fades: SetupCorona leaves
		// its alpha pinned at the target, and Draw only runs the corona block
		// when the corona bit is set.
		if (!s.corona) {
			s.curAlpha = s.alpha;
		} else {
			++coronas_;
			// Start hidden and fade in, rather than popping on at full
			// brightness in the first frame after a level load.
			s.curAlpha = 0.f;
			s.blocked = true;
		}
		sprites_.push_back(s);
	}
	LogInfo("billboards: %zu placed (%zu coronas)", sprites_.size(), coronas_);
}

void BillboardRenderer::SetScaleMultiplier(float k) {
	if (k == scaleMultiplier_) return;
	const float ratio = scaleMultiplier_ != 0.f ? k / scaleMultiplier_ : k;
	scaleMultiplier_ = k;
	for (Sprite& s : sprites_) {
		for (int i = 0; i < 3; ++i) s.pos[i] *= ratio;
		s.size *= ratio;
		s.minSize *= ratio;
		s.curSize *= ratio;
	}
}

void BillboardRenderer::FadeTick(Sprite& s, bool nowVisible, float dt) const {
	// The original guards these three against zero before dividing by them.
	const float fadeIn = s.fadeInTime > 0.f ? s.fadeInTime : 1e-4f;
	const float fadeOut = s.fadeOutTime > 0.f ? s.fadeOutTime : 1e-4f;
	const float target = s.alpha > 0.f ? s.alpha : 1e-4f;

	// On a change of direction the timer is rebuilt from the alpha already
	// reached, so a corona interrupted half way through a fade reverses from
	// where it is instead of restarting.
	if (nowVisible != s.wasVisible) s.fadeTimer = (s.curAlpha / target) * (nowVisible ? fadeIn : fadeOut);
	s.wasVisible = nowVisible;

	if (nowVisible) {
		s.fadeTimer += dt;
		s.curAlpha = (s.fadeTimer / fadeIn) * target;
		if (s.fadeTimer >= fadeIn) {
			s.curAlpha = target;
			s.fadeTimer = fadeIn;
		}
	} else {
		s.fadeTimer -= dt;
		s.curAlpha = (s.fadeTimer / fadeOut) * target;
		if (s.fadeTimer <= 0.f) {
			s.curAlpha = 0.f;
			s.fadeTimer = 0.f;
		}
	}
}

int BillboardRenderer::SetupScriptCorona(int slot, const float args[9],
		const std::string& texture,
		uint32_t packedColor, int blendMode,
		bool spriteOnly, TextureCache& textures,
		const std::string& levelHint) {
	PAINFUL_CHECK(slot < 0 || size_t(slot) < sprites_.size(),
			"BillboardRenderer: stale corona slot %d of %zu", slot, sprites_.size());
	if (slot < 0 || size_t(slot) >= sprites_.size()) {
		sprites_.push_back(Sprite());
		slot = int(sprites_.size() - 1);
	}
	Sprite& s = sprites_[slot];
	const bool wasCorona = s.corona;

	// The native's field-for-field mapping (Engine.dll 0x10137b70).
	s.alpha = args[0];
	s.fadeInTime = args[1];
	s.fadeOutTime = args[2];
	s.minSize = args[3] * scaleMultiplier_;
	s.minDistance = args[4];
	s.size = args[5] * scaleMultiplier_;
	s.maxDistance = args[6];
	s.offDistance = args[7];
	s.traceMargin = args[8];
	s.corona = !spriteOnly;
	// The colour is our own R3D.RGBA packing; its alpha channel is unused -
	// the corona's alpha comes from the fade.
	s.r = uint8_t((packedColor >> 16) & 0xFF);
	s.g = uint8_t((packedColor >> 8) & 0xFF);
	s.b = uint8_t(packedColor & 0xFF);
	s.blendMode = RemapBlendMode(blendMode);
	s.blendState = BlendModeState(s.blendMode);
	s.texture = textures.Get(texture, levelHint);
	s.curSize = s.size;
	// Coronas start hidden and fade in; plain sprites sit at their target.
	s.curAlpha = s.corona ? 0.f : s.alpha;
	if (s.corona && !wasCorona) ++coronas_;
	return slot;
}

void BillboardRenderer::SetScriptSpritePos(int slot, const Vec3& pos) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < sprites_.size(),
			"BillboardRenderer: sprite slot %d of %zu", slot, sprites_.size()))
		return;
	for (int i = 0; i < 3; ++i) sprites_[slot].pos[i] = pos[i] * scaleMultiplier_;
}

void BillboardRenderer::SetScriptSpriteVisible(int slot, bool visible) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < sprites_.size(),
			"BillboardRenderer: sprite slot %d of %zu", slot, sprites_.size()))
		return;
	sprites_[slot].scriptVisible = visible;
}

void BillboardRenderer::RemoveScriptSprite(int slot) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < sprites_.size(),
			"BillboardRenderer: sprite slot %d of %zu", slot, sprites_.size()))
		return;
	sprites_[slot].alive = false;
}

void BillboardRenderer::Update(const Camera& camera, float dt, const Occluder& occluded) {
	visible_ = 0;
	traces_ = 0;
	// A long hitch would otherwise complete a whole fade in one step.
	dt = std::min(dt, 0.1f);

	for (Sprite& s : sprites_) {
		if (!s.alive) continue;
		if (!s.corona) {
			s.curAlpha = s.alpha;
			if (s.curAlpha > 0.f) ++visible_;
			continue;
		}

		Vec3 delta;
		for (int i = 0; i < 3; ++i) delta[i] = s.pos[i] - camera.pos[i];
		s.distance = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);

		bool nowVisible = false;
		if (coronasEnabled_ && s.distance < s.offDistance) {
			s.traceTimer -= dt;
			if (s.traceTimer <= 0.f) {
				s.traceTimer = kTraceInterval;
				++traces_;
				// The segment stops TraceMargin short of the billboard so the
				// surface a lamp is mounted on does not occlude its own corona.
				const float inv = s.distance > 1e-6f ? 1.f / s.distance : 0.f;
				const Vec3 dir{delta[0] * inv, delta[1] * inv, delta[2] * inv};
				const Vec3 from{camera.pos[0] + dir[0] * kTraceStartOffset,
						camera.pos[1] + dir[1] * kTraceStartOffset,
						camera.pos[2] + dir[2] * kTraceStartOffset};
				const Vec3 to{s.pos[0] - dir[0] * s.traceMargin,
						s.pos[1] - dir[1] * s.traceMargin,
						s.pos[2] - dir[2] * s.traceMargin};
				s.blocked = occluded && occluded(from, to);
			}
			nowVisible = !s.blocked;
		}
		// The script can hide it outright; the fade still runs it down.
		if (!s.scriptVisible) {
			nowVisible = false;
		}
		// Runs even past OffDistance, so a corona left behind fades out
		// instead of vanishing between frames.
		FadeTick(s, nowVisible, dt);

		if (s.curAlpha <= 0.f) continue;
		++visible_;

		// Size grows with distance so the corona holds a roughly constant
		// apparent size, clamped at both ends of the ramp.
		if (s.distance >= s.maxDistance) {
			s.curSize = s.size;
		} else if (s.distance > s.minDistance) {
			const float span = s.maxDistance - s.minDistance;
			s.curSize = s.minSize + (s.distance - s.minDistance) *
										((s.size - s.minSize) / (span != 0.f ? span : 1.f));
		} else {
			s.curSize = s.minSize;
		}
	}
}

void BillboardRenderer::DrawImmediate(const Vec3& pos, float size, float rot,
		uint32_t abgr, bgfx::TextureHandle texture) {
	Immediate s;
	for (int c = 0; c < 3; ++c) s.pos[c] = pos[c];
	s.size = size;
	s.rot = rot;
	s.abgr = abgr;
	s.texture = texture;
	immediate_.push_back(s);
}

void BillboardRenderer::DrawBeamImmediate(const Vec3& a, const Vec3& b, float width,
		uint32_t abgr, bgfx::TextureHandle texture) {
	DrawStripImmediate({a, b}, width, abgr, 0, texture);
}

void BillboardRenderer::DrawStripImmediate(std::vector<Vec3> points, float width,
		uint32_t abgr, int mode, bgfx::TextureHandle texture) {
	Strip strip;
	strip.points = std::move(points);
	strip.width = width;
	strip.abgr = abgr;
	strip.texture = texture;
	strip.mode = mode;
	strips_.push_back(std::move(strip));
}

void BillboardRenderer::Draw(bgfx::ViewId view, bgfx::ViewId coronaView, const Camera& camera) {
	drawCalls_ = 0;
	if (!bgfx::isValid(program_) || (sprites_.empty() && immediate_.empty() && strips_.empty())) {
		immediate_.clear();
		strips_.clear();
		return;
	}

	const Vec3 forward = camera.Forward();
	const Vec3 right = camera.Right();
	Vec3 up;
	up[0] = right[1] * forward[2] - right[2] * forward[1];
	up[1] = right[2] * forward[0] - right[0] * forward[2];
	up[2] = right[0] * forward[1] - right[1] * forward[0];

	for (const Sprite& s : sprites_) {
		if (!s.alive || s.curAlpha <= 0.f) continue;
		if (bgfx::getAvailTransientVertexBuffer(4, layout_) < 4) break;
		if (bgfx::getAvailTransientIndexBuffer(6) < 6) break;

		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		bgfx::allocTransientVertexBuffer(&tvb, 4, layout_);
		bgfx::allocTransientIndexBuffer(&tib, 6);
		BillboardVertex* v = reinterpret_cast<BillboardVertex*>(tvb.data);
		uint16_t* idx = reinterpret_cast<uint16_t*>(tib.data);

		const int a = static_cast<int>(s.curAlpha * 255.f + 0.5f);
		auto dim = [this](uint8_t c) {
			const int v = static_cast<int>(c * colorScale_ + 0.5f);
			return static_cast<uint32_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
		};
		const uint32_t abgr = (static_cast<uint32_t>(a < 0 ? 0 : (a > 255 ? 255 : a)) << 24) |
				(dim(s.b) << 16) | (dim(s.g) << 8) | dim(s.r);

		Vec3 rx, uy;
		for (int k = 0; k < 3; ++k) {
			rx[k] = right[k] * s.curSize;
			uy[k] = up[k] * s.curSize;
		}
		// Same corner order and UVs as the particle quads.
		v[0] = {s.pos[0] - rx[0] + uy[0], s.pos[1] - rx[1] + uy[1], s.pos[2] - rx[2] + uy[2],
				abgr, 0.f, 1.f};
		v[1] = {s.pos[0] + rx[0] + uy[0], s.pos[1] + rx[1] + uy[1], s.pos[2] + rx[2] + uy[2],
				abgr, 1.f, 1.f};
		v[2] = {s.pos[0] + rx[0] - uy[0], s.pos[1] + rx[1] - uy[1], s.pos[2] + rx[2] - uy[2],
				abgr, 1.f, 0.f};
		v[3] = {s.pos[0] - rx[0] - uy[0], s.pos[1] - rx[1] - uy[1], s.pos[2] - rx[2] - uy[2],
				abgr, 0.f, 0.f};
		idx[0] = 0; idx[1] = 1; idx[2] = 2;
		idx[3] = 0; idx[4] = 2; idx[5] = 3;

		// A corona disables depth testing outright - its occlusion is the line
		// trace, so it draws over whatever is in front of it once the trace
		// says the light is in view. A plain billboard is depth-tested. That
		// split is literally one bit of the render state D3Dev is handed,
		// taken from the corona flag.
		uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
				s.blendState;
		if (!s.corona) state |= BGFX_STATE_DEPTH_TEST_LESS;

		bgfx::setState(state);
		bgfx::setVertexBuffer(0, &tvb, 0, 4);
		bgfx::setIndexBuffer(&tib, 0, 6);
		float fogColor[4];
		FogColorForBlend(s.blendMode, fogColor_, fogColor);
		bgfx::setUniform(uFog_, fog_);
		bgfx::setUniform(uFogColor_, fogColor);
		bgfx::setTexture(0, sDiffuse_, s.texture);
		bgfx::submit(s.corona ? coronaView : view, program_);
		++drawCalls_;
	}

	// The immediate sprites, drawn once and forgotten. A muzzle flash is the
	// caller: R3D.DrawSprite from a CProcess's Render, alive for 0.14s.
	for (const Immediate& s : immediate_) {
		if (bgfx::getAvailTransientVertexBuffer(4, layout_) < 4) break;
		if (bgfx::getAvailTransientIndexBuffer(6) < 6) break;

		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		bgfx::allocTransientVertexBuffer(&tvb, 4, layout_);
		bgfx::allocTransientIndexBuffer(&tib, 6);
		BillboardVertex* v = reinterpret_cast<BillboardVertex*>(tvb.data);
		uint16_t* idx = reinterpret_cast<uint16_t*>(tib.data);

		// The quad's axes are the camera's, turned by the sprite's own angle
		// in the view plane - which is the whole point of the rotation the
		// script passes: consecutive flashes are the same texture at a
		// different angle, and without it a burst looks like one frozen image.
		const float c = std::cos(s.rot), sn = std::sin(s.rot);
		Vec3 rx, uy;
		for (int k = 0; k < 3; ++k) {
			rx[k] = (right[k] * c + up[k] * sn) * s.size;
			uy[k] = (up[k] * c - right[k] * sn) * s.size;
		}
		v[0] = {s.pos[0] - rx[0] + uy[0], s.pos[1] - rx[1] + uy[1], s.pos[2] - rx[2] + uy[2],
				s.abgr, 0.f, 1.f};
		v[1] = {s.pos[0] + rx[0] + uy[0], s.pos[1] + rx[1] + uy[1], s.pos[2] + rx[2] + uy[2],
				s.abgr, 1.f, 1.f};
		v[2] = {s.pos[0] + rx[0] - uy[0], s.pos[1] + rx[1] - uy[1], s.pos[2] + rx[2] - uy[2],
				s.abgr, 1.f, 0.f};
		v[3] = {s.pos[0] - rx[0] - uy[0], s.pos[1] - rx[1] - uy[1], s.pos[2] - rx[2] - uy[2],
				s.abgr, 0.f, 0.f};
		idx[0] = 0; idx[1] = 1; idx[2] = 2;
		idx[3] = 0; idx[4] = 2; idx[5] = 3;

		// kBlendAlpha - SRC_ALPHA, ONE - which is the mode the particles use and
		// the one the sprite art is authored for: additive, but WEIGHTED BY
		// ALPHA, so the transparent part of a flash contributes nothing. Plain
		// additive (ONE, ONE) ignores the alpha channel outright and the
		// texture's whole square shows.
		//
		// Depth-tested: a flash is still hidden by a wall in front of it.
		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_LESS |
				BlendModeState(kBlendAlpha));
		bgfx::setVertexBuffer(0, &tvb);
		bgfx::setIndexBuffer(&tib);
		// Immediate sprites and beams draw with the alpha blend: fog to black.
		const float fogBlack[4] = {0.f, 0.f, 0.f, 1.f};
		bgfx::setUniform(uFog_, fog_);
		bgfx::setUniform(uFogColor_, fogBlack);
		bgfx::setTexture(0, sDiffuse_, s.texture);
		bgfx::submit(view, program_);
		++drawCalls_;
	}
	immediate_.clear();

	// The strips: DrawSprite1DOF's beam and the Spr_* lines. Each point is
	// pushed HALF the width to either side, U runs along the strip (0,1
	// alternating for modes >= 10) and V across it, and the side vector is
	// the mode's: the segment crossed with the line of sight, so a beam aimed
	// at the eye never flips inside out, or a camera axis. RenderSprites,
	// 0x100a2b50; Billboards.md, "Immediate sprites, and the one with an axis".
	for (const Strip& strip : strips_) {
		const size_t n = strip.points.size();
		if (n < 2 || strip.width <= 0.f) continue;
		const uint32_t nv = uint32_t(n * 2), ni = uint32_t((n - 1) * 6);
		if (bgfx::getAvailTransientVertexBuffer(nv, layout_) < nv) break;
		if (bgfx::getAvailTransientIndexBuffer(ni) < ni) break;

		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;
		bgfx::allocTransientVertexBuffer(&tvb, nv, layout_);
		bgfx::allocTransientIndexBuffer(&tib, ni);
		BillboardVertex* v = reinterpret_cast<BillboardVertex*>(tvb.data);
		uint16_t* idx = reinterpret_cast<uint16_t*>(tib.data);

		const int how = strip.mode >= 10 ? strip.mode - 10 : strip.mode;
		const float half = strip.width * 0.5f;
		Vec3 dir{0.f, 0.f, 0.f};
		for (size_t i = 0; i < n; ++i) {
			const Vec3& p = strip.points[i];
			// The last point keeps the last segment's direction.
			if (i + 1 < n)
				for (int c = 0; c < 3; ++c) dir[c] = strip.points[i + 1][c] - p[c];
			Vec3 side{0.f, 0.f, 0.f};
			if (how == 1) {
				for (int c = 0; c < 3; ++c) side[c] = right[c] * half;
			} else if (how == 2) {
				for (int c = 0; c < 3; ++c) side[c] = up[c] * half;
			} else {
				const Vec3 toEye{camera.pos[0] - p[0], camera.pos[1] - p[1], camera.pos[2] - p[2]};
				side[0] = dir[1]*toEye[2] - dir[2]*toEye[1];
				side[1] = dir[2]*toEye[0] - dir[0]*toEye[2];
				side[2] = dir[0]*toEye[1] - dir[1]*toEye[0];
				float len = std::sqrt(side[0]*side[0] + side[1]*side[1] + side[2]*side[2]);
				if (len < 1e-6f) {
					// Looking straight down the segment: any perpendicular
					// will do, the band is edge-on anyway.
					const Vec3 alt{dir[1], dir[2], dir[0]};
					side[0] = dir[1]*alt[2] - dir[2]*alt[1];
					side[1] = dir[2]*alt[0] - dir[0]*alt[2];
					side[2] = dir[0]*alt[1] - dir[1]*alt[0];
					len = std::sqrt(side[0]*side[0] + side[1]*side[1] + side[2]*side[2]);
				}
				const float k = len > 1e-6f ? half / len : 0.f;
				for (int c = 0; c < 3; ++c) side[c] *= k;
			}
			const float u = strip.mode >= 10 ? float(i & 1) : float(i) / float(n - 1);
			v[i * 2] = {p[0] + side[0], p[1] + side[1], p[2] + side[2], strip.abgr, u, 0.f};
			v[i * 2 + 1] = {p[0] - side[0], p[1] - side[1], p[2] - side[2], strip.abgr, u, 1.f};
			if (i + 1 < n) {
				const uint16_t a = uint16_t(i * 2);
				uint16_t* t = idx + i * 6;
				t[0] = a; t[1] = uint16_t(a + 1); t[2] = uint16_t(a + 2);
				t[3] = uint16_t(a + 1); t[4] = uint16_t(a + 3); t[5] = uint16_t(a + 2);
			}
		}

		// Same alpha blend the immediate sprites use, and depth-tested: the
		// band is hidden by anything between the eye and it.
		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_LESS |
				BlendModeState(kBlendAlpha));
		bgfx::setVertexBuffer(0, &tvb);
		bgfx::setIndexBuffer(&tib);
		// Immediate sprites and strips draw with the alpha blend: fog to black.
		const float fogBlack[4] = {0.f, 0.f, 0.f, 1.f};
		bgfx::setUniform(uFog_, fog_);
		bgfx::setUniform(uFogColor_, fogBlack);
		bgfx::setTexture(0, sDiffuse_, strip.texture);
		bgfx::submit(view, program_);
		++drawCalls_;
	}
	strips_.clear();
}

} // namespace painful
