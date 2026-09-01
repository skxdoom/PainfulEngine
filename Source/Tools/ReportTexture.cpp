// Texture references, and where each one actually resolves to.
#include "Commands.h"

int SkyDumpCmd(const char* path) {
    MapMesh m;
    MapMesh::Load(path, m);
    LogInfo("%s: %zu objects", path, m.objects.size());
    for (size_t i = 0; i < m.objects.size(); ++i) {
        const MapObject& o = m.objects[i];
        LogInfo("  [%zu] name=%-28s uv=%u verts=%-5zu tris=%-5zu mats=%zu bbox x[%.0f..%.0f] y[%.0f..%.0f] z[%.0f..%.0f]",
                i, o.name.c_str(), o.uvChannels, o.vertexCount(), o.triangleCount(),
                o.materials.size(), o.bboxMin[0], o.bboxMax[0], o.bboxMin[1], o.bboxMax[1],
                o.bboxMin[2], o.bboxMax[2]);
        // Float 3 of the 2-UV layout is unaccounted for; if it is a vertex
        // alpha the dome shells would use it to feather their rims.
        if (o.uvChannels == 2 && o.vertexCount() > 0) {
            float lo3 = 1e30f, hi3 = -1e30f;
            for (size_t vi = 0; vi < o.vertexCount(); ++vi) {
                const float f3 = o.verts[vi * 8 + 3];
                lo3 = std::min(lo3, f3);
                hi3 = std::max(hi3, f3);
            }
            LogInfo("        float3 range [%g .. %g]", lo3, hi3);
            float u0l = 1e30f, u0h = -1e30f, v0l = 1e30f, v0h = -1e30f;
            float u1l = 1e30f, u1h = -1e30f, v1l = 1e30f, v1h = -1e30f;
            for (size_t vi = 0; vi < o.vertexCount(); ++vi) {
                float a[2], b[2];
                o.uv(vi, a);
                o.uv1(vi, b);
                u0l = std::min(u0l, a[0]); u0h = std::max(u0h, a[0]);
                v0l = std::min(v0l, a[1]); v0h = std::max(v0h, a[1]);
                u1l = std::min(u1l, b[0]); u1h = std::max(u1h, b[0]);
                v1l = std::min(v1l, b[1]); v1h = std::max(v1h, b[1]);
            }
            LogInfo("        uv0 u[%.2f..%.2f] v[%.2f..%.2f]  uv1 u[%.2f..%.2f] v[%.2f..%.2f]",
                    u0l, u0h, v0l, v0h, u1l, u1h, v1l, v1h);
            // Correlate elevation with UV1 radius, to see which end of the
            // mask gradient lands on the shell apex.
            float apexY = -1e30f, rimY = 1e30f;
            size_t apexI = 0, rimI = 0;
            for (size_t vi = 0; vi < o.vertexCount(); ++vi) {
                const float y = o.verts[vi * 8 + 1];
                if (y > apexY) { apexY = y; apexI = vi; }
                if (y < rimY) { rimY = y; rimI = vi; }
            }
            float ua[2], ur[2];
            o.uv1(apexI, ua);
            o.uv1(rimI, ur);
            LogInfo("        apex y=%.1f uv1=(%.2f,%.2f) r=%.2f | rim y=%.1f uv1=(%.2f,%.2f) r=%.2f",
                    apexY, ua[0], ua[1],
                    std::sqrt((ua[0]-0.5f)*(ua[0]-0.5f) + (ua[1]-0.5f)*(ua[1]-0.5f)),
                    rimY, ur[0], ur[1],
                    std::sqrt((ur[0]-0.5f)*(ur[0]-0.5f) + (ur[1]-0.5f)*(ur[1]-0.5f)));
        }
        for (const Material& mat : o.materials) {
            LogInfo("        slot0=%-26s slot1=%-26s slot2=%-16s slot3=%s",
                    mat.slots[0].name.c_str(), mat.slots[1].name.c_str(),
                    mat.slots[2].name.c_str(), mat.slots[3].name.c_str());
            for (int s = 0; s < 4; ++s) {
                const TextureSlot& t = mat.slots[s];
                if (t.empty()) continue;
                LogInfo("          xform[%d] offset(%.3f %.3f) scale(%.3f %.3f)  %s",
                        s, t.offsetU, t.offsetV, t.scaleU, t.scaleV, t.name.c_str());
            }
        }
    }
    return 0;
}

// Diagnostic: bring up the physics world headlessly and probe it.
//
// Numbers, not a screenshot: what the level put into Jolt, and where a sphere
// pushed along each axis from the spawn actually ends up. A camera that
// collides is a camera whose sphere stops short of what it asked for.
// Drop a ragdoll into a real level and see whether it behaves like a body.
//
// The failure modes this exists to catch are not subtle but they are invisible
// without a number: constraints built in the wrong frame make the parts fly
// apart, a missed unit conversion makes the ragdoll ten times too big, and a
// degenerate hull or basis makes everything NaN. So it measures the SPREAD of
// the parts against the pose the .hke was authored in - a ragdoll that holds
// together keeps roughly its own dimensions however it lands - and it reports
// how far the whole thing travelled and whether it came to rest.

int SkyTexCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);

    const LevelInfo& info = level.info();
    int missing = 0, present = 0;
    LogInfo("%-22s dome=%s", level.name().c_str(), info.skyDomeMap.c_str());
    for (int i = 0; i < 4; ++i) {
        const SkyLayer& L = info.skyLayers[i];
        if (!L.valid()) continue;
        const std::string names[4] = {L.tex1.name, L.tex2.name, L.mask, L.lightmap};
        const char* kind[4] = {"tex1", "tex2", "mask", "lmap"};
        for (int t = 0; t < 4; ++t) {
            if (names[t].empty()) continue;
            const bool ok = !cache.Resolve(names[t], "").empty();
            if (ok) ++present; else ++missing;
            if (!ok) LogInfo("    layer%d %s MISSING: %s", i + 1, kind[t], names[t].c_str());
        }
    }
    LogInfo("    textures resolved %d, missing %d", present, missing);
    return missing == 0 ? 0 : 3;
}

// Diagnostic: what do the placeholder texture names resolve to?
// Diagnostic: decode any game texture to BGRA and report its border/centre
// values, or write it out as a TGA when an output path is given.
int TexDumpCmd(const char* dataRoot, const char* name, const char* outPath) {
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);
    const std::string path = cache.Resolve(name, "");
    if (path.empty()) { LogInfo("unresolved: %s", name); return 2; }

    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) { LogInfo("unreadable: %s", path.c_str()); return 2; }
    bimg::ImageContainer* image =
        bimg::imageParse(&painful::g_allocator, data.data(), uint32_t(data.size()));
    if (!image) { LogInfo("parse failed"); return 2; }

    const uint32_t w = image->m_width, h = image->m_height;
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    bimg::imageDecodeToBgra8(&painful::g_allocator, rgba.data(), image->m_data, w, h, w * 4,
                             image->m_format);
    auto px = [&](uint32_t x, uint32_t y) {
        const uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
        LogInfo("  (%4u,%4u)  B %3u  G %3u  R %3u  A %3u", x, y, p[0], p[1], p[2], p[3]);
    };
    LogInfo("%s  %ux%u fmt %d", path.c_str(), w, h, int(image->m_format));
    px(0, 0); px(w / 2, 0); px(w - 1, 0);
    px(0, h / 2); px(w / 2, h / 2); px(w - 1, h / 2);
    px(0, h - 1); px(w / 2, h - 1); px(w - 1, h - 1);

    if (outPath && outPath[0]) {
        // Minimal uncompressed BGRA TGA.
        std::vector<uint8_t> tga(18, 0);
        tga[2] = 2; tga[12] = uint8_t(w); tga[13] = uint8_t(w >> 8);
        tga[14] = uint8_t(h); tga[15] = uint8_t(h >> 8); tga[16] = 32; tga[17] = 0x28;
        tga.insert(tga.end(), rgba.begin(), rgba.end());
        FILE* f = fopen(outPath, "wb");
        if (f) { fwrite(tga.data(), 1, tga.size(), f); fclose(f); LogInfo("wrote %s", outPath); }
    }
    bimg::imageFree(image);
    return 0;
}

int ResolveCmd(const char* dataRoot, const char* name) {
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);
    const std::string hit = cache.Resolve(name, "");
    LogInfo("  %-16s -> %s", name, hit.empty() ? "(UNRESOLVED)" : hit.c_str());
    return 0;
}

// Writes a complete, loadable level from nothing but numbers.
//
// This is the smallest thing that proves a level is authorable from code. A
// level is almost entirely plain Lua text - the settings file, the class
// script and every placed light, item and spawn point - so the only binary in
// the way was the world mesh, and MapMesh::Write closes that.
//
// The floor is a grid rather than two big triangles: physics does not care,
// but a single quad gives the renderer nothing to interpolate across and the
// result reads as a flat card rather than a surface.

int TexturesCmd(const char* mapPath, const char* dataRoot, const char* hint) {
    MapMesh m;
    MapMesh::Load(mapPath, m);
    TextureCache cache;
    cache.Init(std::string(dataRoot) + "/Textures", false);

    std::map<std::string, std::string> seen;   // reference -> resolved path
    for (const MapObject& o : m.objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (const Material& mat : o.materials) {
            const std::string& d = mat.slots[0].name;
            if (!d.empty() && !seen.count(d)) seen[d] = cache.Resolve(d, hint);
        }
    }
    size_t unresolved = 0, wrongLevel = 0, looksLightmap = 0;
    std::string hintDir = std::string("levels\\") + hint;
    for (const auto& kv : seen) {
        std::string lower = kv.second;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool bad = false;
        if (kv.second.empty()) { ++unresolved; bad = true; }
        else if (lower.find("_l_") != std::string::npos) { ++looksLightmap; bad = true; }
        else if (lower.find("levels/") != std::string::npos &&
                 lower.find(hintDir) == std::string::npos) { ++wrongLevel; bad = true; }
        if (bad) LogInfo("  %-28s -> %s", kv.first.c_str(),
                         kv.second.empty() ? "(UNRESOLVED)" : kv.second.c_str());
    }
    LogInfo("");
    LogInfo("distinct diffuse refs %zu | unresolved %zu | resolved to a LIGHTMAP %zu | resolved to another level %zu",
            seen.size(), unresolved, looksLightmap, wrongLevel);
    return 0;
}

// Walks the four-file chain a placed CParticleFX resolves through and prints
// what each step produced, so the data path can be checked without a window:
//   instance -> template o.Effect -> Effects/<name>.pfx -> Emitters/<file>.ini
