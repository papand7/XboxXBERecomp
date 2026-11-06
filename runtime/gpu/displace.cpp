#include "gpu/displace.h"

#include <cmath>
#include <cstring>
#include <unordered_map>

#include "gpu/detail.h"

namespace gpu {
namespace {

constexpr uint32_t kMaxTriangles = 6000;  // a draw cut no further past this (levels times four each)

// a kind's height, its detail picture's 64 x 64 level (the broad lumps; the finer are the bumps' shading)
struct Heights {
    int n = 0;
    std::vector<float> h;
    float At(float u, float v) const {
        u = (u - std::floor(u)) * n - 0.5f, v = (v - std::floor(v)) * n - 0.5f;
        int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        float fx = u - x0, fy = v - y0;
        auto t = [&](int x, int y) { return h[size_t(((y % n + n) % n) * n + (x % n + n) % n)]; };
        return (t(x0, y0) * (1 - fx) + t(x0 + 1, y0) * fx) * (1 - fy) + (t(x0, y0 + 1) * (1 - fx) + t(x0 + 1, y0 + 1) * fx) * fy;
    }
};
const Heights& HeightsOf(uint8_t kind) {
    static Heights made[kDetailKinds];
    Heights& hs = made[kind];
    if (hs.n) return hs;
    std::vector<std::vector<uint32_t>> levels;
    DetailPicture(kind, levels);
    size_t lv = std::min<size_t>(2, levels.empty() ? 0 : levels.size() - 1);
    hs.n = int(kDetailSize >> lv);
    hs.h.resize(size_t(hs.n) * hs.n, 0.5f);
    if (lv < levels.size() && levels[lv].size() >= hs.h.size())
        for (size_t i = 0; i < hs.h.size(); i++) hs.h[i] = float(levels[lv][i] >> 24) / 255.0f;
    // stretched to 0 to 1, so every kind pushes in as deep
    float lo = 1, hi = 0;
    for (float v : hs.h) lo = std::min(lo, v), hi = std::max(hi, v);
    for (float& v : hs.h) v = hi > lo ? (v - lo) / (hi - lo) : 0.5f;
    return hs;
}

struct V3 { float x, y, z; };
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Unit(V3 a) {
    float l = std::sqrt(Dot(a, a));
    return l > 1e-20f ? a * (1 / l) : V3{0, 1, 0};
}

}  // namespace

bool DisplaceMesh(Assembled& a, const DisplaceParams& p) {
    if (a.vertices.empty() || a.topology != Topology::Triangles || a.strip || !(a.regs & 1) || a.buffer_count != 1 ||
        a.indices.size() < 3 || p.kind == 0 || p.kind >= kDetailKinds || p.depth <= 0 || p.tile <= 0)
        return false;
    const uint32_t regs = a.buffers[0].stride / 16;
    const uint32_t at = a.elements[0].offset / 16;  // register 0's float4 in a vertex
    if (!regs || at >= regs) return false;
    size_t tris = a.indices.size() / 3;
    int levels = p.levels;
    while (levels > 0 && tris << (2 * levels) > kMaxTriangles) levels--;
    if (levels <= 0) return false;

    std::vector<float> verts = a.vertices;
    std::vector<uint32_t> ix(a.indices.begin(), a.indices.begin() + tris * 3);
    const size_t vf = size_t(regs) * 4;
    auto place = [&](uint32_t v) { const float* q = &verts[v * vf + at * 4]; return V3{q[0], q[1], q[2]}; };

    // welded: a place each, whichever vertices (texture seams) stand on it
    std::vector<uint32_t> wid;
    std::vector<V3> wpos;
    std::unordered_map<uint64_t, uint32_t> weld;
    auto weld_of = [&](V3 v) {
        uint32_t b[3];
        std::memcpy(b, &v, 12);
        uint64_t key = (uint64_t(b[0]) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(b[1]) * 0xC2B2AE3D27D4EB4Full) ^ b[2];
        auto [it, fresh] = weld.try_emplace(key, uint32_t(wpos.size()));
        if (fresh) wpos.push_back(v);
        return it->second;
    };
    for (uint32_t v = 0; v < verts.size() / vf; v++) wid.push_back(weld_of(place(v)));

    // ground spans metres: the gun, hands and props (their own small model spaces, skinned) are left alone
    V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const V3& q : wpos)
        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)}, hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
    if (std::max(hi.x - lo.x, hi.z - lo.z) < 3.0f) return false;
    // the ground faces up (the world's +y); a draw mostly facing down or sideways is left alone (walls, ceilings)
    V3 mean{0, 0, 0};
    for (size_t t = 0; t < tris; t++) {
        V3 a0 = place(ix[t * 3]), a1 = place(ix[t * 3 + 1]), a2 = place(ix[t * 3 + 2]);
        mean = mean + Cross(a1 - a0, a2 - a0);
    }
    float ml = std::sqrt(Dot(mean, mean));
    if (ml <= 0 || std::fabs(mean.y) < 0.5f * ml) return false;
    const float facing = mean.y > 0 ? 1.0f : -1.0f;

    // cut each triangle in four, level by level; an edge's middle made once for both its triangles (no cracks)
    for (int lv = 0; lv < levels; lv++) {
        std::unordered_map<uint64_t, uint32_t> mids;
        std::vector<uint32_t> out;
        out.reserve(ix.size() * 4);
        auto mid = [&](uint32_t i, uint32_t j) {
            uint32_t lo = std::min(i, j), hi = std::max(i, j);
            uint64_t key = uint64_t(lo) << 32 | hi;
            auto [it, fresh] = mids.try_emplace(key, 0);
            if (!fresh) return it->second;
            uint32_t n = uint32_t(verts.size() / vf);
            verts.resize(verts.size() + vf);
            for (size_t k = 0; k < vf; k++) verts[n * vf + k] = (verts[lo * vf + k] + verts[hi * vf + k]) * 0.5f;
            // the same place from either side of a seam: the welded ends' middle
            uint32_t wa = std::min(wid[lo], wid[hi]), wb = std::max(wid[lo], wid[hi]);
            V3 m = (wpos[wa] + wpos[wb]) * 0.5f;
            std::memcpy(&verts[n * vf + at * 4], &m, 12);
            wid.push_back(weld_of(m));
            return it->second = n;
        };
        for (size_t t = 0; t + 2 < ix.size(); t += 3) {
            uint32_t v0 = ix[t], v1 = ix[t + 1], v2 = ix[t + 2];
            uint32_t m01 = mid(v0, v1), m12 = mid(v1, v2), m20 = mid(v2, v0);
            out.insert(out.end(), {v0, m01, m20, m01, v1, m12, m20, m12, v2, m01, m12, m20});
        }
        ix.swap(out);
    }

    // each welded place's normal (the faces about it, by area) and whether it is on the mesh's edge (an edge one
    // triangle has): those stay, so the draw still meets its neighbours
    const size_t nw = wpos.size();
    std::vector<V3> wn(nw, V3{0, 0, 0});
    std::unordered_map<uint64_t, uint32_t> uses;
    for (size_t t = 0; t + 2 < ix.size(); t += 3) {
        uint32_t w[3] = {wid[ix[t]], wid[ix[t + 1]], wid[ix[t + 2]]};
        V3 f = Cross(wpos[w[1]] - wpos[w[0]], wpos[w[2]] - wpos[w[0]]) * facing;
        for (int k = 0; k < 3; k++) {
            wn[w[k]] = wn[w[k]] + f;
            uint32_t lo = std::min(w[k], w[(k + 1) % 3]), hi = std::max(w[k], w[(k + 1) % 3]);
            uses[uint64_t(lo) << 32 | hi]++;
        }
    }
    std::vector<uint8_t> edge(nw, 0);
    for (const auto& [key, n] : uses)
        if (n == 1) edge[uint32_t(key >> 32)] = edge[uint32_t(key)] = 1;

    // pushed in by the height (its highest stays on the surface the game collides with), projected along the
    // normal's main axis, so a place has one height whichever triangle it is in
    const Heights& hs = HeightsOf(p.kind);
    std::vector<V3> moved(wpos);
    for (size_t w = 0; w < nw; w++) {
        if (edge[w] || !p.push) continue;
        V3 n = Unit(wn[w]);
        V3 q = wpos[w] * (1 / p.tile);
        float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        float h = ay >= ax && ay >= az ? hs.At(q.x, q.z) : ax >= az ? hs.At(q.z, q.y) : hs.At(q.x, q.y);
        moved[w] = wpos[w] - n * ((1 - h) * p.depth);
    }
    const uint32_t nv = uint32_t(verts.size() / vf);
    for (uint32_t v = 0; v < nv; v++) std::memcpy(&verts[v * vf + at * 4], &moved[wid[v]], 12);

    a.vertices.swap(verts);
    a.indices.swap(ix);
    a.vertex_count = nv;
    a.buffers[0] = {reinterpret_cast<const uint8_t*>(a.vertices.data()), uint32_t(a.vertices.size() * 4), regs * 16};
    a.index_src = a.indices.data();
    a.index_count = uint32_t(a.indices.size());
    a.index16 = false;
    a.base_vertex = 0;
    return true;
}

}  // namespace gpu
