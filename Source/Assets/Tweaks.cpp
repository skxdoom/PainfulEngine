#include "Tweaks.h"
#include "../Core/Common.h"
#include "../Core/Log.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace painful {

namespace {

// Strips a Lua line comment, leaving anything inside a quoted string alone.
std::string StripComment(const std::string& line) {
    bool inString = false;
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        if (line[i] == '"') inString = !inString;
        if (!inString && line[i] == '-' && line[i + 1] == '-') return line.substr(0, i);
    }
    return line;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Evaluates the arithmetic the shipped file actually uses: a chain of numbers
// joined by + - * /, applied left to right. "2*9.81" and "1/8" are the only
// shapes that appear, but the general form costs nothing.
bool EvalNumber(const std::string& expr, double& out) {
    const std::string s = Trim(expr);
    if (s.empty()) return false;

    size_t i = 0;
    auto readNumber = [&](double& value) -> bool {
        const char* begin = s.c_str() + i;
        char* end = nullptr;
        value = std::strtod(begin, &end);
        if (end == begin) return false;
        i += static_cast<size_t>(end - begin);
        return true;
    };

    double acc = 0;
    if (!readNumber(acc)) return false;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) break;
        const char op = s[i];
        if (op != '+' && op != '-' && op != '*' && op != '/') return false;
        ++i;
        double rhs = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (!readNumber(rhs)) return false;
        switch (op) {
        case '+': acc += rhs; break;
        case '-': acc -= rhs; break;
        case '*': acc *= rhs; break;
        case '/': if (rhs == 0) return false; acc /= rhs; break;
        default: return false;
        }
    }
    out = acc;
    return true;
}

bool IsIdentifier(const std::string& s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

} // namespace

bool Tweaks::LoadFromDataRoot(const std::string& dataRoot) {
    return LoadFromFile(dataRoot + "/LScripts/Main/Tweak.lua");
}

bool Tweaks::LoadFromFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) return false;
    LoadFromText(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    loaded_ = true;
    return true;
}

void Tweaks::LoadFromText(const std::string& text) {
    // A path of table names. The outermost is "Tweak" itself, which is dropped
    // from the keys so lookups read PlayerMove.PlayerSpeed.
    std::vector<std::string> path;
    bool started = false;

    auto fullKey = [&](const std::string& leaf) {
        std::string key;
        // path[0] is "Tweak".
        for (size_t i = 1; i < path.size(); ++i) {
            key += path[i];
            key += '.';
        }
        key += leaf;
        return key;
    };

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t eol = text.find('\n', pos);
        std::string line = StripComment(text.substr(pos, eol == std::string::npos
                                                            ? std::string::npos
                                                            : eol - pos));
        pos = (eol == std::string::npos) ? text.size() + 1 : eol + 1;
        line = Trim(line);
        if (line.empty()) continue;

        // Everything after the table literal is behaviour (Tweak:Apply and
        // friends); stop rather than trying to read it.
        if (!started && line.rfind("Tweak", 0) != 0) continue;
        if (started && path.empty()) break;

        // A line can hold several entries: "Key = value, Other = value,".
        size_t at = 0;
        while (at < line.size()) {
            // Closing braces pop as many levels as they appear.
            if (line[at] == '}') {
                if (!path.empty()) path.pop_back();
                ++at;
                continue;
            }
            if (line[at] == '{') {
                // An unnamed table (the array forms elsewhere in the file).
                path.push_back("");
                ++at;
                continue;
            }
            if (line[at] == ',' || std::isspace(static_cast<unsigned char>(line[at]))) {
                ++at;
                continue;
            }

            const size_t eq = line.find('=', at);
            if (eq == std::string::npos) break;
            const std::string name = Trim(line.substr(at, eq - at));
            size_t valueStart = eq + 1;
            while (valueStart < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[valueStart])))
                ++valueStart;
            if (valueStart >= line.size()) {
                // "Name =" with the brace on the next line.
                if (IsIdentifier(name)) {
                    path.push_back(name);
                    started = true;
                }
                break;
            }

            if (line[valueStart] == '{') {
                if (IsIdentifier(name)) path.push_back(name);
                started = true;
                at = valueStart + 1;
                continue;
            }

            // A plain value runs to the next comma at brace depth zero.
            size_t valueEnd = valueStart;
            bool inString = false;
            while (valueEnd < line.size()) {
                const char c = line[valueEnd];
                if (c == '"') inString = !inString;
                if (!inString && (c == ',' || c == '}')) break;
                ++valueEnd;
            }
            const std::string value = Trim(line.substr(valueStart, valueEnd - valueStart));
            if (IsIdentifier(name) && !path.empty()) {
                if (value == "true" || value == "false") {
                    bools_[fullKey(name)] = (value == "true");
                } else {
                    double number = 0;
                    if (EvalNumber(value, number)) numbers_[fullKey(name)] = number;
                }
            }
            at = valueEnd;
        }
    }
}

float Tweaks::Number(const std::string& key, float fallback) const {
    auto it = numbers_.find(key);
    return it == numbers_.end() ? fallback : static_cast<float>(it->second);
}

bool Tweaks::Bool(const std::string& key, bool fallback) const {
    auto it = bools_.find(key);
    return it == bools_.end() ? fallback : it->second;
}

} // namespace painful
