#include "EntityRenderer.h"
#include "ShaderLoad.h"
#include "ShadowMap.h"
#include "LightShadowAtlas.h"
#include "ViewModelShadows.h"
#include "../Core/Vectors.h"
#include "../Core/Check.h"
#include "../Core/Debug.h"
#include "../Core/Matrix.h"
#include "../Core/FileSystem.h"
#include "../Core/Frustum.h"
#include "../Core/Log.h"
#include "GpuBuffers.h"
#include "MeshVertex.h"
#include "TextureFilter.h"

#include <algorithm>
#include <cctype>
#include <bx/math.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace painful {

namespace {

// Mesh names come from the model file; the scripts spell them by hand.
bool EqualsNoCase(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(a[i])) !=
				std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	return true;
}
// Row-vector transform: uniform scale, then rotation, then translation in row 3.
// "rot9" is a row-major 3x3 rotation already in row-vector form.
Mat4 MakeTransform(const Vec3& pos, const float rot9[9], float scale) {
	Mat4 m;
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = rot9[r * 3 + c] * scale;
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

// The specular exponent and strength. skin.shader only says `specular true`,
// leaving the numbers to the fixed-function material, so these are tuned to the
// look rather than read from data. The half-vector is per pixel now, from the
// real eye, so the sheen can be tighter than the original's camera-facing wash
// without reading as wrong. PAINFUL_SPECULAR overrides them as
// "exponent,strength,gate" while that is being judged.
const float* SpecularParams() {
	// z softens the N.L gate on the specular. The original switches on it
	// hard, and can only do that because it lights per vertex and interpolates
	// the result; per pixel the same switch draws a visible line. This is that
	// interpolation put back as a ramp - roughly how much N.L varies across one
	// triangle.
	static float v[4] = {12.f, 0.35f, 0.25f, 0.f};
	static const bool once = [] {
		if (const char* s = DebugText("PAINFUL_SPECULAR"))
			sscanf(s, "%f,%f,%f", &v[0], &v[1], &v[2]);
		return true;
	}();
	(void)once;
	return v;
}

} // namespace

bool EntityRenderer::Init(const std::string& shaderDir) {
	layout_ = MakeMeshLayout();

	namespace fs = std::filesystem;
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_entity");
	bgfx::ShaderHandle fsh = LoadShader(shaderDir, "fs_entity");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fsh)) return false;

	program_ = bgfx::createProgram(vs, fsh, true);
	if (!bgfx::isValid(program_)) return false;

	sDiffuse_ = bgfx::createUniform("s_diffuse", bgfx::UniformType::Sampler);
	sLightmap_ = bgfx::createUniform("s_lightmap", bgfx::UniformType::Sampler);
	uParams_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
	uAmbient_ = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
	uFogColor_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
	uFog_ = bgfx::createUniform("u_fog", bgfx::UniformType::Vec4);
	uUvAnim_ = bgfx::createUniform("u_uvanim", bgfx::UniformType::Vec4);
	uDetail_ = bgfx::createUniform("u_detail", bgfx::UniformType::Vec4);
	sDetail_ = bgfx::createUniform("s_detail", bgfx::UniformType::Sampler);
	uUv0_ = bgfx::createUniform("u_uv0", bgfx::UniformType::Vec4);
	uUv1_ = bgfx::createUniform("u_uv1", bgfx::UniformType::Vec4);
	uTile_ = bgfx::createUniform("u_tile", bgfx::UniformType::Vec4);
	uSpecular_ = bgfx::createUniform("u_specular", bgfx::UniformType::Vec4);
	sStage1_ = bgfx::createUniform("s_stage1", bgfx::UniformType::Sampler);
	uStage1_ = bgfx::createUniform("u_stage1", bgfx::UniformType::Vec4);
	uVmParams_ = bgfx::createUniform("u_vmParams", bgfx::UniformType::Vec4);
	uVmMtx_ = bgfx::createUniform("u_vmMtx", bgfx::UniformType::Mat4);
	uVmLight_ = bgfx::createUniform("u_vmLight", bgfx::UniformType::Vec4);
	sVmShadow_ = bgfx::createUniform("s_vmShadow", bgfx::UniformType::Sampler);
	uDirColor_ = bgfx::createUniform("u_dirColor", bgfx::UniformType::Vec4);
	uDirDir_ = bgfx::createUniform("u_dirDir", bgfx::UniformType::Vec4);
	uEye_ = bgfx::createUniform("u_eye", bgfx::UniformType::Vec4);
	lightUniforms_.Init();
	return true;
}

void EntityRenderer::Shutdown() {
	for (GpuModel& model : models_) {
		for (Part& p : model.parts) {
			if (p.ownsVbo && bgfx::isValid(p.vbo)) bgfx::destroy(p.vbo);
			if (p.ownsIbo && bgfx::isValid(p.ibo)) bgfx::destroy(p.ibo);
		}
	}
	models_.clear();
	instances_.clear();
	if (bgfx::isValid(program_)) { bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sDiffuse_)) { bgfx::destroy(sDiffuse_); sDiffuse_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sLightmap_)) { bgfx::destroy(sLightmap_); sLightmap_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uParams_)) { bgfx::destroy(uParams_); uParams_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uAmbient_)) { bgfx::destroy(uAmbient_); uAmbient_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uFogColor_)) { bgfx::destroy(uFogColor_); uFogColor_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uFog_)) { bgfx::destroy(uFog_); uFog_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uUvAnim_)) { bgfx::destroy(uUvAnim_); uUvAnim_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uDetail_)) { bgfx::destroy(uDetail_); uDetail_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sDetail_)) { bgfx::destroy(sDetail_); sDetail_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sStage1_)) { bgfx::destroy(sStage1_); sStage1_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uStage1_)) { bgfx::destroy(uStage1_); uStage1_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uVmParams_)) { bgfx::destroy(uVmParams_); uVmParams_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uVmMtx_)) { bgfx::destroy(uVmMtx_); uVmMtx_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uVmLight_)) { bgfx::destroy(uVmLight_); uVmLight_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sVmShadow_)) { bgfx::destroy(sVmShadow_); sVmShadow_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uUv0_)) { bgfx::destroy(uUv0_); uUv0_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uUv1_)) { bgfx::destroy(uUv1_); uUv1_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uTile_)) { bgfx::destroy(uTile_); uTile_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uSpecular_)) { bgfx::destroy(uSpecular_); uSpecular_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uDirColor_)) { bgfx::destroy(uDirColor_); uDirColor_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uDirDir_)) { bgfx::destroy(uDirDir_); uDirDir_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uEye_)) { bgfx::destroy(uEye_); uEye_ = BGFX_INVALID_HANDLE; }
	lightUniforms_.Shutdown();
	projector_.Clear();
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
	Vec3 lo(1e30f), hi(-1e30f);
	for (const ModelMesh& mesh : model.meshes) {
		const size_t vertexCount = mesh.vertexCount();
		if (vertexCount == 0 || mesh.indices.empty()) continue;

		std::vector<MeshVertex> verts(vertexCount);
		for (size_t i = 0; i < vertexCount; ++i) {
			MeshVertex& v = verts[i];
			v.x = mesh.verts[i * 8 + 0];
			v.y = mesh.verts[i * 8 + 1];
			v.z = mesh.verts[i * 8 + 2];
			v.nx = mesh.verts[i * 8 + 3];
			v.ny = mesh.verts[i * 8 + 4];
			v.nz = mesh.verts[i * 8 + 5];
			v.u0 = v.u1 = mesh.verts[i * 8 + 6];
			v.v0 = v.v1 = mesh.verts[i * 8 + 7];
			const Vec3 p{v.x, v.y, v.z};
			for (int a = 0; a < 3; ++a) {
				lo[a] = std::min(lo[a], p[a]);
				hi[a] = std::max(hi[a], p[a]);
			}
		}

		// ONE PART PER MATERIAL SLOT. The slots are triangle runs over a shared
		// vertex array (see ModelMaterial), so the vertices - and, when the mesh
		// is skinned, the single posed buffer built from them - are shared, and
		// each slot brings only its own slice of the index array. Drawing the
		// whole mesh with materials[0] left every later run wearing the first
		// run's texture, which on a monk reads as transparent holes: nun.pkmdl
		// paints 148 triangles with NUNtexture4 and the next 240 with
		// NUNtexture2, and apoc_zombie splits 7 of its 12 meshes this way.
		const bgfx::VertexBufferHandle vbo = MakeVertexBuffer(
				verts.data(), uint32_t(verts.size() * sizeof(MeshVertex)), layout_);
		const bgfx::IndexBufferHandle ibo =
			MakeIndexBuffer(mesh.indices.data(), uint32_t(mesh.indices.size()));
		const uint32_t ownerIndex = uint32_t(gpu.parts.size());
		const size_t slots = std::max<size_t>(mesh.materials.size(), 1);
		for (size_t s = 0; s < slots; ++s) {
			uint32_t first = 0, count = uint32_t(mesh.indices.size());
			if (s < mesh.materials.size()) {
				first = mesh.materials[s].firstIndex;
				count = mesh.materials[s].triangles * 3;
			}
			if (count == 0 || first + count > mesh.indices.size()) continue;

			Part part;
			// Keep the source mesh only when it is actually skinned: posing
			// needs the bind-pose vertices and the weights back, and an
			// unskinned model would be paying for a copy nothing ever reads.
			// Only the owning slot keeps it - the posed buffer is shared.
			if (mesh.hasSkin() && s == 0) {
				part.cpu = mesh;
				for (const std::vector<SkinInfluence>& v : mesh.skin)
					for (const SkinInfluence& inf : v)
						part.maxBone = std::max(part.maxBone, inf.bone);
			}
			part.name = mesh.name;
			part.vbo = vbo;
			part.ownsVbo = gpu.parts.size() == ownerIndex;
			part.vboOwner = ownerIndex;
			part.ibo = ibo;
			part.ownsIbo = part.ownsVbo;
			part.firstIndex = first;
			part.indexCount = count;
			part.diffuse = s < mesh.materials.size()
					? textures.Get(mesh.materials[s].texture, "")
					: textures.White();
			// The override key is the MESH name, matching how the world path keys
			// off each object's name. Swamp_dirtywater.pkmdl holds a mesh called
			// "dirtywater", which is the skin.shader entry that makes the swamp
			// water scroll; keying off the file name found nothing.
			// THE MESH NAME PICKS THE SHADER FAMILY, for models exactly as it does
			// for pack meshes a few lines down. evilmonkv2 has meshes called
			// "polySurfa_2sided" (the robe) and "spodnica_2sided" (the skirt), and
			// skin.shader ships `shader palskinned2sided copy palskinned { pass {
			// cull none } }` for precisely them. Asking for plain palskinned
			// backface-culls the robe, so half of it disappears and the monk looks
			// like it has holes cut in it.
			//
			// The whole palskinned family has 2sided variants - _bloody, _freeze,
			// _water, add, emissive - so the suffix goes on whatever the override
			// resolved to rather than only on the default.
			std::string family = "palskinned";
			if (mesh.nameHas("2sided")) family += "2sided";
			part.material = LookupMaterial(shaders_, family, true, mesh.name);
			if (!part.material.map1.empty())
				part.stage1 = textures.Get(part.material.map1, "");
			gpu.parts.push_back(std::move(part));
		}
		// Every slot was empty or out of range: the vertices have no owner.
		if (gpu.parts.size() == ownerIndex) bgfx::destroy(vbo);
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
	Vec3 lo(1e30f), hi(-1e30f);
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
			Vec3 p, n;
			float uv[2];
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
		const bgfx::VertexBufferHandle vbo = MakeVertexBuffer(
				verts.data(), uint32_t(verts.size() * sizeof(MeshVertex)), layout_);
		const bgfx::IndexBufferHandle ibo =
			MakeIndexBuffer(o.indices.data(), uint32_t(o.indices.size()));

		// One part per material run; anything the runs leave uncovered is a
		// couple of stray triangles at most. Parts of one object share the
		// vertex and index buffers; only the first part owns (and destroys) them.
		const size_t partsBefore = gpu.parts.size();
		for (const Material& m : o.materials) {
			const uint32_t first = m.firstIndex;
			const uint32_t count = uint32_t(m.triangleCount) * 3;
			if (count == 0 || first + count > o.indices.size()) continue;
			Part part;
			part.vbo = vbo;
			part.ibo = ibo;
			part.ownsVbo = part.ownsIbo = gpu.parts.size() == partsBefore;
			part.vboOwner = uint32_t(partsBefore);
			part.firstIndex = first;
			part.indexCount = count;
			part.diffuse = m.diffuse().empty() ? textures.White()
					: textures.Get(m.diffuse(), "");
			gpu.parts.push_back(part);
		}
		// No usable material runs: draw the whole object untextured.
		if (gpu.parts.size() == partsBefore) {
			Part part;
			part.vbo = vbo;
			part.ibo = ibo;
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

void EntityRenderer::BuildLighting(const Level& level, TemplateCache& templates,
		bool lightsFromScripts) {
	lighting_.Build(level, templates, lightsFromScripts);
}

void EntityRenderer::Build(const Level& level, TemplateCache& templates,
		TextureCache& textures, const std::string& dataRoot,
		ShaderLibrary* shaders) {
	shaders_ = shaders;
	const std::string modelsRoot = dataRoot + "/Models";
	const std::string itemsRoot = dataRoot + "/Items";
	white_ = textures.White();

	// The lights and CEnvironment boxes this level places. Models carry no
	// lightmap, so this is the whole of their lighting.
	BuildLighting(level, templates);

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
		ReadRotation(e.props, instance.rot9);
		instance.scale = finalScale;
		// Same math as SetScaleMultiplier: the layout scales about world zero.
		const Vec3 scaledPos{instance.pos[0] * scaleMultiplier_,
									instance.pos[1] * scaleMultiplier_,
									instance.pos[2] * scaleMultiplier_};
		instance.transform = MakeTransform(scaledPos, instance.rot9,
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
	instance.transform = MakeTransform(instance.pos, instance.rot9, scale);
	UpdateBounds(instance, models_[slot]);
	instances_.push_back(instance);
	return int(instances_.size() - 1);
}

int EntityRenderer::CreateWorldObject(const MapObject& o, float worldScale,
		const Vec3& origin, TextureCache& textures,
		const std::string& levelHint) {
	const size_t vertexCount = o.vertexCount();
	if (vertexCount == 0 || o.indices.empty()) return -1;

	GpuModel gpu;
	// The same shader families the world uses for these names; winding is
	// the world exporter's, so no clockwise fallback (as for .dat packs).
	std::string shaderName = "defaultNTU";
	const bool isTrans = o.nameHas("trans") || o.nameHas("decal");
	if (isTrans) shaderName += "trans";
	else if (o.nameHas("atest")) shaderName += "atest";
	if (o.nameHas("2sided")) shaderName += "2sided";
	gpu.material = LookupMaterial(shaders_, shaderName, false, o.name);

	// World space (the object's own transform, then the level scale), then
	// re-based on the body's origin so the instance pose is the body pose.
	// Normals take the transform's rotation; the exporter's matrices carry no
	// scale worth normalising away, and the shader normalises anyway.
	std::vector<MeshVertex> verts(vertexCount);
	Vec3 lo(1e30f), hi(-1e30f);
	const Mat4& t = o.transform;
	for (size_t i = 0; i < vertexCount; ++i) {
		Vec3 p, n, w;
		float uv[2];
		o.position(i, p);
		o.normal(i, n);
		o.uv(i, uv);
		w = t.TransformPoint(p);
		MeshVertex& v = verts[i];
		v.x = w[0] * worldScale - origin[0];
		v.y = w[1] * worldScale - origin[1];
		v.z = w[2] * worldScale - origin[2];
		v.nx = n[0] * t[0] + n[1] * t[4] + n[2] * t[8];
		v.ny = n[0] * t[1] + n[1] * t[5] + n[2] * t[9];
		v.nz = n[0] * t[2] + n[1] * t[6] + n[2] * t[10];
		v.u0 = v.u1 = uv[0];
		v.v0 = v.v1 = uv[1];
		for (int a = 0; a < 3; ++a) {
			lo[a] = std::min(lo[a], (&v.x)[a]);
			hi[a] = std::max(hi[a], (&v.x)[a]);
		}
	}
	const bgfx::VertexBufferHandle vbo = MakeVertexBuffer(
			verts.data(), uint32_t(verts.size() * sizeof(MeshVertex)), layout_);
	const bgfx::IndexBufferHandle ibo =
		MakeIndexBuffer(o.indices.data(), uint32_t(o.indices.size()));
	for (const Material& m : o.materials) {
		const uint32_t first = m.firstIndex;
		const uint32_t count = uint32_t(m.triangleCount) * 3;
		if (count == 0 || first + count > o.indices.size()) continue;
		Part part;
		part.vbo = vbo;
		part.ibo = ibo;
		part.ownsVbo = part.ownsIbo = gpu.parts.empty();
		part.vboOwner = 0;
		part.firstIndex = first;
		part.indexCount = count;
		part.diffuse = m.diffuse().empty() ? textures.White()
				: textures.Get(m.diffuse(), levelHint);
		part.material = gpu.material;
		gpu.parts.push_back(part);
	}
	if (gpu.parts.empty()) {
		Part part;
		part.vbo = vbo;
		part.ibo = ibo;
		part.indexCount = uint32_t(o.indices.size());
		part.diffuse = textures.White();
		part.material = gpu.material;
		gpu.parts.push_back(part);
	}
	for (int a = 0; a < 3; ++a) {
		gpu.bboxLo[a] = lo[a];
		gpu.bboxHi[a] = hi[a];
	}
	gpu.name = "world/" + o.name;
	const size_t model = models_.size();
	models_.push_back(std::move(gpu));

	Instance instance;
	instance.model = model;
	instance.scale = 1.f;
	instance.entity = SIZE_MAX;
	for (int c = 0; c < 3; ++c) instance.pos[c] = origin[c];
	instance.transform = MakeTransform(instance.pos, instance.rot9, 1.f);
	UpdateBounds(instance, models_[model]);
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
	instance.transform = MakeTransform(instance.pos, instance.rot9, scale);
	UpdateBounds(instance, models_[slot]);
	instances_.push_back(instance);
	return int(instances_.size() - 1);
}

void EntityRenderer::SetScriptPose(int slot, const Vec3& pos, const Quat& rot) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& instance = instances_[slot];
	for (int c = 0; c < 3; ++c) instance.pos[c] = pos[c];
	EngineQuatToRot9(rot, instance.rot9);
	instance.transform = MakeTransform(instance.pos, instance.rot9, instance.scale);
	UpdateBounds(instance, models_[instance.model]);
}

void EntityRenderer::SetScriptSkinning(int slot, const Mat4* skin, size_t count) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& inst = instances_[slot];
	if (!skin || count == 0) { inst.skin.clear(); return; }
	inst.skin.assign(skin, skin + count);

	// A posed model leaves its bind-pose box - the Catacombs bridge sags five
	// units below it - so grow the culling bounds to every bone's posed
	// centre, padded by the model's half-diagonal.
	const GpuModel& model = models_[inst.model];
	UpdateBounds(inst, model);
	const Vec3 centre = (model.bboxLo + model.bboxHi) * 0.5f;
	// Half the model's diagonal: the most a posed bone can push a vertex out.
	const float pad = ((model.bboxHi - model.bboxLo) * 0.5f).Length() * inst.scale * scaleMultiplier_;
	for (const Mat4& bone : inst.skin) {
		const Vec3 w = inst.transform.TransformPoint(bone.TransformPoint(centre));
		inst.aabbLo = Min(inst.aabbLo, w - Vec3(pad));
		inst.aabbHi = Max(inst.aabbHi, w + Vec3(pad));
	}
}

void EntityRenderer::SetScriptVisible(int slot, bool visible) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].visible = visible;
}

void EntityRenderer::SetScriptCastsShadow(int slot, bool casts) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].castsShadow = casts;
}

void EntityRenderer::SetScriptViewModel(int slot, bool viewModel) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].viewModel = viewModel;
}

bool EntityRenderer::ViewModelBounds(Vec3& centre, float& radius) const {
	Vec3 lo, hi;
	bool any = false;
	for (const Instance& instance : instances_) {
		if (!instance.alive || !instance.visible || !instance.viewModel) continue;
		if (!any) { lo = instance.aabbLo; hi = instance.aabbHi; any = true; }
		else { lo = Min(lo, instance.aabbLo); hi = Max(hi, instance.aabbHi); }
	}
	if (!any) return false;
	centre = (lo + hi) * 0.5f;
	radius = (hi - lo).Length() * 0.5f;
	return radius > 1e-3f;
}

void EntityRenderer::BindViewModel(bool isViewModel) {
	const ViewModelShadows* vm = viewModelShadows_;
	const bool on = isViewModel && vm && vm->ready();
	const float params[4] = {on ? 1.f : 0.f, on && vm->active() ? 1.f : 0.f, 0.f,
			on ? vm->texel() : 0.f};
	bgfx::setUniform(uVmParams_, params);
	if (!on) return;
	bgfx::setUniform(uVmMtx_, vm->matrix());
	bgfx::setUniform(uVmLight_, vm->light());
	bgfx::setTexture(7, sVmShadow_, vm->texture());
}

void EntityRenderer::DrawViewModelShadows(const ViewModelShadows& vm, float timeSeconds) {
	if (!vm.ready() || !vm.active() || !bgfx::isValid(vm.program())) return;
	// The weapon on itself, and nothing else: the world and the other models
	// were tried as casters and read as wrong on a thing held at the eye.
	for (Instance& instance : instances_) {
		if (!instance.alive || !instance.visible || !instance.viewModel) continue;
		DrawCaster(vm.viewId(), vm.program(), instance, models_[instance.model], timeSeconds);
	}
}

void EntityRenderer::DirectionalAt(const Vec3& pos, Vec3& toLight, Vec3& color) const {
	EntityLightFade snap;
	EntityLightState lit;
	lighting_.Evaluate(pos, 0.f, 0.f, snap, lit);
	toLight = lit.dirDir;
	color = lit.dirColor;
}

void EntityRenderer::DrawCaster(bgfx::ViewId view, bgfx::ProgramHandle program,
		const Instance& instance, const GpuModel& model, float timeSeconds) {
	const float identityUv[4] = {1.f, 1.f, 0.f, 0.f};
	static const bool kNoATest = DebugFlag("PAINFUL_NOATEST");
	const bool posing = model.skinned && !instance.skin.empty();

	for (size_t partIndex = 0; partIndex < model.parts.size(); ++partIndex) {
		const Part& part = model.parts[partIndex];
		if (partIndex < instance.hiddenParts.size() && instance.hiddenParts[partIndex])
			continue;
		const MaterialState& mat =
			instance.materialOverride ? instance.material : part.material;
		// Only what writes depth casts, as in the world pass.
		if ((mat.state & BGFX_STATE_BLEND_MASK) || !(mat.state & BGFX_STATE_WRITE_Z))
			continue;
		const float params[4] = {0.f, kNoATest ? -1.f : mat.alphaRef, 0.f, 0.f};
		const float uvAnim[4] = {mat.pan0[0] * timeSeconds, mat.pan0[1] * timeSeconds,
				0.f, 0.f};
		const float tile[4] = {mat.tile0[0], mat.tile0[1], 1.f, 1.f};
		bgfx::setUniform(uParams_, params);
		bgfx::setUniform(uUvAnim_, uvAnim);
		bgfx::setUniform(uUv0_, identityUv);
		bgfx::setUniform(uTile_, tile);
		bgfx::setTransform(instance.transform.m);
		const uint32_t owner = part.vboOwner;
		const bool usePosed = posing && owner < instance.posed.size() &&
				bgfx::isValid(instance.posed[owner]);
		if (usePosed) bgfx::setVertexBuffer(0, instance.posed[owner]);
		else bgfx::setVertexBuffer(0, part.vbo);
		bgfx::setIndexBuffer(part.ibo, part.firstIndex, part.indexCount);
		bgfx::setTexture(0, sDiffuse_, part.diffuse, mat.sampler[0]);
		bgfx::setState(ShadowMap::kState);
		bgfx::submit(view, program);
		++shadowDrawCalls_;
	}
}

void EntityRenderer::DrawShadow(bgfx::ViewId view, const ShadowMap& map, float timeSeconds) {
	// Counted across the maps drawn this frame; Draw resets it.
	if (!map.active() || !bgfx::isValid(map.program())) return;
	const Frustum& frustum = map.frustum();
	for (Instance& instance : instances_) {
		if (!instance.alive || !instance.visible || !instance.castsShadow) continue;
		if (!frustum.VisibleAabb(instance.aabbLo, instance.aabbHi)) continue;
		DrawCaster(view, map.program(), instance, models_[instance.model], timeSeconds);
	}
}

void EntityRenderer::PickShadowLights(const Camera& camera, int count, float radius) {
	shadowPicks_.clear();
	if (!lightShadows_ || !lightShadows_->ready() || count <= 0 || radius <= 0.f) return;

	// Strength is colour x intensity, weighted by a fade over the outer
	// third of the radius; the fade also reaches the shader, so the shadows
	// of a light on its way out thin rather than pop. The flashlight has its
	// own map and is left out; so is anything without a position.
	const std::vector<LightSource>& lights = lighting_.dynamicLights();
	struct Scored { float score; float fade; size_t index; };
	std::vector<Scored> scored;
	for (size_t j = 0; j < lights.size(); ++j) {
		const LightSource& l = lights[j];
		if (l.type == LightSource::kDirectional || !l.projector.empty()) continue;
		if (l.range <= 0.f || l.intensity <= 0.f || l.fakeSpecular) continue;
		const float dist = (l.pos - camera.pos).Length();
		const float fadeFrom = radius * 0.7f;
		float fade = 1.f;
		if (dist >= radius) continue;
		if (dist > fadeFrom) {
			const float k = (dist - fadeFrom) / (radius - fadeFrom);
			fade = 1.f - k * k * (3.f - 2.f * k);
		}
		const float lum = 0.299f * l.color[0] + 0.587f * l.color[1] + 0.114f * l.color[2];
		const float strength = l.intensity * lum * fade;
		if (strength <= 0.f) continue;
		scored.push_back({strength + (l.important ? 1e6f : 0.f), fade, j});
	}
	std::sort(scored.begin(), scored.end(),
			[](const Scored& a, const Scored& b) { return a.score > b.score; });
	for (size_t i = 0; i < scored.size() && int(i) < count; ++i)
		shadowPicks_.push_back({&lights[scored[i].index], int(i), scored[i].fade});
}

void EntityRenderer::DrawLightShadows(float timeSeconds) {
	if (!lightShadows_ || !lightShadows_->ready() || !bgfx::isValid(lightShadows_->program()))
		return;
	const bgfx::ProgramHandle program = lightShadows_->program();
	for (const ShadowedLight& s : shadowPicks_) {
		const int faces = lightShadows_->faceCount(s.slot);
		for (int f = 0; f < faces; ++f) {
			const Frustum& frustum = lightShadows_->faceFrustum(s.slot, f);
			const bgfx::ViewId view = lightShadows_->viewId(s.slot, f);
			for (Instance& instance : instances_) {
				if (!instance.alive || !instance.visible || !instance.castsShadow) continue;
				if (!frustum.VisibleAabb(instance.aabbLo, instance.aabbHi)) continue;
				DrawCaster(view, program, instance, models_[instance.model], timeSeconds);
			}
		}
	}
}

// One named mesh of one instance. A model mesh split across material slots is
// several parts under the SAME name, so every match is set - hiding "blades"
// must take all of it, not just its first material run.
// MDL.SetMaterial(entity, name). CActor gives every gib the template's
// gibShader ("palskinned_bloody" in 64 of them) and the freeze effect swaps
// the whole actor, so this has to be per instance rather than per model.
void EntityRenderer::SetScriptMaterial(int slot, const std::string& name,
		TextureCache& textures) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	if (name.empty()) return;
	Instance& inst = instances_[slot];
	if (!shaders_ || !shaders_->Find(name)) {
		LogWarn("MDL.SetMaterial: no material %s", name.c_str());
		return;
	}
	inst.material = LookupMaterial(shaders_, name, true);
	bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
	if (!inst.material.map1.empty()) tex = textures.Get(inst.material.map1, "");
	inst.stage1 = tex;
	inst.materialOverride = true;
}

void EntityRenderer::SetScriptMeshVisibility(int slot, const std::string& meshName,
		bool visible) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	if (meshName.empty()) return;
	Instance& inst = instances_[slot];
	if (inst.model >= models_.size()) return;
	const GpuModel& model = models_[inst.model];
	if (inst.hiddenParts.size() != model.parts.size())
		inst.hiddenParts.assign(model.parts.size(), 0);
	for (size_t i = 0; i < model.parts.size(); ++i) {
		// Mesh names come from the model file and the script's spelling of
		// them is authored by hand, so match the way every other name lookup
		// in the engine does.
		if (EqualsNoCase(model.parts[i].name, meshName))
			inst.hiddenParts[i] = visible ? 0 : 1;
	}
}

void EntityRenderer::ReleaseScript(int slot) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& inst = instances_[slot];
	// A posed buffer belongs to the instance, so it dies with it. Slots are
	// reused, and leaving these behind would leak one buffer per projectile
	// and per corpse for the life of the level.
	for (bgfx::DynamicVertexBufferHandle h : inst.posed)
		if (bgfx::isValid(h)) bgfx::destroy(h);
	inst.posed.clear();
	inst.alive = false;
}

bool EntityRenderer::GetScriptDimensions(int slot, Vec3& out) const {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return false;
	const Instance& instance = instances_[slot];
	const GpuModel& model = models_[instance.model];
	for (int i = 0; i < 3; ++i)
		out[i] = (model.bboxHi[i] - model.bboxLo[i]) * instance.scale;
	return true;
}

void EntityRenderer::SetEntityPose(size_t entityIndex, const Vec3& pos, const float rot9[9]) {
	for (Instance& instance : instances_) {
		if (instance.entity != entityIndex) continue;
		for (int c = 0; c < 3; ++c) instance.pos[c] = pos[c];
		for (int c = 0; c < 9; ++c) instance.rot9[c] = rot9[c];
		const Vec3 scaledPos{instance.pos[0] * scaleMultiplier_,
									instance.pos[1] * scaleMultiplier_,
									instance.pos[2] * scaleMultiplier_};
		instance.transform = MakeTransform(scaledPos, instance.rot9,
				instance.scale * scaleMultiplier_);
		UpdateBounds(instance, models_[instance.model]);
		return;
	}
}

void EntityRenderer::UpdateBounds(Instance& instance, const GpuModel& model) const {
	instance.aabbLo = Vec3(1e30f);
	instance.aabbHi = Vec3(-1e30f);
	for (int corner = 0; corner < 8; ++corner) {
		const Vec3 local(corner & 1 ? model.bboxHi[0] : model.bboxLo[0],
				corner & 2 ? model.bboxHi[1] : model.bboxLo[1],
				corner & 4 ? model.bboxHi[2] : model.bboxLo[2]);
		const Vec3 w = instance.transform.TransformPoint(local);
		instance.aabbLo = Min(instance.aabbLo, w);
		instance.aabbHi = Max(instance.aabbHi, w);
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
		const Vec3 pos{instance.pos[0] * k, instance.pos[1] * k,
				instance.pos[2] * k};
		instance.transform = MakeTransform(pos, instance.rot9,
				instance.scale * k);
		UpdateBounds(instance, models_[instance.model]);
	}
}

void EntityRenderer::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
		const LevelInfo& info, float timeSeconds) {
	drawCalls_ = 0;
	shadowDrawCalls_ = 0;
	posedInstances_ = 0;
	posedModels_.clear();
	if (!bgfx::isValid(program_) || instances_.empty()) return;

	// Same view setup as the world pass, rebuilt here for the frustum.
	const Vec3 forward = camera.Forward();
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

	// The projector maps, once a light asks for one. Only the flashlight does.
	if (textures_)
		for (const LightSource& l : lighting_.dynamicLights())
			if (projector_.Resolve(l.projector, *textures_, levelHint_)) break;

	// Frame time, for the CEnvironment cross-fade. Draw is the only per-frame
	// hook this renderer has, and a level reload rewinds the clock.
	float dt = timeSeconds - lastTime_;
	if (dt < 0.f || dt > 0.5f) dt = 0.f;
	lastTime_ = timeSeconds;

	const bool beam = shadow_ && shadow_->active();
	const bool box = modelShadow_ && modelShadow_->active();
	bgfx::TextureHandle shadowTex = BGFX_INVALID_HANDLE;
	if (shadow_ && shadow_->ready()) shadowTex = shadow_->texture();
	bgfx::TextureHandle modelShadowTex = BGFX_INVALID_HANDLE;
	if (modelShadow_ && modelShadow_->ready()) modelShadowTex = modelShadow_->texture();

	bgfx::TextureHandle lightShadowTex = BGFX_INVALID_HANDLE;
	if (lightShadows_ && lightShadows_->ready()) lightShadowTex = lightShadows_->texture();

	for (Instance& instance : instances_) {
		if (!instance.alive || !instance.visible) continue;
		// In view, or in a shadow map's frustum, or within a shadowed
		// light's reach: a caster past the screen edge still has to be posed
		// this frame.
		const bool inView = !visCulling_ || frustum.VisibleAabb(instance.aabbLo, instance.aabbHi);
		bool inBeam = instance.castsShadow &&
				((beam && shadow_->frustum().VisibleAabb(instance.aabbLo, instance.aabbHi)) ||
				(box && modelShadow_->frustum().VisibleAabb(instance.aabbLo, instance.aabbHi)));
		if (!inView && !inBeam && instance.castsShadow) {
			const float radius = (instance.aabbHi - instance.aabbLo).Length() * 0.5f;
			for (const ShadowedLight& s : shadowPicks_) {
				const float reach = LightReach(*s.light) + radius;
				if ((instance.pos - s.light->pos).LengthSq() <= reach * reach) { inBeam = true; break; }
			}
		}
		if (!inView && !inBeam) continue;
		const GpuModel& model = models_[instance.model];

		// Pose it, if it is playing something. This is CPU skinning: the
		// matrices are built once per instance and each part is deformed into
		// a buffer of its own. Only instances that survived the frustum test
		// get here, so an actor across the level costs nothing.
		//
		// GPU skinning is the end state (see Docs/Reference/Animation.md); this exists
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
				// Said once per model: a short matrix array is a bug upstream
				// (SetScriptSkinning was handed fewer bones than the rig has),
				// and it shows as limbs left in the bind pose.
				if (size_t(part.maxBone) >= instance.skin.size()) {
					static std::set<std::string> said;
					if (said.insert(model.name + "/" + part.name).second)
						LogInfo("skinning: %s part %s references bone %u but was handed %zu matrices",
								model.name.c_str(), part.name.c_str(), unsigned(part.maxBone),
								instance.skin.size());
				}
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
					out.x = vertScratch_[v * 8 + 0];
					out.y = vertScratch_[v * 8 + 1];
					out.z = vertScratch_[v * 8 + 2];
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
		if (!inView) continue;
		// This model's lighting. The SELECTION is still made at the origin -
		// which of the level's lights are worth a slot is a per-model question,
		// as it is in Entity::AddLight - but the lights themselves are handed
		// over whole and shaded per pixel, the same way the world mesh gets
		// them. Docs/Reference/Lighting.md
		EntityLightState lit;
		// The instance's own bounds, which UpdateBounds already grew to the
		// posed skeleton - so a light reaching an outstretched arm still wins
		// a slot for the model.
		const float lightRadius = (instance.aabbHi - instance.aabbLo).Length() * 0.5f;
		lighting_.Evaluate(instance.pos, lightRadius, dt, instance.lightFade, lit);
		// The lighting mix (SetLightingMix): the original keeps all three at 1.
		lit.ambient *= ambientScale_;
		lit.dirColor *= directionalScale_;
		const float dirColor[4] = {lit.dirColor[0], lit.dirColor[1], lit.dirColor[2], 1.f};
		const float dirDir[4] = {lit.dirDir[0], lit.dirDir[1], lit.dirDir[2], 0.f};
		const float eyePos[4] = {camera.pos[0], camera.pos[1], camera.pos[2], 0.f};
		bgfx::setUniform(uDirColor_, dirColor);
		bgfx::setUniform(uDirDir_, dirDir);
		bgfx::setUniform(uEye_, eyePos);
		bgfx::setUniform(uSpecular_, SpecularParams());

		LightBlock lights;
		for (int s = 0; s < lit.lightCount; ++s) {
			PackLight(lights, s, *lit.lights[s], projector_.name());
			for (int c = 0; c < 3; ++c) lights.color[s][c] *= lightScale_;
			// A slot whose light has a map this frame samples it.
			for (const ShadowedLight& sh : shadowPicks_) {
				if (sh.light != lit.lights[s]) continue;
				float params[4];
				lightShadows_->ReceiverParams(sh.slot, params);
				PackLightShadow(lights, s, params, lightShadows_->info(), sh.fade);
				break;
			}
		}
		PackShadow(lights, shadow_);
		PackDirShadow(lights, modelShadow_);


		const float detail[4] = {1.f, 1.f, 0.f, 0.f};
		// Identity UV transform: entity meshes carry no per-slot xform.
		const float identityUv[4] = {1.f, 1.f, 0.f, 0.f};

		for (size_t partIndex = 0; partIndex < model.parts.size(); ++partIndex) {
			const Part& part = model.parts[partIndex];
			// MDL.SetMeshVisibility hid this one on this instance.
			if (partIndex < instance.hiddenParts.size() &&
					instance.hiddenParts[partIndex])
				continue;
			// Material is per part: one model can mix an ordinary skinned mesh
			// with a scrolling water surface.
			// MDL.SetMaterial overrides the whole model, parts included: the
			// engine swaps the material, not one slot of it.
			const MaterialState& mat =
				instance.materialOverride ? instance.material : part.material;
			const bgfx::TextureHandle stage1Tex =
				instance.materialOverride ? instance.stage1 : part.stage1;
			// No lightmaps on entities, so u_ambient.w (the lightmap scale) is
			// never sampled; alpha test comes from the material scripts.
			//
			// The ambient is THIS MODEL'S, not the level's: the CEnvironment it
			// stands in may have overwritten it, which is the whole reason
			// those boxes exist.
			const float ambientValue[4] = {lit.ambient[0], lit.ambient[1],
					lit.ambient[2], mat.lightScale};
			// PAINFUL_NOATEST disables the alpha test, to tell "the texture alpha
			// is discarding this" apart from "this is not being drawn".
			static const bool kNoATest = DebugFlag("PAINFUL_NOATEST");
			const float params[4] = {0.f, kNoATest ? -1.f : mat.alphaRef, 0.f, 0.f};
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
			const float stage1[4] = {
				bgfx::isValid(stage1Tex) ? float(mat.stage1Op) : 0.f, 0.f, 0.f, 0.f};
			bgfx::setUniform(uStage1_, stage1);
			bgfx::setTransform(instance.transform.m);
			// The posed buffer when there is one, the shared bind-pose buffer
			// otherwise. Indices never change: skinning moves vertices, it
			// does not retopologise.
			// The posed buffer belongs to the slot that owns the vertices.
			const uint32_t owner = part.vboOwner;
			const bool usePosed = posing && owner < instance.posed.size() &&
					bgfx::isValid(instance.posed[owner]);
			if (usePosed) bgfx::setVertexBuffer(0, instance.posed[owner]);
			else bgfx::setVertexBuffer(0, part.vbo);
			bgfx::setIndexBuffer(part.ibo, part.firstIndex, part.indexCount);
			bgfx::setTexture(0, sDiffuse_, part.diffuse, FilteredSampler(mat.sampler[0]));
			// Stage 1 when the material has one; white through the off path so
			// the sampler is always bound.
			bgfx::setTexture(1, sStage1_,
					bgfx::isValid(stage1Tex) ? stage1Tex : white_,
					FilteredSampler(mat.sampler[1]));
			// Stages 2 and 3 are the projector pair, 4 the flashlight's
			// shadow map, 5 the model shadow map, 6 the placed lights'
			// atlas; models sample no detail map, so nothing else wants them.
			lightUniforms_.Submit(lights, 2, 3,
					bgfx::isValid(projector_.cookie()) ? projector_.cookie() : white_,
					bgfx::isValid(projector_.falloff()) ? projector_.falloff() : white_,
					4, shadowTex, 5, modelShadowTex, 6, lightShadowTex);
			BindViewModel(instance.viewModel);
			bgfx::setState(state);
			bgfx::submit(view, program_);
			++drawCalls_;
		}
	}
}

} // namespace painful
