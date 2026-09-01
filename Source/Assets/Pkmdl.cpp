#include <cctype>
#include <cstring>
#include "Pkmdl.h"
#include "Skeleton.h"
#include <algorithm>
#include <cmath>

namespace painful {
// Case-insensitive substring, the same test MapObject::nameHas applies to world
// object names. The model exporter uses the same convention.
bool ModelMesh::nameHas(const char* token) const {
    if (token == nullptr || *token == '\0') return false;
    const size_t n = std::strlen(token);
    if (name.size() < n) return false;
    for (size_t i = 0; i + n <= name.size(); ++i) {
        size_t k = 0;
        while (k < n && std::tolower(uint8_t(name[i + k])) == std::tolower(uint8_t(token[k]))) ++k;
        if (k == n) return true;
    }
    return false;
}


static constexpr uint32_t kMaxName = 1024;   // bone names can be Maya DAG paths

static bool ReadStr(const Reader& r, size_t& p, uint32_t maxLen, std::string& out) {
    size_t n = r.size();
    if (p + 4 > n) return false;
    uint32_t len = r.peekU32(p);
    if (len < 1 || len > maxLen || p + 4 + len > n) return false;
    const uint8_t* d = r.raw();
    for (uint32_t i = 0; i + 1 < len; ++i) {
        uint8_t b = d[p + 4 + i];
        if (b < 32 || b > 126) return false;
    }
    if (d[p + 4 + len - 1] != 0) return false;
    out.assign(reinterpret_cast<const char*>(d + p + 4), len - 1);
    p += 4 + len;
    return true;
}

static bool PeekStr(const Reader& r, size_t p, uint32_t maxLen, std::string& out) {
    size_t q = p;
    return ReadStr(r, q, maxLen, out);
}

static bool VertexOk(const Reader& r, size_t o) {
    double nx = r.peekF32(o + 12), ny = r.peekF32(o + 16), nz = r.peekF32(o + 20);
    double m = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (m != 0.0 && (m < 0.9 || m > 1.1)) return false;
    float u = r.peekF32(o + 24), v = r.peekF32(o + 28);
    if (std::isnan(u) || std::isnan(v) || std::fabs(u) > 512 || std::fabs(v) > 512) return false;
    for (int k = 0; k < 3; ++k) {
        float pc = r.peekF32(o + k * 4);
        if (std::isnan(pc) || std::fabs(pc) > 1e6f) return false;
    }
    return true;
}

// A vertex block is [u32 0][u32 vertexCount][vertexCount * 32 bytes]. Anchoring
// here (rather than on the index header, whose leading fields vary between
// models) is what makes the scan work across the whole shipped set.
static bool VertexBlockAt(const Reader& r, size_t p, uint32_t& vcount, size_t& vertsAt) {
    size_t n = r.size();
    if (p + 8 > n) return false;
    if (r.peekU32(p) != 0) return false;
    vcount = r.peekU32(p + 4);
    if (vcount < 3 || vcount > 300000) return false;
    vertsAt = p + 8;
    if (vertsAt + static_cast<size_t>(vcount) * 32 > n) return false;
    uint32_t check = std::min<uint32_t>(vcount, 96);
    for (uint32_t i = 0; i < check; ++i)
        if (!VertexOk(r, vertsAt + static_cast<size_t>(i) * 32)) return false;
    return true;
}

// Indices sit immediately before the vertex block, preceded by their count.
// Prefer a match that also carries the triangle count and leading zero; fall back
// to matching the index count alone, since not every model lays those out the
// same way (requiring the full header everywhere rejects many valid meshes).
static bool FindIndicesTier(const Reader& r, size_t blockStart, uint32_t vcount,
                            bool strict, size_t& idxAt, uint32_t& icount) {
    if (blockStart < 12) return false;
    size_t maxN = std::min<size_t>((blockStart - 12) / 2, 400000);
    for (size_t n = 3; n <= maxN; n += 3) {
        size_t x = blockStart - 2 * n;
        if (x < 12) break;
        if (r.peekU32(x - 4) != static_cast<uint32_t>(n)) continue;
        if (strict) {
            if (r.peekU32(x - 8) != static_cast<uint32_t>(n / 3)) continue;
            if (r.peekU32(x - 12) != 0) continue;
        }
        bool ok = true;
        for (size_t i = 0; i < n; ++i)
            if (r.peekU16(x + i * 2) >= vcount) { ok = false; break; }
        if (!ok) continue;
        idxAt = x;
        icount = static_cast<uint32_t>(n);
        return true;
    }
    return false;
}

static bool FindIndices(const Reader& r, size_t blockStart, uint32_t vcount,
                        size_t& idxAt, uint32_t& icount) {
    return FindIndicesTier(r, blockStart, vcount, true, idxAt, icount) ||
           FindIndicesTier(r, blockStart, vcount, false, idxAt, icount);
}

bool Model::Load(const std::string& path, Model& out) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) return false;
    out.size = data.size();
    if (data.size() < 16) return false;
    Reader r(data.data(), data.size());

    size_t p = 0;
    out.version = r.peekU32(p); p += 4;
    ReadStr(r, p, 256, out.name);
    // Some models omit the source path; read opportunistically and never abort -
    // the geometry scan does not depend on the header being exactly as expected.
    size_t save = p;
    std::string a;
    if (ReadStr(r, p, 512, a)) {
        if (a == "AnimatedMesh") { out.className = a; out.sourcePath.clear(); }
        else {
            out.sourcePath = a;
            std::string cn;
            if (ReadStr(r, p, 128, cn)) out.className = cn;
            else { p = save; out.className = "(unknown)"; }
        }
    }

    // Bone table: a count followed by that many [name][64-byte matrix][u8].
    size_t scan = p, boneCountAt = static_cast<size_t>(-1);
    uint32_t boneCount = 0;
    size_t limit = std::min(data.size() - 8, p + 65536);
    while (scan < limit) {
        uint32_t c = r.peekU32(scan);
        if (c >= 1 && c <= 512) {
            size_t q = scan + 4;
            uint32_t ok = 0;
            for (uint32_t i = 0; i < c; ++i) {
                std::string bn;
                if (!PeekStr(r, q, kMaxName, bn)) break;
                size_t adv = 4 + bn.size() + 1 + 64 + 1;
                if (q + adv > data.size()) break;
                q += adv; ++ok;
            }
            if (ok == c && c > 0) { boneCountAt = scan; boneCount = c; break; }
        }
        ++scan;
    }
    if (boneCountAt != static_cast<size_t>(-1)) {
        size_t q = boneCountAt + 4;
        for (uint32_t i = 0; i < boneCount; ++i) {
            Bone b;
            ReadStr(r, q, kMaxName, b.name);
            r.seek(q);
            r.readMat4(b.bind);
            q = r.pos();
            b.childCount = data[q]; q += 1;
            out.bones.push_back(std::move(b));
        }
        p = q;
    }
    BuildHierarchy(out.bones);

    // Pass 1: pure byte scan for geometry blocks. Never jump on string matches
    // here - a spurious length-prefixed string can leap over a vertex block.
    struct Found { size_t blockStart, vertsAt, idxAt; uint32_t vc, ic; };
    std::vector<Found> found;
    size_t cur = p;
    while (cur + 8 < data.size()) {
        uint32_t vc = 0, ic = 0;
        size_t vertsAt = 0, idxAt = 0;
        if (VertexBlockAt(r, cur, vc, vertsAt) && FindIndices(r, cur, vc, idxAt, ic)) {
            found.push_back({cur, vertsAt, idxAt, vc, ic});
            cur = vertsAt + static_cast<size_t>(vc) * 32;   // past vertex data only
            continue;
        }
        ++cur;
    }

    // Pass 2: build meshes, taking names/textures from the preceding gap.
    size_t prevEnd = p;
    for (const Found& g : found) {
        std::vector<std::string> pending;
        size_t s = prevEnd;
        while (s < g.blockStart) {
            std::string str;
            if (PeekStr(r, s, 256, str)) { s += 4 + str.size() + 1; pending.push_back(str); }
            else ++s;
        }

        ModelMesh mesh;
        mesh.offset = g.blockStart;
        mesh.indexOffset = g.idxAt;

        // Material header:
        //   u32/char meshName
        //   u32 x3                     (lead; usually 12 bytes, all zero)
        //   u32 materialCount
        //   material: u32/char textureName, u32 firstIndex, u32 triangleCount
        //
        // Those last two used to be treated as an unknown separator and
        // skipped, which is why every mesh drew with materials[0]. They are the
        // triangle RUN each slot covers - see ModelMaterial - and they let the
        // header be checked rather than guessed: the runs must tile the index
        // array exactly, in order, and end on the geometry header.
        {
            static const int kLeads[] = {12, 8, 16, 4, 0};
            // The header ends four bytes before the index data, on a u32
            // holding the index count - so a candidate parse can be checked
            // against the geometry rather than trusted.
            size_t headerEnd = g.idxAt - 4;
            if (g.idxAt < 4 || r.peekU32(headerEnd) != g.ic) headerEnd = 0;
            bool done = false;
            // Search back from the index data rather than forward from the
            // previous mesh: the header sits immediately before its own
            // geometry, and the first mesh in a file has no previous mesh to
            // start from - drzwi3.pkmdl put its first header behind the bone
            // table, where a forward scan never reached it.
            const size_t scanFrom = headerEnd > 8192 ? headerEnd - 8192 : 0;
            for (size_t off = scanFrom; !done && headerEnd && off + 20 < headerEnd; ++off) {
                size_t afterName = off;
                std::string nm;
                if (!ReadStr(r, afterName, 256, nm) || nm.empty()) continue;
                for (int lead : kLeads) {
                    size_t q0 = afterName + lead;
                    if (q0 + 4 > data.size()) continue;
                    const uint32_t matCount = r.peekU32(q0);
                    if (matCount == 0 || matCount > 64) continue;
                    size_t q = q0 + 4;
                    std::vector<ModelMaterial> mats;
                    bool ok = true;
                    uint32_t expect = 0;         // where the next run must start
                    for (uint32_t mi = 0; mi < matCount && ok; ++mi) {
                        ModelMaterial m;
                        if (!ReadStr(r, q, 256, m.texture)) { ok = false; break; }
                        if (q + 8 > data.size()) { ok = false; break; }
                        m.firstIndex = r.peekU32(q);
                        m.triangles = r.peekU32(q + 4);
                        q += 8;
                        // Contiguous, in order, and inside the mesh.
                        // A zero-triangle slot is legal: klatka.pkmdl ends its
                        // list with one named "Models/" covering nothing.
                        if (m.firstIndex != expect ||
                            m.firstIndex + m.triangles * 3 > g.ic) { ok = false; break; }
                        expect = m.firstIndex + m.triangles * 3;
                        mats.push_back(std::move(m));
                    }
                    // The runs must cover the whole index array and the header
                    // must land exactly on the geometry.
                    if (!ok || expect != g.ic || q != headerEnd) continue;
                    mesh.name = nm;
                    mesh.materials = std::move(mats);
                    mesh.materialsExact = true;
                    done = true;
                    break;
                }
            }
        }
        // Fall back to the last non-path string when the material header did
        // not yield a mesh name - or yielded a PATH instead of one.
        //
        // The header's leading string is usually the shape name, but not
        // always: every one of pkw.pkmdl's 13 meshes comes through it as
        // "Models/PKW_PB.tga", the texture. A mesh name is never a path - the
        // format's own convention is the Maya shape name, which is what carries
        // the material variant ("polySurfa_2sided") and what the scripts
        // address meshes by (MDL.SetMeshVisibility hides "polySurfaceShape28"
        // on exactly this model). Taking the texture left all 13 sharing one
        // name, so per-mesh material overrides and visibility could not tell
        // them apart.
        const bool namedLikeAPath = mesh.name.find('/') != std::string::npos ||
                                    mesh.name.find('.') != std::string::npos;
        if (mesh.name.empty() || namedLikeAPath) {
            for (size_t i = pending.size(); i-- > 0;) {
                const std::string& str = pending[i];
                if (str.find('.') == std::string::npos && str.find('/') == std::string::npos) {
                    mesh.name = str; break;
                }
            }
        }
        for (const std::string& str : pending) {
            if (str.size() > 4) {
                std::string ext = str.substr(str.size() - 4);
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".tga" || ext == ".dds") mesh.textures.push_back(str);
            }
        }


        // A mesh whose header did not parse still has to draw. One slot over
        // the whole index array, textured with whatever image name was lying
        // near it, is what this did before the runs were understood - kept as
        // the fallback rather than the rule.
        if (mesh.materials.empty() && !mesh.textures.empty()) {
            ModelMaterial m;
            m.texture = mesh.textures.front();
            m.firstIndex = 0;
            m.triangles = g.ic / 3;
            mesh.materials.push_back(std::move(m));
        }
        mesh.indices.resize(g.ic);
        for (uint32_t i = 0; i < g.ic; ++i) mesh.indices[i] = r.peekU16(g.idxAt + i * 2);
        mesh.verts.resize(static_cast<size_t>(g.vc) * 8);
        for (uint32_t i = 0; i < g.vc; ++i)
            for (int k = 0; k < 8; ++k)
                mesh.verts[i * 8 + k] = r.peekF32(g.vertsAt + static_cast<size_t>(i) * 32 + k * 4);

        // Skin block: [u32 0][u32 0][u32 vertexCount] then, per vertex,
        // [u32 influenceCount][influence: u16 boneIndex, f32 weight]*.
        // It only looks like a fixed 10-byte record on rigidly bound models
        // where influenceCount == 1.
        size_t after = g.vertsAt + static_cast<size_t>(g.vc) * 32;
        if (after + 12 <= data.size() && r.peekU32(after) == 0 &&
            r.peekU32(after + 4) == 0 && r.peekU32(after + 8) == g.vc) {
            size_t q = after + 12;
            bool good = true;
            std::vector<std::vector<SkinInfluence>> skin;
            skin.reserve(g.vc);
            for (uint32_t i = 0; i < g.vc && good; ++i) {
                if (q + 4 > data.size()) { good = false; break; }
                uint32_t n = r.peekU32(q); q += 4;
                if (n == 0 || n > 8 || q + static_cast<size_t>(n) * 6 > data.size()) { good = false; break; }
                std::vector<SkinInfluence> list(n);
                for (uint32_t k = 0; k < n; ++k) {
                    list[k].bone = r.peekU16(q);
                    list[k].weight = r.peekF32(q + 2);
                    q += 6;
                }
                skin.push_back(std::move(list));
            }
            if (good && skin.size() == g.vc) mesh.skin = std::move(skin);
        }

        out.meshes.push_back(std::move(mesh));
        prevEnd = after;
    }
    return true;
}

} // namespace painful
