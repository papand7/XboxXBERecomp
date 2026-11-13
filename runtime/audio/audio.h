#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace audio {

constexpr uint32_t kRate = 48000;

// the platform output pulls interleaved stereo float frames from render
void StartOutput(std::function<void(float* out, uint32_t frames)> render);

// 3D sound (app::Settings::audio_3d, xdk/dsound.cpp), from a title that knows its world. Any thread.
// The listener and the playing 3D voices placed in the world (not head relative), in the title's own units; false
// when 3D sound is off
struct Source {
    uint32_t id;
    float pos[3];
};
bool Sources(float listener[3], std::vector<Source>& out);
// A title's sound heard through a wall: the buffer (its guest object, or a Source's id) quieter and dulled by a
// low-pass (app::Settings::occlusion_filter), 0 clear to 1 fully behind a wall, eased in over about 0.1 s, at once
// with instant. A buffer started again (Play from stopped) is clear until told.
void SetOcclusion(uint32_t buffer, float amount, bool instant = false);
// the room around the listener: indoor 0 in the open to 1 enclosed, size its reach in the title's units (the reverb's
// length); size 0 no room (the menus): no reverb
void SetRoom(float indoor, float size);

}  // namespace audio
