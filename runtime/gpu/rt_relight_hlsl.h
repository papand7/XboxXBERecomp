#pragma once
// The ray traced lighting passes (VulkanBackend::RtRelightPass, RtAccumulate): kRtCommonHlsl (rt_hlsl.h) comes before
// kRtRelightHlsl, after "#define RT_P 136".

namespace gpu {

// the level lit again (RtRelightPass; NightFire's kRtRelightHlsl, made relative to AUF's lightmaps). The baked light is
// the lightmap (stage 1, the static lamps' light and shadows already in it); a lightmapped pixel's colour is multiplied
// by (baked + added - taken) / baked, so nothing the lightmap darkened is darkened again. added: the game's dynamic
// lights (kind 1: muzzle flashes, explosions) with traced shadows (rt_lighting), the sun where a ray toward it is open
// (rt_sun), the bounced light (gi); taken (rt_shadows): a static lamp's light (kind 0, the lights the game sets for its
// models) where a ray toward it meets a D3D-lit model (a character, a prop the lightmap never saw) first. D3D-lit models
// and surfaces without a lightmap are left as the game lit them.
// b1: p[2] the lamps, test (XBE_RT_TEST=6 the change, halved; 7 the bounced light; 8 the air's light), static lamps'
// shadows on, dynamic lamps on; p[3] toward the sun (the traced space), its strength (0 none); p[4] its colour, the
// bounced light's strength; p[5] the air's strength, the lamps' scale, the march's far distance; p[6] 1 / the half
// images' size, bounce rays; p[8 + 2n] a lamp's place and reach (the traced space), p[9 + 2n] its colour and kind.
// t2 the bounced light (ps) or the air's light (volc), built up over frames. gi and vol run at half size, ps (the
// relight) and volc (the air over the scene) at the scene's.
inline const char* kRtRelightHlsl = R"(
Texture2D gitex : register(t2);
static const float kFar = 1e6;
// the first thing toward d within tmax that is a D3D-lit model (a character): its shadow on the lightmap
bool model_shadow(float3 o, float3 d, float tmax) {
    RayDesc r;
    r.Origin = o;
    r.Direction = d;
    r.TMin = 0;
    r.TMax = tmax;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, r);
    bool hit = false;
    while (q.Proceed()) {
        uint tri = rt_tri(q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex());
        if ((rt_mat(tri) >> 25) & 1) {
            hit = true;
            q.Abort();
        }
    }
    return hit;
}
// a lamp's light on a point (N 0: in the air)
float3 lampe(int n, float3 P, float3 N, out float3 dir, out float dist) {
    float4 a = p[8 + 2 * n];
    float3 D = a.xyz - P;
    float d2 = dot(D, D), r2 = a.w * a.w;
    dir = 0, dist = 0;
    if (d2 >= r2) return 0;
    dist = sqrt(d2);
    dir = D / max(dist, 1e-4);
    float ndl = dot(N, N) == 0 ? 1 : dot(N, dir);
    if (ndl <= 0) return 0;
    float att = 1 - dist / a.w;
    return p[9 + 2 * n].rgb * (att * att * ndl * p[5].y);
}
void lamps(float3 P, float3 N, float3 o, bool shadows, out float3 added, out float3 taken) {
    added = 0, taken = 0;
    [loop] for (int n = 0; n < int(p[2].x); n++) {
        float3 dir;
        float dist;
        float3 e = lampe(n, P, N, dir, dist);
        if (dot(e, float3(0.3, 0.59, 0.11)) < 0.004) continue;
        if (p[9 + 2 * n].w > 0.5) {
            if (p[2].w != 0) added += shadows ? e * open(o, dir, dist * 0.95) : e;
        } else if (shadows && p[2].z != 0 && model_shadow(o, dir, dist * 0.95)) {
            taken += e;
        }
    }
}
// the light the lightmap gives a point of a triangle (0: none), and its colour
float3 baked_at(uint tri, float2 bary, out float3 albedo, out float fog) {
    float2 uv0, uv1;
    float4 col;
    rt_attr(tri, bary, uv0, uv1, col, fog);
    uint m0 = rt_mat(tri), m1 = rt_mat2(tri);
    albedo = rt_texel(m0, uv0).rgb;
    if ((m0 >> 24) & 1) albedo *= col.rgb;
    if ((m1 & 0xFFF) == 0xFFF) return 0;
    return rt_texel(m1, ((m1 >> 12) & 1) ? uv1 : uv0).rgb * float(1u << ((m1 >> 13) & 3));
}
float3 sunlight(float3 o, float3 N) {
    if (p[3].w <= 0) return 0;
    float sl = saturate(dot(N, p[3].xyz));
    if (sl <= 0) return 0;
    return p[4].rgb * (p[3].w * sl) * open(o, p[3].xyz, kFar);
}
float4 gi(O i) : SV_Target {
    float3 P;
    uint tri;
    float2 bary;
    if (!rt_primary(i.uv, 0.05, P, tri, bary)) return 0;
    float3 N = rt_face(tri);
    if (dot(N, P) > 0) N = -N;
    N = rt_smooth(tri, bary, N);
    float3 o = rt_off(P, N);
    int rays = int(p[6].z);
    float3 sum = 0;
    [loop] for (int k = 0; k < rays; k++) {
        RayDesc r;
        r.Origin = o;
        r.Direction = rt_cosine(N, uint2(i.pos.xy), k, rays);
        r.TMin = 0;
        r.TMax = metres(40);
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, r);
        RT_PROCEED(q)
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) continue;
        uint ht = rt_tri(q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex());
        float3 alb;
        float fog;
        float3 hb = baked_at(ht, q.CommittedTriangleBarycentrics(), alb, fog);
        float3 n = rt_face(ht);
        if (dot(n, r.Direction) > 0) n = -n;
        float3 H = o + r.Direction * q.CommittedRayT();
        float3 la, lt;
        lamps(H, n, H, false, la, lt);
        float3 light = (dot(hb, 1) > 0 ? hb : 0.5) + sunlight(rt_off(H, n), n) + la;
        sum += saturate(alb) * light;
    }
    return float4(sum / max(rays, 1) * p[4].w, P.z);
}
float marched(float2 uv) {
    float d = depthat(uv);
    float z = sky(d) ? p[5].z : min(viewz(d), p[5].z);
    return length(viewpos(uv, z));
}
float4 vol(O i) : SV_Target {
    float tmax = marched(i.uv);
    float3 dir = normalize(viewpos(i.uv, 1));
    uint2 ip = uint2(i.pos.xy);
    float j = (bayer[(ip.y & 3) * 4 + (ip.x & 3)] + 0.5) / 16.0;
    const int steps = 12;
    float dt = tmax / steps, sigma = 0.004 / metres(1) * p[5].x;
    float g = 0.3, cs = dot(dir, p[3].xyz);
    float phase = (1 - g * g) / pow(1 + g * g - 2 * g * cs, 1.5);
    float3 sunl = p[4].rgb * (p[3].w * phase);
    float3 sum = 0;
    [loop] for (int k = 0; k < steps; k++) {
        float t = (k + j) * dt;
        float3 X = dir * t;
        float w = exp(-sigma * 0.5 * t) * sigma * dt;
        if (p[3].w > 0) sum += sunl * w * open(X, p[3].xyz, kFar);
        [loop] for (int n = 0; n < int(p[2].x); n++) {
            float3 ld;
            float dist;
            float3 e = lampe(n, X, float3(0, 0, 0), ld, dist);
            if (dot(e, float3(0.3, 0.59, 0.11)) < 0.01) continue;
            sum += e * 8 * w * open(X, ld, dist * 0.95);
        }
    }
    return float4(sum, tmax);
}
float4 volc(O i) : SV_Target {
    float2 uv = i.uv;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    if (aftertheclear(uv)) return float4(c, 1);
    float tmax = marched(uv);
    float3 b = 0;
    float wsum = 0;
    [unroll] for (int y = -2; y < 2; y++) {
        [unroll] for (int x = -2; x < 2; x++) {
            float4 g = gitex.SampleLevel(pt, uv + (float2(x, y) + 0.5) * p[6].xy, 0);
            float w = saturate(1 - abs(g.a - tmax) / (tmax * 0.06 + metres(0.05)));
            b += g.rgb * w;
            wsum += w;
        }
    }
    b = wsum > 0 ? b / wsum : gitex.SampleLevel(pt, uv, 0).rgb;
    if (int(p[2].y) == 8) return float4(saturate(b * 2), 1);
    float T = exp(-0.004 / metres(1) * p[5].x * 0.5 * tmax);
    return float4(c * lerp(1, T, 0.5) + b, 1);
}
float4 ps(O i) : SV_Target {
    float2 uv = i.uv;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    int test = int(p[2].y);
    float3 P;
    uint tri;
    float2 bary;
    if (!rt_primary(uv, 0.05, P, tri, bary)) return float4(test == 6 ? c * 0.25 : c, 1);
    float3 albedo;
    float f;
    float3 baked = baked_at(tri, bary, albedo, f);
    if (dot(baked, 1) <= 0) return float4(test == 6 ? c * 0.25 : c, 1);  // D3D lit: as the game lit it
    float3 N = rt_face(tri);
    if (dot(N, P) > 0) N = -N;
    N = rt_smooth(tri, bary, N);
    float3 o = rt_off(P, N);
    float3 added, taken;
    lamps(P, N, o, true, added, taken);
    added += sunlight(o, N);
    if (p[4].w > 0) {
        float3 b = 0;
        float wsum = 0;
        [unroll] for (int y = -2; y < 2; y++) {
            [unroll] for (int x = -2; x < 2; x++) {
                float4 g = gitex.SampleLevel(pt, uv + (float2(x, y) + 0.5) * p[6].xy, 0);
                float w = g.a > 0 ? saturate(1 - abs(g.a - P.z) / (P.z * 0.04 + metres(0.02))) : 0;
                b += g.rgb * w;
                wsum += w;
            }
        }
        b = wsum > 0 ? b / wsum : 0;
        if (test == 7) return float4(saturate(b), 1);
        added += b;
    }
    float3 bk = max(baked, 0.04);
    float3 ratio = clamp(1 + (added - min(taken, baked * 0.85)) / bk, 0.15, 3);
    if (test == 6) return float4(ratio * 0.5, 1);
    if (test == 9) return float4(saturate(luma(added)), saturate(luma(taken)), saturate(baked.g), 1);
    if (f < 0.03) return float4(c, 1);
    return float4(c * ratio, 1);
}
)";

// the bounced and volumetric light built up over frames (RtAccumulate, NightFire's kTemporalHlsl acc): a half-size
// pixel's place (its distance in a: the view's z, or with p[1].z the ray's length) put through the last frame's camera
// (p[2..4]: the traced space now to last frame's) gives where it was; what was there is kept (p[1].y of the new each
// frame) when its distance there agrees. p[1].x: a last frame is there. t0 this frame's, t1 the built up.
inline const char* kRtAccHlsl = R"(
cbuffer P : register(b1) { float4 p[5]; };
Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float3 viewpos(float2 uv, float z) { return float3((uv * 2 - 1) * float2(p[0].w, -1) * p[0].z * z, z); }
float2 screenuv(float3 v) { return v.xy / (v.z * p[0].z * float2(p[0].w, -1)) * 0.5 + 0.5; }
float4 acc(O i) : SV_Target {
    float4 c = t0.SampleLevel(pt, i.uv, 0);
    if (p[1].x == 0 || c.a <= 0) return c;
    float3 v = p[1].z != 0 ? normalize(viewpos(i.uv, 1)) * c.a : viewpos(i.uv, c.a);
    float3 l = float3(dot(p[2].xyz, v) + p[2].w, dot(p[3].xyz, v) + p[3].w, dot(p[4].xyz, v) + p[4].w);
    if (l.z <= 0.05) return c;
    float2 luv = screenuv(l);
    if (any(luv < 0) || any(luv > 1)) return c;
    float4 h = t1.SampleLevel(pt, luv, 0);
    float want = p[1].z != 0 ? length(l) : l.z;
    if (h.a <= 0 || abs(h.a - want) > want * 0.04 + 2) return c;
    return float4(lerp(h.rgb, c.rgb, p[1].y), c.a);
}
)";

}  // namespace gpu
