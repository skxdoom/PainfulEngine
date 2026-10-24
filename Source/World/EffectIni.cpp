#include "EffectIni.h"

#include <cctype>

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

} // namespace

std::map<std::string, std::string> ParseIni(const std::string& text) {
	std::map<std::string, std::string> out;
	std::string section;
	size_t i = 0;
	while (i < text.size()) {
		const size_t end = text.find('\n', i);
		const std::string line =
			Trim(text.substr(i, end == std::string::npos ? std::string::npos : end - i));
		i = end == std::string::npos ? text.size() : end + 1;
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line[0] == '[') {
			const size_t close = line.find(']');
			if (close != std::string::npos) section = Lower(Trim(line.substr(1, close - 1)));
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		out[section + '.' + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
	}
	return out;
}

int BlendModeByName(const std::string& name) {
	static const char* const kNames[] = {"none", "alpha", "add", "modulate", "filter",
			"translucent", "invmodulate", "subtract",
			"revsubtract", "desttranslucent", "destalpha",
			"modulate2x"};
	const std::string n = Lower(name);
	for (int i = 0; i < int(sizeof(kNames) / sizeof(kNames[0])); ++i)
		if (n == kNames[i]) return i;
	return -1;
}

} // namespace painful
