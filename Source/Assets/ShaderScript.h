#pragma once
#include <map>
#include <string>
#include <vector>

namespace painful {

// The game's material system is data: Data/Shaders/Scripts/*.shader describe
// every material class in a Quake-style text format.
//
//     shader<nv20> sky copy terraintu2 {
//         pass {
//             depthwrite false
//             blend      none
//             texop[1]   = texture modulate previous
//             texenv[2]  = clamp bilinear_nomips
//             map[0]     = $colormap
//             vshader    = tu2_blend def tu2
//             fshader    = layer3_blend
//         }
//     }
//
// The <tag> selects a hardware tier (nv20, nv30, nv40, r200, tnl); untagged
// definitions apply to any. "copy BASE" inherits BASE's passes and overrides
// individual keys. This parser keeps every statement as raw text - key on the
// left, the rest of the line on the right - so nothing is interpreted or lost;
// mapping onto bgfx state happens in the renderer.
struct ShaderPass {
    // Full left-hand token (lowercased) -> value text, e.g.
    //   "depthwrite"  -> "true"
    //   "texop[1]"    -> "texture modulate previous"
    //   "texop[1].a"  -> "disable"
    //   "vshader"     -> "tu2_blend def tu2"
    std::map<std::string, std::string> keys;

    std::string Get(const std::string& key, const std::string& fallback = "") const {
        auto it = keys.find(key);
        return it == keys.end() ? fallback : it->second;
    }
};

struct ShaderDef {
    std::string name;
    std::string variant;               // "nv20", "tnl", ... or empty for any
    std::string base;                  // "copy" source, or empty
    std::vector<std::string> flags;    // "setflag warp", "setflag reflect", ...
    std::vector<ShaderPass> passes;
    std::string sourceFile;
};

class ShaderLibrary {
public:
    // Parses every *.shader under <dir> (Data/Shaders/Scripts).
    bool LoadDirectory(const std::string& dir);

    // Finds a shader by name with copy-inheritance applied. Variants are tried
    // in the order given (most capable first), then untagged, then anything.
    // Returns null when the name is unknown.
    const ShaderDef* Find(const std::string& name,
                          const std::vector<std::string>& variantOrder = {"nv40", "nv30",
                                                                          "nv20", "r200",
                                                                          "tnl"});

    size_t size() const { return defs_.size(); }
    const std::vector<ShaderDef>& all() const { return defs_; }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    const ShaderDef* FindRaw(const std::string& name,
                             const std::vector<std::string>& variantOrder) const;

    std::vector<ShaderDef> defs_;
    std::vector<std::string> errors_;
    // Cache of copy-resolved definitions, keyed by name|variantOrder[0].
    std::map<std::string, ShaderDef> resolved_;
};

} // namespace painful
