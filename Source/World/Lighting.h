#pragma once
#include "../Core/Vectors.h"
#include "Level.h"
#include "Templates.h"
#include <string>
#include <vector>

namespace painful {

// How PainEngine lights MODELS. The world mesh has baked lightmaps; entities
// have none, and are lit at runtime instead:
//
//   ambient          from the level's o.Ambient, overwritten by whichever
//                    CEnvironment box the entity stands in
//   one directional  from o.DirLight, likewise overwritten, and CROSS-FADED
//                    over DirLight.FadeTime as the entity crosses a boundary
//   the nearest      picked by attenuated intensity and handed to the shader
//   positional ones  as positions, so they are evaluated PER PIXEL
//
// The original does the last part differently, and worse. Entity::
// ComputeVSLights (0x1d1dc0) evaluates four lights at the ENTITY ORIGIN and
// hands the vertex shader a pre-attenuated colour, direction and half-vector
// each (c12..c23, ambient at c11) - so a monster is lit as a whole, with a cone
// edge that cannot follow the beam and no projector cookie. Against a wall lit
// by the world's own light pass, which IS per pixel and DOES sample the cookie,
// the model reads as pasted on.
//
// So the positional lights go through the same per-pixel evaluation as the
// world mesh (Shaders/shared_lights.sh), and the ambient plus the one
// directional stay as the original computed them.
// Docs/Reference/Lighting.md, "Deviations".
//
// The count is likewise not the original's four: every light now arrives
// through the script layer, a chapel can place a dozen candles, and there is no
// reason left to drop the ones that do not fit four vertex-shader slots.
//
// The number comes from the build, because the SHADERS declare their uniform
// arrays with it too and bgfx pairs the two by name at runtime - a value that
// disagreed would be silently wrong lighting rather than a build error. Set it
// in the top-level CMakeLists.txt, nowhere else.
#ifndef PAINFUL_MAX_DYN_LIGHTS
#error "PAINFUL_MAX_DYN_LIGHTS comes from CMake - it must match the shaders'."
#endif
constexpr int kMaxDynamicLights = PAINFUL_MAX_DYN_LIGHTS;

// One light, level-placed or created at runtime by LIGHT.Setup. The runtime
// ones are the flashlight, the torches monsters carry and the flashes an
// action fires off; they are the only lights the WORLD mesh is lit by, because
// the placed ones are already in its lightmap.
// Docs/Reference/Lighting.md
struct LightSource {
	// Light::GetType - 1 directional, 2 point, 3 spot.
	enum Type { kDirectional = 1, kPoint = 2, kSpot = 3 };

	int type = kPoint;
	Vec3 pos;
	Vec3 dir{0, -1, 0};
	Vec3 color{1, 1, 1}; // 0..1
	float intensity = 1.f;
	// Light::GetAttIntensity: full brightness within startFalloff, zero past
	// range, linear between. The engine stores range first.
	float range = 10.f;
	float startFalloff = 0.f;
	// LIGHT.SetFalloff takes ONE cone angle and derives both cosines from it:
	// cos(a) is the outer edge and cos(0.8a) the inner one, with a in DEGREES
	// straight through (0x10137720). There is no ConeOuterAngle property.
	float coneCos = -1.f, coneOuterCos = -1.f;
	bool fakeSpecular = false;
	// LIGHT.SetDynamicFlag: Light::EnableDynamic (0x101d5f00) sets bit
	// 0x400000 and puts the light in the world's dynamic list, which is what
	// WorldMesh::Draw walks to pick its additive light passes.
	bool dynamic = false;
	// LIGHT.SetImportant: a dynamic light that keeps its world pass even when
	// the video option that drops them is off. The flashlight sets it.
	bool important = false;
	// LIGHT.SetProjector: a cookie texture for a spot, projected down its
	// axis. Only PlayerLight uses one ("special/flashlight").
	std::string projector;
};

// Light::GetAttIntensity (0x101d4380) - the score Entity::AddLight ranks the
// slots by. In the original it is also a point light's whole falloff on a
// MODEL; here only the ranking survives, because the shading uses the world
// pass's quadratic curve for models too. Docs/Reference/Lighting.md
// `radius` scores a SPHERE rather than a point: a light that reaches any part
// of a model has to score above zero, because a zero score costs it a slot and
// the model goes unlit whole. Pass 0 for a genuine point probe.
float LightAttenuation(const LightSource& l, const Vec3& at, float radius = 0.f);
// How far the light can possibly reach, for culling. A SPOT runs `range` along
// its axis, so the rim of its cone sits further out than that - cull it by the
// sphere and the beam is cut off at a chunk boundary. Docs/Reference/Lighting.md
float LightReach(const LightSource& l);
// Fills coneCos / coneOuterCos from ONE authored angle in degrees.
void SetCone(LightSource& l, float degrees);

// Everything a model needs for one frame.
struct EntityLightState {
	Vec3 ambient; // 0..1
	// The environment directional, already faded and scaled by its intensity.
	// It has no position and no falloff, so it is applied on its own rather
	// than competing for a slot the way Entity::ResetLights makes it - with
	// eight slots there is nothing left for it to compete over.
	Vec3 dirColor;
	Vec3 dirDir{0, 1, 0}; // direction TO the light
	// The positional lights picked for this model, strongest first. They point
	// into the light lists, which are stable for the frame.
	const LightSource* lights[kMaxDynamicLights] = {};
	int lightCount = 0;
};

// The per-entity fade state. An entity keeps one of these because
// Entity::GetEnvironmentDirLight lerps colour, intensity and direction toward
// the environment it is entering rather than snapping.
struct EntityLightFade {
	bool primed = false;
	Vec3 ambient;
	Vec3 dirColor;
	Vec3 dirDir{0, 1, 0};
};

class EntityLighting {
public:
	// Reads the level's CLight and CEnvironment entities. Values resolve
	// through the BaseObj chain, because a placed light usually declares only
	// its position and colour - Point_White.CLight is what says it is a point
	// light with Range 10.
	//
	// lightsFromScripts leaves the CLights out. With the script layer running,
	// CLight:Apply places every one of them through LIGHT.Setup and they
	// arrive as dynamic lights instead - measured on Cemetery, 61 script
	// lights against the file's 60, the extra being the flashlight. Reading
	// both lists would count each placed light twice. The viewer and the
	// reports have no script layer and take the file.
	void Build(const Level& level, TemplateCache& templates, bool lightsFromScripts = false);
	void Clear();
	// The world ambient the scripts set through WORLD.AmbientColor (0-255),
	// which is what Entity::GetEnvironmentAmbient falls back to.
	void SetLevelAmbient(const Vec3& rgb255) {
		for (int i = 0; i < 3; ++i) levelAmbient_[i] = rgb255[i] / 255.f;
	}

	// Picks the lighting for a model at pos, `radius` being its bounding
	// sphere - a light that touches any part of it has to win a slot, or the
	// whole model goes dark at once. fade carries the environment cross-fade
	// between calls; dt is the frame time in seconds. Pass a fade block per
	// entity, or a throwaway one to snap. The eye is not needed: the
	// half-vector is per pixel now, so the shader takes it from the real one.
	void Evaluate(const Vec3& pos, float radius, float dt, EntityLightFade& fade,
			EntityLightState& out) const;

	// The lights the scripts made this frame, replaced wholesale: LIGHT.Setup
	// is called every tick by everything that owns one, so tracking edits
	// would buy nothing over rebuilding the list.
	void SetDynamicLights(std::vector<LightSource> lights) {
		dynamic_ = std::move(lights);
	}

	const std::vector<LightSource>& dynamicLights() const { return dynamic_; }
	size_t lightCount() const { return lights_.size(); }
	size_t environmentCount() const { return environments_.size(); }
	size_t dynamicCount() const { return dynamic_.size(); }

private:
	using Light = LightSource;
	static constexpr int kDirectional = LightSource::kDirectional;
	static constexpr int kPoint = LightSource::kPoint;
	static constexpr int kSpot = LightSource::kSpot;

	// A CEnvironment: an axis-aligned box that overwrites the lighting of
	// entities inside it. Cathedral places 50.
	struct Environment {
		Vec3 lo, hi;
		bool ambientOverwrite = false, dirOverwrite = false;
		// Which fields the file actually declared - see the Overwrite note in
		// Build. Anything unstated keeps the level's own value.
		bool hasAmbient = false, hasDirColor = false, hasDirDir = false;
		Vec3 ambient;
		Vec3 dirColor;
		Vec3 dirDir{0, -1, 0};
		float dirIntensity = 1.f;
		float fadeTime = 0.f;
		float volume = 0.f; // smallest box wins
	};

	const Environment* Innermost(const Vec3& pos) const;

	std::vector<Light> lights_;
	std::vector<Light> dynamic_;
	std::vector<Environment> environments_;
	Vec3 levelAmbient_;
	Vec3 levelDirColor_;
	float levelDirIntensity_ = 1.f;
	Vec3 levelDirDir_{0, -1, 0};
};

} // namespace painful
