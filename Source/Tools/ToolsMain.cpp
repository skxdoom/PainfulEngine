// PainfulTools - the diagnostics over a data root.
//
// Every subsystem has a command that resolves what it needs and prints what it
// found, so a change can be checked without opening a window. `run` is the one
// exception: it opens the hand-driven viewer.
//
// One table drives everything. Adding a command means adding a row: the help
// text prints itself from it, and each row declares where its data root sits,
// so the mounting cannot drift from the dispatch the way two parallel lists do.
#include "Commands.h"

#include <cstring>

namespace {

// Where in argv a command's data root is, if it takes one. Anything else is
// given the root its file-path argument points into.
enum class Root { kArgv2, kArgv3, kFromPath };

struct Command {
    const char* name;
    int         minArgs;   // including argv[0] and the command itself
    Root        root;
    const char* group;
    const char* args;      // argument spec, for the help text
    const char* summary;
    int       (*run)(int argc, char** argv);
};

// Captureless lambdas convert to plain function pointers, so each command keeps
// the signature that suits it and the table stays one row per command.
const Command kCommands[] = {

{"run", 4, Root::kArgv3, "viewer", "<levelDir> <DataRoot> [flags]",
 "open a level in the free-camera viewer",
 [](int argc, char** argv) {
     std::string shot;
     int cullMode = 0, entityCull = 1;
     bool skyOnly = false, novis = false, noclip = false, physicsDebug = false;
     float entityScale = 1.f;
     float pos[3], angles[2];
     bool hasPos = false, hasAngles = false;
     for (int i = 4; i < argc; ++i) {
         std::string arg = argv[i];
         if (arg == "--shot" && i + 1 < argc) shot = argv[++i];
         else if (arg == "--skyview") skyOnly = true;
         else if (arg == "--novis") novis = true;
         else if (arg == "--noclip") noclip = true;
         else if (arg == "--physdebug") physicsDebug = true;
         else if (arg == "--pos" && i + 3 < argc) {
             for (int k = 0; k < 3; ++k) pos[k] = float(std::atof(argv[i + 1 + k]));
             i += 3;
             hasPos = true;
         } else if (arg == "--escale" && i + 1 < argc) {
             entityScale = float(std::atof(argv[++i]));
         } else if (arg == "--ecull" && i + 1 < argc) {
             std::string mode = argv[++i];
             entityCull = (mode == "cw") ? 1 : (mode == "none") ? 2 : 0;
         } else if (arg == "--cull" && i + 1 < argc) {
             std::string mode = argv[++i];
             cullMode = (mode == "cw") ? 1 : (mode == "none") ? 2 : 0;
         } else if (arg == "--look" && i + 2 < argc) {
             angles[0] = float(std::atof(argv[i + 1]));
             angles[1] = float(std::atof(argv[i + 2]));
             i += 2;
             hasAngles = true;
         }
     }
     return RunCmd(argv[2], argv[3], shot, argv[0], hasPos ? pos : nullptr,
                   hasAngles ? angles : nullptr, cullMode, entityCull, entityScale,
                   skyOnly, novis, noclip, physicsDebug);
 }},

{"level", 4, Root::kArgv3, "level", "<levelDir> <DataRoot>",
 "level settings, entity counts, world mesh totals",
 [](int, char** argv) { return LevelCmd(argv[2], argv[3]); }},

{"entities", 4, Root::kArgv3, "level", "<levelDir> <DataRoot> [type]",
 "placed entities and how each one resolves",
 [](int argc, char** argv) { return EntitiesCmd(argv[2], argv[3], argc >= 5 ? argv[4] : ""); }},

{"fit", 4, Root::kArgv3, "level", "<levelDir> <DataRoot>",
 "entity/world fit report",
 [](int, char** argv) { return FitCmd(argv[2], argv[3]); }},

{"scale", 4, Root::kArgv3, "level", "<levelDir> <DataRoot>",
 "world scale sanity check",
 [](int, char** argv) { return ScaleCmd(argv[2], argv[3]); }},

{"levels", 3, Root::kArgv2, "level", "<DataRoot>",
 "list levels",
 [](int, char** argv) { return LevelsCmd(argv[2]); }},

{"files", 4, Root::kArgv2, "level", "<DataRoot> <dir>",
 "list a directory through the mounted view",
 [](int, char** argv) { return FilesCmd(argv[2], argv[3]); }},

{"pakcheck", 3, Root::kArgv2, "level", "<DataRoot>",
 "pak name seed formula vs brute-force decode, per archive",
 [](int, char** argv) { return PakCheckCmd(argv[2]); }},

{"zones", 4, Root::kArgv3, "level", "<levelDir> <DataRoot> [x y z]",
 "portal/zone graph, optionally from a point",
 [](int argc, char** argv) {
     float zp[3];
     const bool hasP = argc >= 7;
     if (hasP) for (int k = 0; k < 3; ++k) zp[k] = float(std::atof(argv[4 + k]));
     return ZonesCmd(argv[2], argv[3], hasP ? zp : nullptr);
 }},

{"ground", 8, Root::kArgv3, "level", "<levelDir> <DataRoot> <x y z> <radius>",
 "floor probe",
 [](int, char** argv) {
     return GroundCmd(argv[2], argv[3], float(atof(argv[4])), float(atof(argv[5])),
                      float(atof(argv[6])), float(atof(argv[7])));
 }},

{"lighting", 4, Root::kArgv3, "level", "<levelDir> <DataRoot> [x y z [ex ey ez]]",
 "the lights reaching a point",
 [](int argc, char** argv) {
     const float at[3] = {argc >= 7 ? float(atof(argv[4])) : 0.f,
                          argc >= 7 ? float(atof(argv[5])) : 0.f,
                          argc >= 7 ? float(atof(argv[6])) : 0.f};
     const float eye[3] = {argc >= 10 ? float(atof(argv[7])) : at[0] - 5.f,
                           argc >= 10 ? float(atof(argv[8])) : at[1] + 1.5f,
                           argc >= 10 ? float(atof(argv[9])) : at[2]};
     return LightingCmd(argv[2], argv[3], at, eye);
 }},

{"map", 3, Root::kFromPath, "geometry", "<file.mpk>",
 "objects, materials, bounds",
 [](int, char** argv) { return MapCmd(argv[2]); }},

{"mats", 3, Root::kFromPath, "geometry", "<file.mpk> [filter]",
 "material/lightmap sanity report",
 [](int argc, char** argv) { return MatsCmd(argv[2], argc >= 4 ? argv[3] : ""); }},

{"model", 3, Root::kFromPath, "geometry", "<file.pkmdl>",
 "meshes, skeleton, bounds",
 [](int, char** argv) { return ModelCmd(argv[2]); }},

{"bones", 3, Root::kFromPath, "geometry", "<file.pkmdl> [anim] [time] [joint:ax,ay,az]",
 "skeleton hierarchy",
 [](int argc, char** argv) {
     return BonesCmd(argv[2], argc >= 4 ? argv[3] : nullptr, argc >= 5 ? argv[4] : nullptr,
                     argc >= 6 ? argv[5] : nullptr);
 }},

{"hitboxes", 3, Root::kFromPath, "geometry", "<file.pkmdl>",
 "per-limb hit boxes",
 [](int, char** argv) { return HitboxesCmd(argv[2]); }},

{"dat", 3, Root::kFromPath, "geometry", "<file.dat | dir>",
 "item packs (a directory validates all)",
 [](int, char** argv) { return DatCmd(argv[2]); }},

{"textures", 5, Root::kArgv3, "textures", "<file.mpk> <DataRoot> <hint>",
 "which map textures resolve",
 [](int, char** argv) { return TexturesCmd(argv[2], argv[3], argv[4]); }},

{"resolve", 4, Root::kArgv2, "textures", "<DataRoot> <name>",
 "where a texture reference resolves",
 [](int, char** argv) { return ResolveCmd(argv[2], argv[3]); }},

{"texdump", 4, Root::kArgv2, "textures", "<DataRoot> <name> [out.tga]",
 "decode a texture, print corner pixels",
 [](int argc, char** argv) { return TexDumpCmd(argv[2], argv[3], argc >= 5 ? argv[4] : ""); }},

{"skytex", 4, Root::kArgv3, "textures", "<levelDir> <DataRoot>",
 "sky layer textures and whether they resolve",
 [](int, char** argv) { return SkyTexCmd(argv[2], argv[3]); }},

{"skydump", 3, Root::kFromPath, "textures", "<file.mpk>",
 "dome shells, UV ranges, material slots",
 [](int, char** argv) { return SkyDumpCmd(argv[2]); }},

{"shaders", 3, Root::kArgv2, "render", "<DataRoot> [name]",
 "material scripts; one name prints it resolved",
 [](int argc, char** argv) { return ShadersCmd(argv[2], argc >= 4 ? argv[3] : ""); }},

{"particles", 4, Root::kArgv3, "render", "<levelDir> <DataRoot>",
 "effect -> emitter chain, per-emitter parameters",
 [](int, char** argv) { return ParticlesCmd(argv[2], argv[3]); }},

{"billboards", 4, Root::kArgv3, "render", "<levelDir> <DataRoot>",
 "coronas and sprites, plus collision BVH timing",
 [](int, char** argv) { return BillboardsCmd(argv[2], argv[3]); }},

{"physics", 4, Root::kArgv3, "physics", "<levelDir> <DataRoot>",
 "the physics world, and probes of it",
 [](int, char** argv) { return PhysicsCmd(argv[2], argv[3]); }},

{"ragdoll", 3, Root::kFromPath, "physics", "<file.pkmdl> [modelsRoot]",
 "the .hke ragdoll, joint by joint",
 [](int argc, char** argv) { return RagdollCmd(argv[2], argc >= 4 ? argv[3] : nullptr); }},

{"ragdolldrop", 5, Root::kArgv3, "physics", "<levelDir> <DataRoot> <model>",
 "drop a ragdoll into the level and settle it",
 [](int, char** argv) { return RagdollDropCmd(argv[2], argv[3], argv[4]); }},

{"lua", 3, Root::kArgv2, "script", "<DataRoot> [frames] [level] [exec]",
 "boot the script layer, tick, report native calls",
 [](int argc, char** argv) {
     return LuaCmd(argv[2], argc >= 4 ? std::atoi(argv[3]) : 10, argc >= 5 ? argv[4] : nullptr,
                   argc >= 6 ? argv[5] : nullptr);
 }},

{"sound", 4, Root::kArgv2, "script", "<DataRoot> <name> [seconds]",
 "play one sound through the mixer",
 [](int argc, char** argv) { return SoundCmd(argv[2], argv[3], argc >= 5 ? argv[4] : nullptr); }},

{"wps", 3, Root::kFromPath, "script", "<file.wps>",
 "the waypoint graph",
 [](int, char** argv) { return WpsCmd(argv[2]); }},

{"pose", 4, Root::kFromPath, "script", "<file.pkmdl> <anim> [time]",
 "skinning, checked numerically",
 [](int argc, char** argv) { return PoseCmd(argv[2], argv[3], argc >= 5 ? argv[4] : nullptr); }},

{"blend", 5, Root::kFromPath, "script", "<file.pkmdl> <animA> <animB> [time]",
 "two animations blended",
 [](int argc, char** argv) { return BlendCmd(argv[2], argv[3], argv[4], argc >= 6 ? argv[5] : nullptr); }},

{"mklevel", 3, Root::kArgv2, "script", "<DataRoot> [name] [extent] [height] [tex] [stepHeights] [lightmap]",
 "write a complete level from code",
 [](int argc, char** argv) {
     return MkLevelCmd(argv[2], argc >= 4 ? argv[3] : "TestFloor",
                       argc >= 5 ? float(std::atof(argv[4])) : 200.f,
                       argc >= 6 ? float(std::atof(argv[5])) : 0.f,
                       argc >= 7 ? argv[6] : "beton_tile_all",
                       argc >= 8 ? argv[7] : nullptr,
                       argc >= 9 ? argv[8] : nullptr);
 }},
};

const char* const kGroups[] = {"viewer", "level", "geometry",
                               "textures", "render", "physics", "script"};

const char* GroupTitle(const char* group) {
    if (!std::strcmp(group, "viewer"))   return "Viewer";
    if (!std::strcmp(group, "level"))    return "A level";
    if (!std::strcmp(group, "geometry")) return "Geometry";
    if (!std::strcmp(group, "textures")) return "Textures";
    if (!std::strcmp(group, "render"))   return "Render inputs";
    if (!std::strcmp(group, "physics"))  return "Physics";
    return "Script";
}

int Usage() {
    LogInfo("PainfulTools - diagnostics over a Painkiller data root.");
    LogInfo("%s", "");
    for (const char* group : kGroups) {
        LogInfo("%s", GroupTitle(group));
        for (const Command& c : kCommands) {
            if (std::strcmp(c.group, group)) continue;
            LogInfo("  %-12s %-42s %s", c.name, c.args, c.summary);
        }
        LogInfo("%s", "");
    }
    return 1;
}

// The README's command tables, so they are copied rather than retyped.
int UsageMarkdown() {
    for (const char* group : kGroups) {
        LogInfo("### %s", GroupTitle(group));
        LogInfo("%s", "");
        LogInfo("%s", "| Command | Prints |");
        LogInfo("%s", "|---|---|");
        for (const Command& c : kCommands) {
            if (std::strcmp(c.group, group)) continue;
            LogInfo("| `%s %s` | %s |", c.name, c.args, c.summary);
        }
        LogInfo("%s", "");
    }
    return 0;
}

}  // namespace


int main(int argc, char** argv) {
    InstallCrashHandler("PainfulTools");
    if (argc < 2) return Usage();
    const std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h")
        return (argc >= 3 && std::string(argv[2]) == "--markdown") ? UsageMarkdown() : Usage();

    for (const Command& c : kCommands) {
        if (cmd != c.name) continue;
        if (argc < c.minArgs) {
            LogInfo("usage: PainfulTools %s %s", c.name, c.args);
            return 1;
        }
        // Mount before the command runs, so a path inside a .pak resolves too.
        switch (c.root) {
            case Root::kArgv2:    MountRoot(argv[2]); break;
            case Root::kArgv3:    MountRoot(argv[3]); break;
            case Root::kFromPath: MountForPath(argv[2], argv[0]); break;
        }
        return c.run(argc, argv);
    }
    LogInfo("unknown command '%s'", cmd.c_str());
    LogInfo("%s", "");
    return Usage();
}
