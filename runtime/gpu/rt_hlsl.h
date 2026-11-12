#pragma once
// The Vulkan renderer's ray traced passes (VulkanBackend::RtScenePass, ported from NightFire's): HLSL, shader model
// 6.5, each a full screen triangle with its constants at b1, run over the finished 3D scene before the HUD.

namespace gpu {

// What every ray traced pass reads (prefixed to each, after "#define RT_P n", the float4s at b1). The frame's
// acceleration structure is the scene's triangles as the vertex shaders placed them (RtCapture: 32 bytes a vertex: its
// place on the screen times its distance w, then w, the fog's share; texture coordinates 0, the diffuse colour as
// RGBA8, texture coordinates 1 as two halves), one instance scaled by the view's half widths so it is the view space
// (x right, y up, z ahead, game units); triangles (the opaque ones, then the cut-outs: the second geometry), two
// material words a triangle (RtMaterial) after the two counts, each vertex's triangles (rt_adj) and the textures the
// materials name. b1 p[0]: 1/width, 1/height, tan of half the vertical view, horizontal over vertical tan; p[1]: the
// depth to distance (w = y / (d - x)), the depth kept at the weapon's clear is there (z), game units a metre (w).
// t0 the scene, t1 its depth, t3 the depth kept at the weapon's clear (RtKeepDepth); s0 linear, s1 point.
// RT_PROCEED(q) runs a query, a cut-out's candidate hit kept where its texture's alpha passes its alpha test.
inline const char* kRtCommonHlsl = R"(
cbuffer P : register(b1) { float4 p[RT_P]; };
[[vk::binding(11, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(12, 0)]] ByteAddressBuffer rt_verts;
[[vk::binding(13, 0)]] ByteAddressBuffer rt_tris;
[[vk::binding(14, 0)]] ByteAddressBuffer rt_mats;
[[vk::binding(16, 0)]] ByteAddressBuffer rt_adj;
[[vk::binding(0, 1)]] Texture2D rt_tex[1024];
[[vk::binding(1, 1)]] SamplerState rt_smp;
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D<float> keptz : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
static const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float metres(float m) { return m * p[1].w; }
// a depth's distance (w, the view's z): the camera's projection gives d = x + y / w
float viewz(float d) {
    float den = d - p[1].x;
    return p[1].y / (abs(den) < 1e-9 ? 1e-9 : den);
}
// the scene's depth: the nearer of the end's and the one kept at the weapon's clear
float depthat(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[1].z != 0 ? min(d, keptz.SampleLevel(pt, uv, 0)) : d;
}
// drawn after that clear: the weapon and hands, left as they are
bool aftertheclear(float2 uv) {
    return p[1].z != 0 && depthtex.SampleLevel(pt, uv, 0) < keptz.SampleLevel(pt, uv, 0) - 1e-6;
}
bool sky(float d) { return d >= 0.99999; }
float3 viewpos(float2 uv, float z) { return float3((uv * 2 - 1) * float2(p[0].w, -1) * p[0].z * z, z); }
float2 screenuv(float3 v) { return v.xy / (v.z * p[0].z * float2(p[0].w, -1)) * 0.5 + 0.5; }
float3 rt_scale() { return float3(p[0].z * p[0].w, p[0].z, 1); }  // the instance's (RtBuild)
uint rt_tri(uint geom, uint prim) { return prim + (geom != 0 ? rt_mats.Load(0) : 0); }
uint rt_mat(uint tri) { return rt_mats.Load(8 + tri * 8); }
uint rt_mat2(uint tri) { return rt_mats.Load(12 + tri * 8); }
float3 rt_place(uint v) { return asfloat(rt_verts.Load3(v * 32)) * rt_scale(); }
float4 rt_unpack(uint c) { return float4(c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24) / 255.0; }
// a point of a triangle: texture coordinates 0 and 1, diffuse colour, the fog's share
void rt_attr(uint tri, float2 bary, out float2 uv0, out float2 uv1, out float4 col, out float fog) {
    uint3 t = rt_tris.Load3(tri * 12);
    uint4 a = rt_verts.Load4(t.x * 32 + 16), b = rt_verts.Load4(t.y * 32 + 16), c = rt_verts.Load4(t.z * 32 + 16);
    float w0 = 1 - bary.x - bary.y;
    float3 w = float3(w0, bary.x, bary.y);
    uv0 = asfloat(a.xy) * w.x + asfloat(b.xy) * w.y + asfloat(c.xy) * w.z;
    uv1 = f16tof32(uint2(a.w, a.w >> 16)) * w.x + f16tof32(uint2(b.w, b.w >> 16)) * w.y + f16tof32(uint2(c.w, c.w >> 16)) * w.z;
    col = rt_unpack(a.z) * w.x + rt_unpack(b.z) * w.y + rt_unpack(c.z) * w.z;
    fog = asfloat(rt_verts.Load(t.x * 32 + 12)) * w.x + asfloat(rt_verts.Load(t.y * 32 + 12)) * w.y +
          asfloat(rt_verts.Load(t.z * 32 + 12)) * w.z;
}
// a material's texture there (white without one)
float4 rt_texel(uint slot, float2 uv) {
    slot &= 0xFFF;
    return slot == 0xFFF ? 1 : rt_tex[NonUniformResourceIndex(slot)].SampleLevel(rt_smp, uv, 0);
}
// a triangle's flat normal (unit, either way)
float3 rt_face(uint tri) {
    uint3 t = rt_tris.Load3(tri * 12);
    float3 v0 = rt_place(t.x), n = cross(rt_place(t.y) - v0, rt_place(t.z) - v0);
    return dot(n, n) > 1e-20 ? normalize(n) : float3(0, 0, -1);
}
// a vertex's normal for a triangle there whose own normal is nf: the triangles at the vertex facing within about 50
// degrees of it, by their area (a box keeps its edges, a low-poly curve shades round)
float3 rt_vertex_normal(uint v, float3 nf) {
    uint V = rt_adj.Load(0);
    if (v >= V) return nf;
    uint first = rt_adj.Load(4 + v * 4), last = min(rt_adj.Load(8 + v * 4), first + 24);
    float3 sum = 0;
    [loop] for (uint e = first; e < last; e++) {
        uint t = rt_adj.Load(8 + V * 4 + e * 4);
        uint3 i = rt_tris.Load3(t * 12);
        float3 a = rt_place(i.x), n = cross(rt_place(i.y) - a, rt_place(i.z) - a);
        float l = length(n);
        if (l < 1e-12) continue;
        if (dot(n / l, nf) < 0) n = -n;
        if (dot(n / l, nf) > 0.64) sum += n;
    }
    return dot(sum, sum) > 1e-20 ? normalize(sum) : nf;
}
// the smooth normal at a point of a triangle; nf its flat one, facing the viewer
float3 rt_smooth(uint tri, float2 bary, float3 nf) {
    uint3 t = rt_tris.Load3(tri * 12);
    float w0 = 1 - bary.x - bary.y;
    float3 n = rt_vertex_normal(t.x, nf) * w0 + rt_vertex_normal(t.y, nf) * bary.x + rt_vertex_normal(t.z, nf) * bary.y;
    n = dot(n, n) > 1e-20 ? normalize(n) : nf;
    return dot(n, nf) > 0 ? n : nf;
}
// a cut-out's alpha test (D3D's comparisons, Cmp() less 1) on its texture's alpha
bool rt_solid(uint geom, uint prim, float2 bary) {
    uint tri = rt_tri(geom, prim), mat = rt_mat(tri);
    float2 uv0, uv1;
    float4 col;
    float fog;
    rt_attr(tri, bary, uv0, uv1, col, fog);
    float a = floor(rt_texel(mat, uv0).a * 255 + 0.5), ref = float((mat >> 12) & 255);
    switch ((mat >> 20) & 7) {
    case 0: return false;
    case 1: return a < ref;
    case 2: return a == ref;
    case 3: return a <= ref;
    case 4: return a > ref;
    case 5: return a != ref;
    case 6: return a >= ref;
    default: return true;
    }
}
#define RT_PROCEED(q) \
    while (q.Proceed()) \
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && \
            rt_solid(q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics())) \
            q.CommitNonOpaqueTriangleHit();
// open toward d as far as tmax: nothing in the way
float open(float3 o, float3 d, float tmax) {
    RayDesc r;
    r.Origin = o;
    r.Direction = d;
    r.TMin = 0;
    r.TMax = tmax;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, r);
    RT_PROCEED(q)
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
}
// the pixel's own triangle, where a ray from the camera meets it about the depth's place (within +-k of its
// distance): its exact place, the triangle and where in it. False where nothing traced is there (the sky, the weapon,
// a draw left out: blended glass, smoke)
bool rt_primary(float2 uv, float k, out float3 P, out uint tri, out float2 bary) {
    P = 0, tri = 0, bary = 0;
    float d = depthat(uv);
    if (sky(d) || aftertheclear(uv)) return false;
    P = viewpos(uv, viewz(d));
    float lp = length(P);
    RayDesc pr;
    pr.Origin = 0;
    pr.Direction = P / lp;
    pr.TMin = lp * (1 - k);
    pr.TMax = lp * (1 + k);
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, pr);
    RT_PROCEED(q)
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
    tri = rt_tri(q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex());
    bary = q.CommittedTriangleBarycentrics();
    P = pr.Direction * q.CommittedRayT();
    return true;
}
// a little off a surface, along its normal (against self hits)
float3 rt_off(float3 P, float3 N) { return P + N * (metres(0.01) + 0.002 * P.z); }
// cosine weighted directions about N: two 4x4 patterns, so any 4x4 pixels hold the same set of directions
float3 rt_cosine(float3 N, uint2 ip, int k, int rays) {
    float3 t = normalize(cross(abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), N));
    float3 bt = cross(N, t);
    float a0 = (bayer[(ip.y & 3) * 4 + (ip.x & 3)] + 0.5) / 16.0;
    float b0 = (bayer[((ip.x + 2) & 3) * 4 + ((ip.y + 1) & 3)] + 0.5) / 16.0;
    float phi = 6.2831853 * frac(a0 + k * 0.618034);
    float r2 = frac(b0 + float(k) / rays);
    float sr = sqrt(r2);
    return t * (cos(phi) * sr) + bt * (sin(phi) * sr) + N * sqrt(1 - r2);
}
)";

// ray traced ambient occlusion (RtAoPass). trace, at half the scene's size: a pixel's rays (cosine weighted about its
// triangle's normal) against the frame's structure, r what is open, g the distance; comp at the scene's size: the 4x4
// traced pixels about it weighed by how near their distance is, the scene darkened by it. p[2]: reach (game units),
// strength, rays, test (XBE_RT_TEST=1 the left half without, 2 the occlusion alone, 3 a ray from the camera: green
// where it meets the depth, red nearer, blue farther, black none); p[3]: 1 / the traced size. t2 the traced image.
inline const char* kRtAoHlsl = R"(
Texture2D aotex : register(t2);
float4 trace(O i) : SV_Target {
    float2 uv = i.uv;
    float d = depthat(uv);
    if (sky(d) || aftertheclear(uv)) return float4(1, 0, 0, 1);
    int test = int(p[2].w);
    if (test == 3) {
        float3 P0 = viewpos(uv, viewz(d));
        RayDesc pr;
        pr.Origin = 0;
        pr.Direction = normalize(P0);
        pr.TMin = 0;
        pr.TMax = 1e7;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q0;
        q0.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, pr);
        RT_PROCEED(q0)
        if (q0.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return float4(0, 0, 0, 1);
        float e = (q0.CommittedRayT() - length(P0)) / length(P0);
        return float4(saturate(-e * 20), saturate(1 - abs(e) * 20), saturate(e * 20), 1);
    }
    float3 P;
    uint tri;
    float2 bary;
    if (!rt_primary(uv, 0.1, P, tri, bary)) return float4(1, 0, 0, 1);
    float3 n = rt_face(tri);
    if (dot(n, P) > 0) n = -n;  // toward the camera, at the origin
    float3 o = rt_off(P, n);
    int rays = int(p[2].z);
    float reach = p[2].x, occ = 0;
    [loop] for (int k = 0; k < rays; k++) {
        RayDesc ray;
        ray.Origin = o;
        ray.Direction = rt_cosine(n, uint2(i.pos.xy), k, rays);
        ray.TMin = 0;
        ray.TMax = reach;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
        RT_PROCEED(q)
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            float f = q.CommittedRayT() / reach;
            occ += 1 - f * f;
        }
    }
    return float4(1 - occ / max(rays, 1), P.z, 0, 1);
}
float4 comp(O i) : SV_Target {
    float2 uv = i.uv;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    int test = int(p[2].w);
    float d = depthat(uv);
    if (sky(d) || aftertheclear(uv)) return test >= 2 ? float4(1, 1, 1, 1) : float4(c, 1);
    float z = viewz(d), sum = 0, wsum = 0;
    [unroll] for (int y = -2; y < 2; y++) {
        [unroll] for (int x = -2; x < 2; x++) {
            float4 a = aotex.SampleLevel(pt, uv + (float2(x, y) + 0.5) * p[3].xy, 0);
            float w = a.g > 0 ? saturate(1 - abs(a.g - z) / (z * 0.04 + metres(0.02))) : 0;
            sum += a.r * w;
            wsum += w;
        }
    }
    float ao = wsum > 0 ? sum / wsum : 1;
    ao = pow(saturate(ao), 2 * p[2].y);  // reads at a glance: half open at strength 1 is a quarter
    if (test == 3) return aotex.SampleLevel(pt, uv, 0);
    if (test == 2) return float4(ao.xxx, 1);
    if (test == 1 && uv.x < 0.5) return float4(c, 1);
    return float4(c * ao, 1);
}
)";

}  // namespace gpu
