#include "ParticleRenderer.h"
#include "../Core/Common.h"
#include "../Core/Log.h"
#include "MaterialState.h"

#include <algorithm>
#include <bx/math.h>
#include <cmath>
#include <cstring>

namespace painful {

namespace {

bgfx::ShaderHandle LoadShader(const std::string& path) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) return BGFX_INVALID_HANDLE;
    return bgfx::createShader(bgfx::copy(data.data(), static_cast<uint32_t>(data.size())));
}

struct ParticleVertex {
    float x, y, z;
    uint32_t abgr;
    float u, v;
};

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// Rotates a vector by a row-vector 3x3, the convention the rest of the port
// uses for entity orientation.
void Rotate(const float m[9], const float v[3], float out[3]) {
    out[0] = v[0] * m[0] + v[1] * m[3] + v[2] * m[6];
    out[1] = v[0] * m[1] + v[1] * m[4] + v[2] * m[7];
    out[2] = v[0] * m[2] + v[1] * m[5] + v[2] * m[8];
}

// Euler degrees (the .pfx Rotation triple) to the same row-vector 3x3 form
// entity angles produce - Y then X then Z.
void EulerDegreesToMatrix(const float degrees[3], float out[9]) {
    const float k = 0.01745329252f;
    const float ax = degrees[0] * k, ay = degrees[1] * k, az = degrees[2] * k;
    const float sx = std::sin(ax), cx = std::cos(ax);
    const float sy = std::sin(ay), cy = std::cos(ay);
    const float sz = std::sin(az), cz = std::cos(az);
    out[0] = cy * cz + sy * sx * sz;  out[1] = cx * sz;  out[2] = -sy * cz + cy * sx * sz;
    out[3] = -cy * sz + sy * sx * cz; out[4] = cx * cz;  out[5] = sy * sz + cy * sx * cz;
    out[6] = sy * cx;                 out[7] = -sx;      out[8] = cy * cx;
}

void MatMul3(const float a[9], const float b[9], float out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3 + 0] * b[c] + a[r * 3 + 1] * b[3 + c] +
                             a[r * 3 + 2] * b[6 + c];
}

uint32_t PackAbgr(const float rgb[3], float alpha) {
    auto byteOf = [](float v) {
        const int i = static_cast<int>(v * 255.f + 0.5f);
        return static_cast<uint32_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
    };
    return (byteOf(alpha) << 24) | (byteOf(rgb[2]) << 16) | (byteOf(rgb[1]) << 8) | byteOf(rgb[0]);
}

}  // namespace

float ParticleRenderer::Rand01() {
    // xorshift32; the original calls rand()/RAND_MAX, which has the same
    // uniform shape but would perturb every other rand() user in the process.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ >> 8) * (1.f / 16777216.f);
}

float ParticleRenderer::RandRange(float lo, float hi) {
    return lo == hi ? lo : Lerp(lo, hi, Rand01());
}

void ParticleRenderer::RandVec(const float lo[3], const float hi[3], float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = RandRange(lo[i], hi[i]);
}

bool ParticleRenderer::Init(const std::string& shaderDir) {
    bgfx::ShaderHandle vs = LoadShader(shaderDir + "/vs_particle.bin");
    bgfx::ShaderHandle fs = LoadShader(shaderDir + "/fs_particle.bin");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        LogWarn("particles: missing vs_particle/fs_particle in %s", shaderDir.c_str());
        return false;
    }
    program_ = bgfx::createProgram(vs, fs, true);
    sDiffuse_ = bgfx::createUniform("s_diffuse", bgfx::UniformType::Sampler);

    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return bgfx::isValid(program_);
}

void ParticleRenderer::Shutdown() {
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(sDiffuse_)) bgfx::destroy(sDiffuse_);
    program_ = BGFX_INVALID_HANDLE;
    sDiffuse_ = BGFX_INVALID_HANDLE;
    emitters_.clear();
}

void ParticleRenderer::ApplyScale(Emitter& e, float scale) const {
    const EmitterParams& p = *e.params;
    // ParticleEmitter::SetScale (0x100a19a0) multiplies exactly this set and
    // nothing else: distances, velocities, accelerations, sizes and the spark
    // dimensions. Lifetimes, colours, alpha, fade timings and spin are left
    // alone, which is why a scaled-down flame still burns for as long.
    for (int i = 0; i < 3; ++i) {
        e.posMin[i] = p.posMin[i] * scale;
        e.posMax[i] = p.posMax[i] * scale;
        e.velMin[i] = p.velMin[i] * scale;
        e.velMax[i] = p.velMax[i] * scale;
        e.velEndMin[i] = p.velEndMin[i] * scale;
        e.velEndMax[i] = p.velEndMax[i] * scale;
        e.accelMin[i] = p.accelMin[i] * scale;
        e.accelMax[i] = p.accelMax[i] * scale;
    }
    e.startSizeMin = p.startSizeMin * scale;
    e.startSizeMax = p.startSizeMax * scale;
    e.endSizeMin = p.endSizeMin * scale;
    e.endSizeMax = p.endSizeMax * scale;
    e.thicknessMin = p.thicknessMin * scale;
    e.thicknessMax = p.thicknessMax * scale;
    e.lengthMin = p.lengthMin * scale;
    e.lengthMax = p.lengthMax * scale;
}

void ParticleRenderer::Build(const Level& level, TemplateCache& templates,
                             EmitterLibrary& library, TextureCache& textures,
                             const std::string& dataRoot) {
    (void)dataRoot;
    emitters_.clear();
    effects_ = 0;
    unresolved_ = 0;

    for (const Entity& entity : level.entities()) {
        if (entity.type != "CParticleFX") continue;

        // CParticleFX.lua declares Effect = "Default" as the class default and
        // instances rarely override it - the template names the effect.
        std::string effectName = entity.props.String("Effect", "");
        if (effectName.empty()) effectName = templates.ResolveString(entity.baseObj, "Effect");
        if (effectName.empty()) effectName = "Default";

        const ParticleFxDef* fx = library.Effect(effectName);
        if (!fx || fx->emitters.empty()) {
            ++unresolved_;
            continue;
        }

        // Same precedence EntityRenderer uses: the instance wins, then the
        // template chain, then the class default of 1.0.
        const double templateScale = templates.ResolveNumber(entity.baseObj, "Scale", 1.0);
        const float entityScale = static_cast<float>(
            entity.props.Has("Scale") ? entity.props.Number("Scale", templateScale)
                                      : templateScale);

        float entityRot[9];
        ReadRotation(entity.props, entityRot);

        ++effects_;
        for (const ParticleFxDef::Ref& ref : fx->emitters) {
            const EmitterParams* params = library.Emitter(ref.file);
            if (!params) {
                ++unresolved_;
                continue;
            }

            Emitter e;
            e.params = params;
            // Level placement always emits continuously, whatever the .ini
            // says: CParticleFX:LoadData follows every load with
            // PARTICLE.SetEvolve(entity, true). This path is the hand-driven
            // stand-in for that load, so it makes the same override - a
            // placed torch burns rather than puffing once.
            e.evolve = true;

            // EmitterDef::SetupTransform (0x101e4a60): the entry's offset is
            // scaled by the parent entity but NOT rotated by it, the
            // orientations compose, and the emitter's own scale is the product
            // of both. scaleMultiplier_ is this port's level-scale factor, the
            // same one EntityRenderer applies to placed models.
            float defRot[9];
            EulerDegreesToMatrix(ref.rotation, defRot);
            MatMul3(entityRot, defRot, e.rot);

            for (int i = 0; i < 3; ++i) {
                e.pos[i] = (entity.pos[i] + entityScale * ref.position[i]) * scaleMultiplier_;
                e.prevPos[i] = e.pos[i];
                e.ownerPos[i] = entity.pos[i] * scaleMultiplier_;
            }
            ApplyScale(e, entityScale * ref.scale * scaleMultiplier_);

            e.texture = textures.Get(params->texture, level.name());
            e.blendState = BlendModeState(params->blendMode);
            e.particles.reserve(std::min(params->maxParticles, 4096));
            emitters_.push_back(std::move(e));
        }
    }
    LogInfo("particles: %zu effects, %zu emitters (%zu unresolved)", effects_, emitters_.size(),
            unresolved_);
}

void ParticleRenderer::SetScaleMultiplier(float k) {
    if (k == scaleMultiplier_) return;
    // Rebuilding needs the source scale, which is folded into the ranges. The
    // simplest correct move is to rescale by the ratio, exactly as the layout
    // scales about world zero in EntityRenderer.
    const float ratio = scaleMultiplier_ != 0.f ? k / scaleMultiplier_ : k;
    scaleMultiplier_ = k;
    for (Emitter& e : emitters_) {
        for (int i = 0; i < 3; ++i) {
            e.pos[i] *= ratio;
            e.prevPos[i] *= ratio;
            e.ownerPos[i] *= ratio;
            e.posMin[i] *= ratio;   e.posMax[i] *= ratio;
            e.velMin[i] *= ratio;   e.velMax[i] *= ratio;
            e.velEndMin[i] *= ratio; e.velEndMax[i] *= ratio;
            e.accelMin[i] *= ratio; e.accelMax[i] *= ratio;
        }
        e.startSizeMin *= ratio; e.startSizeMax *= ratio;
        e.endSizeMin *= ratio;   e.endSizeMax *= ratio;
        e.thicknessMin *= ratio; e.thicknessMax *= ratio;
        e.lengthMin *= ratio;    e.lengthMax *= ratio;
        e.particles.clear();
    }
}

void ParticleRenderer::InitParticle(const Emitter& e, Particle& p) const {
    ParticleRenderer* self = const_cast<ParticleRenderer*>(this);
    const EmitterParams& src = *e.params;

    // The velocity pair is drawn first and both ends are rotated into world
    // space by the emitter's orientation; acceleration is NOT rotated.
    float v[3];
    self->RandVec(e.velEndMin, e.velEndMax, v);
    Rotate(e.rot, v, p.velEnd);
    self->RandVec(e.velMin, e.velMax, v);
    Rotate(e.rot, v, p.velStart);
    std::memcpy(p.vel, p.velStart, sizeof(p.vel));

    self->RandVec(e.accelMin, e.accelMax, p.accel);
    p.accelVel[0] = p.accelVel[1] = p.accelVel[2] = 0.f;

    // Colour is always the Min -> Max ramp: the constructor sets the
    // colour-range flag and LoadEmitter never clears it, so InitParticle's
    // random-colour branch is unreachable for .ini emitters. Seeded at Min so
    // a particle drawn before its first update is not black.
    std::memcpy(p.color, src.colorMin, sizeof(p.color));

    p.rotSpeed = self->RandRange(src.rotMin, src.rotMax);
    p.rotAngle = 0.f;
    p.life = self->RandRange(src.lifeMin, src.lifeMax);
    p.age = 0.f;
    p.animTime = 0.f;
    p.endSize = self->RandRange(e.endSizeMin, e.endSizeMax);
    p.startSize = self->RandRange(e.startSizeMin, e.startSizeMax);
    p.size = p.startSize;
    p.alpha = src.alphaMin;
    p.sparkThickness = 0.f;
    p.sparkLength = 0.f;
    if (src.type == 2) {
        p.sparkThickness = self->RandRange(e.thicknessMin, e.thicknessMax);
        p.sparkLength = self->RandRange(e.lengthMin, e.lengthMax);
    }
}

void ParticleRenderer::TickEmitter(Emitter& e, float dt) {
    const EmitterParams& src = *e.params;
    const int maxParticles = std::max(1, src.maxParticles);

    // ---------------------------------------------------------------- spawn
    //
    // Evolve = 0 makes an emitter a one-shot burst of MaxParticles: it emits
    // that many and never again. That is what an impact effect is, and
    // without it every bullet hole smokes forever. Level placement never
    // sees it - CParticleFX:LoadData calls PARTICLE.SetEvolve(entity, true)
    // right after loading, and Apply calls it again - so a placed torch keeps
    // burning whatever its .ini says.
    int count = 0;
    const bool exhausted = !e.evolve && e.spawnedTotal >= maxParticles;
    if (!exhausted && src.spawnInterval > 0.f) {
        e.spawnAccum += dt;
        if (e.spawnAccum >= src.spawnInterval) {
            count = static_cast<int>(e.spawnAccum / src.spawnInterval + 0.5f);
            const int used = e.evolve ? static_cast<int>(e.particles.size()) : e.spawnedTotal;
            if (used + count >= maxParticles) count = maxParticles - used;
            if (count < 0) count = 0;
            e.spawnAccum -= count * src.spawnInterval;
        }
    }
    // Evolve = 0 makes an emitter a one-shot burst of MaxParticles. Level
    // placement never sees that: CParticleFX:LoadData calls
    // PARTICLE.SetEvolve(entity, true) right after loading, and Apply calls it
    // again, so a placed effect always keeps emitting whatever the .ini says.

    for (int i = 1; i <= count; ++i) {
        const float f = static_cast<float>(i) / static_cast<float>(count);
        Particle p{};
        float offset[3], rotated[3];
        RandVec(e.posMin, e.posMax, offset);
        Rotate(e.rot, offset, rotated);
        // Spawns are spread along the path the emitter travelled this frame,
        // so a moving effect leaves a trail instead of a clump.
        for (int a = 0; a < 3; ++a)
            p.pos[a] = e.prevPos[a] + (e.pos[a] - e.prevPos[a]) * f + rotated[a];
        InitParticle(e, p);
        // Sub-frame timestep for the frame it was born in. The original
        // indexes this off the PREVIOUS loop counter, so the particle placed
        // furthest back along the path gets a zero step and the newest gets
        // very nearly a whole frame. Reproduced as-is.
        p.spawnDelta = static_cast<float>(i - 1) * (dt / static_cast<float>(count));
        e.particles.push_back(p);
        ++e.spawnedTotal;
    }

    // --------------------------------------------------------------- update
    const float wrapLo[3] = {e.pos[0] + e.posMin[0], e.pos[1] + e.posMin[1],
                             e.pos[2] + e.posMin[2]};
    const float wrapHi[3] = {e.pos[0] + e.posMax[0], e.pos[1] + e.posMax[1],
                             e.pos[2] + e.posMax[2]};
    const float wrapSpan[3] = {e.posMax[0] - e.posMin[0], e.posMax[1] - e.posMin[1],
                               e.posMax[2] - e.posMin[2]};

    size_t out = 0;
    for (size_t i = 0; i < e.particles.size(); ++i) {
        Particle p = e.particles[i];
        // -1 is the "already had its first update" marker the original writes
        // (as -2) once a particle has been stepped at least once.
        const float step = p.spawnDelta <= -1.f ? dt : p.spawnDelta;
        p.age += step;
        if (p.age > p.life) {
            if (!src.immortal) continue;      // dead: drop it
            p.age = 0.f;                      // immortal particles simply restart
        }
        const float life = p.life > 1e-6f ? p.life : 1e-6f;
        const float t = p.age / life;
        const float pct = t * 100.f;

        // Velocity blends from the [Velocity] draw to the [VelocityEnd] draw
        // between the two VelBlend percentages of the particle's life.
        if (pct < src.velBlendMin) {
            std::memcpy(p.vel, p.velStart, sizeof(p.vel));
        } else if (pct >= src.velBlendMax) {
            std::memcpy(p.vel, p.velEnd, sizeof(p.vel));
        } else {
            const float lo = src.velBlendMin * life * 0.01f;
            const float hi = src.velBlendMax * life * 0.01f;
            const float k = hi > lo ? (p.age - lo) / (hi - lo) : 1.f;
            for (int a = 0; a < 3; ++a) p.vel[a] = Lerp(p.velStart[a], p.velEnd[a], k);
        }
        // Acceleration accumulates into its own velocity and rides on top of
        // the blend, so it survives the blend snapping between endpoints.
        for (int a = 0; a < 3; ++a) {
            p.accelVel[a] += p.accel[a] * step;
            p.vel[a] += p.accelVel[a];
        }

        // Alpha is a three-point curve: Min -> Mid over the first FadeTimeMin
        // percent, flat at Mid until FadeTimeMax percent, then Mid -> Max.
        // Both timings default to 100, which collapses it to a plain Min -> Mid
        // ramp - and Mid defaults to Max.
        if (pct < src.fadeTimeMin) {
            const float span = src.fadeTimeMin * life * 0.01f;
            p.alpha = Lerp(src.alphaMin, src.alphaMid, span > 0.f ? p.age / span : 1.f);
        } else if (pct < src.fadeTimeMax) {
            p.alpha = src.alphaMid;
        } else {
            const float from = src.fadeTimeMax * life * 0.01f;
            const float span = life - from;
            p.alpha = Lerp(src.alphaMid, src.alphaMax, span > 1e-6f ? (p.age - from) / span : 1.f);
        }

        for (int a = 0; a < 3; ++a) p.pos[a] += p.vel[a] * step;

        // Immortal particles are pinned to the owning entity every frame -
        // they are a ring around an object, not a stream leaving it.
        if (src.immortal) std::memcpy(p.pos, e.ownerPos, sizeof(p.pos));

        // Warp wraps a particle back into the PosRange box around the emitter,
        // which is how the rain and mist emitters keep a volume filled.
        if (src.warp) {
            for (int a = 0; a < 3; ++a) {
                if (p.pos[a] > wrapHi[a]) p.pos[a] -= wrapSpan[a];
                else if (p.pos[a] < wrapLo[a]) p.pos[a] += wrapSpan[a];
            }
        }

        p.size = Lerp(p.startSize, p.endSize, t);
        if (p.rotSpeed != 0.f) p.rotAngle += p.rotSpeed * step;
        for (int a = 0; a < 3; ++a) p.color[a] = Lerp(src.colorMin[a], src.colorMax[a], t);
        p.animTime += step;
        p.spawnDelta = -2.f;

        e.particles[out++] = p;
    }
    e.particles.resize(out);

    std::memcpy(e.prevPos, e.pos, sizeof(e.prevPos));
}

void ParticleRenderer::Tick(float dt) {
    if (dt <= 0.f) return;
    // A long hitch (level load, breakpoint) would otherwise spawn a whole
    // emitter's worth of particles in one step and integrate them off-screen.
    dt = std::min(dt, 0.1f);
    live_ = 0;
    for (Emitter& e : emitters_) {
        if (!e.alive || !e.visible) continue;
        TickEmitter(e, dt);
        live_ += e.particles.size();
    }
}

int ParticleRenderer::AddScriptEmitter(const std::string& emitterFile,
                                       EmitterLibrary& library, TextureCache& textures,
                                       const std::string& levelHint) {
    const EmitterParams* params = library.Emitter(emitterFile);
    if (!params) {
        ++unresolved_;
        return -1;
    }
    Emitter e;
    e.params = params;
    e.evolve = params->evolve;
    e.texture = textures.Get(params->texture, levelHint);
    e.blendState = BlendModeState(params->blendMode);
    e.particles.reserve(std::min(params->maxParticles, 4096));
    ApplyScale(e, scaleMultiplier_);
    emitters_.push_back(std::move(e));
    return int(emitters_.size() - 1);
}

bool ParticleRenderer::ScriptEmitterFinished(int slot) const {
    if (slot < 0 || size_t(slot) >= emitters_.size()) return true;
    const Emitter& e = emitters_[slot];
    if (!e.alive) return true;
    if (e.evolve) return false;                 // still emitting
    const int cap = std::max(1, e.params->maxParticles);
    return e.spawnedTotal >= cap && e.particles.empty();
}

void ParticleRenderer::SetScriptEmitterEvolve(int slot, bool evolve) {
    if (slot >= 0 && size_t(slot) < emitters_.size()) emitters_[slot].evolve = evolve;
}

void ParticleRenderer::StopScriptEmitter(int slot) {
    if (slot < 0 || size_t(slot) >= emitters_.size()) return;
    Emitter& e = emitters_[slot];
    e.evolve = false;
    e.spawnedTotal = std::max(e.spawnedTotal, std::max(1, e.params->maxParticles));
}

void ParticleRenderer::SetupScriptEmitter(int slot, float refScale,
                                          const float refOffset[3],
                                          const float refRotDegrees[3]) {
    if (slot < 0 || size_t(slot) >= emitters_.size()) return;
    Emitter& e = emitters_[slot];
    for (int i = 0; i < 3; ++i) {
        e.refOffset[i] = refOffset[i];
        e.refRotDeg[i] = refRotDegrees[i];
    }
    e.refScale = refScale > 0.f ? refScale : 1.f;
    RecomposeScript(e);
}

void ParticleRenderer::SetScriptEmitterOwner(int slot, const float ownerPos[3],
                                             const float ownerRot9[9],
                                             float entityScale, bool visible) {
    if (slot < 0 || size_t(slot) >= emitters_.size()) return;
    Emitter& e = emitters_[slot];
    for (int i = 0; i < 3; ++i) e.ownerPos[i] = ownerPos[i] * scaleMultiplier_;
    for (int i = 0; i < 9; ++i) e.ownerRot9[i] = ownerRot9[i];
    e.entityScale = entityScale > 0.f ? entityScale : 1.f;
    e.visible = visible;
    RecomposeScript(e);
}

void ParticleRenderer::RemoveScriptEmitter(int slot) {
    if (slot < 0 || size_t(slot) >= emitters_.size()) return;
    emitters_[slot].alive = false;
    emitters_[slot].particles.clear();
}

void ParticleRenderer::RecomposeScript(Emitter& e) {
    // The same EmitterDef::SetupTransform rule Build applies: the entry's
    // offset is scaled by the parent entity but NOT rotated by it, the
    // orientations compose, and the emitter's scale is the product of the
    // entity's, the entry's and the level multiplier.
    float defRot[9];
    EulerDegreesToMatrix(e.refRotDeg, defRot);
    MatMul3(e.ownerRot9, defRot, e.rot);
    for (int i = 0; i < 3; ++i) {
        e.pos[i] = e.ownerPos[i] + e.entityScale * e.refOffset[i] * scaleMultiplier_;
        e.prevPos[i] = e.pos[i];
    }
    ApplyScale(e, e.entityScale * e.refScale * scaleMultiplier_);
}

void ParticleRenderer::Draw(bgfx::ViewId view, const Camera& camera, int width, int height) {
    drawCalls_ = 0;
    if (!bgfx::isValid(program_) || emitters_.empty()) return;
    (void)width;
    (void)height;

    float forward[3], right[3], up[3];
    camera.Forward(forward);
    camera.Right(right);
    Cross(right, forward, up);

    for (Emitter& e : emitters_) {
        if (!e.alive || !e.visible) continue;
        const size_t n = e.particles.size();
        if (n == 0) continue;

        const uint32_t vertexCount = static_cast<uint32_t>(n * 4);
        const uint32_t indexCount = static_cast<uint32_t>(n * 6);
        if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout_) < vertexCount) continue;
        if (bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount) continue;

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, vertexCount, layout_);
        bgfx::allocTransientIndexBuffer(&tib, indexCount);
        ParticleVertex* vtx = reinterpret_cast<ParticleVertex*>(tvb.data);
        uint16_t* idx = reinterpret_cast<uint16_t*>(tib.data);

        const bool spark = e.params->type == 2;
        for (size_t i = 0; i < n; ++i) {
            const Particle& p = e.particles[i];
            const uint32_t abgr = PackAbgr(p.color, p.alpha);
            float a[3], b[3], c[3], d[3];

            if (spark) {
                // A streak: one edge sits on the particle, the other is the
                // velocity vector scaled by Length. Thickness runs along the
                // normalised cross of velocity and the view ray, so the streak
                // always presents its width to the camera.
                float toCam[3] = {p.pos[0] - camera.pos[0], p.pos[1] - camera.pos[1],
                                  p.pos[2] - camera.pos[2]};
                float side[3];
                Cross(p.vel, toCam, side);
                const float len = std::sqrt(side[0] * side[0] + side[1] * side[1] +
                                            side[2] * side[2]);
                if (len > 1e-6f) {
                    const float inv = 1.f / len;
                    side[0] *= inv; side[1] *= inv; side[2] *= inv;
                }
                for (int k = 0; k < 3; ++k) {
                    const float tail = p.pos[k] + p.vel[k] * p.sparkLength;
                    a[k] = p.pos[k] + side[k] * p.sparkThickness;
                    b[k] = tail + side[k] * p.sparkThickness;
                    c[k] = tail;
                    d[k] = p.pos[k];
                }
            } else {
                // Camera-facing quad, optionally spun about the view axis.
                float rx[3], uy[3];
                if (p.rotAngle != 0.f) {
                    const float s = std::sin(p.rotAngle), co = std::cos(p.rotAngle);
                    for (int k = 0; k < 3; ++k) {
                        rx[k] = (right[k] * co + up[k] * s) * p.size;
                        uy[k] = (up[k] * co - right[k] * s) * p.size;
                    }
                } else {
                    for (int k = 0; k < 3; ++k) {
                        rx[k] = right[k] * p.size;
                        uy[k] = up[k] * p.size;
                    }
                }
                for (int k = 0; k < 3; ++k) {
                    a[k] = p.pos[k] - rx[k] + uy[k];
                    b[k] = p.pos[k] + rx[k] + uy[k];
                    c[k] = p.pos[k] + rx[k] - uy[k];
                    d[k] = p.pos[k] - rx[k] - uy[k];
                }
            }

            // Corner order and UVs are the engine's own: (0,1) (1,1) (1,0) (0,0).
            ParticleVertex* v = vtx + i * 4;
            v[0] = {a[0], a[1], a[2], abgr, 0.f, 1.f};
            v[1] = {b[0], b[1], b[2], abgr, 1.f, 1.f};
            v[2] = {c[0], c[1], c[2], abgr, 1.f, 0.f};
            v[3] = {d[0], d[1], d[2], abgr, 0.f, 0.f};

            const uint16_t base = static_cast<uint16_t>(i * 4);
            uint16_t* q = idx + i * 6;
            q[0] = base;     q[1] = static_cast<uint16_t>(base + 1);
            q[2] = static_cast<uint16_t>(base + 2);
            q[3] = base;     q[4] = static_cast<uint16_t>(base + 2);
            q[5] = static_cast<uint16_t>(base + 3);
        }

        // Depth write stays off for every mode: the two the data actually uses
        // (alpha and add) disable it explicitly in the original, and the
        // translucent path inherits a particle material that does not write
        // depth either. DepthTest is the emitter's own flag.
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                         e.blendState;
        if (e.params->depthTest) state |= BGFX_STATE_DEPTH_TEST_LESS;

        bgfx::setState(state);
        bgfx::setVertexBuffer(0, &tvb, 0, vertexCount);
        bgfx::setIndexBuffer(&tib, 0, indexCount);
        bgfx::setTexture(0, sDiffuse_, e.texture);
        bgfx::submit(view, program_);
        ++drawCalls_;
    }
}

} // namespace painful
