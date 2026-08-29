#include "EntityRenderer.h"
#include "../Core/Common.h"
#include "../Core/FileSystem.h"
#include "../Core/Log.h"
#include "Frustum.h"
#include "MeshVertex.h"

#include <algorithm>
#include <bx/math.h>
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

// Quiet lookup for per-object material overrides (a script named exactly
// after the mesh, like skin.shader's "polySurfaceShape847" or lm.shader's
// "tasmashape" conveyor). Most meshes have none; that is not an error.
const ShaderDef* FindByName(ShaderLibrary* lib, const std::string& name) {
    if (!lib || name.empty()) return nullptr;
    const ShaderDef* def = lib->Find(name);
    return (def && !def->passes.empty()) ? def : nullptr;
}

// Looks the material up in the game's shader scripts; falls back to plain
// opaque state with the given winding when the library is missing.
MaterialState LookupMaterial(ShaderLibrary* lib, const std::string& name, bool cwFallback,
                             const std::string& overrideName = "") {
    if (lib) {
        const ShaderDef* def = FindByName(lib, overrideName);
        if (!def) {
            def = lib->Find(name);
            if (def && def->passes.empty()) def = nullptr;
        }
        if (def) {
            std::string warn;
            MaterialState m = MaterialState::FromPass(def->passes.front(), &warn);
            if (!warn.empty()) LogWarn("material %s: %s", def->name.c_str(), warn.c_str());
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
    uFog_      = bgfx::createUniform("u_fog",      bgfx::UniformType::Vec4);
    uUvAnim_   = bgfx::createUniform("u_uvanim",   bgfx::UniformType::Vec4);
    uDetail_   = bgfx::createUniform("u_detail",   bgfx::UniformType::Vec4);
    sDetail_   = bgfx::createUniform("s_detail",   bgfx::UniformType::Sampler);
    uUv0_      = bgfx::createUniform("u_uv0",      bgfx::UniformType::Vec4);
    uUv1_      = bgfx::createUniform("u_uv1",      bgfx::UniformType::Vec4);
    uTile_     = bgfx::createUniform("u_tile",     bgfx::UniformType::Vec4);
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
    if (bgfx::isValid(uFog_))      { bgfx::destroy(uFog_);      uFog_      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uUvAnim_))   { bgfx::destroy(uUvAnim_);   uUvAnim_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uDetail_))   { bgfx::destroy(uDetail_);   uDetail_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(sDetail_))   { bgfx::destroy(sDetail_);   sDetail_   = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uUv0_))      { bgfx::destroy(uUv0_);      uUv0_      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uUv1_))      { bgfx::destroy(uUv1_);      uUv1_      = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(uTile_))     { bgfx::destroy(uTile_);     uTile_     = BGFX_INVALID_HANDLE; }
}

bool EntityRenderer::GetModel(const std::string& modelName, TextureCache& textures,
                              const std::string& modelsRoot, size_t& outIndex) {
    auto it = modelIndex_.find(modelName);
    if (it != modelIndex_.end()) {
        outIndex = it->second;
        return true;
    }

    const std::string path = modelsRoot + "/" + modelName + ".pkmdl";
    if (!FileSystem::Get().Exists(path)) return false;

    Model model;
    if (!Model::Load(path, model) || model.meshes.empty()) return false;

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
        // Keep the source mesh only when it is actually skinned: posing needs
        // the bind-pose vertices and the weights back, and an unskinned model
        // would be paying for a copy nothing ever reads.
        if (mesh.hasSkin()) part.cpu = mesh;
        part.indexCount = uint32_t(mesh.indices.size());
        part.vbo = bgfx::createVertexBuffer(
            bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(MeshVertex))), layout_);
        part.ibo = bgfx::createIndexBuffer(
            bgfx::copy(mesh.indices.data(), uint32_t(mesh.indices.size() * sizeof(uint16_t))));
        // Models carry one texture per material; the first covers the whole mesh.
        part.diffuse = mesh.materials.empty() ? textures.White()
                                              : textures.Get(mesh.materials[0], "");
        // The override key is the MESH name, matching how the world path keys
        // off each object's name. Swamp_dirtywater.pkmdl holds a mesh called
        // "dirtywater", which is the skin.shader entry that makes the swamp
        // water scroll; keying off the file name found nothing.
        part.material = LookupMaterial(shaders_, "palskinned", true, mesh.name);
        gpu.parts.push_back(part);
    }
    if (gpu.parts.empty()) return false;

    // Whether this model can be posed at all. The skeleton itself lives with
    // the script side now (SkeletonCache): the joint natives have to answer
    // where a bone is with no window open, and a pose computed twice is a pose
    // that can disagree with itself.
    if (!model.bones.empty())
        for (const Part& p : gpu.parts)
            if (p.cpu.hasSkin()) { gpu.skinned = true; break; }

    gpu.material = LookupMaterial(shaders_, "palskinned", true, modelName);
    outIndex = models_.size();
    // Bind-pose extent, used to interpret o.Scale as a real-world size.
    float extent = 0.f;
    for (int a = 0; a < 3; ++a) {
        extent = std::max(extent, hi[a] - lo[a]);
        gpu.bboxLo[a] = lo[a];
        gpu.bboxHi[a] = hi[a];
    }
    gpu.extent = extent > 1e-4f ? extent : 1.f;
    gpu.name = modelName;
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

    const std::string path = itemsRoot + "/" + packName;
    if (!FileSystem::Get().Exists(path)) return false;

    DatPack pack;
    if (!DatPack::Load(path, pack)) {
        LogWarn("pack %s: %s", packName.c_str(), pack.error.c_str());
        return false;
    }

    GpuModel gpu;
    bool materialSet = false;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
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
            gpu.material = LookupMaterial(shaders_, shaderName, false, o.name);
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
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], o.bboxMin[a]);
            hi[a] = std::max(hi[a], o.bboxMax[a]);
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
    // Pack objects share one material across the whole object, unlike models.
    for (Part& part : gpu.parts) part.material = gpu.material;

    for (int a = 0; a < 3; ++a) {
        gpu.bboxLo[a] = lo[a];
        gpu.bboxHi[a] = hi[a];
    }
    outIndex = models_.size();
    gpu.name = key;
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

    const std::vector<Entity>& placedEntities = level.entities();
    for (size_t entityIndex = 0; entityIndex < placedEntities.size(); ++entityIndex) {
        const Entity& e = placedEntities[entityIndex];
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

        // Instance properties win over the BaseObj chain, for all of these.
        const double scale = templates.ResolveNumber(e.props, e.baseObj, "Scale", 1.0);
        const std::string pack = templates.ResolveString(e.props, e.baseObj, "Pack");

        size_t modelSlot = 0;
        float finalScale = 0.f;
        if (!pack.empty()) {
            // Item meshes live inside a .dat pack; o.Mesh names the object.
            // Pack meshes share the world exporter's units, so o.Scale is a
            // plain multiplier (the slab door is 23.9 units at Scale 0.17,
            // about a 4 m doorway).
            const std::string meshName = templates.ResolveString(e.props, e.baseObj, "Mesh");
            if (!GetPack(pack, meshName, textures, itemsRoot, modelSlot)) {
                ++unresolved_;
                continue;
            }
            ++packed_;
            finalScale = float(scale);
        } else {
            std::string modelName = templates.ResolveString(e.props, e.baseObj, "Model");
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
        instance.entity = entityIndex;
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
        UpdateBounds(instance, models_[modelSlot]);
        instances_.push_back(instance);
    }
}

int EntityRenderer::CreateScriptModel(const std::string& modelName, float scale,
                                      TextureCache& textures,
                                      const std::string& modelsRoot) {
    size_t slot = 0;
    if (modelName.empty() || !GetModel(modelName, textures, modelsRoot, slot))
        return -1;
    Instance instance;
    instance.model = slot;
    instance.scale = scale;
    instance.entity = SIZE_MAX;
    instance.transform = MakeTransform(instance.pos, instance.rot, scale);
    UpdateBounds(instance, models_[slot]);
    instances_.push_back(instance);
    return int(instances_.size() - 1);
}

int EntityRenderer::CreateScriptPack(const std::string& packName,
                                     const std::string& meshName, float scale,
                                     TextureCache& textures,
                                     const std::string& itemsRoot) {
    size_t slot = 0;
    if (packName.empty() || !GetPack(packName, meshName, textures, itemsRoot, slot))
        return -1;
    Instance instance;
    instance.model = slot;
    instance.scale = scale;
    instance.entity = SIZE_MAX;
    instance.transform = MakeTransform(instance.pos, instance.rot, scale);
    UpdateBounds(instance, models_[slot]);
    instances_.push_back(instance);
    return int(instances_.size() - 1);
}

void EntityRenderer::SetScriptPose(int slot, const float pos[3], const float rotWXYZ[4]) {
    if (slot < 0 || size_t(slot) >= instances_.size()) return;
    Instance& instance = instances_[slot];
    for (int c = 0; c < 3; ++c) instance.pos[c] = pos[c];
    EngineQuatToRot9(rotWXYZ, instance.rot);
    instance.transform = MakeTransform(instance.pos, instance.rot, instance.scale);
    UpdateBounds(instance, models_[instance.model]);
}

void EntityRenderer::SetScriptSkinning(int slot, const Mat4* skin, size_t count) {
    if (slot < 0 || size_t(slot) >= instances_.size()) return;
    Instance& inst = instances_[slot];
    if (!skin || count == 0) { inst.skin.clear(); return; }
    inst.skin.assign(skin, skin + count);
}

void EntityRenderer::SetScriptVisible(int slot, bool visible) {
    if (slot < 0 || size_t(slot) >= instances_.size()) return;
    instances_[slot].visible = visible;
}

void EntityRenderer::ReleaseScript(int slot) {
    if (slot < 0 || size_t(slot) >= instances_.size()) return;
    Instance& inst = instances_[slot];
    // A posed buffer belongs to the instance, so it dies with it. Slots are
    // reused, and leaving these behind would leak one buffer per projectile
    // and per corpse for the life of the level.
    for (bgfx::DynamicVertexBufferHandle h : inst.posed)
        if (bgfx::isValid(h)) bgfx::destroy(h);
    inst.posed.clear();
    inst.alive = false;
}

bool EntityRenderer::GetScriptDimensions(int slot, float out[3]) const {
    if (slot < 0 || size_t(slot) >= instances_.size()) return false;
    const Instance& instance = instances_[slot];
    const GpuModel& model = models_[instance.model];
    for (int i = 0; i < 3; ++i)
        out[i] = (model.bboxHi[i] - model.bboxLo[i]) * instance.scale;
    return true;
}

void EntityRenderer::SetEntityPose(size_t entityIndex, const float pos[3], const float rot[9]) {
    for (Instance& instance : instances_) {
        if (instance.entity != entityIndex) continue;
        for (int c = 0; c < 3; ++c) instance.pos[c] = pos[c];
        for (int c = 0; c < 9; ++c) instance.rot[c] = rot[c];
        const float scaledPos[3] = {instance.pos[0] * scaleMultiplier_,
                                    instance.pos[1] * scaleMultiplier_,
                                    instance.pos[2] * scaleMultiplier_};
        instance.transform = MakeTransform(scaledPos, instance.rot,
                                           instance.scale * scaleMultiplier_);
        UpdateBounds(instance, models_[instance.model]);
        return;
    }
}

void EntityRenderer::UpdateBounds(Instance& instance, const GpuModel& model) const {
    instance.aabbLo[0] = instance.aabbLo[1] = instance.aabbLo[2] = 1e30f;
    instance.aabbHi[0] = instance.aabbHi[1] = instance.aabbHi[2] = -1e30f;
    for (int corner = 0; corner < 8; ++corner) {
        const float local[3] = {corner & 1 ? model.bboxHi[0] : model.bboxLo[0],
                                corner & 2 ? model.bboxHi[1] : model.bboxLo[1],
                                corner & 4 ? model.bboxHi[2] : model.bboxLo[2]};
        float w[3];
        instance.transform.TransformPoint(local[0], local[1], local[2], w);
        for (int a = 0; a < 3; ++a) {
            instance.aabbLo[a] = std::min(instance.aabbLo[a], w[a]);
            instance.aabbHi[a] = std::max(instance.aabbHi[a], w[a]);
        }
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
        UpdateBounds(instance, models_[instance.model]);
    }
}

void EntityRenderer::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
                          const LevelInfo& info, float timeSeconds) {
    const float* ambient = info.ambient;
    drawCalls_ = 0;
    posedInstances_ = 0;
    posedModels_.clear();
    if (!bgfx::isValid(program_) || instances_.empty()) return;

    // Same view setup as the world pass, rebuilt here for the frustum.
    float forward[3];
    camera.Forward(forward);
    const bx::Vec3 eye = {camera.pos[0], camera.pos[1], camera.pos[2]};
    const bx::Vec3 at = {camera.pos[0] + forward[0], camera.pos[1] + forward[1],
                         camera.pos[2] + forward[2]};
    float viewMtx[16], projMtx[16];
    bx::mtxLookAt(viewMtx, eye, at, {0.0f, 1.0f, 0.0f}, bx::Handedness::Right);
    bx::mtxProj(projMtx, camera.fovDegrees, float(width) / float(height),
                camera.nearPlane, camera.farPlane, bgfx::getCaps()->homogeneousDepth,
                bx::Handedness::Right);
    const Frustum frustum = Frustum::FromViewProj(viewMtx, projMtx);

    const float fogValue[4] = {info.fogColor[0] / 255.f, info.fogColor[1] / 255.f,
                               info.fogColor[2] / 255.f, 1.f};
    const float fogParams[4] = {float(info.fogMode), info.fogStart, info.fogEnd,
                                info.fogDensity};
    bgfx::setUniform(uFog_, fogParams);

    for (Instance& instance : instances_) {
        if (!instance.alive || !instance.visible) continue;
        if (visCulling_ && !frustum.VisibleAabb(instance.aabbLo, instance.aabbHi)) continue;
        const GpuModel& model = models_[instance.model];

        // Pose it, if it is playing something. This is CPU skinning: the
        // matrices are built once per instance and each part is deformed into
        // a buffer of its own. Only instances that survived the frustum test
        // get here, so an actor across the level costs nothing.
        //
        // GPU skinning is the end state (see Docs/Animation.md); this exists
        // first because SkinMesh was already checked against a known-good
        // reference, which makes it the oracle to diff a shader against.
        const bool posing = model.skinned && !instance.skin.empty();
        if (posing) {
            ++posedInstances_;
            posedModels_.insert(model.name);
            instance.posed.resize(model.parts.size(), BGFX_INVALID_HANDLE);
            for (size_t i = 0; i < model.parts.size(); ++i) {
                const Part& part = model.parts[i];
                if (!part.cpu.hasSkin()) continue;
                // A posed buffer holds this part's vertices alone, so its
                // indices must start at zero. Parts of a .dat pack object share
                // one buffer and index into the middle of it - those are never
                // skinned today, and this keeps it that way rather than
                // reading off the end if one ever is.
                if (!part.ownsVbo) continue;
                SkinMeshVertices(part.cpu, instance.skin, vertScratch_);
                const uint32_t bytes =
                    uint32_t(part.cpu.vertexCount() * sizeof(MeshVertex));
                if (!bgfx::isValid(instance.posed[i]))
                    instance.posed[i] = bgfx::createDynamicVertexBuffer(
                        uint32_t(part.cpu.vertexCount()), layout_);
                // vertScratch_ is the source layout - 8 floats per vertex,
                // which is exactly MeshVertex minus the duplicated second UV
                // set, so it is rebuilt rather than memcpy'd.
                posedVerts_.resize(part.cpu.vertexCount());
                for (size_t v = 0; v < part.cpu.vertexCount(); ++v) {
                    MeshVertex& out = posedVerts_[v];
                    out.x  = vertScratch_[v * 8 + 0];
                    out.y  = vertScratch_[v * 8 + 1];
                    out.z  = vertScratch_[v * 8 + 2];
                    out.nx = vertScratch_[v * 8 + 3];
                    out.ny = vertScratch_[v * 8 + 4];
                    out.nz = vertScratch_[v * 8 + 5];
                    out.u0 = out.u1 = vertScratch_[v * 8 + 6];
                    out.v0 = out.v1 = vertScratch_[v * 8 + 7];
                }
                bgfx::update(instance.posed[i], 0,
                             bgfx::copy(posedVerts_.data(), bytes));
            }
        }
        size_t partIndex = 0;
        const float detail[4] = {1.f, 1.f, 0.f, 0.f};
        // Identity UV transform: entity meshes carry no per-slot xform.
        const float identityUv[4] = {1.f, 1.f, 0.f, 0.f};

        for (const Part& part : model.parts) {
            // Material is per part: one model can mix an ordinary skinned mesh
            // with a scrolling water surface.
            const MaterialState& mat = part.material;
            // No lightmaps on entities, so u_ambient.w (the lightmap scale) is
            // never sampled; alpha test comes from the material scripts.
            const float ambientValue[4] = {ambient[0] / 255.f, ambient[1] / 255.f,
                                           ambient[2] / 255.f, mat.lightScale};
            const float params[4] = {0.f, mat.alphaRef, 0.f, 0.f};
            // Animated materials pan their diffuse UVs; no detail maps here.
            const float uvAnim[4] = {mat.pan0[0] * timeSeconds, mat.pan0[1] * timeSeconds,
                                     mat.pan1[0] * timeSeconds, mat.pan1[1] * timeSeconds};
            const float tile[4] = {mat.tile0[0], mat.tile0[1], mat.tile1[0], mat.tile1[1]};
            uint64_t state = mat.state | BGFX_STATE_MSAA;
            // Diagnostic override: --ecull none strips culling, cw/ccw force it.
            if (cullMode_ == 2) state &= ~BGFX_STATE_CULL_MASK;

            bgfx::setUniform(uAmbient_, ambientValue);
            bgfx::setUniform(uFogColor_, fogValue);
            bgfx::setUniform(uParams_, params);
            bgfx::setUniform(uUvAnim_, uvAnim);
            bgfx::setUniform(uDetail_, detail);
            bgfx::setUniform(uUv0_, identityUv);
            bgfx::setUniform(uUv1_, identityUv);
            bgfx::setUniform(uTile_, tile);
            bgfx::setTransform(instance.transform.m);
            // The posed buffer when there is one, the shared bind-pose buffer
            // otherwise. Indices never change: skinning moves vertices, it
            // does not retopologise.
            const bool usePosed = posing && partIndex < instance.posed.size() &&
                                  bgfx::isValid(instance.posed[partIndex]);
            if (usePosed) bgfx::setVertexBuffer(0, instance.posed[partIndex]);
            else          bgfx::setVertexBuffer(0, part.vbo);
            ++partIndex;
            bgfx::setIndexBuffer(part.ibo, 0, part.indexCount);
            bgfx::setTexture(0, sDiffuse_, part.diffuse, mat.sampler[0]);
            bgfx::setTexture(1, sLightmap_, white_);
            bgfx::setTexture(2, sDetail_, white_);
            bgfx::setState(state);
            bgfx::submit(view, program_);
            ++drawCalls_;
        }
    }
}

} // namespace painful
