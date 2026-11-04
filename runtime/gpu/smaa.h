#pragma once
// SMAA 1x (Jimenez et al., MIT; fetched by cmake/XbeRecomp.cmake, its notice in THIRD_PARTY.txt): the Vulkan
// renderer's scene antialiasing, three full screen passes over the 3D scene before the HUD is drawn
#include <cstdint>
#include <string>

namespace gpu {

// HLSL: SMAA.hlsl behind this renderer's bindings (b1 the metrics, t0..t2, s0 linear, s1 point); entry points vs,
// edges (t0 the scene), weights (t0 the edges, t1 the area texture, t2 the search texture) and blend (t0 the scene,
// t1 the weights)
const std::string& SmaaHlsl();
// the lookup textures: area R8G8 160x560, search R8 64x16, rows top first
const uint8_t* SmaaAreaTex(uint32_t& w, uint32_t& h, uint32_t& pitch);
const uint8_t* SmaaSearchTex(uint32_t& w, uint32_t& h, uint32_t& pitch);

}  // namespace gpu
