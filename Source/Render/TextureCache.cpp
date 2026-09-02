#include "TextureCache.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace painful {

// Shared with the texdump diagnostic in main.cpp.
bx::DefaultAllocator g_allocator;

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string StripExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of('/');
    if (dot == std::string::npos) return path;
    if (slash != std::string::npos && dot < slash) return path;
    return path.substr(0, dot);
}

} // namespace

bool TextureCache::Init(const std::string& texturesRoot, bool createWhite) {
    if (createWhite) {
        // A 1x1 white texture stands in for anything unresolved, so a missing
        // file shows up as untextured geometry rather than a crash.
        const uint32_t whitePixel = 0xffffffff;
        white_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::BGRA8,
                                       BGFX_SAMPLER_NONE, bgfx::copy(&whitePixel, 4));
        // A 1x1 white cube, so an unresolved reflection reads as flat white
        // rather than leaving a stale sampler bound.
        const uint32_t whiteFaces[6] = {0xffffffff, 0xffffffff, 0xffffffff,
                                        0xffffffff, 0xffffffff, 0xffffffff};
        whiteCube_ = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::BGRA8,
                                             BGFX_SAMPLER_NONE, bgfx::copy(whiteFaces, 24));
        const uint32_t clearPixel = 0x00000000;
        transparent_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::BGRA8,
                                             BGFX_SAMPLER_NONE, bgfx::copy(&clearPixel, 4));
    }

    FileSystem& vfs = FileSystem::Get();
    if (!vfs.IsDirectory(texturesRoot)) {
        LogWarn("textures root not found: %s", texturesRoot.c_str());
        return false;
    }
    for (const std::string& relOrig : vfs.ListRecursive(texturesRoot)) {
        const fs::path p(relOrig);
        std::string ext = Lower(p.extension().string());
        if (ext != ".dds" && ext != ".tga" && ext != ".bmp") continue;

        std::string rel = Lower(relOrig);
        std::string noExt = StripExtension(rel);
        std::string base = Lower(p.stem().string());
        std::string full = texturesRoot + "/" + relOrig;

        // When several formats share a name: .dds wins - it is what shipped -
        // then .tga, then .bmp. HUD/ChkChecked ships as a .tga (the red tick,
        // 40x37) beside a .bmp that is a 16-pixel Windows icon; a directory
        // listing hands the .bmp over first, and first-seen would draw the
        // icon.
        const auto rank = [](const std::string& e) {
            return e == ".dds" ? 3 : e == ".tga" ? 2 : 1;
        };
        const auto better = [&](const std::string& key) {
            const auto it = index_.find(key);
            if (it == index_.end()) return true;
            return rank(ext) > rank(Lower(fs::path(it->second).extension().string()));
        };
        if (better(noExt)) index_[noExt] = full;
        if (better(base)) index_[base] = full;
    }
    return true;
}

void TextureCache::Shutdown() {
    for (auto& kv : cache_) {
        if (bgfx::isValid(kv.second)) bgfx::destroy(kv.second);
    }
    cache_.clear();
    if (bgfx::isValid(white_)) { bgfx::destroy(white_); white_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(transparent_)) { bgfx::destroy(transparent_); transparent_ = BGFX_INVALID_HANDLE; }
}

bool TextureCache::Size(const std::string& reference, int& w, int& h) const {
    auto it = sizes_.find(reference);
    if (it == sizes_.end()) return false;
    w = it->second.first;
    h = it->second.second;
    return true;
}

bool TextureCache::Measure(const std::string& reference, const std::string& levelHint,
                           int& w, int& h) {
    if (reference.empty()) return false;
    auto it = sizes_.find(reference);
    if (it != sizes_.end()) { w = it->second.first; h = it->second.second; return true; }

    // bimg parses the header on the CPU; only bgfx::copy/createTexture2D in
    // Get() need a device, so this works with Init(root, createWhite=false).
    const std::string path = Resolve(reference, levelHint);
    std::vector<uint8_t> data;
    if (path.empty() || !ReadFile(path, data) || data.empty()) return false;
    bimg::ImageContainer* image =
        bimg::imageParse(&g_allocator, data.data(), static_cast<uint32_t>(data.size()));
    if (!image) return false;
    w = int(image->m_width);
    h = int(image->m_height);
    sizes_[reference] = {w, h};
    bimg::imageFree(image);
    return true;
}

std::string TextureCache::Resolve(const std::string& reference,
                                  const std::string& levelHint) const {
    std::string key = StripExtension(Lower(reference));
    // Normalise Windows separators. The backslash is written by code point so
    // this line carries no escape sequence.
    const char kBackslash = static_cast<char>(92);
    std::replace(key.begin(), key.end(), kBackslash, '/');

    if (!levelHint.empty()) {
        auto it = index_.find("levels/" + Lower(levelHint) + "/" + key);
        if (it != index_.end()) return it->second;
    }
    auto it = index_.find(key);
    if (it != index_.end()) return it->second;

    size_t slash = key.find_last_of('/');
    if (slash != std::string::npos) {
        auto b = index_.find(key.substr(slash + 1));
        if (b != index_.end()) return b->second;
    }
    return {};
}

bgfx::TextureHandle TextureCache::Get(const std::string& reference,
                                      const std::string& levelHint) {
    if (reference.empty()) return white_;

    std::string cacheKey = Lower(reference) + "|" + Lower(levelHint);
    auto cached = cache_.find(cacheKey);
    if (cached != cache_.end()) return cached->second;

    bgfx::TextureHandle handle = white_;
    std::string path = Resolve(reference, levelHint);
    std::vector<uint8_t> data;
    if (!path.empty() && ReadFile(path, data) && !data.empty()) {
        // bimg understands DDS (including the BC formats the game ships), so the
        // compressed blocks go straight to the GPU with no CPU-side decode.
        bimg::ImageContainer* image =
            bimg::imageParse(&g_allocator, data.data(), static_cast<uint32_t>(data.size()));
        if (image) {
            const bgfx::Memory* mem = bgfx::copy(image->m_data, image->m_size);
            handle = bgfx::createTexture2D(
                uint16_t(image->m_width), uint16_t(image->m_height),
                image->m_numMips > 1, image->m_numLayers,
                bgfx::TextureFormat::Enum(image->m_format),
                BGFX_SAMPLER_NONE, mem);
            sizes_[reference] = {int(image->m_width), int(image->m_height)};
            bimg::imageFree(image);
            if (bgfx::isValid(handle)) ++loaded_;
            else handle = white_;
        }
    }
    if (handle.idx == white_.idx) {
        ++missing_;
        LogWarn("texture fell back to white: %s%s", reference.c_str(),
                path.empty() ? "  (unresolved)" : ("  (decode failed: " + path + ")").c_str());
    }
    cache_[cacheKey] = handle;
    return handle;
}

bgfx::TextureHandle TextureCache::GetCube(const std::string& reference,
                                          const std::string& levelHint) {
    if (reference.empty()) return whiteCube_;

    const std::string cacheKey = "cube|" + Lower(reference) + "|" + Lower(levelHint);
    auto cached = cache_.find(cacheKey);
    if (cached != cache_.end()) return cached->second;

    bgfx::TextureHandle handle = whiteCube_;
    const std::string path = Resolve(reference, levelHint);
    std::vector<uint8_t> data;
    if (!path.empty() && ReadFile(path, data) && !data.empty()) {
        bimg::ImageContainer* image =
            bimg::imageParse(&g_allocator, data.data(), static_cast<uint32_t>(data.size()));
        if (image) {
            if (image->m_cubeMap) {
                const bgfx::Memory* mem = bgfx::copy(image->m_data, image->m_size);
                handle = bgfx::createTextureCube(
                    uint16_t(image->m_width), image->m_numMips > 1, image->m_numLayers,
                    bgfx::TextureFormat::Enum(image->m_format), BGFX_SAMPLER_NONE, mem);
            } else {
                LogWarn("not a cube map: %s", path.c_str());
            }
            bimg::imageFree(image);
            if (bgfx::isValid(handle) && handle.idx != whiteCube_.idx) ++loaded_;
            else handle = whiteCube_;
        }
    }
    if (handle.idx == whiteCube_.idx) {
        ++missing_;
        LogWarn("cube map fell back to white: %s%s", reference.c_str(),
                path.empty() ? "  (unresolved)" : "  (decode failed)");
    }
    cache_[cacheKey] = handle;
    return handle;
}

} // namespace painful
