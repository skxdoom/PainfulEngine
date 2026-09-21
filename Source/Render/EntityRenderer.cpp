#include "EntityRenderer.h"
#include "ShaderLoad.h"
#include "ShadowMap.h"
#include "CharacterShadows.h"
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

// A mesh's specular until MDL.SetMaterialSpecular: 0.5 grey at power 20, what
// SimpleMesh's constructor (0x1005822c) and ResetMaterialSpecular write.
constexpr std::array<float, 4> kDefaultSpecular = {0.5f, 0.5f, 0.5f, 20.f};
// How far into N.L the specular's gate ramps. palskin's `lit` switches on
// N.L > 0 per vertex and the interpolation smears it; per pixel the step
// would draw a line, so it ramps over about one triangle's worth.
constexpr float kSpecularGate = 0.25f;

} // namespace

bool EntityRenderer::Init(const std::string& shaderDir) {
	layout_ = MakeMeshLayout();

	namespace fs = std::filesystem;
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_entity");
	bgfx::ShaderHandle fsh = LoadShader(shaderDir, "fs_entity");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fsh)) return false;

	// The demon pass shares the vertex shader, so it is linked before the
	// main program takes ownership of it.
	{
		bgfx::ShaderHandle fsd = LoadShader(shaderDir, "fs_demon_entity");
		if (bgfx::isValid(fsd)) {
			demonProgram_ = bgfx::createProgram(vs, fsd, false);
			bgfx::destroy(fsd);
		}
	}
	uDemonFresnel_ = bgfx::createUniform("u_demonFresnel", bgfx::UniformType::Vec4);
	// The model water program shares the vertex shader; the fragment side
	// is fs_entity_water. Missing is not fatal: water draws as skin.
	bgfx::ShaderHandle fsw = LoadShader(shaderDir, "fs_entity_water");
	if (bgfx::isValid(fsw)) {
		waterProgram_ = bgfx::createProgram(vs, fsw, false);
		bgfx::destroy(fsw);
	}
	program_ = bgfx::createProgram(vs, fsh, true);
	if (!bgfx::isValid(program_)) return false;
	// The normal-mapped models. Missing is not fatal: they draw vertex-normal lit.
	{
		bgfx::ShaderHandle vsn = LoadShader(shaderDir, "vs_entity_nm");
		bgfx::ShaderHandle fsn = LoadShader(shaderDir, "fs_entity_nm");
		if (bgfx::isValid(vsn) && bgfx::isValid(fsn)) programNm_ = bgfx::createProgram(vsn, fsn, true);
		else LogWarn("entity: vs_entity_nm/fs_entity_nm missing, no normal maps");
	}
	rotLayout_.begin()
		.add(bgfx::Attrib::TexCoord2, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord3, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord4, 3, bgfx::AttribType::Float)
		.end();
	sNormalMap_ = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler);

	sDiffuse_ = bgfx::createUniform("s_diffuse", bgfx::UniformType::Sampler);
	uParams_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
	uAmbient_ = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
	uFogColor_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
	uFog_ = bgfx::createUniform("u_fog", bgfx::UniformType::Vec4);
	uUvAnim_ = bgfx::createUniform("u_uvanim", bgfx::UniformType::Vec4);
	uUv0_ = bgfx::createUniform("u_uv0", bgfx::UniformType::Vec4);
	uTile_ = bgfx::createUniform("u_tile", bgfx::UniformType::Vec4);
	uSpecular_ = bgfx::createUniform("u_specular", bgfx::UniformType::Vec4);
	uMode96_ = bgfx::createUniform("u_mode96", bgfx::UniformType::Vec4);
	uSpecColor_ = bgfx::createUniform("u_specColor", bgfx::UniformType::Vec4);
	uSpecOrigin_ = bgfx::createUniform("u_specOrigin", bgfx::UniformType::Vec4);
	sStage1_ = bgfx::createUniform("s_stage1", bgfx::UniformType::Sampler);
	uStage1_ = bgfx::createUniform("u_stage1", bgfx::UniformType::Vec4);
	uEntWater_ = bgfx::createUniform("u_entWater", bgfx::UniformType::Vec4);
	uEntWaterRefl_ = bgfx::createUniform("u_entWaterRefl", bgfx::UniformType::Vec4);
	uEntWaterRefr_ = bgfx::createUniform("u_entWaterRefr", bgfx::UniformType::Vec4);
	sEnvCube_ = bgfx::createUniform("s_envcube", bgfx::UniformType::Sampler);
	uVmParams_ = bgfx::createUniform("u_vmParams", bgfx::UniformType::Vec4);
	uViewDepth_ = bgfx::createUniform("u_viewDepth", bgfx::UniformType::Vec4);
	uVmMtx_ = bgfx::createUniform("u_vmMtx", bgfx::UniformType::Mat4);
	uVmLight_ = bgfx::createUniform("u_vmLight", bgfx::UniformType::Vec4);
	sVmShadow_ = bgfx::createUniform("s_vmShadow", bgfx::UniformType::Sampler);
	uVmRect_ = bgfx::createUniform("u_vmRect", bgfx::UniformType::Vec4, 1 + ViewModelShadows::kLights);
	uVmLightMtx_ = bgfx::createUniform("u_vmLightMtx", bgfx::UniformType::Mat4, ViewModelShadows::kLights);
	uVmLightPos_ = bgfx::createUniform("u_vmLightPos", bgfx::UniformType::Vec4, ViewModelShadows::kLights);
	uVmSlots_ = bgfx::createUniform("u_vmSlots", bgfx::UniformType::Vec4, 2);
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
			if (bgfx::isValid(p.bindRot)) bgfx::destroy(p.bindRot);
		}
	}
	models_.clear();
	instances_.clear();
	if (bgfx::isValid(program_)) { bgfx::destroy(program_); program_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(programNm_)) { bgfx::destroy(programNm_); programNm_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sNormalMap_)) { bgfx::destroy(sNormalMap_); sNormalMap_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(waterProgram_)) { bgfx::destroy(waterProgram_); waterProgram_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uEntWater_)) { bgfx::destroy(uEntWater_); uEntWater_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uEntWaterRefl_)) { bgfx::destroy(uEntWaterRefl_); uEntWaterRefl_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uEntWaterRefr_)) { bgfx::destroy(uEntWaterRefr_); uEntWaterRefr_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sEnvCube_)) { bgfx::destroy(sEnvCube_); sEnvCube_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(demonProgram_)) { bgfx::destroy(demonProgram_); demonProgram_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uDemonFresnel_)) { bgfx::destroy(uDemonFresnel_); uDemonFresnel_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sDiffuse_)) { bgfx::destroy(sDiffuse_); sDiffuse_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uParams_)) { bgfx::destroy(uParams_); uParams_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uAmbient_)) { bgfx::destroy(uAmbient_); uAmbient_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uFogColor_)) { bgfx::destroy(uFogColor_); uFogColor_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uFog_)) { bgfx::destroy(uFog_); uFog_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uUvAnim_)) { bgfx::destroy(uUvAnim_); uUvAnim_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sStage1_)) { bgfx::destroy(sStage1_); sStage1_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uStage1_)) { bgfx::destroy(uStage1_); uStage1_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uVmParams_)) { bgfx::destroy(uVmParams_); uVmParams_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uViewDepth_)) { bgfx::destroy(uViewDepth_); uViewDepth_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uVmMtx_)) { bgfx::destroy(uVmMtx_); uVmMtx_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uVmLight_)) { bgfx::destroy(uVmLight_); uVmLight_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(sVmShadow_)) { bgfx::destroy(sVmShadow_); sVmShadow_ = BGFX_INVALID_HANDLE; }
	for (bgfx::UniformHandle* u : {&uVmRect_, &uVmLightMtx_, &uVmLightPos_, &uVmSlots_})
		if (bgfx::isValid(*u)) { bgfx::destroy(*u); *u = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uUv0_)) { bgfx::destroy(uUv0_); uUv0_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uTile_)) { bgfx::destroy(uTile_); uTile_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uSpecular_)) { bgfx::destroy(uSpecular_); uSpecular_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uMode96_)) { bgfx::destroy(uMode96_); uMode96_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uSpecColor_)) { bgfx::destroy(uSpecColor_); uSpecColor_ = BGFX_INVALID_HANDLE; }
	if (bgfx::isValid(uSpecOrigin_)) { bgfx::destroy(uSpecOrigin_); uSpecOrigin_ = BGFX_INVALID_HANDLE; }
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

		// One part per material slot. The slots are triangle runs over a shared
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
			// The override key is the mesh name, matching how the world path keys
			// off each object's name. Swamp_dirtywater.pkmdl holds a mesh called
			// "dirtywater", which is the skin.shader entry that makes the swamp
			// water scroll; keying off the file name found nothing.
			// The mesh name picks the shader family, for models exactly as it does
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
			part.name = mesh.name;
			part.water = part.material.vshader == "palskin_water";
			if (part.water) LogInfo("model water: %s (%s)", mesh.name.c_str(), part.material.fshader.c_str());
			if (!part.material.map1.empty())
				part.stage1 = textures.Get(part.material.map1, "");
			// The object-space normal map; one that does not load leaves the part
			// on the plain path, as the original's "not found" texture does.
			if (!mesh.normalMap.empty()) {
				const bgfx::TextureHandle nm = textures.Get(mesh.normalMap, "");
				if (nm.idx != textures.White().idx) {
					part.normalMap = nm;
					if (part.ownsVbo) {
						std::vector<float> rows(vertexCount * 9, 0.f);
						for (size_t v = 0; v < vertexCount; ++v)
							rows[v * 9 + 0] = rows[v * 9 + 4] = rows[v * 9 + 8] = 1.f;
						part.bindRot = bgfx::createVertexBuffer(
								bgfx::copy(rows.data(), uint32_t(rows.size() * sizeof(float))), rotLayout_);
					}
				}
			}
			gpu.parts.push_back(std::move(part));
		}
		// Every slot was empty or out of range: the vertices have no owner.
		if (gpu.parts.size() == ownerIndex) {
			if (bgfx::isValid(vbo)) bgfx::destroy(vbo);
			if (bgfx::isValid(ibo)) bgfx::destroy(ibo);
		}
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
		size_t& outIndex, bool centred) {
	const std::string key = packName + "/" + meshName + (centred ? "|c" : "");
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

	// ENTITY.Create's translateToZero: WorldMesh::CenterGeometry (0x101D6F80)
	// moves the vertices so the bounding-box centre is the origin.
	Vec3 centre;
	if (centred) {
		Vec3 clo(1e30f), chi(-1e30f);
		for (const MapObject& o : pack.objects) {
			if (!meshName.empty() && o.name != meshName && pack.objects.size() > 1) continue;
			for (size_t i = 0; i < o.vertexCount(); ++i) {
				Vec3 p;
				o.position(i, p);
				clo = Min(clo, p);
				chi = Max(chi, p);
			}
		}
		if (clo[0] <= chi[0]) centre = (clo + chi) * 0.5f;
	}
	GpuModel gpu;
	gpu.worldMesh = true;
	bool materialSet = false;
	Vec3 lo(1e30f), hi(-1e30f);
	for (const MapObject& o : pack.objects) {
		// o.Mesh selects one object; when it matches nothing (or is empty),
		// every object is drawn - dead packs hold loose fragments.
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
			v.x = p[0] - centre[0]; v.y = p[1] - centre[1]; v.z = p[2] - centre[2];
			v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
			v.u0 = v.u1 = uv[0];
			v.v0 = v.v1 = uv[1];
		}
		for (int a = 0; a < 3; ++a) {
			lo[a] = std::min(lo[a], o.bboxMin[a] - centre[a]);
			hi[a] = std::max(hi[a], o.bboxMax[a] - centre[a]);
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
	cameraFade_ = EntityLightFade();
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

		// Scrolling barriers (the Slab class) start hidden: Slab:OnPlay calls
		// Open(true) for any instance not marked Closed, which disables
		// drawing and sinks the plate below its start position until an
		// ambush raises it. Drawing them anyway paints floating plates the
		// player is never meant to see.
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
	gpu.worldMesh = true;
	gpu.mapObject = true;
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
	instance.meshOrigin = origin;
	instance.transform = MakeTransform(instance.pos, instance.rot9, 1.f);
	UpdateBounds(instance, models_[model]);
	instances_.push_back(instance);
	return int(instances_.size() - 1);
}

int EntityRenderer::CreateScriptPack(const std::string& packName,
		const std::string& meshName, float scale,
		TextureCache& textures,
		const std::string& itemsRoot, bool centred) {
	size_t slot = 0;
	if (packName.empty() || !GetPack(packName, meshName, textures, itemsRoot, slot, centred))
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

void EntityRenderer::SetScriptScale(int slot, float scale) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& instance = instances_[slot];
	instance.scale = scale;
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

	// A posed model leaves its bind-pose box - a jointed prop can sag well
	// below it - so grow the culling bounds to every bone's posed centre,
	// padded by the model's half-diagonal.
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

void EntityRenderer::SetScriptCharacterShadow(int slot, bool on) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].characterShadow = on;
}

size_t EntityRenderer::characterCount() const {
	size_t n = 0;
	for (const Instance& instance : instances_) n += instance.alive && instance.characterShadow;
	return n;
}

void EntityRenderer::SetScriptViewModel(int slot, bool viewModel) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].viewModel = viewModel;
}

void EntityRenderer::SetScriptMeshWater(int slot, const std::string& mesh, float refract,
		float fresnel, const Vec3& reflTint, const Vec3& refrTint) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& inst = instances_[slot];
	Instance::MeshWater* w = nullptr;
	for (Instance::MeshWater& m : inst.water)
		if (m.mesh == mesh) { w = &m; break; }
	if (!w) {
		inst.water.emplace_back();
		w = &inst.water.back();
		w->mesh = mesh;
	}
	w->refract = refract;
	w->fresnel = fresnel;
	w->reflTint = reflTint;
	w->refrTint = refrTint;
}

void EntityRenderer::SetLevelCubeMap(const std::string& name, TextureCache& textures) {
	levelCube_ = BGFX_INVALID_HANDLE;
	if (!name.empty()) levelCube_ = textures.GetCube(name, "");
}

void EntityRenderer::SetScriptDemonic(int slot, bool demonic) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].demonic = demonic;
}

void EntityRenderer::SetScriptNormalMaps(int slot, bool on) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].normalMaps = on;
}

void EntityRenderer::SetDemonPass(bool on, bgfx::ViewId view, bgfx::TextureHandle detail,
		bgfx::TextureHandle ramp, float fresnelScale) {
	demonOn_ = on && bgfx::isValid(demonProgram_) && bgfx::isValid(detail) && bgfx::isValid(ramp);
	demonView_ = view;
	demonDetail_ = detail;
	demonRamp_ = ramp;
	demonScale_ = fresnelScale;
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

void EntityRenderer::BindViewModel(bool isViewModel, const float cellOfSlot[8]) {
	static_assert(kMaxDynamicLights <= 8, "u_vmSlots holds eight light slots");
	const ViewModelShadows* vm = viewModelShadows_;
	const bool on = isViewModel && vm && vm->ready();
	const float params[4] = {on ? 1.f : 0.f, on && vm->active() ? 1.f : 0.f,
			on ? vm->texelUv()[0] : 0.f, on ? vm->texelUv()[1] : 0.f};
	bgfx::setUniform(uVmParams_, params);
	if (!on) return;
	const ViewModelShadows::Cell& dir = vm->cell(0);
	const float dirLight[4] = {0.f, 0.f, 0.f, dir.texel};
	bgfx::setUniform(uVmMtx_, dir.receiverMatrix);
	bgfx::setUniform(uVmLight_, dirLight);
	float rects[1 + ViewModelShadows::kLights][4];
	float mtx[ViewModelShadows::kLights][16];
	float pos[ViewModelShadows::kLights][4];
	for (int i = 0; i <= ViewModelShadows::kLights; ++i) {
		const ViewModelShadows::Cell& c = vm->cell(i);
		for (int k = 0; k < 4; ++k) rects[i][k] = c.rect[k];
		if (i == 0) continue;
		for (int k = 0; k < 16; ++k) mtx[i - 1][k] = c.receiverMatrix[k];
		pos[i - 1][0] = c.lightPos[0];
		pos[i - 1][1] = c.lightPos[1];
		pos[i - 1][2] = c.lightPos[2];
		pos[i - 1][3] = c.texel;
	}
	bgfx::setUniform(uVmRect_, rects, 1 + ViewModelShadows::kLights);
	bgfx::setUniform(uVmLightMtx_, mtx, ViewModelShadows::kLights);
	bgfx::setUniform(uVmLightPos_, pos, ViewModelShadows::kLights);
	bgfx::setUniform(uVmSlots_, cellOfSlot, 2);
	bgfx::setTexture(7, sVmShadow_, vm->texture());
}

void EntityRenderer::PickViewModelLights(ViewModelShadows& vm, const Vec3& centre, float radius,
		bool placed, bool dynamic) const {
	std::vector<std::pair<float, const LightSource*>> picks;
	for (const LightSource& l : lighting_.dynamicLights()) {
		if (l.type != LightSource::kPoint && l.type != LightSource::kSpot) continue;
		if (l.fakeSpecular || !l.projector.empty() || l.id == 0) continue;
		if (!(l.dynamic ? dynamic : placed)) continue;
		const float score = LightAttenuation(l, centre, radius) * l.intensity *
				(l.color[0] + l.color[1] + l.color[2]);
		if (score > 0.f) picks.emplace_back(score, &l);
	}
	std::sort(picks.begin(), picks.end(), [](const auto& a, const auto& b) {
		return a.first > b.first;
	});
	for (const auto& p : picks) {
		if (vm.lightCount() >= ViewModelShadows::kLights) break;
		vm.AddLight(p.second->id, p.second->pos, centre, radius);
	}
}

void EntityRenderer::DrawViewModelShadows(const ViewModelShadows& vm, float timeSeconds) {
	if (!vm.ready() || !bgfx::isValid(vm.program())) return;
	// The weapon casts on itself and on nothing else; the world and the other
	// models are not casters for a thing held at the eye.
	for (int i = 0; i <= ViewModelShadows::kLights; ++i) {
		const ViewModelShadows::Cell& cell = vm.cell(i);
		if (!cell.active) continue;
		for (Instance& instance : instances_) {
			if (!instance.alive || !instance.visible || !instance.viewModel) continue;
			float transform[16];
			bx::mtxMul(transform, instance.transform.m, cell.drawMatrix);
			DrawCaster(vm.viewId(), vm.program(), instance, models_[instance.model], timeSeconds,
					transform, cell.scissor);
		}
	}
}

void EntityRenderer::CameraDirectional(const Vec3& pos, float timeSeconds, Vec3& toLight,
		Vec3& color) {
	lighting_.UpdateFade(pos, timeSeconds, cameraFade_);
	toLight = cameraFade_.dirDir;
	color = cameraFade_.dirColor * cameraFade_.intensity;
}

void EntityRenderer::DrawCaster(bgfx::ViewId view, bgfx::ProgramHandle program,
		const Instance& instance, const GpuModel& model, float timeSeconds,
		const float* transform, const uint16_t* scissor) {
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
		{ const float rv[4] = {mode96Mip_, mode96_ ? mode96Colors_ : 0.f, mode96_ ? 1.f : 0.f, 0.f};
			bgfx::setUniform(uMode96_, rv); }
		bgfx::setUniform(uUv0_, identityUv);
		bgfx::setUniform(uTile_, tile);
		bgfx::setTransform(transform ? transform : instance.transform.m);
		if (scissor) bgfx::setScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
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

void EntityRenderer::PickCharacterShadows(const Camera& camera, int width, int height,
		float timeSeconds, float strength, CharacterShadows& shadows) {
	for (Instance& instance : instances_) instance.hasCharacterSlot = false;
	if (!shadows.ready() || strength <= 0.f) return;

	float viewMtx[16], projMtx[16];
	camera.ViewProj(width, height, camera.farPlane, viewMtx, projMtx);
	const Frustum frustum = Frustum::FromViewProj(viewMtx, projMtx);
	// View::RenderShadowmaps takes up to 24 of the scene's casters; here the
	// nearest first, as many as there are slots.
	struct Pick { float dist2; size_t index; };
	std::vector<Pick> picks;
	for (size_t i = 0; i < instances_.size(); ++i) {
		Instance& instance = instances_[i];
		if (!instance.alive || !instance.visible || !instance.castsShadow ||
				!instance.characterShadow || instance.viewModel)
			continue;
		// The character's own light direction, faded as its lighting is (UpdateFade
		// steps once per frame, so Draw's Evaluate will not step it again).
		lighting_.UpdateFade(instance.pos, timeSeconds, instance.lightFade);
		Vec3 reachLo, reachHi;
		CharacterShadows::Reach(instance.aabbLo, instance.aabbHi, instance.lightFade.dirDir,
				reachLo, reachHi);
		if (!frustum.VisibleAabb(reachLo, reachHi)) continue;
		picks.push_back({(instance.pos - camera.pos).LengthSq(), i});
	}
	std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.dist2 < b.dist2; });
	for (const Pick& p : picks) {
		Instance& instance = instances_[p.index];
		if (!shadows.Add(int(p.index), instance.aabbLo, instance.aabbHi, instance.lightFade.dirDir,
				strength))
			break;
		instance.hasCharacterSlot = true;
	}
}

void EntityRenderer::DrawCharacterShadows(const CharacterShadows& shadows, float timeSeconds) {
	if (!shadows.ready() || !bgfx::isValid(shadows.program())) return;
	for (const CharacterShadows::Caster& c : shadows.casters()) {
		if (c.instance < 0 || size_t(c.instance) >= instances_.size()) continue;
		const Instance& instance = instances_[size_t(c.instance)];
		if (!instance.alive) continue;
		float transform[16];
		bx::mtxMul(transform, instance.transform.m, c.drawMatrix);
		DrawCaster(shadows.view(), shadows.program(), instance, models_[instance.model], timeSeconds,
				transform, c.scissor);
	}
}

void EntityRenderer::PickShadowLights(const Camera& camera, int count, float radius, bool placed,
		bool dynamic) {
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
		if (!(l.dynamic ? dynamic : placed)) continue;
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
// several parts under the same name, so every match is set - hiding "blades"
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

void EntityRenderer::SetScriptMaterialSpecular(int slot, const std::string& mesh,
		const float rgbPower[4]) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& inst = instances_[slot];
	if (inst.model >= models_.size()) return;
	const GpuModel& model = models_[inst.model];
	if (inst.partSpecular.size() != model.parts.size())
		inst.partSpecular.assign(model.parts.size(), kDefaultSpecular);
	// Model::SetMaterialSpecular matches the mesh name with String::operator==.
	for (size_t i = 0; i < model.parts.size(); ++i)
		if (model.parts[i].name == mesh)
			inst.partSpecular[i] = {rgbPower[0], rgbPower[1], rgbPower[2], rgbPower[3]};
}

void EntityRenderer::ResetScriptMaterialSpecular(int slot) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].partSpecular.clear();
}

void EntityRenderer::SetScriptLighting(int slot, bool on) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	instances_[slot].unlit = !on;
}

void EntityRenderer::SetScriptMeshLighting(int slot, const std::string& mesh, bool on,
		const Vec3& color) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& inst = instances_[slot];
	if (inst.model >= models_.size()) return;
	const GpuModel& model = models_[inst.model];
	inst.unlitColor = color;
	if (inst.partUnlit.size() != model.parts.size()) inst.partUnlit.assign(model.parts.size(), 0);
	for (size_t i = 0; i < model.parts.size(); ++i)
		if (mesh == "*" || model.parts[i].name == mesh) inst.partUnlit[i] = on ? 0 : 1;
}

void EntityRenderer::ReleaseScript(int slot) {
	if (!PAINFUL_CHECK(slot >= 0 && size_t(slot) < instances_.size(),
			"EntityRenderer: instance slot %d of %zu", slot, instances_.size()))
		return;
	Instance& inst = instances_[slot];
	// A posed buffer belongs to the instance, so it dies with it; leaving these
	// behind would leak one buffer per projectile and per corpse.
	for (bgfx::DynamicVertexBufferHandle h : inst.posed)
		if (bgfx::isValid(h)) bgfx::destroy(h);
	inst.posed.clear();
	for (bgfx::DynamicVertexBufferHandle h : inst.posedRot)
		if (bgfx::isValid(h)) bgfx::destroy(h);
	inst.posedRot.clear();
	// A map object's model is its own (CreateWorldObject makes one per object and
	// nothing shares it), so its buffers go too: a level of active meshes would
	// otherwise leak a vertex and an index buffer each on every load.
	if (inst.model < models_.size() && models_[inst.model].mapObject) {
		for (Part& p : models_[inst.model].parts) {
			if (p.ownsVbo && bgfx::isValid(p.vbo)) bgfx::destroy(p.vbo);
			if (p.ownsIbo && bgfx::isValid(p.ibo)) bgfx::destroy(p.ibo);
		}
		models_[inst.model].parts.clear();
	}
	inst.normalMaps = false;
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
	// The whole layout scales about the shared origin - world (0,0,0), the
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
	// The view model pass is the frame's second Draw: it adds to the scene's counts.
	if (drawSet_ != kViewModelOnly) {
		drawCalls_ = 0;
		shadowDrawCalls_ = 0;
		posedInstances_ = 0;
		posedModels_.clear();
	}
	if (!bgfx::isValid(program_) || instances_.empty()) return;

	// Same view setup as the world pass, rebuilt here for the frustum.
	float viewMtx[16], projMtx[16];
	camera.ViewProj(width, height, camera.farPlane, viewMtx, projMtx);
	const Frustum frustum = Frustum::FromViewProj(viewMtx, projMtx);
	// The demon pass draws into its own view, from the same camera.
	if (demonOn_) bgfx::setViewTransform(demonView_, viewMtx, projMtx);

	const float fogValue[4] = {info.fogColor[0] / 255.f, info.fogColor[1] / 255.f,
			info.fogColor[2] / 255.f, 1.f};
	const float fogParams[4] = {float(info.fogMode), info.fogStart, info.fogEnd,
								info.fogDensity};
	bgfx::setUniform(uFog_, fogParams);

	// The projector maps, once a light asks for one. Only the flashlight does.
	if (textures_)
		for (const LightSource& l : lighting_.dynamicLights())
			if (projector_.Resolve(l.projector, *textures_, levelHint_)) break;

	const bool beam = shadow_ && shadow_->active();
	bgfx::TextureHandle shadowTex = BGFX_INVALID_HANDLE;
	if (shadow_ && shadow_->ready()) shadowTex = shadow_->texture();

	bgfx::TextureHandle lightShadowTex = BGFX_INVALID_HANDLE;
	if (lightShadows_ && lightShadows_->ready()) lightShadowTex = lightShadows_->texture();

	for (Instance& instance : instances_) {
		if (!instance.alive || !instance.visible) continue;
		if (drawSet_ == kSceneOnly ? instance.viewModel : (drawSet_ == kViewModelOnly && !instance.viewModel))
			continue;
		// In view, or in a shadow map's frustum, or within a shadowed
		// light's reach: a caster past the screen edge still has to be posed
		// this frame.
		const bool inView = !visCulling_ || frustum.VisibleAabb(instance.aabbLo, instance.aabbHi);
		bool inBeam = instance.castsShadow &&
				((beam && shadow_->frustum().VisibleAabb(instance.aabbLo, instance.aabbHi)) ||
				instance.hasCharacterSlot);
		if (!inView && !inBeam && instance.castsShadow) {
			const float radius = (instance.aabbHi - instance.aabbLo).Length() * 0.5f;
			for (const ShadowedLight& s : shadowPicks_) {
				const float reach = LightReach(*s.light) + radius;
				if ((instance.pos - s.light->pos).LengthSq() <= reach * reach) { inBeam = true; break; }
			}
		}
		if (!inView && !inBeam) {
			// Entity::Tick runs out of view too, so the environment fade does.
			lighting_.UpdateFade(instance.pos, timeSeconds, instance.lightFade);
			continue;
		}
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

				// The normal map's rotation: the vertex's first bone, as the
				// original's vertex shader takes it (Skin.fxo, FXSkinBump). Its rows
				// carry the bind axes to the posed model.
				if (!instance.normalMaps || !bgfx::isValid(part.normalMap)) continue;
				instance.posedRot.resize(model.parts.size(), BGFX_INVALID_HANDLE);
				const size_t vc = part.cpu.vertexCount();
				rotScratch_.assign(vc * 9, 0.f);
				for (size_t v = 0; v < vc; ++v) {
					float* row = &rotScratch_[v * 9];
					const std::vector<SkinInfluence>& infl = part.cpu.skin[v];
					if (infl.empty() || infl[0].bone >= instance.skin.size()) {
						row[0] = row[4] = row[8] = 1.f;
						continue;
					}
					const Mat4& m = instance.skin[infl[0].bone];
					for (int r = 0; r < 3; ++r)
						for (int c = 0; c < 3; ++c) row[r * 3 + c] = m[r * 4 + c];
				}
				if (!bgfx::isValid(instance.posedRot[i]))
					instance.posedRot[i] = bgfx::createDynamicVertexBuffer(uint32_t(vc), rotLayout_);
				bgfx::update(instance.posedRot[i], 0,
						bgfx::copy(rotScratch_.data(), uint32_t(rotScratch_.size() * sizeof(float))));
			}
		}
		if (!inView) {
			lighting_.UpdateFade(instance.pos, timeSeconds, instance.lightFade);
			continue;
		}
		// This model's lighting. The selection is still made at the origin -
		// which of the level's lights are worth a slot is a per-model question,
		// as it is in Entity::AddLight - but the lights themselves are handed
		// over whole and shaded per pixel, the same way the world mesh gets
		// them. Docs/Reference/Lighting.md
		EntityLightState lit;
		// The instance's own bounds, which UpdateBounds already grew to the
		// posed skeleton - so a light reaching an outstretched arm still wins
		// a slot for the model.
		const float lightRadius = (instance.aabbHi - instance.aabbLo).Length() * 0.5f;
		lighting_.Evaluate(instance.pos, lightRadius, timeSeconds, instance.lightFade, lit);
		const float dirColor[4] = {lit.dirColor[0], lit.dirColor[1], lit.dirColor[2], 1.f};
		const float dirDir[4] = {lit.dirDir[0], lit.dirDir[1], lit.dirDir[2], 0.f};
		const float eyePos[4] = {camera.pos[0], camera.pos[1], camera.pos[2], 0.f};
		bgfx::setUniform(uDirColor_, dirColor);
		bgfx::setUniform(uDirDir_, dirDir);
		bgfx::setUniform(uEye_, eyePos);
		// The original's entity position: for a map object LoadMeshPakFile leaves
		// it at the map's origin, moved only as its body moves - pose less the
		// rotated rest offset. Lighting.md, "Which entities glint"
		float specOrigin[4] = {instance.pos[0], instance.pos[1], instance.pos[2], 0.f};
		for (int c = 0; c < 3; ++c)
			for (int r = 0; r < 3; ++r) specOrigin[c] -= instance.meshOrigin[r] * instance.rot9[r * 3 + c];
		bgfx::setUniform(uSpecOrigin_, specOrigin);

		LightBlock lights;
		for (int s = 0; s < lit.lightCount; ++s) {
			PackLight(lights, s, *lit.lights[s], projector_.name());
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
		// The slots that may glint (a bitmask): all of them on a model. RenderNTU
		// zeroes, on a pack or map mesh, the lights its additive passes draw - the
		// dynamic ones, and at Dynamic Lights 2 every one but the directional. A
		// map object's lights are attenuated at the map's origin: none reach.
		float specSlots = 0.f;
		for (int s = 0; s < lit.lightCount; ++s)
			if (!model.worldMesh ||
					(!model.mapObject && drawDynLights_ < 2 && !lit.lights[s]->dynamic))
				specSlots += float(1 << s);
		static const LightBlock unlitLights;
		// The weapon's light slots that have a cell in its own map, by light id.
		float vmCells[8] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f};
		if (instance.viewModel && viewModelShadows_)
			for (int s = 0; s < lit.lightCount && s < 8; ++s)
				for (int k = 1; k <= ViewModelShadows::kLights; ++k) {
					const ViewModelShadows::Cell& c = viewModelShadows_->cell(k);
					if (c.active && c.lightId == lit.lights[s]->id) vmCells[s] = float(k - 1);
				}

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
			// The ambient is this model's, not the level's: the CEnvironment it
			// stands in may have overwritten it, which is the whole reason
			// those boxes exist.
			// Unlit (MESH.SetLighting, MDL.SetMeshLighting): c11 = the flat colour,
			// no lights, c10 = 0 (RenderNTU; RenderDefault at 0x10004603).
			const bool unlit = instance.unlit ||
					(partIndex < instance.partUnlit.size() && instance.partUnlit[partIndex]);			const Vec3 flat = instance.unlit ? Vec3{1.f, 1.f, 1.f} : instance.unlitColor;
			const float ambientValue[4] = {unlit ? flat[0] : lit.ambient[0],
					unlit ? flat[1] : lit.ambient[1], unlit ? flat[2] : lit.ambient[2], mat.lightScale};
			const float noDir[4] = {0.f, 0.f, 0.f, 1.f};
			bgfx::setUniform(uDirColor_, unlit ? noDir : dirColor);
			// PAINFUL_NOATEST disables the alpha test, to tell "the texture alpha
			// is discarding this" apart from "this is not being drawn".
			static const bool kNoATest = DebugFlag("PAINFUL_NOATEST");
			// w: the lighting-only view's grey albedo, blended materials excepted.
			const bool greyAlbedo = lightingOnly_ && !(mat.state & BGFX_STATE_BLEND_MASK);
			const float params[4] = {0.f, kNoATest ? -1.f : mat.alphaRef, 0.f, greyAlbedo ? 1.f : 0.f};
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
			{ const float rv[4] = {mode96Mip_, mode96_ ? mode96Colors_ : 0.f, mode96_ ? 1.f : 0.f, 0.f};
			bgfx::setUniform(uMode96_, rv); }
			bgfx::setUniform(uUv0_, identityUv);
			bgfx::setUniform(uTile_, tile);
			const float stage1[4] = {
				bgfx::isValid(stage1Tex) ? float(mat.stage1Op) : 0.f, 0.f, 0.f, 0.f};
			bgfx::setUniform(uStage1_, stage1);
			// c10: this mesh's specular colour, its power in w - the level's
			// DynamicLighting on a pack or map mesh.
			std::array<float, 4> spec = partIndex < instance.partSpecular.size()
					? instance.partSpecular[partIndex] : kDefaultSpecular;
			if (model.worldMesh)
				spec = {worldMeshSpecular_[0], worldMeshSpecular_[1], worldMeshSpecular_[2],
						worldMeshSpecular_[3]};
			if (unlit) spec = {0.f, 0.f, 0.f, 1.f};
			// specMask is u_specColor in the plain program: zeroing it takes the
			// directional highlight and the dynamic lights with it. Pf.Mode96.
			const float specParams[4] = {spec[3], 1.f, kSpecularGate, specSlots};
			const float specColor[4] = {
				specularEnabled_ ? spec[0] : 0.f,
				specularEnabled_ ? spec[1] : 0.f,
				specularEnabled_ ? spec[2] : 0.f, 0.f};
			bgfx::setUniform(uSpecular_, specParams);
			bgfx::setUniform(uSpecColor_, specColor);
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
			// Demon Morph: a demonic model goes to the demon pass alone, as the
			// fresnel detail times the ramp (AnimatedMeshMatPal::RenderDemonFX).
			const bool demonDraw = demonOn_ && instance.demonic;
			if (demonDraw) {
				const float fresnel[4] = {demonScale_, 0.f, 0.f, 0.f};
				bgfx::setUniform(uDemonFresnel_, fresnel);
				bgfx::setTexture(0, sDiffuse_, demonDetail_);
				bgfx::setTexture(1, sStage1_, demonRamp_, BGFX_SAMPLER_UVW_CLAMP);
			} else {
				bgfx::setTexture(0, sDiffuse_, part.diffuse, FilteredSampler(mat.sampler[0]));
				// Stage 1 when the material has one; white through the off path
				// so the sampler is always bound.
				bgfx::setTexture(1, sStage1_,
						bgfx::isValid(stage1Tex) ? stage1Tex : white_,
						FilteredSampler(mat.sampler[1]));
			}
			// Stages 2 and 3 are the projector pair, 4 the flashlight's
			// shadow map, 5 the model shadow map, 6 the placed lights'
			// atlas; models sample no detail map, so nothing else wants them.
			lightUniforms_.Submit(unlit ? unlitLights : lights, 2, 3,
					bgfx::isValid(projector_.cookie()) ? projector_.cookie() : white_,
					bgfx::isValid(projector_.falloff()) ? projector_.falloff() : white_,
					4, shadowTex, 5, BGFX_INVALID_HANDLE, 6, lightShadowTex);
			BindViewModel(instance.viewModel, vmCells);
			// The weapon in the nearest tenth of the depth range: the world cannot
			// cover it. PAINFUL_VMDEPTH overrides the scale, 1 turns it off.
			static const float kViewModelDepth = DebugFloat("PAINFUL_VMDEPTH", 0.1f);
			const float viewDepth[4] = {instance.viewModel ? kViewModelDepth : 1.f,
					bgfx::getCaps()->homogeneousDepth ? 1.f : 0.f, 0.f, 0.f};
			bgfx::setUniform(uViewDepth_, viewDepth);
			// The model water look (palskin_water): its own program, the cube
			// map at stage 1, MDL.SetMaterialRefractFresnel's numbers per mesh.
			const bgfx::TextureHandle cubeTex = bgfx::isValid(envCube_) ? envCube_ : levelCube_;
			if (part.water && !demonDraw && bgfx::isValid(waterProgram_) && bgfx::isValid(cubeTex)) {
				Instance::MeshWater mw;
				for (const Instance::MeshWater& w : instance.water)
					if (w.mesh == part.name) { mw = w; break; }
				const float entWater[4] = {mw.refract, mw.refract * mw.refract, mw.fresnel,
						mat.fshader == "skin_dirtywater" ? 1.f : 0.f};
				const float reflTint[4] = {mw.reflTint[0], mw.reflTint[1], mw.reflTint[2], 0.f};
				const float refrTint[4] = {mw.refrTint[0], mw.refrTint[1], mw.refrTint[2], 0.f};
				bgfx::setUniform(uEntWater_, entWater);
				bgfx::setUniform(uEntWaterRefl_, reflTint);
				bgfx::setUniform(uEntWaterRefr_, refrTint);
				bgfx::setTexture(1, sEnvCube_, cubeTex);
				bgfx::setState(state);
				bgfx::submit(view, waterProgram_);
				++drawCalls_;
				continue;
			}
			bgfx::setState(state);
			// MDL.EnableNormalMaps on a part that has one: the second stream (the
			// posed rows, or the bind pose's identity) and the map at stage 8.
			const Part& ownerPart = model.parts[owner < model.parts.size() ? owner : partIndex];
			const bool posedRot = usePosed && owner < instance.posedRot.size() &&
					bgfx::isValid(instance.posedRot[owner]);
			const bool bump = specularEnabled_ && !demonDraw && instance.normalMaps && bgfx::isValid(programNm_) &&
					bgfx::isValid(part.normalMap) && (posedRot || bgfx::isValid(ownerPart.bindRot));
			if (bump) {
				if (posedRot) bgfx::setVertexBuffer(1, instance.posedRot[owner]);
				else bgfx::setVertexBuffer(1, ownerPart.bindRot);
				bgfx::setTexture(8, sNormalMap_, part.normalMap, FilteredSampler(mat.sampler[0]));
				bgfx::submit(view, programNm_);
			} else if (demonDraw) {
				bgfx::submit(demonView_, demonProgram_);
			} else {
				bgfx::submit(view, program_);
			}
			++drawCalls_;
		}
	}
}

} // namespace painful
