#pragma once
// Temporal upscalers for the Vulkan renderer (GraphicsStyle::upscaler): AMD FSR 2 (MIT; built from the fetched
// FidelityFX-FSR2 sources, gpu/upscale_fsr.cpp). NVIDIA DLSS and Intel XeSS are vendor SDKs under their own licences
// with binaries (nvngx_dlss.dll, libxess.dll): neither is fetched or built in; the settings show them as not
// installed (UpscalerAvailable). The includer has Vulkan's types already (vulkan.cpp, with VK_NO_PROTOTYPES).
#include <cstdint>

#include "gpu/style.h"

namespace gpu {

// the upscaler's choices, UpscalerAvailable and UpscaleRatio: gpu/style.h

// FSR 2's jitter: the phase count for a render and display width, and phase i's offset in render pixels
int FsrJitterPhases(uint32_t render_w, uint32_t display_w);
void FsrJitterOffset(int index, int phases, float& x, float& y);

struct FsrImage {
    VkImage img = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    uint32_t w = 0, h = 0;
};

struct FsrDispatch {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    FsrImage color;   // the scene at render size (GENERAL; read)
    FsrImage depth;   // device depth, R32F, 0 near to 1 far (cameraNear / cameraFar below)
    FsrImage motion;  // RG16F: the last frame's place minus this one's, in UV units (motion scale: the render size)
    FsrImage output;  // display size, a storage image (GENERAL)
    FsrImage reactive;  // optional, R8 at render size: how much of this frame to take (0 to 1; none: img null)
    uint32_t render_w = 0, render_h = 0;
    float jitter_x = 0, jitter_y = 0;  // render pixels, as FsrJitterOffset gave
    float sharpness = 0;               // 0 to 1, 0 no sharpening
    float frame_ms = 16.7f;
    float near_z = 1, far_z = 8192, fov_y = 1.0472f;
    bool reset = false;
    bool hdr = false;
};

// an FSR 2 context, made again when the sizes or HDR change
class Fsr2 {
public:
    ~Fsr2() { Destroy(); }
    // the device's: the instance's vkGetInstanceProcAddr (the physical device's queries go through it)
    bool Init(VkInstance instance, VkPhysicalDevice pd, VkDevice dev, PFN_vkGetInstanceProcAddr gipa,
              PFN_vkGetDeviceProcAddr gdpa);
    // false: the context could not be made (logged once), nothing was recorded. The colour, depth and motion images
    // are left in VK_IMAGE_LAYOUT_GENERAL afterwards, as they came.
    bool Dispatch(const FsrDispatch& d, uint32_t display_w, uint32_t display_h);
    // the GPU must be done with what used it (the caller waits)
    void Destroy();
    bool Ready() const { return ctx_ != nullptr; }
    // the context fits these sizes (else Dispatch makes it again: the caller waits for the GPU first)
    bool Same(uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, bool hdr) const {
        return !ctx_ || (rw == rw_ && rh == rh_ && dw == dw_ && dh == dh_ && hdr == hdr_);
    }

private:
    bool Ensure(uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, bool hdr);
    void* ctx_ = nullptr;      // FfxFsr2Context
    void* scratch_ = nullptr;  // the backend's
    uint32_t rw_ = 0, rh_ = 0, dw_ = 0, dh_ = 0;
    bool hdr_ = false, failed_ = false;
    VkPhysicalDevice pd_ = VK_NULL_HANDLE;
    VkDevice dev_ = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr gdpa_ = nullptr;
};

}  // namespace gpu
