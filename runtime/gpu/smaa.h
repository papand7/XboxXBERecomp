#pragma once
#include <cstdint>
#include <string>

namespace gpu {

// SMAA on the finished frame (smaa.cpp): entry points vsedge/psedge (t0 the frame), vsweight/psweight (t0 edges, t1 area,
// t2 search), vsblend/psblend (t0 the frame, t1 weights); b1 uvrect (unused), then 1/w, 1/h, w, h
extern const char* const kSmaaHlsl;
extern const unsigned char* const kSmaaArea;    // R8G8, kSmaaAreaWidth x kSmaaAreaHeight
extern const unsigned char* const kSmaaSearch;  // R8, kSmaaSearchWidth x kSmaaSearchHeight
constexpr uint32_t kSmaaAreaWidth = 160, kSmaaAreaHeight = 560, kSmaaSearchWidth = 64, kSmaaSearchHeight = 16;

// the Vulkan renderer's form: SMAA.hlsl behind its bindings (b1 the metrics, t0..t2, s0 linear, s1 point); entry points vs,
// edges (t0 the scene), weights (t0 the edges, t1 the area texture, t2 the search texture) and blend (t0 the scene,
// t1 the weights)
const std::string& SmaaHlsl();
// the lookup textures: area R8G8 160x560, search R8 64x16, rows top first
const uint8_t* SmaaAreaTex(uint32_t& w, uint32_t& h, uint32_t& pitch);
const uint8_t* SmaaSearchTex(uint32_t& w, uint32_t& h, uint32_t& pitch);

}  // namespace gpu
