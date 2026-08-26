#pragma once
#include <bgfx/bgfx.h>
#include <map>
#include <string>

namespace painful {

// Resolves PainEngine texture references to GPU textures.
//
// References do NOT match the files on disk: models ask for "Models/foo.tga"
// while the shipped file is "Models/foo.dds", and map materials store bare names
// with no extension at all. Lookup is therefore extension-agnostic and
// case-insensitive, with map textures also searched under Levels/<mapName>/.
class TextureCache {
public:
    ~TextureCache() { Shutdown(); }

    // texturesRoot is Data_Extracted/Textures.
    // createWhite=false builds only the file index, so the resolver can be used
    // without an initialised graphics device (diagnostics, tests).
    bool Init(const std::string& texturesRoot, bool createWhite = true);
    void Shutdown();

    // Returns a valid handle; falls back to a white texture when unresolved.
    bgfx::TextureHandle Get(const std::string& reference, const std::string& levelHint);
    bgfx::TextureHandle White() const { return white_; }
    // Fully transparent 1x1. A sky layer whose texture is missing should vanish,
    // not paint an opaque sheet across the sky.
    bgfx::TextureHandle Transparent() const { return transparent_; }

    size_t indexedFiles() const { return index_.size(); }
    size_t loadedTextures() const { return loaded_; }
    size_t missing() const { return missing_; }

    // Exposed for diagnostics: which file does a reference map to?
    std::string Resolve(const std::string& reference, const std::string& levelHint) const;

private:

    std::map<std::string, std::string> index_;              // key -> absolute path
    std::map<std::string, bgfx::TextureHandle> cache_;      // reference -> handle
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle transparent_ = BGFX_INVALID_HANDLE;
    size_t loaded_ = 0, missing_ = 0;
};

} // namespace painful
