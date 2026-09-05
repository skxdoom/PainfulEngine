#include "WorldRenderer.h"
#include "ShaderLoad.h"
#include "../Core/Common.h"
#include "../Core/Log.h"
#include "GpuBuffers.h"
#include "MeshVertex.h"

#include <algorithm>
#include <bx/math.h>
#include <filesystem>

namespace painful {

namespace {

} // namespace

bool WorldRenderer::Init(const std::string& shaderDir) {
    layout_ = MakeMeshLayout();

    namespace fs = std::filesystem;
    bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_world");
    bgfx::ShaderHandle fs_ = LoadShader(shaderDir, "fs_world");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs_)) return false;

    program_ = bgfx::createProgram(vs, fs_, true);

    // Water gets its own program. Missing shaders are not fatal: water then
    // falls back to the ordinary world path.
    bgfx::ShaderHandle wvs = LoadShader(shaderDir, "vs_water");
    bgfx::ShaderHandle wfs = LoadShader(shaderDir, "fs_water");
    if (bgfx::isValid(wvs) && bgfx::isValid(wfs)) {
        waterProgram_ = bgfx::createProgram(wvs, wfs, true);
        sNormal_ = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
        sCube_   = bgfx::createUniform("s_cube",   bgfx::UniformType::Sampler);
        uEye_    = bgfx::createUniform("u_eye",    bgfx::UniformType::Vec4);
        uWater_        = bgfx::createUniform("u_water",        bgfx::UniformType::Vec4);
        uWaterDeep_    = bgfx::createUniform("u_waterDeep",    bgfx::UniformType::Vec4);
        uWaterShallow_ = bgfx::createUniform("u_waterShallow", bgfx::UniformType::Vec4);
    } else {
        LogWarn("water: vs_water/fs_water missing, water draws as ordinary geometry");
    }
    if (!bgfx::isValid(program_)) return false;

    sDiffuse_  = bgfx::createUniform("s_diffuse",  bgfx::UniformType::Sampler);
    sLightmap_ = bgfx::createUniform("s_lightmap", bgfx::UniformType::Sampler);
    uParams_   = bgfx::createUniform("u_params",   bgfx::UniformType::Vec4);
    uAmbient_  = bgfx::createUniform("u_ambient",  bgfx::UniformType::Vec4);
    uFogColor_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
    uFog_      = bgfx::createUniform("u_fog",      bgfx::UniformType::Vec4);
    uUvAnim_   = bgfx::createUniform("u_uvanim",   bgfx::UniformType::Vec4);
    uDetail_   = bgfx::createUniform("u_detail",   bgfx::UniformType::Vec4);
    sDetail_   = bgfx::createUniform("s_detail",   bgfx::UniformType::Sampler);
    sBlend2_   = bgfx::createUniform("s_blend2",   bgfx::UniformType::Sampler);
    sMask_     = bgfx::createUniform("s_mask2",    bgfx::UniformType::Sampler);
    uUv0_      = bgfx::createUniform("u_uv0",      bgfx::UniformType::Vec4);
    uUv1_      = bgfx::createUniform("u_uv1",      bgfx::UniformType::Vec4);
    uTile_     = bgfx::createUniform("u_tile",     bgfx::UniformType::Vec4);
    return true;
}

// Drops the LEVEL and keeps the programs: what a level switch wants. Upload
// sets every other per-level member itself.
void WorldRenderer::SetObjectVisible(size_t object, bool visible) {
    for (Chunk& c : chunks_)
        if (c.object == object) c.hidden = !visible;
}

void WorldRenderer::Clear() {
    for (Chunk& c : chunks_) {
        if (bgfx::isValid(c.vbo)) bgfx::destroy(c.vbo);
        if (bgfx::isValid(c.ibo)) bgfx::destroy(c.ibo);
    }
    chunks_.clear();
    zoneGraph_ = ZoneGraph();
    waterChunks_ = 0;
    detailOn_ = false;
}

void WorldRenderer::Shutdown() {
    Clear();
    if (bgfx::isValid(program_))   { bgfx::destroy(program_);   program_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sDiffuse_))  { bgfx::destroy(sDiffuse_);  sDiffuse_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sLightmap_)) { bgfx::destroy(sLightmap_); sLightmap_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uParams_))   { bgfx::destroy(uParams_);   uParams_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uAmbient_))  { bgfx::destroy(uAmbient_);  uAmbient_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uFogColor_)) { bgfx::destroy(uFogColor_); uFogColor_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uFog_))      { bgfx::destroy(uFog_);      uFog_      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uUvAnim_))   { bgfx::destroy(uUvAnim_);   uUvAnim_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uDetail_))   { bgfx::destroy(uDetail_);   uDetail_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sDetail_))   { bgfx::destroy(sDetail_);   sDetail_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sBlend2_))   { bgfx::destroy(sBlend2_);   sBlend2_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sMask_))     { bgfx::destroy(sMask_);     sMask_     = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uUv0_))      { bgfx::destroy(uUv0_);      uUv0_      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uUv1_))      { bgfx::destroy(uUv1_);      uUv1_      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uTile_))     { bgfx::destroy(uTile_);     uTile_     = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(waterProgram_)) { bgfx::destroy(waterProgram_); waterProgram_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sNormal_))   { bgfx::destroy(sNormal_);   sNormal_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sCube_))     { bgfx::destroy(sCube_);     sCube_     = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uEye_))      { bgfx::destroy(uEye_);      uEye_      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uWater_))        { bgfx::destroy(uWater_);        uWater_        = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uWaterDeep_))    { bgfx::destroy(uWaterDeep_);    uWaterDeep_    = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uWaterShallow_)) { bgfx::destroy(uWaterShallow_); uWaterShallow_ = BGFX_INVALID_HANDLE; }
}

void WorldRenderer::Upload(const MapMesh& map, TextureCache& textures,
                           const std::string& levelHint, const LevelInfo& info,
                           ShaderLibrary* shaders, bool skipActiveMeshes) {
    const float worldScale = info.scale;
    const bool overbright = info.overbright;
    chunks_.reserve(map.objects.size());
    worldScale_ = worldScale;
    // The zone/portal helper geometry drives visibility culling.
    zoneGraph_.Build(map, worldScale);

    // The level-wide detail map, applied to world geometry the way
    // MESH.SetDefaultDetailMaps does (addsigned grain, heavy tiling).
    // WorldMesh::SetupMaterials hardcodes both of these rather than taking them
    // from the map - see Docs/Reference/Water.md.
    waterChunks_ = 0;
    waterNormal_ = textures.Get("special/ripples_00", levelHint);
    waterCube_   = textures.GetCube("special/cube_wenecja", levelHint);

    detailOn_ = false;
    if (!info.detailTex.empty() && !textures.Resolve(info.detailTex, "").empty()) {
        detailTex_ = textures.Get(info.detailTex, "");
        detailTile_[0] = info.detailTileU;
        detailTile_[1] = info.detailTileV;
        detailOn_ = true;
    }

    for (size_t objectIndex = 0; objectIndex < map.objects.size(); ++objectIndex) {
        const MapObject& o = map.objects[objectIndex];
        const size_t vertexCount = o.vertexCount();
        if (vertexCount == 0 || o.indices.empty()) continue;
        // Portals, antiportals and zone volumes are invisible helper geometry.
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone") ||
            o.nameHas("vollight") || o.nameHas("volfog") || o.nameHas("barrier")) {
            continue;
        }
        // A rigid body is drawn where physics put it, by EntityRenderer.
        if (skipActiveMeshes && o.isActiveMesh()) continue;

        std::vector<MeshVertex> verts(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            float p[3], n[3], uv[2];
            o.position(i, p);
            o.normal(i, n);
            o.uv(i, uv);
            MeshVertex& v = verts[i];
            v.x = p[0]; v.y = p[1]; v.z = p[2];
            v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
            v.u0 = uv[0]; v.v0 = uv[1];
            // Lightmapped geometry keeps its second UV set in slots 6/7.
            if (o.uvChannels == 2) {
                v.u1 = o.verts[i * 8 + 6];
                v.v1 = o.verts[i * 8 + 7];
            } else {
                v.u1 = uv[0];
                v.v1 = uv[1];
            }
        }

        Chunk chunk;
        chunk.object = objectIndex;
        chunk.transform = o.transform;
        // The engine scales the static world by the level's o.Scale at load
        // (WORLD.LoadMap in CLevel.lua). Entities are authored in the scaled
        // space already, so only the world mesh gets this factor.
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 3; ++c) chunk.transform.m[r * 4 + c] *= worldScale;
        }

        // Culling data: zones are matched in raw mesh space (the graph's
        // space), the AABB is kept in world space for the frustum test.
        std::vector<int> overlapping;
        zoneGraph_.ZonesForBox(o.bboxMin, o.bboxMax, overlapping);
        for (int z : overlapping) chunk.zones.push_back(uint16_t(z));
        chunk.aabbLo[0] = chunk.aabbLo[1] = chunk.aabbLo[2] = 1e30f;
        chunk.aabbHi[0] = chunk.aabbHi[1] = chunk.aabbHi[2] = -1e30f;
        for (int corner = 0; corner < 8; ++corner) {
            const float raw[3] = {corner & 1 ? o.bboxMax[0] : o.bboxMin[0],
                                  corner & 2 ? o.bboxMax[1] : o.bboxMin[1],
                                  corner & 4 ? o.bboxMax[2] : o.bboxMin[2]};
            float w[3];
            chunk.transform.TransformPoint(raw[0], raw[1], raw[2], w);
            for (int a = 0; a < 3; ++a) {
                chunk.aabbLo[a] = std::min(chunk.aabbLo[a], w[a]);
                chunk.aabbHi[a] = std::max(chunk.aabbHi[a], w[a]);
            }
        }
        // Material selection works the way the engine's own scripts are named.
        // A script can define a material for a SPECIFIC object - lm.shader has
        // "shader tasmashape copy defaultTU2 { pan[0] = -2.22 0 }" for the
        // Factory conveyor belts - so the object's own name is tried first.
        // Otherwise lightmapped objects use the defaultTU2 family (the x2 set
        // when the level is Overbright), plain ones defaultNTU, and the
        // trans / atest / 2sided name substrings pick the variant.
        const bool trans = o.nameHas("trans") || o.nameHas("decal");
        std::string shaderName = (o.uvChannels == 2)
            ? (overbright ? "defaultTU2x2" : "defaultTU2") : "defaultNTU";
        if (trans) shaderName += "trans";
        else if (o.nameHas("atest")) shaderName += "atest";
        if (o.nameHas("2sided")) shaderName += "2sided";

        // Water is a flag on the mesh, and WorldMesh::SetupFlags (Engine.dll
        // 0x101d7050) sets it from a plain substring test on the object's name:
        //   strstr(name, "water") -> flags |= 0x8000000
        // WorldMesh::SetupMaterials then falls back to the "water" material
        // family; which member depends on the device tier and the quality bits
        // (water2_refl / _refr / _refl_refr).
        //
        // The variant matters more than usual here, because the tiers are not
        // the same picture drawn better - they are different constructions:
        //
        //   nv30  one pass, fx = FXWater_20, compiled bytecode in Water.fxo
        //   nv20  TWO passes - the lightmap alone, then "blend modulate" with
        //         an EMBM pass sampling $cubemap through a scrolling $normalmap
        //   tnl   one pass, $colormap * $lightmap with modulate2x, no cubemap,
        //         no normal map, xform[0] = $identity so no tiling or scroll
        //
        // Only the tnl construction is expressible with what this port has, and
        // it is fully specified in the script - no FX bytecode, no render
        // target. So water is pinned to that variant rather than taking the
        // default nv30 preference and rendering a normal map as if it were a
        // colour, which is what the higher tiers would degrade into.
        const bool isWater = o.nameHas("water");
        if (isWater) shaderName = "water";

        const ShaderDef* def = shaders ? shaders->Find(o.name) : nullptr;
        if (def) {
            LogInfo("material by object name: %s", o.name.c_str());
        } else if (shaders) {
            def = isWater ? shaders->Find(shaderName, {"nv20"}) : shaders->Find(shaderName);
            if (isWater && def) LogInfo("water surface: %s", o.name.c_str());
        }
        if (def && !def->passes.empty()) {
            std::string warn;
            chunk.material = MaterialState::FromPass(def->passes.front(), &warn);
            if (!warn.empty()) {
                LogWarn("material %s: %s", shaderName.c_str(), warn.c_str());
            }
            // The nv20 water is two passes and they carry different things:
            // pass 0 is the lightmap draw, which is where the render state
            // belongs, while the normal map's tile and pan live on pass 1, the
            // EMBM pass. Folding both into one draw means taking each from
            // where it actually is. Note pass 1 declares tile[0]/pan[0] only -
            // one scrolling layer, not the two the nv30 variant uses.
            if (isWater && def->passes.size() > 1) {
                const MaterialState embm = MaterialState::FromPass(def->passes[1]);
                chunk.material.tile0[0] = embm.tile0[0];
                chunk.material.tile0[1] = embm.tile0[1];
                chunk.material.pan0[0] = embm.pan0[0];
                chunk.material.pan0[1] = embm.pan0[1];
            }
        } else {
            if (shaders) LogWarn("material not found: %s", shaderName.c_str());
            // Built-in stand-in matching the script defaults.
            MaterialState& m = chunk.material;
            m.state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS;
            if (trans) m.state |= BGFX_STATE_BLEND_ALPHA;
            else m.state |= BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z;
            if (!trans && !o.nameHas("2sided")) m.state |= BGFX_STATE_CULL_CCW;
            if (o.nameHas("atest")) m.alphaRef = 0.5f;
            m.lightScale = overbright ? 2.f : 1.f;
        }
        chunk.isWater = isWater && bgfx::isValid(waterProgram_);
        if (chunk.isWater) ++waterChunks_;
        chunk.vbo = MakeVertexBuffer(verts.data(),
                                     uint32_t(verts.size() * sizeof(MeshVertex)), layout_);
        chunk.ibo = MakeIndexBuffer(o.indices.data(), uint32_t(o.indices.size()));

        if (o.materials.empty()) {
            Batch b;
            b.indexCount = uint32_t(o.indices.size());
            b.diffuse = textures.White();
            b.lightmap = textures.White();
            chunk.batches.push_back(b);
        } else {
            for (const Material& m : o.materials) {
                Batch b;
                b.firstIndex = m.firstIndex;
                b.indexCount = uint32_t(m.triangleCount) * 3;
                if (b.indexCount == 0) continue;
                b.diffuse = textures.Get(m.diffuse(), levelHint);
                // Every slot carries its own UV transform, and honouring it is
                // not optional: Enclave's terrain tiles its ground textures
                // 30x and 20x, so sampling at 1x shows one magnified texel
                // patch instead of the surface.
                auto slotUv = [](const TextureSlot& s, float out[4]) {
                    out[0] = s.scaleU; out[1] = s.scaleV;
                    out[2] = s.offsetU; out[3] = s.offsetV;
                };
                slotUv(m.slots[0], b.uvDiffuse);

                // All four slots filled = a terrain blend: two TILED textures
                // mixed by a mask. The tiled pair (large scale) are the
                // terrains; the 1x1 pair are the lightmap and the mask.
                if (o.uvChannels == 2 && !m.slots[0].empty() && !m.slots[1].empty() &&
                    !m.slots[2].empty() && !m.slots[3].empty()) {
                    const bool s1Tiled = m.slots[1].scaleU > 1.5f;
                    const bool s2Tiled = m.slots[2].scaleU > 1.5f;
                    const int blendSlot = s2Tiled ? 2 : (s1Tiled ? 1 : -1);
                    if (blendSlot > 0) {
                        const int maskSlot = blendSlot == 2 ? 3 : 2;
                        const int lightSlot = blendSlot == 2 ? 1 : 3;
                        b.blended = true;
                        b.blend2 = textures.Get(m.slots[blendSlot].name, levelHint);
                        b.mask = textures.Get(m.slots[maskSlot].name, levelHint);
                        b.lightmap = textures.Get(m.slots[lightSlot].name, levelHint);
                        b.hasLightmap = true;
                        slotUv(m.slots[blendSlot], b.uvBlend);
                        chunk.batches.push_back(b);
                        continue;
                    }
                }
                // A lightmap is only usable when the object actually carries a
                // second UV set. Some materials name one on single-UV objects;
                // sampling it with the tiling diffuse UVs smears the lightmap
                // across the surface and hides the real texture.
                b.hasLightmap = (o.uvChannels == 2) && !m.lightmap().empty();
                b.lightmap = b.hasLightmap ? textures.Get(m.lightmap(), levelHint)
                                           : textures.White();
                chunk.batches.push_back(b);
            }
        }

        triangles_ += o.triangleCount();
        chunks_.push_back(std::move(chunk));
    }
}

void WorldRenderer::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
                         const LevelInfo& info, float timeSeconds) {
    const float* ambient = info.ambient;
    drawCalls_ = 0;
    if (!bgfx::isValid(program_)) return;

    float forward[3];
    camera.Forward(forward);
    const bx::Vec3 eye = {camera.pos[0], camera.pos[1], camera.pos[2]};
    const bx::Vec3 at = {camera.pos[0] + forward[0],
                         camera.pos[1] + forward[1],
                         camera.pos[2] + forward[2]};

    float viewMtx[16], projMtx[16];
    // PainEngine data is right-handed (Maya export). bx defaults to left-handed,
    // which renders the whole world mirrored.
    bx::mtxLookAt(viewMtx, eye, at, {0.0f, 1.0f, 0.0f}, bx::Handedness::Right);
    bx::mtxProj(projMtx, camera.fovDegrees, float(width) / float(height),
                camera.nearPlane, camera.farPlane, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    bgfx::setViewTransform(view, viewMtx, projMtx);

    const float fogValue[4] = {info.fogColor[0] / 255.f, info.fogColor[1] / 255.f,
                               info.fogColor[2] / 255.f, 1.f};
    bgfx::setUniform(uFogColor_, fogValue);
    // Fog per CLevel.lua: mode 0 none, 1 exp, 2 exp2, 3 linear.
    const float fogParams[4] = {float(info.fogMode), info.fogStart, info.fogEnd,
                                info.fogDensity};
    bgfx::setUniform(uFog_, fogParams);

    // Visibility: frustum-cull every chunk, and walk the zone graph so only
    // rooms reachable through in-view portals draw at all.
    const Frustum frustum = Frustum::FromViewProj(viewMtx, projMtx);
    zonesVisible_ = zoneGraph_.zoneCount();
    if (visCulling_ && !zoneGraph_.empty()) {
        const float raw[3] = {camera.pos[0] / worldScale_, camera.pos[1] / worldScale_,
                              camera.pos[2] / worldScale_};
        // Zone volumes overlap, so the camera can stand in several at once;
        // visibility starts from all of them. Outside every zone the graph
        // stays permissive and the frustum alone culls.
        zoneGraph_.ZonesAt(raw, cameraZones_);
        // Portals within arm's reach stay open, so walking through a doorway
        // never blinks the far room out for a frame. This is a distance about
        // the body, not a property of the projection: it used to be derived
        // from the near plane, which silently tied it to a value chosen for
        // an entirely different reason - pulling the near plane in to stop
        // walls clipping would have started the doorways popping.
        constexpr float kPortalNearRadius = 2.f;
        zoneGraph_.VisibleZones(frustum, cameraZones_, worldScale_, camera.pos,
                                kPortalNearRadius, zoneVisible_);
        zonesVisible_ = size_t(std::count(zoneVisible_.begin(), zoneVisible_.end(), true));
    }

    for (const Chunk& c : chunks_) {
        if (c.hidden) continue;
        if (visCulling_) {
            // A chunk is culled by the graph only when it overlaps at least
            // one zone and none of them are visible.
            if (!c.zones.empty() && !zoneVisible_.empty()) {
                bool anyVisible = false;
                for (uint16_t z : c.zones) {
                    if (z < zoneVisible_.size() && zoneVisible_[z]) { anyVisible = true; break; }
                }
                if (!anyVisible) continue;
            }
            if (!frustum.VisibleAabb(c.aabbLo, c.aabbHi)) continue;
        }
        // Ambient carries the chunk's lightmap scale in w (1, or 2 for the
        // Overbright material set).
        const float ambientValue[4] = {ambient[0] / 255.f, ambient[1] / 255.f,
                                       ambient[2] / 255.f, c.material.lightScale};

        uint64_t state = c.material.state | BGFX_STATE_MSAA;
        // Diagnostic override: --cull none strips culling, --cull cw flips it.
        if (cullMode_ == 2) {
            state &= ~BGFX_STATE_CULL_MASK;
        } else if (cullMode_ == 1 && (state & BGFX_STATE_CULL_MASK)) {
            state = (state & ~BGFX_STATE_CULL_MASK) | BGFX_STATE_CULL_CW;
        }

        // pan animation offsets for this chunk's material.
        const float uvAnim[4] = {c.material.pan0[0] * timeSeconds,
                                 c.material.pan0[1] * timeSeconds,
                                 c.material.pan1[0] * timeSeconds,
                                 c.material.pan1[1] * timeSeconds};
        const float tile[4] = {c.material.tile0[0], c.material.tile0[1],
                               c.material.tile1[0], c.material.tile1[1]};
        const float detail[4] = {detailTile_[0], detailTile_[1],
                                 detailOn_ ? 1.f : 0.f, 0.f};
        // Water takes the reflection program instead. The two nv20 passes -
        // the lightmap alone, then "blend modulate" over it - multiply out to
        // one expression, so they fold into a single draw here.
        if (c.isWater) {
            // o.Water in the level's .CLevel is the authority for these, not
            // water.shader: the script's tile[0]/pan[0] only restate CLevel.lua's
            // class defaults, and 21 levels override them.
            const WaterInfo& w = info.water;
            const float eye[4] = {camera.pos[0], camera.pos[1], camera.pos[2], 0.f};
            const float waterTile[4] = {w.tile[0], w.tile[1], w.tile[0], w.tile[1]};
            const float waterPan[4] = {w.pan[0] * timeSeconds, w.pan[1] * timeSeconds,
                                       w.pan[0] * timeSeconds, w.pan[1] * timeSeconds};
            const float waterParams[4] = {w.bumpHeight, w.fresnelBias, w.fresnelExponent,
                                          w.reflectionAmount};
            const float deep[4] = {w.deepColor[0] / 255.f, w.deepColor[1] / 255.f,
                                   w.deepColor[2] / 255.f, w.waterAmount};
            const float shallow[4] = {w.shallowColor[0] / 255.f, w.shallowColor[1] / 255.f,
                                      w.shallowColor[2] / 255.f, 1.f};
            for (const Batch& b : c.batches) {
                bgfx::setUniform(uAmbient_, ambientValue);
                bgfx::setUniform(uUvAnim_, waterPan);
                bgfx::setUniform(uTile_, waterTile);
                bgfx::setUniform(uWater_, waterParams);
                bgfx::setUniform(uWaterDeep_, deep);
                bgfx::setUniform(uWaterShallow_, shallow);
                bgfx::setUniform(uEye_, eye);
                bgfx::setUniform(uFogColor_, fogValue);
                bgfx::setUniform(uFog_, fogParams);
                bgfx::setTransform(c.transform.m);
                bgfx::setVertexBuffer(0, c.vbo);
                bgfx::setIndexBuffer(c.ibo, b.firstIndex, b.indexCount);
                bgfx::setTexture(0, sNormal_, waterNormal_,
                                 BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC);
                bgfx::setTexture(1, sCube_, waterCube_);
                bgfx::setTexture(2, sLightmap_, b.lightmap, c.material.sampler[1]);
                bgfx::setState(state);
                bgfx::submit(view, waterProgram_);
                ++drawCalls_;
            }
            continue;
        }

        for (const Batch& b : c.batches) {
            const float params[4] = {b.hasLightmap ? 1.f : 0.f,
                                     c.material.alphaRef,
                                     b.blended ? 1.f : 0.f, 0.f};
            bgfx::setUniform(uAmbient_, ambientValue);
            bgfx::setUniform(uParams_, params);
            bgfx::setUniform(uUvAnim_, uvAnim);
            bgfx::setUniform(uDetail_, detail);
            bgfx::setUniform(uUv0_, b.uvDiffuse);
            bgfx::setUniform(uUv1_, b.uvBlend);
            bgfx::setUniform(uTile_, tile);
            bgfx::setTexture(2, sDetail_, detailOn_ ? detailTex_ : b.diffuse,
                             BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC);
            bgfx::setTexture(3, sBlend2_, b.blended ? b.blend2 : b.diffuse);
            bgfx::setTexture(4, sMask_, b.blended ? b.mask : b.diffuse);
            bgfx::setTransform(c.transform.m);
            bgfx::setVertexBuffer(0, c.vbo);
            bgfx::setIndexBuffer(c.ibo, b.firstIndex, b.indexCount);
            // Anisotropic filtering keeps the diffuse and detail grain alive
            // at the glancing angles terrain is mostly seen at.
            bgfx::setTexture(0, sDiffuse_, b.diffuse,
                             c.material.sampler[0] | BGFX_SAMPLER_MIN_ANISOTROPIC |
                                 BGFX_SAMPLER_MAG_ANISOTROPIC);
            bgfx::setTexture(1, sLightmap_, b.lightmap, c.material.sampler[1]);
            bgfx::setState(state);
            bgfx::submit(view, program_);
            ++drawCalls_;
        }
    }
}

} // namespace painful
