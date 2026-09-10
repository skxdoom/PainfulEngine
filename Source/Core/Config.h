#pragma once
#include <cstddef>
#include <map>
#include <string>

// painful_config.ini: the engine's own settings, beside the executable, for
// everything the original's config.ini has no field for. Written the way the
// original writes its own - one `Pf.Key = value` a line, sorted, no prose -
// and read back the same way, an older `Key = value` line included. The
// console's `pf` prefix lists, sets, saves and reloads them (Game/ConfigConsole).
// Docs/Reference/Console.md, "pf".
namespace painful {

class EngineConfig {
public:
	// A key the engine knows: its default, whether it is a boolean (written
	// true/false, as the original writes its own), and one line of help.
	struct Known {
		const char* key;
		const char* value;
		bool boolean;
		const char* help;
	};
	static const Known* KnownKeys(size_t& count);
	static const char* FileName() { return "painful_config.ini"; }

	// Reads `dir/painful_config.ini`; missing is not an error. Returns false
	// only when the file exists and cannot be read. A file from an older
	// build is rewritten in the current style with the keys it lacked.
	bool Load(const std::string& dir);
	bool Reload();
	// Writes every key, known ones first in order, the rest after.
	bool Save() const;

	std::string GetString(const std::string& key, const std::string& fallback) const;
	int GetInt(const std::string& key, int fallback) const;
	bool GetBool(const std::string& key, bool fallback) const;
	// Stores under the key's canonical spelling when it is known, and a
	// known boolean as true/false whatever spelling of yes/no arrived.
	void Set(const std::string& key, const std::string& value);
	// The known key `key` names, case-insensitively; false when none does.
	bool Canonical(const std::string& key, std::string& out) const;

	const std::string& path() const { return path_; }
	// Bumped by every Set and Load, so a reader can apply changes when they
	// happen rather than re-reading every frame.
	unsigned generation() const { return generation_; }

private:
	std::string path_;
	std::map<std::string, std::string> values_;
	unsigned generation_ = 0;
};

// The one instance, loaded by the entry point before anything reads it. The
// tools never load it and read the defaults.
EngineConfig& Settings();

} // namespace painful
