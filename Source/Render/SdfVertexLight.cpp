#include "SdfVertexLight.h"
#include "SdfField.h"
#include "ShaderLoad.h"
#include "../Core/Log.h"

#include <algorithm>

namespace painful {

namespace {

constexpr uint64_t kPoint = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
constexpr int kJobRows = SdfVertexLight::kTracesPerFrame / SdfVertexLight::kJobWidth;
const char* const kSheenSamplers[3] = {"s_sdfSheenR", "s_sdfSheenG", "s_sdfSheenB"};

void Destroy(bgfx::TextureHandle& t) {
	if (bgfx::isValid(t)) bgfx::destroy(t);
	t = BGFX_INVALID_HANDLE;
}

void Destroy(bgfx::UniformHandle& u) {
	if (bgfx::isValid(u)) bgfx::destroy(u);
	u = BGFX_INVALID_HANDLE;
}

} // namespace

bool SdfVertexLight::Init(const std::string& shaderDir) {
	const bgfx::Caps* caps = bgfx::getCaps();
	const uint32_t r32u = caps->formats[bgfx::TextureFormat::R32U];
	const uint32_t rgba16f = caps->formats[bgfx::TextureFormat::RGBA16F];
	const bool compute = (caps->supported & BGFX_CAPS_COMPUTE) != 0;
	const bool images = (r32u & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ) && (r32u & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) &&
			(rgba16f & BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE);
	if (!compute || !images) {
		LogWarn("distance field: per-vertex light needs compute (%s), R32U images read and written and RGBA16F "
				"images written (%s); RendererType 1 stays off", compute ? "yes" : "no", images ? "yes" : "no");
		return false;
	}
	const bgfx::ShaderHandle cs = LoadShader(shaderDir, "cs_sdfvertex");
	if (bgfx::isValid(cs)) program_ = bgfx::createProgram(cs, true);
	const std::vector<uint32_t> zero(size_t(kSide) * size_t(kSide), 0);
	history_ = bgfx::createTexture2D(uint16_t(kSide), uint16_t(kSide), false, 1, bgfx::TextureFormat::R32U,
			BGFX_TEXTURE_COMPUTE_WRITE | kPoint, bgfx::copy(zero.data(), uint32_t(zero.size() * sizeof(uint32_t))));
	for (int c = 0; c < 3; ++c) {
		sheen_[c] = bgfx::createTexture2D(uint16_t(kSide), uint16_t(kSide), false, 1, bgfx::TextureFormat::RGBA16F,
				BGFX_TEXTURE_COMPUTE_WRITE | kPoint);
		sSheen_[c] = bgfx::createUniform(kSheenSamplers[c], bgfx::UniformType::Sampler);
	}
	jobPosTexture_ = bgfx::createTexture2D(uint16_t(kJobWidth), uint16_t(kJobRows), false, 1,
			bgfx::TextureFormat::RGBA32F, kPoint);
	jobNormalTexture_ = bgfx::createTexture2D(uint16_t(kJobWidth), uint16_t(kJobRows), false, 1,
			bgfx::TextureFormat::RGBA32F, kPoint);
	sHistory_ = bgfx::createUniform("s_sdfVertexLight", bgfx::UniformType::Sampler);
	sJobPos_ = bgfx::createUniform("s_sdfJobPos", bgfx::UniformType::Sampler);
	sJobNormal_ = bgfx::createUniform("s_sdfJobNormal", bgfx::UniformType::Sampler);
	uJob_ = bgfx::createUniform("u_sdfVertexJob", bgfx::UniformType::Vec4);
	uBlend_ = bgfx::createUniform("u_sdfVertexBlend", bgfx::UniformType::Vec4);
	uVertex_ = bgfx::createUniform("u_sdfVertex", bgfx::UniformType::Vec4);
	jobPos_.assign(size_t(kTracesPerFrame) * 4, 0.f);
	jobNormal_.assign(size_t(kTracesPerFrame) * 4, 0.f);
	Clear();
	ok_ = bgfx::isValid(program_) && bgfx::isValid(history_) && bgfx::isValid(jobPosTexture_) &&
			bgfx::isValid(jobNormalTexture_) && bgfx::isValid(sheen_[0]) && bgfx::isValid(sheen_[1]) &&
			bgfx::isValid(sheen_[2]);
	if (!ok_) LogWarn("distance field: the per-vertex program or textures did not load; RendererType 1 stays off");
	return ok_;
}

void SdfVertexLight::Shutdown() {
	if (bgfx::isValid(program_)) bgfx::destroy(program_);
	program_ = BGFX_INVALID_HANDLE;
	for (bgfx::TextureHandle* t : {&history_, &sheen_[0], &sheen_[1], &sheen_[2], &jobPosTexture_, &jobNormalTexture_})
		Destroy(*t);
	for (bgfx::UniformHandle* u : {&sHistory_, &sSheen_[0], &sSheen_[1], &sSheen_[2], &sJobPos_, &sJobNormal_, &uJob_,
			&uBlend_, &uVertex_})
		Destroy(*u);
	ok_ = false;
}

void SdfVertexLight::Clear() {
	free_.assign(1, {0, kSide * kSide});
	jobs_ = 0;
	++generation_;
}

int SdfVertexLight::Allocate(int count) {
	if (count <= 0) return -1;
	for (size_t i = 0; i < free_.size(); ++i) {
		std::pair<int, int>& run = free_[i];
		if (run.second < count) continue;
		const int first = run.first;
		run.first += count;
		run.second -= count;
		if (run.second == 0) free_.erase(free_.begin() + ptrdiff_t(i));
		return first;
	}
	return -1;
}

void SdfVertexLight::Release(int first, int count) {
	if (first < 0 || count <= 0) return;
	auto at = free_.insert(std::lower_bound(free_.begin(), free_.end(), std::make_pair(first, 0)), {first, count});
	if (at + 1 != free_.end() && at->first + at->second == (at + 1)->first) {
		at->second += (at + 1)->second;
		free_.erase(at + 1);
	}
	if (at != free_.begin() && (at - 1)->first + (at - 1)->second == at->first) {
		(at - 1)->second += at->second;
		free_.erase(at);
	}
}

bool SdfVertexLight::WasTraced(uint32_t frame) const {
	if (frame >= frame_) return false;
	const uint32_t back = frame_ - 1 - frame;
	return back < 64 && ((traced_ >> back) & 1u) != 0;
}

void SdfVertexLight::Queue(const Vec3& pos, const Vec3& normal, int slot, bool reset) {
	if (jobs_ >= kTracesPerFrame || slot < 0) return;
	float* p = &jobPos_[size_t(jobs_) * 4];
	float* n = &jobNormal_[size_t(jobs_) * 4];
	p[0] = pos.x;
	p[1] = pos.y;
	p[2] = pos.z;
	p[3] = float(slot);
	n[0] = normal.x;
	n[1] = normal.y;
	n[2] = normal.z;
	n[3] = reset ? 1.f : 0.f;
	++jobs_;
}

void SdfVertexLight::Dispatch(const SdfField& field, bgfx::ViewId view) {
	const int jobs = jobs_;
	jobs_ = 0;
	reserved_ = 0;
	const bgfx::Stats* stats = bgfx::getStats();
	if (stats && stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
		statGpuMs_ += double(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1000.0 / double(stats->gpuTimerFreq);
	statJobs_ += double(jobs);
	if (++statFrames_ == statSayAt_) {
		LogInfo("distance field: per-vertex light traced %.0f vertices a frame over %u frames, GPU frame %.1f ms",
				statJobs_ / double(statFrames_), statFrames_, statGpuMs_ / double(statFrames_));
		statSayAt_ = statSayAt_ < 3600 ? statSayAt_ * 10 : statSayAt_ + 3600;
	}
	fieldGeneration_ = field.lightGeneration();
	const bool trace = ok_ && jobs > 0 && field.ready();
	traced_ = (traced_ << 1) | (trace ? 1u : 0u);
	++frame_;
	if (!trace) return;
	const uint16_t rows = uint16_t((jobs + kJobWidth - 1) / kJobWidth);
	const uint32_t bytes = uint32_t(rows) * uint32_t(kJobWidth) * 16;
	bgfx::updateTexture2D(jobPosTexture_, 0, 0, 0, 0, uint16_t(kJobWidth), rows, bgfx::copy(jobPos_.data(), bytes));
	bgfx::updateTexture2D(jobNormalTexture_, 0, 0, 0, 0, uint16_t(kJobWidth), rows,
			bgfx::copy(jobNormal_.data(), bytes));
	field.BindSurfaces(6);
	const float job[4] = {float(jobs), float(kJobWidth), float(kSide), 0.f};
	const float blend[4] = {kBlend, 0.f, 0.f, 0.f};
	bgfx::setUniform(uJob_, job);
	bgfx::setUniform(uBlend_, blend);
	bgfx::setImage(0, history_, 0, bgfx::Access::ReadWrite, bgfx::TextureFormat::R32U);
	for (int c = 0; c < 3; ++c)
		bgfx::setImage(uint8_t(1 + c), sheen_[c], 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
	bgfx::setTexture(4, sJobPos_, jobPosTexture_, kPoint);
	bgfx::setTexture(5, sJobNormal_, jobNormalTexture_, kPoint);
	bgfx::dispatch(view, program_, uint32_t((jobs + 63) / 64), 1, 1);
}

void SdfVertexLight::Bind(uint8_t stage, int firstSlot, int sheenStage) const {
	const float vertex[4] = {float(std::max(firstSlot, 0)), ok_ && firstSlot >= 0 ? 1.f : 0.f, float(kSide), 0.f};
	bgfx::setUniform(uVertex_, vertex);
	if (!ok_) return;
	bgfx::setTexture(stage, sHistory_, history_, kPoint);
	if (sheenStage >= 0)
		for (int c = 0; c < 3; ++c) bgfx::setTexture(uint8_t(sheenStage + c), sSheen_[c], sheen_[c], kPoint);
}

} // namespace painful
