// Decals: the .ini definitions, Decal::Spawn's projection and clip, and
// Decal::Tick's clock. Recovered from Engine.dll; the evidence and the
// addresses are in Docs/Reference/Decals.md.

#include "Decals.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace painful {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// The [General] keys of one .ini, names folded like ConfigFile::GetString.
std::map<std::string, std::string> ParseIni(const std::string& text) {
    std::map<std::string, std::string> out;
    std::string section;
    size_t i = 0;
    while (i < text.size()) {
        const size_t end = text.find('\n', i);
        const std::string line =
            Trim(text.substr(i, end == std::string::npos ? std::string::npos : end - i));
        i = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close != std::string::npos) section = Lower(Trim(line.substr(1, close - 1)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[section + '.' + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
    }
    return out;
}

// The material-script blend names (FUN_100973a0), -1 for an unknown one.
int BlendModeByName(const std::string& name) {
    static const char* const kNames[] = {"none", "alpha", "add", "modulate", "filter",
                                         "translucent", "invmodulate", "subtract",
                                         "revsubtract", "desttranslucent", "destalpha",
                                         "modulate2x"};
    const std::string n = Lower(name);
    for (int i = 0; i < int(sizeof(kNames) / sizeof(kNames[0])); ++i)
        if (n == kNames[i]) return i;
    return -1;
}

float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
float Normalize(float v[3]) {
    const float len = std::sqrt(Dot(v, v));
    if (len > 1e-12f) for (int c = 0; c < 3; ++c) v[c] /= len;
    return len;
}

// Inverse of the 3x3 row basis, so local = (p - T) * inv.
bool Invert3(const float b[9], float inv[9]) {
    const float det = b[0] * (b[4] * b[8] - b[5] * b[7]) - b[1] * (b[3] * b[8] - b[5] * b[6]) +
                      b[2] * (b[3] * b[7] - b[4] * b[6]);
    if (std::fabs(det) < 1e-18f) return false;
    const float k = 1.f / det;
    inv[0] = (b[4] * b[8] - b[5] * b[7]) * k;
    inv[1] = (b[2] * b[7] - b[1] * b[8]) * k;
    inv[2] = (b[1] * b[5] - b[2] * b[4]) * k;
    inv[3] = (b[5] * b[6] - b[3] * b[8]) * k;
    inv[4] = (b[0] * b[8] - b[2] * b[6]) * k;
    inv[5] = (b[2] * b[3] - b[0] * b[5]) * k;
    inv[6] = (b[3] * b[7] - b[4] * b[6]) * k;
    inv[7] = (b[1] * b[6] - b[0] * b[7]) * k;
    inv[8] = (b[0] * b[4] - b[1] * b[3]) * k;
    return true;
}

struct LocalVert {
    float l[3];   // box space: the box is [-0.5, 0.5] on each axis
};

// One half-space clip of a convex polygon, Sutherland-Hodgman with the
// original's 0.001 tolerance (ConvexPolygon::Split).
void ClipAxis(std::vector<LocalVert>& poly, std::vector<LocalVert>& tmp, int axis, float sign) {
    tmp.clear();
    const size_t n = poly.size();
    if (n == 0) return;
    auto dist = [&](const LocalVert& v) { return 0.5f - sign * v.l[axis]; };   // >= 0 inside
    for (size_t i = 0; i < n; ++i) {
        const LocalVert& a = poly[i];
        const LocalVert& b = poly[(i + 1) % n];
        const float da = dist(a), db = dist(b);
        const bool ina = da >= -0.001f, inb = db >= -0.001f;
        if (ina) tmp.push_back(a);
        if (ina != inb) {
            const float t = da / (da - db);
            LocalVert m;
            for (int c = 0; c < 3; ++c) m.l[c] = a.l[c] + (b.l[c] - a.l[c]) * t;
            tmp.push_back(m);
        }
    }
    poly.swap(tmp);
}

} // namespace

// ---------------------------------------------------------------- library

const DecalDef& DecalLibrary::Get(const std::string& name) {
    const std::string key = Lower(name);
    auto it = defs_.find(key);
    if (it != defs_.end()) return it->second;

    DecalDef def;
    def.name = name;
    std::vector<uint8_t> bytes;
    const std::string path = dir_ + "/" + name + ".ini";
    if (ReadFile(path, bytes) && !bytes.empty()) {
        const auto ini = ParseIni(std::string(bytes.begin(), bytes.end()));
        auto get = [&](const char* k) -> const std::string* {
            auto f = ini.find(std::string("general.") + k);
            return f == ini.end() ? nullptr : &f->second;
        };
        if (const std::string* v = get("texture")) def.texture = *v;
        if (const std::string* v = get("scale")) def.scale = float(std::atof(v->c_str()));
        if (const std::string* v = get("lifetime")) def.lifeTime = float(std::atof(v->c_str()));
        if (const std::string* v = get("decaytime")) {
            def.decayTime = float(std::atof(v->c_str()));
            def.animTime = def.decayTime;
        }
        if (const std::string* v = get("animationtime")) def.animTime = float(std::atof(v->c_str()));
        def.zScale = 0.f;
        if (const std::string* v = get("zscale")) def.zScale = float(std::atof(v->c_str()));
        def.cullBackFaces = -1.f;
        if (const std::string* v = get("cullbackfaces")) def.cullBackFaces = float(std::atof(v->c_str()));
        if (const std::string* v = get("fps")) def.fps = float(std::atof(v->c_str()));
        if (const std::string* v = get("blendmode")) def.blendMode = BlendModeByName(*v);
        if (const std::string* v = get("cuttris")) def.cutTris = std::atol(v->c_str()) != 0;
    } else {
        LogWarn("decals: %s not found, using the defaults", path.c_str());
    }
    // A one-character texture name is the white texture too (the loader
    // tests the length against 1, not 0).
    if (def.texture.size() <= 1) def.texture.clear();
    return defs_.emplace(key, def).first->second;
}

// ---------------------------------------------------------------- system

int DecalSystem::Create(const DecalDef& def, float scale) {
    int slot = -1;
    for (size_t i = 0; i < decals_.size(); ++i)
        if (!decals_[i].alive) { slot = int(i); break; }
    if (slot < 0) {
        decals_.emplace_back();
        slot = int(decals_.size() - 1);
    }
    DecalInstance& d = decals_[size_t(slot)];
    d = DecalInstance();
    d.alive = true;
    d.def = def;
    d.scale = scale;
    d.life = def.lifeTime;
    d.immortal = def.lifeTime < 0.f;
    ++live_;
    return slot;
}

void DecalSystem::SetBasis(int slot, const float pos[3], const float normal[3]) {
    if (!Valid(slot)) return;
    DecalInstance& d = decals_[size_t(slot)];
    float n[3] = {normal[0], normal[1], normal[2]};
    if (Normalize(n) <= 0.f) { n[0] = 0.f; n[1] = 1.f; n[2] = 0.f; }
    for (int c = 0; c < 3; ++c) d.normal[c] = n[c];

    // Z points into the surface; ZScale is its length when given.
    float z[3] = {-n[0], -n[1], -n[2]};
    if (d.def.zScale > 0.f) for (int c = 0; c < 3; ++c) z[c] *= d.def.zScale;

    // Any perpendicular: the world axis least aligned with the normal.
    const float ax = std::fabs(n[0]), ay = std::fabs(n[1]), az = std::fabs(n[2]);
    float p[3] = {0, 0, 0};
    if (ax <= ay && ax <= az) p[0] = 1.f;
    else if (ay <= az) p[1] = 1.f;
    else p[2] = 1.f;
    const float width = d.def.scale * d.scale;
    float x[3], y[3];
    Cross(z, p, x);
    Normalize(x);
    Cross(x, z, y);
    Normalize(y);

    // Decal::Spawn spins a mortal decal by rand() * 2pi / RAND_MAX about its
    // own Z; immortal ones (shadows, statics) keep the frame as built.
    if (!d.immortal) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float angle = float(rng_ >> 8) * (6.2831853f / 16777216.f);
        const float cs = std::cos(angle), sn = std::sin(angle);
        float rx[3], ry[3];
        for (int c = 0; c < 3; ++c) {
            rx[c] = cs * x[c] + sn * y[c];
            ry[c] = -sn * x[c] + cs * y[c];
        }
        std::memcpy(x, rx, sizeof x);
        std::memcpy(y, ry, sizeof y);
    }
    for (int c = 0; c < 3; ++c) {
        d.basis[c] = x[c] * width;
        d.basis[3 + c] = y[c] * width;
        d.basis[6 + c] = z[c];
        d.basis[9 + c] = pos[c];
    }
}

void DecalSystem::SetBasisOriented(int slot, const float pos[3], const float normal[3],
                                   const float up[3], const float right[3]) {
    if (!Valid(slot)) return;
    DecalInstance& d = decals_[size_t(slot)];
    float n[3] = {normal[0], normal[1], normal[2]};
    if (Normalize(n) <= 0.f) { n[0] = 0.f; n[1] = 1.f; n[2] = 0.f; }
    for (int c = 0; c < 3; ++c) d.normal[c] = n[c];
    const float width = d.def.scale * d.scale;
    const float depth = d.def.zScale > 0.f ? d.def.zScale : 1.f;
    for (int c = 0; c < 3; ++c) {
        d.basis[c] = up[c] * width;
        d.basis[3 + c] = right[c] * width;
        d.basis[6 + c] = -n[c] * depth;
        d.basis[9 + c] = pos[c];
    }
}

void DecalSystem::ClearGeometry(int slot) {
    if (!Valid(slot)) return;
    decals_[size_t(slot)].verts.clear();
    decals_[size_t(slot)].objects = 0;
}

void DecalSystem::SetTextureOverride(int slot, const std::string& texture) {
    if (Valid(slot)) decals_[size_t(slot)].textureOverride = texture;
}

bool DecalSystem::HasGeometry(int slot) const {
    return Valid(slot) && !decals_[size_t(slot)].verts.empty();
}

void DecalSystem::Box(int slot, float lo[3], float hi[3]) const {
    for (int c = 0; c < 3; ++c) { lo[c] = 1e30f; hi[c] = -1e30f; }
    if (!Valid(slot)) return;
    const DecalInstance& d = decals_[size_t(slot)];
    // An unbounded box (ZScale 0) is searched as deep as it is wide.
    float z[3] = {d.basis[6], d.basis[7], d.basis[8]};
    if (d.def.zScale <= 0.f) {
        const float w = std::sqrt(Dot(d.basis, d.basis));
        for (int c = 0; c < 3; ++c) z[c] *= w;
    }
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
                for (int c = 0; c < 3; ++c) {
                    const float p = d.basis[9 + c] + 0.5f * (sx * d.basis[c] + sy * d.basis[3 + c] + sz * z[c]);
                    lo[c] = std::min(lo[c], p);
                    hi[c] = std::max(hi[c], p);
                }
}

void DecalSystem::Append(int slot, const MapObject& object, const Mat4& objectToWorld) {
    if (!Valid(slot)) return;
    DecalInstance& d = decals_[size_t(slot)];
    if (int(d.verts.size()) + 3 > kMaxVertices) return;

    // Whole-object reject on bounds, so a level-wide search stays cheap.
    float lo[3], hi[3];
    Box(slot, lo, hi);
    float olo[3] = {1e30f, 1e30f, 1e30f}, ohi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 8; ++i) {
        float p[3];
        objectToWorld.TransformPoint((i & 1) ? object.bboxMax[0] : object.bboxMin[0],
                                     (i & 2) ? object.bboxMax[1] : object.bboxMin[1],
                                     (i & 4) ? object.bboxMax[2] : object.bboxMin[2], p);
        for (int c = 0; c < 3; ++c) {
            olo[c] = std::min(olo[c], p[c]);
            ohi[c] = std::max(ohi[c], p[c]);
        }
    }
    for (int c = 0; c < 3; ++c)
        if (olo[c] > hi[c] || ohi[c] < lo[c]) return;

    float inv[9];
    if (!Invert3(d.basis, inv)) return;
    ++d.objects;

    const size_t nv = object.vertexCount();
    for (size_t t = 0; t + 2 < object.indices.size(); t += 3) {
        if (int(d.verts.size()) + 3 > kMaxVertices) break;
        float w[3][3];
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            const uint16_t idx = object.indices[t + size_t(k)];
            if (idx >= nv) { ok = false; break; }
            float p[3];
            object.position(idx, p);
            objectToWorld.TransformPoint(p[0], p[1], p[2], w[k]);
        }
        if (!ok) continue;
        ProjectTriangle(d, w, inv);
    }
}

// One surface triangle into the box: Decal::Spawn(Entity*, Matrix) per
// triangle. Back faces go first, then either the whole triangle when any
// vertex lies inside every half-space (CutTris 0) or the polygon clipped to
// the box and fanned (CutTris 1). UV is the box-space position plus 0.5.
void DecalSystem::ProjectTriangle(DecalInstance& d, const float w[3][3], const float inv[9]) {
    if (d.def.cullBackFaces > -1.f) {
        // The .mpk winds so that (v2-v0) x (v1-v0) is the outward normal.
        float e1[3], e2[3], tn[3];
        for (int c = 0; c < 3; ++c) { e1[c] = w[2][c] - w[0][c]; e2[c] = w[1][c] - w[0][c]; }
        Cross(e1, e2, tn);
        if (Normalize(tn) <= 0.f) return;
        if (Dot(tn, d.normal) < d.def.cullBackFaces) return;
    }

    LocalVert l[3];
    for (int k = 0; k < 3; ++k) {
        float r[3];
        for (int c = 0; c < 3; ++c) r[c] = w[k][c] - d.basis[9 + c];
        for (int c = 0; c < 3; ++c)
            l[k].l[c] = r[0] * inv[c] + r[1] * inv[3 + c] + r[2] * inv[6 + c];
    }
    const int axes = d.def.zScale > 0.f ? 3 : 2;

    if (!d.def.cutTris) {
        for (int a = 0; a < axes; ++a) {
            bool lo = false, hi = false;
            for (int k = 0; k < 3; ++k) {
                if (l[k].l[a] >= -0.5f) lo = true;
                if (l[k].l[a] <= 0.5f) hi = true;
            }
            if (!lo || !hi) return;
        }
        for (int k = 0; k < 3; ++k)
            d.verts.push_back({{w[k][0], w[k][1], w[k][2]}, l[k].l[0] + 0.5f, l[k].l[1] + 0.5f});
        return;
    }

    for (int a = 0; a < axes; ++a) {
        bool allLo = true, allHi = true;
        for (int k = 0; k < 3; ++k) {
            if (l[k].l[a] >= -0.5f) allLo = false;
            if (l[k].l[a] <= 0.5f) allHi = false;
        }
        if (allLo || allHi) return;
    }
    std::vector<LocalVert> poly(l, l + 3), tmp;
    for (int a = 0; a < axes && !poly.empty(); ++a) {
        ClipAxis(poly, tmp, a, 1.f);
        ClipAxis(poly, tmp, a, -1.f);
    }
    if (poly.size() < 3) return;
    auto toWorld = [&](const LocalVert& v, DecalVertex& out) {
        for (int c = 0; c < 3; ++c)
            out.pos[c] = d.basis[9 + c] + v.l[0] * d.basis[c] + v.l[1] * d.basis[3 + c] +
                         v.l[2] * d.basis[6 + c];
        out.u = v.l[0] + 0.5f;
        out.v = v.l[1] + 0.5f;
    };
    for (size_t i = 1; i + 1 < poly.size(); ++i) {
        if (int(d.verts.size()) + 3 > kMaxVertices) return;
        DecalVertex a, b, c;
        toWorld(poly[0], a);
        toWorld(poly[i], b);
        toWorld(poly[i + 1], c);
        d.verts.push_back(a);
        d.verts.push_back(b);
        d.verts.push_back(c);
    }
}

// Decal::Tick (0x101CDD70): an unanimated decal ages at Cfg.DecalsStayTime
// times real time and holds still under R3D.KeepDecals; an animated one ages
// at real time regardless. Under DecayTime the fade factor is life/decay.
void DecalSystem::Tick(float dt) {
    if (dt <= 0.f) return;
    for (DecalInstance& d : decals_) {
        if (!d.alive || d.immortal || d.finished) continue;
        if (d.def.fps <= 0.f) {
            if (!keep_) d.life -= dt * speed_;
        } else {
            d.life -= dt;
        }
        if (d.life < 0.f) {
            d.alpha = 0;
            d.finished = true;
        } else if (d.life < d.def.decayTime && d.def.decayTime > 0.f) {
            const int a = int(std::lround(d.life / d.def.decayTime * 255.f));
            d.alpha = uint8_t(std::max(0, std::min(255, a)));
        }
    }
}

bool DecalSystem::Finished(int slot) const {
    return Valid(slot) && decals_[size_t(slot)].finished;
}

void DecalSystem::Remove(int slot) {
    if (!Valid(slot)) return;
    if (decals_[size_t(slot)].verts.empty()) ++empty_;
    decals_[size_t(slot)] = DecalInstance();
    --live_;
}

void DecalSystem::Clear() {
    decals_.clear();
    live_ = 0;
    empty_ = 0;
}

} // namespace painful
