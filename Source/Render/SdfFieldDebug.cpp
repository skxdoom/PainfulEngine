#include "SdfFieldDebug.h"
#include "FullScreenPass.h"
#include "SdfField.h"
#include "ShaderLoad.h"

#include <bx/math.h>

namespace painful {

bool SdfFieldDebug::Init(const std::string& shaderDir) {
	bgfx::ShaderHandle vs = LoadShader(shaderDir, "vs_post");
	bgfx::ShaderHandle fs = LoadShader(shaderDir, "fs_sdffield");
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

void SdfFieldDebug::Shutdown() {
	for (bgfx::UniformHandle* u : {&uInvViewProj_, &uEye_, &uScreen_}) {
		if (bgfx::isValid(*u)) bgfx::destroy(*u);
		*u = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
}

void SdfFieldDebug::Draw(bgfx::ViewId view, const Camera& camera, int width, int height,
		const SdfField& field) {
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
	field.BindSurfaces(0);
	bgfx::submit(view, program_);
}

} // namespace painful
