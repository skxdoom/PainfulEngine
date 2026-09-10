#include "ConfigConsole.h"
#include "Console.h"
#include "../Core/Config.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace painful {

namespace {

std::string Lower(std::string s) {
	for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string Trim(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
	return s.substr(a, b - a);
}

// "\cmd", "/cmd" and ".cmd" are the same command, as Console.lua takes them.
std::string StripLead(std::string s) {
	s = Trim(s);
	if (!s.empty() && (s[0] == '\\' || s[0] == '/' || s[0] == '.')) s.erase(0, 1);
	return s;
}

// Every command: pf + each key, lowercase.
std::vector<std::string> Commands() {
	std::vector<std::string> out;
	size_t count = 0;
	const EngineConfig::Known* known = EngineConfig::KnownKeys(count);
	for (size_t i = 0; i < count; ++i) out.push_back("pf" + Lower(known[i].key));
	return out;
}

const EngineConfig::Known* KeyOf(const std::string& command) {
	size_t count = 0;
	const EngineConfig::Known* known = EngineConfig::KnownKeys(count);
	for (size_t i = 0; i < count; ++i)
		if ("pf" + Lower(known[i].key) == command) return &known[i];
	return nullptr;
}

// The bare command answers as `fov` does: a usage line, then the value.
void Describe(const EngineConfig::Known& k, Console& console) {
	const std::string name = "pf" + Lower(k.key);
	console.Print(name + (k.boolean ? " 1|0  (" : " value  (") + k.help + ")");
	console.Print("current " + name + ":  " + Settings().GetString(k.key, ""));
}

// A boolean takes 1/0, true/false, on/off, yes/no; anything else is a whole
// number. False means the value is refused and nothing changes.
bool ValidValue(const EngineConfig::Known& k, const std::string& value) {
	const std::string v = Lower(value);
	if (k.boolean)
		return v == "1" || v == "0" || v == "true" || v == "false" || v == "on" ||
				v == "off" || v == "yes" || v == "no";
	if (v.empty()) return false;
	size_t i = v[0] == '-' ? 1 : 0;
	if (i >= v.size()) return false;
	for (; i < v.size(); ++i)
		if (!std::isdigit(static_cast<unsigned char>(v[i]))) return false;
	return true;
}

} // namespace

bool ConfigCommand(const std::string& line, Console& console) {
	std::istringstream in(StripLead(line));
	std::vector<std::string> words;
	std::string w;
	while (in >> w) words.push_back(w);
	if (words.empty()) return false;
	const std::string name = Lower(words[0]);
	if (name.rfind("pf", 0) != 0) return false;

	const EngineConfig::Known* key = KeyOf(name);
	if (!key) {
		console.Print("Unknown command: " + name);
		return true;
	}
	if (words.size() < 2) {
		Describe(*key, console);
		return true;
	}
	// A set is a write, as the scripts' own settings commands write Cfg.
	if (!ValidValue(*key, words[1])) {
		console.Print(name + ": " + (key->boolean ? "1 or 0" : "a whole number") +
				", not " + words[1]);
		return true;
	}
	Settings().Set(key->key, words[1]);
	if (!Settings().Save()) console.Print("could not write " + Settings().path());
	console.Print(name + " " + Settings().GetString(key->key, ""));
	return true;
}

bool ConfigTab(const std::string& text, Console& console) {
	const std::string t = Lower(StripLead(text));
	if (t.rfind("pf", 0) != 0) return false;
	std::vector<std::string> matches;
	for (const std::string& c : Commands())
		if (c.rfind(t, 0) == 0) matches.push_back(c);
	std::sort(matches.begin(), matches.end());
	// Console:OnPrompt's shape: several matches list themselves and leave the
	// common part typed, one completes with a space after it.
	if (matches.size() > 1) {
		console.Print(">" + t);
		std::string common = matches[0];
		for (const std::string& m : matches) {
			console.Print("    " + m);
			size_t j = 0;
			while (j < common.size() && j < m.size() && common[j] == m[j]) ++j;
			common.resize(j);
		}
		console.SetCurrentText("\\" + common);
	} else if (matches.size() == 1) {
		console.SetCurrentText("\\" + matches[0] + " ");
	}
	return true;
}

} // namespace painful
