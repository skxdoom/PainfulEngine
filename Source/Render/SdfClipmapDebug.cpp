#include "SdfClipmapDebug.h"
#include "FullScreenPass.h"
#include "SdfProbes.h"
#include "ShaderLoad.h"

#include <bx/math.h>

namespace painful {

bool SdfClipmapDebug::Init(const std::string& shaderDir) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_sdfclipmap");
	if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
		if (bgfx::isValid(vs)) bgfx::destroy(vs);
		if (bgfx::isValid(fs)) bgfx::destroy(fs);
		return false;
	}
	program_ = bgfx::createProgram(vs, fs, true);
	layout_ = PostVertexLayout();
	uInvViewProj_ = bgfx::createUniform("u_sdfInvViewProj", bgfx::UniformType::Mat4);
	uEye_ = bgfx::createUniform("u_sdfEye", bgfx::UniformType::Vec4);
	uScreen_ = bgfx::createUniform("u_sdfScreen", bgfx::UniformType::Vec4);
	return bgfx::isValid(program_);
}

void SdfClipmapDebug::Shutdown() {
	for (bgfx::UniformHandle* u : {&uInvViewProj_, &uEye_, &uScreen_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
}

void SdfClipmapDebug::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
		const SdfProbes& probes) {
	if (!bgfx::isValid(program_) || width <= 0 || height <= 0) return;
	float viewMtx[16], projMtx[16], viewProj[16], inverse[16];
	camera.ViewProj(width, height, camera.farPlane, viewMtx, projMtx);
	bx::mtxMul(viewProj, viewMtx, projMtx);
	bx::mtxInverse(inverse, viewProj);
	const float eye[4] = {camera.pos.x, camera.pos.y, camera.pos.z, 0.f};
	const float screen[4] = {float(width), float(height),
			bgfx::getCaps()->originBottomLeft ? 1.f : 0.f, 0.f};

	FullScreenTriangle(view, layout_, width, height);
	bgfx::setUniform(uInvViewProj_, inverse);
	bgfx::setUniform(uEye_, eye);
	bgfx::setUniform(uScreen_, screen);
	probes.BindSurfaces(0);
	bgfx::submit(view, program_);
}

} // namespace painful
