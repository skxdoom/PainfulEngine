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
	{"HudAspect", "2", false, "aspect ratio of the interface: 0 stretched, 1 centred, 2 anchored by thirds"},
	{"WindowMode", "0", false, "0 default (set from config.ini), 1 windowed, 2 borderless"},
	{"FlashlightShadows", "true", true, "whether the flashlight casts shadows"},
	{"FlashlightShadowMapSize", "512", false, "sets flashlight shadow map size in texels"},
	{"CharacterShadowSize", "256", false, "character shadow size in texels (32 to 1024)"},
	{"CharacterShadowStrength", "60", false, "characters shadow strength"},
	{"CharacterShadowCasters", "24", false, "characters with a shadow per frame, nearest first (the original's cap is 24; up to 64)"},
	{"ShadowMapPlacedLights", "true", true, "whether the placed lights in the level places cast shadow maps"},
	{"ShadowMapDynLights", "true", true, "whether the dynamically created lights cast shadow maps"},
	{"ShadowMapMaxLights", "8", false, "lights with a shadow map per frame, up to 8"},
	{"ShadowMapLightsRadius", "40", false, "the radius in which a light gets a shadow map"},
	{"ShadowMapSize", "256", false, "light shadow map size in texels"},
	{"ShadowMapStrength", "80", false, "light shadow map strength"},
	{"ViewModelShadows", "true", true, "whether the weapon view model cast self shadows"},
	{"ViewModelShadowMapSize", "1024", false, "view model shadow map size in texels, for each of its four maps"},
	{"SSAO", "false", true, "enables SSAO"},
	{"SSAOScreenRadius", "40", false, "SSAO radius, thousandths of the screen's height at any distance"},
	{"SSAOIntensity", "500", false, "SSAO intensity"},
	{"BloomScale", "2", false, "bloom is blurred at 1/N of the screen; the original is 2"},
	{"BloomKernel", "0", false, "bloom blur: 0 the Gaussian to three sigma, 1 the original 13 taps"},
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
		// A key this build does not know (renamed or dropped) is left out.
		std::string canonical;
		if (!Canonical(key, canonical)) { oldStyle = true; continue; }
		Set(key, Trim(t.substr(eq + 1)));
		seen[canonical] = true;
	}
	in.close();
	// A key this build knows and the file does not, one it does not know, or the
	// older style: rewritten so the file reads as this build writes it.
	bool missing = false;
	for (const Known& k : kKnown)
		if (!seen.count(k.key)) missing = true;
	if (missing || oldStyle) Save();
	return true;
}

bool EngineConfig::Save() const {
	std::ofstream out(path_);
	if (!out) return false;
	for (const Known& k : kKnown) {
		const auto it = values_.find(k.key);
		out << kPrefix << k.key << " = " << (it != values_.end() ? it->second : k.value) << "\n";
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
