#include "WorldRenderer.h"
#include "../Core/Common.h"
#include "../Core/Log.h"
#include "MeshVertex.h"

#include <algorithm>
#include <bx/math.h>
#include <filesystem>

namespace painful {

namespace {

bgfx::ShaderHandle LoadShader(const std::string& path) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) {
        LogWarn("cannot read shader %s", path.c_str());
        return BGFX_INVALID_HANDLE;
    }
    const bgfx::Memory* mem = bgfx::copy(data.data(), static_cast<uint32_t>(data.size()));
    return bgfx::createShader(mem);
}

} // namespace

bool WorldRenderer::Init(const std::string& shaderDir) {
    layout_ = MakeMeshLayout();

    namespace fs = std::filesystem;
    bgfx::ShaderHandle vs = LoadShader((fs::path(shaderDir) / "vs_world.bin").string());
    bgfx::ShaderHandle fs_ = LoadShader((fs::path(shaderDir) / "fs_world.bin").string());
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs_)) return false;

    program_ = bgfx::createProgram(vs, fs_, true);
    if (!bgfx::isValid(program_)) return false;

    sDiffuse_  = bgfx::createUniform("s_diffuse",  bgfx::UniformType::Sampler);
    sLightmap_ = bgfx::createUniform("s_lightmap", bgfx::UniformType::Sampler);
    uParams_   = bgfx::createUniform("u_params",   bgfx::UniformType::Vec4);
    uAmbient_  = bgfx::createUniform("u_ambient",  bgfx::UniformType::Vec4);
    uFogColor_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
    uFog_      = bgfx::createUniform("u_fog",      bgfx::UniformType::Vec4);
    return true;
}

void WorldRenderer::Shutdown() {
    for (Chunk& c : chunks_) {
        if (bgfx::isValid(c.vbo)) bgfx::destroy(c.vbo);
        if (bgfx::isValid(c.ibo)) bgfx::destroy(c.ibo);
    }
    chunks_.clear();
    if (bgfx::isValid(program_))   { bgfx::destroy(program_);   program_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sDiffuse_))  { bgfx::destroy(sDiffuse_);  sDiffuse_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sLightmap_)) { bgfx::destroy(sLightmap_); sLightmap_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uParams_))   { bgfx::destroy(uParams_);   uParams_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uAmbient_))  { bgfx::destroy(uAmbient_);  uAmbient_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uFogColor_)) { bgfx::destroy(uFogColor_); uFogColor_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uFog_))      { bgfx::destroy(uFog_);      uFog_      = BGFX_INVALID_HANDLE; }
}

void WorldRenderer::Upload(const MapMesh& map, TextureCache& textures,
                           const std::string& levelHint, float worldScale,
                           ShaderLibrary* shaders, bool overbright) {
    chunks_.reserve(map.objects.size());
    worldScale_ = worldScale;
    // The zone/portal helper geometry drives visibility culling.
    zoneGraph_.Build(map, worldScale);

    for (const MapObject& o : map.objects) {
        const size_t vertexCount = o.vertexCount();
        if (vertexCount == 0 || o.indices.empty()) continue;
        // Portals, antiportals and zone volumes are invisible helper geometry.
        if (o.nameHas("portal") || o.nameHas("antyp") || o.nameHas("zone") ||
            o.nameHas("vollight") || o.nameHas("volfog") || o.nameHas("barrier")) {
            continue;
        }

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
        // Material selection works the way the engine's own scripts are named:
        // lightmapped objects use the defaultTU2 family (the x2 set when the
        // level is Overbright), plain ones defaultNTU, and the trans / atest /
        // 2sided object-name substrings pick the variant. All render state
        // then comes from the resolved .shader definition.
        const bool trans = o.nameHas("trans") || o.nameHas("decal");
        std::string shaderName = (o.uvChannels == 2)
            ? (overbright ? "defaultTU2x2" : "defaultTU2") : "defaultNTU";
        if (trans) shaderName += "trans";
        else if (o.nameHas("atest")) shaderName += "atest";
        if (o.nameHas("2sided")) shaderName += "2sided";

        const ShaderDef* def = shaders ? shaders->Find(shaderName) : nullptr;
        if (def && !def->passes.empty()) {
            std::string warn;
            chunk.material = MaterialState::FromPass(def->passes.front(), &warn);
            if (!warn.empty()) {
                LogWarn("material %s: %s", shaderName.c_str(), warn.c_str());
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
        chunk.vbo = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(MeshVertex))), layout_);
        chunk.ibo = bgfx::createIndexBuffer(
            bgfx::copy(o.indices.data(), uint32_t(o.indices.size() * sizeof(uint16_t))));

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
                         const LevelInfo& info) {
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
        zoneGraph_.VisibleZones(frustum, cameraZones_, worldScale_, zoneVisible_);
        zonesVisible_ = size_t(std::count(zoneVisible_.begin(), zoneVisible_.end(), true));
    }

    for (const Chunk& c : chunks_) {
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

        for (const Batch& b : c.batches) {
            const float params[4] = {b.hasLightmap ? 1.f : 0.f,
                                     c.material.alphaRef,
                                     0.f, 0.f};
            bgfx::setUniform(uAmbient_, ambientValue);
            bgfx::setUniform(uParams_, params);
            bgfx::setTransform(c.transform.m);
            bgfx::setVertexBuffer(0, c.vbo);
            bgfx::setIndexBuffer(c.ibo, b.firstIndex, b.indexCount);
            bgfx::setTexture(0, sDiffuse_, b.diffuse, c.material.sampler[0]);
            bgfx::setTexture(1, sLightmap_, b.lightmap, c.material.sampler[1]);
            bgfx::setState(state);
            bgfx::submit(view, program_);
            ++drawCalls_;
        }
    }
}

} // namespace painful
