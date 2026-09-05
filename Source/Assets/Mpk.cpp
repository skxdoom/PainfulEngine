#include "Mpk.h"
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace painful {

static bool ContainsNoCase(const std::string& hay, const char* needle) {
    std::string a = hay, b = needle;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a.find(b) != std::string::npos;
}

bool MapObject::nameHas(const char* token) const { return ContainsNoCase(name, token); }

bool MapObject::isActiveMesh() const {
    // SetupFlags: "phys" sets the bit AddMesh branches on.
    return nameHas("phys");
}

std::string MapObject::piecePrefix() const {
    // strstr, so case-sensitive like the original: "statdest_x_actgrp02_shape"
    // -> "physdest_x_actgrp02_", and the pieces are the "physdest" objects
    // whose names start with that.
    const size_t at = name.find("statdest");
    if (at == std::string::npos) return {};
    std::string s = "phys" + name.substr(at + 4);       // "stat" -> "phys", "dest" stays
    size_t shape = s.find("Shape");
    if (shape == std::string::npos) shape = s.find("shape");
    if (shape != std::string::npos) s.erase(shape, 5);
    return s;
}

int MapObject::activeGroup() const {
    // World::LoadMeshPakFile: sscanf(strstr(name, "actgrp"), "actgrp%d", &g).
    const size_t at = name.find("actgrp");
    if (at == std::string::npos) return -1;
    int g = -1;
    if (std::sscanf(name.c_str() + at, "actgrp%d", &g) != 1) return -1;
    return g;
}

bool MapObject::isCollidable() const {
    // Portals, antiportals, zones and volumetrics are non-solid helper
    // volumes; "noclip" sets flag 0x400000 (SetupFlags 0x101D7050) and
    // ReloadWorld (0x1019B180) skips AddMesh for it. Physics.md, "noclip".
    return !nameHas("portal") && !nameHas("antyp") && !nameHas("vollight") &&
           !nameHas("volfog") && !nameHas("zone") && !nameHas("noclip");
}

static bool HeaderAt(const Reader& r, size_t p) {
    size_t n = r.size();
    if (p + 4 > n) return false;
    uint32_t nameLen = r.peekU32(p);
    if (nameLen < 2 || nameLen > 128) return false;
    if (p + 4 + nameLen + 72 > n) return false;
    const uint8_t* d = r.raw();
    for (uint32_t i = 0; i < nameLen - 1; ++i) {
        uint8_t b = d[p + 4 + i];
        if (b < 32 || b > 126) return false;
    }
    if (d[p + 4 + nameLen - 1] != 0) return false;
    size_t q = p + 4 + nameLen + 64;
    uint32_t uv = r.peekU32(q), vc = r.peekU32(q + 4);
    if (uv != 1 && uv != 2) return false;
    if (vc == 0 || vc > 500000) return false;
    if (q + 8 + static_cast<size_t>(vc) * 32 + 4 > n) return false;
    return true;
}

bool MapMesh::Load(const std::string& path, MapMesh& out) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) { out.error = "cannot read file"; return false; }
    out.size = data.size();
    Reader r(data.data(), data.size());
    if (data.size() < 8 || r.peekU32(0) != kMagic) { out.error = "bad magic"; return false; }

    size_t p = 4;
    while (p < data.size()) {
        if (p + 4 <= data.size() && r.peekU32(p) == kTerminator) { out.terminated = true; out.parseEnd = p; break; }
        if (!HeaderAt(r, p)) {
            size_t q = p + 1;
            bool found = false;
            while (q + 4 <= data.size()) {
                if (r.peekU32(q) == kTerminator || HeaderAt(r, q)) { found = true; break; }
                ++q;
            }
            if (!found) { out.error = "no further record"; break; }
            out.skippedBytes += (q - p);
            out.skips.push_back({p, q - p});
            ++out.skippedRegions;
            p = q;
            continue;
        }

        MapObject o;
        r.seek(p);
        uint32_t nameLen = r.u32();
        o.name.assign(reinterpret_cast<const char*>(data.data() + r.pos()), nameLen - 1);
        r.seek(r.pos() + nameLen);
        r.readMat4(o.transform);
        o.uvChannels = r.u32();
        uint32_t vcount = r.u32();
        o.verts.resize(static_cast<size_t>(vcount) * 8);
        for (size_t i = 0; i < o.verts.size(); ++i) o.verts[i] = r.f32();
        uint32_t ncount = r.u32();
        if (ncount > vcount + 1) { out.error = "bad normal count in " + o.name; break; }
        o.normals.resize(static_cast<size_t>(ncount) * 3);
        for (size_t i = 0; i < o.normals.size(); ++i) o.normals[i] = r.f32();
        for (int i = 0; i < 3; ++i) o.bboxMin[i] = r.f32();
        for (int i = 0; i < 3; ++i) o.bboxMax[i] = r.f32();
        uint32_t icount = r.u32();
        if (r.pos() + static_cast<size_t>(icount) * 2 > data.size()) {
            out.error = "index overflow in " + o.name; break;
        }
        o.indices.resize(icount);
        for (uint32_t i = 0; i < icount; ++i) o.indices[i] = r.u16();

        // Material block:
        //   u32 materialCount
        //   material: u16 firstIndex, u16 triangleCount,
        //             slot[4]: u32 nameLen, name, f32 offsetU/V, scaleU/V
        size_t mp = r.pos();
        if (mp + 4 <= data.size()) {
            uint32_t matCount = r.peekU32(mp);
            if (matCount > 0 && matCount <= 256) {
                size_t q = mp + 4;
                std::vector<Material> mats;
                bool ok = true;
                for (uint32_t mi = 0; mi < matCount && ok; ++mi) {
                    if (q + 4 > data.size()) { ok = false; break; }
                    Material mat;
                    mat.firstIndex = r.peekU16(q);
                    mat.triangleCount = r.peekU16(q + 2);
                    q += 4;
                    for (int s = 0; s < 4 && ok; ++s) {
                        if (q + 4 > data.size()) { ok = false; break; }
                        uint32_t len = r.peekU32(q); q += 4;
                        if (len < 1 || len > 256 || q + len + 16 > data.size()) { ok = false; break; }
                        for (uint32_t c = 0; c + 1 < len; ++c) {
                            uint8_t ch = data[q + c];
                            if (ch < 32 || ch > 126) { ok = false; break; }
                        }
                        if (!ok) break;
                        if (data[q + len - 1] != 0) { ok = false; break; }
                        mat.slots[s].name.assign(reinterpret_cast<const char*>(data.data() + q), len - 1);
                        q += len;
                        mat.slots[s].offsetU = r.peekF32(q);
                        mat.slots[s].offsetV = r.peekF32(q + 4);
                        mat.slots[s].scaleU  = r.peekF32(q + 8);
                        mat.slots[s].scaleV  = r.peekF32(q + 12);
                        q += 16;
                    }
                    if (ok) mats.push_back(mat);
                }
                if (ok && mats.size() == matCount) o.materials = std::move(mats);
            }
        }

        out.objects.push_back(std::move(o));
        p = r.pos();
    }
    return out.error.empty();
}

namespace {

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 24));
}

void PutU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
}

void PutF32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    PutU32(out, bits);
}

// Length-prefixed and NUL-terminated, with the length counting the NUL. The
// loader rejects a zero length, so an unused texture slot is written as a
// length of 1 and a single NUL - an empty string, not an absent one.
void PutString(std::vector<uint8_t>& out, const std::string& s) {
    PutU32(out, uint32_t(s.size() + 1));
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}

} // namespace

bool MapMesh::Write(const std::string& path, const MapMesh& mesh) {
    std::vector<uint8_t> out;
    PutU32(out, kMagic);

    for (const MapObject& o : mesh.objects) {
        PutString(out, o.name);
        for (int i = 0; i < 16; ++i) PutF32(out, o.transform.m[i]);
        PutU32(out, o.uvChannels);

        PutU32(out, uint32_t(o.verts.size() / 8));
        for (float f : o.verts) PutF32(out, f);
        PutU32(out, uint32_t(o.normals.size() / 3));
        for (float f : o.normals) PutF32(out, f);

        for (int i = 0; i < 3; ++i) PutF32(out, o.bboxMin[i]);
        for (int i = 0; i < 3; ++i) PutF32(out, o.bboxMax[i]);

        PutU32(out, uint32_t(o.indices.size()));
        for (uint16_t i : o.indices) PutU16(out, i);

        // The loader PEEKS at the material block without moving its cursor and
        // then resynchronises by scanning for the next valid header, so this
        // block is read but not consumed. That is fine as long as it holds no
        // byte sequence that looks like a header - short slot names keep it
        // that way, and a header needs a 2..128 byte printable name followed
        // by 64 bytes of matrix and a uvChannels of 1 or 2.
        PutU32(out, uint32_t(o.materials.size()));
        for (const Material& m : o.materials) {
            PutU16(out, m.firstIndex);
            PutU16(out, m.triangleCount);
            for (int s = 0; s < 4; ++s) {
                PutString(out, m.slots[s].name);
                PutF32(out, m.slots[s].offsetU);
                PutF32(out, m.slots[s].offsetV);
                PutF32(out, m.slots[s].scaleU);
                PutF32(out, m.slots[s].scaleV);
            }
        }
    }

    PutU32(out, kTerminator);
    return WriteFile(path, out);
}

} // namespace painful
