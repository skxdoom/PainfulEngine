#include "Dat.h"

namespace painful {

namespace {

// u32 length (including the NUL) followed by the bytes.
bool ReadStr(Reader& r, std::string& out) {
    if (!r.ok(4)) return false;
    const uint32_t len = r.u32();
    if (len < 1 || len > 512 || !r.ok(len)) return false;
    out.assign(reinterpret_cast<const char*>(r.raw() + r.pos()), len - 1);
    r.seek(r.pos() + len);
    return true;
}

} // namespace

bool DatPack::Load(const std::string& path, DatPack& out) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data)) { out.error = "cannot read file"; return false; }
    out.size = data.size();
    Reader r(data.data(), data.size());

    // Header: a string table (the pack's own filename, then object mesh names
    // and class names like "WorldMesh"), then a table of contents. Each TOC
    // entry names an object by string-table index and locates its payload:
    //
    //   u32 stringCount, strings
    //   u32 objectCount
    //   per object: u32 unknown, u32 classNameIndex, u32 meshNameIndex,
    //               u32 payloadSize, u32 payloadOffset
    //
    // Offsets are contiguous and the last one ends exactly at the file's end.
    if (!r.ok(4)) { out.error = "truncated"; return false; }
    const uint32_t stringCount = r.u32();
    if (stringCount < 3 || stringCount > 512) { out.error = "bad string count"; return false; }
    std::vector<std::string> table(stringCount);
    for (uint32_t i = 0; i < stringCount; ++i) {
        if (!ReadStr(r, table[i])) { out.error = "bad header strings"; return false; }
    }
    if (stringCount > 1) out.meshName = table[1];

    if (!r.ok(4)) { out.error = "truncated header"; return false; }
    const uint32_t objectCount = r.u32();
    if (objectCount == 0 || objectCount > 512 || !r.ok(size_t(objectCount) * 20)) {
        out.error = "bad object count";
        return false;
    }
    struct TocEntry { uint32_t classIdx, nameIdx, size, offset; };
    std::vector<TocEntry> toc(objectCount);
    for (TocEntry& e : toc) {
        r.u32();                            // unknown, 0 in every file checked
        e.classIdx = r.u32();
        e.nameIdx = r.u32();
        e.size = r.u32();
        e.offset = r.u32();
    }
    if (toc[0].offset != r.pos() ||
        size_t(toc.back().offset) + toc.back().size != data.size()) {
        out.error = "size mismatch";
        return false;
    }

    for (const TocEntry& entry : toc) {
        // Only WorldMesh objects carry geometry this parser understands.
        if (entry.classIdx < stringCount && table[entry.classIdx] != "WorldMesh") continue;
        r.seek(entry.offset);
        MapObject o;
        if (!ReadStr(r, o.name)) { out.error = "bad object name"; break; }
        if (!r.ok(4 + 24 + 64)) { out.error = "truncated object " + o.name; break; }
        r.u32();                            // flags
        for (int i = 0; i < 3; ++i) o.bboxMin[i] = r.f32();
        for (int i = 0; i < 3; ++i) o.bboxMax[i] = r.f32();
        r.readMat4(o.transform);
        r.u32();                            // unknown

        // Lightmap name (or "notex"), one more unknown name, then the material
        // list: each entry covers a run of the index array, like .mpk.
        std::string lightmap, texB;
        if (!ReadStr(r, lightmap) || !ReadStr(r, texB)) { out.error = "bad textures"; break; }
        if (!r.ok(4)) { out.error = "truncated"; break; }
        const uint32_t matCount = r.u32();
        if (matCount > 256) { out.error = "bad material count"; break; }
        uint32_t triTotal = 0;
        for (uint32_t mi = 0; mi < matCount; ++mi) {
            Material mat;
            if (!ReadStr(r, mat.slots[0].name) || !r.ok(8)) { out.error = "bad material"; break; }
            if (lightmap != "notex") mat.slots[1].name = lightmap;
            mat.firstIndex = uint16_t(r.u32());
            const uint32_t triCount = r.u32();
            mat.triangleCount = uint16_t(triCount);
            triTotal += triCount;
            o.materials.push_back(mat);
        }
        if (!out.error.empty()) break;

        if (!r.ok(4)) { out.error = "truncated counts"; break; }
        // The material runs usually sum to the index count, but a few packs
        // leave a couple of triangles uncovered, so the count itself is the
        // authority.
        const uint32_t indexCount = r.u32();
        if (indexCount % 3 != 0 || !r.ok(size_t(indexCount) * 2)) {
            out.error = "bad index count in " + o.name;
            break;
        }
        (void)triTotal;
        o.indices.resize(indexCount);
        for (uint32_t i = 0; i < indexCount; ++i) o.indices[i] = r.u16();

        if (!r.ok(8)) { out.error = "truncated vertex count"; break; }
        r.u32();                            // unknown, 0 in every file checked
        const uint32_t vertCount = r.u32();
        if (!r.ok(size_t(vertCount) * 32)) { out.error = "bad vertex count in " + o.name; break; }
        o.uvChannels = 1;                   // pos, normal, uv - the .mpk 1-UV layout
        o.verts.resize(size_t(vertCount) * 8);
        for (size_t i = 0; i < o.verts.size(); ++i) o.verts[i] = r.f32();

        out.objects.push_back(std::move(o));
    }

    if (out.objects.empty() && out.error.empty()) out.error = "no objects";
    return out.error.empty();
}

} // namespace painful
