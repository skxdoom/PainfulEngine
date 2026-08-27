#pragma once
#include <map>
#include <string>

namespace painful {

// LScripts/Main/Tweak.lua - the game's physics and movement constants.
//
// These are not our numbers. The engine reads this exact file:
// PhysicsEngine::GetTweaksFromScript (Engine.dll 0x10185a80) walks the Lua
// table field by field into a flat struct, and Tweak:Apply() calls
// WORLD.ApplyTweaks() to push it into the physics world. So the file is the
// authority on player speed, gravity, jump strength, air control, step height
// and the rest, and reading it is preferable to copying values into C++.
//
// The format is a nested Lua table literal rather than the "o.Key = value"
// form Properties handles, so it gets its own small reader. Keys are the
// dotted path with the leading "Tweak." removed:
//
//     Tweak = { PlayerMove = { PlayerSpeed = 8.0 } }   ->  PlayerMove.PlayerSpeed
//
// Values may be simple arithmetic - the shipped file writes gravity as
// "2*9.81" and the bullet-time slowdown as "1/8" - so a left-to-right
// evaluator handles + - * / .
class Tweaks {
public:
    // Reads <dataRoot>/LScripts/Main/Tweak.lua. Returns false if the file is
    // missing; the accessors then answer with the caller's fallbacks.
    bool LoadFromDataRoot(const std::string& dataRoot);
    bool LoadFromFile(const std::string& path);
    void LoadFromText(const std::string& text);

    bool Has(const std::string& key) const { return numbers_.count(key) || bools_.count(key); }
    float Number(const std::string& key, float fallback) const;
    bool  Bool(const std::string& key, bool fallback) const;

    bool loaded() const { return loaded_; }
    size_t size() const { return numbers_.size() + bools_.size(); }

private:
    std::map<std::string, double> numbers_;
    std::map<std::string, bool> bools_;
    bool loaded_ = false;
};

} // namespace painful
