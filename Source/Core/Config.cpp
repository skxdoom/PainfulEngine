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

std::string Lower(std::string s) {
	for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// The prefix the file carries on every line, the way config.ini says Cfg.
const char* kPrefix = "Pf.";

// The keys the engine knows, in the order the file is written. Booleans are
// written true/false. The help is one line, for the console's `pf help`.
const EngineConfig::Known kKnown[] = {
	{"HudAspect", "2", false, "4:3 interface on a wide screen: 0 stretched, 1 centred, 2 anchored by thirds"},
	{"WindowMode", "0", false, "0 config.ini's Fullscreen decides, 1 always a window, 2 borderless"},
	{"FlashlightShadows", "true", true, "the flashlight casts shadows"},
	{"ShadowMapSize", "512", false, "flashlight shadow map, texels"},
	{"ModelShadows", "true", true, "models cast shadows from the level's directional light"},
	{"ModelShadowMapSize", "1024", false, "model shadow map, texels, over 48 units about the camera"},
	{"ModelShadowStrength", "60", false, "how dark a model's shadow falls on the world, percent"},
	{"LightShadows", "true", true, "the placed lights cast shadows"},
	{"LightShadowLights", "8", false, "placed lights with a shadow map per frame, up to 8"},
	{"LightShadowRadius", "40", false, "how far from the camera a placed light gets a map, units"},
	{"LightShadowMapSize", "256", false, "placed light shadow map, texels per face"},
	{"LightShadowWorldStrength", "100", false, "how much of a placed light a model's shadow takes off the world, percent"},
	{"ModelLighting", "0", false, "0 as the original, 1 led by the lights (the three scales below)"},
	{"ModelAmbientScale", "50", false, "mode 1: percent of the box ambient a model keeps"},
	{"ModelDirectionalScale", "50", false, "mode 1: percent of the box directional a model keeps"},
	{"ModelLightScale", "100", false, "mode 1: percent of the lights' strength on a model"},
};

const EngineConfig::Known* FindKnown(const std::string& key) {
	const std::string want = Lower(key);
	for (const EngineConfig::Known& k : kKnown)
		if (Lower(k.key) == want) return &k;
	return nullptr;
}

bool ParseBool(const std::string& v, bool& out) {
	const std::string s = Lower(Trim(v));
	if (s == "1" || s == "true" || s == "yes" || s == "on") { out = true; return true; }
	if (s == "0" || s == "false" || s == "no" || s == "off") { out = false; return true; }
	return false;
}

} // namespace

const EngineConfig::Known* EngineConfig::KnownKeys(size_t& count) {
	count = sizeof(kKnown) / sizeof(kKnown[0]);
	return kKnown;
}

bool EngineConfig::Canonical(const std::string& key, std::string& out) const {
	const Known* k = FindKnown(key);
	if (!k) return false;
	out = k->key;
	return true;
}

bool EngineConfig::Load(const std::string& dir) {
	path_ = dir.empty() ? std::string(FileName()) : dir + "/" + FileName();
	return Reload();
}

bool EngineConfig::Reload() {
	values_.clear();
	for (const Known& k : kKnown) values_[k.key] = k.value;
	++generation_;

	std::ifstream in(path_);
	if (!in) {
		Save(); // so the file is there to edit, with its defaults
		return true;
	}
	std::string line;
	std::map<std::string, bool> seen;
	bool oldStyle = false;
	while (std::getline(in, line)) {
		const std::string t = Trim(line);
		if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
		const size_t eq = t.find('=');
		if (eq == std::string::npos) continue;
		std::string key = Trim(t.substr(0, eq));
		// An older file said `Key = value` with no prefix.
		if (Lower(key).rfind(Lower(kPrefix), 0) == 0) key = key.substr(3);
		else oldStyle = true;
		Set(key, Trim(t.substr(eq + 1)));
		std::string canonical;
		seen[Canonical(key, canonical) ? canonical : key] = true;
	}
	in.close();
	// A key this build knows and the file does not, or the older style:
	// rewritten so the file reads as this build writes it, everything kept.
	bool missing = false;
	for (const Known& k : kKnown)
		if (!seen.count(k.key)) missing = true;
	if (missing || oldStyle) Save();
	return true;
}

bool EngineConfig::Save() const {
	std::ofstream out(path_);
	if (!out) return false;
	std::map<std::string, std::string> rest = values_;
	for (const Known& k : kKnown) {
		const auto it = rest.find(k.key);
		out << kPrefix << k.key << " = " << (it != rest.end() ? it->second : k.value) << "\n";
		if (it != rest.end()) rest.erase(it);
	}
	// Not known to this build, kept as found.
	for (const auto& kv : rest) out << kPrefix << kv.first << " = " << kv.second << "\n";
	return true;
}

std::string EngineConfig::GetString(const std::string& key, const std::string& fallback) const {
	const auto it = values_.find(key);
	return it != values_.end() ? it->second : fallback;
}

int EngineConfig::GetInt(const std::string& key, int fallback) const {
	const auto it = values_.find(key);
	if (it == values_.end()) return fallback;
	bool b;
	if (ParseBool(it->second, b) && !std::isdigit(static_cast<unsigned char>(it->second[0])))
		return b ? 1 : 0;
	char* end = nullptr;
	const long v = std::strtol(it->second.c_str(), &end, 10);
	return end && *end == '\0' ? int(v) : fallback;
}

bool EngineConfig::GetBool(const std::string& key, bool fallback) const {
	const auto it = values_.find(key);
	if (it == values_.end()) return fallback;
	bool b;
	return ParseBool(it->second, b) ? b : fallback;
}

void EngineConfig::Set(const std::string& key, const std::string& value) {
	const Known* k = FindKnown(key);
	std::string v = Trim(value);
	if (k && k->boolean) {
		bool b;
		if (ParseBool(v, b)) v = b ? "true" : "false";
	}
	values_[k ? k->key : key] = v;
	++generation_;
}

EngineConfig& Settings() {
	static EngineConfig config;
	static bool defaults = false;
	if (!defaults) {
		defaults = true;
		for (const EngineConfig::Known& k : kKnown) config.Set(k.key, k.value);
	}
	return config;
}

} // namespace painful
