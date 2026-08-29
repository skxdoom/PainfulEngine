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
    uint8_t header[5];
    if (std::fread(header, 1, 5, fp_) != 5) {
        error_ = "truncated header";
        return false;
    }
    uint32_t dirOff;
    std::memcpy(&dirOff, header + 1, 4);

    if (Seek64(fp_, 0) != 0) { error_ = "seek failed"; return false; }
#ifdef _WIN32
    _fseeki64(fp_, 0, SEEK_END);
    const uint64_t fileSize = static_cast<uint64_t>(_ftelli64(fp_));
#else
    fseeko(fp_, 0, SEEK_END);
    const uint64_t fileSize = static_cast<uint64_t>(ftello(fp_));
#endif
    if (dirOff >= fileSize) { error_ = "directory offset out of range"; return false; }

    // The directory is a contiguous tail region; read it whole.
    std::vector<uint8_t> dir(static_cast<size_t>(fileSize - dirOff));
    if (Seek64(fp_, dirOff) != 0 ||
        std::fread(dir.data(), 1, dir.size(), fp_) != dir.size()) {
        error_ = "cannot read directory";
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
    if (!u32(count)) { error_ = "truncated directory"; return false; }

    entries_.reserve(count);
    // Every printable decode of every name, kept for the coherence pass.
    std::vector<std::vector<std::pair<int, std::string>>> cands(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nameLen;
        if (!u32(nameLen) || p + nameLen > dir.size()) {
            error_ = "truncated entry name";
            return false;
        }
        const uint8_t* enc = dir.data() + p;
        p += nameLen;
        for (int k0 = 0; k0 < 256; ++k0) {
            std::string s = TryDecode(enc, nameLen, k0);
            if (!s.empty() || nameLen == 0) cands[i].emplace_back(k0, std::move(s));
            if (nameLen == 0) break;
        }
        Entry e;
        if (!u32(e.offset) || !u32(e.uncompressedSize) || !u32(e.compressedSize)) {
            error_ = "truncated entry";
            return false;
        }
        entries_.push_back(std::move(e));
    }

    // First pass: highest-scoring candidate per entry.
    for (uint32_t i = 0; i < count; ++i) {
        int best = INT_MIN;
        for (const auto& kv : cands[i]) {
            int sc = ScoreName(kv.second);
            if (sc > best) { best = sc; entries_[i].name = kv.second; }
        }
    }
    // Second pass: neighbours disambiguate near-ties - the arithmetic
    // keystream leaves several printable decodes for short names. Two
    // signals: sharing a confident neighbour's directory, and keeping the
    // archive's lexicographic order (the directory is stored sorted, which
    // is what separates true ties like "ntu.vso" vs "fog.vso" - identical
    // length, letter count and a known extension either way).
    for (uint32_t i = 0; i < count; ++i) {
        if (cands[i].size() <= 1) continue;
        const std::string prevDir = i > 0 ? DirOf(entries_[i - 1].name) : std::string();
        const std::string nextDir =
            i + 1 < count ? DirOf(entries_[i + 1].name) : std::string();
        const std::string prevName =
            i > 0 ? LowerCopy(entries_[i - 1].name) : std::string();
        const std::string nextName =
            i + 1 < count ? LowerCopy(entries_[i + 1].name) : std::string();
        int best = INT_MIN;
        for (const auto& kv : cands[i]) {
            int sc = ScoreName(kv.second);
            const std::string d = DirOf(kv.second);
            if (!d.empty() && CleanDir(d) && (d == prevDir || d == nextDir)) sc += 20;
            const std::string lc = LowerCopy(kv.second);
            if ((prevName.empty() || lc >= prevName) &&
                (nextName.empty() || lc <= nextName))
                sc += 10;
            if (sc > best) { best = sc; entries_[i].name = kv.second; }
        }
    }
    for (Entry& e : entries_)
        e.isDirectory = !e.name.empty() && e.name.back() == '/';
    return true;
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

} // namespace painful
