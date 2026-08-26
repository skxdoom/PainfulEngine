#include "Properties.h"
#include "../Core/Common.h"
#include <cctype>
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

        // Accept "<name>.<path> = <value>" for any leading identifier. Level and
        // entity files use "o.", but templates use their own name as the prefix
        // (e.g. "Bat_Adrian.BaseObj = ..."), so keying on "o." alone misses them.
        size_t dot = line.find('.');
        if (dot == std::string::npos || dot == 0) continue;
        bool identifier = true;
        for (size_t i = 0; i < dot; ++i) {
            char c = line[i];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) { identifier = false; break; }
        }
        if (!identifier) continue;

        size_t eq = line.find('=', dot);
        if (eq == std::string::npos) continue;

        std::string key = line.substr(dot + 1, eq - dot - 1);
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

} // namespace painful
