#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace painful {

// Blend modes as the engine numbers them. The name table is Engine.dll's
// material-script parser (FUN_100973a0); D3Dev.dll's state setter turns each
// number into D3D render states, quoted here so the bgfx translation can be
// checked against the original rather than guessed:
//
//     mode              SRCBLEND      DESTBLEND        BLENDOP   ZWRITE
//     0  none           -             -                -         material
//     1  alpha          SRCALPHA      ONE              add       off
//     2  add            ONE           ONE              add       off
//     3  modulate       DESTCOLOR     ZERO             add       on
//     4  filter         ZERO          SRCCOLOR         add       on
//     5  translucent    SRCALPHA      INVSRCALPHA      add       material
//     6  invmodulate    ZERO          INVSRCCOLOR      add       off
//     7  subtract       ONE           ONE              sub       off
//     8  revsubtract    ONE           ONE              revsub    off
//     9  desttranslucent DESTALPHA    INVDESTALPHA     add       material
//     10 destalpha      DESTALPHA     ONE              add       off
//     11 modulate2x     DESTCOLOR     SRCCOLOR         add       on
//
// Note "alpha" is NOT ordinary alpha blending - it is additive weighted by the
// source alpha. "translucent" is the ordinary one.
enum BlendMode {
    kBlendNone = 0,
    kBlendAlpha = 1,
    kBlendAdd = 2,
    kBlendModulate = 3,
    kBlendFilter = 4,
    kBlendTranslucent = 5,
    kBlendInvModulate = 6,
    kBlendSubtract = 7,
    kBlendRevSubtract = 8,
    kBlendDestTranslucent = 9,
    kBlendDestAlpha = 10,
    kBlendModulate2x = 11,
};

// One Data/Scripts/Emitters/<name>.ini.
//
// Field-for-field from ParticleSystem::LoadEmitter (Engine.dll 0x100a4b40);
// every default below is the one ParticleEmitter's constructor installs, which
// is also exactly what Emitters/Default.ini spells out. Two keys the file
// format carries are parsed and then DISCARDED by the original, so they are
// deliberately absent here:
//
//   UseColorRange - the constructor hard-enables the colour-over-life ramp and
//                   LoadEmitter never clears it, so colour always interpolates
//                   Color.Min -> Color.Max across a particle's life. The
//                   per-particle random colour branch in InitParticle is dead
//                   code for anything loaded from an .ini.
//   Scale         - read with atof and thrown away; per-emitter scale comes
//                   from the .pfx entry instead.
struct EmitterParams {
    std::string name;                 // file stem, e.g. "Flame_factory1"

    std::string texture;              // General.Texture
    std::string material;             // General.Material
    std::string warpTex;              // General.WarpTex
    float texAnimFps = 10.f;          // General.TexAnimFPS
    int   type = 1;                   // 1 = camera-facing sprite, 2 = spark
    int   blendMode = kBlendAlpha;    // General.BlendMode, already remapped
    bool  randomNormal = false;       // General.UseRandomNormal
    bool  depthTest = true;           // General.DepthTest
    bool  evolve = true;              // General.Evolve - keep spawning forever
    bool  warp = false;               // General.Warp - wrap inside the PosRange box
    int   maxParticles = 128;         // General.MaxParticles
    // General.SpawnRate is stored inverted by the original: seconds between
    // particles, not particles per second. Rate 0 therefore means "never".
    float spawnInterval = 0.01f;
    float killDistSq = 0.f;           // General.KillDist, squared on load

    float editorPos[3] = {0, 0, 0};   // [EditorPosition] - authoring aid only

    float posMin[3] = {-1, -1, -1};   // [PosRange]
    float posMax[3] = {1, 1, 1};

    float velMin[3] = {-80, -6, 0};   // [Velocity] Min/Max
    float velMax[3] = {-64, 6, 0};
    // Acceleration.* writes BOTH ends of the range; AccelMax.* then overrides
    // the maximum, so a file with only Acceleration gets a constant.
    float accelMin[3] = {0, -0.1f, 0};
    float accelMax[3] = {0, -0.1f, 0};

    // [VelocityEnd] - the velocity a particle blends towards. Defaults to the
    // matching [Velocity] value, which makes the blend a no-op.
    float velEndMin[3] = {-80, -6, 0};
    float velEndMax[3] = {-64, 6, 0};

    float colorMin[3] = {0.5f, 0.1f, 0};   // [Color] Min/Max, 0..1 linear
    float colorMax[3] = {1.f, 0.5f, 0};
    float alphaMin = 0.8f, alphaMax = 0.f;
    float alphaMid = 0.f;             // defaults to AlphaMax when absent
    // Fade and velocity-blend timings are PERCENTAGES of a particle's life.
    // Both default to 100, i.e. the mid value is never reached.
    float fadeTimeMin = 100.f, fadeTimeMax = 100.f;
    float velBlendMin = 100.f, velBlendMax = 100.f;
    // Spin, radians per second. The constructor's -1..1 is zeroed by
    // LoadEmitter before reading, so an .ini without these does not spin.
    float rotMin = 0.f, rotMax = 0.f;

    float startSizeMin = 2.f, startSizeMax = 4.f;    // [SizeLife]
    float endSizeMin = 8.f, endSizeMax = 12.f;
    float lifeMin = 0.5f, lifeMax = 0.8f;
    bool  immortal = false;           // pins particles to the emitter, never dies

    // [SparkEmitter] - Type 2 only. Thickness is the quad's width across the
    // velocity; length multiplies the velocity vector to get its extent.
    float thicknessMin = 0.3f, thicknessMax = 0.5f;
    float lengthMin = 0.3f, lengthMax = 0.5f;
};

// One Data/Scripts/Effects/<name>.pfx: the list of emitters that make up a
// named effect, each with its own offset, rotation and scale.
//
//     ParticleFX =
//     {
//         Emitters =
//         {
//             { File = "Flame_factory1.ini", Scale = 1.00,
//               Position = {0,0,0}, Rotation = {0,0,0} },
//         },
//         FixedTransform = false
//     }
struct ParticleFxDef {
    struct Ref {
        std::string file;                  // emitter .ini name
        float scale = 1.f;
        float position[3] = {0, 0, 0};     // emitter-space offset from the entity
        float rotation[3] = {0, 0, 0};     // degrees, euler
    };
    std::string name;
    std::vector<Ref> emitters;
    bool fixedTransform = false;           // effect ignores the parent's transform
};

// Loads and caches both halves of the particle data, from Data/Scripts.
class EmitterLibrary {
public:
    // scriptsRoot is Data_Extracted/Scripts (holding Emitters/ and Effects/).
    bool Init(const std::string& scriptsRoot);

    // Both take a bare name ("Flame_factory1"); a trailing extension is fine.
    const EmitterParams* Emitter(const std::string& name);
    const ParticleFxDef* Effect(const std::string& name);

    size_t indexedEmitters() const { return emitterIndex_.size(); }
    size_t indexedEffects() const { return effectIndex_.size(); }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::map<std::string, std::string> emitterIndex_;   // lowercase stem -> path
    std::map<std::string, std::string> effectIndex_;
    std::map<std::string, std::unique_ptr<EmitterParams>> emitters_;
    std::map<std::string, std::unique_ptr<ParticleFxDef>> effects_;
    std::vector<std::string> errors_;
};

// Exposed for tests and diagnostics: the two file parsers.
bool ParseEmitterIni(const std::string& text, EmitterParams& out);
bool ParseParticleFx(const std::string& text, ParticleFxDef& out);

} // namespace painful
