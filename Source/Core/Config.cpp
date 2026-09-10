#include "Config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

namespace painful {

namespace {

std::string Trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
	return s.substr(a, b - a);
}

// The keys the engine knows, with their defaults and the comment written
// above each in the file. Anything else in the file is kept as it was.
struct Known {
	const char* key;
	const char* value;
	const char* comment;
};
const Known kKnown[] = {
	{"HudAspect", "2",
			"How the 4:3 interface is laid onto a wider screen.\n"
			"#   0 - as the original: stretched to the window\n"
			"#   1 - the whole interface centred in a 4:3 area\n"
			"#   2 - anchored: the left third of the interface sticks to the left edge, the\n"
			"#       right third to the right edge, the middle stays centred (health left,\n"
			"#       ammo right, crosshair centred)"},
	{"WindowMode", "0",
			"The window mode, kept whenever a resolution is applied in the video options.\n"
			"#   0 - as the original: config.ini's Fullscreen decides, fullscreen or a window\n"
			"#   1 - always a window, at the chosen resolution\n"
			"#   2 - always borderless: fills the desktop at its own size; the resolution\n"
			"#       setting is not used"},
	{"FlashlightShadows", "1",
			"Whether the flashlight casts shadows: the world and every model into one\n"
			"# shadow map. 1 on, 0 off. The video options' Shadows setting toggles it too."},
	{"ShadowMapSize", "512",
			"The shadow map's size in texels. Larger is sharper and dearer; 512 reads as\n"
			"# a torch beam, 2048 as a spotlight."},
	{"ModelShadows", "1",
			"Whether the models cast shadows from the level's directional light, onto\n"
			"# the world and each other. 1 on, 0 off. The world's own shadows are baked\n"
			"# into its lightmaps and are not affected."},
	{"ModelShadowMapSize", "1024",
			"The model shadow map's size in texels. It covers 48 units about the camera."},
	{"LightShadows", "1",
			"Whether the placed lights - lamps, torches, candles - cast shadows on the\n"
			"# models, each other and themselves included. The world keeps its lightmap.\n"
			"# 1 on, 0 off."},
	{"LightShadowLights", "8",
			"How many placed lights get a shadow map each frame, the strongest within\n"
			"# the radius below; the rest light without shadows. Up to 8."},
	{"LightShadowRadius", "40",
			"How far from the camera, in world units, a placed light can be and still\n"
			"# get a shadow map. Its shadows fade out over the outer third of that."},
	{"LightShadowMapSize", "256",
			"Texels per face of a placed light's shadow map; a point light has six."},
	{"LightShadowWorldStrength", "100",
			"How much of a placed light a model's shadow takes off the world beneath it,\n"
			"# in percent of that light's own contribution as the engine computes it - the\n"
			"# lightmap already holds the light, so the shadow subtracts. 0 keeps the\n"
			"# world untouched."},
	{"ModelLighting", "0",
			"How the models are lit.\n"
			"#   0 - as the original: the environment box's ambient and directional at\n"
			"#       full, the placed and dynamic lights on top\n"
			"#   1 - led by the lights: the box's ambient and directional scaled by the\n"
			"#       two values below, the lights by the third, so a model's shade side\n"
			"#       goes as dark as the shadow it casts"},
	{"ModelAmbientScale", "50",
			"Mode 1: percent of the box ambient a model keeps."},
	{"ModelDirectionalScale", "50",
			"Mode 1: percent of the box directional a model keeps."},
	{"ModelLightScale", "100",
			"Mode 1: percent of the placed and dynamic lights' strength on a model."},
	{"ModelShadowStrength", "60",
			"How dark a model's shadow falls on the world, in percent of the baked light,\n"
			"# where the level's environment boxes give their full directional; a box that\n"
			"# says shade weakens the shadow with the light."},
};

} // namespace

bool EngineConfig::Load(const std::string& dir) {
	path_ = dir.empty() ? std::string(FileName()) : dir + "/" + FileName();
	for (const Known& k : kKnown) values_[k.key] = k.value;

	std::ifstream in(path_);
	if (!in) {
		Save(); // so the file is there to edit, with its defaults
		return true;
	}
	std::string line;
	std::map<std::string, bool> seen;
	while (std::getline(in, line)) {
		const std::string t = Trim(line);
		if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
		const size_t eq = t.find('=');
		if (eq == std::string::npos) continue;
		const std::string key = Trim(t.substr(0, eq));
		values_[key] = Trim(t.substr(eq + 1));
		seen[key] = true;
	}
	in.close();
	// A key this build knows and the file does not: an older file. Rewritten
	// so the new key appears with its comment, everything else kept.
	for (const Known& k : kKnown)
		if (!seen.count(k.key)) { Save(); break; }
	return true;
}

bool EngineConfig::Save() const {
	std::ofstream out(path_);
	if (!out) return false;
	out << "# PainfulEngine settings. The original's config.ini keeps everything it\n"
			"# always had; only what is new with PainfulEngine lives here.\n";
	std::map<std::string, std::string> rest = values_;
	for (const Known& k : kKnown) {
		out << "\n# " << k.comment << "\n";
		const auto it = rest.find(k.key);
		out << k.key << " = " << (it != rest.end() ? it->second : k.value) << "\n";
		if (it != rest.end()) rest.erase(it);
	}
	if (!rest.empty()) {
		out << "\n# Not known to this build, kept as found.\n";
		for (const auto& kv : rest) out << kv.first << " = " << kv.second << "\n";
	}
	return true;
}

std::string EngineConfig::GetString(const std::string& key, const std::string& fallback) const {
	const auto it = values_.find(key);
	return it != values_.end() ? it->second : fallback;
}

int EngineConfig::GetInt(const std::string& key, int fallback) const {
	const auto it = values_.find(key);
	if (it == values_.end()) return fallback;
	char* end = nullptr;
	const long v = std::strtol(it->second.c_str(), &end, 10);
	return end && *end == '\0' ? int(v) : fallback;
}

bool EngineConfig::GetBool(const std::string& key, bool fallback) const {
	const auto it = values_.find(key);
	if (it == values_.end()) return fallback;
	std::string v = it->second;
	for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
	if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
	if (v == "0" || v == "false" || v == "no" || v == "off") return false;
	return fallback;
}

void EngineConfig::Set(const std::string& key, const std::string& value) {
	values_[key] = value;
}

EngineConfig& Settings() {
	static EngineConfig config;
	static bool defaults = false;
	if (!defaults) {
		defaults = true;
		for (const Known& k : kKnown) config.Set(k.key, k.value);
	}
	return config;
}

} // namespace painful
