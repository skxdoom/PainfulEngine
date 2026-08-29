#include "SkyRenderer.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "MeshVertex.h"

#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace painful {

namespace {

bgfx::ShaderHandle LoadShader(const std::string& path) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) return BGFX_INVALID_HANDLE;
    return bgfx::createShader(bgfx::copy(data.data(), static_cast<uint32_t>(data.size())));
}

// Sky dome objects name their layer: "layer01shape", "_trans_layer03shape".
// The order of objects in the mesh does NOT match layer order, so the name is
// the only reliable pairing.
int LayerFromName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t at = lower.find("layer");
    if (at == std::string::npos) return 0;
    at += 5;
    int value = 0, digits = 0;
    while (at < lower.size() && lower[at] >= '0' && lower[at] <= '9') {
        value = value * 10 + (lower[at] - '0');
        ++at;
        ++digits;
    }
    return digits > 0 ? value : 0;
}

bool IsTransparentShell(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("_trans") != std::string::npos;
}

} // namespace

bool SkyRenderer::Init(const std::string& shaderDir) {
    layout_ = MakeMeshLayout();
    namespace fs = std::filesystem;
    bgfx::ShaderHandle vs = LoadShader((fs::path(shaderDir) / "vs_world.bin").string());
    bgfx::ShaderHandle fsh = LoadShader((fs::path(shaderDir) / "fs_sky.bin").string());
    if (!bgfx::isValid(vs) || !bgfx::isValid(fsh)) return false;
    program_ = bgfx::createProgram(vs, fsh, true);
    if (!bgfx::isValid(program_)) return false;

    sTex1_ = bgfx::createUniform("s_tex1", bgfx::UniformType::Sampler);
    sTex2_ = bgfx::createUniform("s_tex2", bgfx::UniformType::Sampler);
    sMask_ = bgfx::createUniform("s_mask", bgfx::UniformType::Sampler);
    sLmap_ = bgfx::createUniform("s_lmap", bgfx::UniformType::Sampler);
    uXform1_ = bgfx::createUniform("u_tex1Xform", bgfx::UniformType::Vec4);
    uXform2_ = bgfx::createUniform("u_tex2Xform", bgfx::UniformType::Vec4);
    uRot_ = bgfx::createUniform("u_skyRot", bgfx::UniformType::Vec4);
    return true;
}

void SkyRenderer::Shutdown() {
    for (Part& p : parts_) {
        if (bgfx::isValid(p.vbo)) bgfx::destroy(p.vbo);
        if (bgfx::isValid(p.ibo)) bgfx::destroy(p.ibo);
    }
    parts_.clear();
    bgfx::UniformHandle* uniforms[] = {&sTex1_, &sTex2_, &sMask_, &sLmap_,
                                       &uXform1_, &uXform2_, &uRot_};
    for (bgfx::UniformHandle* u : uniforms) {
        if (bgfx::isValid(*u)) { bgfx::destroy(*u); *u = BGFX_INVALID_HANDLE; }
    }
    if (bgfx::isValid(program_)) { bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; }
}

bool SkyRenderer::LoadDome(const std::string& path) {
    MapMesh mesh;
    if (!MapMesh::Load(path, mesh) || mesh.objects.empty()) return false;

    for (const MapObject& o : mesh.objects) {
        const size_t vertexCount = o.vertexCount();
        if (vertexCount == 0 || o.indices.empty()) continue;

        std::vector<MeshVertex> verts(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            float p[3], n[3], uv[2], uvb[2];
            o.position(i, p);
            o.normal(i, n);
            o.uv(i, uv);
            o.uv1(i, uvb);
            MeshVertex& v = verts[i];
            v.x = p[0]; v.y = p[1]; v.z = p[2];
            v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
            // Channel 0 carries the animated layer textures, channel 1 the
            // mask and lightmap - same split as world lightmapping.
            v.u0 = uv[0];  v.v0 = uv[1];
            v.u1 = uvb[0]; v.v1 = uvb[1];
        }

        Part part;
        part.layer = LayerFromName(o.name);
        part.blend = IsTransparentShell(o.name);
        part.indexCount = uint32_t(o.indices.size());
        part.vbo = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(MeshVertex))), layout_);
        part.ibo = bgfx::createIndexBuffer(
            bgfx::copy(o.indices.data(), uint32_t(o.indices.size() * sizeof(uint16_t))));
        parts_.push_back(part);
    }
    return !parts_.empty();
}

bool SkyRenderer::Load(const std::string& mapsRoot, const LevelInfo& info,
                       TextureCache& textures) {
    white_ = textures.White();

    // Prefer the full layered dome; fall back to the LowQuality sky.
    std::string domeName = info.skyDomeMap;
    layered_ = !domeName.empty();
    if (!layered_) domeName = info.skyMap;
    if (domeName.empty()) return false;

    const std::string path = mapsRoot + "/" + domeName;
    if (!FileSystem::Get().Exists(path)) {
        LogWarn("sky mesh not found: %s", path.c_str());
        return false;
    }
    if (!LoadDome(path)) return false;

    if (layered_) {
        for (int i = 0; i < 4; ++i) {
            const SkyLayer& src = info.skyLayers[i];
            if (!src.valid()) continue;
            GpuLayer& dst = layers_[layerCount_];
            dst.number = i + 1;
            dst.anim1 = src.tex1;
            dst.anim2 = src.tex2;
            // A sky texture that cannot be resolved must fall back to fully
            // transparent. Falling back to white paints an opaque sheet across
            // the sky and hides every layer beneath it.
            const bgfx::TextureHandle clear = textures.Transparent();
            auto pick = [&](const std::string& name) {
                if (name.empty()) return clear;
                const bool found = !textures.Resolve(name, "").empty();
                return found ? textures.Get(name, "") : clear;
            };
            dst.tex1 = pick(src.tex1.name);
            dst.tex2 = pick(src.tex2.name);
            // A missing mask should select tex1 (mask 0), not tex2.
            dst.mask = src.mask.empty() || textures.Resolve(src.mask, "").empty()
                           ? clear : textures.Get(src.mask, "");
            dst.lmap = src.lightmap.empty() || textures.Resolve(src.lightmap, "").empty()
                           ? white_ : textures.Get(src.lightmap, "");
            ++layerCount_;
        }
    }
    if (layerCount_ == 0) {
        // LowQuality path: one opaque layer showing a single texture. Only
        // THIS path takes the authored rotation - the engine passes Angle to
        // LoadLowQualitySky but LoadSky (the layered dome) has no angle
        // parameter at all, so the full dome must render as authored.
        angle_ = info.skyAngle;
        GpuLayer& dst = layers_[0];
        dst.tex1 = info.skyTexture.empty() ? white_ : textures.Get(info.skyTexture, "");
        dst.tex2 = dst.tex1;
        dst.mask = white_;
        dst.lmap = white_;
        layerCount_ = 1;
        layered_ = false;
    }
    return true;
}

void SkyRenderer::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
                       float timeSeconds) {
    if (parts_.empty() || !bgfx::isValid(program_)) return;

    float forward[3];
    camera.Forward(forward);
    const bx::Vec3 eye = {camera.pos[0], camera.pos[1], camera.pos[2]};
    const bx::Vec3 at = {camera.pos[0] + forward[0],
                         camera.pos[1] + forward[1],
                         camera.pos[2] + forward[2]};
    float viewMtx[16], projMtx[16];
    bx::mtxLookAt(viewMtx, eye, at, {0.0f, 1.0f, 0.0f}, bx::Handedness::Right);
    bx::mtxProj(projMtx, camera.fovDegrees, float(width) / float(height),
                camera.nearPlane, 2000.f, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    bgfx::setViewTransform(view, viewMtx, projMtx);

    // Centre the dome on the camera so it never gets nearer or further away.
    const float a = angle_ * 3.14159265f / 180.f;
    const float c = std::cos(a), s = std::sin(a);
    Mat4 world;
    world.m[0] = c; world.m[1] = 0; world.m[2] = -s; world.m[3] = 0;
    world.m[4] = 0; world.m[5] = 1; world.m[6] = 0;  world.m[7] = 0;
    world.m[8] = s; world.m[9] = 0; world.m[10] = c; world.m[11] = 0;
    world.m[12] = camera.pos[0];
    world.m[13] = camera.pos[1];
    world.m[14] = camera.pos[2];
    world.m[15] = 1.f;

    // Diagnostic: PAINFUL_SKYLAYER=N draws only that layer (1-based).
    int only = 0;
    if (const char* e = getenv("PAINFUL_SKYLAYER")) only = atoi(e);
    for (int i = 0; i < layerCount_; ++i) {
        const GpuLayer& layer = layers_[i];
        if (only > 0 && layer.number != only) continue;
        const float xform1[4] = {layer.anim1.panU * timeSeconds, layer.anim1.panV * timeSeconds,
                                 layer.anim1.tileU, layer.anim1.tileV};
        const float xform2[4] = {layer.anim2.panU * timeSeconds, layer.anim2.panV * timeSeconds,
                                 layer.anim2.tileU, layer.anim2.tileV};
        const float rot[4] = {layer.anim1.rotSpeed * timeSeconds,
                              layer.anim2.rotSpeed * timeSeconds, 0.f, 0.f};

        for (const Part& p : parts_) {
            // Shells are matched to layers BY NAME, because the order of objects
            // inside the mesh does not follow layer order.
            if (layered_ && p.layer != layer.number) continue;

            bgfx::setUniform(uXform1_, xform1);
            bgfx::setUniform(uXform2_, xform2);
            bgfx::setUniform(uRot_, rot);
            bgfx::setTransform(world.m);
            bgfx::setVertexBuffer(0, p.vbo);
            bgfx::setIndexBuffer(p.ibo, 0, p.indexCount);
            bgfx::setTexture(0, sTex1_, layer.tex1);
            bgfx::setTexture(1, sTex2_, layer.tex2);
            bgfx::setTexture(2, sMask_, layer.mask);
            bgfx::setTexture(3, sLmap_, layer.lmap);
            // The mesh itself says which shells blend, via the "_trans_" prefix.
            // Depth is never written, so world geometry always paints on top.
            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA;
            if (p.blend) state |= BGFX_STATE_BLEND_ALPHA;
            bgfx::setState(state);
            bgfx::submit(view, program_);
        }
    }
}

} // namespace painful
