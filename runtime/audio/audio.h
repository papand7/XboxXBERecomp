#pragma once
#include <cstdint>
#include <functional>

namespace audio {

constexpr uint32_t kRate = 48000;

// the platform output pulls interleaved stereo float frames from render
void StartOutput(std::function<void(float* out, uint32_t frames)> render);

// A title's sound heard through a wall (xdk/dsound.cpp, app::Settings::occlusion_filter): the buffer (its guest
// object) dulled by a low-pass, 0 clear to 1 fully behind a wall, eased in over about 0.1 s, at once with instant. The
// title lowers its level itself. A buffer started again (Play from stopped) is clear until told. Any thread.
void SetOcclusion(uint32_t buffer, float amount, bool instant = false);

}  // namespace audio
