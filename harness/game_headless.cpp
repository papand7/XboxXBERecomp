// Stand-ins for what a game's PC features call in files the harness leaves out (gpu/style.cpp, app/*, core/input.cpp,
// core/wheel.cpp): no graphics styles, hit marks, wheel or key names headless. Each does nothing, or answers as a PC
// with the original look, no wheel and no keyboard would.
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "gpu/style.h"

namespace gpu {

GraphicsStyle& Style() {
    static GraphicsStyle s;
    return s;
}
GraphicsStyle StyleSnapshot() { return Style(); }
void RequestStyle(const GraphicsStyle& s) { Style() = s; }
const std::vector<StylePreset>& StylePresets() {
    static const std::vector<StylePreset> none;
    return none;
}
int MatchQuality(const GraphicsStyle&, uint32_t, bool, bool) { return 0; }
void QualityValues(int, uint32_t&, bool&, bool&, int&, bool&) {}
bool StyleRendererActive() { return false; }  // the null backend: the Direct3D path's choices
namespace {
std::atomic<int> g_views{1};
}
void SetViewCount(int n) { g_views = n; }
int ViewCount() { return g_views; }
bool RayTracingSupported() { return false; }
bool UpscalerAvailable(int upscaler) { return upscaler == 0; }
void SetAiming(bool) {}
void SetSceneLights(const SceneLight*, uint32_t) {}
void SetSunDirection(const float*) {}
void SetWalkers(const Walker*, uint32_t) {}
void ClearFootprints() {}
void EmitParticles(uint32_t, const float*, const float*, float, const float*) {}
void AddWaterTextures(const uint64_t*, size_t) {}

}  // namespace gpu

namespace app {

void HideOverlays(bool) {}
void HitMarker(float, float, int) {}
void BloodSpray(float, float, float) {}
void BloodHurt(float, float, float, float, float) {}
bool FramePoint(float&, float&) { return false; }
void SetMidFrame(void (*)()) {}
std::array<uint16_t, 2> DefaultKeys(int) { return {0, 0}; }
std::string KeyToName(uint16_t) { return {}; }
int8_t DefaultWheelButton(int) { return -1; }
void WheelDetectStart(WheelDetect, Settings::WheelBind&) {}
void WheelDetectCancel() {}
WheelDetect WheelDetecting(const Settings::WheelBind*) { return WheelDetect::None; }
bool WheelDetectPoll() { return false; }
const char* WheelAxisName(int) { return ""; }
std::string WheelBindText(const Settings::WheelBind&) { return "-"; }

}  // namespace app

namespace input {

void WheelSetup() {}
WheelInfo Wheel() { return {}; }
uint32_t HeldControls() { return 0; }
bool PeekMouse(float&, float&) { return false; }

}  // namespace input
