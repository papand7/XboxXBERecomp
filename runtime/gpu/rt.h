#pragma once
// What a game's hooks tell the Vulkan renderer's ray tracing (gpu/rt_hlsl.h) about its scene.

namespace gpu {

// the level's sun (a game's hooks read it from the map): toward the sun in the world, its colour (0..1) and strength
// (1 a plain sun). SetRtSun(nullptr): none known, the passes take the light the game shades its models by instead.
struct RtSun {
    float dir[3];
    float colour[3];
    float strength;
};
void SetRtSun(const RtSun* sun);
bool GetRtSun(RtSun& out);  // false: none set

}  // namespace gpu
