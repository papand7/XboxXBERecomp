#pragma once
// The Vulkan renderer's own screen passes (shader styles, weather ...): HLSL for Present's post chain. Each pass is a
// full screen triangle with its constants at b1; see VulkanBackend::StylePass and friends.

namespace gpu {

// Shader styles on the finished 3D scene. t0 the scene so far, t1 the depth, t2 the scene before the HUD was drawn
// and t3 the frame as drawn (where those two differ the HUD is, and it is left as it is).
// p[0] 1/w 1/h w h; p[1] wbuffer style tonemap clarity; p[2] bands outline(px) inner threshold;
// p[3] halftone pixel bits dither; p[4] tint hasdepth hasmask tan(fov/2); p[5] aspect frame 0 0
inline const char* kStyleHlsl = R"(
cbuffer P : register(b1) { float4 p[6]; };
Texture2D src : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D prehud : register(t2);
Texture2D frame : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 q = float2((id << 1) & 2, id & 2);
    o.pos = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = q; return o;
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float zat(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[1].x > 0.5 ? d * 4096.0 : rcp(max(1.0 - d, 1e-5));
}
float3 posat(float2 uv) {
    float z = zat(uv);
    return float3((uv * 2 - 1) * float2(p[5].x, -1) * p[4].w * z, z);
}
float3 normat(float2 uv, float2 e) {
    float3 dx = posat(uv + float2(e.x, 0)) - posat(uv - float2(e.x, 0));
    float3 dy = posat(uv + float2(0, e.y)) - posat(uv - float2(0, e.y));
    return normalize(cross(dx, dy) + 1e-9);
}
float3 tone(float3 c, int mode) {
    if (mode == 0) return c;
    float3 l = pow(max(c, 0), 2.2);
    if (mode == 1) {
        l *= 1.15;
        l = saturate((l * (2.51 * l + 0.03)) / (l * (2.43 * l + 0.59) + 0.14));
        l = lerp(pow(max(c, 0), 2.2), l, 0.85);
    } else {
        l = l * (1 + l / 6.0) / (1 + l);
        l = saturate(l * 1.12);
    }
    return pow(max(l, 0), 1 / 2.2);
}
float bayer(int2 q) {
    int x = q.x & 3, y = q.y & 3;
    int m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (m[y * 4 + x] + 0.5) / 16.0;
}
float4 style_ps(O i) : SV_Target {
    int style = (int)p[1].y;
    float2 uv = i.uv;
    float2 t = p[0].xy;
    float4 orig = src.SampleLevel(pt, uv, 0);
    if (p[4].z > 0.5) {
        float3 pre = prehud.SampleLevel(pt, uv, 0).rgb, raw = frame.SampleLevel(pt, uv, 0).rgb;
        float3 df = abs(pre - raw);
        if (max(df.r, max(df.g, df.b)) > 0.006) return orig;
    }
    float2 cell = floor(i.pos.xy);
    float pixel = max(p[3].y, 1);
    if (style == 3 && pixel > 1.5) {
        cell = floor(i.pos.xy / pixel);
        uv = (cell * pixel + pixel * 0.5) * t;
    }
    float3 c = style == 3 && pixel > 1.5 ? src.SampleLevel(pt, uv, 0).rgb : orig.rgb;
    bool hasdepth = p[4].y > 0.5;
    bool sky = hasdepth && depthtex.SampleLevel(pt, uv, 0) >= 0.99999;
    c = tone(c, (int)p[1].z);
    if (style == 1) {
        float3 b = 0;
        [unroll] for (int k = 0; k < 8; k++) {
            float a = k * 0.785398;
            b += src.SampleLevel(ls, uv + float2(cos(a), sin(a)) * t * 3.0, 0).rgb;
        }
        b = (b + c * 2) / 10;
        c = saturate(c + (c - b) * p[1].w * 2.0);
        c = lerp(luma(c).xxx, c, 1.08);
    } else if (style == 2) {
        float y = luma(c);
        float th = max(p[2].y, 0.0) * 0.1 * p[0].w / 960.0;  // outline thickness in pixels
        float edge = 0;
        float thr = p[2].w;
        if (th > 0.01) {
            float2 e = t * th;
            if (hasdepth && !sky) {
                float z = zat(uv);
                float zl = zat(uv - float2(e.x, 0)), zr = zat(uv + float2(e.x, 0));
                float zu = zat(uv - float2(0, e.y)), zd = zat(uv + float2(0, e.y));
                float lap = max(abs(zl + zr - 2 * z), abs(zu + zd - 2 * z)) / max(z, 0.05);
                edge = max(edge, saturate((lap - thr * 0.06) / (thr * 0.06 + 1e-4)));
                if (p[2].z > 0.5) {
                    float3 nl = normat(uv - float2(e.x, 0), e), nr = normat(uv + float2(e.x, 0), e);
                    float3 nu = normat(uv - float2(0, e.y), e), nd = normat(uv + float2(0, e.y), e);
                    float crease = max(1 - dot(nl, nr), 1 - dot(nu, nd));
                    edge = max(edge, saturate((crease - thr * 0.5) / (thr * 0.5 + 1e-4)));
                }
            }
            if (p[2].z > 0.5 || !hasdepth) {
                float yl = luma(src.SampleLevel(pt, uv - float2(e.x, 0), 0).rgb), yr = luma(src.SampleLevel(pt, uv + float2(e.x, 0), 0).rgb);
                float yu = luma(src.SampleLevel(pt, uv - float2(0, e.y), 0).rgb), yd = luma(src.SampleLevel(pt, uv + float2(0, e.y), 0).rgb);
                float g = max(abs(yl - yr), abs(yu - yd));
                edge = max(edge, saturate((g - thr * 0.6) / 0.15) * 0.8);
            }
        }
        float bands = max(p[2].x, 2);
        float yq = (min(floor(saturate(y) * bands), bands - 1) + 0.5) / bands;
        c = saturate(c * (yq / max(y, 0.04)));
        float ht = p[3].x;
        if (ht > 0.001) {
            float2 q = mul(float2x2(0.7071, -0.7071, 0.7071, 0.7071), frac(i.pos.xy / 6.0) - 0.5);
            float d = length(q);
            float dots = step(d, (1 - saturate(y * 1.3)) * 0.55);
            c = lerp(c, c * 0.45, dots * ht);
        }
        c = lerp(c, float3(0.02, 0.025, 0.035), edge * 0.9);
    } else if (style == 3) {
        float tint = p[4].x;
        float3 g = c * float3(1.07, 1.0, 0.86) + float3(0.03, 0.015, 0.0);
        g = lerp(luma(g).xxx, g, 0.8);
        g = saturate((g - 0.5) * 1.12 + 0.5);
        c = lerp(c, g, tint);
        float levels = exp2(p[3].z) - 1;
        float dith = p[3].w > 0.5 ? bayer(int2(cell)) : 0.5;
        c = floor(saturate(c) * levels + dith) / levels;
    }
    return float4(saturate(c), orig.a);
}
)";

}  // namespace gpu
