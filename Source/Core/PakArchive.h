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
// k0 = 2*nameLen + nameLen%5 + entryIndex, from the writer's scrambler at
// Engine.dll 0x101810c0 (GPack::Make and GFileManager::ClosePAK both call
// it). NameKey is that byte stream; the archive also keeps the older
// brute-force scorer as a fallback for a name the formula does not decode to
// printable ASCII. Docs/Reference/Formats.md, "Name obfuscation".
class PakArchive {
public:
    // The keystream byte for character j of an entry with this name length
    // and directory index. Shared with PakWriter, so the two cannot drift.
    static uint8_t NameKey(size_t nameLen, uint32_t entryIndex, size_t j) {
        return uint8_t(2 * (nameLen + j) + nameLen % 5 + entryIndex);
    }
    // Diagnostics: decode every name by brute-force scoring alone and count
    // how often that disagrees with the formula. Zero across the shipped
    // archives is what validates the formula.
    static size_t VerifyNameFormula(const std::string& path, size_t* entriesOut);

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

// Writes one archive in the same layout: the save system's Save.dat is a
// pak that GFileManager::CreatePAK opens, every File_Open between then and
// ClosePAK lands in, and ClosePAK finishes with the directory (Engine.dll
// 0x1017e6d0). Entries are held in memory and written whole at End, which is
// also when the header's directory offset is known.
class PakWriter {
public:
    bool Begin(const std::string& path);
    // Adds one file; `name` is stored as given (the original stores the bare
    // basename of what File_Open was asked for).
    bool Add(const std::string& name, const std::vector<uint8_t>& data);
    bool End();
    bool open() const { return open_; }
    const std::string& path() const { return path_; }

private:
    struct Entry {
        std::string name;
        uint32_t offset = 0, uncompressedSize = 0, compressedSize = 0;
    };
    std::string path_;
    std::vector<uint8_t> body_;
    std::vector<Entry> dir_;
    bool open_ = false;
};

} // namespace painful
