#include "gpu/displace.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "gpu/detail.h"
#include "gpu/shaders.h"

namespace gpu {
namespace {

constexpr uint32_t kMaxTriangles = 6000;  // a draw cut no further past this (levels times four each)
constexpr float kCrease = 0.82f;          // a place whose faces turn more than about 35 degrees from its normal stays

// a kind's height for the geometry: its detail picture's 8 x 8 level (the broad lumps; the finer are the detail's
// shading), softened (a vertex every half metre or so would sample finer lumps as noise), stretched to 0 to 1
struct Heights {
    int n = 0;
    std::vector<float> h;
    float At(float u, float v) const {
        u = (u - std::floor(u)) * n - 0.5f, v = (v - std::floor(v)) * n - 0.5f;
        int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        float fx = u - x0, fy = v - y0;
        // smoothstep between the texels: no creases along the texel rows
        fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
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
    size_t lv = std::min<size_t>(5, levels.empty() ? 0 : levels.size() - 1);
    hs.n = int(kDetailSize >> lv);
    hs.h.resize(size_t(hs.n) * hs.n, 0.5f);
    if (lv < levels.size() && levels[lv].size() >= hs.h.size())
        for (size_t i = 0; i < hs.h.size(); i++) hs.h[i] = float(levels[lv][i] >> 24) / 255.0f;
    // a 1 2 1 blur each way, wrapping (the picture tiles)
    const int n = hs.n;
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float> o(hs.h.size());
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                auto t = [&](int dx, int dy) { return hs.h[size_t(((y + dy + n) % n) * n + (x + dx + n) % n)]; };
                o[size_t(y) * n + x] = pass == 0 ? (t(-1, 0) + 2 * t(0, 0) + t(1, 0)) * 0.25f : (t(0, -1) + 2 * t(0, 0) + t(0, 1)) * 0.25f;
            }
        hs.h.swap(o);
    }
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
float Len(V3 a) { return std::sqrt(Dot(a, a)); }
V3 Unit(V3 a) {
    float l = Len(a);
    return l > 1e-20f ? a * (1 / l) : V3{0, 1, 0};
}


}  // namespace

uint64_t DisplaceParams::Key() const {
    const float f[] = {float(kind), depth, side_depth, tile, edge, float(levels), push ? 1.0f : 0.0f, float(color_reg), shade, facing};
    return Hash(f, sizeof(f));
}

bool DisplaceMesh(Assembled& a, const DisplaceParams& p, DisplaceResult* out) {
    if (a.vertices.empty() || a.topology != Topology::Triangles || a.strip || !(a.regs & 1) || a.buffer_count != 1 ||
        a.indices.size() < 3 || p.kind == 0 || p.kind >= kDetailKinds || (p.depth <= 0 && p.side_depth <= 0) || p.tile <= 0)
        return false;
    const uint32_t regs = a.buffers[0].stride / 16;
    const uint32_t at = a.elements[0].offset / 16;  // register 0's float4 in a vertex
    if (!regs || at >= regs) return false;
    const size_t tris = a.indices.size() / 3;
    const size_t vf = size_t(regs) * 4;
    const uint32_t nv0 = uint32_t(a.vertices.size() / vf);
    for (size_t i = 0; i < tris * 3; i++)
        if (a.indices[i] >= nv0) return false;

    std::vector<float> verts = a.vertices;
    std::vector<uint32_t> ix(a.indices.begin(), a.indices.begin() + tris * 3);
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
    for (uint32_t v = 0; v < nv0; v++) wid.push_back(weld_of(place(v)));

    // the scenery spans metres: the gun, hands and props (their own small model spaces, skinned) are left alone
    V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const V3& q : wpos)
        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)}, hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
    const float span = p.side_depth > 0 ? std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) : std::max(hi.x - lo.x, hi.z - lo.z);
    if (span < 3.0f) return false;

    // a register of unit vectors that agree with the faces: the vertex normals, which also tell the outside
    V3 mean{0, 0, 0};
    for (size_t t = 0; t < tris; t++) {
        V3 a0 = place(ix[t * 3]), a1 = place(ix[t * 3 + 1]), a2 = place(ix[t * 3 + 2]);
        mean = mean + Cross(a1 - a0, a2 - a0);
    }
    int normal_reg = -1;
    float facing = 0;
    for (uint32_t r = 1; r < 16 && normal_reg < 0; r++) {
        if (!((a.regs >> r) & 1) || int(r) == p.color_reg) continue;
        const uint32_t ro = a.elements[r].offset / 16;
        if (ro >= regs || ro == at) continue;
        auto nrm = [&](uint32_t v) { const float* q = &verts[v * vf + ro * 4]; return V3{q[0], q[1], q[2]}; };
        uint32_t unit = 0, seen = 0;
        const uint32_t step = std::max<uint32_t>(1, nv0 / 256);
        for (uint32_t v = 0; v < nv0; v += step, seen++) unit += std::fabs(Len(nrm(v)) - 1) < 0.06f;
        if (!seen || unit < seen * 95 / 100) continue;
        double agree = 0, total = 0;
        for (size_t t = 0; t < tris; t++) {
            uint32_t i0 = ix[t * 3], i1 = ix[t * 3 + 1], i2 = ix[t * 3 + 2];
            V3 f = Cross(place(i1) - place(i0), place(i2) - place(i0));
            float fl = Len(f);
            if (fl < 1e-12f) continue;
            agree += Dot(f * (1 / fl), Unit(nrm(i0) + nrm(i1) + nrm(i2))) * fl;
            total += fl;
        }
        if (total > 0 && std::fabs(agree) > 0.6 * total) normal_reg = int(r), facing = agree > 0 ? 1.0f : -1.0f;
    }
    // the side the camera sees it from is its outside: a wall's own normals pointed in (pushed out through the snow
    // laid over it). A surface seen edge on, or a closed one, takes its normals' or the ground's side; a wall neither
    // tells is left alone
    const bool forced = p.facing != 0;
    bool eye_said = false;
    if (forced) facing = p.facing > 0 ? 1.0f : -1.0f;
    else if (p.eye_ok) {
        const V3 eye{p.eye[0], p.eye[1], p.eye[2]};
        double side = 0, total = 0;
        for (size_t t = 0; t < tris; t++) {
            V3 a0 = place(ix[t * 3]), a1 = place(ix[t * 3 + 1]), a2 = place(ix[t * 3 + 2]);
            V3 f = Cross(a1 - a0, a2 - a0);
            float d = Dot(f, Unit(eye - (a0 + a1 + a2) * (1.0f / 3)));
            side += d, total += std::fabs(d);
        }
        if (total > 0 && std::fabs(side) > 0.7 * total) facing = side > 0 ? 1.0f : -1.0f, eye_said = true;
    }
    float ml = Len(mean);
    const bool mostly_level = ml > 0 && std::fabs(mean.y) >= 0.5f * ml;
    if (facing == 0 && mostly_level) facing = mean.y > 0 ? 1.0f : -1.0f;  // the ground faces up
    if (facing == 0) {
        if (out) out->retry = p.side_depth > 0;  // a wall: again when the camera sees it from one side
        return false;
    }
    const bool up = mostly_level && mean.y * facing > 0;
    // the ground alone (no side depth): a draw mostly facing up
    if (p.side_depth <= 0 && !up) return false;
    if (!up && !forced && !eye_said) {  // a wall whose outside the camera did not tell (seen edge on): later
        if (out) out->retry = true;
        return false;
    }

    // how many cuts: until the typical edge is about p.edge long
    std::vector<float> lens;
    const size_t tstep = std::max<size_t>(1, tris / 256);
    for (size_t t = 0; t < tris; t += tstep)
        for (int k = 0; k < 3; k++) lens.push_back(Len(place(ix[t * 3 + k]) - place(ix[t * 3 + (k + 1) % 3])));
    std::nth_element(lens.begin(), lens.begin() + lens.size() / 2, lens.end());
    const float typical = lens[lens.size() / 2];
    int levels = 0;
    while (levels < p.levels && typical / float(1 << levels) > p.edge * 1.2f) levels++;
    while (levels > 0 && (tris << (2 * levels)) > kMaxTriangles) levels--;

    // the mesh's own edges (an edge not two of its triangles have): never cut, so no new corner stands on the line it
    // shares with the draw beside it (a T junction there left a pixel thin crack along a wall's seams)
    std::unordered_map<uint64_t, uint32_t> edge_uses;
    auto wkey = [&](uint32_t a, uint32_t b) {
        uint32_t lo = std::min(wid[a], wid[b]), hi = std::max(wid[a], wid[b]);
        return uint64_t(lo) << 32 | hi;
    };
    for (size_t t = 0; t + 2 < ix.size(); t += 3)
        for (int k = 0; k < 3; k++) edge_uses[wkey(ix[t + k], ix[t + (k + 1) % 3])]++;
    auto outer = [&](uint32_t a, uint32_t b) {
        auto it = edge_uses.find(wkey(a, b));
        return it != edge_uses.end() && it->second != 2;
    };
    // cut each triangle in four, level by level; an edge's middle made once for both its triangles (no cracks). A
    // triangle on the mesh's edge is cut in three or two instead, its outer edge whole
    for (int lv = 0; lv < levels; lv++) {
        std::unordered_map<uint64_t, uint32_t> mids;
        std::vector<uint32_t> cut;
        cut.reserve(ix.size() * 4);
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
            uint32_t v[3] = {ix[t], ix[t + 1], ix[t + 2]};
            bool split[3];
            int n = 0;
            for (int k = 0; k < 3; k++) n += split[k] = !outer(v[k], v[(k + 1) % 3]);
            if (n == 3) {
                uint32_t m01 = mid(v[0], v[1]), m12 = mid(v[1], v[2]), m20 = mid(v[2], v[0]);
                cut.insert(cut.end(), {v[0], m01, m20, m01, v[1], m12, m20, m12, v[2], m01, m12, m20});
            } else if (n == 0) {
                cut.insert(cut.end(), {v[0], v[1], v[2]});
            } else {
                // turned (the winding kept) so that edge 0-1 is cut and, with two cut, edge 1-2 too
                int r = 0;
                for (int k = 0; k < 3; k++)
                    if (n == 1 ? split[k] : split[k] && split[(k + 1) % 3]) r = k;
                const uint32_t a0 = v[r], a1 = v[(r + 1) % 3], a2 = v[(r + 2) % 3];
                const uint32_t m01 = mid(a0, a1);
                if (n == 1) {
                    cut.insert(cut.end(), {a0, m01, a2, m01, a1, a2});
                } else {
                    const uint32_t m12 = mid(a1, a2);
                    cut.insert(cut.end(), {m01, a1, m12, a0, m01, m12, a0, m12, a2});
                }
            }
        }
        ix.swap(cut);
    }

    // each welded place's normal (the faces about it, by area); it stays where it is on the mesh's edge (an edge not
    // two triangles have) or a crease (a face about it turned well away from its normal), so the draw still meets its
    // neighbours and keeps its corners
    const size_t nw = wpos.size();
    auto normals = [&](const std::vector<V3>& pos, std::vector<V3>& wn) {
        wn.assign(nw, V3{0, 0, 0});
        for (size_t t = 0; t + 2 < ix.size(); t += 3) {
            uint32_t w0 = wid[ix[t]], w1 = wid[ix[t + 1]], w2 = wid[ix[t + 2]];
            V3 f = Cross(pos[w1] - pos[w0], pos[w2] - pos[w0]) * facing;
            wn[w0] = wn[w0] + f, wn[w1] = wn[w1] + f, wn[w2] = wn[w2] + f;
        }
        for (V3& n : wn) n = Unit(n);
    };
    std::vector<V3> wn;
    normals(wpos, wn);
    std::unordered_map<uint64_t, uint32_t> uses;
    std::vector<uint8_t> fixed(nw, 0), edge(nw, 0);
    for (size_t t = 0; t + 2 < ix.size(); t += 3) {
        uint32_t w[3] = {wid[ix[t]], wid[ix[t + 1]], wid[ix[t + 2]]};
        V3 f = Cross(wpos[w[1]] - wpos[w[0]], wpos[w[2]] - wpos[w[0]]) * facing;
        const bool flat = Len(f) < 1e-12f;
        f = Unit(f);
        for (int k = 0; k < 3; k++) {
            uint32_t lo = std::min(w[k], w[(k + 1) % 3]), hi = std::max(w[k], w[(k + 1) % 3]);
            uses[uint64_t(lo) << 32 | hi]++;
            if (!flat && Dot(f, wn[w[k]]) < kCrease) fixed[w[k]] = 1;
        }
    }
    for (const auto& [key, n] : uses)
        if (n != 2) fixed[uint32_t(key >> 32)] = fixed[uint32_t(key)] = edge[uint32_t(key >> 32)] = edge[uint32_t(key)] = 1;
    // a place beside a kept one pushed half as far: no sudden step down beside an edge
    std::vector<float> weight(nw, 1.0f);
    for (size_t t = 0; t + 2 < ix.size(); t += 3) {
        uint32_t w[3] = {wid[ix[t]], wid[ix[t + 1]], wid[ix[t + 2]]};
        if (fixed[w[0]] || fixed[w[1]] || fixed[w[2]])
            for (uint32_t k : w) weight[k] = std::min(weight[k], 0.5f);
    }

    // pushed in by the height (its highest stays on the surface the game collides with), projected along the
    // normal's main axis, so a place has one height whichever triangle it is in; two scales (the second turned and
    // larger) so the lumps do not repeat in rows
    const Heights& hs = HeightsOf(p.kind);
    auto height = [&](V3 q, V3 n) {
        float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
        float u, v;
        if (ay >= ax && ay >= az) u = q.x, v = q.z;
        else if (ax >= az) u = q.z, v = q.y;
        else u = q.x, v = q.y;
        float a1 = hs.At(u / p.tile, v / p.tile);
        float ru = (u * 0.8f - v * 0.6f) / (p.tile * 2.3f) + 0.37f, rv = (u * 0.6f + v * 0.8f) / (p.tile * 2.3f) + 0.71f;
        return a1 * 0.6f + hs.At(ru, rv) * 0.4f;
    };
    std::vector<V3> moved(wpos);
    std::vector<float> pushed(nw, 0.0f);  // 0 to 1: how deep in its hollow
    float most = 0;
    for (size_t w = 0; w < nw; w++) {
        if (fixed[w] || !p.push) continue;
        const V3 n = wn[w];
        // facing up: the ground's depth; sideways or down: the walls' (a blend between)
        const float t = std::clamp((n.y - 0.55f) / 0.3f, 0.0f, 1.0f);
        const float depth = p.depth * t + p.side_depth * (1 - t);
        if (depth <= 0) continue;
        const float h = height(wpos[w], n);
        const float d = (1 - h) * depth * weight[w];
        moved[w] = wpos[w] - n * d;
        pushed[w] = (1 - h) * weight[w];
        most = std::max(most, d);
    }
    if (most <= 0 && p.push) return false;

    // the new slopes' light: the baked light has none of it, so the colour is scaled by how much more or less the
    // surface faces a light from above and to the side than it did, and darkened a little in the hollows
    std::vector<V3> wn2;
    normals(moved, wn2);
    const uint32_t nv = uint32_t(verts.size() / vf);
    if (p.color_reg > 0 && p.color_reg < 16 && ((a.regs >> p.color_reg) & 1) && p.shade > 0) {
        const uint32_t co = a.elements[p.color_reg].offset / 16;
        const V3 L = Unit(V3{0.45f, 0.8f, 0.4f});
        std::vector<float> scale(nw, 1.0f);
        for (size_t w = 0; w < nw; w++) {
            if (edge[w]) continue;  // shaded as the draw beside it is (not at all): no line along the join
            float slope = (Dot(wn2[w], L) - Dot(wn[w], L)) * 1.6f;
            float hollow = -0.22f * pushed[w];
            scale[w] = std::clamp(1 + (slope + hollow) * p.shade, 0.55f, 1.45f);
        }
        if (co < regs && co != at)
            for (uint32_t v = 0; v < nv; v++) {
                float* c = &verts[v * vf + co * 4];
                const float k = scale[wid[v]];
                c[0] *= k, c[1] *= k, c[2] *= k;
            }
    }
    // the normals turned as the surface was (dynamic lights shade the lumps)
    if (normal_reg > 0) {
        const uint32_t no = a.elements[normal_reg].offset / 16;
        for (uint32_t v = 0; v < nv; v++) {
            float* q = &verts[v * vf + no * 4];
            const uint32_t w = wid[v];
            V3 n = Unit(V3{q[0], q[1], q[2]} + (wn2[w] - wn[w]));
            q[0] = n.x, q[1] = n.y, q[2] = n.z;
        }
    }
    for (uint32_t v = 0; v < nv; v++) std::memcpy(&verts[v * vf + at * 4], &moved[wid[v]], 12);

    a.vertices.swap(verts);
    a.indices.swap(ix);
    a.vertex_count = nv;
    a.buffers[0] = {reinterpret_cast<const uint8_t*>(a.vertices.data()), uint32_t(a.vertices.size() * 4), regs * 16};
    a.index_src = a.indices.data();
    a.index_count = uint32_t(a.indices.size());
    a.index16 = false;
    a.base_vertex = 0;
    if (out) out->levels = levels, out->normal_reg = normal_reg, out->up = up, out->facing = facing;
    return true;
}

}  // namespace gpu
