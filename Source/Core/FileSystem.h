#pragma once
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "PakArchive.h"

namespace painful {

struct DirEntry {
    std::string name;        // file or directory name, original case
    bool isDirectory = false;
};

// A unified view over the game's Data directory: the shipped .pak archives
// plus whatever loose files sit on disk. Callers keep using ordinary paths
// ("<DataRoot>/Levels/C1L1_Cathedral/...") - any path below the mounted root
// is looked up in the archives first and falls through to the disk, and any
// path outside the root is plain disk access. Lookups are case-insensitive
// and accept either separator, like the Windows filesystem the game grew up
// on.
//
// Mount order follows the original engine: "Textures.pak" serves
// "<DataRoot>/Textures/...", numbered archives override the base one
// (Textures2 > Textures1 > Textures), and archive entries take precedence
// over loose files with the same path.
class FileSystem {
public:
    static FileSystem& Get();

    // Scans dataDir for *.pak archives and mounts them, making dataDir the
    // root of the unified view. A directory with no archives (the extracted
    // reference tree) mounts nothing and every lookup falls through to disk.
    // Remounting the same root is a no-op. Returns the number of archives.
    size_t MountData(const std::string& dataDir);

    bool mounted() const { return !archives_.empty(); }
    const std::string& dataRoot() const { return dataRoot_; }

    // Reads a file served by a mounted archive. Returns false when the path
    // is outside the data root or in no archive; painful::ReadFile calls this
    // first and falls back to the physical file.
    bool ReadPakFile(const std::string& path, std::vector<uint8_t>& out) const;

    // Archive-aware replacements for the std::filesystem checks.
    bool Exists(const std::string& path) const;
    bool IsDirectory(const std::string& path) const;

    // Immediate children of a directory, archives and disk merged, sorted by
    // name. An archive entry shadows a loose entry with the same name.
    std::vector<DirEntry> List(const std::string& dir) const;

    // Every file below dir (archives and disk merged, sorted), as paths
    // RELATIVE to dir with '/' separators.
    std::vector<std::string> ListRecursive(const std::string& dir) const;

    // --- packs mounted at an arbitrary directory (FS.RegisterPack) --------
    // A save's Save.dat is registered over its own folder, so
    // "<dir>/LevelStart.Info" reads out of the pack while "<dir>/SaveGame.Info"
    // stays a loose file beside it. Returns a handle (> 0) or 0 on failure;
    // a pack's entries shadow loose files under the same directory.
    int MountPack(const std::string& pakPath, const std::string& dir);
    void UnmountPack(int handle);

    // --- writing (FS.File_*, FS.CreatePAK / ClosePAK) ---------------------
    // Between BeginPak and EndPak every WriteFile goes INTO the pak under its
    // basename, which is how the original's GFileManager::CreateFileWriter
    // behaves while a pak is open; otherwise WriteFile is a plain disk write.
    bool BeginPak(const std::string& pakPath);
    bool EndPak();
    bool pakOpen() const { return writer_.open(); }
    bool WriteFile(const std::string& path, const std::vector<uint8_t>& data);

private:
    struct FileRef {
        uint32_t archive = 0;
        uint32_t entry = 0;
    };

    FileSystem() = default;

    // Registers one archive entry under its virtual path (original case, no
    // trailing slash), creating the directory chain above it.
    void AddEntry(uint32_t archive, uint32_t entry, const std::string& virtualPath,
                  bool isDirectory);
    void EnsureDirChain(const std::string& dirPath);

    // Turns any spelling of a path below the data root into the lowercase
    // relative key used by the maps ("" for the root itself). False when the
    // path is outside the root or nothing is mounted.
    bool RelToRoot(const std::string& path, std::string& lcRel) const;

    std::string dataRoot_;       // absolute, normalized, '/'-separated
    std::string rootKey_;        // lowercase of dataRoot_
    std::vector<std::unique_ptr<PakArchive>> archives_;
    std::vector<std::string> mountPrefixes_;   // per archive, e.g. "Textures"

    // lowercase virtual path -> archive entry (later mounts overwrite).
    std::unordered_map<std::string, FileRef> files_;
    // lowercase directory path -> children keyed by lowercase name. The root
    // is "". std::map keeps each listing sorted.
    std::unordered_map<std::string, std::map<std::string, DirEntry>> tree_;

    struct ExtraMount {
        int handle = 0;
        std::string dirKey;            // lowercase absolute directory, no trailing slash
        std::unique_ptr<PakArchive> archive;
        std::unordered_map<std::string, uint32_t> entries;   // lowercase relative name -> entry
    };
    // The mount serving `path`, and the entry index, when a registered pack
    // holds it.
    const ExtraMount* FindExtra(const std::string& path, uint32_t* entry) const;
    std::vector<ExtraMount> extra_;
    int nextExtraHandle_ = 1;
    PakWriter writer_;
};

} // namespace painful
