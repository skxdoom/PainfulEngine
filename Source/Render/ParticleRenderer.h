#pragma once
#include "../Assets/Emitter.h"
#include "../World/Level.h"
#include "../World/Templates.h"
#include "Camera.h"
#include "TextureCache.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>
#include <vector>

namespace painful {

// Simulates and draws the particle effects a level places.
//
// The data path is four files deep, and every step of it is in the shipped
// data rather than in code:
//
//   Levels/<lvl>/CParticleFX/Flame_001.CParticleFX   o.BaseObj, o.Pos, o.Scale
//     -> LScripts/Templates/ParticleFX/Flame.CParticleFX     o.Effect = "Pochodnia"
//        -> Scripts/Effects/pochodnia.pfx                    a list of emitters
//           -> Scripts/Emitters/swiecznik.ini                the actual parameters
//
// Simulation follows ParticleEmitter::Tick / InitParticle (Engine.dll
// 0x100a1fc0 / 0x100a1cc0) and the quad builder at 0x101e6040. Where this
// deviates - and it does in two places - the comment says so.
class ParticleRenderer {
public:
    ~ParticleRenderer() { Shutdown(); }

    bool Init(const std::string& shaderDir);
    void Shutdown();

    // Walks the level's CParticleFX entities and instantiates their emitters.
    void Build(const Level& level, TemplateCache& templates, EmitterLibrary& library,
               TextureCache& textures, const std::string& dataRoot);

    // Advances every emitter. dt is real seconds.
    void Tick(float dt);

    void Draw(bgfx::ViewId view, const Camera& camera, int width, int height);

    // Multiplies emitter positions and sizes, like EntityRenderer's - the level
    // o.Scale that the world mesh is drawn at.
    void SetScaleMultiplier(float k);
    float scaleMultiplier() const { return scaleMultiplier_; }

    size_t effects() const { return effects_; }
    size_t emitters() const { return emitters_.size(); }
    size_t liveParticles() const { return live_; }
    size_t unresolved() const { return unresolved_; }
    size_t drawCalls() const { return drawCalls_; }

private:
    // Mirrors the original's Particle, minus the intrusive list pointers.
    struct Particle {
        float pos[3];
        float vel[3];          // this frame's velocity, rebuilt from the blend
        float color[3];
        float accelVel[3];     // integral of accel, added on top of the blend
        float accel[3];        // per-particle constant, from the AccelMin/Max range
        float velStart[3], velEnd[3];
        float spawnDelta;      // sub-frame timestep for the frame it was born in
        float life, age;
        float size, alpha;
        float startSize, endSize;
        float rotSpeed, rotAngle;
        float animTime;
        float sparkThickness, sparkLength;   // Type 2 only
    };

    struct Emitter {
        const EmitterParams* params = nullptr;

        // The .pfx entry's Scale, times the entity scale, times the level
        // scale. ParticleEmitter::SetScale multiplies exactly these ranges and
        // leaves lifetimes, colours, alpha and spin alone.
        float posMin[3], posMax[3];
        float velMin[3], velMax[3];
        float velEndMin[3], velEndMax[3];
        float accelMin[3], accelMax[3];
        float startSizeMin, startSizeMax, endSizeMin, endSizeMax;
        float thicknessMin, thicknessMax, lengthMin, lengthMax;

        float pos[3] = {0, 0, 0};        // world position, this frame
        float prevPos[3] = {0, 0, 0};    // and last frame, for spawn interpolation
        // The owning entity's position, which is where Immortal pins particles
        // - it is the emitter position minus the .pfx entry's offset.
        float ownerPos[3] = {0, 0, 0};
        float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

        float spawnAccum = 0.f;
        int   spawnedTotal = 0;
        std::vector<Particle> particles;

        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        uint64_t blendState = 0;
    };

    // Rebuilds the scaled ranges from params, the way SetScale does.
    void ApplyScale(Emitter& e, float scale) const;
    void TickEmitter(Emitter& e, float dt);
    void InitParticle(const Emitter& e, Particle& p) const;

    std::vector<Emitter> emitters_;
    float scaleMultiplier_ = 1.f;
    size_t effects_ = 0, unresolved_ = 0, live_ = 0, drawCalls_ = 0;
    // The original draws with C rand(); this keeps the same uniform shape
    // without disturbing any other rand() user in the process.
    uint32_t rng_ = 0x9e3779b9u;

    float Rand01();
    float RandRange(float lo, float hi);
    void  RandVec(const float lo[3], const float hi[3], float out[3]);

    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sDiffuse_ = BGFX_INVALID_HANDLE;
};

} // namespace painful
