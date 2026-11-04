// SMAA's shader and lookup textures (smaa.h)
#include "gpu/smaa.h"

#include "AreaTex.h"
#include "SearchTex.h"

namespace gpu {

namespace {

// SMAA_CUSTOM_SL: the HLSL 4.1 porting macros, with samplers this renderer binds (s0 linear, s1 point, both clamp)
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

// (its comments name the authors in Latin-1: those bytes become spaces in SmaaHlsl)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-source-encoding"
const char* kSmaaSource =
#include "smaa_hlsl.inc"
    ;
#pragma clang diagnostic pop

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
