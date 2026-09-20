#include "TextureCache.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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

	root_ = Lower(texturesRoot);
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
		// A miss is cached as the shared white handle; those are destroyed once, below.
		const uint16_t h = kv.second.idx;
		if (h == white_.idx || h == whiteCube_.idx || h == transparent_.idx) continue;
		if (bgfx::isValid(kv.second)) bgfx::destroy(kv.second);
	}
	cache_.clear();
	if (bgfx::isValid(white_)) { bgfx::destroy(white_); white_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(transparent_)) { bgfx::destroy(transparent_); transparent_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(whiteCube_)) { bgfx::destroy(whiteCube_); whiteCube_ = BGFX_INVALID_HANDLE; }
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

// The base-name fallback is the port's own: MaterialSystem::TextureOnDisk (0x10098ef0)
// tries the name, then items/ and models/ and the current level's folder by base
// name, never another level's. anyLevel false keeps to that. Formats.md, "Where the engine looks"
std::string TextureCache::Resolve(const std::string& reference,
		const std::string& levelHint, bool anyLevel) const {
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
	// One map holds both path and base-name keys; the strict lookup takes a path.
	if (it != index_.end() && (anyLevel || StripExtension(Lower(it->second)) == root_ + "/" + key))
		return it->second;

	size_t slash = key.find_last_of('/');
	const std::string base = slash != std::string::npos ? key.substr(slash + 1) : key;
	if (!anyLevel) {
		for (const char* dir : {"items/", "models/"}) {
			const auto b = index_.find(dir + base);
			if (b != index_.end()) return b->second;
		}
		return {};
	}
	if (slash != std::string::npos) {
		auto b = index_.find(base);
		if (b != index_.end()) return b->second;
	}
	return {};
}

namespace {

// The missing levels of a single-level 2D texture, as D3DX builds them for
// D3DXCreateTextureFromFileInMemoryEx at MipLevels D3DX_DEFAULT: a 2x2 box
// average of the stored values, down to 1x1. Decoded to RGBA8 for it.
// Docs/Reference/Formats.md, "Mip levels"
bgfx::TextureHandle CreateWithMips(const bimg::ImageContainer& image) {
	bimg::ImageContainer* rgba =
		bimg::imageConvert(&g_allocator, bimg::TextureFormat::RGBA8, image, false);
	if (!rgba) return BGFX_INVALID_HANDLE;
	uint32_t w = rgba->m_width, h = rgba->m_height;
	uint64_t total = 0;
	for (uint32_t lw = w, lh = h;; lw = std::max(1u, lw / 2), lh = std::max(1u, lh / 2)) {
		total += uint64_t(lw) * lh * 4;
		if (lw == 1 && lh == 1) break;
	}
	const bgfx::Memory* mem = bgfx::alloc(uint32_t(total));
	std::memcpy(mem->data, rgba->m_data, size_t(w) * h * 4);
	uint8_t* src = mem->data;
	while (w > 1 || h > 1) {
		const uint32_t dw = std::max(1u, w / 2), dh = std::max(1u, h / 2);
		uint8_t* dst = src + size_t(w) * h * 4;
		for (uint32_t y = 0; y < dh; ++y) {
			const uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
			for (uint32_t x = 0; x < dw; ++x) {
				const uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
				for (int c = 0; c < 4; ++c) {
					const uint32_t sum = src[(y0 * w + x0) * 4 + c] + src[(y0 * w + x1) * 4 + c] +
							src[(y1 * w + x0) * 4 + c] + src[(y1 * w + x1) * 4 + c];
					dst[(y * dw + x) * 4 + c] = uint8_t((sum + 2) / 4);
				}
			}
		}
		src = dst;
		w = dw;
		h = dh;
	}
	const bgfx::TextureHandle handle = bgfx::createTexture2D(uint16_t(rgba->m_width),
			uint16_t(rgba->m_height), true, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, mem);
	bimg::imageFree(rgba);
	return handle;
}

} // namespace

bgfx::TextureHandle TextureCache::Get(const std::string& reference,
		const std::string& levelHint, bool anyLevel, bool mips) {
	if (reference.empty()) return white_;

	std::string cacheKey = Lower(reference) + "|" + Lower(levelHint) + (anyLevel ? "" : "|level") +
			(mips ? "" : "|nomips");
	auto cached = cache_.find(cacheKey);
	if (cached != cache_.end()) return cached->second;

	bgfx::TextureHandle handle = white_;
	std::string path = Resolve(reference, levelHint, anyLevel);
	std::vector<uint8_t> data;
	if (!path.empty() && ReadFile(path, data) && !data.empty()) {
		// bimg understands DDS (including the BC formats the game ships), so the
		// compressed blocks go straight to the GPU with no CPU-side decode.
		bimg::ImageContainer* image =
			bimg::imageParse(&g_allocator, data.data(), static_cast<uint32_t>(data.size()));
		if (image) {
			if (mips && image->m_numMips <= 1 && image->m_numLayers == 1 && !image->m_cubeMap &&
					image->m_depth <= 1 && (image->m_width > 1 || image->m_height > 1)) {
				handle = CreateWithMips(*image);
				if (bgfx::isValid(handle)) ++mipsBuilt_;
			}
			if (!bgfx::isValid(handle) || handle.idx == white_.idx) {
				const bgfx::Memory* mem = bgfx::copy(image->m_data, image->m_size);
				handle = bgfx::createTexture2D(
						uint16_t(image->m_width), uint16_t(image->m_height),
						image->m_numMips > 1, image->m_numLayers,
						bgfx::TextureFormat::Enum(image->m_format),
						BGFX_SAMPLER_NONE, mem);
			}
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
