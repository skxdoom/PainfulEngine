#include "Properties.h"
#include "../Core/Common.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace painful {

namespace {

void TrimInPlace(std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
}

// Splits "1.0, 2, 3" into numbers. Non-numeric arguments become 0.
std::vector<double> ParseArgs(const std::string& inside) {
    std::vector<double> out;
    size_t start = 0;
    while (start <= inside.size()) {
        size_t comma = inside.find(',', start);
        std::string piece = inside.substr(start, comma == std::string::npos
                                                 ? std::string::npos : comma - start);
        TrimInPlace(piece);
        if (!piece.empty()) out.push_back(std::atof(piece.c_str()));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

} // namespace

void Properties::LoadFromText(const std::string& text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos
                                            ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? text.size() : eol + 1;

        // Strip Lua comments before looking for an assignment.
        size_t comment = line.find("--");
        if (comment != std::string::npos) line = line.substr(0, comment);
        TrimInPlace(line);

        // Two assignment forms appear in the data, both with an arbitrary
        // leading identifier - level and entity files use "o.", templates use
        // their own name (e.g. "Bat_Adrian.BaseObj = ..."), so keying on "o."
        // alone misses them:
        //
        //     Flame.Scale      = 0.2
        //     Flame["Effect"]  = "Pochodnia"
        //
        // The bracket form is not decoration: 117 shipped templates use it, and
        // for the ParticleFX ones it is the only place o.Effect is declared.
        size_t nameEnd = 0;
        while (nameEnd < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[nameEnd])) || line[nameEnd] == '_'))
            ++nameEnd;
        if (nameEnd == 0 || nameEnd >= line.size()) continue;

        std::string key;
        size_t eq;
        if (line[nameEnd] == '[') {
            const size_t open = line.find('"', nameEnd);
            if (open == std::string::npos) continue;
            const size_t close = line.find('"', open + 1);
            if (close == std::string::npos) continue;
            const size_t bracket = line.find(']', close);
            if (bracket == std::string::npos) continue;
            eq = line.find('=', bracket);
            if (eq == std::string::npos) continue;
            key = line.substr(open + 1, close - open - 1);
        } else if (line[nameEnd] == '.') {
            eq = line.find('=', nameEnd);
            if (eq == std::string::npos) continue;
            key = line.substr(nameEnd + 1, eq - nameEnd - 1);
        } else {
            continue;
        }

        std::string rhs = line.substr(eq + 1);
        TrimInPlace(key);
        TrimInPlace(rhs);
        if (key.empty() || rhs.empty()) continue;
        if (!rhs.empty() && rhs.back() == ',') rhs.pop_back();
        TrimInPlace(rhs);
        if (rhs.empty()) continue;

        Value v;
        if (rhs.front() == '"') {
            size_t close = rhs.find('"', 1);
            if (close == std::string::npos) continue;
            v.kind = Value::Kind::String;
            v.text = rhs.substr(1, close - 1);
        } else if (rhs == "true" || rhs == "false") {
            v.kind = Value::Kind::Bool;
            v.boolean = (rhs == "true");
        } else if (size_t call = rhs.find(":New("); call != std::string::npos) {
            size_t close = rhs.rfind(')');
            if (close == std::string::npos || close < call) continue;
            v.kind = Value::Kind::Ctor;
            v.text = rhs.substr(0, call);
            v.args = ParseArgs(rhs.substr(call + 5, close - (call + 5)));
        } else {
            char* end = nullptr;
            double n = std::strtod(rhs.c_str(), &end);
            if (end == rhs.c_str()) continue;   // not a plain number: ignore
            v.kind = Value::Kind::Number;
            v.number = n;
        }
        values_[key] = std::move(v);
    }
}

bool Properties::LoadFromFile(const std::string& path) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) return false;
    LoadFromText(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    return true;
}

const Value* Properties::Find(const std::string& key) const {
    auto it = values_.find(key);
    return it == values_.end() ? nullptr : &it->second;
}

double Properties::Number(const std::string& key, double fallback) const {
    const Value* v = Find(key);
    return (v && v->kind == Value::Kind::Number) ? v->number : fallback;
}

std::string Properties::String(const std::string& key, const std::string& fallback) const {
    const Value* v = Find(key);
    return (v && v->kind == Value::Kind::String) ? v->text : fallback;
}

bool Properties::Bool(const std::string& key, bool fallback) const {
    const Value* v = Find(key);
    return (v && v->kind == Value::Kind::Bool) ? v->boolean : fallback;
}

bool Properties::Vector3(const std::string& key, float out[3]) const {
    const Value* v = Find(key);
    if (!v || v->kind != Value::Kind::Ctor || v->args.size() < 3) return false;
    out[0] = v->Arg(0);
    out[1] = v->Arg(1);
    out[2] = v->Arg(2);
    return true;
}

// Placed objects store their orientation one of two ways, both straight out of
// the shipped data:
//
//     o.Rot = Quaternion:New(w, x, y, z)   -- 6039 instances
//     o.Ang.X / o.Ang.Y / o.Ang.Z          -- Euler radians
//
// Neither is universal, so whichever is present wins and identity is the
// fallback. Component order (w, x, y, z) and the matrix form were both read out
// of Engine.dll: PhysicsWorld::GetHavokBodyRotation stores Havok's (x,y,z,w)
// into the engine layout as (-w, x, y, z), and the engine's quaternion-to-
// matrix routine (FUN_1000bb90) emits the standard TEXTBOOK matrix, applied to
// row vectors as-is - NOT transposed into row-vector form. Pre-transposing
// here mirrored every rotation (+28 degrees rendered as -28).
void ReadRotation(const Properties& props, float out[9]) {
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(out, identity, sizeof(identity));

    if (const Value* q = props.Find("Rot")) {
        if (q->kind == Value::Kind::Ctor && q->args.size() >= 4) {
            const float w = q->Arg(0), x = q->Arg(1), y = q->Arg(2), z = q->Arg(3);
            const float n = std::sqrt(w * w + x * x + y * y + z * z);
            if (n > 1e-6f) {
                const float iw = w / n, ix = x / n, iy = y / n, iz = z / n;
                // Verbatim from the engine's own conversion.
                out[0] = 1 - 2 * (iy * iy + iz * iz);
                out[1] = 2 * (ix * iy - iz * iw);
                out[2] = 2 * (ix * iz + iy * iw);
                out[3] = 2 * (ix * iy + iz * iw);
                out[4] = 1 - 2 * (ix * ix + iz * iz);
                out[5] = 2 * (iy * iz - ix * iw);
                out[6] = 2 * (ix * iz - iy * iw);
                out[7] = 2 * (iy * iz + ix * iw);
                out[8] = 1 - 2 * (ix * ix + iy * iy);
                return;
            }
        }
    }

    if (!props.Has("Ang.X") && !props.Has("Ang.Y") && !props.Has("Ang.Z")) return;
    const float ax = float(props.Number("Ang.X", 0.0));
    const float ay = float(props.Number("Ang.Y", 0.0));
    const float az = float(props.Number("Ang.Z", 0.0));
    const float sx = std::sin(ax), cx = std::cos(ax);
    const float sy = std::sin(ay), cy = std::cos(ay);
    const float sz = std::sin(az), cz = std::cos(az);
    // Y (yaw) * X (pitch) * Z (roll), row-vector.
    out[0] = cy * cz + sy * sx * sz;  out[1] = cx * sz;  out[2] = -sy * cz + cy * sx * sz;
    out[3] = -cy * sz + sy * sx * cz; out[4] = cx * cz;  out[5] = sy * sz + cy * sx * cz;
    out[6] = sy * cx;                 out[7] = -sx;      out[8] = cy * cx;
}

} // namespace painful
