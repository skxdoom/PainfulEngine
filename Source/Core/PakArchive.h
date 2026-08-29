#pragma once
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace painful {

// One shipped .pak archive (little-endian):
//   [0]      u8   version (0x01 = zlib entries, 0x00 = stored - Sounds.pak)
//   [1..4]   u32  directory offset
//   [dir]    u32  entry count, then per entry:
//              u32   name length
//              bytes obfuscated name: dec[j] = enc[j] XOR ((k0 + 2*j) & 0xFF)
//              u32   data offset (from file start)
//              u32   uncompressed size
//              u32   compressed size
// Entry data is a zlib stream (or stored raw when the sizes match and no zlib
// header is present). Names are forward-slash paths relative to the archive's
// category; directory entries end with '/' and have both sizes zero.
//
// k0 is a per-entry seed whose generator is unknown, so it is recovered by
// brute force: only a handful of the 256 seeds decode to printable ASCII, and
// scoring for "looks like an asset path" plus directory coherence with the
// neighbouring entries picks the right one. Validated by extracting every
// shipped archive and matching the content byte-for-byte (RE/tools/PakTool.ps1).
class PakArchive {
public:
    struct Entry {
        std::string name;              // decoded, '/'-separated; dirs end with '/'
        uint32_t offset = 0;
        uint32_t uncompressedSize = 0;
        uint32_t compressedSize = 0;
        bool isDirectory = false;
    };

    PakArchive() = default;
    ~PakArchive();
    PakArchive(const PakArchive&) = delete;
    PakArchive& operator=(const PakArchive&) = delete;

    // Parses and decodes the directory, and keeps the file handle open for
    // subsequent Read calls. On failure error() says why.
    bool Open(const std::string& path);

    // Decompresses one entry into out. Returns false for directory entries
    // and on any decode failure.
    bool Read(const Entry& e, std::vector<uint8_t>& out) const;

    const std::vector<Entry>& entries() const { return entries_; }
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }

private:
    std::string path_;
    std::string error_;
    std::vector<Entry> entries_;
    std::FILE* fp_ = nullptr;
    mutable std::mutex ioMutex_;   // guards seek+read on the shared handle
};

} // namespace painful
