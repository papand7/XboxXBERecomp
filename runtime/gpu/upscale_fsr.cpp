// AMD FSR 2 (FidelityFX Super Resolution 2.2.1, MIT) for the Vulkan renderer: its context made for the scene's render
// and display sizes, dispatched into the frame's command buffer (gpu/upscale.h). The library and its Vulkan backend
// are built from the fetched FidelityFX-FSR2 sources (cmake/XbeRecomp.cmake), their shaders by tools/fsr2_shaders.
#include <vulkan/vulkan.h>

#include <cstdlib>
#include <cstring>

#include "ffx_fsr2.h"
#include "ffx_fsr2_vk.h"

#include "core/log.h"
#include "gpu/upscale.h"

// FSR 2's Vulkan backend calls these few instance-level entry points by name (the device's it loads through
// vkGetDeviceProcAddr). The runtime links no vulkan-1.lib (it loads vulkan-1.dll at run time), so they are defined
// here and forwarded to the instance's own, set by Fsr2::Init.
namespace {
PFN_vkEnumerateDeviceExtensionProperties g_enum_ext = nullptr;
PFN_vkGetPhysicalDeviceFeatures2 g_features2 = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties g_mem_props = nullptr;
PFN_vkGetPhysicalDeviceProperties g_props = nullptr;
PFN_vkGetPhysicalDeviceProperties2 g_props2 = nullptr;
PFN_vkGetDeviceProcAddr g_gdpa = nullptr;
}  // namespace

extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char* layer,
                                                                    uint32_t* count, VkExtensionProperties* props) {
    if (!g_enum_ext) {
        *count = 0;
        return VK_SUCCESS;
    }
    return g_enum_ext(pd, layer, count, props);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2* f) {
    if (g_features2) g_features2(pd, f);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties* p) {
    if (g_mem_props) g_mem_props(pd, p);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties* p) {
    if (g_props) g_props(pd, p);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2* p) {
    if (g_props2) g_props2(pd, p);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char* name) {
    return g_gdpa ? g_gdpa(dev, name) : nullptr;
}
}

namespace gpu {

bool UpscalerAvailable(int upscaler) { return upscaler == kUpscalerOff || upscaler == kUpscalerFsr; }

float UpscaleRatio(int quality) {
    switch (quality) {
        case kUpscaleQuality: return 1.5f;
        case kUpscaleBalanced: return 1.7f;
        case kUpscalePerformance: return 2.0f;
        default: return 1.0f;
    }
}

int FsrJitterPhases(uint32_t render_w, uint32_t display_w) {
    return ffxFsr2GetJitterPhaseCount(int32_t(render_w), int32_t(display_w));
}

void FsrJitterOffset(int index, int phases, float& x, float& y) {
    x = y = 0;
    ffxFsr2GetJitterOffset(&x, &y, index, phases);
}

namespace {
void Message(FfxFsr2MsgType type, const wchar_t* msg) {
    LOG_FIRST(16, "fsr2: %s %ls", type == FFX_FSR2_MESSAGE_TYPE_ERROR ? "error:" : "warning:", msg);
}
}  // namespace

bool Fsr2::Init(VkInstance instance, VkPhysicalDevice pd, VkDevice dev, PFN_vkGetInstanceProcAddr gipa,
                PFN_vkGetDeviceProcAddr gdpa) {
    g_enum_ext = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance, "vkEnumerateDeviceExtensionProperties"));
    g_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(gipa(instance, "vkGetPhysicalDeviceFeatures2"));
    g_mem_props = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance, "vkGetPhysicalDeviceMemoryProperties"));
    g_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
    g_props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance, "vkGetPhysicalDeviceProperties2"));
    g_gdpa = gdpa;
    pd_ = pd;
    dev_ = dev;
    gdpa_ = gdpa;
    return g_enum_ext && g_features2 && g_mem_props && g_props && g_props2 && gdpa;
}

void Fsr2::Destroy() {
    if (ctx_) ffxFsr2ContextDestroy(static_cast<FfxFsr2Context*>(ctx_));
    delete static_cast<FfxFsr2Context*>(ctx_);
    std::free(scratch_);
    ctx_ = nullptr;
    scratch_ = nullptr;
    rw_ = rh_ = dw_ = dh_ = 0;
}

bool Fsr2::Ensure(uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, bool hdr) {
    if (ctx_ && rw == rw_ && rh == rh_ && dw == dw_ && dh == dh_ && hdr == hdr_) return true;
    Destroy();
    if (failed_ || !dev_) return false;
    const size_t bytes = ffxFsr2GetScratchMemorySizeVK(pd_);
    scratch_ = std::calloc(1, bytes);
    FfxFsr2ContextDescription cd{};
    if (!scratch_ || ffxFsr2GetInterfaceVK(&cd.callbacks, scratch_, bytes, pd_, gdpa_) != FFX_OK) {
        LOGE("fsr2: the Vulkan backend could not be set up; no upscaling");
        failed_ = true;
        Destroy();
        return false;
    }
    cd.device = ffxGetDeviceVK(dev_);
    cd.maxRenderSize = {rw, rh};
    cd.displaySize = {dw, dh};
    cd.flags = hdr ? FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE : 0;
    cd.fpMessage = Message;
    auto* ctx = new FfxFsr2Context{};
    FfxErrorCode e = ffxFsr2ContextCreate(ctx, &cd);
    if (e != FFX_OK) {
        LOGE("fsr2: the context could not be made (%d); no upscaling", int(e));
        delete ctx;
        failed_ = true;
        Destroy();
        return false;
    }
    ctx_ = ctx;
    rw_ = rw, rh_ = rh, dw_ = dw, dh_ = dh, hdr_ = hdr;
    LOGR("fsr2: FSR 2.2.1 context %ux%u -> %ux%u%s", rw, rh, dw, dh, hdr ? " (HDR input)" : "");
    return true;
}

bool Fsr2::Dispatch(const FsrDispatch& d, uint32_t display_w, uint32_t display_h) {
    if (!Ensure(d.color.w, d.color.h, display_w, display_h, d.hdr)) return false;
    auto* ctx = static_cast<FfxFsr2Context*>(ctx_);
    auto res = [&](const FsrImage& im, const wchar_t* name, FfxResourceStates st) {
        return ffxGetTextureResourceVK(ctx, im.img, im.view, im.w, im.h, im.fmt, name, st);
    };
    FfxFsr2DispatchDescription dd{};
    dd.commandList = ffxGetCommandListVK(d.cmd);
    // the inputs come in GENERAL (FFX's generic read); the backend moves them to shader-read layouts
    dd.color = res(d.color, L"color", FFX_RESOURCE_STATE_GENERIC_READ);
    dd.depth = res(d.depth, L"depth", FFX_RESOURCE_STATE_GENERIC_READ);
    dd.motionVectors = res(d.motion, L"motion", FFX_RESOURCE_STATE_GENERIC_READ);
    dd.output = res(d.output, L"output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    if (d.reactive.img) dd.reactive = res(d.reactive, L"reactive", FFX_RESOURCE_STATE_GENERIC_READ);
    dd.jitterOffset = {d.jitter_x, d.jitter_y};
    dd.motionVectorScale = {float(d.render_w), float(d.render_h)};
    dd.renderSize = {d.render_w, d.render_h};
    dd.enableSharpening = d.sharpness > 0;
    dd.sharpness = d.sharpness;
    dd.frameTimeDelta = d.frame_ms;
    dd.preExposure = 1.0f;
    dd.reset = d.reset;
    dd.cameraNear = d.near_z;
    dd.cameraFar = d.far_z;
    dd.cameraFovAngleVertical = d.fov_y;
    dd.viewSpaceToMetersFactor = 1.0f;
    FfxErrorCode e = ffxFsr2ContextDispatch(ctx, &dd);
    // back to GENERAL, where the renderer keeps every image
    static PFN_vkCmdPipelineBarrier barrier =
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(gdpa_(dev_, "vkCmdPipelineBarrier"));
    VkImageMemoryBarrier ib[4]{};
    const FsrImage* ins[4] = {&d.color, &d.depth, &d.motion, &d.reactive};
    const uint32_t n_in = d.reactive.img ? 4 : 3;
    for (uint32_t i = 0; i < n_in; i++) {
        ib[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ib[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ib[i].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        ib[i].oldLayout = e == FFX_OK ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
        ib[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib[i].image = ins[i]->img;
        ib[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    if (barrier && e == FFX_OK)
        barrier(d.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                nullptr, n_in, ib);
    if (e != FFX_OK) {
        LOG_FIRST(4, "fsr2: dispatch failed (%d)", int(e));
        return false;
    }
    return true;
}

}  // namespace gpu
