// What is in a level: settings, entities, placement and the visibility graph.
#include "LevelStats.h"
#include "Commands.h"

int LevelCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) {
        LogInfo("failed: %s", level.error().c_str());
        return 2;
    }
    const LevelInfo& i = level.info();

    LogInfo("level '%s'", level.name().c_str());
    LogInfo("  map          : %s%s", i.mapFile.c_str(), level.mapLoaded() ? "" : "  (not loaded)");
    LogInfo("  waypoints    : %s", i.waypointsFile.c_str());
    LogInfo("  scale        : %g", i.scale);
    LogInfo("  start pos    : (%.3f, %.3f, %.3f)", i.startPos[0], i.startPos[1], i.startPos[2]);
    LogInfo("  ambient      : (%.0f, %.0f, %.0f)", i.ambient[0], i.ambient[1], i.ambient[2]);
    LogInfo("  fog          : density %g, start %g, mode %d, colour (%.0f, %.0f, %.0f)",
            i.fogDensity, i.fogStart, i.fogMode, i.fogColor[0], i.fogColor[1], i.fogColor[2]);
    LogInfo("  mesh friction: %g", i.meshFriction);

    std::map<std::string, size_t> byType;
    for (const Entity& e : level.entities()) byType[e.type]++;
    LogInfo("  entities     : %zu", level.entities().size());
    for (const auto& kv : byType) LogInfo("      %-16s %zu", kv.first.c_str(), kv.second);

    if (level.mapLoaded()) {
        const LevelStats s = Summarise(level);
        LogInfo("  world mesh   : %zu objects, %zu verts, %zu tris, %zu materials",
                s.objects, s.verts, s.tris, s.materials);
        LogInfo("  collidable   : %zu objects (excludes noclip/portal/zone volumes)", s.collidable);
        LogInfo("  active meshes: %zu objects (%zu pinned), %zu material runs; "
                "%zu GPU buffers with the world's",
                s.active, s.activePinned, s.activeRuns, 2 * s.objects);
    }
    return 0;
}

// Diagnostic: where do placed entities sit relative to real world geometry?
// One property, printed the way the level file wrote it.
static std::string Describe(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Number: return std::to_string(v.number);
        case Value::Kind::Bool:   return v.boolean ? "true" : "false";
        case Value::Kind::String: return "\"" + v.text + "\"";
        case Value::Kind::Ctor: {
            std::string s = v.text + "(";
            for (size_t i = 0; i < v.args.size(); ++i)
                s += (i ? ", " : "") + std::to_string(v.args[i]);
            return s + ")";
        }
    }
    return "";
}

int EntitiesCmd(const char* levelDir, const char* dataRoot, const char* type) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");

    // `entities <level> <root> <Type>` dumps every property of the first few
    // instances of one entity class, which is how the light and environment
    // fields get read rather than guessed at.
    if (type && type[0]) {
        int shown = 0;
        for (const Entity& e : level.entities()) {
            if (e.type != type || shown >= 3) continue;
            ++shown;
            LogInfo("%s %s  base=%s  pos=(%.2f, %.2f, %.2f)", e.type.c_str(), e.name.c_str(),
                    e.baseObj.c_str(), e.pos[0], e.pos[1], e.pos[2]);
            for (const auto& kv : e.props.all())
                LogInfo("    %-28s %s", kv.first.c_str(), Describe(kv.second).c_str());
            // Then the BaseObj chain, because the instance only carries its
            // overrides: a CLight that never declares Type is a point light
            // because Point_White.CLight says so.
            std::string base = e.baseObj;
            for (int hop = 0; hop < 8 && !base.empty(); ++hop) {
                const Properties* t = templates.Find(base);
                if (!t) { LogInfo("  <- %s (not found)", base.c_str()); break; }
                LogInfo("  <- %s", base.c_str());
                for (const auto& kv : t->all())
                    LogInfo("       %-25s %s", kv.first.c_str(), Describe(kv.second).c_str());
                std::string next = t->String("BaseObj", "");
                if (next == base) break;
                base = next;
            }
        }
        // Three instances show the shape; the tally shows the spread - which
        // templates the level actually uses and every property any of them
        // sets, so a field that only a handful of lights carry is not missed.
        std::map<std::string, int> bases;
        std::map<std::string, int> keys;
        std::map<std::string, std::set<std::string>> scalars;
        int total = 0;
        for (const Entity& e : level.entities()) {
            if (e.type != type) continue;
            ++total;
            ++bases[e.baseObj];
            for (const auto& kv : e.props.all())
                LogInfo("    %-28s %s", kv.first.c_str(), Describe(kv.second).c_str());
        }
        LogInfo("%d %s total", total, type);
        for (const auto& kv : bases) LogInfo("  %5d  %s", kv.second, kv.first.c_str());
        LogInfo("properties set, by count (distinct scalar values in braces):");
        for (const auto& kv : keys) {
            std::string vals;
            auto it = scalars.find(kv.first);
            if (it != scalars.end() && it->second.size() <= 12) {
                for (const std::string& v : it->second) vals += (vals.empty() ? " {" : ", ") + v;
                vals += "}";
            }
            LogInfo("  %5d  %s%s", kv.second, kv.first.c_str(), vals.c_str());
        }
        if (shown == 0) LogInfo("no %s in this level", type);
        return 0;
    }

    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const MapObject& o : level.map().objects) {
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3];
            o.position(i, p);
            for (int c = 0; c < 3; ++c) { if (p[c] < lo[c]) lo[c] = p[c]; if (p[c] > hi[c]) hi[c] = p[c]; }
        }
    }
    LogInfo("world bounds x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]",
            lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    LogInfo("level start   (%.1f, %.1f, %.1f)",
            level.info().startPos[0], level.info().startPos[1], level.info().startPos[2]);
    LogInfo("");
    LogInfo("OUTLIERS ONLY (outside bounds, or >20 units from any geometry)");
    LogInfo("%-26s %-22s %9s %8s  %s", "entity", "position", "nearestGeo", "inBounds", "model");

    int shown = 0, outside = 0, far = 0;
    (void)shown;
    for (const Entity& e : level.entities()) {
        std::string model = templates.ResolveString(e.props, e.baseObj, "Model");
        if (model.empty()) continue;
        ++shown;

        // Distance to the closest world vertex, as a proxy for "is it in a room".
        double best = 1e30;
        float bx_ = 0, by_ = 0, bz_ = 0;
        for (const MapObject& o : level.map().objects) {
            for (size_t i = 0; i < o.vertexCount(); ++i) {
                float p[3];
                o.position(i, p);
                double dx = p[0] - e.pos[0], dy = p[1] - e.pos[1], dz = p[2] - e.pos[2];
                double d = dx * dx + dy * dy + dz * dz;
                if (d < best) { best = d; bx_ = p[0]; by_ = p[1]; bz_ = p[2]; }
            }
        }
        best = std::sqrt(best);
        const double tScale = templates.ResolveNumber(e.baseObj, "Scale", 1.0);
        const bool hasOwn = e.props.Has("Scale");
        const double fScale = hasOwn ? e.props.Number("Scale", tScale) : tScale;
        bool inBounds = e.pos[0] >= lo[0] && e.pos[0] <= hi[0] &&
                        e.pos[1] >= lo[1] && e.pos[1] <= hi[1] &&
                        e.pos[2] >= lo[2] && e.pos[2] <= hi[2];
        if (!inBounds) ++outside;
        if (best > 20.0) ++far;
        if (shown <= 14) {
            LogInfo("%-12s %-24s model=%-16s instScale=%-4s tmpl=%.3f final=%.3f",
                    e.type.c_str(), e.name.c_str(), model.c_str(),
                    hasOwn ? "yes" : "no", tScale, fScale);
        }
    }

    LogInfo("");
    LogInfo("placed with models: %d   outside world bounds: %d   further than 20 units from geometry: %d",
            shown, outside, far);
    return 0;
}

// Diagnostic: the .CLevel carries o.Pos and o.Ang. Do they place the world mesh?
// Try candidate transforms and see which seats entities closest to geometry.

int FitCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");

    std::vector<std::array<float, 3>> targets;
    for (const Entity& e : level.entities()) {
        if (templates.ResolveString(e.props, e.baseObj, "Model").empty()) continue;
        targets.push_back({e.pos[0], e.pos[1], e.pos[2]});
    }
    // Subsample the world; we only need a ranking, not exact distances.
    std::vector<std::array<float, 3>> verts;
    for (const MapObject& o : level.map().objects) {
        for (size_t i = 0; i < o.vertexCount(); i += 8) {
            float p[3];
            o.position(i, p);
            verts.push_back({p[0], p[1], p[2]});
        }
    }
    const float* pos = level.info().startPos;
    const float* ang = level.info().angles;
    LogInfo("entities %zu, sampled world verts %zu", targets.size(), verts.size());
    LogInfo("o.Pos (%.2f, %.2f, %.2f)   o.Ang (%.3f, %.3f, %.3f)",
            pos[0], pos[1], pos[2], ang[0], ang[1], ang[2]);
    LogInfo("");

    struct Candidate { const char* name; int axis; float degrees; bool translate; };
    const Candidate candidates[] = {
        {"identity",                    0,  0.f,      false},
        {"translate by Pos",            0,  0.f,      true },
        {"rotX(+Ang.x)",                0,  ang[0],   false},
        {"rotX(+Ang.x) + Pos",          0,  ang[0],   true },
        {"rotX(-Ang.x)",                0, -ang[0],   false},
        {"rotX(-Ang.x) + Pos",          0, -ang[0],   true },
        {"rotY(+Ang.x) + Pos",          1,  ang[0],   true },
        {"rotY(-Ang.x) + Pos",          1, -ang[0],   true },
        {"translate by -Pos",           0,  0.f,      false},
    };

    for (const Candidate& c : candidates) {
        const float r = c.degrees * 3.14159265f / 180.f;
        const float cs = std::cos(r), sn = std::sin(r);
        double total = 0;
        for (const auto& t : targets) {
            double best = 1e30;
            for (const auto& v : verts) {
                float x = v[0], y = v[1], z = v[2];
                float tx, ty, tz;
                if (c.axis == 0) { tx = x; ty = y * cs - z * sn; tz = y * sn + z * cs; }
                else             { tx = x * cs + z * sn; ty = y; tz = -x * sn + z * cs; }
                if (c.translate) { tx += pos[0]; ty += pos[1]; tz += pos[2]; }
                if (std::string(c.name) == "translate by -Pos") { tx -= pos[0]; ty -= pos[1]; tz -= pos[2]; }
                double dx = tx - t[0], dy = ty - t[1], dz = tz - t[2];
                double d = dx * dx + dy * dy + dz * dz;
                if (d < best) best = d;
            }
            total += std::sqrt(best);
        }
        LogInfo("  %-24s mean nearest-geometry distance: %8.2f", c.name, total / targets.size());
    }
    return 0;
}

// Boots the Lua 5.0.2 script layer: runs LScripts/Loader.lua with the
// recovered native surface installed as instrumented stubs, then follows the
// engine's own sequence - Game:Init() and per-frame ticks - and prints what
// the scripts actually called. This is the recovery loop for the native API:
// boot, read the report, implement what the game hit, repeat.
//
// Given a level it goes all the way into gameplay: LoadLevel then
// Game:OnPlay, so the player exists and the whole tick chain runs - actors,
// weapons, pickup polling, triggers. That is what makes the report a
// measurement of the GAME rather than of the boot. `exec` then runs one
// chunk between OnPlay and the first tick, which is how a scripted situation
// gets set up headlessly (teleport into a trigger, poke a template).

int LevelsCmd(const char* dataRoot) {
    std::vector<std::string> dirs;
    for (const DirEntry& entry : FileSystem::Get().List(std::string(dataRoot) + "/Levels")) {
        if (entry.isDirectory) dirs.push_back(entry.name);
    }
    std::sort(dirs.begin(), dirs.end());
    for (size_t i = 0; i < dirs.size(); ++i) {
        LogInfo("  [%2zu] %s", i + 1, dirs[i].c_str());
    }
    LogInfo("");
    LogInfo("%zu levels. Cycle them in-engine with the [ and ] keys.", dirs.size());
    return 0;
}

// Diagnostic: dump a sky dome mesh - object names and their materials.
// Diagnostic: parse a .dat item pack, or with a directory, validate every pack.

// Every *.pak under the data root: how many directory names the writer's seed
// formula (PakArchive::NameKey) decodes differently from the scoring decoder.
// Zero everywhere is what lets the formula be trusted; the count of entries
// is the evidence. Docs/Reference/Formats.md, "Name obfuscation".
int PakCheckCmd(const char* dataRoot) {
    size_t totalEntries = 0, totalMismatch = 0;
    int archives = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dataRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".pak") continue;
        size_t entries = 0;
        const size_t mism = PakArchive::VerifyNameFormula(entry.path().string(), &entries);
        if (mism == size_t(-1)) {
            LogInfo("%-20s unreadable", entry.path().filename().string().c_str());
            continue;
        }
        LogInfo("%-20s %7zu entries, %zu differ", entry.path().filename().string().c_str(),
                entries, mism);
        totalEntries += entries;
        totalMismatch += mism;
        ++archives;
    }
    LogInfo("%d archives, %zu entries, %zu decode differently", archives, totalEntries,
            totalMismatch);
    return totalMismatch == 0 ? 0 : 1;
}

int FilesCmd(const char* dataRoot, const char* dir) {
    FileSystem::Get().MountData(dataRoot);
    std::vector<uint8_t> data;
    if (ReadFile(dir, data)) {
        LogInfo("%s: %zu bytes", dir, data.size());
        std::string text(data.begin(), data.end());
        bool binary = false;
        for (unsigned char c : text)
            if (c != '\t' && c != '\n' && c != '\r' && (c < 32 || c > 126)) { binary = true; break; }
        if (binary) {
            // A .vso/.pso is compiled D3D shader bytecode: 32-bit tokens, the
            // first 0xFFFE0101 for vs_1_1. Dumped as tokens so the constant
            // registers a shader reads can be seen.
            LogInfo("  (binary, %zu bytes / %zu tokens)", data.size(), data.size() / 4);
            for (size_t i = 0; i + 4 <= data.size(); i += 4) {
                uint32_t t = uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8) |
                             (uint32_t(data[i + 2]) << 16) | (uint32_t(data[i + 3]) << 24);
                LogInfo("  %4zu  %08X", i / 4, t);
            }
            return 0;
        }
        else LogInfo("%s", text.c_str());
        return 0;
    }
    const std::vector<DirEntry> entries = FileSystem::Get().List(dir);
    LogInfo("%s: %zu entries", dir, entries.size());
    for (const DirEntry& e : entries)
        LogInfo("  %s%s", e.name.c_str(), e.isDirectory ? "/" : "");
    return entries.empty() ? 2 : 0;
}

// Reports what the model lighting solver sees at a point: the level's own
// ambient and directional, the CEnvironment box that overwrites them, and the
// four CLights it picked. Entities carry no lightmap, so this is the whole of
// their lighting and there is nowhere else to look when a model comes out
// black.
int LightingCmd(const char* levelDir, const char* dataRoot,
                       const float* at, const float* eye) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");

    const LevelInfo& info = level.info();
    LogInfo("level ambient   (%.0f, %.0f, %.0f)", info.ambient[0], info.ambient[1], info.ambient[2]);
    LogInfo("level dirlight  (%.0f, %.0f, %.0f) x %.2f  dir (%.2f, %.2f, %.2f)",
            info.dirLightColor[0], info.dirLightColor[1], info.dirLightColor[2],
            info.dirLightIntensity, info.dirLightDir[0], info.dirLightDir[1], info.dirLightDir[2]);

    EntityLighting lighting;
    lighting.Build(level, templates);
    LogInfo("%zu CLights, %zu CEnvironment boxes", lighting.lightCount(),
            lighting.environmentCount());

    float pos[3] = {at[0], at[1], at[2]};
    EntityLightFade fade;
    EntityLightState lit;
    lighting.Evaluate(pos, eye, 0.f, fade, lit);
    LogInfo("at (%.1f, %.1f, %.1f), eye (%.1f, %.1f, %.1f):", pos[0], pos[1], pos[2],
            eye[0], eye[1], eye[2]);
    LogInfo("  ambient   %.3f %.3f %.3f", lit.ambient[0], lit.ambient[1], lit.ambient[2]);
    LogInfo("  dirlight  %.3f %.3f %.3f   toward (%.2f, %.2f, %.2f)  (competes for a slot)",
            fade.dirColor[0], fade.dirColor[1], fade.dirColor[2],
            fade.dirDir[0], fade.dirDir[1], fade.dirDir[2]);
    for (int s = 0; s < kMaxEntityLights; ++s) {
        const EntityLightSlot& l = lit.slots[s];
        if (l.dir[3] < 0.5f) { LogInfo("  light %d   -", s); continue; }
        LogInfo("  light %d   %.3f %.3f %.3f  att %.3f  toward (%.2f, %.2f, %.2f)  %s",
                s, l.color[0], l.color[1], l.color[2], l.color[3],
                l.dir[0], l.dir[1], l.dir[2],
                l.half[3] < 0.5f ? "specular only" : "diffuse+specular");
    }
    // What the fragment shader would end up multiplying a white texel by, for a
    // normal facing straight at the camera.
    float toEye[3] = {eye[0] - pos[0], eye[1] - pos[1], eye[2] - pos[2]};
    const float n = std::sqrt(toEye[0] * toEye[0] + toEye[1] * toEye[1] + toEye[2] * toEye[2]);
    if (n > 1e-4f) for (int i = 0; i < 3; ++i) toEye[i] /= n;
    float diffuse[3] = {lit.ambient[0], lit.ambient[1], lit.ambient[2]};
    for (int s = 0; s < kMaxEntityLights; ++s) {
        const EntityLightSlot& l = lit.slots[s];
        if (l.dir[3] < 0.5f) continue;
        const float d = std::max(0.f, toEye[0] * l.dir[0] + toEye[1] * l.dir[1] + toEye[2] * l.dir[2]);
        for (int i = 0; i < 3; ++i) diffuse[i] += l.color[i] * d * l.half[3];
    }
    LogInfo("  => a camera-facing white texel lands at %.3f %.3f %.3f",
            diffuse[0], diffuse[1], diffuse[2]);
    return 0;
}


int ZonesCmd(const char* levelDir, const char* dataRoot,
                    const float* pos) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    const float ws = level.info().scale;
    ZoneGraph graph;
    graph.Build(level.map(), ws);
    LogInfo("%zu zones, %zu portals (world scale %.2f)", graph.zoneCount(),
            graph.portalCount(), ws);
    graph.Dump(ws);
    if (pos) {
        const float raw[3] = {pos[0] / ws, pos[1] / ws, pos[2] / ws};
        std::vector<int> zs;
        graph.ZonesAt(raw, zs);
        std::string s;
        for (int z : zs) s += std::to_string(z) + " ";
        LogInfo("point (%.1f %.1f %.1f) raw (%.1f %.1f %.1f) in zones: %s",
                pos[0], pos[1], pos[2], raw[0], raw[1], raw[2],
                s.empty() ? "(none)" : s.c_str());
    }
    return 0;
}

int GroundCmd(const char* levelDir, const char* dataRoot,
                     float x, float y, float z, float radius) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    float best = -1e30f;
    const float r2 = radius * radius;
    // Compare in the same space the renderer draws: world mesh times o.Scale.
    const float ws = level.info().scale;
    for (const MapObject& o : level.map().objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3], w[3];
            o.position(i, p);
            o.transform.TransformPoint(p[0], p[1], p[2], w);
            w[0] *= ws; w[1] *= ws; w[2] *= ws;
            const float dx = w[0] - x, dz = w[2] - z;
            if (dx * dx + dz * dz > r2 || w[1] > y) continue;
            if (w[1] > best) best = w[1];
        }
    }
    if (best < -1e29f) LogInfo("(%.2f, %.2f, %.2f): no ground below", x, y, z);
    else LogInfo("(%.2f, %.2f, %.2f): ground y=%.2f, drop %.2f", x, y, z, best, y - best);
    return 0;
}

int ScaleCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed"); return 2; }
    const float* start = level.info().startPos;

    // Nearest vertex below the spawn, within a small horizontal radius.
    double bestDrop = 1e30;
    float floorY = 0.f;
    for (const MapObject& o : level.map().objects) {
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone")) continue;
        for (size_t i = 0; i < o.vertexCount(); ++i) {
            float p[3];
            o.position(i, p);
            const double dx = p[0] - start[0], dz = p[2] - start[2];
            if (dx * dx + dz * dz > 25.0) continue;      // within 5 units horizontally
            if (p[1] > start[1]) continue;               // must be below
            const double drop = start[1] - p[1];
            if (drop < bestDrop) { bestDrop = drop; floorY = p[1]; }
        }
    }
    LogInfo("%s", level.name().c_str());
    LogInfo("  level Scale      : %g", level.info().scale);
    LogInfo("  spawn            : (%.2f, %.2f, %.2f)", start[0], start[1], start[2]);
    if (bestDrop > 1e29) {
        LogInfo("  no floor found within 5 units horizontally of the spawn");
        return 0;
    }
    LogInfo("  floor below      : y = %.2f", floorY);
    LogInfo("  camera height    : %.2f units", bestDrop);
    // A first-person camera sits around 1.65 m above the floor.
    LogInfo("  => units per metre: %.2f   (assuming a 1.65 m eye height)", bestDrop / 1.65);
    return 0;
}

// Diagnostic: do the bone bind matrices carry a scale the raw mesh lacks?
// Cross-fades one animation into another and reports where a bone ends up at
// each weight. This is how the blend MDL.SetAnim asks for gets checked: the
// intermediate poses must move monotonically from one animation to the other,
// and a bone must not change length on the way.
