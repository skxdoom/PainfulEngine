#include "ShaderScript.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace painful {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Splits a line into whitespace-separated tokens, treating '=', '{' and '}'
// as tokens of their own. "//" starts a comment.
std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else if (c == '=' || c == '{' || c == '}') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            out.push_back(std::string(1, c));
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string Join(const std::vector<std::string>& tokens, size_t from) {
    std::string out;
    for (size_t i = from; i < tokens.size(); ++i) {
        if (!out.empty()) out += ' ';
        out += tokens[i];
    }
    return out;
}

} // namespace

bool ShaderLibrary::LoadDirectory(const std::string& dir) {
    namespace fs = std::filesystem;
    for (const DirEntry& entry : FileSystem::Get().List(dir)) {
        if (entry.isDirectory) continue;
        if (Lower(fs::path(entry.name).extension().string()) != ".shader") continue;
        const std::string path = dir + "/" + entry.name;
        std::vector<uint8_t> data;
        if (!ReadFile(path, data)) {
            errors_.push_back("cannot read " + path);
            continue;
        }
        const std::string& file = entry.name;
        std::string text(data.begin(), data.end());

        // The format is line-oriented: one statement per line, blocks opened
        // and closed by braces that may share a line with their header.
        ShaderDef def;
        ShaderPass pass;
        int depth = 0;                 // 0 = top level, 1 = shader, 2 = pass
        size_t lineStart = 0, lineNo = 0;
        while (lineStart <= text.size()) {
            size_t lineEnd = text.find('\n', lineStart);
            if (lineEnd == std::string::npos) lineEnd = text.size();
            ++lineNo;
            std::vector<std::string> t = Tokenize(text.substr(lineStart, lineEnd - lineStart));
            lineStart = lineEnd + 1;
            if (t.empty()) continue;

            // Brace tokens are processed in line order, interleaved with the
            // statement they belong to.
            size_t i = 0;
            while (i < t.size()) {
                const std::string& tok = t[i];
                if (tok == "{") { ++depth; ++i; continue; }
                if (tok == "}") {
                    --depth;
                    if (depth == 1) { def.passes.push_back(pass); pass = ShaderPass(); }
                    if (depth == 0) { def.sourceFile = file; defs_.push_back(def); def = ShaderDef(); }
                    ++i;
                    continue;
                }
                if (depth == 0) {
                    // shader[<variant>] NAME [copy BASE]
                    std::string head = Lower(tok);
                    if (head.rfind("shader", 0) != 0) {
                        errors_.push_back(file + ":" + std::to_string(lineNo) +
                                          ": expected 'shader', got '" + tok + "'");
                        i = t.size();
                        continue;
                    }
                    size_t lt = head.find('<'), gt = head.find('>');
                    if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
                        def.variant = head.substr(lt + 1, gt - lt - 1);
                    }
                    if (i + 1 < t.size() && t[i + 1] != "{") def.name = t[i + 1];
                    if (i + 3 < t.size() && Lower(t[i + 2]) == "copy") def.base = t[i + 3];
                    // Consume the rest of the header; braces were split out and
                    // will be handled on their own.
                    while (i < t.size() && t[i] != "{") ++i;
                    continue;
                }
                if (depth == 1) {
                    const std::string head = Lower(tok);
                    if (head == "setflag" && i + 1 < t.size()) {
                        def.flags.push_back(Lower(t[i + 1]));
                        i = t.size();
                        continue;
                    }
                    if (head != "pass") {
                        errors_.push_back(file + ":" + std::to_string(lineNo) +
                                          ": expected 'pass', got '" + tok + "'");
                    } else if (i + 2 < t.size() && Lower(t[i + 1]) == "copy" &&
                               Lower(t[i + 2]) == "previous" && !def.passes.empty()) {
                        // "pass copy previous" starts from the last pass's keys.
                        pass = def.passes.back();
                    }
                    while (i < t.size() && t[i] != "{") ++i;
                    continue;
                }
                // depth >= 2: a pass statement. Key, optional '=', values to
                // the end of the line.
                std::string key = Lower(tok);
                size_t v = i + 1;
                if (v < t.size() && t[v] == "=") ++v;
                pass.keys[key] = Join(t, v);
                i = t.size();
            }
        }
        if (depth != 0) {
            errors_.push_back(file + ": unbalanced braces at end of file");
        }
    }
    return errors_.empty() && !defs_.empty();
}

const ShaderDef* ShaderLibrary::FindRaw(const std::string& name,
                                        const std::vector<std::string>& variantOrder) const {
    const std::string wanted = Lower(name);
    for (const std::string& variant : variantOrder) {
        for (const ShaderDef& d : defs_) {
            if (Lower(d.name) == wanted && d.variant == variant) return &d;
        }
    }
    for (const ShaderDef& d : defs_) {
        if (Lower(d.name) == wanted && d.variant.empty()) return &d;
    }
    for (const ShaderDef& d : defs_) {
        if (Lower(d.name) == wanted) return &d;
    }
    return nullptr;
}

const ShaderDef* ShaderLibrary::Find(const std::string& name,
                                     const std::vector<std::string>& variantOrder) {
    const std::string cacheKey =
        Lower(name) + "|" + (variantOrder.empty() ? "" : variantOrder.front());
    auto cached = resolved_.find(cacheKey);
    if (cached != resolved_.end()) return &cached->second;

    const ShaderDef* raw = FindRaw(name, variantOrder);
    if (!raw) return nullptr;

    ShaderDef result = *raw;
    // Walk the copy chain, merging overrides pass-by-pass onto the base.
    // Bounded so a copy cycle cannot hang.
    for (int depth = 0; depth < 8 && !result.base.empty(); ++depth) {
        const ShaderDef* base = FindRaw(result.base, variantOrder);
        if (!base) {
            errors_.push_back(result.name + ": copy source '" + result.base + "' not found");
            break;
        }
        ShaderDef merged = *base;
        merged.name = result.name;
        merged.variant = result.variant.empty() ? base->variant : result.variant;
        merged.sourceFile = result.sourceFile;
        for (size_t p = 0; p < result.passes.size(); ++p) {
            if (p >= merged.passes.size()) {
                merged.passes.push_back(result.passes[p]);
                continue;
            }
            for (const auto& kv : result.passes[p].keys) {
                merged.passes[p].keys[kv.first] = kv.second;
            }
        }
        result = merged;
    }
    result.base.clear();
    return &(resolved_[cacheKey] = std::move(result));
}

} // namespace painful
