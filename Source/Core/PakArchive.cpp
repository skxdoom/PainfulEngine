#include "PakArchive.h"

#include <miniz.h>

#include <climits>
#include <cstring>
#include <utility>

namespace painful {

namespace {

// Portable 64-bit seek; long is 32 bits on Windows and Textures.pak is 1.2GB.
int Seek64(std::FILE* fp, uint64_t offset) {
#ifdef _WIN32
    return _fseeki64(fp, static_cast<long long>(offset), SEEK_SET);
#else
    return fseeko(fp, static_cast<off_t>(offset), SEEK_SET);
#endif
}

// Decode a name under one seed, or empty if any byte is non-printable.
std::string TryDecode(const uint8_t* enc, size_t n, int k0) {
    std::string s(n, '\0');
    for (size_t j = 0; j < n; ++j) {
        int c = enc[j] ^ ((k0 + 2 * static_cast<int>(j)) & 0xFF);
        if (c < 32 || c > 126) return {};
        s[j] = static_cast<char>(c);
    }
    return s;
}

// Every extension the shipped data uses (censused over a full extraction).
// Completeness matters: a real name whose extension is missing here can lose
// to a junk decode, and one wrong name then poisons its neighbours through
// the directory-coherence pass - that is exactly how ".rde" being absent
// turned "beast.pkmdl" into garbage and silently dropped models.
const char* const kKnownExt[] = {
    ".ani", ".ase", ".bin", ".bmp", ".bwd", ".caction", ".cactor",
    ".cacousticenv", ".carea", ".cbillboard", ".cbox", ".cenvironment",
    ".cfg", ".citem", ".clevel", ".clight", ".cmusicenv", ".cobject",
    ".cparticlefx", ".cplayer", ".cprocess", ".cspawnpoint", ".csound",
    ".cweapon", ".dat", ".dds", ".def", ".editor", ".emesh", ".evolumetric",
    ".exe", ".fnt", ".fx", ".fxo", ".hke", ".ini", ".lua", ".map", ".mopp",
    ".mpk", ".ogg", ".particlesdef", ".pbd", ".pcx", ".pfx", ".pkmdl",
    ".psh", ".pso", ".rde", ".scc", ".shader", ".shd", ".soundsdef",
    ".state", ".tga", ".ttf", ".txt", ".vsh", ".vso", ".wav", ".wps",
    ".xbox"};

// How much a decoded candidate "looks like a real asset path".
int ScoreName(const std::string& s) {
    int score = 0, weird = 0;
    for (char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9'))
            ++score;
        else if (c != '/' && c != '_' && c != '.' && c != '-' && c != ' ')
            ++weird;
    }
    score -= weird * 6;                                   // punctuation is rare
    if (s.find('/') != std::string::npos) score += 8;     // almost all files live in a subdir
    std::string low = s;
    for (char& c : low)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    for (const char* e : kKnownExt) {
        size_t n = std::strlen(e);
        if (low.size() >= n && low.compare(low.size() - n, n, e) == 0) {
            score += 12;
            break;
        }
    }
    if (!low.empty() && low.back() == '/') score += 6;    // directory entry
    return score;
}

std::string DirOf(const std::string& name) {
    size_t idx = name.find_last_of('/');
    return idx == std::string::npos ? std::string() : name.substr(0, idx);
}

// Only a directory made of ordinary path characters may hand out the
// coherence bonus. A junk decode can produce a slash, and two junk
// neighbours sharing a garbage "directory" would otherwise vote each other
// past the correct names.
bool CleanDir(const std::string& dir) {
    for (char c : dir) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '/' || c == '_' ||
                        c == '.' || c == '-' || c == ' ';
        if (!ok) return false;
    }
    return true;
}

std::string LowerCopy(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool LooksZlib(const uint8_t* d, size_t n) {
    return n >= 2 && d[0] == 0x78 &&
           (d[1] == 0x01 || d[1] == 0x9C || d[1] == 0xDA || d[1] == 0x5E);
}

// One directory entry as stored: the name still scrambled.
struct RawEntry {
    std::vector<uint8_t> enc;
    uint32_t offset = 0, uncompressedSize = 0, compressedSize = 0;
};

bool ReadDirectory(std::FILE* fp, std::vector<RawEntry>& out, std::string& error) {
    uint8_t header[5];
    if (Seek64(fp, 0) != 0 || std::fread(header, 1, 5, fp) != 5) {
        error = "truncated header";
        return false;
    }
    uint32_t dirOff;
    std::memcpy(&dirOff, header + 1, 4);
#ifdef _WIN32
    _fseeki64(fp, 0, SEEK_END);
    const uint64_t fileSize = static_cast<uint64_t>(_ftelli64(fp));
#else
    fseeko(fp, 0, SEEK_END);
    const uint64_t fileSize = static_cast<uint64_t>(ftello(fp));
#endif
    if (dirOff >= fileSize) { error = "directory offset out of range"; return false; }

    // The directory is a contiguous tail region; read it whole.
    std::vector<uint8_t> dir(static_cast<size_t>(fileSize - dirOff));
    if (Seek64(fp, dirOff) != 0 ||
        std::fread(dir.data(), 1, dir.size(), fp) != dir.size()) {
        error = "cannot read directory";
        return false;
    }

    size_t p = 0;
    auto u32 = [&](uint32_t& v) {
        if (p + 4 > dir.size()) return false;
        std::memcpy(&v, dir.data() + p, 4);
        p += 4;
        return true;
    };
    uint32_t count;
    if (!u32(count)) { error = "truncated directory"; return false; }
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RawEntry e;
        uint32_t nameLen;
        if (!u32(nameLen) || p + nameLen > dir.size()) {
            error = "truncated entry name";
            return false;
        }
        e.enc.assign(dir.data() + p, dir.data() + p + nameLen);
        p += nameLen;
        if (!u32(e.offset) || !u32(e.uncompressedSize) || !u32(e.compressedSize)) {
            error = "truncated entry";
            return false;
        }
        out.push_back(std::move(e));
    }
    return true;
}

// The writer's own keystream (PakArchive::NameKey), or empty when it yields
// anything non-printable.
std::string DecodeByFormula(const RawEntry& e, uint32_t index) {
    std::string s(e.enc.size(), '\0');
    for (size_t j = 0; j < e.enc.size(); ++j) {
        const int c = e.enc[j] ^ PakArchive::NameKey(e.enc.size(), index, j);
        if (c < 32 || c > 126) return {};
        s[j] = static_cast<char>(c);
    }
    return s;
}

// The scoring decoder: every printable seed per name, then the best-looking
// candidate, then a neighbour pass for near-ties (shared directory, and the
// archive's lexicographic order). Kept as the fallback and the verifier.
void DecodeByScoring(const std::vector<RawEntry>& raw, std::vector<std::string>& names) {
    const size_t count = raw.size();
    std::vector<std::vector<std::pair<int, std::string>>> cands(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t nameLen = raw[i].enc.size();
        for (int k0 = 0; k0 < 256; ++k0) {
            std::string s = TryDecode(raw[i].enc.data(), nameLen, k0);
            if (!s.empty() || nameLen == 0) cands[i].emplace_back(k0, std::move(s));
            if (nameLen == 0) break;
        }
    }
    names.assign(count, std::string());
    for (size_t i = 0; i < count; ++i) {
        int best = INT_MIN;
        for (const auto& kv : cands[i]) {
            int sc = ScoreName(kv.second);
            if (sc > best) { best = sc; names[i] = kv.second; }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (cands[i].size() <= 1) continue;
        const std::string prevDir = i > 0 ? DirOf(names[i - 1]) : std::string();
        const std::string nextDir = i + 1 < count ? DirOf(names[i + 1]) : std::string();
        const std::string prevName = i > 0 ? LowerCopy(names[i - 1]) : std::string();
        const std::string nextName = i + 1 < count ? LowerCopy(names[i + 1]) : std::string();
        int best = INT_MIN;
        for (const auto& kv : cands[i]) {
            int sc = ScoreName(kv.second);
            const std::string d = DirOf(kv.second);
            if (!d.empty() && CleanDir(d) && (d == prevDir || d == nextDir)) sc += 20;
            const std::string lc = LowerCopy(kv.second);
            if ((prevName.empty() || lc >= prevName) &&
                (nextName.empty() || lc <= nextName))
                sc += 10;
            if (sc > best) { best = sc; names[i] = kv.second; }
        }
    }
}

} // namespace

PakArchive::~PakArchive() {
    if (fp_) std::fclose(fp_);
}

bool PakArchive::Open(const std::string& path) {
    path_ = path;
#ifdef _MSC_VER
    if (fopen_s(&fp_, path.c_str(), "rb") != 0 || !fp_) {
        error_ = "cannot open";
        return false;
    }
#else
    fp_ = std::fopen(path.c_str(), "rb");
    if (!fp_) {
        error_ = "cannot open";
        return false;
    }
#endif
    std::vector<RawEntry> raw;
    if (!ReadDirectory(fp_, raw, error_)) return false;

    entries_.clear();
    entries_.reserve(raw.size());
    std::vector<std::string> scored;
    for (size_t i = 0; i < raw.size(); ++i) {
        Entry e;
        e.offset = raw[i].offset;
        e.uncompressedSize = raw[i].uncompressedSize;
        e.compressedSize = raw[i].compressedSize;
        e.name = DecodeByFormula(raw[i], uint32_t(i));
        if (e.name.empty() && !raw[i].enc.empty()) {
            if (scored.empty()) DecodeByScoring(raw, scored);
            e.name = scored[i];
        }
        e.isDirectory = !e.name.empty() && e.name.back() == '/';
        entries_.push_back(std::move(e));
    }
    return true;
}

size_t PakArchive::VerifyNameFormula(const std::string& path, size_t* entriesOut) {
    if (entriesOut) *entriesOut = 0;
    std::FILE* fp = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) return size_t(-1);
#else
    fp = std::fopen(path.c_str(), "rb");
    if (!fp) return size_t(-1);
#endif
    std::vector<RawEntry> raw;
    std::string error;
    const bool ok = ReadDirectory(fp, raw, error);
    std::fclose(fp);
    if (!ok) return size_t(-1);
    std::vector<std::string> scored;
    DecodeByScoring(raw, scored);
    size_t mismatches = 0;
    for (size_t i = 0; i < raw.size(); ++i)
        if (DecodeByFormula(raw[i], uint32_t(i)) != scored[i]) ++mismatches;
    if (entriesOut) *entriesOut = raw.size();
    return mismatches;
}

bool PakArchive::Read(const Entry& e, std::vector<uint8_t>& out) const {
    if (e.isDirectory || !fp_) return false;
    if (e.uncompressedSize == 0) { out.clear(); return true; }

    std::vector<uint8_t> comp(e.compressedSize);
    {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (Seek64(fp_, e.offset) != 0 ||
            std::fread(comp.data(), 1, comp.size(), fp_) != comp.size())
            return false;
    }
    // Stored entries (Sounds.pak, and any entry the packer left raw) are
    // detected per entry by the zlib magic, not by the archive version.
    if (e.compressedSize == e.uncompressedSize && !LooksZlib(comp.data(), comp.size())) {
        out = std::move(comp);
        return true;
    }
    out.resize(e.uncompressedSize);
    mz_ulong destLen = e.uncompressedSize;
    if (mz_uncompress(out.data(), &destLen, comp.data(),
                      static_cast<mz_ulong>(comp.size())) != MZ_OK ||
        destLen != e.uncompressedSize) {
        out.clear();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- writer

bool PakWriter::Begin(const std::string& path) {
    End();
    path_ = path;
    dir_.clear();
    // Header first: version 1, and the directory offset patched in by End -
    // GFileManager::CreatePAK (0x1017f0e0) writes the same five bytes.
    body_.assign(5, 0);
    body_[0] = 1;
    open_ = true;
    return true;
}

bool PakWriter::Add(const std::string& name, const std::vector<uint8_t>& data) {
    if (!open_) return false;
    Entry e;
    e.name = name;
    e.offset = uint32_t(body_.size());
    e.uncompressedSize = uint32_t(data.size());
    // Level 1: the shipped saves carry the 0x78 0x01 zlib header, and the
    // archive reader detects stored-vs-deflated per entry so it does not
    // matter for reading, only for matching what the original wrote.
    mz_ulong cap = mz_compressBound(mz_ulong(data.size()));
    std::vector<uint8_t> comp(static_cast<size_t>(cap), uint8_t(0));
    if (mz_compress2(comp.data(), &cap, data.data(), mz_ulong(data.size()), 1) != MZ_OK)
        return false;
    comp.resize(size_t(cap));
    e.compressedSize = uint32_t(comp.size());
    body_.insert(body_.end(), comp.begin(), comp.end());
    dir_.push_back(std::move(e));
    return true;
}

bool PakWriter::End() {
    if (!open_) return false;
    open_ = false;
    const uint32_t dirOff = uint32_t(body_.size());
    std::memcpy(body_.data() + 1, &dirOff, 4);
    auto u32 = [&](uint32_t v) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
        body_.insert(body_.end(), b, b + 4);
    };
    u32(uint32_t(dir_.size()));
    for (size_t i = 0; i < dir_.size(); ++i) {
        const Entry& e = dir_[i];
        u32(uint32_t(e.name.size()));
        for (size_t j = 0; j < e.name.size(); ++j)
            body_.push_back(uint8_t(e.name[j]) ^ PakArchive::NameKey(e.name.size(), uint32_t(i), j));
        u32(e.offset);
        u32(e.uncompressedSize);
        u32(e.compressedSize);
    }
    std::FILE* fp = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&fp, path_.c_str(), "wb") != 0 || !fp) return false;
#else
    fp = std::fopen(path_.c_str(), "wb");
    if (!fp) return false;
#endif
    const bool ok = std::fwrite(body_.data(), 1, body_.size(), fp) == body_.size();
    std::fclose(fp);
    body_.clear();
    dir_.clear();
    return ok;
}

} // namespace painful
