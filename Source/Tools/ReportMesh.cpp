// Geometry as it is stored: world meshes, models, skeletons and item packs.
#include "Commands.h"

int DatCmd(const char* path) {
    namespace fs = std::filesystem;
    if (FileSystem::Get().IsDirectory(path)) {
        size_t ok = 0, bad = 0;
        for (const std::string& rel : FileSystem::Get().ListRecursive(path)) {
            if (fs::path(rel).extension() != ".dat") continue;
            DatPack pack;
            if (DatPack::Load(std::string(path) + "/" + rel, pack)) {
                ++ok;
            } else {
                ++bad;
                LogInfo("FAIL %-40s %s", fs::path(rel).filename().string().c_str(),
                        pack.error.c_str());
            }
        }
        LogInfo("%zu parsed, %zu failed", ok, bad);
        return bad == 0 ? 0 : 3;
    }

    DatPack pack;
    if (!DatPack::Load(path, pack)) {
        LogInfo("%s: %s", path, pack.error.c_str());
        return 2;
    }
    LogInfo("%s: mesh %s, %zu objects", path, pack.meshName.c_str(), pack.objects.size());
    for (const MapObject& o : pack.objects) {
        const Mat4& t = o.transform;
        const bool ident = t.m[0] == 1.f && t.m[5] == 1.f && t.m[10] == 1.f &&
                           t.m[1] == 0.f && t.m[2] == 0.f && t.m[4] == 0.f;
        LogInfo("  %-28s %5zu verts %5zu tris  diffuse=%s  bbox %.2fx%.2fx%.2f  xform %s t=(%.2f %.2f %.2f)",
                o.name.c_str(), o.vertexCount(), o.triangleCount(),
                o.materials.empty() ? "" : o.materials[0].diffuse().c_str(),
                o.bboxMax[0] - o.bboxMin[0], o.bboxMax[1] - o.bboxMin[1],
                o.bboxMax[2] - o.bboxMin[2],
                ident ? "identity" : "ROTATED", t.m[12], t.m[13], t.m[14]);
        LogInfo("      y range [%.2f .. %.2f]  x [%.2f .. %.2f]  z [%.2f .. %.2f]",
                o.bboxMin[1], o.bboxMax[1], o.bboxMin[0], o.bboxMax[0],
                o.bboxMin[2], o.bboxMax[2]);
    }
    return 0;
}

// Diagnostic: parse the game's material scripts and dump what they define.
// Lists a directory through the mounted archives, and prints a file when the
// path names one. The .pak table of contents is hashed, so there is otherwise
// no way to see what the game data actually contains.

// The posed mesh's vertical extent per animation, against the bind pose. The
// original sizes a monster's body from its entity's local bounding-box
// minimum Y (PhysicsWorld::CreatePhysicsObject 0x101999F0, Entity+0x58);
// which pose that box reflects decides the body's size and where it stands.
// Docs/Reference/MonsterMovement.md, "The body".
int PoseBoundsCmd(const char* path) {
    namespace fs = std::filesystem;
    Model m;
    if (!Model::Load(path, m)) { LogInfo("failed"); return 2; }
    std::string dir = path, base = path;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) { dir = base.substr(0, slash); base = base.substr(slash + 1); }
    else dir = ".";
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string baseLower = lower(base);

    double bindLo = 1e30, bindHi = -1e30;
    for (const ModelMesh& mesh : m.meshes)
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            bindLo = std::min(bindLo, double(mesh.verts[i * 8 + 1]));
            bindHi = std::max(bindHi, double(mesh.verts[i * 8 + 1]));
        }
    LogInfo("%s: bind mesh y[%.2f..%.2f]", path, bindLo, bindHi);

    std::vector<Bone> bones = m.bones;
    BuildHierarchy(bones);
    std::vector<Mat4> bw, ib;
    ComputeBindWorld(bones, bw, ib);
    AnimationCache cache;
    cache.SetRoot(dir);
    double allLo = bindLo, allHi = bindHi;
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string fn = lower(entry.path().filename().string());
        if (fn.size() <= baseLower.size() + 5 || fn.compare(0, baseLower.size() + 1, baseLower + ".") != 0 ||
            fn.compare(fn.size() - 4, 4, ".ani") != 0)
            continue;
        names.push_back(fn.substr(baseLower.size() + 1, fn.size() - baseLower.size() - 5));
    }
    std::sort(names.begin(), names.end());
    std::vector<Mat4> posed, skin;
    std::vector<float> verts;
    for (const std::string& animName : names) {
        const Animation* anim = cache.Get(base, animName);
        if (!anim) continue;
        std::vector<const AnimTrack*> tracks;
        ResolveAnimTracks(bones, *anim, tracks);
        double lo = 1e30, hi = -1e30;
        const int samples = 12;
        for (int s = 0; s <= samples; ++s) {
            const float t = anim->duration() * float(s) / float(samples);
            ComputeBoneWorldAtTime(bones, tracks, t, posed);
            BoneWorldToSkinning(ib, posed, skin);
            for (const ModelMesh& mesh : m.meshes) {
                if (!mesh.hasSkin()) continue;
                SkinMeshVertices(mesh, skin, verts);
                for (size_t i = 0; i < mesh.vertexCount(); ++i) {
                    lo = std::min(lo, double(verts[i * 8 + 1]));
                    hi = std::max(hi, double(verts[i * 8 + 1]));
                }
            }
        }
        LogInfo("  %-16s y[%7.2f..%6.2f]", animName.c_str(), lo, hi);
        allLo = std::min(allLo, lo);
        allHi = std::max(allHi, hi);
    }
    LogInfo("  all poses y[%.2f..%.2f]  (bind %.2f..%.2f)", allLo, allHi, bindLo, bindHi);
    return 0;
}

int BonesCmd(const char* path, const char* animName, const char* timeArg,
                    const char* rotArg) {
    Model m;
    if (!Model::Load(path, m)) { LogInfo("failed"); return 2; }
    LogInfo("%s: %zu bones", path, m.bones.size());
    // Every bone, not the first handful: the question this answers is
    // usually about ONE named bone deep in the list - where a weapon or a
    // shield hangs off the rig - and a truncated dump cannot answer it.
    for (size_t i = 0; i < m.bones.size(); ++i) {
        const Bone& b = m.bones[i];
        const float* v = b.bind.m;
        double sx = std::sqrt(double(v[0])*v[0] + double(v[1])*v[1] + double(v[2])*v[2]);
        double sy = std::sqrt(double(v[4])*v[4] + double(v[5])*v[5] + double(v[6])*v[6]);
        double sz = std::sqrt(double(v[8])*v[8] + double(v[9])*v[9] + double(v[10])*v[10]);
        LogInfo("  [%zu] %-22s parent=%-3d scale(%.4f, %.4f, %.4f) trans(%.2f, %.2f, %.2f)",
                i, b.name.c_str(), b.parent, sx, sy, sz, v[12], v[13], v[14]);
    }
    // Compare the extent of the composed skeleton with the extent of the mesh.
    // These must agree; if they do not, the mesh is not in skeleton space.
    std::vector<Mat4> bindWorld, invBind;
    ComputeBindWorld(m.bones, bindWorld, invBind);
    double blo[3] = {1e30, 1e30, 1e30}, bhi[3] = {-1e30, -1e30, -1e30};
    for (const Mat4& w : bindWorld) {
        for (int c = 0; c < 3; ++c) {
            const double v = w.m[12 + c];
            if (v < blo[c]) blo[c] = v;
            if (v > bhi[c]) bhi[c] = v;
        }
    }
    double mlo[3] = {1e30, 1e30, 1e30}, mhi[3] = {-1e30, -1e30, -1e30};
    for (const ModelMesh& mesh : m.meshes) {
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            for (int c = 0; c < 3; ++c) {
                const double v = mesh.verts[i * 8 + c];
                if (v < mlo[c]) mlo[c] = v;
                if (v > mhi[c]) mhi[c] = v;
            }
        }
    }
    LogInfo("  skeleton extent : %.2f x %.2f x %.2f",
            bhi[0]-blo[0], bhi[1]-blo[1], bhi[2]-blo[2]);
    LogInfo("  mesh extent     : %.2f x %.2f x %.2f",
            mhi[0]-mlo[0], mhi[1]-mlo[1], mhi[2]-mlo[2]);
    // Named joints, posed. This is the joint natives' own arithmetic - what
    // MDL.GetJointPos answers before the entity transform is applied - so a
    // bone that ends up in the wrong place shows here rather than only as a
    // muzzle flash in the wrong spot.
    if (animName) {
        std::string dir = path, base = path;
        const size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos) { dir = base.substr(0, slash); base = base.substr(slash + 1); }
        else dir = ".";
        const size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);

        AnimationCache cache;
        cache.SetRoot(dir);
        const Animation* anim = cache.Get(base, animName);
        if (!anim) {
            LogInfo("  no animation %s.%s.ani in %s", base.c_str(), animName, dir.c_str());
            return 0;
        }
        std::vector<Bone> bones = m.bones;
        BuildHierarchy(bones);
        std::vector<Mat4> bw, ib;
        ComputeBindWorld(bones, bw, ib);
        std::vector<const AnimTrack*> tracks;
        ResolveAnimTracks(bones, *anim, tracks);
        const float t = timeArg ? float(std::atof(timeArg)) : anim->duration() * 0.5f;
        std::vector<Mat4> posed;
        ComputeBoneWorldAtTime(bones, tracks, t, posed);

        // "<joint>:<ax>,<ay>,<az>" applies MDL.ApplyJointRotation's own
        // override and reports which bones moved. A rotation at one joint must
        // move that bone's descendants and NOTHING else - the check that it
        // turns where it sits rather than swinging about its parent.
        if (rotArg) {
            JointOverride ov;
            if (std::sscanf(rotArg, "%d:%f,%f,%f", &ov.bone, &ov.euler[0], &ov.euler[1],
                            &ov.euler[2]) == 4) {
                std::vector<Mat4> turned;
                ComputeBoneWorldAtTime(bones, tracks, t, turned, &ov, 1);
                LogInfo("  joint %d turned by (%.3f %.3f %.3f) rad:", ov.bone,
                        ov.euler[0], ov.euler[1], ov.euler[2]);
                for (size_t i = 0; i < bones.size(); ++i) {
                    float d = 0.f;
                    for (int c = 0; c < 3; ++c)
                        d = std::max(d, std::fabs(turned[i].m[12 + c] - posed[i].m[12 + c]));
                    if (d > 1e-4f)
                        LogInfo("    [%2zu] %-20s parent=%-3d moved %.3f",
                                i, bones[i].name.c_str(), bones[i].parent, d);
                }
                return 0;
            }
            LogInfo("  could not read \"%s\" as <joint>:<ax>,<ay>,<az>", rotArg);
        }

        LogInfo("  joints at t=%.3f of %.3f, model space (bind -> posed):", t, anim->duration());
        // The root chain is what a reader needs: it must rise monotonically in
        // both columns, because a spine is a spine in any pose. A 111-bone
        // weapon would bury that under its own hardware.
        const size_t kShown = 12;
        for (size_t i = 0; i < bones.size() && i < kShown; ++i)
            LogInfo("    [%2zu] %-20s (%7.2f %7.2f %7.2f) -> (%7.2f %7.2f %7.2f)",
                    i, bones[i].name.c_str(),
                    bw[i].m[12], bw[i].m[13], bw[i].m[14],
                    posed[i].m[12], posed[i].m[13], posed[i].m[14]);
        if (bones.size() > kShown)
            LogInfo("    ... and %zu more not shown", bones.size() - kShown);
    }

    if (bhi[1] - blo[1] > 1e-6) {
        LogInfo("  mesh / skeleton height ratio: %.3f", (mhi[1]-mlo[1]) / (bhi[1]-blo[1]));
    }
    return 0;
}


// Diagnostic: do all four textures of every sky layer resolve to a file?

int MapCmd(const char* path, const char* nameFilter) {
    MapMesh m;
    MapMesh::Load(path, m);
    // With a filter: every object whose name contains it, with its raw bounds -
    // the way to find WHERE a named piece of the world is.
    if (nameFilter && nameFilter[0]) {
        size_t shown = 0;
        for (const MapObject& o : m.objects) {
            if (!o.nameHas(nameFilter)) continue;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (size_t i = 0; i < o.vertexCount(); ++i) {
                float p[3];
                o.position(i, p);
                for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], p[c]); hi[c] = std::max(hi[c], p[c]); }
            }
            LogInfo("  %-40s %5zu verts  x[%8.2f..%8.2f] y[%8.2f..%8.2f] z[%8.2f..%8.2f]%s",
                    o.name.c_str(), o.vertexCount(), lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
                    o.isCollidable() ? "" : "  (not collidable)");
            ++shown;
        }
        LogInfo("%zu objects match '%s' (raw mesh units; times the level scale for world)",
                shown, nameFilter);
        return 0;
    }
    size_t verts = 0, tris = 0;
    for (const MapObject& o : m.objects) { verts += o.vertexCount(); tris += o.triangleCount(); }

    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const MapObject& o : m.objects) {
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3];
            o.position(i, p);
            for (int c = 0; c < 3; ++c) { if (p[c] < lo[c]) lo[c] = p[c]; if (p[c] > hi[c]) hi[c] = p[c]; }
        }
    }
    LogInfo("%s: %zu objects, %zu verts, %zu tris, terminator %s",
            path, m.objects.size(), verts, tris, m.terminated ? "OK" : "MISSING");
    LogInfo("  bounds x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]  (size %.1f x %.1f x %.1f)",
            lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
            hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]);
    LogInfo("  parse stopped at 0x%zx of 0x%zx (%zu bytes unread)", m.parseEnd, m.size, m.size - m.parseEnd);
    LogInfo("  parser skipped %zu bytes across %zu regions", m.skippedBytes, m.skippedRegions);

    // Which way the exporter winds its triangles, measured rather than assumed:
    // for each triangle, does cross(b-a, c-a) agree with the vertex normals or
    // oppose them? PhysicsWorld reverses the winding on the strength of this,
    // and anything that WRITES a .mpk has to match it or the floor comes out
    // one-sided the wrong way.
    size_t agree = 0, oppose = 0;
    for (const MapObject& o : m.objects) {
        for (size_t t = 0; t + 2 < o.indices.size(); t += 3) {
            const uint32_t ia = o.indices[t], ib = o.indices[t + 1], ic = o.indices[t + 2];
            if (ia >= o.vertexCount() || ib >= o.vertexCount() || ic >= o.vertexCount()) continue;
            float a[3], b[3], c[3], n[3];
            o.position(ia, a); o.position(ib, b); o.position(ic, c);
            o.normal(ia, n);
            const float u[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
            const float v[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
            const float g[3] = {u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2],
                                u[0]*v[1] - u[1]*v[0]};
            const float d = g[0]*n[0] + g[1]*n[1] + g[2]*n[2];
            if (d > 0.f) ++agree; else if (d < 0.f) ++oppose;
        }
    }
    LogInfo("  winding: %zu triangles agree with their vertex normals, %zu oppose",
            agree, oppose);
    size_t nonIdentity = 0;
    std::map<std::string, size_t> translations;
    for (const MapObject& o : m.objects) {
        const Mat4& t = o.transform;
        const bool ident = t.m[0] == 1.f && t.m[5] == 1.f && t.m[10] == 1.f &&
                           t.m[12] == 0.f && t.m[13] == 0.f && t.m[14] == 0.f;
        if (ident) continue;
        ++nonIdentity;
        char buf[96];
        snprintf(buf, sizeof buf, "(%.1f, %.1f, %.1f) diag(%.2f %.2f %.2f)",
                 t.m[12], t.m[13], t.m[14], t.m[0], t.m[5], t.m[10]);
        ++translations[buf];
    }
    LogInfo("  %zu objects with non-identity transform", nonIdentity);
    size_t noMats = 0, noMatTris = 0, totalTris = 0;
    for (const MapObject& o : m.objects) {
        totalTris += o.triangleCount();
        if (o.materials.empty()) { ++noMats; noMatTris += o.triangleCount(); }
    }
    LogInfo("  %zu objects have NO parsed materials (%zu of %zu tris, %.1f%%)",
            noMats, noMatTris, totalTris, totalTris ? 100.0 * noMatTris / totalTris : 0.0);
    for (const auto& kv : translations) {
        if (kv.second > 2) LogInfo("    %zux %s", kv.second, kv.first.c_str());
    }
    std::vector<std::pair<size_t, size_t>> bySize(m.skips);
    std::sort(bySize.begin(), bySize.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < bySize.size() && i < 8; ++i) {
        LogInfo("    skip %zu bytes at offset 0x%zx", bySize[i].second, bySize[i].first);
    }
    return m.terminated ? 0 : 3;
}

// Diagnostic: how are the four texture slots actually populated?
int MatsCmd(const char* path, const char* nameFilter) {
    MapMesh m;
    MapMesh::Load(path, m);

    // With a name, report that object in full instead of the whole-map summary:
    // every slot with its UV transform, plus the raw UV span of the geometry.
    // Those two together are what decides how many times a texture repeats.
    if (nameFilter && *nameFilter) {
        std::string want = nameFilter;
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        size_t found = 0;
        for (const MapObject& o : m.objects) {
            std::string lower = o.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (lower.find(want) == std::string::npos) continue;
            ++found;
            float lo0[2] = {1e30f, 1e30f}, hi0[2] = {-1e30f, -1e30f};
            float lo1[2] = {1e30f, 1e30f}, hi1[2] = {-1e30f, -1e30f};
            for (size_t i = 0; i < o.vertexCount(); ++i) {
                float uv[2];
                o.uv(i, uv);
                for (int c = 0; c < 2; ++c) {
                    lo0[c] = std::min(lo0[c], uv[c]);
                    hi0[c] = std::max(hi0[c], uv[c]);
                }
                o.uv1(i, uv);
                for (int c = 0; c < 2; ++c) {
                    lo1[c] = std::min(lo1[c], uv[c]);
                    hi1[c] = std::max(hi1[c], uv[c]);
                }
            }
            LogInfo("%s  (%zu verts, %zu tris, uvChannels %u)", o.name.c_str(),
                    o.vertexCount(), o.triangleCount(), o.uvChannels);
            LogInfo("  bounds raw x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]",
                    o.bboxMin[0], o.bboxMax[0], o.bboxMin[1], o.bboxMax[1],
                    o.bboxMin[2], o.bboxMax[2]);
            LogInfo("  texcoord0 u[%.3f..%.3f] v[%.3f..%.3f]   span %.2f x %.2f",
                    lo0[0], hi0[0], lo0[1], hi0[1], hi0[0] - lo0[0], hi0[1] - lo0[1]);
            LogInfo("  texcoord1 u[%.3f..%.3f] v[%.3f..%.3f]   span %.2f x %.2f",
                    lo1[0], hi1[0], lo1[1], hi1[1], hi1[0] - lo1[0], hi1[1] - lo1[1]);
            // The raw 8 floats, so a suspected channel mix-up can be judged
            // rather than argued about: index 3 has no documented meaning on
            // 2-UV objects.
            LogInfo("  raw vertex floats (first 4 verts):");
            for (size_t i = 0; i < o.vertexCount() && i < 4; ++i) {
                LogInfo("    [%zu] %8.3f %8.3f %8.3f | %8.3f | %8.3f %8.3f | %8.3f %8.3f",
                        i, o.verts[i * 8 + 0], o.verts[i * 8 + 1], o.verts[i * 8 + 2],
                        o.verts[i * 8 + 3], o.verts[i * 8 + 4], o.verts[i * 8 + 5],
                        o.verts[i * 8 + 6], o.verts[i * 8 + 7]);
            }
            for (const Material& mat : o.materials) {
                for (int s = 0; s < 4; ++s) {
                    const TextureSlot& t = mat.slots[s];
                    if (t.name.empty()) continue;
                    LogInfo("  slot%d %-22s off(%.3f,%.3f) scale(%.3f,%.3f)  -> repeats %.1f x %.1f",
                            s, t.name.c_str(), t.offsetU, t.offsetV, t.scaleU, t.scaleV,
                            (hi0[0] - lo0[0]) * t.scaleU, (hi0[1] - lo0[1]) * t.scaleV);
                }
            }
        }
        if (!found) LogInfo("no object matching '%s'", nameFilter);
        return 0;
    }
    size_t total = 0, slot0empty = 0, slot1empty = 0, bothFilled = 0, slot0LooksLightmap = 0;
    size_t nonIdentity = 0;
    int shown = 0;
    LogInfo("materials with a non-identity slot0 UV transform:");
    for (const MapObject& o : m.objects) {
        for (const Material& mat : o.materials) {
            ++total;
            const std::string& s0 = mat.slots[0].name;
            const std::string& s1 = mat.slots[1].name;
            if (s0.empty()) ++slot0empty;
            if (s1.empty()) ++slot1empty;
            if (!s0.empty() && !s1.empty()) ++bothFilled;
            // Lightmaps are named after the mesh plus a lightmap suffix.
            if (s0.find("_L_") != std::string::npos) ++slot0LooksLightmap;
            const TextureSlot& t0 = mat.slots[0];
            bool identity = (t0.offsetU == 0.f && t0.offsetV == 0.f &&
                             t0.scaleU == 1.f && t0.scaleV == 1.f);
            if (!identity) {
                ++nonIdentity;
                if (shown++ < 10)
                    LogInfo("%-30s %-18s off(%.3f,%.3f) scale(%.3f,%.3f)",
                            o.name.c_str(), t0.name.c_str(),
                            t0.offsetU, t0.offsetV, t0.scaleU, t0.scaleV);
            }
        }
    }
    LogInfo("");
    LogInfo("materials %zu | slot0 empty %zu | slot1 empty %zu | both filled %zu",
            total, slot0empty, slot1empty, bothFilled);
    LogInfo("slot0 contains a lightmap-looking name (_L_): %zu", slot0LooksLightmap);
    LogInfo("materials whose slot0 UV transform is NOT identity: %zu", nonIdentity);
    size_t uv1WithLightmap = 0, uv1Total = 0, uv2Total = 0;
    for (const MapObject& o : m.objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (const Material& mat : o.materials) {
            if (o.uvChannels == 1) {
                ++uv1Total;
                if (!mat.slots[1].name.empty()) ++uv1WithLightmap;
            } else ++uv2Total;
        }
    }
    LogInfo("materials on 1-UV objects: %zu (of which %zu still name a lightmap)", uv1Total, uv1WithLightmap);
    LogInfo("materials on 2-UV objects: %zu", uv2Total);
    return 0;
}

// Diagnostic: where does every diffuse reference in a map actually resolve?

int HitboxesCmd(const char* modelPath) {
    Model model;
    if (!Model::Load(modelPath, model)) {
        LogInfo("failed to load %s", modelPath);
        return 2;
    }
    std::string rdePath = modelPath;
    const size_t dot = rdePath.find_last_of('.');
    if (dot != std::string::npos) rdePath = rdePath.substr(0, dot);
    rdePath += ".rde";

    Ragdoll ragdoll;
    if (!Ragdoll::Load(rdePath, ragdoll)) {
        LogInfo("%s: %s", rdePath.c_str(), ragdoll.error.c_str());
        return 2;
    }

    const std::vector<LimbBounds> limbs = BuildLimbBounds(model, ragdoll);
    LogInfo("%s: %zu bones, %zu limbs named by %s, %zu resolved", modelPath,
            model.bones.size(), ragdoll.limbs.size(), rdePath.c_str(), limbs.size());

    size_t missing = 0, unweighted = 0;
    for (const RagdollLimb& limb : ragdoll.limbs) {
        bool found = false;
        for (const Bone& bone : model.bones) if (bone.name == limb.bone) found = true;
        if (!found) { LogInfo("  NOT A BONE: %s", limb.bone.c_str()); ++missing; }
    }
    for (const LimbBounds& limb : limbs) {
        if (!limb.valid()) { ++unweighted; LogInfo("  %-20s no vertices", limb.name.c_str()); continue; }
        LogInfo("  %-20s %6zu verts   %.3f x %.3f x %.3f", limb.name.c_str(),
                limb.vertices, limb.extent(0), limb.extent(1), limb.extent(2));
    }
    LogInfo("  %zu named bones absent from the model, %zu limbs with no vertices",
            missing, unweighted);
    return 0;
}

int ModelCmd(const char* path) {
    Model model;
    if (!Model::Load(path, model)) { LogInfo("failed to load %s", path); return 2; }
    size_t verts = 0, tris = 0;
    for (const ModelMesh& mesh : model.meshes) {
        verts += mesh.vertexCount();
        tris += mesh.triangleCount();
    }

    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const ModelMesh& mesh : model.meshes) {
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            for (int c = 0; c < 3; ++c) {
                float v = mesh.verts[i * 8 + c];
                if (v < lo[c]) lo[c] = v;
                if (v > hi[c]) hi[c] = v;
            }
        }
    }
    LogInfo("%s: %zu bones, %zu meshes, %zu verts, %zu tris",
            path, model.bones.size(), model.meshes.size(), verts, tris);
    LogInfo("  bind-pose bounds x[%.2f..%.2f] y[%.2f..%.2f] z[%.2f..%.2f]  (size %.2f x %.2f x %.2f)",
            lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
            hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]);
    // Per-mesh names and material texture references. The mesh name is what a
    // .shader script keys off for a per-object material override, so it has to
    // be visible to tell why a model did or did not pick one up.
    // Do the normals point OUT? For a roughly closed character mesh most
    // vertices should have their normal pointing away from the model centre.
    // A majority pointing inward means the file's normals are inverted, which
    // no amount of tuning the lighting will fix.
    {
        double outward = 0, inward = 0, degenerate = 0;
        for (const ModelMesh& mesh : model.meshes) {
            double c[3] = {0, 0, 0};
            const size_t n = mesh.vertexCount();
            if (n == 0) continue;
            for (size_t i = 0; i < n; ++i)
                for (int a = 0; a < 3; ++a) c[a] += mesh.verts[i * 8 + a];
            for (int a = 0; a < 3; ++a) c[a] /= double(n);
            for (size_t i = 0; i < n; ++i) {
                double r[3], nv[3], dot = 0, rl = 0, nl = 0;
                for (int a = 0; a < 3; ++a) {
                    r[a] = mesh.verts[i * 8 + a] - c[a];
                    nv[a] = mesh.verts[i * 8 + 3 + a];
                    dot += r[a] * nv[a];
                    rl += r[a] * r[a];
                    nl += nv[a] * nv[a];
                }
                if (rl < 1e-12 || nl < 1e-12) { ++degenerate; continue; }
                if (dot > 0) ++outward; else ++inward;
            }
        }
        const double total = outward + inward;
        LogInfo("  normals: %.1f%% outward, %.1f%% inward, %.0f degenerate",
                total > 0 ? 100.0 * outward / total : 0.0,
                total > 0 ? 100.0 * inward / total : 0.0, degenerate);
    }

    for (const ModelMesh& mesh : model.meshes) {
        LogInfo("  mesh %-28s %6zu verts %6zu tris  skin %s  materials%s: %s",
                mesh.name.c_str(), mesh.vertexCount(), mesh.triangleCount(),
                mesh.hasSkin() ? "yes" : "no ", mesh.materialsExact ? "" : " (INEXACT)",
                [&] {
                    std::string s;
                    for (const ModelMaterial& m : mesh.materials) {
                        if (!s.empty()) s += ", ";
                        s += m.texture + "[" + std::to_string(m.firstIndex / 3) + ".." +
                             std::to_string(m.firstIndex / 3 + m.triangles) + ")";
                    }
                    return s.empty() ? std::string("(none)") : s;
                }().c_str());
        // Which bones drive this mesh, by vertex-weight share. The question a
        // detached hand raises is "what is this piece bound to", and it
        // cannot be answered from a picture.
        if (mesh.hasSkin() && !model.bones.empty()) {
            std::vector<double> share(model.bones.size(), 0.0);
            for (const std::vector<SkinInfluence>& v : mesh.skin)
                for (const SkinInfluence& inf : v)
                    if (inf.bone < share.size()) share[inf.bone] += inf.weight;
            std::string s;
            for (size_t b = 0; b < share.size(); ++b) {
                if (share[b] <= 0.0) continue;
                if (!s.empty()) s += ", ";
                char buf[96];
                std::snprintf(buf, sizeof buf, "%s %.0f%%", model.bones[b].name.c_str(),
                              100.0 * share[b] / double(mesh.vertexCount()));
                s += buf;
            }
            LogInfo("      bones: %s", s.c_str());
        }
    }
    return 0;
}

// Double-click launch: no arguments, so find the data ourselves and open the
// first campaign level. The [ ] keys cycle through every level from there.
