#include "WorldRenderer.h"
#include "../Core/Common.h"
#include "../Core/Log.h"
#include "MeshVertex.h"

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
}

void WorldRenderer::Upload(const MapMesh& map, TextureCache& textures,
                           const std::string& levelHint, float worldScale) {
    chunks_.reserve(map.objects.size());

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
        chunk.twoSided = o.nameHas("2sided") || o.nameHas("trans");
        // "trans" marks translucent geometry such as glass and grates. It has to
        // alpha-blend and must not write depth, or it hides what is behind it.
        chunk.translucent = o.nameHas("trans") || o.nameHas("decal");
        chunk.vbo = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(MeshVertex))), layout_);
        chunk.ibo = bgfx::createIndexBuffer(
            bgfx::copy(o.indices.data(), uint32_t(o.indices.size() * sizeof(uint16_t))));

        const bool alphaTest = o.nameHas("atest");
        if (o.materials.empty()) {
            Batch b;
            b.indexCount = uint32_t(o.indices.size());
            b.diffuse = textures.White();
            b.lightmap = textures.White();
            b.alphaTest = alphaTest;
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
                b.alphaTest = alphaTest;
                chunk.batches.push_back(b);
            }
        }

        triangles_ += o.triangleCount();
        chunks_.push_back(std::move(chunk));
    }
}

void WorldRenderer::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
                         const float ambient[3], const float fogColor[3],
                         float fogDensity, float fogStart) {
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

    const float ambientValue[4] = {ambient[0] / 255.f, ambient[1] / 255.f, ambient[2] / 255.f, 1.f};
    const float fogValue[4] = {fogColor[0] / 255.f, fogColor[1] / 255.f, fogColor[2] / 255.f, 1.f};
    bgfx::setUniform(uAmbient_, ambientValue);
    bgfx::setUniform(uFogColor_, fogValue);

    for (const Chunk& c : chunks_) {
        for (const Batch& b : c.batches) {
            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA;
            if (!c.translucent) state |= BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z;
            state |= BGFX_STATE_DEPTH_TEST_LESS;
            if (c.translucent) state |= BGFX_STATE_BLEND_ALPHA;
            // Right-handed rendering reverses winding relative to the screen.
            if (!c.twoSided) {
                if (cullMode_ == 0)      state |= BGFX_STATE_CULL_CCW;
                else if (cullMode_ == 1) state |= BGFX_STATE_CULL_CW;
            }

            const float params[4] = {b.hasLightmap ? 1.f : 0.f,
                                     b.alphaTest ? 1.f : 0.f,
                                     fogDensity, fogStart};
            bgfx::setUniform(uParams_, params);
            bgfx::setTransform(c.transform.m);
            bgfx::setVertexBuffer(0, c.vbo);
            bgfx::setIndexBuffer(c.ibo, b.firstIndex, b.indexCount);
            bgfx::setTexture(0, sDiffuse_, b.diffuse);
            bgfx::setTexture(1, sLightmap_, b.lightmap);
            bgfx::setState(state);
            bgfx::submit(view, program_);
            ++drawCalls_;
        }
    }
}

} // namespace painful
