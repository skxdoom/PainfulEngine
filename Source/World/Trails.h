#pragma once
#include "../Core/Vectors.h"
#include <map>
#include <string>
#include <vector>

namespace painful {

// One Data/Scripts/Trails/<name>.ini, with TrailEffect's defaults (0x100b0180)
// where a key is absent. Docs/Reference/Trails.md.
struct TrailDef {
	std::string name;
	std::string texture; // "Particles/<Texture>"
	int blendMode = 5; // translucent
	float width = 0.1f;
	float lifeTime = 1.f;
	float fps = 0.f;
	bool constLen = false; // the clock runs on distance, not seconds
};

class TrailLibrary {
public:
	void Init(const std::string& scriptsDir) { dir_ = scriptsDir + "/Trails"; }
	const TrailDef& Get(const std::string& name);

private:
	std::string dir_;
	std::map<std::string, TrailDef> defs_;
};

// One trail entity's ribbon (Trail::AddNode 0x101e9cd0, CutTail 0x101ea080):
// a ring of nodes, each the sampled points and the clock when they were
// taken. With `rails` of 2 or more a node is one point per joint and the
// ribbon is strung between them; below that a node is one point and the
// direction it moved in, and the ribbon faces the camera when drawn.
class TrailRibbon {
public:
	explicit TrailRibbon(const TrailDef& def) : def_(&def) {}

	// Trail::Init(2000, max(rails, 2)) as AttachToBones calls it.
	void Attach(int rails);
	void SetScale(float s) { scale_ = s; }

	// Trail::Tick: the clock, then AddNode with this frame's points (none
	// when the owner is gone), then CutTail.
	void Tick(float dt, const Vec3* points, int count);
	// Nothing left to draw: CutTail's test before the trail deletes itself.
	bool Empty() const { return !attached_ || tail_ == head_ || (tail_ + 1) % cap_ == head_; }

	const TrailDef& def() const { return *def_; }
	int rails() const { return rails_; }
	float scale() const { return scale_; }
	// Nodes, oldest first: [tail, head).
	int nodeCount() const { return attached_ ? (cap_ - tail_ + head_) % cap_ : 0; }
	// The node's points (rails() of them), or its position and direction.
	const Vec3* node(int i) const { return &points_[size_t((tail_ + i) % cap_) * size_t(stride_)]; }
	// u along the ribbon: the node's age over LifeTime.
	float age(int i) const { return (time_ - times_[size_t((tail_ + i) % cap_)]) / def_->lifeTime; }

private:
	void AddNode(const Vec3* points, int count);
	void CutTail();

	const TrailDef* def_;
	bool attached_ = false;
	int rails_ = 1, stride_ = 2, cap_ = 1;
	int head_ = 0, tail_ = 0;
	float scale_ = 1.f, time_ = 0.f;
	Vec3 last_; // where a node was last committed (+0x78c)
	std::vector<Vec3> points_;
	std::vector<float> times_;
};

} // namespace painful
