#include "Lighting.h"
#include "../Core/Vectors.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace painful {
namespace {

// Vec3::Normalized() rescales any non-zero length; the light directions were
// written with a 1e-6 floor, so that floor is kept rather than widened.
void Normalize(Vec3& d) {
	if (d.LengthSq() > 1e-12f) d /= d.Length();
}

float Dist(const Vec3& a, const Vec3& b) { return Distance(AsVec3(a), AsVec3(b)); }

// Colours are authored 0..255 in Color:New(...). Returns false when nothing in
// the chain declares the key, which matters for CEnvironment: "Dark001" sets
// DirLight.Overwrite and DirLight.Intensity but no colour, and means "the
// level's light at half strength", not "black".
bool ReadColor(TemplateCache& templates, const Properties& props, const std::string& base,
		const std::string& key, Vec3& out) {
	Vec3 raw = out * 255.f;
	bool found = true;
	if (!props.Vector3(key, raw)) {
		found = false;
		// Not on the instance, so walk the template chain by hand: Vector3 has
		// no resolving form, and the colour usually lives on Point_White.CLight.
		std::string name = base;
		for (int hop = 0; hop < 8 && !name.empty(); ++hop) {
			const Properties* t = templates.Find(name);
			if (!t) break;
			if (t->Vector3(key, raw)) { found = true; break; }
			const std::string next = t->String("BaseObj", "");
			if (next == name) break;
			name = next;
		}
	}
	if (found) out = raw / 255.f;
	return found;
}

bool ReadVector(TemplateCache& templates, const Properties& props, const std::string& base,
		const std::string& key, Vec3& out) {
	if (props.Vector3(key, out)) return true;
	std::string name = base;
	for (int hop = 0; hop < 8 && !name.empty(); ++hop) {
		const Properties* t = templates.Find(name);
		if (!t) break;
		if (t->Vector3(key, out)) return true;
		const std::string next = t->String("BaseObj", "");
		if (next == name) break;
		name = next;
	}
	return false;
}

} // namespace

// LIGHT.SetFalloff's third argument, and the ConeAngle property behind it.
// 0x10137720 stores cos(a * pi/180) as the cone cosine and cos(a * pi/180 *
// 0.8) beside it, so ONE angle gives both edges: full brightness inside 0.8a,
// ramping to nothing at a. Degrees go through unhalved.
void SetCone(LightSource& l, float degrees) {
	if (degrees <= 0.f) {
		l.coneCos = l.coneOuterCos = -1.f;
		return;
	}
	const float rad = degrees * 3.14159265f / 180.f;
	l.coneOuterCos = std::cos(rad);
	l.coneCos = std::cos(rad * 0.8f);
}

float LightReach(const LightSource& l) {
	if (l.type != LightSource::kSpot || l.coneOuterCos <= -1.f) return l.range;
	// The beam runs `range` along the axis; the cone's rim is the slant, which
	// is longer. Clamped so a near-hemispherical cone does not blow up.
	return l.range / std::max(l.coneOuterCos, 0.2f);
}

float LightAttenuation(const LightSource& l, const Vec3& at, float radius) {
	if (l.type == LightSource::kDirectional) return l.intensity;

	const Vec3 toPoint = AsVec3(at) - l.pos;
	const float dist = toPoint.Length();
	// A spot's falloff runs ALONG ITS AXIS - tu2_proj_1 reads the ramp from
	// one plane row, not from a radius - so its score is measured there too.
	// Measured radially it drops to zero on a sphere the beam is wider than,
	// and a model in the far half of the beam loses the light entirely.
	// GetAttIntensity uses the radius for both; this is a deviation.
	const bool spot = l.type == LightSource::kSpot && l.coneOuterCos > -1.f;

	float d = dist; // how far along the light the shape sits
	if (spot) {
		d = Dot(toPoint, l.dir);
		if (d < -radius) return 0.f;
		// Sphere against cone: the perpendicular offset against the cone's
		// radius at that depth, both grown by `radius`. Tested at the CENTRE
		// alone this is a knife edge, and because a zero score means "no slot"
		// rather than "no light", a model whose origin leaves the beam goes
		// dark whole while half of it is still inside - a pop as the beam
		// sweeps past, with every other slot free.
		const float perp2 = std::max(0.f, dist * dist - d * d);
		const float c = std::min(std::max(l.coneOuterCos, 1e-3f), 0.999f);
		const float coneR = std::max(d, 0.f) * (std::sqrt(1.f - c * c) / c) + radius;
		if (perp2 > coneR * coneR) return 0.f;
	}
	d = std::max(0.f, d - radius);
	if (d > l.range) return 0.f;

	float att = 1.f;
	if (d > l.startFalloff && l.range > l.startFalloff)
		att = (d - l.range) / (l.startFalloff - l.range);

	// The cone's soft edge, for the POINT form only. With a radius the sphere
	// test above has already answered, and grading by the centre's angle would
	// score a model whose near side is fully lit as though it were unlit.
	if (spot && radius <= 0.f) {
		Vec3 dir = toPoint;
		Normalize(dir);
		const float c = Dot(dir, l.dir);
		if (c < l.coneCos && l.coneCos > l.coneOuterCos)
			att *= (c - l.coneOuterCos) / (l.coneCos - l.coneOuterCos);
	}
	return att * l.intensity;
}

void EntityLighting::Clear() {
	lights_.clear();
	environments_.clear();
}

void EntityLighting::Build(const Level& level, TemplateCache& templates,
		bool lightsFromScripts) {
	Clear();
	const LevelInfo& info = level.info();
	for (int i = 0; i < 3; ++i) {
		levelAmbient_[i] = info.ambient[i] / 255.f;
		levelDirColor_[i] = info.dirLightColor[i] / 255.f;
		levelDirDir_[i] = -info.dirLightDir[i]; // direction TO the light
	}
	levelDirIntensity_ = info.dirLightIntensity;
	Normalize(levelDirDir_);

	for (const Entity& e : level.entities()) {
		if (e.type == "CLight") {
			if (lightsFromScripts) continue;
			Light l;
			l.type = static_cast<int>(
					templates.ResolveNumber(e.props, e.baseObj, "Type", kPoint));
			l.intensity = static_cast<float>(
					templates.ResolveNumber(e.props, e.baseObj, "Intensity", 1.0));
			l.range = static_cast<float>(
					templates.ResolveNumber(e.props, e.baseObj, "Range", 10.0));
			l.startFalloff = static_cast<float>(
					templates.ResolveNumber(e.props, e.baseObj, "StartFalloff", 0.0));
			// One authored angle, two cosines - see SetCone. A cone that was
			// never authored must not black out a spot light, so an absent
			// angle means "no cone".
			SetCone(l, float(templates.ResolveNumber(e.props, e.baseObj, "ConeAngle", 0.0)));
			l.fakeSpecular = templates.ResolveNumber(e.props, e.baseObj, "IsFakeSpecular", 0.0) != 0.0 ||
					e.props.Bool("IsFakeSpecular", false);
			l.color[0] = l.color[1] = l.color[2] = 1.f;
			ReadColor(templates, e.props, e.baseObj, "Color", l.color);
			l.pos = e.pos;
			e.props.Vector3("Pos", l.pos);
			ReadVector(templates, e.props, e.baseObj, "Direction", l.dir);
			Normalize(l.dir);
			// An IsFakeSpecular light is not an entity light at all. Engine.dll
			// has WorldMesh::AddSpecularLight beside Entity::AddLight, the
			// scripts call MESH.ResetSpecularLights, and Entity::AddLight
			// (0x1d1b70) rejects a light carrying the fake-specular flag
			// (0x01000000) unless the entity names it explicitly. It shows:
			// Cathedral's aa_fake1 has Range 5000 and would otherwise occupy a
			// slot everywhere in the level, crowding out a real light.
			if (l.fakeSpecular) continue;
			if (l.intensity > 0.f && l.range > 0.f) lights_.push_back(l);
			continue;
		}
		if (e.type != "CEnvironment") continue;

		Environment env;
		Vec3 centre = e.pos;
		e.props.Vector3("Pos", centre);
		const float w = float(templates.ResolveNumber(e.props, e.baseObj, "Size.Width", 0.0));
		const float h = float(templates.ResolveNumber(e.props, e.baseObj, "Size.Height", 0.0));
		const float d = float(templates.ResolveNumber(e.props, e.baseObj, "Size.Depth", 0.0));
		if (w <= 0.f || h <= 0.f || d <= 0.f) continue;
		const Vec3 half{w * 0.5f, h * 0.5f, d * 0.5f};
		for (int i = 0; i < 3; ++i) {
			env.lo[i] = centre[i] - half[i];
			env.hi[i] = centre[i] + half[i];
		}
		env.volume = w * h * d;
		env.ambientOverwrite = e.props.Bool("Ambient.Overwrite", false);
		env.dirOverwrite = e.props.Bool("DirLight.Overwrite", false);
		// Overwrite means "my values win", not "everything I did not mention is
		// black": Cathedral's "Dark001" sets DirLight.Overwrite and
		// DirLight.Intensity 0.5 and no colour, meaning the level's light at
		// half strength. So each field is applied only where it was authored.
		env.hasAmbient = ReadColor(templates, e.props, e.baseObj, "Ambient.Color", env.ambient);
		env.hasDirColor = ReadColor(templates, e.props, e.baseObj, "DirLight.Color", env.dirColor);
		env.hasDirDir = ReadVector(templates, e.props, e.baseObj, "DirLight.Dir", env.dirDir);
		Normalize(env.dirDir);
		env.dirIntensity = float(
				templates.ResolveNumber(e.props, e.baseObj, "DirLight.Intensity", 1.0));
		env.fadeTime = float(
				templates.ResolveNumber(e.props, e.baseObj, "DirLight.FadeTime", 0.0));
		// A box thinner than two margins would never reach full weight.
		env.margin = std::min(kEnvBlendMargin, std::min(half[0], std::min(half[1], half[2])));
		environments_.push_back(env);
	}
	// Outermost first: Evaluate applies them in this order, so a dark alcove
	// inside a dark hall overrides the hall, weighted by its own edge ramp.
	std::sort(environments_.begin(), environments_.end(),
			[](const Environment& a, const Environment& b) { return a.volume > b.volume; });
}

namespace {

// 0 outside the box, rising to 1 `margin` inside its nearest face.
float BoxWeight(const Vec3& lo, const Vec3& hi, float margin, const Vec3& p) {
	float inside = 1e9f;
	for (int i = 0; i < 3; ++i)
		inside = std::min(inside, std::min(p[i] - lo[i], hi[i] - p[i]));
	if (inside <= 0.f) return 0.f;
	return margin > 1e-6f ? std::min(1.f, inside / margin) : 1.f;
}

float Luminance(const Vec3& c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; }

} // namespace

void EntityLighting::DirectionalBoxes(std::vector<DirBox>& out, float& levelFactor) const {
	// Each box's directional on its own, against the brightest in the level.
	std::vector<float> strength;
	float reference = Luminance(levelDirColor_) * levelDirIntensity_;
	for (const Environment& env : environments_) {
		if (!env.dirOverwrite) { strength.push_back(-1.f); continue; }
		const float s = Luminance(env.hasDirColor ? env.dirColor : levelDirColor_) *
				env.dirIntensity;
		strength.push_back(s);
		reference = std::max(reference, s);
	}
	out.clear();
	if (reference <= 1e-6f) { levelFactor = 0.f; return; }
	levelFactor = Luminance(levelDirColor_) * levelDirIntensity_ / reference;
	for (size_t i = 0; i < environments_.size(); ++i) {
		if (strength[i] < 0.f) continue;
		const Environment& env = environments_[i];
		out.push_back({env.lo, env.hi, env.margin, strength[i] / reference});
	}
}

void EntityLighting::Evaluate(const Vec3& pos, float radius, float dt,
		EntityLightFade& fade, EntityLightState& out) const {
	// --- ambient and the one directional, per environment ---
	// Outermost box first, each weighted by how far inside its faces the
	// point is (kEnvBlendMargin), so a doorway is a ramp in space. The
	// original snaps to the tightest box and lerps over DirLight.FadeTime;
	// fs_world blends the same list the same way for the model shadows.
	(void)dt;
	Vec3 ambient = levelAmbient_;
	Vec3 dirColor = levelDirColor_; // intensity applied below
	Vec3 dirDir = levelDirDir_;
	float intensity = levelDirIntensity_;
	for (const Environment& env : environments_) {
		const float w = BoxWeight(env.lo, env.hi, env.margin, pos);
		if (w <= 0.f) continue;
		if (env.ambientOverwrite && env.hasAmbient)
			ambient += (env.ambient - ambient) * w;
		if (env.dirOverwrite) {
			// Intensity always applies; colour and direction only where stated.
			intensity += (env.dirIntensity - intensity) * w;
			if (env.hasDirColor) dirColor += (env.dirColor - dirColor) * w;
			if (env.hasDirDir) {
				dirDir += (-env.dirDir - dirDir) * w;
				Normalize(dirDir);
			}
		}
	}
	// The intensity multiplies whichever colour won.
	dirColor *= intensity;
	fade.ambient = ambient;
	fade.dirColor = dirColor;
	fade.dirDir = dirDir;
	fade.primed = true;
	out.ambient = ambient;
	out.dirColor = dirColor;
	out.dirDir = dirDir;

	// --- the positional lights ---
	// Entity::AddLight keeps its list sorted by attenuated intensity,
	// descending, so a model near two torches takes the two brightest. That
	// part is kept; the length is not (see kMaxDynamicLights), and the
	// environment directional no longer competes for a place in it.
	//
	// The list is the placed lights followed by the ones the scripts made this
	// frame, so a torch a monk is carrying is ranked against the level's own on
	// the same terms - which is what Entity::AddLight does, it has one list and
	// does not care where a light came from. Only the SCORE is evaluated at the
	// entity origin now; the light itself is handed over whole and shaded per
	// pixel.
	const LightSource* best[kMaxDynamicLights] = {};
	float score[kMaxDynamicLights] = {};
	int count = 0;
	auto consider = [&](const LightSource& l, float s) {
		// Entity::AddLight (0x1d1b70) rejects a light carrying the
		// fake-specular flag: those belong to the world mesh, and Cathedral's
		// aa_fake1 has Range 5000, so one would occupy a slot level-wide.
		if (s <= 0.f || l.fakeSpecular) return;
		int at = 0;
		while (at < count && score[at] >= s) ++at;
		if (at >= kMaxDynamicLights) return;
		for (int j = std::min(count, kMaxDynamicLights - 1); j > at; --j) {
			best[j] = best[j - 1];
			score[j] = score[j - 1];
		}
		best[at] = &l;
		score[at] = s;
		if (count < kMaxDynamicLights) ++count;
	};
	for (const Light& l : lights_) consider(l, LightAttenuation(l, pos, radius));
	for (const Light& l : dynamic_) consider(l, LightAttenuation(l, pos, radius));

	out.lightCount = count;
	for (int s = 0; s < kMaxDynamicLights; ++s) out.lights[s] = best[s];
}

} // namespace painful
