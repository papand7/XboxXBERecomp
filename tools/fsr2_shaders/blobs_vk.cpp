// FSR 2's Vulkan backend's shaders: the SPIR-V fsr2_shaders compiled at build time (fsr2_blobs.inc), in place of
// the FSR 2 repository's own ffx_fsr2_shaders_vk.cpp (whose headers its FidelityFX_SC.exe writes). The options the
// runtime never sets (display-sized or jittered motion vectors, inverted depth) have no shaders: an empty blob.
// 16-bit types and wave64 are not built: those flags are left out (the 32-bit shaders run everywhere).
#include <cstdint>

#include "ffx_fsr2_shaders_vk.h"

namespace {
struct Fsr2Blob {
    int pass;       // the order fsr2_shaders compiles them in (kFiles below)
    uint32_t perm;  // bit 0 reprojection's Lanczos table, 1 HDR input, 2 sharpening
    const uint32_t* code;
    uint32_t size;
    uint32_t n_uav, n_srv, n_cb;
    const char** uav_names;
    const uint32_t* uav_binds;
    const char** srv_names;
    const uint32_t* srv_binds;
    const char** cb_names;
    const uint32_t* cb_binds;
};
#include "fsr2_blobs.inc"
}  // namespace

extern "C" Fsr2ShaderBlobVK fsr2GetPermutationBlobByIndexVK(FfxFsr2Pass passId, uint32_t options) {
    int file = -1;
    switch (passId) {
        case FFX_FSR2_PASS_DEPTH_CLIP: file = 0; break;
        case FFX_FSR2_PASS_RECONSTRUCT_PREVIOUS_DEPTH: file = 1; break;
        case FFX_FSR2_PASS_LOCK: file = 2; break;
        case FFX_FSR2_PASS_ACCUMULATE:
        case FFX_FSR2_PASS_ACCUMULATE_SHARPEN: file = 3; break;
        case FFX_FSR2_PASS_RCAS: file = 4; break;
        case FFX_FSR2_PASS_COMPUTE_LUMINANCE_PYRAMID: file = 5; break;
        case FFX_FSR2_PASS_GENERATE_REACTIVE: file = 6; break;
        case FFX_FSR2_PASS_TCR_AUTOGENERATE: file = 7; break;
        default: break;
    }
    Fsr2ShaderBlobVK b = {};
    if (file < 0 || !(options & FSR2_SHADER_PERMUTATION_LOW_RES_MOTION_VECTORS) ||
        (options & (FSR2_SHADER_PERMUTATION_JITTER_MOTION_VECTORS | FSR2_SHADER_PERMUTATION_DEPTH_INVERTED)))
        return b;
    uint32_t perm = ((options & FSR2_SHADER_PERMUTATION_REPROJECT_USE_LANCZOS_TYPE) ? 1u : 0u) |
                    ((options & FSR2_SHADER_PERMUTATION_HDR_COLOR_INPUT) ? 2u : 0u) |
                    ((options & FSR2_SHADER_PERMUTATION_ENABLE_SHARPENING) ? 4u : 0u);
    for (const Fsr2Blob& e : kFsr2Blobs) {
        if (e.pass != file || e.perm != perm) continue;
        b.data = reinterpret_cast<const uint8_t*>(e.code);
        b.size = e.size;
        b.storageImageCount = e.n_uav;
        b.sampledImageCount = e.n_srv;
        b.uniformBufferCount = e.n_cb;
        b.boundStorageImageNames = e.uav_names;
        b.boundStorageImageBindings = e.uav_binds;
        b.boundSampledImageNames = e.srv_names;
        b.boundSampledImageBindings = e.srv_binds;
        b.boundUniformBufferNames = e.cb_names;
        b.boundUniformBufferBindings = e.cb_binds;
        break;
    }
    return b;
}
