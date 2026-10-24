#include "Trails.h"
#include "EffectIni.h"

#include "../Core/FileSystem.h"
#include "../Core/Log.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace painful {

const TrailDef& TrailLibrary::Get(const std::string& name) {
	std::string key = name;
	for (char& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
	auto it = defs_.find(key);
	if (it != defs_.end()) return it->second;

	// TrailEffect::Load (0x100b0290): [General] Texture, LifeTime, Width, FPS,
	// BlendMode (the material system's names, FUN_100973a0), ConstLen as a flag.
	TrailDef def;
	def.name = name;
	std::vector<uint8_t> bytes;
	const std::string path = dir_ + "/" + name + ".ini";
	if (ReadFile(path, bytes) && !bytes.empty()) {
		const auto ini = ParseIni(std::string(bytes.begin(), bytes.end()));
		auto get = [&](const char* k) -> const std::string* {
			auto f = ini.find(std::string("general.") + k);
			return f == ini.end() ? nullptr : &f->second;
		};
		if (const std::string* v = get("texture")) def.texture = "Particles/" + *v;
		if (const std::string* v = get("lifetime")) def.lifeTime = float(std::atof(v->c_str()));
		if (const std::string* v = get("width")) def.width = float(std::atof(v->c_str()));
		if (const std::string* v = get("fps")) def.fps = float(std::atof(v->c_str()));
		if (const std::string* v = get("blendmode")) def.blendMode = BlendModeByName(*v);
		if (const std::string* v = get("constlen")) def.constLen = std::atol(v->c_str()) != 0;
	} else {
		LogWarn("trails: %s not found, using the defaults", path.c_str());
	}
	if (def.lifeTime <= 0.f) def.lifeTime = 1.f;
	return defs_.emplace(key, def).first->second;
}

void TrailRibbon::Attach(int rails) {
	rails_ = rails;
	stride_ = rails < 2 ? 2 : rails;
	cap_ = 2000;
	points_.assign(size_t(cap_) * size_t(stride_), Vec3());
	times_.assign(size_t(cap_), 0.f);
	head_ = tail_ = 0;
	attached_ = true;
}

void TrailRibbon::Tick(float dt, const Vec3* points, int count) {
	if (!def_->constLen) time_ += dt;
	if (attached_ && count > 0) AddNode(points, count);
	if (attached_) CutTail();
}

void TrailRibbon::AddNode(const Vec3* p, int count) {
	const float d = (p[0] - last_).Length();
	const float spacing = def_->width * scale_;
	if (rails_ > 1) {
		// The newest node follows the joints; a new one is begun once the
		// first joint has moved a quarter of the width from the last.
		int next;
		if (d >= spacing * 0.25f) {
			if (def_->constLen) time_ += d;
			last_ = p[0];
			next = (head_ + 1) % cap_;
		} else {
			next = head_;
			head_ = (head_ - 1 + cap_) % cap_;
		}
		for (int j = 0; j < rails_ && j < count; ++j)
			points_[size_t(head_) * size_t(stride_) + size_t(j)] = p[j];
		times_[size_t(head_)] = time_;
		head_ = next;
		if (head_ == tail_) tail_ = (tail_ + 1) % cap_;
		return;
	}
	// One point: a node every half width, with the direction it came from.
	if (d < spacing * 0.5f) return;
	if (def_->constLen) time_ += d;
	last_ = p[0];
	const int next = (head_ + 1) % cap_;
	Vec3* node = &points_[size_t(head_) * 2];
	node[0] = p[0];
	if (head_ == tail_) {
		node[1] = Vec3();
	} else {
		const Vec3* prev = &points_[size_t((head_ - 1 + cap_) % cap_) * 2];
		node[1] = p[0] - prev[0];
		if (node[1].Length() < 1e-7f) node[1] = prev[1];
	}
	const float len = node[1].Length();
	if (len > 1e-7f) node[1] *= 1.f / len;
	times_[size_t(head_)] = time_;
	head_ = next;
	if (head_ == tail_) tail_ = (tail_ + 1) % cap_;
}

void TrailRibbon::CutTail() {
	// The tail is left on the newest node past its LifeTime, so the ribbon
	// reaches the end of the texture before it goes; a span over twice the
	// LifeTime drops that one too.
	for (int i = tail_; i != head_; i = (i + 1) % cap_) {
		if ((time_ - times_[size_t(i)]) / def_->lifeTime <= 1.f) break;
		tail_ = i;
	}
	if (tail_ != head_ &&
			times_[size_t((head_ - 1 + cap_) % cap_)] - times_[size_t(tail_)] > 2.f * def_->lifeTime)
		tail_ = (tail_ + 1) % cap_;
}

} // namespace painful
