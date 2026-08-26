#include "CollisionMesh.h"
#include "../Core/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace painful {

namespace {
constexpr uint32_t kLeafSize = 8;
constexpr int kMaxDepth = 40;
}  // namespace

void CollisionMesh::Clear() {
    tris_.clear();
    nodes_.clear();
    centroids_.clear();
}

void CollisionMesh::Build(const MapMesh& map, float worldScale) {
    Clear();

    size_t total = 0;
    for (const MapObject& o : map.objects)
        if (o.isCollidable()) total += o.triangleCount();
    tris_.reserve(total);

    for (const MapObject& o : map.objects) {
        if (!o.isCollidable()) continue;
        for (size_t t = 0; t + 2 < o.indices.size(); t += 3) {
            float p[3][3];
            bool ok = true;
            for (int c = 0; c < 3; ++c) {
                const size_t vi = o.indices[t + c];
                if (vi >= o.vertexCount()) { ok = false; break; }
                o.position(vi, p[c]);
            }
            if (!ok) continue;
            // Map objects carry their own transform; every shipped map has it
            // at identity, but honouring it costs nothing and avoids a silent
            // wrong answer if one ever does not.
            for (int c = 0; c < 3; ++c) {
                float w[3];
                o.transform.TransformPoint(p[c][0], p[c][1], p[c][2], w);
                for (int a = 0; a < 3; ++a) p[c][a] = w[a] * worldScale;
            }
            Tri tri;
            for (int a = 0; a < 3; ++a) {
                tri.v0[a] = p[0][a];
                tri.e1[a] = p[1][a] - p[0][a];
                tri.e2[a] = p[2][a] - p[0][a];
            }
            tris_.push_back(tri);
        }
    }
    if (tris_.empty()) return;

    centroids_.resize(tris_.size() * 3);
    for (size_t i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
        for (int a = 0; a < 3; ++a)
            centroids_[i * 3 + a] = t.v0[a] + (t.e1[a] + t.e2[a]) * (1.f / 3.f);
    }

    nodes_.reserve(tris_.size() / kLeafSize * 2 + 16);
    BuildNode(0, static_cast<uint32_t>(tris_.size()), 0);
    centroids_.clear();
    centroids_.shrink_to_fit();

    LogInfo("collision: %zu triangles, %zu BVH nodes", tris_.size(), nodes_.size());
}

uint32_t CollisionMesh::BuildNode(uint32_t begin, uint32_t end, int depth) {
    const uint32_t self = static_cast<uint32_t>(nodes_.size());
    nodes_.emplace_back();

    float lo[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    float hi[3] = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max()};
    for (uint32_t i = begin; i < end; ++i) {
        const Tri& t = tris_[i];
        for (int c = 0; c < 3; ++c) {
            const float p[3] = {t.v0[c], t.v0[c] + t.e1[c], t.v0[c] + t.e2[c]};
            for (int k = 0; k < 3; ++k) {
                lo[c] = std::min(lo[c], p[k]);
                hi[c] = std::max(hi[c], p[k]);
            }
        }
    }
    for (int c = 0; c < 3; ++c) {
        nodes_[self].lo[c] = lo[c];
        nodes_[self].hi[c] = hi[c];
    }

    if (end - begin <= kLeafSize || depth >= kMaxDepth) {
        nodes_[self].start = begin;
        nodes_[self].count = end - begin;
        return self;
    }

    // Median split on the widest centroid axis. Not SAH - this is built once
    // per level over a few hundred thousand triangles and only ever answers
    // "does anything block this short segment", so build speed wins.
    int axis = 0;
    float widest = -1.f;
    for (int c = 0; c < 3; ++c) {
        float mn = std::numeric_limits<float>::max(), mx = -mn;
        for (uint32_t i = begin; i < end; ++i) {
            const float v = centroids_[i * 3 + c];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        if (mx - mn > widest) { widest = mx - mn; axis = c; }
    }

    const uint32_t mid = begin + (end - begin) / 2;
    // Sorting the centroid array alongside the triangles keeps the two in step.
    std::vector<uint32_t> order(end - begin);
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = begin + i;
    std::nth_element(order.begin(), order.begin() + (mid - begin), order.end(),
                     [&](uint32_t a, uint32_t b) {
                         return centroids_[a * 3 + axis] < centroids_[b * 3 + axis];
                     });
    {
        std::vector<Tri> tmpTris(end - begin);
        std::vector<float> tmpCent((end - begin) * 3);
        for (uint32_t i = 0; i < order.size(); ++i) {
            tmpTris[i] = tris_[order[i]];
            for (int c = 0; c < 3; ++c) tmpCent[i * 3 + c] = centroids_[order[i] * 3 + c];
        }
        for (uint32_t i = 0; i < order.size(); ++i) {
            tris_[begin + i] = tmpTris[i];
            for (int c = 0; c < 3; ++c) centroids_[(begin + i) * 3 + c] = tmpCent[i * 3 + c];
        }
    }

    BuildNode(begin, mid, depth + 1);
    nodes_[self].right = BuildNode(mid, end, depth + 1);
    nodes_[self].count = 0;
    return self;
}

bool CollisionMesh::Occluded(const float from[3], const float to[3]) const {
    if (nodes_.empty()) return false;

    float dir[3];
    for (int c = 0; c < 3; ++c) dir[c] = to[c] - from[c];

    float invDir[3];
    for (int c = 0; c < 3; ++c) {
        // A zero component would make the slab test 0*inf; the large finite
        // value keeps the comparisons well defined and still rejects.
        invDir[c] = dir[c] != 0.f ? 1.f / dir[c] : std::numeric_limits<float>::max();
    }

    // Explicit stack: the tree is shallow enough that 64 entries cannot
    // overflow at kMaxDepth 40, and recursion here would be the hot path.
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        const Node& n = nodes_[stack[--sp]];

        float tMin = 0.f, tMax = 1.f;
        for (int c = 0; c < 3; ++c) {
            float t0 = (n.lo[c] - from[c]) * invDir[c];
            float t1 = (n.hi[c] - from[c]) * invDir[c];
            if (t0 > t1) std::swap(t0, t1);
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
        }
        if (tMin > tMax) continue;

        if (n.count == 0) {
            if (sp + 2 <= 64) {
                stack[sp++] = n.right;
                stack[sp++] = static_cast<uint32_t>(&n - nodes_.data()) + 1;
            }
            continue;
        }

        for (uint32_t i = n.start; i < n.start + n.count; ++i) {
            const Tri& t = tris_[i];
            // Moller-Trumbore, two-sided: map geometry is single-sided but a
            // wall must block from either side.
            float pv[3];
            pv[0] = dir[1] * t.e2[2] - dir[2] * t.e2[1];
            pv[1] = dir[2] * t.e2[0] - dir[0] * t.e2[2];
            pv[2] = dir[0] * t.e2[1] - dir[1] * t.e2[0];
            const float det = t.e1[0] * pv[0] + t.e1[1] * pv[1] + t.e1[2] * pv[2];
            if (std::fabs(det) < 1e-12f) continue;
            const float inv = 1.f / det;

            float tv[3];
            for (int c = 0; c < 3; ++c) tv[c] = from[c] - t.v0[c];
            const float u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) * inv;
            if (u < 0.f || u > 1.f) continue;

            float qv[3];
            qv[0] = tv[1] * t.e1[2] - tv[2] * t.e1[1];
            qv[1] = tv[2] * t.e1[0] - tv[0] * t.e1[2];
            qv[2] = tv[0] * t.e1[1] - tv[1] * t.e1[0];
            const float v = (dir[0] * qv[0] + dir[1] * qv[1] + dir[2] * qv[2]) * inv;
            if (v < 0.f || u + v > 1.f) continue;

            const float hit = (t.e2[0] * qv[0] + t.e2[1] * qv[1] + t.e2[2] * qv[2]) * inv;
            if (hit > 1e-5f && hit <= 1.f) return true;
        }
    }
    return false;
}

} // namespace painful
