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

    // texturesRoot is <DataRoot>/Textures.
    // createWhite=false builds only the file index, so the resolver can be used
    // without an initialised graphics device (diagnostics, tests).
    bool Init(const std::string& texturesRoot, bool createWhite = true);
    void Shutdown();

    // Returns a valid handle; falls back to a white texture when unresolved.
    bgfx::TextureHandle Get(const std::string& reference, const std::string& levelHint);
    // Cube maps need their own creation call and their own sampler type in the
    // shader, so they cannot share Get(). The engine hardcodes exactly one:
    // special/cube_wenecja, for water reflections.
    bgfx::TextureHandle GetCube(const std::string& reference, const std::string& levelHint);
    bgfx::TextureHandle WhiteCube() const { return whiteCube_; }
    bgfx::TextureHandle White() const { return white_; }
    // Fully transparent 1x1. A sky layer whose texture is missing should vanish,
    // not paint an opaque sheet across the sky.
    bgfx::TextureHandle Transparent() const { return transparent_; }

    size_t indexedFiles() const { return index_.size(); }
    size_t loadedTextures() const { return loaded_; }
    size_t missing() const { return missing_; }

    // Exposed for diagnostics: which file does a reference map to?
    std::string Resolve(const std::string& reference, const std::string& levelHint) const;

    // The texture's pixel size, for MATERIAL.Size. False when the reference
    // has not been loaded, or resolved to the white fallback.
    bool Size(const std::string& reference, int& w, int& h) const;

    // Pixel size WITHOUT a graphics device: resolves the reference and parses
    // the image header on the CPU, recording the result for Size(). This is
    // what lets a headless run lay the HUD out - MATERIAL.Size is how every
    // HUD script scales itself, and the layout is not part of the drawing.
    // Pairs with Init(root, createWhite=false).
    bool Measure(const std::string& reference, const std::string& levelHint,
                 int& w, int& h);

private:

    std::map<std::string, std::string> index_;              // key -> absolute path
    std::map<std::string, bgfx::TextureHandle> cache_;      // reference -> handle
    // Pixel dimensions, kept because MATERIAL.Size is how every HUD script
    // lays itself out: it asks an image how big it is and scales from there.
    std::map<std::string, std::pair<int, int>> sizes_;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteCube_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle transparent_ = BGFX_INVALID_HANDLE;
    size_t loaded_ = 0, missing_ = 0;
};

} // namespace painful
