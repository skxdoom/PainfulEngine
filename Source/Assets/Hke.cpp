#include "Hke.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_map>

namespace painful {

// ROTATION is ANGLE-AXIS - `angle x y z` - and a zero axis means no rotation
// rather than an invalid one. Rodrigues, written into the row-vector form:
// row i is the image of basis vector i, so out[i*4+j] = R(j,i).
void HkeBody::RestMatrix(float out[16]) const {
    for (int i = 0; i < 16; ++i) out[i] = 0.f;
    out[0] = out[5] = out[10] = out[15] = 1.f;

    float k[3] = {rotAxis[0], rotAxis[1], rotAxis[2]};
    const float len = std::sqrt(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]);
    if (len > 1e-6f && std::fabs(rotAngle) > 1e-9f) {
        for (int c = 0; c < 3; ++c) k[c] /= len;
        const float s = std::sin(rotAngle), co = std::cos(rotAngle), t = 1.f - co;
        const float R[3][3] = {
            {co + k[0]*k[0]*t,      k[0]*k[1]*t - k[2]*s,  k[0]*k[2]*t + k[1]*s},
            {k[1]*k[0]*t + k[2]*s,  co + k[1]*k[1]*t,      k[1]*k[2]*t - k[0]*s},
            {k[2]*k[0]*t - k[1]*s,  k[2]*k[1]*t + k[0]*s,  co + k[2]*k[2]*t}};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) out[i * 4 + j] = R[j][i];
    }
    for (int c = 0; c < 3; ++c) out[12 + c] = translation[c];
}

const HkeGeometry* Hke::Find(const std::string& geometry) const {
    for (const HkeGeometry& g : geometries)
        if (g.name == geometry) return &g;
    return nullptr;
}

const HkeBody* Hke::Body(const std::string& bone) const {
    for (const HkeBody& b : bodies)
        if (b.bone == bone) return &b;
    return nullptr;
}

bool Hke::Linked(const std::string& a, const std::string& b) const {
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;

    // Breadth-first over the constraint graph. It is a handful of nodes - the
    // largest shipped ragdoll is 30 bodies - so an adjacency walk per query is
    // cheaper than building and keeping an index.
    std::vector<std::string> open{a};
    std::vector<std::string> seen{a};
    while (!open.empty()) {
        const std::string at = open.back();
        open.pop_back();
        for (const HkeConstraint& c : constraints) {
            std::string other;
            if      (c.bodyA == at) other = c.bodyB;
            else if (c.bodyB == at) other = c.bodyA;
            else continue;
            if (other.empty()) continue;
            if (other == b) return true;
            if (std::find(seen.begin(), seen.end(), other) != seen.end()) continue;
            seen.push_back(other);
            open.push_back(other);
        }
    }
    return false;
}

namespace {

// The file is whitespace-separated tokens and nothing else, and it HAS to be
// read that way rather than line by line: a constraint packs three keys onto
// one line ("STRENGTH 1.0 TAU 0.1 TWIST_MIN -0.087266"), so a line-oriented
// parser sees the first key and throws the rest away.
struct Tokens {
    std::vector<std::string> t;
    size_t i = 0;

    bool done() const { return i >= t.size(); }
    const std::string& peek() const {
        static const std::string kEmpty;
        return i < t.size() ? t[i] : kEmpty;
    }
    std::string next() { return i < t.size() ? t[i++] : std::string(); }
    float f() { return float(std::atof(next().c_str())); }
    int d() { return std::atoi(next().c_str()); }
    bool b() { return next() == "TRUE"; }
    void vec3(float out[3]) { for (int c = 0; c < 3; ++c) out[c] = f(); }
    // ANGLE first, then the axis - see HkeBody::rotAngle.
    void angleAxis(float& angle, float axis[3]) { angle = f(); vec3(axis); }
};

void Tokenize(const std::string& text, Tokens& out) {
    size_t p = 0;
    while (p < text.size()) {
        while (p < text.size() && (unsigned char)(text[p]) <= ' ') ++p;
        if (p >= text.size()) break;
        const size_t s = p;
        while (p < text.size() && (unsigned char)(text[p]) > ' ') ++p;
        out.t.push_back(text.substr(s, p - s));
    }
}

void Note(Hke& out, const std::string& key) {
    if (std::find(out.unknown.begin(), out.unknown.end(), key) == out.unknown.end())
        out.unknown.push_back(key);
}

void ParseGeometry(Tokens& k, Hke& out) {
    HkeGeometry g;
    k.next();                       // the kind, always "Inline" in the shipped set
    g.name = k.next();
    const int vc = k.d();
    if (vc < 0 || vc > 1 << 20) return;
    g.verts.reserve(size_t(vc) * 3);
    for (int v = 0; v < vc; ++v)
        for (int c = 0; c < 3; ++c) g.verts.push_back(k.f());
    const int tc = k.d();
    if (tc >= 0 && tc <= 1 << 20) {
        g.tris.reserve(size_t(tc) * 3);
        for (int t = 0; t < tc; ++t)
            for (int c = 0; c < 3; ++c) g.tris.push_back(uint32_t(k.d()));
    }
    while (!k.done() && k.peek() != "END_GEOMETRY") k.next();
    k.next();                       // END_GEOMETRY
    out.geometries.push_back(std::move(g));
}

void ParsePrimitive(Tokens& k, Hke& out, HkeBody& body) {
    k.next();                       // kind ("GeometricPrimitive")
    k.next();                       // name
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_PRIMITIVE") return;
        else if (key == "MASS")                body.mass = k.f();
        else if (key == "ROTATION")            k.angleAxis(body.primRotAngle, body.primRotAxis);
        else if (key == "TRANSLATION")         k.vec3(body.primTranslation);
        else if (key == "COLLISION_MASK")      body.collisionMask = k.d();
        else if (key == "GEOMETRY")            body.geometry = k.next();
        else if (key == "CONVEX")              body.convex = k.b();
        else if (key == "COLLISIONS_DISABLED") k.b();
        else Note(out, key);
    }
}

void ParseRigidBody(Tokens& k, Hke& out) {
    HkeBody body;
    body.bone = k.next();
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_RIGID_BODY") break;
        else if (key == "ELLASTICITY")         body.elasticity = k.f();
        else if (key == "STATIC_FRICTION")     body.staticFriction = k.f();
        else if (key == "DYNAMIC_FRICTION")    body.dynamicFriction = k.f();
        else if (key == "ROTATION")            k.angleAxis(body.rotAngle, body.rotAxis);
        else if (key == "TRANSLATION")         k.vec3(body.translation);
        else if (key == "DISPLACEMENT")        k.vec3(body.displacement);
        else if (key == "ACTIVE")              body.active = k.b();
        else if (key == "COLLISIONS_DISABLED") body.collisionsDisabled = k.b();
        else if (key == "LINEAR_VELOCITY" || key == "ANGULAR_VELOCITY") {
            float ignored[3];
            k.vec3(ignored);        // authored at rest in every shipped file
        }
        else if (key == "BEGIN_PRIMITIVE") ParsePrimitive(k, out, body);
        else Note(out, key);
    }
    out.bodies.push_back(std::move(body));
}

void ParseConstraint(Tokens& k, Hke& out) {
    HkeConstraint c;
    const std::string kind = k.next();
    if      (kind == "Hinge")       c.kind = HkeConstraint::kHinge;
    else if (kind == "Ragdoll")     c.kind = HkeConstraint::kRagdoll;
    else if (kind == "StiffSpring") c.kind = HkeConstraint::kStiffSpring;
    else { c.kind = HkeConstraint::kRagdoll; Note(out, "CONSTRAINT:" + kind); }
    c.name = k.next();
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_CONSTRAINT") break;
        else if (key == "TWO_BODIED")   c.twoBodied = k.b();
        else if (key == "IS_BREAKABLE") c.breakable = k.b();
        else if (key == "STRENGTH")     c.strength = k.f();
        else if (key == "TAU")          c.tau = k.f();
        // Hinge names its pair A/B, Ragdoll names it reference/attached. Both
        // land in the same two fields, reference first, so the graph walk does
        // not have to know which kind it is looking at.
        else if (key == "RIGID_BODY_A" || key == "RIGID_BODY_REFERENCE") c.bodyA = k.next();
        else if (key == "RIGID_BODY_B" || key == "RIGID_BODY_ATTACHED")  c.bodyB = k.next();
        else if (key == "IS_LIMITED")          c.limited = k.b();
        else if (key == "HINGE_POS_IN_A")      k.vec3(c.hingePosA);
        else if (key == "HINGE_POS_IN_B")      k.vec3(c.hingePosB);
        else if (key == "HINGE_DIR_IN_A")      k.vec3(c.hingeDirA);
        else if (key == "HINGE_DIR_IN_B")      k.vec3(c.hingeDirB);
        else if (key == "HINGE_DIR_PERP_IN_A") k.vec3(c.hingePerpA);
        else if (key == "HINGE_DIR_PERP_IN_B") k.vec3(c.hingePerpB);
        else if (key == "LIMIT_MIN_ANGLE")     c.limitMinAngle = k.f();
        else if (key == "LIMIT_MAX_ANGLE")     c.limitMaxAngle = k.f();
        else if (key == "LIMIT_FRICTION")      c.limitFriction = k.f();
        else if (key == "TWIST_MIN")           c.twistMin = k.f();
        else if (key == "TWIST_MAX")           c.twistMax = k.f();
        else if (key == "CONE_MIN")            c.coneMin = k.f();
        else if (key == "CONE_MAX")            c.coneMax = k.f();
        else if (key == "PLANE_MIN")           c.planeMin = k.f();
        else if (key == "PLANE_MAX")           c.planeMax = k.f();
        // The world-space form of either kind. A file states its frames one
        // way or the other and both appear across the set, so remember which
        // arrived rather than leaving a builder to guess from zeroes.
        else if (key == "WORLD_PIVOT_POINT") { c.worldSpace = true; k.vec3(c.worldPivot); }
        else if (key == "TWIST_AXIS")          k.vec3(c.twistAxis);
        else if (key == "PLANE_AXIS")          k.vec3(c.planeAxis);
        else if (key == "WORLD_HINGE_POS")   { c.worldSpace = true; k.vec3(c.worldHingePos); }
        else if (key == "WORLD_HINGE_DIR")     k.vec3(c.worldHingeDir);
        // StiffSpring
        else if (key == "LOCAL_POINT_A")       k.vec3(c.localPointA);
        else if (key == "LOCAL_POINT_B")       k.vec3(c.localPointB);
        else if (key == "SPRING_LENGTH")       c.springLength = k.f();
        else if (key == "LINEAR_STRENGTH")     c.linearStrength = k.f();
        else if (key == "ANGULAR_STRENGTH")    c.angularStrength = k.f();
        else if (key.rfind("CS_TO_REF_TM_COL", 0) == 0) {
            const int col = std::atoi(key.c_str() + 16);
            if (col >= 0 && col < 4) k.vec3(c.csToRef[col]); else { float x[3]; k.vec3(x); }
        }
        else if (key.rfind("CS_TO_ATT_TM_COL", 0) == 0) {
            const int col = std::atoi(key.c_str() + 16);
            if (col >= 0 && col < 4) k.vec3(c.csToAtt[col]); else { float x[3]; k.vec3(x); }
        }
        else Note(out, key);
    }
    out.constraints.push_back(std::move(c));
}

// BEGIN_ACTION Spring - a soft spring between two limbs, and the third thing
// an action can be after Drag and FastConstraintSolver. 17 files carry them.
void ParseSpring(Tokens& k, Hke& out) {
    HkeSpring s;
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_ACTION") break;
        else if (key == "COLLECTION")      k.next();
        else if (key == "BODY_A")          s.bodyA = k.next();
        else if (key == "BODY_B")          s.bodyB = k.next();
        else if (key == "POINT_A")         k.vec3(s.pointA);
        else if (key == "POINT_B")         k.vec3(s.pointB);
        else if (key == "HAS_TWO_BODIES")  s.twoBodied = k.b();
        else if (key == "RESTITUTION")     s.restitution = k.f();
        else if (key == "REST_LENGTH")     s.restLength = k.f();
        else if (key == "DAMPING")         s.damping = k.f();
        else if (key == "ON_COMPRESSION")  s.onCompression = k.b();
        else if (key == "ON_EXTENSION")    s.onExtension = k.b();
        else Note(out, key);
    }
    out.springs.push_back(std::move(s));
}

// BEGIN_ACTION Dashpot - hkLinearDashpotAction; C2L2_Door2 only.
void ParseDashpot(Tokens& k, Hke& out) {
    HkeDashpot d;
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_ACTION") break;
        else if (key == "COLLECTION")     k.next();
        else if (key == "BODY_A")         d.bodyA = k.next();
        else if (key == "BODY_B")         d.bodyB = k.next();
        else if (key == "LOCAL_POINT_A")  k.vec3(d.pointA);
        else if (key == "LOCAL_POINT_B")  k.vec3(d.pointB);
        else if (key == "TWO_BODIES")     d.twoBodied = k.b();
        else if (key == "STRENGTH")       d.strength = k.f();
        else if (key == "DAMPING")        d.damping = k.f();
        else Note(out, key);
    }
    out.dashpots.push_back(std::move(d));
}

void ParseAction(Tokens& k, Hke& out) {
    const std::string kind = k.next();   // Drag, FastConstraintSolver, Spring or Dashpot
    k.next();                            // name
    if (kind == "Spring") { ParseSpring(k, out); return; }
    if (kind == "Dashpot") { ParseDashpot(k, out); return; }
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_ACTION") return;
        else if (key == "LINEAR_DRAG")             out.linearDrag = k.f();
        else if (key == "ANGULAR_DRAG")            out.angularDrag = k.f();
        else if (key == "DEACTIVATION_THRESHOLD")  out.deactivationThreshold = k.f();
        else if (key == "RB_COLLECTION")           k.next();   // names the collection it solves
        else if (key == "BEGIN_CONSTRAINT")        ParseConstraint(k, out);
        else Note(out, key);
    }
}

void ParseCollection(Tokens& k, Hke& out) {
    k.next();                       // kind ("RBCollection")
    k.next();                       // name
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_COLLECTION") return;
        else if (key == "ODE_SOLVER")       k.next();
        else if (key == "REFRESH_RATE")     k.f();
        else if (key == "BEGIN_RIGID_BODY") ParseRigidBody(k, out);
        else Note(out, key);
    }
}

void ParseSubspace(Tokens& k, Hke& out) {
    k.next();                       // name
    while (!k.done()) {
        const std::string key = k.next();
        if (key == "END_SUBSPACE") return;
        else if (key == "GRAVITY")                     k.vec3(out.gravity);
        else if (key == "TOLERANCE")                   k.f();
        else if (key == "HAS_DEACTIVATOR")             k.b();
        else if (key == "DEACTIVATOR_SHORT_FREQUENCY") k.f();
        else if (key == "DEACTIVATOR_LONG_FREQUENCY")  k.f();
        else if (key == "RESOLVER")                    k.next();
        else if (key == "RB_COLLECTION")               k.next();   // a reference by name
        // The block also appears inside the subspace, not only at world level,
        // and is empty in every shipped file.
        else if (key == "BEGIN_ENABLED_COLLISION_GROUPS") {
            k.next();
            while (!k.done() && k.peek() != "END_ENABLED_COLLISION_GROUPS") k.next();
            k.next();
        }
        else if (key == "BEGIN_COLLECTION")            ParseCollection(k, out);
        else if (key == "BEGIN_ACTION")                ParseAction(k, out);
        else Note(out, key);
    }
}

} // namespace

// ---------------------------------------------------------------- binary form
//
// The 'B' form is the same token stream as the text form, written by Havok 2's
// hke exporter and read by the engine's importer (FUN_10262F20, FUN_10264240):
// every keyword and every word-valued argument (TRUE, Inline, Hinge, ...) is a
// u32 id = ELF hash of the word modulo 0x7fffffff (FUN_102641C0, DAT_103E70F4);
// an END_* keyword is preceded by the marker 0x12ABCDEF; ints and floats are
// four bytes; strings are NUL-terminated. Decoded back into text here and
// parsed by the text path. Docs/Reference/Physics.md, "The binary .hke".
namespace {

uint32_t HkeWordId(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        h = h * 16u + uint32_t(uint8_t(*s));
        const uint32_t g = h & 0xF0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h % 0x7fffffffu;
}

// Argument shape per keyword: S string, I int, F float, T word, G the geometry
// body (vertex count, vertices, triangle count, triangles). Words without a
// shape are values (TRUE, Inline, ...) and block ends.
struct HkeWord {
    const char* name;
    const char* args;
};
const HkeWord kHkeWords[] = {
    {"BEGIN_WORLD", "S"}, {"END_WORLD", ""}, {"VERSION", "I"}, {"WORLD_SCALE", "F"},
    {"FAST_SUBSPACE", "B"}, {"BEGIN_GEOMETRY", "TSG"}, {"END_GEOMETRY", ""},
    {"BEGIN_SUBSPACE", "S"}, {"END_SUBSPACE", ""}, {"GRAVITY", "FFF"}, {"TOLERANCE", "F"},
    {"HAS_DEACTIVATOR", "B"}, {"DEACTIVATOR_SHORT_FREQUENCY", "F"},
    {"DEACTIVATOR_LONG_FREQUENCY", "F"}, {"DEACTIVATION_THRESHOLD", "F"}, {"RESOLVER", "T"},
    {"BEGIN_COLLECTION", "TS"}, {"END_COLLECTION", ""}, {"ODE_SOLVER", "T"},
    {"REFRESH_RATE", "F"}, {"BEGIN_RIGID_BODY", "S"}, {"END_RIGID_BODY", ""},
    {"ELLASTICITY", "F"}, {"STATIC_FRICTION", "F"}, {"DYNAMIC_FRICTION", "F"},
    {"ROTATION", "FFFF"}, {"TRANSLATION", "FFF"}, {"LINEAR_VELOCITY", "FFF"},
    {"ANGULAR_VELOCITY", "FFF"}, {"COLLISIONS_DISABLED", "B"}, {"ACTIVE", "B"},
    {"DISPLACEMENT", "FFF"}, {"BEGIN_PRIMITIVE", "TS"}, {"END_PRIMITIVE", ""}, {"MASS", "F"},
    {"COLLISION_MASK", "I"}, {"GEOMETRY", "S"}, {"CONVEX", "B"}, {"BEGIN_ACTION", "TS"},
    {"END_ACTION", ""}, {"LINEAR_DRAG", "F"}, {"ANGULAR_DRAG", "F"}, {"BEGIN_CONSTRAINT", "TS"},
    {"END_CONSTRAINT", ""}, {"TWO_BODIED", "B"}, {"HAS_TWO_BODIES", "B"}, {"TWO_BODIES", "B"}, {"RIGID_BODY_A", "S"},
    {"RIGID_BODY_B", "S"}, {"RIGID_BODY_REFERENCE", "S"}, {"RIGID_BODY_ATTACHED", "S"},
    {"BODY_A", "S"}, {"BODY_B", "S"}, {"HINGE_POS_IN_A", "FFF"}, {"HINGE_POS_IN_B", "FFF"},
    {"HINGE_DIR_IN_A", "FFF"}, {"HINGE_DIR_IN_B", "FFF"}, {"HINGE_DIR_PERP_IN_A", "FFF"},
    {"HINGE_DIR_PERP_IN_B", "FFF"}, {"IS_BREAKABLE", "B"}, {"STRENGTH", "F"}, {"TAU", "F"},
    {"IS_LIMITED", "B"}, {"LIMIT_MIN_ANGLE", "F"}, {"LIMIT_MAX_ANGLE", "F"},
    {"LIMIT_FRICTION", "F"}, {"CS_TO_REF_TM_COL0", "FFF"}, {"CS_TO_REF_TM_COL1", "FFF"},
    {"CS_TO_REF_TM_COL2", "FFF"}, {"CS_TO_REF_TM_COL3", "FFF"}, {"CS_TO_ATT_TM_COL0", "FFF"},
    {"CS_TO_ATT_TM_COL1", "FFF"}, {"CS_TO_ATT_TM_COL2", "FFF"}, {"CS_TO_ATT_TM_COL3", "FFF"},
    {"TWIST_MIN", "F"}, {"TWIST_MAX", "F"}, {"CONE_MIN", "F"}, {"CONE_MAX", "F"},
    {"PLANE_MIN", "F"}, {"PLANE_MAX", "F"}, {"WORLD_PIVOT_POINT", "FFF"}, {"TWIST_AXIS", "FFF"},
    {"PLANE_AXIS", "FFF"}, {"WORLD_HINGE_POS", "FFF"}, {"WORLD_HINGE_DIR", "FFF"},
    {"LOCAL_POINT_A", "FFF"}, {"LOCAL_POINT_B", "FFF"}, {"SPRING_LENGTH", "F"},
    {"LINEAR_STRENGTH", "F"}, {"ANGULAR_STRENGTH", "F"}, {"POINT_A", "FFF"}, {"POINT_B", "FFF"},
    {"RESTITUTION", "F"}, {"REST_LENGTH", "F"}, {"DAMPING", "F"}, {"ON_COMPRESSION", "B"}, {"ON_EXTENSION", "B"},
    {"BEGIN_ENABLED_COLLISION_GROUPS", "S"}, {"END_ENABLED_COLLISION_GROUPS", ""},
    {"RB_COLLECTION", "S"}, {"COLLECTION", "S"}, {"BEGIN_MARKER_LIST", ""},
    {"END_MARKER_LIST", ""}, {"BEGIN_DISPLAY_WORLD", "S"}, {"END_DISPLAY_WORLD", ""},
    // Word values.
    {"TRUE", nullptr}, {"FALSE", nullptr}, {"Inline", nullptr}, {"Euler", nullptr},
    {"RK4", nullptr}, {"ComplexFriction", nullptr}, {"SimpleFriction", nullptr},
    {"RBCollection", nullptr}, {"GeometricPrimitive", nullptr}, {"Hinge", nullptr},
    {"Ragdoll", nullptr}, {"StiffSpring", nullptr}, {"BallAndSocket", nullptr},
    {"Prismatic", nullptr}, {"PointToPlane", nullptr}, {"Wheel", nullptr}, {"Pulley", nullptr},
    {"Spring", nullptr}, {"Dashpot", nullptr}, {"Drag", nullptr}, {"FastConstraintSolver", nullptr},
    {"ConstraintSolver", nullptr}, {"Mesh", nullptr},
};

const HkeWord* FindHkeWord(uint32_t id) {
    static std::unordered_map<uint32_t, const HkeWord*> table;
    if (table.empty())
        for (const HkeWord& w : kHkeWords) table[HkeWordId(w.name)] = &w;
    const auto it = table.find(id);
    return it == table.end() ? nullptr : it->second;
}

bool DecodeBinaryHke(const std::vector<uint8_t>& data, std::string& text, std::string& error) {
    size_t pos = 1;                                 // past the 'B'
    char buf[64];
    const auto u32 = [&](uint32_t& v) {
        if (pos + 4 > data.size()) return false;
        std::memcpy(&v, data.data() + pos, 4);
        pos += 4;
        return true;
    };
    const auto emitInt = [&]() {
        uint32_t v; if (!u32(v)) return false;
        std::snprintf(buf, sizeof buf, " %d", int(v)); text += buf; return true;
    };
    const auto emitFloat = [&]() {
        uint32_t v; if (!u32(v)) return false;
        float f; std::memcpy(&f, &v, 4);
        std::snprintf(buf, sizeof buf, " %.6f", double(f)); text += buf; return true;
    };
    const auto emitString = [&]() {
        const size_t end = std::find(data.begin() + ptrdiff_t(pos), data.end(), uint8_t(0)) -
                           data.begin();
        if (end >= data.size()) return false;
        text += ' ';
        text.append(reinterpret_cast<const char*>(data.data() + pos), end - pos);
        pos = end + 1;
        return true;
    };
    const auto emitWord = [&]() {
        uint32_t v; if (!u32(v)) return false;
        if (const HkeWord* w = FindHkeWord(v)) { text += ' '; text += w->name; }
        else { std::snprintf(buf, sizeof buf, " WORD_%08x", v); text += buf; }
        return true;
    };
    text = "A\n";
    while (pos + 4 <= data.size()) {
        uint32_t id;
        u32(id);
        if (id == 0x12abcdefu && !u32(id)) break;   // the END marker, then the keyword
        const HkeWord* w = FindHkeWord(id);
        if (!w || !w->args) {
            std::snprintf(buf, sizeof buf, "unknown keyword id %08x at offset %zu", id, pos - 4);
            error = buf;
            return false;
        }
        text += w->name;
        for (const char* a = w->args; *a; ++a) {
            bool ok = true;
            switch (*a) {
                case 'S': ok = emitString(); break;
                case 'I': ok = emitInt(); break;
                case 'F': ok = emitFloat(); break;
                case 'T': ok = emitWord(); break;
                case 'B':                           // a boolean is ONE byte
                    if (pos >= data.size()) ok = false;
                    else text += data[pos++] ? " TRUE" : " FALSE";
                    break;
                case 'G': {
                    uint32_t nv = 0, nt = 0;
                    ok = u32(nv);
                    if (ok) { std::snprintf(buf, sizeof buf, "\n%u\n", nv); text += buf; }
                    for (uint32_t i = 0; ok && i < nv; ++i) {
                        ok = emitFloat() && emitFloat() && emitFloat();
                        text += '\n';
                    }
                    if (ok) ok = u32(nt);
                    if (ok) { std::snprintf(buf, sizeof buf, "%u\n", nt); text += buf; }
                    for (uint32_t i = 0; ok && i < nt; ++i) {
                        ok = emitInt() && emitInt() && emitInt();
                        text += '\n';
                    }
                    break;
                }
                default: break;
            }
            if (!ok) {
                std::snprintf(buf, sizeof buf, "truncated after %s at offset %zu", w->name, pos);
                error = buf;
                return false;
            }
        }
        text += '\n';
        if (std::strcmp(w->name, "END_WORLD") == 0) break;
    }
    return true;
}

} // namespace

bool Hke::DecodeToText(const std::string& path, std::string& text, std::string& error) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) { error = "cannot read file"; return false; }
    if (data[0] == 'B') return DecodeBinaryHke(data, text, error);
    text.assign(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

bool Hke::Load(const std::string& path, Hke& out) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) {
        out.error = "cannot read file";
        return false;
    }
    if (data.empty()) {
        out.error = "empty file";
        return false;
    }
    // 'A' is the text form, 'B' the binary one - the same token stream, decoded
    // above into text so one parser reads both.
    std::string text;
    if (data[0] == 'B') {
        out.binary = true;
        if (!DecodeBinaryHke(data, text, out.error)) return false;
    } else if (data[0] != 'A') {
        out.error = "not an .hke (bad leading byte)";
        return false;
    } else {
        text.assign(reinterpret_cast<const char*>(data.data()), data.size());
    }

    Tokens k;
    Tokenize(text, k);
    k.next();                       // the leading 'A'

    while (!k.done()) {
        const std::string key = k.next();
        if (key == "BEGIN_WORLD")   { k.next(); continue; }   // name
        else if (key == "END_WORLD")     break;
        else if (key == "VERSION")       out.version = k.d();
        else if (key == "WORLD_SCALE")   out.worldScale = k.f();
        else if (key == "FAST_SUBSPACE") k.b();
        else if (key == "BEGIN_GEOMETRY") ParseGeometry(k, out);
        else if (key == "BEGIN_ENABLED_COLLISION_GROUPS") {
            k.next();               // name
            while (!k.done() && k.peek() != "END_ENABLED_COLLISION_GROUPS") k.next();
            k.next();
        }
        else if (key == "BEGIN_SUBSPACE") ParseSubspace(k, out);
        else Note(out, key);
    }

    if (out.bodies.empty()) out.error = "no rigid bodies";
    return out.error.empty();
}

} // namespace painful
