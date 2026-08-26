#include "EntityRenderer.h"
#include "../Core/Common.h"
#include "../Core/Log.h"
#include "MeshVertex.h"

#include <cmath>
#include <cstring>
#include <filesystem>

namespace painful {

namespace {

bgfx::ShaderHandle LoadShader(const std::string& path) {
    std::vector<uint8_t> data;
    if (!ReadFile(path, data) || data.empty()) return BGFX_INVALID_HANDLE;
    return bgfx::createShader(bgfx::copy(data.data(), static_cast<uint32_t>(data.size())));
}

// Row-vector transform: uniform scale, then rotation, then translation in row 3.
// "rot" is a row-major 3x3 rotation already in row-vector form.
Mat4 MakeTransform(const float pos[3], const float rot[9], float scale) {
    Mat4 m;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = rot[r * 3 + c] * scale;
        m.m[r * 4 + 3] = 0.f;
    }
    m.m[12] = pos[0]; m.m[13] = pos[1]; m.m[14] = pos[2]; m.m[15] = 1.f;
    return m;
}

// Placed objects store their orientation one of two ways, both straight out of
// the shipped data:
//
//     o.Rot = Quaternion:New(w, x, y, z)   -- 6039 instances
//     o.Ang.X / o.Ang.Y / o.Ang.Z          -- Euler radians
//
// Neither is universal, so whichever is present wins and identity is the
// fallback. Component order (w, x, y, z) and the matrix form were both read out
// of Engine.dll: PhysicsWorld::GetHavokBodyRotation stores Havok's (x,y,z,w)
// into the engine layout as (-w, x, y, z), and the engine's quaternion-to-
// matrix routine (FUN_1000bb90) emits the standard TEXTBOOK matrix, applied to
// row vectors as-is - NOT transposed into row-vector form. Pre-transposing
// here mirrored every rotation (+28 degrees rendered as -28).
void ReadRotation(const Properties& props, float out[9]) {
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(out, identity, sizeof(identity));

    if (const Value* q = props.Find("Rot")) {
        if (q->kind == Value::Kind::Ctor && q->args.size() >= 4) {
            const float w = q->Arg(0), x = q->Arg(1), y = q->Arg(2), z = q->Arg(3);
            const float n = std::sqrt(w * w + x * x + y * y + z * z);
            if (n > 1e-6f) {
                const float iw = w / n, ix = x / n, iy = y / n, iz = z / n;
                // Verbatim from the engine's own conversion.
                out[0] = 1 - 2 * (iy * iy + iz * iz);
                out[1] = 2 * (ix * iy - iz * iw);
                out[2] = 2 * (ix * iz + iy * iw);
                out[3] = 2 * (ix * iy + iz * iw);
                out[4] = 1 - 2 * (ix * ix + iz * iz);
                out[5] = 2 * (iy * iz - ix * iw);
                out[6] = 2 * (ix * iz - iy * iw);
                out[7] = 2 * (iy * iz + ix * iw);
                out[8] = 1 - 2 * (ix * ix + iy * iy);
                return;
            }
        }
    }

    if (!props.Has("Ang.X") && !props.Has("Ang.Y") && !props.Has("Ang.Z")) return;
    const float ax = float(props.Number("Ang.X", 0.0));
    const float ay = float(props.Number("Ang.Y", 0.0));
    const float az = float(props.Number("Ang.Z", 0.0));
    const float sx = std::sin(ax), cx = std::cos(ax);
    const float sy = std::sin(ay), cy = std::cos(ay);
    const float sz = std::sin(az), cz = std::cos(az);
    // Y (yaw) * X (pitch) * Z (roll), row-vector.
    out[0] = cy * cz + sy * sx * sz;  out[1] = cx * sz;  out[2] = -sy * cz + cy * sx * sz;
    out[3] = -cy * sz + sy * sx * cz; out[4] = cx * cz;  out[5] = sy * sz + cy * sx * cz;
    out[6] = sy * cx;                 out[7] = -sx;      out[8] = cy * cx;
}

// Looks the material up in the game's shader scripts; falls back to plain
// opaque state with the given winding when the library is missing.
MaterialState LookupMaterial(ShaderLibrary* lib, const std::string& name, bool cwFallback) {
    if (lib) {
        if (const ShaderDef* def = lib->Find(name); def && !def->passes.empty()) {
            std::string warn;
            MaterialState m = MaterialState::FromPass(def->passes.front(), &warn);
            if (!warn.empty()) LogWarn("material %s: %s", name.c_str(), warn.c_str());
            return m;
        }
        LogWarn("material not found: %s", name.c_str());
    }
    MaterialState m;
    m.state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
              BGFX_STATE_DEPTH_TEST_LESS |
              (cwFallback ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW);
    return m;
}

} // namespace

bool EntityRenderer::Init(const std::string& shaderDir) {
    layout_ = MakeMeshLayout();

    namespace fs = std::filesystem;
    bgfx::ShaderHandle vs = LoadShader((fs::path(shaderDir) / "vs_world.bin").string());
    bgfx::ShaderHandle fsh = LoadShader((fs::path(shaderDir) / "fs_world.bin").string());
    if (!bgfx::isValid(vs) || !bgfx::isValid(fsh)) return false;

    program_ = bgfx::createProgram(vs, fsh, true);
    if (!bgfx::isValid(program_)) return false;

    sDiffuse_  = bgfx::createUniform("s_diffuse",  bgfx::UniformType::Sampler);
    sLightmap_ = bgfx::createUniform("s_lightmap", bgfx::UniformType::Sampler);
    uParams_   = bgfx::createUniform("u_params",   bgfx::UniformType::Vec4);
    uAmbient_  = bgfx::createUniform("u_ambient",  bgfx::UniformType::Vec4);
    uFogColor_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
    return true;
}

void EntityRenderer::Shutdown() {
    for (GpuModel& model : models_) {
        for (Part& p : model.parts) {
            if (p.ownsVbo && bgfx::isValid(p.vbo)) bgfx::destroy(p.vbo);
            if (bgfx::isValid(p.ibo)) bgfx::destroy(p.ibo);
        }
    }
    models_.clear();
    instances_.clear();
    if (bgfx::isValid(program_))   { bgfx::destroy(program_);   program_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sDiffuse_))  { bgfx::destroy(sDiffuse_);  sDiffuse_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sLightmap_)) { bgfx::destroy(sLightmap_); sLightmap_ = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uParams_))   { bgfx::destroy(uParams_);   uParams_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uAmbient_))  { bgfx::destroy(uAmbient_);  uAmbient_  = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uFogColor_)) { bgfx::destroy(uFogColor_); uFogColor_ = BGFX_INVALID_HANDLE; }
}

bool EntityRenderer::GetModel(const std::string& modelName, TextureCache& textures,
                              const std::string& modelsRoot, size_t& outIndex) {
    auto it = modelIndex_.find(modelName);
    if (it != modelIndex_.end()) {
        outIndex = it->second;
        return true;
    }

    namespace fs = std::filesystem;
    fs::path path = fs::path(modelsRoot) / (modelName + ".pkmdl");
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;

    Model model;
    if (!Model::Load(path.string(), model) || model.meshes.empty()) return false;

    GpuModel gpu;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const ModelMesh& mesh : model.meshes) {
        const size_t vertexCount = mesh.vertexCount();
        if (vertexCount == 0 || mesh.indices.empty()) continue;

        std::vector<MeshVertex> verts(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            MeshVertex& v = verts[i];
            v.x  = mesh.verts[i * 8 + 0];
            v.y  = mesh.verts[i * 8 + 1];
            v.z  = mesh.verts[i * 8 + 2];
            v.nx = mesh.verts[i * 8 + 3];
            v.ny = mesh.verts[i * 8 + 4];
            v.nz = mesh.verts[i * 8 + 5];
            v.u0 = v.u1 = mesh.verts[i * 8 + 6];
            v.v0 = v.v1 = mesh.verts[i * 8 + 7];
            const float p[3] = {v.x, v.y, v.z};
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], p[a]);
                hi[a] = std::max(hi[a], p[a]);
            }
        }

        Part part;
        part.indexCount = uint32_t(mesh.indices.size());
        part.vbo = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(MeshVertex))), layout_);
        part.ibo = bgfx::createIndexBuffer(
            bgfx::copy(mesh.indices.data(), uint32_t(mesh.indices.size() * sizeof(uint16_t))));
        // Models carry one texture per material; the first covers the whole mesh.
        part.diffuse = mesh.materials.empty() ? textures.White()
                                              : textures.Get(mesh.materials[0], "");
        gpu.parts.push_back(part);
    }
    if (gpu.parts.empty()) return false;

    gpu.material = LookupMaterial(shaders_, "palskinned", true);
    outIndex = models_.size();
    // Bind-pose extent, used to interpret o.Scale as a real-world size.
    float extent = 0.f;
    for (int a = 0; a < 3; ++a) extent = std::max(extent, hi[a] - lo[a]);
    gpu.extent = extent > 1e-4f ? extent : 1.f;
    models_.push_back(std::move(gpu));
    modelIndex_[modelName] = outIndex;
    return true;
}

bool EntityRenderer::GetPack(const std::string& packName, const std::string& meshName,
                             TextureCache& textures, const std::string& itemsRoot,
                             size_t& outIndex) {
    const std::string key = packName + "/" + meshName;
    auto it = modelIndex_.find(key);
    if (it != modelIndex_.end()) {
        outIndex = it->second;
        return true;
    }

    namespace fs = std::filesystem;
    fs::path path = fs::path(itemsRoot) / packName;
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;

    DatPack pack;
    if (!DatPack::Load(path.string(), pack)) {
        LogWarn("pack %s: %s", packName.c_str(), pack.error.c_str());
        return false;
    }

    GpuModel gpu;
    bool materialSet = false;
    for (const MapObject& o : pack.objects) {
        // o.Mesh selects one object; when it matches nothing (or is empty),
        // every object is drawn - DEAD packs hold loose fragments.
        if (!meshName.empty() && o.name != meshName && pack.objects.size() > 1) continue;
        const size_t vertexCount = o.vertexCount();
        if (vertexCount == 0 || o.indices.empty()) continue;

        // Pack meshes are WorldMesh objects, so the world material families
        // apply: defaultNTU (1-UV) plus the usual name-substring variants.
        if (!materialSet) {
            std::string shaderName = "defaultNTU";
            const bool isTrans = o.nameHas("trans") || o.nameHas("decal");
            if (isTrans) shaderName += "trans";
            else if (o.nameHas("atest")) shaderName += "atest";
            if (o.nameHas("2sided")) shaderName += "2sided";
            gpu.material = LookupMaterial(shaders_, shaderName, false);
            materialSet = true;
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
            v.u0 = v.u1 = uv[0];
            v.v0 = v.v1 = uv[1];
        }
        const bgfx::VertexBufferHandle vbo = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(MeshVertex))), layout_);

        // One part per material run; anything the runs leave uncovered is a
        // couple of stray triangles at most. Parts of one object share the
        // vertex buffer; only the first part owns (and later destroys) it.
        const size_t partsBefore = gpu.parts.size();
        for (const Material& m : o.materials) {
            const uint32_t first = m.firstIndex;
            const uint32_t count = uint32_t(m.triangleCount) * 3;
            if (count == 0 || first + count > o.indices.size()) continue;
            Part part;
            part.vbo = vbo;
            part.ownsVbo = gpu.parts.size() == partsBefore;
            part.ibo = bgfx::createIndexBuffer(
                bgfx::copy(o.indices.data() + first, count * sizeof(uint16_t)));
            part.indexCount = count;
            part.diffuse = m.diffuse().empty() ? textures.White()
                                               : textures.Get(m.diffuse(), "");
            gpu.parts.push_back(part);
        }
        // No usable material runs: draw the whole object untextured.
        if (gpu.parts.size() == partsBefore) {
            Part part;
            part.vbo = vbo;
            part.ibo = bgfx::createIndexBuffer(bgfx::copy(
                o.indices.data(), uint32_t(o.indices.size() * sizeof(uint16_t))));
            part.indexCount = uint32_t(o.indices.size());
            part.diffuse = textures.White();
            gpu.parts.push_back(part);
        }
    }
    if (gpu.parts.empty()) return false;

    outIndex = models_.size();
    models_.push_back(std::move(gpu));
    modelIndex_[key] = outIndex;
    return true;
}

void EntityRenderer::Build(const Level& level, TemplateCache& templates,
                           TextureCache& textures, const std::string& dataRoot,
                           ShaderLibrary* shaders) {
    shaders_ = shaders;
    const std::string modelsRoot = dataRoot + "/Models";
    const std::string itemsRoot = dataRoot + "/Items";
    white_ = textures.White();

    for (const Entity& e : level.entities()) {
        if (e.baseObj.empty()) continue;

        // Scrolling barriers (the Slab class) start HIDDEN: Slab:OnPlay calls
        // Open(true) for any instance not marked Closed, which disables
        // drawing and sinks the plate below its start position until an
        // ambush raises it. Drawing them anyway paints floating plates the
        // player is never meant to see (all 26 in Cemetery, for example).
        // "Closed" only exists on this class, so its presence identifies one.
        if (e.props.Has("Closed") || templates.ResolveHas(e.baseObj, "Closed")) {
            const bool closed = e.props.Has("Closed")
                ? e.props.Bool("Closed", false)
                : templates.ResolveBool(e.baseObj, "Closed", false);
            if (!closed) { ++hidden_; continue; }
        }
        // The same start-invisible rule, stated directly.
        if (e.props.Has("Invisible")
                ? e.props.Bool("Invisible", false)
                : templates.ResolveBool(e.baseObj, "Invisible", false)) {
            ++hidden_;
            continue;
        }

        // Scale comes from the instance when it declares one, otherwise from
        // the template chain. Both o.Pack and o.Mesh resolve through the same
        // chain: templates usually declare them, instances rarely override.
        const double templateScale = templates.ResolveNumber(e.baseObj, "Scale", 1.0);
        const double scale = e.props.Has("Scale") ? e.props.Number("Scale", templateScale)
                                                  : templateScale;
        const std::string pack = e.props.Has("Pack")
            ? e.props.String("Pack") : templates.ResolveString(e.baseObj, "Pack");

        size_t modelSlot = 0;
        float finalScale = 0.f;
        if (!pack.empty()) {
            // Item meshes live inside a .dat pack; o.Mesh names the object.
            // Pack meshes share the world exporter's units, so o.Scale is a
            // plain multiplier (the slab door is 23.9 units at Scale 0.17,
            // about a 4 m doorway).
            const std::string meshName = e.props.Has("Mesh")
                ? e.props.String("Mesh") : templates.ResolveString(e.baseObj, "Mesh");
            if (!GetPack(pack, meshName, textures, itemsRoot, modelSlot)) {
                ++unresolved_;
                continue;
            }
            ++packed_;
            finalScale = float(scale);
        } else {
            std::string modelName = templates.ResolveString(e.baseObj, "Model");
            if (modelName.empty()) { ++unresolved_; continue; }
            if (!GetModel(modelName, textures, modelsRoot, modelSlot)) { ++unresolved_; continue; }
            // Models are created with Scale * 0.1 as a plain multiplier - the
            // rule is literal in the shipped scripts, for actors and items
            // alike:
            //   CActor.lua: ENTITY.Create(ETypes.Model, self.Model, ..., self.Scale*0.1)
            //   CItem.lua:  ENTITY.Create(ETypes.Model, self.Model, "",  self.Scale*0.1)
            finalScale = float(scale) * 0.1f;
        }

        Instance instance;
        instance.model = modelSlot;
        instance.pos[0] = e.pos[0];
        instance.pos[1] = e.pos[1];
        instance.pos[2] = e.pos[2];
        ReadRotation(e.props, instance.rot);
        instance.scale = finalScale;
        // Same math as SetScaleMultiplier: the layout scales about world zero.
        const float scaledPos[3] = {instance.pos[0] * scaleMultiplier_,
                                    instance.pos[1] * scaleMultiplier_,
                                    instance.pos[2] * scaleMultiplier_};
        instance.transform = MakeTransform(scaledPos, instance.rot,
                                           finalScale * scaleMultiplier_);
        instances_.push_back(instance);
    }
}

void EntityRenderer::SetScaleMultiplier(float k) {
    if (k == scaleMultiplier_) return;
    scaleMultiplier_ = k;
    // The whole layout scales about the SHARED origin - world (0,0,0), the
    // frame every o.Pos is expressed in - so positions scale together with
    // sizes. A multiplier that makes everything land correctly would expose a
    // hidden unit factor in the entity coordinates.
    for (Instance& instance : instances_) {
        const float pos[3] = {instance.pos[0] * k, instance.pos[1] * k,
                              instance.pos[2] * k};
        instance.transform = MakeTransform(pos, instance.rot,
                                           instance.scale * k);
    }
}

void EntityRenderer::Draw(bgfx::ViewId view, const float ambient[3], const float fogColor[3],
                          float fogDensity, float fogStart) {
    drawCalls_ = 0;
    if (!bgfx::isValid(program_) || instances_.empty()) return;

    const float fogValue[4] = {fogColor[0] / 255.f, fogColor[1] / 255.f, fogColor[2] / 255.f, 1.f};

    for (const Instance& instance : instances_) {
        const GpuModel& model = models_[instance.model];
        // No lightmaps on entities, so u_ambient.w (the lightmap scale) is
        // never sampled; alpha test comes from the material scripts.
        const float ambientValue[4] = {ambient[0] / 255.f, ambient[1] / 255.f,
                                       ambient[2] / 255.f, model.material.lightScale};
        const float params[4] = {0.f, model.material.alphaRef, fogDensity, fogStart};

        uint64_t state = model.material.state | BGFX_STATE_MSAA;
        // Diagnostic override: --ecull none strips culling, cw/ccw force it.
        if (cullMode_ == 2) {
            state &= ~BGFX_STATE_CULL_MASK;
        }

        for (const Part& part : model.parts) {
            bgfx::setUniform(uAmbient_, ambientValue);
            bgfx::setUniform(uFogColor_, fogValue);
            bgfx::setUniform(uParams_, params);
            bgfx::setTransform(instance.transform.m);
            bgfx::setVertexBuffer(0, part.vbo);
            bgfx::setIndexBuffer(part.ibo, 0, part.indexCount);
            bgfx::setTexture(0, sDiffuse_, part.diffuse, model.material.sampler[0]);
            bgfx::setTexture(1, sLightmap_, white_);
            bgfx::setState(state);
            bgfx::submit(view, program_);
            ++drawCalls_;
        }
    }
}

} // namespace painful
