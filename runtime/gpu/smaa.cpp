// SMAA 1x (Jimenez et al., runtime/gpu/smaa) on the finished frame: its three passes and lookup textures for both renderers
#include "gpu/smaa.h"

#include "gpu/smaa/AreaTex.h"
#include "gpu/smaa/SearchTex.h"

namespace gpu {

const char* const kSmaaHlsl =
R"(#define SMAA_CUSTOM_SL 1
#define SMAA_PRESET_HIGH 1
cbuffer B : register(b1) { float4 uvrect; float4 smaa_rt; };
#define SMAA_RT_METRICS smaa_rt
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
SamplerState smaa_linear : register(s0);
SamplerState smaa_point : register(s1);
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(smaa_linear, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(smaa_point, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(smaa_linear, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(smaa_linear, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(smaa_point, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(smaa_linear, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
)"
#include "gpu/smaa/SMAA.hlsl.inc"
R"(
struct SmaaV { float4 p : SV_Position; float2 uv : TEXCOORD0; float2 pix : TEXCOORD1; float4 o0 : TEXCOORD2; float4 o1 : TEXCOORD3; float4 o2 : TEXCOORD4; };
SmaaV SmaaBase(uint id) {
    SmaaV o = (SmaaV)0;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
SmaaV vsedge(uint id : SV_VertexID) {
    SmaaV o = SmaaBase(id);
    float4 f[3];
    SMAAEdgeDetectionVS(o.uv, f);
    o.o0 = f[0], o.o1 = f[1], o.o2 = f[2];
    return o;
}
SmaaV vsweight(uint id : SV_VertexID) {
    SmaaV o = SmaaBase(id);
    float4 f[3];
    SMAABlendingWeightCalculationVS(o.uv, o.pix, f);
    o.o0 = f[0], o.o1 = f[1], o.o2 = f[2];
    return o;
}
SmaaV vsblend(uint id : SV_VertexID) {
    SmaaV o = SmaaBase(id);
    SMAANeighborhoodBlendingVS(o.uv, o.o0);
    return o;
}
float4 psedge(SmaaV i) : SV_Target {
    float4 f[3] = {i.o0, i.o1, i.o2};
    return float4(SMAALumaEdgeDetectionPS(i.uv, f, tex0), 0, 0);
}
float4 psweight(SmaaV i) : SV_Target {
    float4 f[3] = {i.o0, i.o1, i.o2};
    return SMAABlendingWeightCalculationPS(i.uv, i.pix, f, tex0, tex1, tex2, float4(0, 0, 0, 0));
}
float4 psblend(SmaaV i) : SV_Target { return SMAANeighborhoodBlendingPS(i.uv, i.o0, tex0, tex1); }
)";

const unsigned char* const kSmaaArea = areaTexBytes;
const unsigned char* const kSmaaSearch = searchTexBytes;

// the Vulkan renderer's form (smaa.h): NightFire's bindings over the same vendored SMAA.hlsl
namespace {

const char* kHead = R"(
cbuffer P : register(b1) { float4 metrics; };
#define SMAA_RT_METRICS metrics
#define SMAA_CUSTOM_SL 1
#define SMAA_PRESET_ULTRA 1
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAATexture2DMS2(tex) Texture2DMS<float4, 2> tex
#define SMAALoad(tex, pos, sample) tex.Load(pos, sample)
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)
)";

const char* kSmaaSource =
#include "gpu/smaa/SMAA.hlsl.inc"
    ;

const char* kTail = R"(
Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Texture2D t2 : register(t2);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
// no edge is discarded: the edges image is cleared first
float4 edges(O i) : SV_Target {
    float4 off[3];
    SMAAEdgeDetectionVS(i.uv, off);
    return float4(SMAALumaEdgeDetectionPS(i.uv, off, t0), 0, 0);
}
float4 weights(O i) : SV_Target {
    float2 pix;
    float4 off[3];
    SMAABlendingWeightCalculationVS(i.uv, pix, off);
    return SMAABlendingWeightCalculationPS(i.uv, pix, off, t0, t1, t2, 0);
}
float4 blend(O i) : SV_Target {
    float4 off;
    SMAANeighborhoodBlendingVS(i.uv, off);
    return float4(SMAANeighborhoodBlendingPS(i.uv, off, t0, t1).rgb, 1);
}
)";

}  // namespace

const std::string& SmaaHlsl() {
    static const std::string s = [] {
        std::string v = std::string(kHead) + kSmaaSource + kTail;
        for (char& c : v)
            if (static_cast<unsigned char>(c) >= 0x80) c = ' ';
        return v;
    }();
    return s;
}

const uint8_t* SmaaAreaTex(uint32_t& w, uint32_t& h, uint32_t& pitch) {
    w = AREATEX_WIDTH, h = AREATEX_HEIGHT, pitch = AREATEX_PITCH;
    return areaTexBytes;
}

const uint8_t* SmaaSearchTex(uint32_t& w, uint32_t& h, uint32_t& pitch) {
    w = SEARCHTEX_WIDTH, h = SEARCHTEX_HEIGHT, pitch = SEARCHTEX_PITCH;
    return searchTexBytes;
}

}  // namespace gpu
