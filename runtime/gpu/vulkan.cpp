// The title's Direct3D 8 calls drawn with Vulkan 1.3 (renderer = "vulkan" in the settings file). The console path of
// d3d11.cpp on Vulkan, with what does not depend on the API shared through d3d_common (its D3D12 enums translated
// here). The shaders are d3d_common's and shaders.cpp's HLSL, compiled to SPIR-V by DXC (dxcompiler.dll beside the
// exe). Vulkan is loaded at run time (vulkan-1.dll): without it Init fails and the fallback renderer takes over.
//
// Synchronisation is kept simple and correct before fast: every image lives in VK_IMAGE_LAYOUT_GENERAL, one dynamic
// rendering instance is open at a time, and each end of one and each transfer is followed by a full memory barrier.
// Not here yet: MSAA, F3 captures, renderer extensions and the perf report.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <dxcapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_win32.h>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/d3d_common.h"
#include "gpu/ext.h"
#include "gpu/shaders.h"
#include "gpu/smaa.h"
#include "gpu/style.h"
#include "gpu/texture.h"
#include "gpu/texture_pack.h"
#include "gpu/detail.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"

using Microsoft::WRL::ComPtr;

namespace gpu {

namespace {

// ---- the Vulkan entry points, loaded from vulkan-1.dll

#define VK_GLOBAL_FUNCS(X)                  \
    X(vkCreateInstance)                     \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties)   \
    X(vkEnumerateInstanceVersion)

#define VK_INSTANCE_FUNCS(X)                     \
    X(vkDestroyInstance)                         \
    X(vkEnumeratePhysicalDevices)                \
    X(vkGetPhysicalDeviceProperties)             \
    X(vkGetPhysicalDeviceFeatures2)              \
    X(vkGetPhysicalDeviceQueueFamilyProperties)  \
    X(vkGetPhysicalDeviceMemoryProperties)       \
    X(vkGetPhysicalDeviceFormatProperties)       \
    X(vkEnumerateDeviceExtensionProperties)      \
    X(vkCreateDevice)                            \
    X(vkGetDeviceProcAddr)                       \
    X(vkCreateWin32SurfaceKHR)                   \
    X(vkDestroySurfaceKHR)                       \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)      \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)      \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define VK_DEBUG_FUNCS(X)              \
    X(vkCreateDebugUtilsMessengerEXT)  \
    X(vkDestroyDebugUtilsMessengerEXT)

#define VK_DEVICE_FUNCS(X)          \
    X(vkDestroyDevice)              \
    X(vkGetDeviceQueue)             \
    X(vkDeviceWaitIdle)             \
    X(vkQueueSubmit)                \
    X(vkQueuePresentKHR)            \
    X(vkCreateSwapchainKHR)         \
    X(vkDestroySwapchainKHR)        \
    X(vkGetSwapchainImagesKHR)      \
    X(vkAcquireNextImageKHR)        \
    X(vkCreateCommandPool)          \
    X(vkDestroyCommandPool)         \
    X(vkAllocateCommandBuffers)     \
    X(vkBeginCommandBuffer)         \
    X(vkEndCommandBuffer)           \
    X(vkResetCommandBuffer)         \
    X(vkCreateSemaphore)            \
    X(vkDestroySemaphore)           \
    X(vkWaitSemaphores)             \
    X(vkGetSemaphoreCounterValue)   \
    X(vkAllocateMemory)             \
    X(vkFreeMemory)                 \
    X(vkMapMemory)                  \
    X(vkCreateBuffer)               \
    X(vkDestroyBuffer)              \
    X(vkGetBufferMemoryRequirements) \
    X(vkBindBufferMemory)           \
    X(vkCreateImage)                \
    X(vkDestroyImage)               \
    X(vkGetImageMemoryRequirements) \
    X(vkBindImageMemory)            \
    X(vkCreateImageView)            \
    X(vkDestroyImageView)           \
    X(vkCreateSampler)              \
    X(vkDestroySampler)             \
    X(vkCreateShaderModule)         \
    X(vkDestroyShaderModule)        \
    X(vkCreatePipelineLayout)       \
    X(vkDestroyPipelineLayout)      \
    X(vkCreateDescriptorSetLayout)  \
    X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool)       \
    X(vkDestroyDescriptorPool)      \
    X(vkCreateGraphicsPipelines)    \
    X(vkDestroyPipeline)            \
    X(vkCreatePipelineCache)        \
    X(vkGetPipelineCacheData)       \
    X(vkDestroyPipelineCache)       \
    X(vkCreateQueryPool)            \
    X(vkDestroyQueryPool)           \
    X(vkGetQueryPoolResults)        \
    X(vkResetQueryPool)             \
    X(vkCmdBeginRendering)          \
    X(vkCmdEndRendering)            \
    X(vkCmdPipelineBarrier2)        \
    X(vkCmdBindPipeline)            \
    X(vkCmdBindVertexBuffers2)      \
    X(vkCmdBindIndexBuffer)         \
    X(vkCmdDraw)                    \
    X(vkCmdDrawIndexed)             \
    X(vkCmdSetViewport)             \
    X(vkCmdSetScissor)              \
    X(vkCmdSetBlendConstants)       \
    X(vkCmdSetStencilReference)     \
    X(vkCmdSetPrimitiveTopology)    \
    X(vkCmdPushDescriptorSetKHR)    \
    X(vkCmdCopyBufferToImage)       \
    X(vkCmdCopyImageToBuffer)       \
    X(vkCmdCopyImage)               \
    X(vkCmdClearAttachments)        \
    X(vkCmdBeginQuery)              \
    X(vkCmdWriteTimestamp2)         \
    X(vkCmdEndQuery)                \
    X(vkCmdPushConstants)           \
    X(vkAllocateDescriptorSets)     \
    X(vkUpdateDescriptorSets)       \
    X(vkCmdBindDescriptorSets)

// ray tracing's, loaded when the GPU has ray queries (VulkanBackend::rt_ok_)
#define VK_RT_FUNCS(X)                         \
    X(vkGetBufferDeviceAddress)                \
    X(vkCreateAccelerationStructureKHR)        \
    X(vkDestroyAccelerationStructureKHR)       \
    X(vkGetAccelerationStructureBuildSizesKHR) \
    X(vkCmdBuildAccelerationStructuresKHR)     \
    X(vkGetAccelerationStructureDeviceAddressKHR)

#define VK_DECLARE(f) PFN_##f f = nullptr;
VK_GLOBAL_FUNCS(VK_DECLARE)
VK_INSTANCE_FUNCS(VK_DECLARE)
VK_DEBUG_FUNCS(VK_DECLARE)
VK_DEVICE_FUNCS(VK_DECLARE)
VK_RT_FUNCS(VK_DECLARE)
#undef VK_DECLARE
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
VkInstance g_instance = VK_NULL_HANDLE;  // for ImGui's loader

constexpr uint32_t kFramesInFlight = 2;
constexpr uint64_t kStaleFrames = 8;            // frames a target may go unused and still be read back
constexpr VkDeviceSize kStreamChunk = 16u << 20;  // per-draw data: vertices, indices, constants, uploads
constexpr VkDeviceSize kBlockSize = 64u << 20;    // device memory blocks images and buffers are carved from
constexpr int kLateSlots = 3;                   // copies in flight per surface locked without a wait
constexpr uint32_t kQueries = 4096;             // occlusion queries
constexpr VkFormat kBgra = VK_FORMAT_B8G8R8A8_UNORM;
// descriptor bindings, set 0, as DXC's register shifts lay them out: b0 b1, t0-t3, s0-s3
constexpr uint32_t kBindVsc = 0, kBindDc = 1, kBindTex = 2, kBindSmp = 6, kBindings = 10;
// after them: u0, where a ray traced frame's vertex shaders write the scene's positions (VulkanBackend::RtCapture),
// and the acceleration structure the ray traced passes read
constexpr uint32_t kBindCapture = 10, kBindTlas = 11;
// the ray traced passes' too: the frame's captured vertices and triangles (a hit's own triangle, its slope)
constexpr uint32_t kBindRtVerts = 12, kBindRtTris = 13;
// and each triangle's material (a word: its texture, alpha test), the first word the opaque triangles' count; set 1
// (not pushed, one a frame slot): the frame's textures the materials name, and the sampler they are read with
constexpr uint32_t kBindRtMats = 14, kRtTextures = 1024;
// and where the level lighting adds up its outdoor pixels' baked light (RtFrame::stats, RtRelightPass)
constexpr uint32_t kBindRtStats = 15;
constexpr uint32_t kRtLights = 64;  // the game's lights the level lighting takes, the nearest (RtRelightPass)
// a captured vertex: its place (float4), then its texture coordinates (float2), diffuse colour (RGBA8) and a pad
constexpr uint32_t kRtVertBytes = 32;

const char* VkResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VkResult";
    }
}

// ---- D3D12 values d3d_common hands over, as Vulkan's

VkBlendFactor VkBlend(D3D12_BLEND b) {
    switch (b) {
        case D3D12_BLEND_ZERO: return VK_BLEND_FACTOR_ZERO;
        case D3D12_BLEND_ONE: return VK_BLEND_FACTOR_ONE;
        case D3D12_BLEND_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
        case D3D12_BLEND_INV_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case D3D12_BLEND_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
        case D3D12_BLEND_INV_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case D3D12_BLEND_DEST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
        case D3D12_BLEND_INV_DEST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case D3D12_BLEND_DEST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
        case D3D12_BLEND_INV_DEST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case D3D12_BLEND_SRC_ALPHA_SAT: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case D3D12_BLEND_BLEND_FACTOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case D3D12_BLEND_INV_BLEND_FACTOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case D3D12_BLEND_SRC1_COLOR: return VK_BLEND_FACTOR_SRC1_COLOR;
        case D3D12_BLEND_INV_SRC1_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case D3D12_BLEND_SRC1_ALPHA: return VK_BLEND_FACTOR_SRC1_ALPHA;
        case D3D12_BLEND_INV_SRC1_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}
// D3D12's comparison, stencil, blend op and address enums run in Vulkan's order from 1
VkCompareOp VkCmp(D3D12_COMPARISON_FUNC f) { return f ? VkCompareOp(f - 1) : VK_COMPARE_OP_NEVER; }
VkStencilOp VkStencil(D3D12_STENCIL_OP o) { return o ? VkStencilOp(o - 1) : VK_STENCIL_OP_KEEP; }
VkBlendOp VkBlendOperation(D3D12_BLEND_OP o) { return o ? VkBlendOp(o - 1) : VK_BLEND_OP_ADD; }
VkSamplerAddressMode VkAddress(D3D12_TEXTURE_ADDRESS_MODE m) {
    return m ? VkSamplerAddressMode(m - 1) : VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkFormat VkFormatOf(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_BC1_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case DXGI_FORMAT_BC2_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
        case DXGI_FORMAT_BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
        case DXGI_FORMAT_R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
        case DXGI_FORMAT_R16_UNORM: return VK_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R16G16B16A16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
        case DXGI_FORMAT_R16G16B16A16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
        case DXGI_FORMAT_R16G16_SINT: return VK_FORMAT_R16G16_SINT;
        case DXGI_FORMAT_R16G16_SNORM: return VK_FORMAT_R16G16_SNORM;
        case DXGI_FORMAT_R16_SINT: return VK_FORMAT_R16_SINT;
        case DXGI_FORMAT_R16_SNORM: return VK_FORMAT_R16_SNORM;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case DXGI_FORMAT_R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
        case DXGI_FORMAT_R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
        case DXGI_FORMAT_R32_UINT: return VK_FORMAT_R32_UINT;
        case DXGI_FORMAT_R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
        case DXGI_FORMAT_R8_UNORM: return VK_FORMAT_R8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkPrimitiveTopology VkTopology(D3D_PRIMITIVE_TOPOLOGY t) {
    switch (t) {
        case D3D_PRIMITIVE_TOPOLOGY_POINTLIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINELIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// bytes per block of a compressed host format, 0 for the 4 byte texel formats
uint32_t BlockBytes(VkFormat f) {
    switch (f) {
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return 8;
        case VK_FORMAT_BC2_UNORM_BLOCK: case VK_FORMAT_BC3_UNORM_BLOCK: return 16;
        default: return 0;
    }
}

// ---- device memory: blocks carved first fit, big requests their own

struct Mem {
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize off = 0, size = 0;
    uint8_t* map = nullptr;  // host visible memory, mapped for good
    int block = -1;          // -1: a dedicated allocation
};

class Allocator {
public:
    void Init(VkPhysicalDevice pd, VkDevice dev) {
        dev_ = dev;
        vkGetPhysicalDeviceMemoryProperties(pd, &props_);
    }
    // linear: a buffer (kept out of image blocks, so bufferImageGranularity never matters)
    bool Alloc(const VkMemoryRequirements& r, VkMemoryPropertyFlags want, VkMemoryPropertyFlags prefer, bool linear,
               Mem& out) {
        int type = Type(r.memoryTypeBits, want | prefer);
        if (type < 0) type = Type(r.memoryTypeBits, want);
        if (type < 0) return false;
        bool host = (props_.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        if (r.size > kBlockSize / 4) return Dedicated(r.size, uint32_t(type), host, out);
        for (size_t b = 0; b < blocks_.size(); b++) {
            Block& bl = blocks_[b];
            if (bl.type != uint32_t(type) || bl.linear != linear) continue;
            if (Carve(bl, r.size, r.alignment, out)) {
                out.block = int(b);
                return true;
            }
        }
        Block bl;
        bl.type = uint32_t(type);
        bl.linear = linear;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, kBlockSize, uint32_t(type)};
        if (vkAllocateMemory(dev_, &ai, nullptr, &bl.mem) != VK_SUCCESS) return Dedicated(r.size, uint32_t(type), host, out);
        if (host) vkMapMemory(dev_, bl.mem, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&bl.map));
        bl.free[0] = kBlockSize;
        blocks_.push_back(std::move(bl));
        if (!Carve(blocks_.back(), r.size, r.alignment, out)) return false;
        out.block = int(blocks_.size() - 1);
        return true;
    }
    // a buffer whose device address is taken (ray tracing's): memory of its own, allocated for addresses
    bool Addressable(const VkMemoryRequirements& r, VkMemoryPropertyFlags want, Mem& out) {
        int type = Type(r.memoryTypeBits, want);
        if (type < 0) return false;
        bool host = (props_.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        return Dedicated(r.size, uint32_t(type), host, out, true);
    }
    void Free(Mem& m) {
        if (!m.mem) return;
        if (m.block < 0) {
            vkFreeMemory(dev_, m.mem, nullptr);
        } else {
            auto& fr = blocks_[size_t(m.block)].free;
            auto it = fr.emplace(m.off, m.size).first;
            auto next = std::next(it);
            if (next != fr.end() && it->first + it->second == next->first) {
                it->second += next->second;
                fr.erase(next);
            }
            if (it != fr.begin()) {
                auto prev = std::prev(it);
                if (prev->first + prev->second == it->first) {
                    prev->second += it->second;
                    fr.erase(it);
                }
            }
        }
        m = Mem{};
    }

private:
    struct Block {
        VkDeviceMemory mem = VK_NULL_HANDLE;
        uint8_t* map = nullptr;
        uint32_t type = 0;
        bool linear = false;
        std::map<VkDeviceSize, VkDeviceSize> free;  // offset to size
    };
    int Type(uint32_t bits, VkMemoryPropertyFlags want) const {
        for (uint32_t i = 0; i < props_.memoryTypeCount; i++)
            if ((bits >> i & 1) && (props_.memoryTypes[i].propertyFlags & want) == want) return int(i);
        return -1;
    }
    bool Dedicated(VkDeviceSize size, uint32_t type, bool host, Mem& out, bool address = false) {
        VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, address ? &fi : nullptr, size, type};
        if (vkAllocateMemory(dev_, &ai, nullptr, &out.mem) != VK_SUCCESS) return false;
        out.off = 0;
        out.size = size;
        out.block = -1;
        out.map = nullptr;
        if (host) vkMapMemory(dev_, out.mem, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&out.map));
        return true;
    }
    static bool Carve(Block& bl, VkDeviceSize size, VkDeviceSize align, Mem& out) {
        for (auto it = bl.free.begin(); it != bl.free.end(); ++it) {
            VkDeviceSize start = (it->first + align - 1) / align * align;
            VkDeviceSize end = it->first + it->second;
            if (start + size > end) continue;
            VkDeviceSize head = start - it->first;
            VkDeviceSize first = it->first;
            bl.free.erase(it);
            if (head) bl.free[first] = head;
            if (start + size < end) bl.free[start + size] = end - start - size;
            out.mem = bl.mem;
            out.off = start;
            out.size = size;
            out.map = bl.map ? bl.map + start : nullptr;
            return true;
        }
        return false;
    }
    VkDevice dev_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties props_{};
    std::vector<Block> blocks_;
};

// an image and its views
struct Image {
    VkImage img = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;  // sampled (a depth image's depth alone)
    VkImageView att = VK_NULL_HANDLE;   // a depth image's depth and stencil, for rendering
    Mem mem;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    uint32_t w = 0, h = 0, levels = 1, layers = 1;
    bool depth = false;
    explicit operator bool() const { return img != VK_NULL_HANDLE; }
};

struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    Mem mem;
    VkDeviceSize size = 0;
};

// a stretch of a stream buffer
struct Stretch {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceSize off = 0;
    uint8_t* p = nullptr;
};

// a console surface drawn into
struct Target {
    Image res;
    Image fb;      // copy read by draws that sample the target they render to
    Image shrunk;  // guest sized copy for read-backs of an upscaled target, kept for the next one
    bool depth = false;
    uint32_t gw = 0, gh = 0, w = 0, h = 0;
    uint32_t data = 0, bytes = 0;
    uint64_t epoch = 0, sig = 0;  // last lock seen, guest memory as last matched
    bool drawn = false;           // the GPU wrote it since the last readback
    bool linear32 = true;         // a 32 bit linear surface in guest memory, written back row by row
    bool swizzled32 = false;      // a 32 bit swizzled one, written back in Morton order (a title reading it as data)
    std::vector<uint8_t> read;    // what Sync left in guest memory
    // the scene pass's, made for the 3D scene's target alone: SMAA's edges and weights; the reflection mask (r: how
    // much each pixel reflects) and its depth (the nearest reflective surface), cleared once a frame
    Image smaa_edges, smaa_weights, mask, mask_ds;
    uint64_t mask_frame = 0;  // the frame (presents_ + 1) it was cleared for
    // a depth target's copy soft particles read (drawn into the target at the time: no feedback loop), made at the
    // frame's first soft particle
    Image soft_depth;
    uint64_t soft_frame = 0;
    // ray tracing: the scene's depth as it was when the game cleared it mid-frame (NightFire, before the gun and
    // hands), which the traced passes take the nearer of with the depth at the end
    Image rt_depth;
    uint64_t rt_depth_frame = 0;
    // effect lights: what added (glowing) draws give off this frame (RGBA16F: their colour, a their brightness by
    // distance), cleared at the frame's first; the lights it makes, a tile each (RGBA32F: colour and brightness;
    // where, as the screen's fractions and the distance)
    Image glow, light_a, light_b, light_list;
    Image atmo[5];  // light shafts and fog: a quarter of the scene's size, then each half the one before
    Image atmo_pick;  // their lights: each quarter's bright centre, the sky's colour
    Image bounce;     // bounced light: the gathered light, a quarter of the scene's size
    Image rt_ao;      // ray traced ambient occlusion: what is open (r) and the distance (g), the scene's size
    Image rt_gi;      // ray traced bounced light (kRtRelightHlsl's gi): its light and distance, half the scene's size
    Image dof_focus[2];  // depth of field: the distance in focus (1 / metres), this frame's and the last's in turn
    uint64_t dof_frame = 0;  // the frame (presents_ + 1) it was last drawn for
    uint64_t glow_frame = 0;
    Image hdr_scene;  // HDR: the scene as the scene pass left it (before the HUD), of the frame hdr_frame
    uint64_t hdr_frame = 0;
};

// a texture from guest memory
struct Tex {
    Image img;
    TextureDesc d;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    uint32_t levels = 1;
    uint64_t sig = 0;
    uint64_t frame = 0;
    uint64_t palette = 0;  // hash of the colours a P8 texture was decoded with
    uint64_t faces = 0;    // a cube's frame its faces drawn on the GPU were last copied in
    uint64_t key = 0;      // its texture pack name (TextureKey), what mods/reflections.toml lists
    float glass = 0;       // how much its alpha looks like window glass, 0 to 1 (GlassScore)
    bool solid = false;    // its alpha is all 1 (or it has none): drawn blended, it still covers
    uint8_t detail = 0;    // its ground detail's kind (gpu::TextureDetail), 0 none, kDetailUnknown not found yet
};

// a draw's four texture stages
struct DrawTextures {
    TexInfo info[4];
    VkImageView view[4] = {};
    uint32_t levels[4] = {1, 1, 1, 1};
    bool replaced[4] = {};  // a title's replacement picture (FindReplacement)
    StageReads reads;
};

// Blit's settings past the source and its rectangle
struct BlitOptions {
    bool gamma = false;  // through the gamma lookup
    bool alpha = false;  // the source's alpha, else 1
    uint32_t taps = 1;   // n x n box filter per pixel
    bool over = false;   // premultiplied over the target, which keeps its alpha
    bool final = false;  // the finished frame: psfinal with the settings' FXAA and sharpening (taps, alpha unused)
};

// what a rendering instance draws into
struct Attachments {
    VkImageView color = VK_NULL_HANDLE;
    VkImageView depth = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0;
    bool operator==(const Attachments& o) const {
        return color == o.color && depth == o.depth && w == o.w && h == o.h;
    }
};

// after a Sync every byte is compared: the signature samples a big surface and misses small writes (XFONT text)
bool Changed(const Target& t) {
    if (t.drawn || t.read.empty()) return false;
    uint32_t row = t.gw * 4;
    uint32_t pitch = GuestPitch(t.gw);
    const uint8_t* p = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++) {
        if (std::memcmp(p + size_t(y) * pitch, &t.read[size_t(y) * row], row)) return true;
    }
    return false;
}

// a lock alone proves nothing, reads lock too: only changed bytes mean the CPU wrote it
bool LatchCpuWrite(Target& t) {
    uint64_t e = DirtyEpoch(t.data);
    if (e <= t.epoch) return false;
    t.epoch = e;
    uint64_t sig = SurfaceSig(t.data, t.bytes);
    if (sig == t.sig && !Changed(t)) return false;
    t.sig = sig;
    return true;
}

// the pixels the CPU changed since the last Sync, opaque, the rest 0; t.read takes them
std::vector<uint8_t> LatchCpuChanges(Target& t) {
    std::vector<uint8_t> px(t.read.size(), 0);
    uint32_t guest_pitch = GuestPitch(t.gw);
    const uint8_t* guest = static_cast<const uint8_t*>(kernel::HostPtr(t.data));
    for (uint32_t y = 0; y < t.gh; y++) {
        for (uint32_t x = 0; x < t.gw; x++) {
            const uint8_t* c = guest + size_t(y) * guest_pitch + x * 4;
            size_t i = (size_t(y) * t.gw + x) * 4;
            if (!std::memcmp(c, &t.read[i], 4)) continue;
            std::memcpy(&t.read[i], c, 4);
            std::memcpy(&px[i], c, 3);
            px[i + 3] = 255;
        }
    }
    return px;
}

uint64_t TargetMapKey(uint32_t data, bool depth) { return uint64_t(data) << 1 | (depth ? 1 : 0); }

// a smaller depth surface's console size, above the address bits
constexpr int kKeyWidthShift = 34;
constexpr int kKeyHeightShift = 48;

// a P8 texture is decoded with the stage's palette: 256 colours, fewer by the size code in the top two bits
struct StagePalette {
    uint32_t colours[256] = {};
    uint32_t va = 0;
    uint64_t hash = 0;
};

void ReadStagePalette(const State& s, int stage, const TextureDesc& d, StagePalette& palette) {
    constexpr uint32_t kFmtP8 = 0x0B;       // D3DFMT_P8
    constexpr uint32_t kPaletteCommon = 0;  // D3DPalette: Common, its top two bits the size code
    constexpr uint32_t kPaletteData = 4;    // D3DPalette: Data, physical address of the colours
    constexpr uint32_t kPaletteSizeShift = 30;
    if (d.format != kFmtP8 || !s.palettes[size_t(stage)]) return;
    uint32_t header = s.palettes[size_t(stage)];
    palette.va = mem::VirtualFromPhysical(kernel::R32(header + kPaletteData));
    uint32_t size_code = kernel::R32(header + kPaletteCommon) >> kPaletteSizeShift;
    size_t bytes = (std::size(palette.colours) >> size_code) * sizeof(uint32_t);
    std::memcpy(palette.colours, kernel::HostPtr(palette.va), bytes);
    palette.hash = Hash(palette.colours, sizeof(palette.colours));
}

bool SameTexture(const Tex& tx, const TextureDesc& d) {
    return tx.img && tx.d.format == d.format && tx.d.width == d.width && tx.d.height == d.height &&
           tx.d.depth == d.depth && tx.d.levels == d.levels && tx.d.cube == d.cube && tx.d.pitch == d.pitch;
}

// a texture's level 0 alpha as window glass has it: see-through all over and even (a smoke puff or a leaf's alpha
// spans the range, a cut-out's is 0 or 1); 0 to 1, 0 for formats without alpha. Up to 4096 texels are read
float GlassScore(VkFormat fmt, const std::vector<uint8_t>& px, uint32_t pitch, uint32_t w, uint32_t h, bool& solid) {
    solid = fmt != VK_FORMAT_B8G8R8A8_UNORM && fmt != VK_FORMAT_R8G8B8A8_UNORM && fmt != VK_FORMAT_BC2_UNORM_BLOCK &&
            fmt != VK_FORMAT_BC3_UNORM_BLOCK;  // BC1 too: its cut-outs are what alpha test drops
    std::vector<uint8_t> a;
    a.reserve(4096);
    if (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_UNORM) {
        uint32_t step = std::max(1u, uint32_t(std::sqrt(double(w) * h / 4096.0)));
        for (uint32_t y = 0; y < h; y += step)
            for (uint32_t x = 0; x < w; x += step)
                if (size_t(y) * pitch + x * 4 + 3 < px.size()) a.push_back(px[size_t(y) * pitch + x * 4 + 3]);
    } else if (fmt == VK_FORMAT_BC2_UNORM_BLOCK || fmt == VK_FORMAT_BC3_UNORM_BLOCK) {
        size_t blocks = px.size() / 16, step = std::max<size_t>(1, blocks / 256);
        for (size_t b = 0; b < blocks; b += step) {
            const uint8_t* q = &px[b * 16];
            for (int i = 0; i < 16; i++) {
                if (fmt == VK_FORMAT_BC2_UNORM_BLOCK) {
                    a.push_back(uint8_t(((q[i / 2] >> (i & 1) * 4) & 15) * 17));
                    continue;
                }
                uint32_t a0 = q[0], a1 = q[1], bit = 16 + i * 3;
                uint32_t idx = ((q[bit / 8] | q[std::min(bit / 8 + 1, 7u)] << 8) >> (bit & 7)) & 7;
                uint32_t v = idx == 0 ? a0 : idx == 1 ? a1
                             : a0 > a1 ? ((8 - idx) * a0 + (idx - 1) * a1) / 7
                             : idx == 6 ? 0 : idx == 7 ? 255 : ((6 - idx) * a0 + (idx - 1) * a1) / 5;
                a.push_back(uint8_t(v));
            }
        }
    }
    if (a.size() < 16) return 0;
    double sum = 0, sq = 0;
    size_t mid = 0, full = 0;
    for (uint8_t v : a) sum += v, sq += double(v) * v, mid += v >= 16 && v <= 235, full += v >= 240;
    solid = full == a.size();
    double n = double(a.size()), mean = sum / n, sd = std::sqrt(std::max(0.0, sq / n - mean * mean));
    double part = double(mid) / n;
    if (part < 0.6 || sd > 45 || mean < 16 || mean > 215) return 0;
    return float(std::clamp((part - 0.6) / 0.3, 0.0, 1.0) * std::clamp((45 - sd) / 25, 0.0, 1.0));
}

// drawn over what is there without mixing: blending off, or on with ONE, ZERO (NightFire leaves it on for walls)
bool Opaque(const PsoKey& k) { return !k.blend || (k.src == 1 && k.dst == 0); }

// HLSL's vertex inputs ATTRn at location n, as the pipeline's vertex attributes number them (DXC numbers them in
// declaration order otherwise)
std::string LocateInputs(std::string src) {
    size_t at = 0;
    while ((at = src.find(": ATTR", at)) != std::string::npos) {
        size_t n = at + 6;
        int reg = 0;
        while (n < src.size() && src[n] >= '0' && src[n] <= '9') reg = reg * 10 + (src[n++] - '0');
        size_t line = src.rfind('\n', at);
        line = line == std::string::npos ? 0 : line + 1;
        while (line < at && (src[line] == ' ' || src[line] == '\t')) line++;
        std::string tag = "[[vk::location(" + std::to_string(reg) + ")]] ";
        src.insert(line, tag);
        at = n + tag.size();
    }
    return src;
}

// The graphics styles' scene pass (style.h): the 3D scene, copied, drawn back over itself before the first HUD draw.
// b1 p[8]: [0] mode, 1/width, 1/height, seconds; [1] exposure, tone map, contrast, saturation; [2] temperature,
// sharpen, bloom, bloom threshold; [3] AO, AO radius, vignette, grain; [4] bands, outline px, internal edges, edge
// threshold; [5] outline colour, halftone; [6] block px, colour levels, dither, retro grading; [7] depth bound,
// width, height, host px per console px; [8] reflections' strength (0 none), ray steps, tan of half the vertical view
// angle, width / height; [9] the thickness a ray hits and its reach (parts of the surface's distance); [10] the depth
// holds what was drawn after a mid-frame clear alone (NightFire's gun and hands: RtKeepDepth kept the world's). t0 the
// scene, t1 its depth, t2 the reflection mask (r), t3 its depth; s0 linear, s1 point.
const char* kScenePostHlsl = R"(
cbuffer P : register(b1) { float4 p[11]; };
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D maskc : register(t2);
Texture2D<float> maskz : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
static const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
float3 tex(float2 uv) { return scene.SampleLevel(pt, uv, 0).rgb; }
float3 texl(float2 uv) { return scene.SampleLevel(ls, uv, 0).rgb; }
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float rawz(float2 uv) { return depthtex.SampleLevel(pt, uv, 0); }
// grows with distance for a perspective depth: what neighbour comparisons are made on
float linz(float d) { return 1.0 / max(1.0 - d, 1e-6); }
// the distance a depth stands for: a W-buffer's (p[9].w 1, NightFire's) is the distance itself, scaled; linz would
// make all of it nearly 1 and every comparison nothing
float viewz(float d) { return p[9].w != 0 ? d * 4096 : linz(d); }
float noise(float2 v) { return frac(sin(dot(v, float2(12.9898, 78.233))) * 43758.5453); }
float3 aces(float3 a) { return saturate((a * (2.51 * a + 0.03)) / (a * (2.43 * a + 0.59) + 0.14)); }
float3 grade(float3 c) {
    c *= p[1].x;
    // filmic: an S-curve with mid grey (0.18) and white kept where they were, so a night stays a night
    if (p[1].y == 1) c = saturate(aces(c * 0.5) / 0.616);
    if (p[1].y == 2) { float3 x = c * 1.25; c = saturate(x * (1 + x / 2.25) / (1 + x) / 0.871); }
    c = (c - 0.5) * p[1].z + 0.5;
    c = lerp(luma(c).xxx, c, p[1].w);
    c *= float3(1 + 0.1 * p[2].x, 1, 1 - 0.1 * p[2].x);
    return saturate(c);
}
// noir: black and white, hard, but what is red (blood, lamps, the HUD's red) kept a deep red; the player's own gun
// and hands never (skin under a red or warm lamp passes for red)
float3 noir(float3 c, bool hands) {
    float red = hands ? 0 : saturate((c.r / max(max(c.g, c.b), 0.02) - 2.2) * 2) * saturate(c.r * 4);
    float g = saturate((sqrt(luma(c)) - 0.15) / 0.55);  // a dim level's mid tones kept off black
    g = g * g * (3 - 2 * g);
    return lerp(g.xxx, float3(0.85, 0.03, 0.03) * (0.4 + 0.6 * saturate(c.r * 1.3)), red);
}
float ao(float2 uv, float2 px, float d) {
    if (p[7].x == 0 || p[3].x == 0 || d >= 0.99999) return 1;
    float zc = viewz(d), occ = 0;
    float r = 6 * p[3].y * p[7].w;
    float a0 = noise(uv * 917.0) * 6.2832;
    [unroll] for (int i = 0; i < 8; i++) {
        float a = a0 + i * 2.3999;
        float rr = r * (0.35 + 0.65 * frac(i * 0.618 + a0));
        float zn = viewz(rawz(uv + float2(cos(a), sin(a)) * rr * px));
        float diff = (zc - zn) / zc;
        occ += saturate(diff * 12) * saturate(1.5 - diff * 3);
    }
    return saturate(1 - occ / 8 * p[3].x);
}
float3 bloom(float2 uv, float2 px) {
    if (p[2].z == 0) return 0;
    float3 sum = 0;
    float r = 5 * p[7].w;
    [unroll] for (int i = 0; i < 12; i++) {
        float a = i * 0.5236;
        float2 o = float2(cos(a), sin(a)) * px;
        sum += max(texl(uv + o * r) - p[2].w, 0) + max(texl(uv + o * r * 2.6) - p[2].w, 0);
    }
    return sum / 24 * p[2].z * 2;
}
float edge(float2 uv, float2 px, float d) {
    float t = p[4].y;
    if (t <= 0) return 0;
    float2 o = px * t;
    float e = 0;
    if (p[7].x != 0 && d < 0.99999) {
        float zc = viewz(d);
        float zl = viewz(rawz(uv - float2(o.x, 0))), zr = viewz(rawz(uv + float2(o.x, 0)));
        float zu = viewz(rawz(uv - float2(0, o.y))), zd = viewz(rawz(uv + float2(0, o.y)));
        float dz = max(max(abs(zl - zc), abs(zr - zc)), max(abs(zu - zc), abs(zd - zc))) / zc;
        e = saturate((dz - 0.06) * 20);
    } else if (p[7].x != 0) {
        // sky: an edge where a neighbour is geometry
        float m = min(min(rawz(uv - float2(o.x, 0)), rawz(uv + float2(o.x, 0))), min(rawz(uv - float2(0, o.y)), rawz(uv + float2(0, o.y))));
        e = m < 0.99999 ? 1 : 0;
    }
    if (p[4].z != 0) {
        float gx = luma(tex(uv + float2(o.x, 0))) - luma(tex(uv - float2(o.x, 0)));
        float gy = luma(tex(uv + float2(0, o.y))) - luma(tex(uv - float2(0, o.y)));
        e = max(e, saturate((sqrt(gx * gx + gy * gy) - p[4].w) * 8));
    }
    return e;
}
// screen-space reflections: from each pixel of a reflective surface (the mask, its own depth: glass need not write
// the scene's), a ray mirrored about the surface's slope is marched through the scene's depth; what it meets on screen
// is mixed in, more at a grazing view, fading where the ray nears the picture's edge or its reach. The view is taken
// as p[8].z wide (the game's projection is in its own matrices): positions are then off by a scale, not in direction
float3 viewpos(float2 uv, float z) { return float3((uv * 2 - 1) * float2(p[8].w, -1) * p[8].z * z, z); }
float2 screenuv(float3 v) { return v.xy / (v.z * p[8].z * float2(p[8].w, -1)) * 0.5 + 0.5; }

float3 ssr(float2 uv, float2 px, float3 c) {
    if (p[8].x == 0) return c;
    float4 mk = maskc.SampleLevel(pt, uv, 0);
    float m = mk.r;
    if (m < 0.01) return c;
    if (p[9].z == 1) return lerp(c, float3(1, 0, 0), 0.5 * m);  // XBE_SSR_TEST=2: the mask shown
    float gz = mk.a;
    if (gz <= 0 || viewz(rawz(uv)) < gz * 0.995) return c;  // drawn over since by something nearer
    float3 P = viewpos(uv, gz);
    // the slope: the surface a pixel across and a pixel down, its distance as the mask gives its change
    float3 dx = viewpos(uv + float2(px.x, 0), gz * (1 + mk.g)) - P;
    float3 dy = viewpos(uv + float2(0, px.y), gz * (1 + mk.b)) - P;
    float3 N = normalize(cross(dx, dy));
    if (dot(N, P) > 0) N = -N;
    float3 V = normalize(P);
    float3 R = reflect(V, N);
    if (p[9].z == 2) return N * 0.5 + 0.5;  // XBE_SSR_TEST=3: the slope
    int steps = int(p[8].y);
    float len = gz * p[9].y / steps;
    float t = len * (0.5 + noise(uv * 731.0 + p[0].w));
    float3 hit = 0;
    float found = 0;
    [loop] for (int i = 0; i < steps; i++) {
        float3 Q = P + R * t;
        if (Q.z < gz * 0.02) break;  // nearly at the camera
        float2 q = screenuv(Q);
        if (any(q < 0) || any(q > 1)) break;
        float diff = Q.z - viewz(rawz(q));
        if (diff > 0 && diff < Q.z * p[9].x + len) {
            float a = t - len, b = t;
            [unroll] for (int k = 0; k < 5; k++) {
                float mid = (a + b) * 0.5;
                float3 M = P + R * mid;
                if (M.z - viewz(rawz(screenuv(M))) > 0) b = mid;
                else a = mid;
            }
            q = screenuv(P + R * b);
            float2 e = min(q, 1 - q);
            found = saturate(min(e.x, e.y) * 12) * (1 - float(i) / steps);
            hit = texl(q);
            break;
        }
        t += len;
        len *= 1.06;
    }
    if (p[9].z == 3) return float3(found <= 0, found, 0);  // XBE_SSR_TEST=4: red missed, green met
    if (found <= 0) return c;
    float fres = 0.3 + 0.7 * pow(1 - saturate(dot(-V, N)), 4);
    return lerp(c, hit, saturate(m * p[8].x * fres * found));
}
float4 ps(O i) : SV_Target {
    float2 px = p[0].yz;
    float2 uv = i.uv;
    int mode = int(p[0].x);
    if (mode == 3) {
        // retro: blocks of the console's pixels, fewer colours, dithered, graded
        float blk = max(p[6].x, 1);
        float2 cell = floor(i.pos.xy / blk);
        float3 c = tex((cell + 0.5) * blk * px);
        c = lerp(c, saturate(c * float3(1.08, 1.0, 0.82) + float3(0.02, 0.01, 0.0)), p[6].w);
        c = grade(c);
        float levels = p[6].y;
        float b = p[6].z != 0 ? (bayer[(int(cell.x) & 3) + (int(cell.y) & 3) * 4] + 0.5) / 16 - 0.5 : 0;
        c = saturate(floor(c * levels + 0.5 + b) / levels);
        return float4(c, 1);
    }
    float d = p[7].x != 0 ? rawz(uv) : 1;
    float3 c = ssr(uv, px, tex(uv));
    if (mode == 2 || mode == 4) {
        // toon: flat lighting bands, outlines, halftone in the shadows; noir the same in black, white and red
        float L = luma(c), bands = p[4].x;
        // bands even to the eye (in the square root of brightness), each pixel moved to its band's middle, by at most
        // a factor of two either way (a night scene keeps its darks, noise in them is not lifted into colour)
        float Lp = sqrt(L);
        float Lq = (floor(Lp * bands) + 0.5) / bands;
        Lq *= Lq;
        float3 banded = saturate(c * clamp(Lq / max(L, 1e-3), 0.5, 2.0));
        c = lerp(banded, c, 0.3);  // some of the texture's own shading kept: walls and faces stay legible
        if (p[5].w > 0) {
            float cell = 5 * p[7].w;
            float2 g = frac(i.pos.xy / cell) - 0.5;
            float dotr = sqrt(saturate(1 - Lq)) * 0.7;
            c *= 1 - p[5].w * 0.6 * (length(g) < dotr ? 1 : 0);
        }
        c = grade(c);
        if (mode == 4) c = noir(c, p[10].x != 0 && d < 0.99999);
        c = lerp(c, p[5].rgb, edge(uv, px, d));
        return float4(c, 1);
    }
    // enhanced
    if (p[2].y > 0) {
        float3 n = tex(uv + float2(px.x, 0)) + tex(uv - float2(px.x, 0)) + tex(uv + float2(0, px.y)) + tex(uv - float2(0, px.y));
        c = saturate(c + (c - n * 0.25) * p[2].y * 1.5);
    }
    c *= ao(uv, px, d);
    c += bloom(uv, px);
    c = grade(c);
    float v = length(uv - 0.5) * 1.414;
    c *= 1 - p[3].z * smoothstep(0.35, 1.0, v);
    c += (noise(uv * 1000 + p[0].w) - 0.5) * p[3].w * 0.12;
    return float4(saturate(c), 1);
}
)";

// light shafts and fog (VulkanBackend::AtmospherePass, RGBA16F images): src makes a quarter-size picture of what
// lights the air (the sky, by its brightness, and the brightest surfaces: lamps, windows) as colour times weight, a
// the weight; down halves it, four times, to a field whose slope points to the light; comp marches each pixel up that
// slope through the quarter picture (the shafts: what lies between it and the light, fading) and fogs it by its
// distance in the colour of the sky about it. b1 p: [0] 1/width, 1/height, shafts, fog; [1] W-buffer, seconds,
// 1/field width, 1/field height; [2] fog's density, the source's sky threshold, the light's reach, aspect.
// t0 the scene (src, comp) or the level below (down), t1 depth, t2 the quarter picture, t3 the field; s0 linear, s1 point
const char* kAtmoHlsl = R"(
cbuffer P : register(b1) { float4 p[4]; };
Texture2D t0 : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D src4 : register(t2);
Texture2D field : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float linz(float d) { return 1.0 / max(1.0 - d, 1e-6); }
float viewz(float d) { return p[1].x != 0 ? d * 4096 : linz(d); }
float noise(float2 v) { return frac(sin(dot(v, float2(12.9898, 78.233))) * 43758.5453); }
float4 src(O i) : SV_Target {
    float2 px = p[0].xy;
    float4 acc = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        float2 uv = i.uv + (float2(k & 1, k >> 1) - 0.5) * 2 * px;
        float3 c = t0.SampleLevel(ls, uv, 0).rgb;
        float d = depthtex.SampleLevel(pt, uv, 0), l = luma(c);
        float w = d >= 0.99999 ? smoothstep(p[2].y * 0.5, p[2].y + 0.3, l) : 0.8 * smoothstep(0.75, 0.95, l);
        acc += float4(c * w, w);
    }
    return acc / 4;
}
float4 down(O i) : SV_Target {
    float2 px = p[0].xy;
    float4 s = 0;
    [unroll] for (int k = 0; k < 4; k++) s += t0.SampleLevel(ls, i.uv + (float2(k & 1, k >> 1) - 0.5) * px, 0);
    return s / 4;
}
// the lights: texel i (0-3) the bright centre of the picture's quarter i (field-weighted uv, its weight), texel 4 the
// fog's colour: the sky's own as seen (every sky pixel alike, so a night sky stays dark; the lamps and windows that
// light the shafts would bleach a night street), else, no sky in view, the picture's mean; from the field (t0, p[1].zw
// its texel), at those places the depth (t1) and the scene (t2)
float4 pick(O i) : SV_Target {
    int k = int(i.pos.x);
    uint fw = uint(1 / p[1].z + 0.5), fh = uint(1 / p[1].w + 0.5);
    float4 acc = 0, skyacc = 0, all = 0;
    float2 best = 0.5;
    float most = 0;
    for (uint y = 0; y < fh; y++)
        for (uint x = 0; x < fw; x++) {
            float2 uv = (float2(x, y) + 0.5) * p[1].zw;
            if (k == 4) {
                float3 c = src4.SampleLevel(ls, uv, 0).rgb;
                all += float4(c, 1);
                if (depthtex.SampleLevel(pt, uv, 0) >= 0.99999) skyacc += float4(c, 1);
                continue;
            }
            if (uint(uv.x >= 0.5) != uint(k & 1) || uint(uv.y >= 0.5) != uint(k >> 1)) continue;
            float4 s = t0.Load(int3(x, y, 0));
            acc += s;
            if (s.a > most) most = s.a, best = uv;
        }
    if (k == 4) return float4(skyacc.a > all.a * 0.02 ? skyacc.rgb / skyacc.a : all.rgb / max(all.a, 1), skyacc.a / max(all.a, 1));
    return float4(best, most, 0);  // the quarter's brightest spot: its light (a lamp, an opening, the sun's sky)
}
float4 comp(O i) : SV_Target {
    float2 uv = i.uv;
    float3 c = t0.SampleLevel(pt, uv, 0).rgb;
    float d = depthtex.SampleLevel(pt, uv, 0);
    bool sky = d >= 0.99999;
    float3 skyc = field.Load(int3(4, 0, 0)).rgb;
    // the shafts: from each quarter's light toward this pixel through the quarter picture, what lies between cutting
    // them into rays (GPU Gems 3, 13)
    float3 sh = 0;
    if (p[0].z > 0) {
        float j = noise(uv * 613.0 + p[1].y);
        [unroll] for (int k = 0; k < 4; k++) {
            float4 L = field.Load(int3(k, 0, 0));
            if (L.z < 0.02) continue;
            float2 delta = (uv - L.xy) * (0.85 / 32);
            float2 q = uv - delta * j;
            float3 acc = 0;
            float decay = 1;
            [loop] for (int s = 0; s < 32; s++) {
                q -= delta;
                acc += src4.SampleLevel(ls, q, 0).rgb * decay;
                decay *= 0.965;
            }
            sh += acc / 32 * saturate(L.z * 4);
        }
        sh *= p[0].z * 1.6;
    }
    // the fog: by distance, the sky's colour, not over the sky itself; only over a W-buffer, whose depth is the
    // distance (a Z-buffer's, Driving.xbe's, is not metres without the projection: every pixel came out far, fogged
    // to the full strength)
    float z = sky ? 0 : viewz(d);
    float fog = sky || p[1].x == 0 ? 0 : (1 - exp(-z * p[2].x)) * p[0].w;
    float3 orig = c;
    c = lerp(c, skyc, saturate(fog));
    c += sh * (sky ? 0.35 : 1.0) * (1 - luma(c) * 0.8);  // into the darks more than over what is bright already
    int test = int(p[3].x);
    if (test == 1 && uv.x > 0.5) c = orig;              // XBE_ATMO_TEST=1: the left half with them, the right without
    if (test == 2) c = float3(saturate(fog), saturate(z / 200), sky ? 1 : 0);  // 2: the fog (r), distance / 200 (g)
    if (test == 3) c = sh;                               // 3: the shafts alone
    return float4(saturate(c), 1);
}
)";

// bounced light (VulkanBackend::BouncePass, GraphicsStyle::bounce_light): gather, at a quarter of the scene's size,
// averages the scene's colour over a disc about each pixel (16 golden-angle taps, about 2 m across at its distance),
// each tap by how near its distance is to the pixel's; comp lights the scene by it in its colour (the gathered light's
// own brightness mostly taken out, so a room takes its colour more than it brightens). b1 p: [0] 1/width, 1/height,
// strength, W-buffer; [1] aspect, seconds, test (XBE_GI_TEST=1 the left half with it, 2 the gathered light alone),
// the kept depth is there. t0 the scene, t1 its depth, t2 the gathered light, t3 the depth kept at a mid-frame clear
// (RtKeepDepth: the world's, the end's holds the gun alone); s0 linear, s1 point
const char* kBounceHlsl = R"(
cbuffer P : register(b1) { float4 p[2]; };
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D gathered : register(t2);
Texture2D<float> keptz : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float viewz(float d) { return p[0].w != 0 ? d * 4096 : 1.0 / max(1.0 - d, 1e-6); }
float depthat(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[1].w != 0 ? min(d, keptz.SampleLevel(pt, uv, 0)) : d;
}
float noise(float2 v) { return frac(sin(dot(v, float2(12.9898, 78.233))) * 43758.5453); }
float4 gather(O i) : SV_Target {
    float2 uv = i.uv;
    float d = depthat(uv);
    if (d >= 0.99999) return 0;
    float z = viewz(d);
    float r = clamp(0.866 * 1.0 / max(z, 0.1), 0.015, 0.22);  // a 1 m radius on screen (60 degrees tall)
    float j = noise(uv * 517.0 + p[1].y);
    float3 sum = 0;
    float wsum = 0;
    [unroll] for (int k = 0; k < 16; k++) {
        float a = (k + j) * 2.39996, rr = r * sqrt((k + 0.5) / 16);
        float2 q = uv + float2(cos(a) / p[1].x, sin(a)) * rr;
        if (any(q < 0) || any(q > 1)) continue;
        float ds = depthat(q);
        if (ds >= 0.99999) continue;
        float w = saturate(1 - abs(viewz(ds) - z) / (z * 0.5 + 0.5));
        sum += scene.SampleLevel(ls, q, 0).rgb * w;
        wsum += w;
    }
    return float4(wsum > 0 ? sum / wsum : 0, 1);
}
float4 comp(O i) : SV_Target {
    float2 uv = i.uv;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    float d = depthat(uv);
    int test = int(p[1].z);
    float3 g = gathered.SampleLevel(ls, uv, 0).rgb;
    if (test == 2) return float4(g, 1);
    if (d >= 0.99999 || (test == 1 && uv.x > 0.5)) return float4(c, 1);
    // the surroundings' light in their colour, their brightness mostly taken out: a tint, a little light
    float3 tint = g - luma(g) * 0.7;
    float3 lit = c * (1 + tint * 2.2 * p[0].z) + c * luma(g) * 0.25 * p[0].z;
    return float4(saturate(lit), 1);
}
)";

// depth of field (VulkanBackend::DofPass, GraphicsStyle::dof): focus, into a 1 x 1 image, eases the distance in focus
// (as 1 / metres, so a near focus moves as quickly as a far one) toward the nearest of five points about the middle
// of the view; blur gathers 32 golden-angle taps over a disc as wide as the pixel's blur, a tap nearer than the pixel
// only as far as its own blur reaches (an object in focus casts no halo over the blurred distance behind it). The
// world's distance is the depth kept at the game's mid-frame clear (RtKeepDepth); what was drawn after it (the gun
// and hands) stays sharp and is not gathered. b1 p: [0] 1/width, 1/height, mode (1 distance, 2 focus), the most blur
// in pixels; [1] distance mode's start and full (metres), focus's sharp band and gain; [2] W-buffer, the kept depth
// is there, the last frame's focus is there, its ease; [3] test (XBE_DOF_TEST=1 the left half with it, 2 the blur:
// white the most, blue the gun, red the focus point). t0 the scene, t1 its depth, t2 the kept depth, t3 the focus
// (the last frame's for focus); s0 linear, s1 point
const char* kDofHlsl = R"(
cbuffer P : register(b1) { float4 p[4]; };
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D<float> keptz : register(t2);
Texture2D focustex : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float viewz(float d) { return d >= 0.99999 ? 10000 : p[2].x != 0 ? d * 4096 : 1.0 / max(1.0 - d, 1e-6); }
bool gun(float2 uv) { return p[2].y != 0 && depthtex.SampleLevel(pt, uv, 0) < keptz.SampleLevel(pt, uv, 0) - 1e-6; }
// the world's distance: the depth kept at the clear, else the depth at the end
float worldz(float2 uv) { return viewz(p[2].y != 0 ? keptz.SampleLevel(pt, uv, 0) : depthtex.SampleLevel(pt, uv, 0)); }
float4 focus(O i) : SV_Target {
    float z = worldz(float2(0.5, 0.5));
    [unroll] for (int k = 0; k < 4; k++) {
        float2 o = float2(k == 0 ? 1 : k == 1 ? -1 : 0, k == 2 ? 1 : k == 3 ? -1 : 0) * float2(0.01 * p[0].x / p[0].y, 0.01);
        z = min(z, worldz(0.5 + o));
    }
    float want = 1 / clamp(z, 0.3, 2000), was = focustex.Load(int3(0, 0, 0)).x;
    float f = p[2].z != 0 && was > 0 ? lerp(was, want, p[2].w) : want;
    return float4(f, 1 / f, 0, 1);
}
// a distance's blur in pixels
float blurof(float z) {
    if (p[0].z < 1.5) return smoothstep(p[1].x, p[1].y, z) * p[0].w;
    float f = 1 / focustex.Load(int3(0, 0, 0)).x;
    return saturate((abs(1 - f / z) - p[1].z) * p[1].w) * p[0].w;
}
float4 blur(O i) : SV_Target {
    float2 uv = i.uv, px = p[0].xy;
    float3 c0 = scene.SampleLevel(pt, uv, 0).rgb;
    int test = int(p[3].x);
    if (test == 1 && uv.x > 0.5) return float4(c0, 1);
    bool g0 = gun(uv);
    float z0 = worldz(uv), r0 = g0 ? 0 : blurof(z0);
    if (test == 2) {
        float2 dc = (uv - 0.5) / px;
        if (dot(dc, dc) < 36) return float4(1, 0, 0, 1);
        return g0 ? float4(0, 0, 1, 1) : float4((r0 / max(p[0].w, 1)).xxx, 1);
    }
    if (r0 < 0.5) return float4(c0, 1);
    float3 sum = c0;
    float wsum = 1;
    [loop] for (int k = 0; k < 32; k++) {
        float a = k * 2.39996, rr = r0 * sqrt((k + 0.5) / 32);
        float2 q = uv + float2(cos(a), sin(a)) * rr * px;
        if (gun(q)) continue;
        float z = worldz(q);
        float w = z >= z0 * 0.97 ? 1 : saturate(blurof(z) - rr + 1);
        sum += scene.SampleLevel(ls, q, 0).rgb * w;
        wsum += w;
    }
    return float4(sum / wsum, 1);
}
)";

// ray tracing's capture (VulkanBackend::RtCapture): the draw's own vertex shader (renamed game_main), then where the
// vertex is in the view written to u0 at the push constant's base + its index less the draw's least one: its place on
// the screen times its distance (the acceleration structure's instance scales x and y by the view's width), the
// distance as the scene pass's viewz reads a depth, then the fog's share (RtRelightPass); 16 bytes on, its texture coordinates and diffuse colour (a cut-out's
// alpha, a reflection's colour where it is off screen). %s the shader's parameters, %s its argument.
const char* kCaptureMain = R"(
RWByteAddressBuffer rt_cap : register(u0);
struct RtCap { uint base; uint count; int vmin; uint pad; };
[[vk::push_constant]] RtCap rt_pc;
VOut main(%s, uint rt_vid : SV_VertexID) {
    VOut o = game_main(%s);
    int slot = int(rt_vid) - rt_pc.vmin;
    if (slot >= 0 && uint(slot) < rt_pc.count) {
        float3 v;
        if (hud.w != 0) {
            float k = wdepth.w * wdepth.z * 4096;  // W-buffered: x y w scale to metres alike
            v = float3(o.pos.xy, o.wlin) * k;
        } else {
            float w = abs(o.pos.w) > 1e-8 ? o.pos.w : 1e-8;
            float z = 1 / max(1 - o.pos.z / w, 1e-6);
            v = float3(o.pos.xy / w * z, z);
        }
        uint at = (rt_pc.base + uint(slot)) * 32;
        rt_cap.Store4(at, asuint(float4(v, saturate(o.fogamt))));
        uint4 col = uint4(saturate(o.d0) * 255 + 0.5);
        rt_cap.Store4(at + 16, uint4(asuint(o.tc0.xy), col.r | col.g << 8 | col.b << 16 | col.a << 24, 0));
    }
    return o;
}
)";

// what every ray traced pass reads (prefixed to kRtHlsl, kRtReflectHlsl and kLightsHlsl's RT variant): the frame's
// acceleration structure, its captured vertices (RtCapture: a place, then texture coordinates and diffuse colour, 32
// bytes), triangles (the opaque ones, then the cut-outs: the second geometry) and their materials (RtMaterial), and
// the textures those name. RT_PROCEED(q) runs a query, a cut-out's candidate hit kept where its texture's alpha
// passes its alpha test; rt_tri gives a hit's triangle in the frame's list.
const char* kRtCommonHlsl = R"(
[[vk::binding(11, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(12, 0)]] ByteAddressBuffer rt_verts;
[[vk::binding(13, 0)]] ByteAddressBuffer rt_tris;
[[vk::binding(14, 0)]] ByteAddressBuffer rt_mats;
[[vk::binding(0, 1)]] Texture2D rt_tex[1024];
[[vk::binding(1, 1)]] SamplerState rt_smp;
// a hit's triangle: the world's instance (custom index 0) its opaque geometry then its cut-outs, the gun's (1) after
uint rt_tri(uint inst, uint geom, uint prim) {
    return prim + (inst != 0 ? rt_mats.Load(4) : geom != 0 ? rt_mats.Load(0) : 0);
}
uint rt_mat(uint tri) { return rt_mats.Load(8 + tri * 4); }
float3 rt_place(uint v) { return asfloat(rt_verts.Load3(v * 32)); }
float4 rt_unpack(uint c) { return float4(c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24) / 255.0; }
// a point of a triangle: its texture coordinates and diffuse colour
void rt_attr(uint tri, float2 bary, out float2 uv, out float4 col) {
    uint3 t = rt_tris.Load3(tri * 12);
    uint4 a = rt_verts.Load4(t.x * 32 + 16), b = rt_verts.Load4(t.y * 32 + 16), c = rt_verts.Load4(t.z * 32 + 16);
    float w0 = 1 - bary.x - bary.y;
    uv = asfloat(a.xy) * w0 + asfloat(b.xy) * bary.x + asfloat(c.xy) * bary.y;
    col = rt_unpack(a.z) * w0 + rt_unpack(b.z) * bary.x + rt_unpack(c.z) * bary.y;
}
// the texture's colour there (white without one)
float4 rt_texel(uint mat, float2 uv) {
    uint slot = mat & 0xFFF;
    return slot == 0xFFF ? 1 : rt_tex[NonUniformResourceIndex(slot)].SampleLevel(rt_smp, uv, 0);
}
// a cut-out's alpha test (D3D's comparisons, Cmp() less 1) on its texture's alpha
bool rt_solid(uint inst, uint geom, uint prim, float2 bary) {
    uint tri = rt_tri(inst, geom, prim), mat = rt_mat(tri);
    float2 uv;
    float4 col;
    rt_attr(tri, bary, uv, col);
    float a = floor(rt_texel(mat, uv).a * 255 + 0.5), ref = float((mat >> 12) & 255);
    switch ((mat >> 20) & 7) {
    case 0: return false;
    case 1: return a < ref;
    case 2: return a == ref;
    case 3: return a <= ref;
    case 4: return a > ref;
    case 5: return a != ref;
    case 6: return a >= ref;
    default: return true;
    }
}
#define RT_PROCEED(q) \
    while (q.Proceed()) \
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && \
            rt_solid(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), \
                     q.CandidateTriangleBarycentrics())) \
            q.CommitNonOpaqueTriangleHit();
)";

// ray traced ambient occlusion (VulkanBackend::RtPass): trace, at half the scene's size, a pixel's rays (cosine
// weighted over the hemisphere about its normal, which the depth's neighbours give) against the frame's acceleration
// structure: r what is open, g its distance; comp, at the scene's size, the 4x4 traced pixels the rays' pattern
// repeats over averaged by how near their distance is, the scene darkened by it. b1 p: [0] 1/width, 1/height, tan of half the vertical view, width / height; [1] reach (metres),
// strength, rays, W-buffer; [2] test (XBE_RT_TEST=1 the left half with it, 2 the occlusion alone, 3 a ray from the
// camera: green where it meets the depth, red nearer, blue farther), the kept depth is there, 1 / the traced size. t0 the scene, t1 its
// depth, t2 the traced occlusion, t3 the depth kept at a mid-frame clear (RtKeepDepth); s1 point. Shader model 6.5.
const char* kRtHlsl = R"(
cbuffer P : register(b1) { float4 p[3]; };
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D aotex : register(t2);
Texture2D<float> keptz : register(t3);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
static const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
float viewz(float d) { return p[1].w != 0 ? d * 4096 : 1.0 / max(1.0 - d, 1e-6); }
// the scene's depth: the nearer of the end's and the one kept when the game cleared it (p[2].y)
float depthat(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[2].y != 0 ? min(d, keptz.SampleLevel(pt, uv, 0)) : d;
}
// drawn after the mid-frame clear (the gun and hands): nearer than the world kept there. Its rays meet the world and
// itself (the instance masks: the world 1, the gun 2); the world's leave the gun out (it reaches into walls)
bool aftertheclear(float2 uv) {
    return p[2].y != 0 && depthtex.SampleLevel(pt, uv, 0) < keptz.SampleLevel(pt, uv, 0) - 1e-6;
}
// the view space the captured triangles are in: x right, y up, z ahead, in metres for a W-buffer
float3 viewpos(float2 uv) {
    float z = viewz(depthat(uv));
    return float3((uv.x * 2 - 1) * p[0].z * p[0].w, (1 - uv.y * 2) * p[0].z, 1) * z;
}
float4 trace(O i) : SV_Target {
    float2 uv = i.uv, px = p[0].xy;
    float d = depthat(uv);
    if (d >= 0.99999) return float4(1, 0, 0, 1);
    float3 P = viewpos(uv);
    uint mask = aftertheclear(uv) ? 0xFF : 1;
    if (int(p[2].x) == 3) {
        RayDesc pr;
        pr.Origin = 0;
        pr.Direction = normalize(P);
        pr.TMin = 0;
        pr.TMax = 1e5;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q0;
        q0.TraceRayInline(tlas, RAY_FLAG_NONE, mask, pr);
        RT_PROCEED(q0)
        if (q0.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return float4(0, 0, 0, 1);
        float e = (q0.CommittedRayT() - length(P)) / length(P);
        return float4(saturate(-e * 20), saturate(1 - abs(e) * 20), saturate(e * 20), 1);
    }
    // the surface's own triangle, where a ray from the camera meets it about the depth's place: its exact place and
    // slope (the depth's neighbours give a wrong one where the depth steps, flat ground far off, and rays from it
    // then meet the ground they leave: dark lines across it)
    float3 n = 0;
    {
        float lp = length(P);
        RayDesc pr;
        pr.Origin = 0;
        pr.Direction = P / lp;
        pr.TMin = lp * 0.9;
        pr.TMax = lp * 1.1;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q1;
        q1.TraceRayInline(tlas, RAY_FLAG_NONE, mask, pr);
        RT_PROCEED(q1)
        if (q1.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            uint hit = rt_tri(q1.CommittedInstanceID(), q1.CommittedGeometryIndex(), q1.CommittedPrimitiveIndex());
            uint3 tri = rt_tris.Load3(hit * 12);
            float3 s = float3(p[0].z * p[0].w, p[0].z, 1);  // the instance's scale (RtBuild)
            float3 v0 = rt_place(tri.x) * s, v1 = rt_place(tri.y) * s, v2 = rt_place(tri.z) * s;
            n = cross(v1 - v0, v2 - v0);
            P = pr.Direction * q1.CommittedRayT();
        }
    }
    if (dot(n, n) < 1e-20) {
        // not traced (a draw left out): the slope from the nearer neighbour each way (an edge's far side would bend it)
        float3 l = viewpos(uv - float2(px.x, 0)), r = viewpos(uv + float2(px.x, 0));
        float3 u = viewpos(uv - float2(0, px.y)), b = viewpos(uv + float2(0, px.y));
        float3 dx = abs(r.z - P.z) < abs(P.z - l.z) ? r - P : P - l;
        float3 dy = abs(b.z - P.z) < abs(P.z - u.z) ? b - P : P - u;
        n = cross(dx, dy);
    }
    if (dot(n, n) < 1e-20) return float4(1, P.z, 0, 1);
    n = normalize(n);
    if (dot(n, P) > 0) n = -n;  // toward the camera, at the origin
    float3 t = normalize(cross(abs(n.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), n));
    float3 bt = cross(n, t);
    float3 o = P + n * (0.01 + 0.002 * P.z);
    uint2 ip = uint2(i.pos.xy);
    // two 4x4 patterns, so any 4x4 pixels (what comp averages) hold the same set of directions: no pattern left
    float a0 = (bayer[(ip.y & 3) * 4 + (ip.x & 3)] + 0.5) / 16.0;
    float b0 = (bayer[((ip.x + 2) & 3) * 4 + ((ip.y + 1) & 3)] + 0.5) / 16.0;
    int rays = int(p[1].z);
    float reach = p[1].x, occ = 0;
    [loop] for (int k = 0; k < rays; k++) {
        float phi = 6.2831853 * frac(a0 + k * 0.618034);
        float r2 = frac(b0 + float(k) / rays);
        float sr = sqrt(r2);
        RayDesc ray;
        ray.Origin = o;
        ray.Direction = t * (cos(phi) * sr) + bt * (sin(phi) * sr) + n * sqrt(1 - r2);
        ray.TMin = 0;
        ray.TMax = reach;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        q.TraceRayInline(tlas, RAY_FLAG_NONE, mask, ray);
        RT_PROCEED(q)
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            float f = q.CommittedRayT() / reach;
            occ += 1 - f * f;
        }
    }
    return float4(1 - occ / rays, P.z, 0, 1);
}
float4 comp(O i) : SV_Target {
    float2 uv = i.uv, px = p[0].xy;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    float d = depthat(uv);
    int test = int(p[2].x);
    if (d >= 0.99999) return test >= 2 ? float4(1, 1, 1, 1) : float4(c, 1);
    float z = viewz(d), sum = 0, wsum = 0;
    [unroll] for (int y = -2; y < 2; y++) {
        [unroll] for (int x = -2; x < 2; x++) {
            float4 a = aotex.SampleLevel(pt, uv + (float2(x, y) + 0.5) * p[2].zw, 0);
            float w = saturate(1 - abs(a.g - z) / (z * 0.04 + 0.02));
            sum += a.r * w;
            wsum += w;
        }
    }
    float ao = wsum > 0 ? sum / wsum : 1;
    ao = pow(saturate(ao), 2 * p[1].y);  // contact shading that reads at a glance: half open at strength 1 is a quarter
    if (test == 3) return aotex.SampleLevel(pt, uv, 0);
    if (test == 2) return float4(ao.xxx, 1);
    if (test == 1 && uv.x > 0.5) return float4(c, 1);
    return float4(c * ao, 1);
}
)";

// ray traced reflections (VulkanBackend::RtReflectPass; kRtCommonHlsl before it): from each pixel of a reflective
// surface (the reflection mask, its own distance: glass need not write the scene's depth), a ray mirrored about the
// surface's slope (its own triangle's where it is traced, else the mask's) against the frame's acceleration
// structure. What it meets is the scene's colour where that point shows on screen (lit as the game lit it), else its
// texture by its diffuse colour, brought to the scene's brightness about the surface; mixed in more at a grazing
// view, as the screen-space reflections are (which this takes the place of). b1 p: [0] 1/width, 1/height, tan of
// half the vertical view, width / height; [1] strength, W-buffer, test (XBE_RT_TEST=4: green met on screen, blue off
// it, red missed), the lit texture's scale; [2] x the kept depth is there. t0 the scene, t1 its depth, t2 the
// reflection mask, t3 the depth kept at a mid-frame clear (RtKeepDepth: the world's, the end's holds the gun alone); s0
// linear, s1 point. Shader model 6.5.
const char* kRtReflectHlsl = R"(
cbuffer P : register(b1) { float4 p[3]; };
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D maskc : register(t2);
Texture2D<float> keptz : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float viewz(float d) { return p[1].y != 0 ? d * 4096 : 1.0 / max(1.0 - d, 1e-6); }
float depthat(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[2].x != 0 ? min(d, keptz.SampleLevel(pt, uv, 0)) : d;
}
float3 viewpos(float2 uv, float z) { return float3((uv * 2 - 1) * float2(p[0].w, -1) * p[0].z * z, z); }
float2 screenuv(float3 v) { return v.xy / (v.z * p[0].z * float2(p[0].w, -1)) * 0.5 + 0.5; }
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 ps(O i) : SV_Target {
    float2 uv = i.uv, px = p[0].xy;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    float4 mk = maskc.SampleLevel(pt, uv, 0);
    float m = mk.r, gz = mk.a;
    if (m < 0.01 || gz <= 0 || viewz(depthat(uv)) < gz * 0.995) return float4(c, 1);
    float3 P = viewpos(uv, gz);
    // the slope: the surface's own triangle where a ray from the camera meets it, else the mask's
    float3 N = 0;
    {
        float lp = length(P);
        RayDesc pr;
        pr.Origin = 0;
        pr.Direction = P / lp;
        pr.TMin = lp * 0.97;
        pr.TMax = lp * 1.03;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q1;
        q1.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, pr);
        RT_PROCEED(q1)
        if (q1.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            uint hit = rt_tri(q1.CommittedInstanceID(), q1.CommittedGeometryIndex(), q1.CommittedPrimitiveIndex());
            uint3 tri = rt_tris.Load3(hit * 12);
            float3 s = float3(p[0].z * p[0].w, p[0].z, 1);  // the instance's scale (RtBuild)
            float3 v0 = rt_place(tri.x) * s, v1 = rt_place(tri.y) * s, v2 = rt_place(tri.z) * s;
            N = cross(v1 - v0, v2 - v0);
            P = pr.Direction * q1.CommittedRayT();
        }
    }
    if (dot(N, N) < 1e-20) {
        float3 dx = viewpos(uv + float2(px.x, 0), gz * (1 + mk.g)) - P;
        float3 dy = viewpos(uv + float2(0, px.y), gz * (1 + mk.b)) - P;
        N = cross(dx, dy);
    }
    if (dot(N, N) < 1e-20) return float4(c, 1);
    N = normalize(N);
    if (dot(N, P) > 0) N = -N;
    float3 V = normalize(P);
    float3 R = reflect(V, N);
    RayDesc ray;
    ray.Origin = P + N * (0.01 + 0.002 * P.z);
    ray.Direction = R;
    ray.TMin = 0;
    ray.TMax = 1e5;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 1, ray);  // the world: the gun is not where it is drawn
    RT_PROCEED(q)
    int test = int(p[1].z);
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return float4(test == 4 ? float3(1, 0, 0) : c, 1);
    float t = q.CommittedRayT();
    float3 H = ray.Origin + R * t;
    float3 hit;
    float2 hq = screenuv(H);
    bool shown = false;
    if (H.z > 0.05 && all(hq > 0) && all(hq < 1)) {
        float hz = viewz(depthat(hq));
        shown = abs(hz - H.z) < H.z * 0.03 + 0.05;
    }
    if (shown) {
        hit = scene.SampleLevel(ls, hq, 0).rgb;
    } else {
        // off screen or hidden: its texture by its diffuse colour, as bright as the scene is about the surface
        uint tri = rt_tri(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex());
        float2 tuv;
        float4 col;
        rt_attr(tri, q.CommittedTriangleBarycentrics(), tuv, col);
        float3 albedo = rt_texel(rt_mat(tri), tuv).rgb * col.rgb;
        float around = 0;
        [unroll] for (int k = 0; k < 9; k++)
            around += luma(scene.SampleLevel(ls, uv + (float2(k % 3, k / 3) - 1) * 0.04, 0).rgb) / 9;
        hit = saturate(albedo * around * p[1].w);
    }
    if (test == 4) return float4(shown ? float3(0, 1, 0) : float3(0, 0, 1), 1);
    float fres = 0.3 + 0.7 * pow(1 - saturate(dot(-V, N)), 4);
    return float4(lerp(c, hit, saturate(m * p[1].x * fres)), 1);
}
)";

// the reflection mask (VulkanBackend::DrawMask, RGBA16F): the draw's own pixel shader (renamed game_main) for its
// coverage, out r its strength (DrawConstants::style z; w 1 for a surface drawn opaque, all of it covered) by that,
// g b how its distance changes a pixel across and down (in parts of it: the surface's slope, exact where the depth
// buffer's W is whole units), a its distance as the scene pass's viewz reads a depth
const char* kMaskMain = R"(
float4 main(VOut i, bool front : SV_IsFrontFace) : SV_Target {
    float4 c = game_main(i, front);
    float cover = style.w > 0 ? 1 : smoothstep(0.02, 0.12, c.a);
    if (cover <= 0) discard;
    float vz = 1 / max(1 - i.pos.z, 1e-6);
    return float4(style.z * cover, ddx_fine(vz) / vz, ddy_fine(vz) / vz, vz);
}
)";
const char* kMaskMainW = R"(
float4 main(VOut i, bool front : SV_IsFrontFace, out float depth : SV_Depth) : SV_Target {
    float4 c = game_main(i, front, depth);
    float cover = style.w > 0 ? 1 : smoothstep(0.02, 0.12, c.a);
    if (cover <= 0) discard;
    float vz = i.wlin * wdepth.w * wdepth.z * 4096;
    return float4(style.z * cover, ddx_fine(vz) / vz, ddy_fine(vz) / vz, vz);
}
)";
// effect lights (VulkanBackend::DrawGlow, RGBA16F, added): the draw's own pixel shader (renamed game_main), what it
// gives off (its colour, by its alpha when the game adds it so), a its brightness times its distance (as the scene
// pass's viewz reads a depth); depth tested against the scene, not written
const char* kGlowMain = R"(
float4 main(VOut i, bool front : SV_IsFrontFace%s) : SV_Target {
    float4 c = game_main(i, front%s);
    float3 e = %s;
    float l = dot(e, float3(0.3, 0.59, 0.11));
    if (l < 0.01) discard;
    return float4(e, l * %s);
}
)";
// the level lit again (VulkanBackend::RtRelightPass, GraphicsStyle::rt_lighting; kRtCommonHlsl before it). The level's
// light is baked into its vertex colours (its draws: texture x diffuse x 2), which the capture keeps with each vertex
// (and the fog's share at the vertex, after its place): a ray from the camera finds the pixel's triangle, the baked
// light there is taken out and the new put in, as a ratio (so decals and what was blended over it keep theirs), the
// fog's share held apart. The new light: a little of the baked (the bounced light it had), the game's own lights
// (SetSceneLights: their places in the ray traced view) with traced shadows. Outdoors (a ray up from the surface meets
// nothing: the capture leaves the sky out) the baked light is kept, darker in shade and brighter where a ray toward
// the sun is open, so a night level stays a night. b1 p: [0] 1/width, 1/height, tan of half the vertical view,
// width / height; [1] the lights, the kept depth is there, test (XBE_RT_TEST=5 the light's change, halved, 6 the
// lights as dots), the outdoor baked light added up into u (binding 15: red, green, blue in 64ths, pixels; every
// eighth pixel each way); [2] toward the sun, its strength; [3] the sun's colour, the baked light's share indoors; [4] the
// fog's colour, the lights' scale; [5] up, the shade's share outdoors; [6] the least light; [7] the bounced light's
// strength (0 none), its rays a pixel, 1 / its image's width, height; [8 + 2n] a light's place and reach, [9 + 2n] its
// colour. t0 the scene, t1 its depth, t2 the depth kept at a mid-frame clear (RtKeepDepth), t3 the bounced light; s1
// point. Shader model 6.5.
// gi (GraphicsStyle::rt_gi, at half the scene's size, RGBA16F): the light bounced once onto a pixel's surface, in the
// baked light's units: rays cosine weighted about its slope (two 4x4 patterns, as the occlusion's), each met surface's
// texture by the light on it (a share of its baked light, the sun with its shadow, the game's lights unshadowed); a
// the surface's distance, which ps weighs the 4x4 traced pixels about it by (XBE_RT_TEST=7: the bounced light alone).
const char* kRtRelightHlsl = R"(
cbuffer P : register(b1) { float4 p[8 + 2 * 64]; };
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D<float> keptz : register(t2);
Texture2D gitex : register(t3);
SamplerState pt : register(s1);
#if STATS
[[vk::binding(15, 0)]] RWByteAddressBuffer stats;
#endif
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float depthat(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[1].y != 0 ? min(d, keptz.SampleLevel(pt, uv, 0)) : d;
}
bool aftertheclear(float2 uv) {
    return p[1].y != 0 && depthtex.SampleLevel(pt, uv, 0) < keptz.SampleLevel(pt, uv, 0) - 1e-6;
}
float3 viewpos(float2 uv, float z) { return float3((uv * 2 - 1) * float2(p[0].w, -1) * p[0].z * z, z); }
float2 screenuv(float3 v) { return v.xy / (v.z * p[0].z * float2(p[0].w, -1)) * 0.5 + 0.5; }
static const float kFar = 2000;
// open toward d as far as tmax: nothing of the world's in the way
float open(float3 o, float3 d, float tmax) {
    RayDesc r;
    r.Origin = o;
    r.Direction = d;
    r.TMin = 0;
    r.TMax = tmax;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 1, r);
    RT_PROCEED(q)
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
}
// the pixel's surface, its triangle where a ray from the camera meets it about the depth's place: its place, slope
// (toward the camera), baked light and the fog's share there. 0 none there (the sky, the gun), 1 not traced, 2 found
int surface(float2 uv, out float3 P, out float3 N, out float3 baked, out float f) {
    P = 0, N = 0, baked = 0, f = 1;
    float d = depthat(uv);
    if (d >= 0.99999 || d * 4096 > kFar || aftertheclear(uv)) return 0;
    P = viewpos(uv, d * 4096);
    float lp = length(P);
    RayDesc pr;
    pr.Origin = 0;
    pr.Direction = P / lp;
    pr.TMin = lp * 0.95;
    pr.TMax = lp * 1.05;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 1, pr);
    RT_PROCEED(q)
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return 1;
    uint hit = rt_tri(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex());
    uint3 t = rt_tris.Load3(hit * 12);
    float2 bary = q.CommittedTriangleBarycentrics(), tuv;
    float4 col;
    rt_attr(hit, bary, tuv, col);
    float w0 = 1 - bary.x - bary.y;
    f = asfloat(rt_verts.Load(t.x * 32 + 12)) * w0 + asfloat(rt_verts.Load(t.y * 32 + 12)) * bary.x +
        asfloat(rt_verts.Load(t.z * 32 + 12)) * bary.y;
    float3 s = float3(p[0].z * p[0].w, p[0].z, 1);  // the instance's scale (RtBuild)
    float3 v0 = rt_place(t.x) * s, v1 = rt_place(t.y) * s, v2 = rt_place(t.z) * s;
    N = cross(v1 - v0, v2 - v0);
    if (dot(N, N) < 1e-20) return 1;
    N = normalize(N);
    if (dot(N, P) > 0) N = -N;
    P = pr.Direction * q.CommittedRayT();
    baked = col.rgb * 2;
    return 2;
}
// the game's lights on a point (o a little off it), each with its shadow where shadows
float3 lamps(float3 P, float3 N, float3 o, bool shadows) {
    float3 added = 0;
    [loop] for (int n = 0; n < int(p[1].x); n++) {
        float4 a = p[8 + 2 * n];
        float3 D = a.xyz - P;
        float d2 = dot(D, D), r2 = a.w * a.w;
        if (d2 >= r2) continue;
        float dl = sqrt(d2);
        float ndl = dot(N, D / max(dl, 1e-4));
        if (ndl <= 0) continue;
        float att = 1 - sqrt(d2 / r2);
        float3 e = p[9 + 2 * n].rgb * (att * att * ndl * p[4].w);
        if (dot(e, float3(0.3, 0.59, 0.11)) < 0.004) continue;
        added += shadows ? e * open(o, D / dl, dl * 0.95) : e;
    }
    return added;
}
static const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
float4 gi(O i) : SV_Target {
    float3 P, N, baked;
    float f;
    if (surface(i.uv, P, N, baked, f) != 2) return 0;
    float3 t = normalize(cross(abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), N));
    float3 bt = cross(N, t);
    float3 o = P + N * (0.01 + 0.002 * P.z);
    uint2 ip = uint2(i.pos.xy);
    float a0 = (bayer[(ip.y & 3) * 4 + (ip.x & 3)] + 0.5) / 16.0;
    float b0 = (bayer[((ip.x + 2) & 3) * 4 + ((ip.y + 1) & 3)] + 0.5) / 16.0;
    float3 s = float3(p[0].z * p[0].w, p[0].z, 1);
    int rays = int(p[7].y);
    float3 sum = 0;
    [loop] for (int k = 0; k < rays; k++) {
        float phi = 6.2831853 * frac(a0 + k * 0.618034);
        float r2 = frac(b0 + float(k) / rays);
        float sr = sqrt(r2);
        RayDesc r;
        r.Origin = o;
        r.Direction = t * (cos(phi) * sr) + bt * (sin(phi) * sr) + N * sqrt(1 - r2);
        r.TMin = 0;
        r.TMax = kFar;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        q.TraceRayInline(tlas, RAY_FLAG_NONE, 1, r);
        RT_PROCEED(q)
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) continue;
        uint tri = rt_tri(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex());
        float2 tuv;
        float4 col;
        rt_attr(tri, q.CommittedTriangleBarycentrics(), tuv, col);
        uint3 tv = rt_tris.Load3(tri * 12);
        float3 v0 = rt_place(tv.x) * s, v1 = rt_place(tv.y) * s, v2 = rt_place(tv.z) * s;
        float3 n = cross(v1 - v0, v2 - v0);
        if (dot(n, n) < 1e-20) continue;
        n = normalize(n);
        if (dot(n, r.Direction) > 0) n = -n;
        float3 H = o + r.Direction * q.CommittedRayT();
        float3 oh = H + n * (0.01 + 0.002 * abs(H.z));
        float3 hb = col.rgb * 2;
        float3 light = hb * p[3].w + p[6].rgb + lamps(H, n, oh, false);
        float sl = saturate(dot(n, p[2].xyz));
        if (sl > 0 && p[2].w > 0) light += hb * p[3].rgb * (0.75 * p[2].w * sl) * open(oh, p[2].xyz, kFar);
        sum += rt_texel(rt_mat(tri), tuv).rgb * light;
    }
    return float4(sum / max(rays, 1) * p[7].x, P.z);
}
float4 ps(O i) : SV_Target {
    float2 uv = i.uv;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    int test = int(p[1].z);
    if (test == 6) {
        for (int n = 0; n < int(p[1].x); n++) {
            float3 l = p[8 + 2 * n].xyz;
            if (l.z > 0.1 && length((uv - screenuv(l)) * float2(p[0].w, 1)) < 0.01) return float4(saturate(p[9 + 2 * n].rgb * 4), 1);
        }
    }
    float3 P, N, baked;
    float f;
    int found = surface(uv, P, N, baked, f);
    if (found == 0) return float4(test >= 5 ? c * 0.25 : c, 1);
    if (found == 1) return float4(test == 5 ? float3(1, 0, 1) : c, 1);
    float3 o = P + N * (0.01 + 0.002 * P.z);
    // the new light, in the baked light's units (1 the texture as it is): indoors a little of the baked (its bounce)
    // and the game's lights, each with its shadow; the bounced light traced (gi) where it is on
    float3 added = lamps(P, N, o, true);
    if (p[7].x > 0) {
        float3 b = 0;
        float wsum = 0;
        [unroll] for (int y = -2; y < 2; y++) {
            [unroll] for (int x = -2; x < 2; x++) {
                float4 g = gitex.SampleLevel(pt, uv + (float2(x, y) + 0.5) * p[7].zw, 0);
                float w = g.a > 0 ? saturate(1 - abs(g.a - P.z) / (P.z * 0.04 + 0.02)) : 0;
                b += g.rgb * w;
                wsum += w;
            }
        }
        b = wsum > 0 ? b / wsum : 0;
        if (test == 7) return float4(saturate(b), 1);
        added += b;
    }
    float3 ratio = (baked * p[3].w + p[6].rgb + added) / max(baked, 0.04);
    // outdoors (open to the sky above it): the baked light kept, in shade a part of it, in the sun more
    float3 up = normalize(p[5].xyz + N * 0.5);
    if (open(o, up, kFar) > 0) {
        float3 sun = p[2].xyz;
        float sl = saturate(dot(N, sun));
        if (sl > 0 && p[2].w > 0) sl *= open(o, sun, kFar);
        ratio = p[5].w + p[3].rgb * (0.75 * p[2].w * sl) + added / max(baked, 0.04);
#if STATS
        if (p[1].w != 0 && all((uint2(i.pos.xy) & 7) == 0)) {  // the outdoor baked light added up (the sun's colour)
            uint3 b = uint3(min(baked, 4) * 64 + 0.5);
            stats.InterlockedAdd(0, b.r);
            stats.InterlockedAdd(4, b.g);
            stats.InterlockedAdd(8, b.b);
            stats.InterlockedAdd(12, 1);
        }
#endif
    }
    ratio = clamp(ratio, 0, 4);
    if (test == 5 || test == 6) return float4(ratio * 0.5, 1);
    if (f < 0.03) return float4(c, 1);
    float3 fogc = p[4].rgb;
    float3 L = saturate((c - fogc * (1 - f)) / f);
    float3 relit = L * ratio;
    return float4(saturate(lerp(fogc, relit, f)), 1);
}
)";

// effect lights' passes (VulkanBackend::LightPass). tilea / tileb: a tile of the glow image each (every second pixel
// each way): a its mean colour and brightness; b where its light is, the brightness' centre (the screen's fractions)
// and its mean distance. light: each lit tile a point light at that place, a little nearer than what glows; a
// pixel's position and slope from the scene's depth (an assumed view of p[1].y, as the reflections take it). A tile's
// light grows with its distance squared (what covers a tile far away is larger), falls off with the square of the
// distance to the surface; the scene's own colour is what it lights. p[0] the glow's size, the lights listed (w); p[1] strength,
// tan of half the vertical view, width / height, W-buffer; p[2] the tiles across and down, the least brightness, the
// test view; p[3] 1 / width, 1 / height, the lights' power, the kept depth is there. t0 the scene (or the glow), t1
// its depth, t2 t3 the tiles (light: t3 the depth kept at a mid-frame clear, RtKeepDepth: the world's, the end's holds
// the gun alone).
const char* kLightsHlsl = R"(
cbuffer P : register(b1) { float4 p[4]; };
// RT (GraphicsStyle::rt_shadows; kRtCommonHlsl before it): each light's ray from the surface, against the frame's
// acceleration structure; the depth as the plain pass reads it, so only shadows change
Texture2D t0 : register(t0);
Texture2D<float> t1 : register(t1);
Texture2D t2 : register(t2);
Texture2D t3 : register(t3);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
void sums(float2 tpos, out float4 a, out float4 b) {
    int2 t = int2(tpos);
    float2 tile = p[0].xy / p[2].xy;
    int2 o = int2(t * tile), e = int2(min((t + 1) * tile, p[0].xy));
    float3 rgb = 0;
    float l = 0, lu = 0, lv = 0, lz = 0, n = 0;
    [loop] for (int y = o.y; y < e.y; y += 2)
        [loop] for (int x = o.x; x < e.x; x += 2) {
            float4 g = t0.Load(int3(x, y, 0));
            float w = dot(g.rgb, float3(0.3, 0.59, 0.11));
            rgb += g.rgb;
            l += w;
            lu += w * (x + 0.5);
            lv += w * (y + 0.5);
            lz += g.a;
            n += 1;
        }
    a = float4(rgb, l) / max(n, 1);
    b = l > 1e-5 ? float4(lu / l / p[0].x, lv / l / p[0].y, lz / l, 0) : 0;
}
float4 tilea(O i) : SV_Target { float4 a, b; sums(i.pos.xy, a, b); return a; }
float4 tileb(O i) : SV_Target { float4 a, b; sums(i.pos.xy, a, b); return b; }
// pick: the brightest tiles listed, brightest first: pixel 2n the n-th's a, 2n + 1 its b (t2 t3 the tiles); the light
// pass reads the list from t2 and goes through p[0].w of them
float4 pick(O i) : SV_Target {
    int px = int(i.pos.x), want = px >> 1, tx = int(p[2].x), n = tx * int(p[2].y);
    [loop] for (int j = 0; j < n; j++) {
        float lj = t2.Load(int3(j % tx, j / tx, 0)).w;
        int rank = 0;
        [loop] for (int k = 0; k < n; k++) {
            float lk = t2.Load(int3(k % tx, k / tx, 0)).w;
            rank += lk > lj || (lk == lj && k < j) ? 1 : 0;
        }
        if (rank == want) return (px & 1) ? t3.Load(int3(j % tx, j / tx, 0)) : t2.Load(int3(j % tx, j / tx, 0));
    }
    return 0;
}
float viewz(float d) { return p[1].w != 0 ? d * 4096 : 1.0 / max(1.0 - d, 1e-6); }
float3 viewpos(float2 uv, float z) { return float3((uv * 2 - 1) * float2(p[1].z, -1) * p[1].y * z, z); }
float depthat(float2 uv) {
    float d = t1.SampleLevel(pt, uv, 0);
    return p[3].w != 0 ? min(d, t3.SampleLevel(pt, uv, 0).r) : d;
}
float3 at(float2 uv) { return viewpos(uv, viewz(depthat(uv))); }
float4 light(O i) : SV_Target {
    float2 uv = i.uv, px = p[3].xy;
    float3 c = t0.SampleLevel(pt, uv, 0).rgb;
    float d = depthat(uv);
    if (p[2].w == 2) {  // XBE_LIGHT_TEST=2: the listed lights as dots where they are
        float3 m = 0;
        [loop] for (int n = 0; n < int(p[0].w); n++) {
            float4 a = t2.Load(int3(2 * n, 0, 0)), b = t2.Load(int3(2 * n + 1, 0, 0));
            if (a.w < p[2].z) break;
            if (length((uv - b.xy) * float2(p[1].z, 1)) < 0.012) m = saturate(a.rgb / a.w);
        }
        return float4(m + c * 0.25, 1);
    }
    if (d >= 0.99999) return float4(p[2].w == 1 ? 0 : c, 1);
    float3 P = at(uv);
    // the slope from the nearer neighbour each way (an edge's other side is not the surface)
    float3 r = at(uv + float2(px.x, 0)) - P, l = P - at(uv - float2(px.x, 0));
    float3 dn = at(uv + float2(0, px.y)) - P, up = P - at(uv - float2(0, px.y));
    float3 dx = abs(r.z) < abs(l.z) ? r : l, dy = abs(dn.z) < abs(up.z) ? dn : up;
    float3 N = normalize(cross(dx, dy));
    if (any(isnan(N))) N = normalize(-P);
    if (dot(N, P) > 0) N = -N;
    float omega = 4 * p[1].y * p[1].y * p[1].z / (p[2].x * p[2].y);
    float3 E = 0;
    [loop] for (int n = 0; n < int(p[0].w); n++) {
            float4 a = t2.Load(int3(2 * n, 0, 0));
            if (a.w < p[2].z) break;  // brightest first
            float4 b = t2.Load(int3(2 * n + 1, 0, 0));
            float zl = b.z * 0.97;
            float3 D = viewpos(b.xy, zl) - P;
            float d2 = dot(D, D) + zl * zl * 0.004;
            float ndl = saturate((dot(N, D * rsqrt(d2)) + 0.15) / 1.15);
            float3 e = a.rgb * (omega * zl * zl / d2 * ndl);
#ifdef RT
            // a shadow: anything between the surface and the light (only for a light that shows)
            if (dot(e, float3(0.3, 0.59, 0.11)) * p[1].x * p[3].z > 0.004) {
                float dl = length(D);
                RayDesc ray;
                ray.Origin = P + N * (0.01 + 0.002 * P.z);
                ray.Direction = D / dl;
                ray.TMin = 0;
                ray.TMax = dl * 0.92;
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
                // the world's rays leave the gun out (it reaches into walls), the gun's meet it (masks: world 1, gun 2)
                bool gun = p[3].w != 0 && t1.SampleLevel(pt, uv, 0) < t3.SampleLevel(pt, uv, 0).r - 1e-6;
                q.TraceRayInline(tlas, RAY_FLAG_NONE, gun ? 0xFF : 1, ray);
                RT_PROCEED(q)
                if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) e = 0;
            }
#endif
            E += e;
        }
    E = min(E * p[3].z * p[1].x, 1.5);
    if (p[2].w == 1) return float4(E / (1 + E), 1);  // XBE_LIGHT_TEST=1: the light alone
    return float4(c * (1 + E) + E * 0.03, 1);
}
)";
// soft particles (VulkanBackend::SoftParticle): the draw's own pixel shader (renamed game_main), its output faded
// where the scene behind is near: tex%d the scene depth's copy (a stage the draw does not use), style z 1 fades
// alpha (alpha blended), 2 colour and alpha (added), style w the fade's depth in parts of the particle's distance
const char* kSoftMain = R"(
float4 main(VOut i, bool front : SV_IsFrontFace%s) : SV_Target {
    float4 c = game_main(i, front%s);
    float fz = %s, sz = tex%d.Load(int3(i.pos.xy, 0)).r;
    float lf = %s, ls = %s;
    float soft = saturate((ls - lf) / max(lf * style.w, 1e-6));
    if (style.z == 2) c.rgb *= soft;
    c.a *= soft;
    return c;
}
)";
// ground detail (VulkanBackend::DetailShader): the draw's own pixel shader (renamed game_main), then its texture's
// kind of detail (tex%d: normal xy, brightness, height) at four times the texture's own tiling: the bumped normal's
// light from above the view and from the camera against the flat one's scales the colour (the material shader's
// frame and light), and the detail's brightness too, both fading out from the camera to pbr.z (metres); pbr.w how
// strong. The colour is the game's, so the detail takes each texture's own tint
const char* kDetailMain = R"(
float4 main(VOut i, bool front : SV_IsFrontFace%s) : SV_Target {
    float2 uv0 = %s;
    float4 c = game_main(i, front%s);
    // the view depth in metres (as the mask pass's); none nearer than a metre (the gun and hands in front)
    float w = max(abs(i.wlin * wdepth.w * wdepth.z * 4096), 1e-4);
    float fade = saturate((pbr.z - w) / (pbr.z * 0.65)) * saturate((w - 0.8) / 0.6);
    if (fade <= 0) return c;
    float aspect = pbr.y / pbr.x, tanv = 0.5774;
    float3 P = float3((i.pos.x * pbr.x * 2 - 1) * aspect * tanv * w, -(i.pos.y * pbr.y * 2 - 1) * tanv * w, w);
    float3 dpx = ddx(P), dpy = ddy(P);
    float2 dux = ddx(uv0), duy = ddy(uv0);
    float3 Ng = normalize(cross(dpx, dpy));
    if (dot(Ng, P) > 0) Ng = -Ng;
    float3 t1 = cross(dpy, Ng), t2 = cross(Ng, dpx);
    float3 T = t1 * dux.x + t2 * duy.x, B = t1 * dux.y + t2 * duy.y;
    float inv = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    T *= inv, B *= inv;
    float2 duv = uv0 * 4;
    float4 m = tex%d.SampleGrad(smp%d, frac(duv), dux * 4, duy * 4);
    float3 nt = float3(m.rg * 2 - 1, 0);
    nt.z = sqrt(saturate(1 - dot(nt.xy, nt.xy)));
    float3 N = normalize(T * nt.x + B * nt.y + Ng * nt.z);
    if (any(isnan(N))) N = Ng;
    float3 V = normalize(-P), L = normalize(float3(0.3, 0.8, -0.5));
    float flat = 0.6 * saturate(dot(Ng, L)) + 0.4 * saturate(dot(Ng, V)) + 0.15;
    float bump = 0.6 * saturate(dot(N, L)) + 0.4 * saturate(dot(N, V)) + 0.15;
    if (pbr.w < 0 && i.pos.x * pbr.x < 0.5) return c;
    float k = abs(pbr.w) * fade;
    float shade = clamp(lerp(1, bump / max(flat, 0.05), k), 0.5, 1.6);
    float bright = 1 + (m.b - 0.5) * 1.6 * k;
    c.rgb *= shade * bright;
    return c;
}
)";
// materials (VulkanBackend::MaterialShader): the draw's own pixel shader (renamed game_main) with the pack picture's
// material (tex%d: normal xy, roughness, height). The surface's frame is made from the screen derivatives of its
// position (the view taken as 60 degrees) and of its texture coordinates (a cotangent frame). Relief (pbr.z above 0):
// parallax occlusion mapping, the view's ray stepped down the height map from the surface, its texture coordinates
// moved to where it meets it, for the game's own textures too (game_main is handed the moved stage 0 coordinates).
// Light from above the view and from the camera: the ratio of the bumped normal's light to the flat one's scales the
// game's colour, and a GGX highlight of the roughness adds a sheen, kept in proportion to it
const char* kMatMain = R"(
float4 main(VOut i, bool front : SV_IsFrontFace%s) : SV_Target {
    float2 uv0 = %s;
    float2 tcscale = %s;
    float w = max(abs(i.wlin * wdepth.w), 1e-4);
    float aspect = pbr.y / pbr.x, tanv = 0.5774;
    float3 P = float3((i.pos.x * pbr.x * 2 - 1) * aspect * tanv * w, -(i.pos.y * pbr.y * 2 - 1) * tanv * w, w);
    float3 dpx = ddx(P), dpy = ddy(P);
    float2 dux = ddx(uv0), duy = ddy(uv0);
    float3 Ng = normalize(cross(dpx, dpy));
    if (dot(Ng, P) > 0) Ng = -Ng;
    float3 t1 = cross(dpy, Ng), t2 = cross(Ng, dpx);
    float3 T = t1 * dux.x + t2 * duy.x, B = t1 * dux.y + t2 * duy.y;
    float inv = rsqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    T *= inv, B *= inv;
    float3 V = normalize(-P);
    float2 uv = uv0;
    if (pbr.z > 0) {
        // the view in the texture's frame; steps down from the top (height 1) until under the height map
        float3 vt = float3(dot(V, T), dot(V, B), max(dot(V, Ng), 0.08));
        float2 dir = -vt.xy / vt.z * pbr.z * 0.05;
        int steps = int(lerp(24, 8, saturate(vt.z)));
        float layer = 1.0 / steps, d = 0, hd = 1 - tex%d.SampleGrad(smp%d, uv, dux, duy).a, prev_hd = hd;
        float2 st = dir * layer, cur = uv;
        [loop] for (int k = 0; k < steps && d < hd; k++) {
            cur += st;
            d += layer;
            prev_hd = hd;
            hd = 1 - tex%d.SampleGrad(smp%d, cur, dux, duy).a;
        }
        // between the last two steps, where the ray crossed
        float after = hd - d, before = prev_hd - (d - layer);
        float f = after / min(after - before, -1e-5);
        uv = cur - st * saturate(f);
        i.tc0.xy += (uv - uv0) * tcscale;
    }
    float4 c = game_main(i, front%s);
    float4 m = tex%d.SampleGrad(smp%d, uv, dux, duy);
    float3 nt = float3(m.rg * 2 - 1, 0);
    nt.z = sqrt(saturate(1 - dot(nt.xy, nt.xy)));
    float3 N = normalize(T * nt.x + B * nt.y + Ng * nt.z);
    if (any(isnan(N))) N = Ng;
    float3 L = normalize(float3(0.3, 0.8, -0.5));
    float flat = 0.55 * saturate(dot(Ng, L)) + 0.45 * saturate(dot(Ng, V)) + 0.15;
    float bump = 0.55 * saturate(dot(N, L)) + 0.45 * saturate(dot(N, V)) + 0.15;
    float shade = clamp(lerp(1, bump / max(flat, 0.05), pbr.w), 0.4, 1.8);
    float r = max(m.b, 0.06), a2 = r * r * r * r;
    float3 H = normalize(L + V);
    float nh = saturate(dot(N, H)), dn = nh * nh * (a2 - 1) + 1;
    float D = a2 / (3.14159 * dn * dn);
    float F = 0.04 + 0.96 * pow(1 - saturate(dot(H, V)), 5);
    float spec = min(D * F * saturate(dot(N, L)) * 0.25, 4);
    float lum = dot(c.rgb, float3(0.299, 0.587, 0.114));
    c.rgb = c.rgb * shade + spec * pbr.w * saturate(lum * 1.5) * 0.5;
    return c;
}
)";
constexpr uint64_t kMatKeySalt = 0x6D6174657269616Cull;  // "material"
constexpr uint64_t kDetailKeySalt = 0x64657461696C7321ull;  // "details!"
constexpr uint64_t kSoftKeySalt = 0x736F667470617274ull;  // "softpart"
constexpr uint64_t kMaskKeySalt = 0x6D61736B6D61736Cull;
constexpr uint64_t kGlowKeySalt = 0x676C6F776C696768ull;  // "glowligh"
constexpr uint64_t kCaptureKeySalt = 0x7274636170747572ull;  // "rtcaptur"
constexpr uint32_t kLightTilesX = 16, kLightTilesY = 9, kLights = 32;
constexpr VkFormat kMaskFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr uint32_t kMaskRtFmt = 0x4D41534Bu;  // PsoKey::rtfmt of the mask's pipelines  // "maskmask": the mask's pixel shaders apart from the game's

// ---- DXC, loaded from dxcompiler.dll beside the exe

struct Dxc {
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcUtils> utils;
    bool tried = false;
    bool Load() {
        if (tried) return compiler != nullptr;
        tried = true;
        std::filesystem::path dll = app::ExeDir() / "dxcompiler.dll";
        HMODULE m = LoadLibraryW(dll.wstring().c_str());
        if (!m) m = LoadLibraryW(L"dxcompiler.dll");
        if (!m) {
            LOGE("vulkan: dxcompiler.dll not found beside the exe; the shaders cannot be compiled");
            return false;
        }
        auto create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(m, "DxcCreateInstance"));
        if (!create || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) ||
            FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
            LOGE("vulkan: dxcompiler.dll has no compiler");
            compiler = nullptr;
            return false;
        }
        return true;
    }
};

// ---- the backend

class VulkanBackend final : public Backend {
public:
    ~VulkanBackend() override;

    bool Init(uint32_t width, uint32_t height) override;
    void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
               uint32_t stencil) override;
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override;
    void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) override;
    void Present(const Surface& front) override;
    void Sync(uint32_t data, uint32_t size) override;
    void SyncLate(uint32_t data) override {
        if (!DeliverLate(data)) Sync(data, 0);
        RecordLate(data);
    }
    bool DeliverLate(uint32_t data) override;
    void RecordLate(uint32_t data) override;
    // the GPU is handed what was recorded, so every report asked for so far is written (titles poll inside a frame)
    void Flush() override {
        if (dev_) Submit();
        DeliverReports(true);
    }
    void SetGamma(const uint8_t* ramp) override {
        std::memcpy(ramp_, ramp, sizeof(ramp_));
        ramp_dirty_ = true;
    }
    void ClearReport() override;
    void GetReport(uint32_t report) override { reports_.push_back({report, counted_}); }

private:
    struct Query {
        uint32_t index;
        uint64_t serial;  // the submission it was recorded in
        float samples_per_pixel;
    };
    struct Report {
        uint32_t va;
        std::vector<Query> queries;
    };
    // a surface locked without a wait: copies in flight, the newest finished one kept on the CPU for DeliverLate
    struct Late {
        Buffer staging[kLateSlots];
        uint64_t frame[kLateSlots] = {};   // 0 free
        uint64_t serial[kLateSlots] = {};  // the submission copying into it
        std::vector<uint8_t> cpu;          // rows packed
        uint64_t cpu_frame = 0, delivered = 0;
        uint32_t gw = 0, gh = 0, next = 0;
    };
    struct Chunk {
        Buffer b;
        VkDeviceSize used = 0;
        uint64_t serial = 0;  // the last submission that reads it
    };
    // a pass pipeline: a full screen triangle into one colour or depth format
    struct PassKey {
        uint64_t vs, ps;
        VkFormat color, depth;
        uint32_t blend;  // 0 opaque, 1 premultiplied over
        uint32_t zmode;  // 0 none, 1 depth always written
        uint32_t rt = 0, pad = 0;  // 1: rt_layout_ (reads the acceleration structure)
    };

    // Init, in order
    bool LoadVulkan();
    bool CreateInstance();
    bool PickDevice();
    bool CreateDevice();
    bool CreateSwapchain();
    void DestroySwapchain();
    bool CreateFixed();
    bool InitUi();
    // submission
    VkCommandBuffer Cmd();
    void Submit(VkSemaphore wait = VK_NULL_HANDLE, VkSemaphore signal = VK_NULL_HANDLE);
    uint64_t Completed();
    void Wait(uint64_t serial);
    void WaitIdle();
    void Collect();
    void Retire(std::function<void()> f) { retire_.emplace_back(serial_ + 1, std::move(f)); }
    // rendering instances and barriers
    void Begin(const Attachments& a);
    void End();
    void Barrier();
    void ToGeneral(VkImage img, bool depth, uint32_t layers);
    // resources
    bool MakeImage(Image& im, VkImageType type, VkFormat fmt, uint32_t w, uint32_t h, uint32_t d, uint32_t levels,
                   uint32_t layers, VkImageUsageFlags usage, bool cube = false);
    void DropImage(Image& im);
    bool MakeBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host);
    void DropBuffer(Buffer& b);
    Stretch Stream(VkDeviceSize bytes, VkDeviceSize align = 16);
    VkSampler Sampler(const D3D12_SAMPLER_DESC& d);
    // Present's steps
    bool Acquire(uint32_t& index);
    void UploadGammaRamp();
    void BlitFrame(Target& t, VkImageView view, VkFormat fmt);
    void RenderUi();
    void ReleaseOldTextures();
    void CountFps();
    void ApplySettings();
    void RescaleTargets();
    void SavePipelineCache();
    // targets
    uint64_t SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const;
    Target* FindTarget(const Surface& s, bool depth);
    Target* EnsureTarget(const Surface& s, bool depth, uint32_t gw = 0, uint32_t gh = 0);
    Target* DepthTarget(const State& s);
    bool CreateTarget(Target& t);
    void ReleaseTarget(std::unordered_map<uint64_t, Target>::iterator it);
    bool TargetStale(uint32_t data) const;
    bool BindTargets(const State& s);
    void Unbind() {
        bound_rt_ = bound_ds_ = nullptr;
        ForgetDrawState();
    }
    Attachments TargetAttachments(Target* rt, Target* ds) const;
    void UploadTarget(Target& t, const Surface& s);
    Image Shrink(Target& t, uint32_t w, uint32_t h);
    Image* PrepareCopySource(Target& t, Image& shrunk);  // guest sized BGRA: the target, or a shrunk copy
    void CopyToBuffer(const Image& src, uint32_t w, uint32_t h, VkBuffer dst);
    bool ReadBack(const Image& src, uint32_t w, uint32_t h, std::vector<uint8_t>& out);
    // shaders and passes
    VkShaderModule Compile(const std::string& src, const char* entry, const char* profile, uint64_t& hash);
    VkShaderModule Module(uint64_t hash);
    VkPipeline PassPipeline(const PassKey& k);
    void FullScreen(VkPipeline p, const Attachments& a, const VkViewport& vp, const VkRect2D& sc, const float* cb,
                    uint32_t cb_floats, const VkImageView* views, uint32_t n_views,
                    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE);
    void Blit(VkImageView src, const Attachments& a, const VkViewport& vp, const VkRect2D& sc, const float uvrect[4],
              const BlitOptions& o, VkFormat fmt = kBgra);
    void BlitWhole(VkImageView src, const Image& dst, const BlitOptions& o);
    bool DepthCopy(const State& s);
    void ClearRects(Target& t, const std::vector<VkRect2D>& rs, uint32_t flags, const float colour[4], float z,
                    uint32_t stencil);
    void CopyByDraw(Target& src_t, Target& dst_t, uint32_t count, uint32_t src_rects, uint32_t dst_points);
    void CopyByRegion(Target& src_t, Target& dst_t, uint32_t count, uint32_t src_rects, uint32_t dst_points);
    // a draw's steps, in call order
    void EnsureVertexShader(const State& s, const Assembled& a, DrawShaders& sh);
    VkImageView BindTexture(const State& s, int stage, DrawTextures& stages);
    VkImageView BindTargetTexture(const State& s, int stage, const TextureDesc& d, Target& t, DrawTextures& stages);
    VkImageView BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi,
                                 DrawTextures& stages);
    bool CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi);
    void UploadTexture(Tex& tx, const TextureDesc& d, const uint8_t* p, const uint32_t* colours);
    void UploadLevel(const Image& im, uint32_t level, uint32_t layer, uint32_t w, uint32_t h, uint32_t depth,
                     const void* p, size_t bytes);
    VkImageView BindReplacement(const Replacement& r, const TextureDesc& d, uint32_t& levels);
    void CopyCubeTargets(Tex& tx, const TextureDesc& d);
    void EnsurePixelShader(const PixelKey& pk, DrawShaders& sh);
    VkPipeline EnsurePipeline(const PsoKey& k);
    void PushDescriptors(const Stretch& vsc, const Stretch& dc, const DrawTextures& stages, const VkSampler smp[4]);
    void DrawGeometry(const Assembled& a);
    // visibility tests
    bool NewQuery(uint32_t& index);
    float SamplesPerPixel() const;
    void DeliverReports(bool wait);

    // what the draw path set last, so equal state is not set again; anything else that sets it calls ForgetDrawState
    struct DrawStateCache {
        VkPipeline pipeline = VK_NULL_HANDLE;
        float bf[4] = {-1, -1, -1, -1};
        uint32_t sref = ~0u;
        int topo = -1;
        VkViewport vp{};
        VkRect2D sc{};
        bool vp_ok = false;
    } ds_;
    void ForgetDrawState() { ds_ = {}; }

    // Vulkan objects
    HMODULE lib_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice pd_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    VkDevice dev_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swap_ = VK_NULL_HANDLE;
    VkFormat swap_fmt_ = kBgra;
    // HDR (Settings::hdr): an HDR10 swap chain (PQ, BT.2020) where the screen offers one (Windows' HDR on); the frame
    // and the overlay drawn at the screen's size first (hdr_sdr_, hdr_ui_: the overlay premultiplied), then HdrOut.
    // ui_fmt_ the overlay's format, fixed when ImGui starts; the screen's brightness (nits) read at each new chain
    bool colorspaces_ = false, swap_hdr_ = false, swap_hdr_asked_ = false;
    VkFormat ui_fmt_ = kBgra;
    Image hdr_sdr_, hdr_ui_;
    float display_peak_ = 0, display_paper_ = 0;
    struct HdrNits { float paper, peak; };
    HdrNits HdrLevels() const;  // Settings' or the screen's, else 200 and 1000
    void HdrCompose(Target* t);
    void HdrOut(Target* t, VkImageView view);
    std::vector<VkImage> swap_images_;
    std::vector<VkImageView> swap_views_;
    std::vector<VkSemaphore> present_sem_;  // per swapchain image
    VkSemaphore acquire_sem_[kFramesInFlight] = {};
    uint64_t frame_serial_[kFramesInFlight] = {};
    uint32_t frame_slot_ = 0;
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    bool swap_vsync_ = true;
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    uint64_t serial_ = 0;     // the last submission
    uint64_t completed_ = 0;  // the last one known finished
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::vector<std::pair<VkCommandBuffer, uint64_t>> cmds_;  // and the submission each was last in
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;                     // recording
    std::deque<std::pair<uint64_t, std::function<void()>>> retire_;
    Allocator alloc_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
    VkDescriptorPool ui_pool_ = VK_NULL_HANDLE;
    VkQueryPool queries_ = VK_NULL_HANDLE;
    VkFormat depth_fmt_ = VK_FORMAT_D24_UNORM_S8_UINT;
    bool null_descriptor_ = false;
    bool custom_border_ = false;
    bool mirror_once_ = false;
    bool host_reset_ = false;
    bool fill_lines_ = false;
    bool aniso_ok_ = false;
    bool gs_ok_ = false;  // geometry shaders, which points are drawn with
    bool open_ = false;  // a rendering instance is open
    Attachments cur_;
    bool dirty_ = false;  // writes since the last barrier
    // fixed objects
    VkSampler pass_samplers_[4] = {};  // linear, then point; all clamp
    Image lut_;
    Image dummy_;  // 1x1 white, for stages without a texture where null descriptors are missing
    std::vector<Chunk> chunks_;  // stream chunks: [0] is being filled
    Buffer readback_;
    Dxc dxc_;
    // caches
    std::unordered_map<uint64_t, std::vector<uint32_t>> spirv_;
    std::unordered_map<uint64_t, VkShaderModule> modules_;
    std::unordered_map<uint64_t, uint64_t> shader_src_;  // vertex and pixel shader keys to source hash
    uint64_t point_gs_ = 0;
    std::unordered_map<uint64_t, VkPipeline> pipelines_;
    std::unordered_map<uint64_t, VkPipeline> pass_pipelines_;
    std::unordered_map<uint64_t, VkSampler> samplers_;
    std::unordered_map<uint64_t, Target> targets_;
    std::unordered_map<uint32_t, uint64_t> target_used_;  // target address to the frame it was last used
    std::unordered_map<uint64_t, Tex> textures_;          // by data address, a P8 texture's with its palette's above
    std::unordered_map<uint32_t, uint32_t> reloads_;      // targets reloaded from guest memory, for the log
    std::unordered_map<uint32_t, Late> late_;
    std::mutex late_lock_;                    // DeliverLate runs on the title's thread
    std::atomic<uint64_t> presents_seen_{0};  // presents_ for DeliverLate
    std::vector<uint32_t> free_queries_;
    std::vector<Query> counted_;  // since the last ClearReport
    std::vector<Query> dropped_;  // counts no report took, free once their submission is done
    std::deque<Report> reports_;
    Stretch last_vsc_, last_dc_;  // the constants last streamed, in submission last_serial_
    uint64_t last_serial_ = 0;
    std::vector<uint8_t> vsc_last_, dc_last_;
    Assembled asm_;  // kept between draws for its buffers
    bool draw_2d_ = false;  // the draw is 2D: its textures are sharpened (Sharpen2D)
    Target* bound_rt_ = nullptr;
    Target* bound_ds_ = nullptr;
    Target* draw_rt_ = nullptr;  // the draw's targets, the bound ones go when a texture's copy is drawn
    Target* draw_ds_ = nullptr;
    uint32_t guest_w_ = 640, guest_h_ = 480;
    uint32_t bb_w_ = 0, bb_h_ = 0;
    float scale_ = 1;
    float aspect_ = 0;  // app::FrameAspect() the targets were made for
    uint8_t ramp_[768] = {};
    bool ramp_dirty_ = false;
    bool gamma_ = false;
    bool ui_ = false;
    uint64_t presents_ = 0;
    double fps_ = 0;
    std::chrono::steady_clock::time_point fps_t_ = std::chrono::steady_clock::now();
    uint64_t fps_n_ = 0;
    uint64_t validation_errors_ = 0;
    // graphics styles: the frame's 3D scene target and depth, and whether its scene pass ran
    Target* scene_rt_ = nullptr;
    Target* scene_ds_ = nullptr;
    bool post_done_ = false;
    void ScenePass(Target& rt, Target* ds);
    void StylePass(Target& rt, Target* ds);  // the style's own pass (StylePassWanted)
    void AtmospherePass(Target& rt, Target* ds);  // light shafts and fog (GraphicsStyle::atmosphere)
    void BouncePass(Target& rt, Target* ds);      // bounced light (GraphicsStyle::bounce_light)
    void DofPass(Target& rt, Target* ds);         // depth of field (GraphicsStyle::dof)
    bool DofWanted() const { return Style().dof != kDofOff && Style().mode != kStyleRetro; }
    // the scene's passes that read the world's distance (the depth kept at the mid-frame clear, RtKeepDepth)
    bool KeptDepthWanted() const {
        const GraphicsStyle& st = Style();
        return DofWanted() || st.mode == kStyleNoir || (st.mode != kStyleRetro && (st.effect_lights || st.bounce_light));
    }
    float dof_aim_ = 0;          // aiming down the sights, eased 0 to 1
    uint32_t scene_zwrites_ = 0;  // the frame's 3D draws that wrote the scene's depth
    // ray tracing (GraphicsStyle::ray_tracing): the 3D scene's opaque draws write their vertices' places in the view
    // (RtCapture); the scene pass builds them into a bottom level acceleration structure, one instance of it the top
    // level, and traces against it (RtPass). A frame slot's buffers and structures, used again once its frame is done.
    struct RtFrame {
        Buffer verts;  // float4 places, written by the capture shaders (device)
        Buffer tris;   // the triangles, indices into verts (host): the opaque ones, then the cut-outs
        Buffer mats;   // their materials (host): the opaque triangles' count, then a word a triangle (RtMaterial)
        VkDescriptorSet tex_set = VK_NULL_HANDLE;  // set 1: the textures the materials name
        uint32_t tex_written = 0;                  // of them written with a texture last time (the rest the dummy)
        Buffer inst;   // the top level's one instance (host)
        Buffer stats;  // the level lighting's outdoor baked light: red, green, blue (64ths), pixels (host)
        uint64_t stats_frame = 0;  // the frame that last read and cleared it
        Buffer scratch, blas_mem, tlas_mem;
        Buffer gun_blas_mem;  // the gun and hands' bottom level (RtBuild)
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE, tlas = VK_NULL_HANDLE, gun_blas = VK_NULL_HANDLE;
        uint32_t vert_cap = 0;
    };
    bool RtWanted() const { return rt_ok_ && Style().ray_tracing && Style().mode != kStyleRetro; }
    bool rt_built_ = false;  // this frame's acceleration structure is built (the scene pass, before its passes)
    bool RtCapture(const State& s, const Assembled& a, const DrawShaders& sh, PsoKey& k, VkImageView tex, uint32_t push[4]);
    // a W-buffered scene's sky draw (SkyDraw): the frame's own count, the result kept for the draw
    struct SkyCheck { bool sky = false, eye = false, cam_ok = false; float cam[3][4] = {}; };
    SkyCheck SkyDraw(const State& s, const Assembled& a, const DrawShaders& sh);
    SkyCheck sky_;
    uint64_t sky_draw_ = ~0ull, draw_seq_ = 0;  // the draw sky_ was worked out for, the draws so far
    bool sky_keeps_game_ = false;               // this draw is the sky: no pack picture for it
    bool RtBuild(float aspect);
    void RtKeepDepth(Target& ds);
    void RtPass(Target& rt, Target* ds);
    void RtReflectPass(Target& rt, Target* ds);
    bool rt_reflected_ = false;  // this frame's reflections were traced: the screen-space ones are left out
    bool RtStorage(Buffer& mem, VkAccelerationStructureKHR& as, VkDeviceSize need, VkAccelerationStructureTypeKHR type);
    VkDeviceAddress Address(const Buffer& b) const {
        VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, b.buf};
        return vkGetBufferDeviceAddress(dev_, &ai);
    }
    void DropRt(RtFrame& f);
    RtFrame rt_[kFramesInFlight];
    bool rt_ok_ = false;  // the GPU has ray queries, and they are turned on
    bool frag_stores_ = false;  // pixel shaders may write storage buffers (the level lighting's sums)
    VkDeviceSize rt_scratch_align_ = 256;
    VkDescriptorSetLayout rt_set_layout_ = VK_NULL_HANDLE, rt_tex_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool rt_pool_ = VK_NULL_HANDLE;
    VkSampler rt_sampler_ = VK_NULL_HANDLE;
    VkPipelineLayout rt_layout_ = VK_NULL_HANDLE;
    bool rt_capturing_ = false;  // the draw being recorded writes its places (PushDescriptors adds u0)
    uint32_t rt_used_ = 0;       // vertices captured this frame
    uint32_t rt_want_ = 1u << 20;  // the capture buffers' size in vertices: doubled after a frame that ran out
    bool rt_full_ = false;
    bool rt_drawn_ = false;  // this frame's occlusion was traced: the screen-space one is left out
    std::vector<uint32_t> rt_tris_, rt_cut_;     // opaque triangles, cut-outs (alpha tested): indices into the vertices
    std::vector<uint32_t> rt_mats_, rt_cut_mats_;  // a material word each (RtMaterial)
    std::vector<uint32_t> rt_gun_, rt_gun_mats_;   // the gun and hands (drawn after the mid-frame depth clear)
    std::vector<VkImageView> rt_views_;          // the frame's textures the materials name (their slots)
    std::unordered_map<VkImageView, uint32_t> rt_view_slot_;
    struct RtUndo { size_t tris, cut, mats, cut_mats, gun, gun_mats; uint32_t used; } rt_undo_{};  // before the last capture
    uint32_t RtMaterial(const State& s, VkImageView tex, bool cut);
    // the level lit again (RtRelightPass): the frame's cameras (a world place to the ray traced view's, before the
    // instance's scale: rows x, y, distance) by the triangles drawn with each, and the level's fog colour
    struct RtCam { float r[3][4]; uint64_t tris = 0; };
    std::unordered_map<uint64_t, RtCam> rt_cams_;
    float rt_fog_[4] = {};
    uint64_t rt_fog_tris_[2] = {};  // the frame's world triangles drawn with fog off, on
    float rt_fogged_ = 0;           // the last frame's share fogged
    float rt_sky_[4] = {};          // the outdoor baked light eased over frames, its weight (the sun's colour)
    int rt_sky_kind_ = -1;          // logged: 0 day, 1 night, 2 dusk, 3 snow
    void RtRelightPass(Target& rt, Target* ds);
    bool rt_gi_drawn_ = false;  // this frame's level lighting traced its bounced light (the screen-space one left out)
    uint64_t perf_rt_draws_ = 0, perf_rt_tris_ = 0;
    uint64_t rt_skips_[5] = {};  // draws not traced: not the scene, depth not written, not triangles, blended, cut-out
    // SMAA over the scene: its shaders (vs, edges, weights, blend) and lookup textures, made at the first use
    void SmaaPass(Target& rt);
    bool EnsureSmaa();
    uint64_t smaa_h_[4] = {};
    bool smaa_tried_ = false, smaa_ok_ = false;
    Image smaa_area_, smaa_search_;
    // reflections: the draw's stage 0 texture as BindGuestTexture saw it (none for a target or a replacement)
    uint64_t stage_key_ = 0;
    uint8_t stage_detail_ = 0;
    float stage_glass_ = 0;
    bool stage_solid_ = false, stage_seen_ = false;
    bool draw_env_ = false;
    bool scene_wbuf_ = false;  // the scene's last 3D draw wrote W (D3DZB_USEW), not Z, to its depth  // the draw samples a cube map: the game's own environment reflection (shiny metal)
    float Reflectivity(const PsoKey& k);
    // soft particles: 0 none, 1 alpha blended, 2 added; the variant's pixel shader and the depth copy's stage
    int SoftParticle(const PsoKey& k, const PixelKey& pk, float refl);
    uint64_t SoftShader(const DrawShaders& sh, const PixelKey& pk, int slot);
    VkImageView SoftDepth(Target& ds);
    // materials: the pack picture's material image (made once from texture_pack's), the variant's pixel shader
    std::unordered_map<uint64_t, Image> materials_;
    // ground detail: each kind's picture (made once), the variant's pixel shader
    Image details_[kDetailKinds];
    VkImageView DetailView(uint8_t kind);
    uint64_t DetailShader(const DrawShaders& sh, const PixelKey& pk, int slot);
    VkImageView MaterialView(uint64_t key);
    uint64_t MaterialShader(const DrawShaders& sh, const PixelKey& pk, int slot);
    bool EnsureMask(Target& t);
    int Emitter(const PsoKey& k);
    // effect lights: the geometry the 3D scene's solid draws used this frame (their indices', else their first
    // buffer's, guest address), so a model drawn again added (NightFire's weapon pickups' shimmer) lights nothing
    std::unordered_set<const void*> solid_geom_;
    uint64_t solid_geom_frame_ = 0;
    void DrawGlow(const Assembled& a, PsoKey k, const DrawShaders& sh, const PixelKey& pk, const DrawTextures& stages,
                  const VkSampler smp[4], const float bf[4], uint32_t sref, const VkViewport& vp, const VkRect2D& sc,
                  int mode);
    void LightPass(Target& rt, Target* ds);
    void DrawMask(const Assembled& a, PsoKey k, const DrawShaders& sh, const PixelKey& pk, const DrawTextures& stages,
                  const VkSampler smp[4], const float bf[4], uint32_t sref, const VkViewport& vp, const VkRect2D& sc);
    void DropScene(Target& t) {
        DropImage(t.smaa_edges), DropImage(t.smaa_weights), DropImage(t.mask), DropImage(t.mask_ds);
        DropImage(t.soft_depth);
        DropImage(t.glow), DropImage(t.light_a), DropImage(t.light_b), DropImage(t.light_list);
        DropImage(t.rt_ao), DropImage(t.rt_depth), DropImage(t.rt_gi);
        DropImage(t.dof_focus[0]), DropImage(t.dof_focus[1]);
        DropImage(t.hdr_scene);
    }
    // the scene pass's HLSL: mods/shaders/scene_post.hlsl beside the exe when there is one (a trusted local shader
    // mod, read again when it changes), else the built-in kScenePostHlsl
    const std::string& ScenePassSource();
    std::string builtin_post_ = kScenePostHlsl, mod_post_;
    std::filesystem::file_time_type mod_post_time_{};
    std::chrono::steady_clock::time_point mod_post_checked_{};
    uint64_t scene_src_hash_ = 0;
    // GPU timestamps a frame slot: frame start, frame end, scene pass start, end
    VkQueryPool stamps_ = VK_NULL_HANDLE;
    uint32_t stamp_written_[kFramesInFlight] = {};  // bits of the four written
    bool frame_stamp_due_ = true;
    double gpu_frame_ms_ = 0, gpu_post_ms_ = 0;
    uint32_t gpu_frames_ = 0, gpu_posts_ = 0;
    void Stamp(uint32_t which);
    void ReadStamps(uint32_t slot);
    // XBE_PERF: a line a second
    double perf_wait_ = 0;  // ms the render thread waited for the GPU
    double perf_draw_ = 0, perf_present_ = 0, perf_acquire_ = 0, perf_sync_ = 0, perf_submit_ = 0;  // ms in each
    struct PerfScope {
        double& acc;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~PerfScope() { acc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
    };
    uint64_t perf_frames_ = 0, perf_submits_ = 0, perf_draws_ = 0;
    uint64_t perf_glow_ = 0;
    uint64_t perf_prog_ = 0, perf_fixed_ = 0, perf_lit_ = 0, perf_2d_ = 0, perf_reflect_ = 0, perf_soft_ = 0,
             perf_mat_ = 0;  // draws by kind
    uint64_t perf_det_[4] = {};  // ground detail: tagged 3D draws, not opaque, no free stage, drawn
    std::chrono::steady_clock::time_point perf_t_ = std::chrono::steady_clock::now();
    void LogPerf();
    friend VkBool32 VKAPI_PTR DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
                                           const VkDebugUtilsMessengerCallbackDataEXT*, void*);
};

VkBool32 VKAPI_PTR DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto* self = static_cast<VulkanBackend*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        uint64_t n = ++self->validation_errors_;
        if (n <= 64) LOGE("vulkan validation: %s", data->pMessage);
        else if (n == 65) LOGE("vulkan validation: more errors, not logged");
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_FIRST(32, "vulkan validation warning: %s", data->pMessage);
    }
    return VK_FALSE;
}

bool DebugWanted() {
    static const bool on = [] {
        const char* e = std::getenv("XBE_VK_DEBUG");
        return e && *e && *e != '0';
    }();
    return on;
}

// ---- instance, device, swap chain, fixed objects

bool VulkanBackend::LoadVulkan() {
    lib_ = LoadLibraryW(L"vulkan-1.dll");
    if (!lib_) {
        LOGE("vulkan: vulkan-1.dll not found (no Vulkan driver)");
        return false;
    }
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(lib_, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) return false;
#define VK_LOAD(f) f = reinterpret_cast<PFN_##f>(vkGetInstanceProcAddr(nullptr, #f));
    VK_GLOBAL_FUNCS(VK_LOAD)
#undef VK_LOAD
    return vkCreateInstance != nullptr;
}

bool VulkanBackend::CreateInstance() {
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_3) {
        LOGE("vulkan: the loader offers Vulkan %u.%u, 1.3 is needed", VK_API_VERSION_MAJOR(version),
             VK_API_VERSION_MINOR(version));
        return false;
    }
    std::vector<const char*> exts = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    {
        // HDR: the swap chain's colour spaces past sRGB (HDR10's)
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> ep(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, ep.data());
        for (auto& e : ep)
            if (!std::strcmp(e.extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
                exts.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME), colorspaces_ = true;
    }
    std::vector<const char*> layers;
    if (DebugWanted()) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> lp(n);
        vkEnumerateInstanceLayerProperties(&n, lp.data());
        for (auto& l : lp)
            if (!std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) layers.push_back("VK_LAYER_KHRONOS_validation");
        if (layers.empty()) LOGW("vulkan: XBE_VK_DEBUG set, but the validation layer is not installed");
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "xbe-recomp";
    app.pEngineName = "xbe-recomp";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = uint32_t(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = uint32_t(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) {
        LOGE("vulkan: vkCreateInstance failed: %s", VkResultName(r));
        return false;
    }
    g_instance = instance_;
#define VK_LOAD(f) f = reinterpret_cast<PFN_##f>(vkGetInstanceProcAddr(instance_, #f));
    VK_INSTANCE_FUNCS(VK_LOAD)
    VK_DEBUG_FUNCS(VK_LOAD)
#undef VK_LOAD
    if (DebugWanted() && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = DebugMessage;
        mi.pUserData = this;
        vkCreateDebugUtilsMessengerEXT(instance_, &mi, nullptr, &messenger_);
        LOGR("vulkan: validation %s", layers.empty() ? "unavailable" : "on");
    }
    VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    si.hinstance = GetModuleHandleW(nullptr);
    si.hwnd = static_cast<HWND>(app::Window());
    if (vkCreateWin32SurfaceKHR(instance_, &si, nullptr, &surface_) != VK_SUCCESS) {
        LOGE("vulkan: no surface for the window");
        return false;
    }
    return true;
}

bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (auto& e : list)
        if (!std::strcmp(e.extensionName, name)) return true;
    return false;
}

// a discrete GPU first (hybrid laptops), with Vulkan 1.3, push descriptors and a queue that draws and presents
bool VulkanBackend::PickDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> list(n);
    vkEnumeratePhysicalDevices(instance_, &n, list.data());
    int best = -1;
    uint32_t best_family = 0;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(list[i], &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        uint32_t en = 0;
        vkEnumerateDeviceExtensionProperties(list[i], nullptr, &en, nullptr);
        std::vector<VkExtensionProperties> ext(en);
        vkEnumerateDeviceExtensionProperties(list[i], nullptr, &en, ext.data());
        if (!HasExtension(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME) || !HasExtension(ext, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
            continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(list[i], &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(list[i], &qn, qf.data());
        int family = -1;
        for (uint32_t q = 0; q < qn && family < 0; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(list[i], q, surface_, &present);
            if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) family = int(q);
        }
        if (family < 0) continue;
        bool discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        if (best < 0 || discrete) {
            best = int(i);
            best_family = uint32_t(family);
            if (discrete) break;
        }
    }
    if (best < 0) {
        LOGE("vulkan: no GPU with Vulkan 1.3, push descriptors and presentation to this window");
        return false;
    }
    pd_ = list[size_t(best)];
    family_ = best_family;
    vkGetPhysicalDeviceProperties(pd_, &props_);
    LOGR("Vulkan adapter: %s, Vulkan %u.%u.%u, driver %08X", props_.deviceName, VK_API_VERSION_MAJOR(props_.apiVersion),
         VK_API_VERSION_MINOR(props_.apiVersion), VK_API_VERSION_PATCH(props_.apiVersion), props_.driverVersion);
    return true;
}

bool VulkanBackend::CreateDevice() {
    uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(pd_, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> ext(en);
    vkEnumerateDeviceExtensionProperties(pd_, nullptr, &en, ext.data());
    std::vector<const char*> want = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    bool robust2 = HasExtension(ext, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    bool border = HasExtension(ext, VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
    // ray tracing: ray queries from any shader (Khronos', not a vendor's: NVIDIA, AMD and Intel GPUs alike)
    static const bool rt_off = [] {
        const char* e = std::getenv("XBE_NO_RT");
        return e && *e && *e != '0';
    }();
    bool rt_exts = !rt_off && HasExtension(ext, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                   HasExtension(ext, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                   HasExtension(ext, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    if (HasExtension(ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) want.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    // what the device offers
    VkPhysicalDeviceCustomBorderColorFeaturesEXT border_f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT};
    VkPhysicalDeviceRobustness2FeaturesEXT robust_f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as_f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rq_f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12;
    f12.pNext = &f13;
    void** tail = &f13.pNext;
    if (robust2) *tail = &robust_f, tail = &robust_f.pNext;
    if (border) *tail = &border_f, tail = &border_f.pNext;
    if (rt_exts) *tail = &as_f, as_f.pNext = &rq_f, tail = &rq_f.pNext;
    vkGetPhysicalDeviceFeatures2(pd_, &f2);
    if (!f13.dynamicRendering || !f13.synchronization2 || !f12.timelineSemaphore) {
        LOGE("vulkan: the GPU lacks dynamic rendering, synchronization2 or timeline semaphores");
        return false;
    }

    // what is turned on
    VkPhysicalDeviceCustomBorderColorFeaturesEXT border_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT};
    VkPhysicalDeviceRobustness2FeaturesEXT robust_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rq_on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceVulkan13Features on13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features on12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 on{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    on.pNext = &on12;
    on12.pNext = &on13;
    tail = &on13.pNext;
    on13.dynamicRendering = VK_TRUE;
    on13.synchronization2 = VK_TRUE;
    on12.timelineSemaphore = VK_TRUE;
    on12.hostQueryReset = f12.hostQueryReset;
    on12.samplerMirrorClampToEdge = f12.samplerMirrorClampToEdge;
    host_reset_ = f12.hostQueryReset;
    mirror_once_ = f12.samplerMirrorClampToEdge;
    on.features.geometryShader = f2.features.geometryShader;
    on.features.fillModeNonSolid = f2.features.fillModeNonSolid;
    on.features.samplerAnisotropy = f2.features.samplerAnisotropy;
    on.features.textureCompressionBC = f2.features.textureCompressionBC;
    on.features.dualSrcBlend = f2.features.dualSrcBlend;
    on.features.occlusionQueryPrecise = f2.features.occlusionQueryPrecise;
    on.features.depthBiasClamp = f2.features.depthBiasClamp;
    on.features.fragmentStoresAndAtomics = f2.features.fragmentStoresAndAtomics;
    frag_stores_ = f2.features.fragmentStoresAndAtomics;
    fill_lines_ = f2.features.fillModeNonSolid;
    aniso_ok_ = f2.features.samplerAnisotropy;
    gs_ok_ = f2.features.geometryShader;
    if (!f2.features.geometryShader) LOGW("vulkan: no geometry shaders, points are not drawn");
    if (!f2.features.textureCompressionBC) LOGW("vulkan: no BC texture compression, DXT textures are not drawn");
    if (robust2 && robust_f.nullDescriptor) {
        want.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        robust_on.nullDescriptor = VK_TRUE;
        *tail = &robust_on, tail = &robust_on.pNext;
        null_descriptor_ = true;
    }
    if (border && border_f.customBorderColors && border_f.customBorderColorWithoutFormat) {
        want.push_back(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
        border_on.customBorderColors = VK_TRUE;
        border_on.customBorderColorWithoutFormat = VK_TRUE;
        *tail = &border_on, tail = &border_on.pNext;
        custom_border_ = true;
    }
    // ray tracing: the vertex shaders write the scene's positions (stores), acceleration structures are built from
    // buffers' addresses, ray queries read them, a hit's texture is one of an array (cut-outs, reflections)
    if (rt_exts && as_f.accelerationStructure && rq_f.rayQuery && f12.bufferDeviceAddress &&
        f2.features.vertexPipelineStoresAndAtomics && f12.shaderSampledImageArrayNonUniformIndexing) {
        want.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        want.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        want.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        as_on.accelerationStructure = VK_TRUE;
        rq_on.rayQuery = VK_TRUE;
        on12.bufferDeviceAddress = VK_TRUE;
        on12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        on.features.vertexPipelineStoresAndAtomics = VK_TRUE;
        *tail = &as_on, as_on.pNext = &rq_on, tail = &rq_on.pNext;
        rt_ok_ = true;
    }
    float prio = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext = &on;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = uint32_t(want.size());
    ci.ppEnabledExtensionNames = want.data();
    VkResult r = vkCreateDevice(pd_, &ci, nullptr, &dev_);
    if (r != VK_SUCCESS) {
        LOGE("vulkan: vkCreateDevice failed: %s", VkResultName(r));
        return false;
    }
#define VK_LOAD(f)                                                          \
    f = reinterpret_cast<PFN_##f>(vkGetDeviceProcAddr(dev_, #f));          \
    if (!f) f = reinterpret_cast<PFN_##f>(vkGetInstanceProcAddr(instance_, #f));
    VK_DEVICE_FUNCS(VK_LOAD)
#undef VK_LOAD
#define VK_CHECK_LOADED(f)                                   \
    if (!f) {                                                \
        LOGE("vulkan: %s missing from the driver", #f);      \
        return false;                                        \
    }
    VK_DEVICE_FUNCS(VK_CHECK_LOADED)
#undef VK_CHECK_LOADED
    if (rt_ok_) {
#define VK_LOAD_RT(f) f = reinterpret_cast<PFN_##f>(vkGetDeviceProcAddr(dev_, #f)), rt_ok_ = rt_ok_ && f;
        VK_RT_FUNCS(VK_LOAD_RT)
#undef VK_LOAD_RT
        if (!rt_ok_) LOGW("vulkan: the driver lists ray queries but not all their entry points; no ray tracing");
    }
    if (rt_ok_) {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR ap{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &ap;
        if (auto get = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceProperties2")))
            get(pd_, &p2);
        rt_scratch_align_ = std::max<VkDeviceSize>(ap.minAccelerationStructureScratchOffsetAlignment, 256);
    }
    SetRayTracingSupported(rt_ok_);
    LOGR("vulkan: ray tracing (ray queries) %s", rt_ok_ ? "available" : rt_off ? "turned off (XBE_NO_RT)" : "not offered by this GPU or driver");
    vkGetDeviceQueue(dev_, family_, 0, &queue_);
    alloc_.Init(pd_, dev_);
    // D24S8 where the GPU has it (it keeps the console's depth bias units), else D32 with stencil
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(pd_, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    depth_fmt_ = (fp.optimalTilingFeatures & need) == need ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
    LOGR("vulkan: depth %s, null descriptors %s, custom border colours %s", depth_fmt_ == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8",
         null_descriptor_ ? "yes" : "no", custom_border_ ? "yes" : "no");
    return true;
}

// the window's screen: its peak brightness (DXGI's) and the one Windows shows SDR white at in HDR (its "SDR content
// brightness"), in nits; 0 where unknown
static void DisplayLuminance(HWND wnd, float& peak, float& paper) {
    peak = paper = 0;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
    Microsoft::WRL::ComPtr<IDXGIFactory1> f;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; !peak && f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            Microsoft::WRL::ComPtr<IDXGIOutput> o;
            for (UINT j = 0; a->EnumOutputs(j, &o) != DXGI_ERROR_NOT_FOUND; j++) {
                Microsoft::WRL::ComPtr<IDXGIOutput6> o6;
                DXGI_OUTPUT_DESC1 d;
                if (SUCCEEDED(o.As(&o6)) && SUCCEEDED(o6->GetDesc1(&d)) && !wcscmp(d.DeviceName, mi.szDevice)) {
                    peak = d.MaxLuminance;
                    break;
                }
            }
        }
    }
    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return;
    for (UINT32 i = 0; i < np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(src), paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || wcscmp(src.viewGdiDeviceName, mi.szDevice)) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL wl{};
        wl.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(wl), paths[i].targetInfo.adapterId, paths[i].targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&wl.header) == ERROR_SUCCESS) paper = float(wl.SDRWhiteLevel) / 1000.0f * 80.0f;
        break;
    }
}

bool VulkanBackend::CreateSwapchain() {
    RECT rc;
    GetClientRect(static_cast<HWND>(app::Window()), &rc);
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd_, surface_, &caps);
    uint32_t w = std::max<LONG>(rc.right - rc.left, 1), h = std::max<LONG>(rc.bottom - rc.top, 1);
    if (caps.currentExtent.width != 0xFFFFFFFFu) w = caps.currentExtent.width, h = caps.currentExtent.height;
    if (!w || !h) return false;  // minimised
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd_, surface_, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd_, surface_, &fn, formats.data());
    VkSurfaceFormatKHR sf = formats.empty() ? VkSurfaceFormatKHR{kBgra, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            sf = f;
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM) break;
        }
    swap_hdr_asked_ = app::Get().hdr;
    swap_hdr_ = false;
    if (swap_hdr_asked_) {
        for (auto& f : formats)
            if ((f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || f.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32) &&
                f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT && colorspaces_) {
                sf = f, swap_hdr_ = true;
                break;
            }
        DisplayLuminance(static_cast<HWND>(app::Window()), display_peak_, display_paper_);
        static bool told = false;
        if (!told || swap_hdr_) {
            told = true;
            LOGR("vulkan: HDR %s; the screen's peak %.0f nits, SDR white %.0f nits", swap_hdr_ ? "on (HDR10)" :
                 colorspaces_ ? "asked, but the screen offers no HDR10 (Windows' HDR off?)" : "asked, but the driver has no HDR colour spaces",
                 display_peak_, display_paper_);
        }
    }
    swap_fmt_ = sf.format;
    uint32_t pn = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd_, surface_, &pn, nullptr);
    std::vector<VkPresentModeKHR> modes(pn);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd_, surface_, &pn, modes.data());
    swap_vsync_ = app::Get().vsync;
    present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    if (!swap_vsync_) {
        for (VkPresentModeKHR m : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            if (std::find(modes.begin(), modes.end(), m) != modes.end()) {
                present_mode_ = m;
                break;
            }
    }
    uint32_t images = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount) images = std::min(images, caps.maxImageCount);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = images;
    ci.imageFormat = sf.format;
    ci.imageColorSpace = sf.colorSpace;
    ci.imageExtent = {w, h};
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present_mode_;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swap_;
    VkSwapchainKHR made = VK_NULL_HANDLE;
    VkResult r = vkCreateSwapchainKHR(dev_, &ci, nullptr, &made);
    if (swap_) DestroySwapchain();
    if (r != VK_SUCCESS) {
        LOGE("vulkan: no swap chain: %s", VkResultName(r));
        return false;
    }
    swap_ = made;
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(dev_, swap_, &n, nullptr);
    swap_images_.resize(n);
    vkGetSwapchainImagesKHR(dev_, swap_, &n, swap_images_.data());
    for (VkImage img : swap_images_) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swap_fmt_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v = VK_NULL_HANDLE;
        vkCreateImageView(dev_, &vi, nullptr, &v);
        swap_views_.push_back(v);
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s = VK_NULL_HANDLE;
        vkCreateSemaphore(dev_, &sci, nullptr, &s);
        present_sem_.push_back(s);
    }
    bb_w_ = w;
    bb_h_ = h;
    return true;
}

// the GPU is idle first: the old images and semaphores may still be in use
void VulkanBackend::DestroySwapchain() {
    WaitIdle();
    for (VkImageView v : swap_views_) vkDestroyImageView(dev_, v, nullptr);
    for (VkSemaphore s : present_sem_) vkDestroySemaphore(dev_, s, nullptr);
    swap_views_.clear();
    present_sem_.clear();
    swap_images_.clear();
    vkDestroySwapchainKHR(dev_, swap_, nullptr);
    swap_ = VK_NULL_HANDLE;
}

bool VulkanBackend::CreateFixed() {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family_;
    if (vkCreateCommandPool(dev_, &pci, nullptr, &pool_) != VK_SUCCESS) return false;
    VkSemaphoreTypeCreateInfo tci{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    sci.pNext = &tci;
    if (vkCreateSemaphore(dev_, &sci, nullptr, &timeline_) != VK_SUCCESS) return false;
    VkSemaphoreCreateInfo bci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& s : acquire_sem_) vkCreateSemaphore(dev_, &bci, nullptr, &s);

    // one layout for every draw and pass: two constant buffers, four textures, four samplers, pushed per draw
    VkDescriptorSetLayoutBinding b[kBindCapture + 1]{};
    for (uint32_t i = 0; i < kBindings; i++) {
        b[i].binding = i;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
        b[i].descriptorType = i < kBindTex ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                              : i < kBindSmp ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                             : VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    b[kBindCapture] = {kBindCapture, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = kBindCapture + 1;
    dl.pBindings = b;
    if (vkCreateDescriptorSetLayout(dev_, &dl, nullptr, &set_layout_) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};  // RtCapture's: where a draw's positions go
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &set_layout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(dev_, &pl, nullptr, &layout_) != VK_SUCCESS) return false;
    // the ray traced passes': the same, and the acceleration structure
    if (rt_ok_) {
        VkDescriptorSetLayoutBinding rb[kBindRtStats + 1]{};
        std::memcpy(rb, b, sizeof(VkDescriptorSetLayoutBinding) * (kBindCapture + 1));
        rb[kBindTlas] = {kBindTlas, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr};
        rb[kBindRtVerts] = {kBindRtVerts, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        rb[kBindRtTris] = {kBindRtTris, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        rb[kBindRtMats] = {kBindRtMats, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        rb[kBindRtStats] = {kBindRtStats, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo rdl = dl;
        rdl.bindingCount = kBindRtStats + 1;
        rdl.pBindings = rb;
        // set 1: the frame's textures and their sampler (repeating, linear)
        VkDescriptorSetLayoutBinding tb[2] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kRtTextures, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo tdl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        tdl.bindingCount = 2;
        tdl.pBindings = tb;
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.maxLod = VK_LOD_CLAMP_NONE;
        VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kRtTextures * kFramesInFlight},
                                      {VK_DESCRIPTOR_TYPE_SAMPLER, kFramesInFlight}};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpi.maxSets = kFramesInFlight;
        dpi.poolSizeCount = 2;
        dpi.pPoolSizes = ps;
        VkDescriptorSetLayout sets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
        VkPipelineLayoutCreateInfo rpl = pl;
        rpl.setLayoutCount = 2;
        rpl.pSetLayouts = sets;
        bool made = vkCreateDescriptorSetLayout(dev_, &rdl, nullptr, &rt_set_layout_) == VK_SUCCESS &&
                    vkCreateDescriptorSetLayout(dev_, &tdl, nullptr, &rt_tex_layout_) == VK_SUCCESS &&
                    vkCreateSampler(dev_, &sci, nullptr, &rt_sampler_) == VK_SUCCESS &&
                    vkCreateDescriptorPool(dev_, &dpi, nullptr, &rt_pool_) == VK_SUCCESS;
        sets[0] = rt_set_layout_, sets[1] = rt_tex_layout_;
        for (RtFrame& f : rt_) {
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = rt_pool_;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &rt_tex_layout_;
            made = made && vkAllocateDescriptorSets(dev_, &ai, &f.tex_set) == VK_SUCCESS;
            f.tex_written = kRtTextures;  // all of them the dummy at the first frame
        }
        if (!made || vkCreatePipelineLayout(dev_, &rpl, nullptr, &rt_layout_) != VK_SUCCESS) {
            LOGW("vulkan: the ray traced passes' layout could not be made; no ray tracing");
            rt_ok_ = false;
            SetRayTracingSupported(false);
        }
    }

    // the pipeline cache kept beside the exe
    std::vector<char> cache_data;
    if (std::ifstream in{app::ExeDir() / "pipelinecache_vk.bin", std::ios::binary})
        cache_data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    VkPipelineCacheCreateInfo cci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cci.initialDataSize = cache_data.size();
    cci.pInitialData = cache_data.empty() ? nullptr : cache_data.data();
    if (vkCreatePipelineCache(dev_, &cci, nullptr, &cache_) != VK_SUCCESS) {
        cci.initialDataSize = 0;
        cci.pInitialData = nullptr;
        vkCreatePipelineCache(dev_, &cci, nullptr, &cache_);
    }
    LOGR("vulkan: pipeline cache %zu bytes from earlier runs", cache_data.size());

    for (int i = 0; i < 4; i++) {
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.magFilter = sc.minFilter = i == 0 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sc.mipmapMode = i == 0 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sc.maxLod = VK_LOD_CLAMP_NONE;
        vkCreateSampler(dev_, &sc, nullptr, &pass_samplers_[i]);
    }
    if (kQueries) {
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qi.queryType = VK_QUERY_TYPE_OCCLUSION;
        qi.queryCount = kQueries;
        if (host_reset_ && vkCreateQueryPool(dev_, &qi, nullptr, &queries_) == VK_SUCCESS) {
            vkResetQueryPool(dev_, queries_, 0, kQueries);
            for (uint32_t i = kQueries; i-- > 0;) free_queries_.push_back(i);
        } else {
            LOGW("vulkan: no host query reset, visibility tests stay pending");
        }
    }
    // GPU timings: four timestamps a frame slot
    {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, qf.data());
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = 4 * kFramesInFlight;
        if (host_reset_ && qf[family_].timestampValidBits && vkCreateQueryPool(dev_, &qi, nullptr, &stamps_) == VK_SUCCESS)
            vkResetQueryPool(dev_, stamps_, 0, 4 * kFramesInFlight);
    }
    // the gamma lookup: one BGRA texel per ramp entry; the stand-in texture
    const VkImageUsageFlags sampled = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!MakeImage(lut_, VK_IMAGE_TYPE_2D, kBgra, 256, 1, 1, 1, 1, sampled) ||
        !MakeImage(dummy_, VK_IMAGE_TYPE_2D, kBgra, 1, 1, 1, 1, 1, sampled))
        return false;
    uint32_t white = 0xFFFFFFFFu;
    UploadLevel(dummy_, 0, 0, 1, 1, 1, &white, 4);
    return true;
}

bool VulkanBackend::InitUi() {
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 16;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(dev_, &pi, nullptr, &ui_pool_) != VK_SUCCESS) return false;
    auto loader = [](const char* name, void*) -> PFN_vkVoidFunction { return vkGetInstanceProcAddr(g_instance, name); };
    if (!ImGui_ImplVulkan_LoadFunctions(loader)) return false;
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = instance_;
    ii.PhysicalDevice = pd_;
    ii.Device = dev_;
    ii.QueueFamily = family_;
    ii.Queue = queue_;
    ii.DescriptorPool = ui_pool_;
    ii.MinImageCount = 2;
    ii.ImageCount = uint32_t(std::max<size_t>(swap_images_.size(), 2));
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.PipelineCache = cache_;
    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    // the overlay is drawn into the screen's format, or into hdr_ui_ (BGRA) on an HDR10 chain
    ui_fmt_ = swap_hdr_ ? kBgra : swap_fmt_;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &ui_fmt_;
    return ImGui_ImplVulkan_Init(&ii);
}

bool VulkanBackend::Init(uint32_t width, uint32_t height) {
    guest_w_ = width;
    guest_h_ = height;
    aspect_ = app::FrameAspect();
    scale_ = app::RenderScale(guest_h_);
    if (!LoadVulkan() || !CreateInstance() || !PickDevice() || !CreateDevice() || !CreateFixed() || !CreateSwapchain())
        return false;
    if (!dxc_.Load()) return false;
    // the shaders the passes need, compiled once now: a DXC that cannot make SPIR-V fails here, not mid-game
    uint64_t h;
    if (!Compile(kBlitHlsl, "vs", "vs_5_0", h) || !Compile(kBlitHlsl, "psmain", "ps_5_0", h)) {
        LOGE("vulkan: the pass shaders do not compile to SPIR-V (a dxcompiler.dll without SPIR-V?)");
        return false;
    }
    LoadStyle();
    SetStyleRendererActive(true);
    ui_ = InitUi();
    if (!ui_) LOGW("vulkan: the overlay could not start");
    if (g_extension) LOGW("vulkan: the renderer extension is not used under Vulkan");
    LOGR("vulkan: drawing renderer, %ux%u guest, %ux%u window, render scale %.3f", width, height, bb_w_, bb_h_, scale_);
    return true;
}

VulkanBackend::~VulkanBackend() {
    if (!dev_) {
        if (instance_) {
            if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
            vkDestroyInstance(instance_, nullptr);
        }
        return;
    }
    WaitIdle();
    SavePipelineCache();
    if (ui_) ImGui_ImplVulkan_Shutdown();
    for (auto& [k, t] : targets_) DropImage(t.res), DropImage(t.fb), DropImage(t.shrunk), DropScene(t);
    targets_.clear();
    for (auto& [k, t] : textures_) DropImage(t.img);
    textures_.clear();
    DropImage(lut_);
    DropImage(smaa_area_);
    for (auto& [k, m] : materials_) DropImage(m);
    for (Image& d : details_) DropImage(d);
    DropImage(smaa_search_);
    DropImage(dummy_);
    for (Chunk& c : chunks_) DropBuffer(c.b);
    DropBuffer(readback_);
    if (rt_ok_)
        for (RtFrame& f : rt_) DropRt(f);
    for (auto& [k, l] : late_)
        for (Buffer& b : l.staging) DropBuffer(b);
    if (cmd_) Submit();
    vkDeviceWaitIdle(dev_);
    for (auto& [serial, f] : retire_) f();
    retire_.clear();
    for (auto& [k, m] : modules_) vkDestroyShaderModule(dev_, m, nullptr);
    for (auto& [k, p] : pipelines_) vkDestroyPipeline(dev_, p, nullptr);
    for (auto& [k, p] : pass_pipelines_) vkDestroyPipeline(dev_, p, nullptr);
    for (auto& [k, s] : samplers_) vkDestroySampler(dev_, s, nullptr);
    for (VkSampler s : pass_samplers_) vkDestroySampler(dev_, s, nullptr);
    if (queries_) vkDestroyQueryPool(dev_, queries_, nullptr);
    if (stamps_) vkDestroyQueryPool(dev_, stamps_, nullptr);
    if (ui_pool_) vkDestroyDescriptorPool(dev_, ui_pool_, nullptr);
    vkDestroyPipelineCache(dev_, cache_, nullptr);
    vkDestroyPipelineLayout(dev_, layout_, nullptr);
    vkDestroyDescriptorSetLayout(dev_, set_layout_, nullptr);
    if (rt_layout_) vkDestroyPipelineLayout(dev_, rt_layout_, nullptr);
    if (rt_set_layout_) vkDestroyDescriptorSetLayout(dev_, rt_set_layout_, nullptr);
    if (rt_tex_layout_) vkDestroyDescriptorSetLayout(dev_, rt_tex_layout_, nullptr);
    if (rt_pool_) vkDestroyDescriptorPool(dev_, rt_pool_, nullptr);
    if (rt_sampler_) vkDestroySampler(dev_, rt_sampler_, nullptr);
    if (swap_) DestroySwapchain();
    for (VkSemaphore s : acquire_sem_) vkDestroySemaphore(dev_, s, nullptr);
    vkDestroySemaphore(dev_, timeline_, nullptr);
    vkDestroyCommandPool(dev_, pool_, nullptr);
    vkDestroyDevice(dev_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

void VulkanBackend::SavePipelineCache() {
    size_t n = 0;
    if (!cache_ || vkGetPipelineCacheData(dev_, cache_, &n, nullptr) != VK_SUCCESS || !n) return;
    std::vector<char> data(n);
    if (vkGetPipelineCacheData(dev_, cache_, &n, data.data()) != VK_SUCCESS) return;
    std::ofstream out(app::ExeDir() / "pipelinecache_vk.bin", std::ios::binary);
    out.write(data.data(), std::streamsize(n));
}

// ---- submission: one command buffer recording at a time, every submission signalling the timeline

VkCommandBuffer VulkanBackend::Cmd() {
    if (cmd_) return cmd_;
    uint64_t done = Completed();
    for (auto& [cb, serial] : cmds_) {
        if (serial > done) continue;
        cmd_ = cb;
        serial = UINT64_MAX;  // in use until submitted
        vkResetCommandBuffer(cb, 0);
        break;
    }
    if (!cmd_) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(dev_, &ai, &cmd_);
        cmds_.push_back({cmd_, UINT64_MAX});
    }
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_, &bi);
    ForgetDrawState();
    if (frame_stamp_due_) {
        frame_stamp_due_ = false;
        Stamp(0);
    }
    return cmd_;
}

void VulkanBackend::Submit(VkSemaphore wait, VkSemaphore signal) {
    if (!cmd_ && !wait && !signal) return;
    Cmd();
    End();
    vkEndCommandBuffer(cmd_);
    uint64_t value = ++serial_;
    for (auto& [cb, serial] : cmds_)
        if (cb == cmd_) serial = value;
    VkSemaphore signals[2] = {timeline_, signal};
    uint64_t values[2] = {value, 0};
    uint64_t wait_value = 0;
    VkTimelineSemaphoreSubmitInfo ti{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    ti.waitSemaphoreValueCount = wait ? 1 : 0;
    ti.pWaitSemaphoreValues = &wait_value;
    ti.signalSemaphoreValueCount = signal ? 2 : 1;
    ti.pSignalSemaphoreValues = values;
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.pNext = &ti;
    si.waitSemaphoreCount = wait ? 1 : 0;
    si.pWaitSemaphores = &wait;
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = signal ? 2 : 1;
    si.pSignalSemaphores = signals;
    VkResult r;
    {
        PerfScope pq{perf_submit_};
        r = vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    }
    if (r != VK_SUCCESS) LOG_FIRST(8, "vulkan: vkQueueSubmit failed: %s", VkResultName(r));
    cmd_ = VK_NULL_HANDLE;
    dirty_ = true;  // the next command buffer has no memory dependency on this one's writes without a barrier
    perf_submits_++;
}

uint64_t VulkanBackend::Completed() {
    uint64_t v = 0;
    if (vkGetSemaphoreCounterValue(dev_, timeline_, &v) == VK_SUCCESS) completed_ = std::max(completed_, v);
    return completed_;
}

void VulkanBackend::Wait(uint64_t serial) {
    if (serial > serial_) Submit();  // recorded, not yet handed over
    if (Completed() >= serial) return;
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &timeline_;
    wi.pValues = &serial;
    auto t0 = std::chrono::steady_clock::now();
    VkResult r = vkWaitSemaphores(dev_, &wi, 5'000'000'000ull);
    perf_wait_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (r != VK_SUCCESS) LOG_FIRST(8, "vulkan: waiting for the GPU: %s", VkResultName(r));
    Completed();
}

void VulkanBackend::WaitIdle() {
    if (cmd_) Submit();
    if (dev_) vkDeviceWaitIdle(dev_);
    Completed();
    Collect();
}

// objects the GPU is done with
void VulkanBackend::Collect() {
    uint64_t done = Completed();
    while (!retire_.empty() && retire_.front().first <= done) {
        retire_.front().second();
        retire_.pop_front();
    }
}

// ---- rendering instances and barriers

void VulkanBackend::Begin(const Attachments& a) {
    if (open_ && cur_ == a) return;
    End();
    VkCommandBuffer cb = Cmd();
    if (dirty_) Barrier();
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = a.color;
    color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth = color;
    depth.imageView = a.depth;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {a.w, a.h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = a.color ? 1 : 0;
    ri.pColorAttachments = a.color ? &color : nullptr;
    ri.pDepthAttachment = a.depth ? &depth : nullptr;
    ri.pStencilAttachment = a.depth ? &depth : nullptr;
    vkCmdBeginRendering(cb, &ri);
    open_ = true;
    cur_ = a;
    ForgetDrawState();
}

void VulkanBackend::End() {
    if (!open_) return;
    vkCmdEndRendering(cmd_);
    open_ = false;
    cur_ = {};
    dirty_ = true;
}

// everything before against everything after: the images all stay GENERAL
void VulkanBackend::Barrier() {
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(Cmd(), &di);
    dirty_ = false;
}

void VulkanBackend::ToGeneral(VkImage img, bool depth, uint32_t layers) {
    End();
    VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ib.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ib.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = img;
    ib.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                    : VK_IMAGE_ASPECT_COLOR_BIT),
                           0, VK_REMAINING_MIP_LEVELS, 0, layers};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &ib;
    vkCmdPipelineBarrier2(Cmd(), &di);
}

// ---- resources

bool VulkanBackend::MakeImage(Image& im, VkImageType type, VkFormat fmt, uint32_t w, uint32_t h, uint32_t d,
                              uint32_t levels, uint32_t layers, VkImageUsageFlags usage, bool cube) {
    im = Image{};
    im.depth = fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.imageType = type;
    ci.format = fmt;
    ci.extent = {w, h, d};
    ci.mipLevels = levels;
    ci.arrayLayers = layers;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev_, &ci, nullptr, &im.img) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev_, im.img, &req);
    if (!alloc_.Alloc(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, im.mem) ||
        vkBindImageMemory(dev_, im.img, im.mem.mem, im.mem.off) != VK_SUCCESS) {
        vkDestroyImage(dev_, im.img, nullptr);
        im.img = VK_NULL_HANDLE;
        alloc_.Free(im.mem);
        return false;
    }
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.img;
    vi.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {VkImageAspectFlags(im.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0,
                           levels, 0, layers};
    vkCreateImageView(dev_, &vi, nullptr, &im.view);
    if (im.depth) {
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        vkCreateImageView(dev_, &vi, nullptr, &im.att);
    } else if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        im.att = im.view;
    }
    im.fmt = fmt;
    im.w = w;
    im.h = h;
    im.levels = levels;
    im.layers = layers;
    ToGeneral(im.img, im.depth, layers);
    return true;
}

// destroyed once the GPU is past every use recorded so far
void VulkanBackend::DropImage(Image& im) {
    if (!im.img) return;
    if (open_ && (cur_.color == im.att || cur_.depth == im.att || cur_.color == im.view)) End();
    VkImage img = im.img;
    VkImageView view = im.view, att = im.att != im.view ? im.att : VK_NULL_HANDLE;
    Mem m = im.mem;
    Retire([this, img, view, att, m]() mutable {
        if (att) vkDestroyImageView(dev_, att, nullptr);
        vkDestroyImageView(dev_, view, nullptr);
        vkDestroyImage(dev_, img, nullptr);
        alloc_.Free(m);
    });
    im = Image{};
}

bool VulkanBackend::MakeBuffer(Buffer& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
    b = Buffer{};
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev_, &ci, nullptr, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev_, b.buf, &req);
    VkMemoryPropertyFlags want = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                      : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    // reads back cached; streams to the GPU from memory it reads fast where there is some (resizable BAR)
    VkMemoryPropertyFlags prefer = host ? (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT
                                                                                   : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                                        : 0;
    bool address = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
    if (!(address ? alloc_.Addressable(req, want, b.mem) : alloc_.Alloc(req, want, prefer, true, b.mem)) ||
        vkBindBufferMemory(dev_, b.buf, b.mem.mem, b.mem.off) != VK_SUCCESS) {
        vkDestroyBuffer(dev_, b.buf, nullptr);
        b.buf = VK_NULL_HANDLE;
        alloc_.Free(b.mem);
        return false;
    }
    b.size = size;
    return true;
}

void VulkanBackend::DropBuffer(Buffer& b) {
    if (!b.buf) return;
    VkBuffer buf = b.buf;
    Mem m = b.mem;
    Retire([this, buf, m]() mutable {
        vkDestroyBuffer(dev_, buf, nullptr);
        alloc_.Free(m);
    });
    b = Buffer{};
}

// bytes the GPU reads once, in the submission being recorded: from the chunk being filled, a finished one, or a new one
Stretch VulkanBackend::Stream(VkDeviceSize bytes, VkDeviceSize align) {
    align = std::max<VkDeviceSize>(align, std::max<VkDeviceSize>(props_.limits.minUniformBufferOffsetAlignment, 16));
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (bytes > kStreamChunk) {
        Buffer own;
        if (!MakeBuffer(own, bytes, usage, true)) return {};
        Stretch s{own.buf, 0, own.mem.map};
        DropBuffer(own);  // freed once this submission is done with it
        return s;
    }
    if (!chunks_.empty()) {
        Chunk& c = chunks_[0];
        VkDeviceSize at = (c.used + align - 1) / align * align;
        if (at + bytes <= c.b.size) {
            c.used = at + bytes;
            c.serial = serial_ + 1;
            return {c.b.buf, at, c.b.mem.map + at};
        }
    }
    // the full chunk goes to the back; a finished one comes to the front, or a new one
    uint64_t done = Completed();
    size_t pick = 0;
    for (size_t i = 1; i < chunks_.size() && !pick; i++)
        if (chunks_[i].serial <= done) pick = i;
    if (pick) {
        std::rotate(chunks_.begin(), chunks_.begin() + pick, chunks_.begin() + pick + 1);
    } else {
        Chunk c;
        if (!MakeBuffer(c.b, kStreamChunk, usage, true)) return {};
        chunks_.insert(chunks_.begin(), std::move(c));
    }
    Chunk& c = chunks_[0];
    c.used = bytes;
    c.serial = serial_ + 1;
    return {c.b.buf, 0, c.b.mem.map};
}

VkSampler VulkanBackend::Sampler(const D3D12_SAMPLER_DESC& d) {
    uint64_t key = Hash(&d, sizeof(d));
    auto& s = samplers_[key];
    if (s) return s;
    uint32_t f = uint32_t(d.Filter);
    bool aniso = (f & 0x40) != 0;
    bool compare = ((f >> 7) & 3) == 1;
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc.minFilter = ((f >> 4) & 3) || aniso ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sc.magFilter = ((f >> 2) & 3) || aniso ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sc.mipmapMode = (f & 3) || aniso ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    auto address = [&](D3D12_TEXTURE_ADDRESS_MODE m) {
        VkSamplerAddressMode a = VkAddress(m);
        if (a == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE && !mirror_once_) a = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        return a;
    };
    sc.addressModeU = address(d.AddressU);
    sc.addressModeV = address(d.AddressV);
    sc.addressModeW = address(d.AddressW);
    sc.mipLodBias = std::clamp(d.MipLODBias, -props_.limits.maxSamplerLodBias, props_.limits.maxSamplerLodBias);
    sc.anisotropyEnable = aniso && d.MaxAnisotropy > 1 && aniso_ok_;
    sc.maxAnisotropy = std::clamp(float(d.MaxAnisotropy), 1.0f, props_.limits.maxSamplerAnisotropy);
    sc.compareEnable = compare;
    sc.compareOp = VkCmp(d.ComparisonFunc);
    sc.minLod = d.MinLOD;
    sc.maxLod = d.MaxLOD >= 3.0e38f ? VK_LOD_CLAMP_NONE : std::max(d.MaxLOD, d.MinLOD);
    VkSamplerCustomBorderColorCreateInfoEXT bc{VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    bool border = sc.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
                  sc.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
                  sc.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    if (border && custom_border_) {
        std::memcpy(bc.customBorderColor.float32, d.BorderColor, sizeof(d.BorderColor));
        bc.format = VK_FORMAT_UNDEFINED;
        sc.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
        sc.pNext = &bc;
    } else {
        // the nearest of the fixed ones
        const float* c = d.BorderColor;
        bool white = c[0] + c[1] + c[2] > 1.5f;
        sc.borderColor = c[3] < 0.5f ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
                         : white     ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                     : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
    if (vkCreateSampler(dev_, &sc, nullptr, &s) != VK_SUCCESS) LOG_FIRST(4, "vulkan: a sampler could not be made");
    return s;
}

// ---- targets

uint64_t VulkanBackend::SizedTargetKey(uint32_t data, bool depth, uint32_t gw, uint32_t gh) const {
    uint64_t key = TargetMapKey(data, depth);
    bool small_depth = depth && (gw != guest_w_ || gh != guest_h_);
    if (!small_depth) return key;
    return key | uint64_t(gw) << kKeyWidthShift | uint64_t(gh) << kKeyHeightShift;
}

Target* VulkanBackend::FindTarget(const Surface& s, bool depth) {
    if (!s.header) return nullptr;
    uint32_t data = mem::VirtualFromPhysical(s.data);
    uint32_t gw, gh;
    SurfaceSize(s, gw, gh);
    auto it = targets_.find(SizedTargetKey(data, depth, gw, gh));
    if (it == targets_.end() || it->second.gw != gw || it->second.gh != gh) return nullptr;
    target_used_[data] = presents_;
    return &it->second;
}

bool VulkanBackend::CreateTarget(Target& t) {
    t.w = std::max(1u, uint32_t(std::lround(t.gw * scale_ * XStretch(aspect_, guest_h_, t.gw, t.gh))));
    t.h = std::max(1u, uint32_t(std::lround(t.gh * scale_)));
    const VkImageUsageFlags common = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (t.depth) {
        if (!MakeImage(t.res, VK_IMAGE_TYPE_2D, depth_fmt_, t.w, t.h, 1, 1, 1,
                       common | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
            return false;
    } else if (!MakeImage(t.res, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1, common | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        return false;
    }
    // a new target starts cleared: depth 1, colour 0
    Begin(t.depth ? Attachments{VK_NULL_HANDLE, t.res.att, t.w, t.h} : Attachments{t.res.att, VK_NULL_HANDLE, t.w, t.h});
    VkClearAttachment ca{};
    ca.aspectMask = t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    ca.clearValue.depthStencil = {1.0f, 0};
    if (!t.depth) ca.clearValue.color = {{0, 0, 0, 0}};
    VkClearRect cr{{{0, 0}, {t.w, t.h}}, 0, 1};
    vkCmdClearAttachments(cmd_, 1, &ca, 1, &cr);
    return true;
}

void VulkanBackend::ReleaseTarget(std::unordered_map<uint64_t, Target>::iterator it) {
    if (bound_rt_ == &it->second || bound_ds_ == &it->second) Unbind();
    if (scene_rt_ == &it->second || scene_ds_ == &it->second) scene_rt_ = scene_ds_ = nullptr;
    DropImage(it->second.res);
    DropImage(it->second.fb);
    DropScene(it->second);
    DropImage(it->second.shrunk);
    targets_.erase(it);
}

Target* VulkanBackend::EnsureTarget(const Surface& s, bool depth, uint32_t gw, uint32_t gh) {
    if (!s.header) return nullptr;
    uint32_t data = mem::VirtualFromPhysical(s.data);
    uint32_t sw, sh;
    SurfaceSize(s, sw, sh);
    if (gw && gh) sw = gw, sh = gh;
    uint64_t key = SizedTargetKey(data, depth, sw, sh);
    target_used_[data] = presents_;
    auto it = targets_.find(key);
    if (it != targets_.end()) {
        if (it->second.gw == sw && it->second.gh == sh) return &it->second;
        ReleaseTarget(it);
    }
    Target t;
    t.depth = depth;
    t.gw = sw;
    t.gh = sh;
    if (!CreateTarget(t)) {
        LOG_FIRST(8, "vulkan: target %08X %ux%u could not be made", data, sw, sh);
        return nullptr;
    }
    t.data = data;
    TextureDesc d;
    t.linear32 = depth || (Describe(s.header, d) && d.linear && Format(d.format).bits == 32);
    t.swizzled32 = !depth && !t.linear32 && Describe(s.header, d) && Format(d.format).swizzled &&
                   !Format(d.format).compressed && Format(d.format).bits == 32;
    t.bytes = depth ? sw * sh * 4 : GuestPitch(sw) * sh;
    t.epoch = DirtyEpoch(data);
    t.sig = SurfaceSig(data, t.bytes);
    return &(targets_[key] = std::move(t));
}

Target* VulkanBackend::DepthTarget(const State& s) {
    uint32_t rw = 0, rh = 0, zw = 0, zh = 0;
    if (s.rt.header && s.zs.header) {
        SurfaceSize(s.rt, rw, rh);
        SurfaceSize(s.zs, zw, zh);
        if (rw <= zw && rh <= zh && (rw != zw || rh != zh)) return EnsureTarget(s.zs, true, rw, rh);
    }
    return EnsureTarget(s.zs, true);
}

bool VulkanBackend::TargetStale(uint32_t data) const {
    auto it = target_used_.find(data);
    uint64_t used = it == target_used_.end() ? 0 : it->second;
    return presents_ - used > kStaleFrames;
}

Attachments VulkanBackend::TargetAttachments(Target* rt, Target* ds) const {
    Attachments a;
    a.color = rt ? rt->res.att : VK_NULL_HANDLE;
    a.depth = ds ? ds->res.att : VK_NULL_HANDLE;
    const Target* t = rt ? rt : ds;
    a.w = t ? t->w : 0;
    a.h = t ? t->h : 0;
    return a;
}

// which targets the draw goes to (d3d11.cpp BindTargets); the rendering instance opens at the draw
bool VulkanBackend::BindTargets(const State& s) {
    bool depth = (s.rs[kRsZEnable] || s.rs[kRsStencilEnable]) && !DepthOnlySampled(s);
    bool shared = s.rt.header && s.zs.header && s.rt.data == s.zs.data;
    bool colour = SimpleState(s, kMColorWriteEnable) != 0;
    Target* rt = shared && depth && !colour ? nullptr : EnsureTarget(s.rt, false);
    Target* ds = depth && !(shared && colour) ? DepthTarget(s) : nullptr;
    if (rt && ds && (rt->w != ds->w || rt->h != ds->h)) ds = nullptr;
    if (!rt && !ds) return false;
    if (rt && LatchCpuWrite(*rt)) {
        if (++reloads_[rt->data] <= 3)
            LOGR("vulkan: target %08X %ux%u reloaded from guest memory before drawing (frame %llu)", rt->data, rt->gw,
                 rt->gh, (unsigned long long)presents_);
        UploadTarget(*rt, s.rt);
    }
    if (rt) rt->drawn = true;
    if (ds) ds->drawn = true;
    bound_rt_ = rt;
    bound_ds_ = ds;
    return true;
}

// CPU pixels of a colour surface into its target, scaled
void VulkanBackend::UploadTarget(Target& t, const Surface& s) {
    TextureDesc d;
    if (!Describe(s.header, d) || Format(d.format).depth) return;
    std::vector<uint8_t> px;
    uint32_t pitch;
    bool over = !t.drawn && d.width == t.gw && d.height == t.gh && t.read.size() == size_t(t.gw) * t.gh * 4;
    if (over) {
        pitch = t.gw * 4;
        px = LatchCpuChanges(t);
    } else {
        DecodeLevel(d, 0, 0, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), nullptr, px, pitch);
    }
    if (px.size() < size_t(pitch) * d.height) px.resize(size_t(pitch) * d.height);
    Image tmp;
    if (!MakeImage(tmp, VK_IMAGE_TYPE_2D, kBgra, d.width, d.height, 1, 1, 1,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    std::vector<uint8_t> rows(size_t(d.width) * 4 * d.height);
    for (uint32_t y = 0; y < d.height; y++) std::memcpy(&rows[size_t(y) * d.width * 4], &px[size_t(y) * pitch], d.width * 4);
    UploadLevel(tmp, 0, 0, d.width, d.height, 1, rows.data(), rows.size());
    BlitWhole(tmp.view, t.res, {.alpha = true, .over = over});
    DropImage(tmp);
    t.epoch = DirtyEpoch(t.data);
    t.sig = SurfaceSig(t.data, t.bytes);
}

// the target filtered into a new w x h BGRA image
Image VulkanBackend::Shrink(Target& t, uint32_t w, uint32_t h) {
    Image down;
    if (!MakeImage(down, VK_IMAGE_TYPE_2D, kBgra, w, h, 1, 1, 1,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        return {};
    BlitWhole(t.res.view, down, {.alpha = true});
    return down;
}

Image* VulkanBackend::PrepareCopySource(Target& t, Image& shrunk) {
    (void)shrunk;
    if (t.w == t.gw && t.h == t.gh) return &t.res;
    if (!t.shrunk && !MakeImage(t.shrunk, VK_IMAGE_TYPE_2D, kBgra, t.gw, t.gh, 1, 1, 1,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        return nullptr;
    BlitWhole(t.res.view, t.shrunk, {.alpha = true});
    return &t.shrunk;
}

void VulkanBackend::CopyToBuffer(const Image& src, uint32_t w, uint32_t h, VkBuffer dst) {
    End();
    if (dirty_) Barrier();
    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(Cmd(), src.img, VK_IMAGE_LAYOUT_GENERAL, dst, 1, &r);
    // made visible to the CPU, which reads it once the submission is done
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd_, &di);
    dirty_ = true;  // a later draw into the image waits for the copy's read
}

// a BGRA image to the CPU, rows packed; waits for the GPU
bool VulkanBackend::ReadBack(const Image& src, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    VkDeviceSize bytes = VkDeviceSize(w) * h * 4;
    if (readback_.size < bytes) {
        DropBuffer(readback_);
        if (!MakeBuffer(readback_, std::max<VkDeviceSize>(bytes, 4u << 20), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
            return false;
    }
    CopyToBuffer(src, w, h, readback_.buf);
    Submit();
    Wait(serial_);
    out.resize(size_t(bytes));
    std::memcpy(out.data(), readback_.mem.map, size_t(bytes));
    return true;
}

void VulkanBackend::Sync(uint32_t data, uint32_t) {
    PerfScope ps{perf_sync_};
    auto it = targets_.find(TargetMapKey(data, false));
    if (it == targets_.end() || !it->second.drawn || TargetStale(data)) return;
    Target& t = it->second;
    if (!t.linear32 && !t.swizzled32) {
        LOG_FIRST(8, "vulkan: read-back of %08X skipped, not a 32 bit surface", data);
        return;
    }
    Image shrunk;
    Image* src = PrepareCopySource(t, shrunk);
    std::vector<uint8_t> px;
    bool ok = src && ReadBack(*src, t.gw, t.gh, px);
    DropImage(shrunk);
    if (!ok) return;
    uint32_t pitch = GuestPitch(t.gw);
    uint8_t* guest = static_cast<uint8_t*>(kernel::HostPtr(data));
    if (t.swizzled32) {
        for (uint32_t y = 0; y < t.gh; y++)
            for (uint32_t x = 0; x < t.gw; x++)
                std::memcpy(guest + size_t(SwizzledOffset(x, y, t.gw, t.gh)) * 4, &px[(size_t(y) * t.gw + x) * 4], 4);
    } else {
        for (uint32_t y = 0; y < t.gh; y++) std::memcpy(guest + size_t(y) * pitch, &px[size_t(y) * t.gw * 4], t.gw * 4);
    }
    // compared against guest memory row by row, so only a linear surface keeps its pixels
    if (t.swizzled32) t.read.clear();
    else t.read = std::move(px);
    t.drawn = false;
    t.sig = SurfaceSig(t.data, t.bytes);
}

bool VulkanBackend::DeliverLate(uint32_t data) {
    std::lock_guard lock(late_lock_);
    auto it = late_.find(data);
    if (it == late_.end() || it->second.cpu.empty()) return false;
    Late& l = it->second;
    if (presents_seen_.load() - l.cpu_frame > uint64_t(kLateSlots) + 1) return false;
    if (l.cpu_frame > l.delivered) {
        uint32_t pitch = GuestPitch(l.gw);
        uint8_t* dst = static_cast<uint8_t*>(kernel::HostPtr(data));
        for (uint32_t y = 0; y < l.gh; y++) std::memcpy(dst + size_t(y) * pitch, &l.cpu[size_t(y) * l.gw * 4], l.gw * 4);
        l.delivered = l.cpu_frame;
    }
    return true;
}

void VulkanBackend::RecordLate(uint32_t data) {
    auto it = targets_.find(TargetMapKey(data, false));
    if (it == targets_.end() || TargetStale(data) || !it->second.linear32) return;
    Target& t = it->second;
    t.sig = SurfaceSig(t.data, t.bytes);
    t.read.clear();
    std::lock_guard lock(late_lock_);
    Late& l = late_[data];
    if (l.gw != t.gw || l.gh != t.gh) {
        for (Buffer& b : l.staging) DropBuffer(b);
        l = Late{};
        l.gw = t.gw;
        l.gh = t.gh;
    }
    // finished copies to the CPU, the newest kept
    uint64_t done = Completed();
    for (int i = 0; i < kLateSlots; i++) {
        if (!l.frame[i] || l.serial[i] > done) continue;
        if (l.frame[i] >= l.cpu_frame) {
            l.cpu.resize(size_t(l.gw) * l.gh * 4);
            std::memcpy(l.cpu.data(), l.staging[i].mem.map, l.cpu.size());
            l.cpu_frame = l.frame[i];
        }
        l.frame[i] = 0;
    }
    uint32_t slot = l.next;
    if (l.frame[slot]) return;
    if (!l.staging[slot].buf &&
        !MakeBuffer(l.staging[slot], VkDeviceSize(l.gw) * l.gh * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
        return;
    Image shrunk;
    Image* src = PrepareCopySource(t, shrunk);
    if (!src) return;
    CopyToBuffer(*src, l.gw, l.gh, l.staging[slot].buf);
    DropImage(shrunk);
    l.frame[slot] = presents_ + 1;
    l.serial[slot] = serial_ + 1;
    l.next = (slot + 1) % kLateSlots;
}

// ---- shaders and full screen passes

// HLSL to SPIR-V: the D3D profile's stage at shader model 6, registers as the descriptor bindings lay them out, every
// entry point named "main" in the module
VkShaderModule VulkanBackend::Compile(const std::string& source, const char* entry, const char* profile, uint64_t& hash) {
    std::string src = profile[0] == 'v' ? LocateInputs(source) : source;
    uint64_t profile_seed = Hash(profile, std::strlen(profile), 0x5350495256ull);  // "SPIRV": apart from D3D blobs
    uint64_t entry_seed = Hash(entry, std::strlen(entry), profile_seed);
    hash = Hash(src.data(), src.size(), entry_seed);
    if (modules_.count(hash)) return modules_[hash];
    std::vector<uint32_t> code;
    if (ComPtr<ID3DBlob> kept; CachedShader(hash, kept.GetAddressOf())) {
        code.resize(kept->GetBufferSize() / 4);
        std::memcpy(code.data(), kept->GetBufferPointer(), code.size() * 4);
    } else {
        if (!dxc_.Load()) return modules_[hash] = VK_NULL_HANDLE;
        std::wstring wentry(entry, entry + std::strlen(entry));
        // a 6.x profile (ray queries: 6.5) as it is, on Vulkan 1.2's SPIR-V; the D3D 5 ones at 6.0
        bool sm6 = profile[3] == '6';
        std::wstring target = sm6 ? std::wstring(profile, profile + std::strlen(profile))
                                  : std::wstring(1, wchar_t(profile[0])) + L"s_6_0";
        std::vector<const wchar_t*> args = {L"-E", wentry.c_str(), L"-T", target.c_str(), L"-spirv", L"-HV", L"2018",
                                            sm6 ? L"-fspv-target-env=vulkan1.2" : L"-fspv-target-env=vulkan1.1",
                                            L"-fvk-use-dx-layout", L"-O1",
                                            L"-fvk-b-shift", L"0", L"0", L"-fvk-t-shift", L"2", L"0",
                                            L"-fvk-s-shift", L"6", L"0", L"-fvk-u-shift", L"10", L"0",
                                            L"-fspv-entrypoint-name=main"};
        if (profile[0] == 'p') args.push_back(L"-fvk-use-dx-position-w");
        DxcBuffer buf{src.data(), src.size(), DXC_CP_UTF8};
        ComPtr<IDxcResult> res;
        HRESULT status = E_FAIL;
        if (SUCCEEDED(dxc_.compiler->Compile(&buf, args.data(), UINT32(args.size()), nullptr, IID_PPV_ARGS(&res))))
            res->GetStatus(&status);
        ComPtr<IDxcBlob> obj;
        if (SUCCEEDED(status)) res->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&obj), nullptr);
        if (FAILED(status) || !obj || !obj->GetBufferSize()) {
            ComPtr<IDxcBlobUtf8> err;
            if (res) res->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&err), nullptr);
            LOGE("vulkan: %s compile failed: %s", profile, err && err->GetStringLength() ? err->GetStringPointer() : "?");
            LOGE("%s", src.c_str());
            return modules_[hash] = VK_NULL_HANDLE;
        }
        code.resize(obj->GetBufferSize() / 4);
        std::memcpy(code.data(), obj->GetBufferPointer(), code.size() * 4);
        g_shaders_compiled.fetch_add(1, std::memory_order_relaxed);
        ComPtr<ID3DBlob> keep;
        if (SUCCEEDED(D3DCreateBlob(code.size() * 4, &keep))) {
            std::memcpy(keep->GetBufferPointer(), code.data(), code.size() * 4);
            KeepShader(hash, keep.Get());
        }
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size() * 4;
    ci.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev_, &ci, nullptr, &m) != VK_SUCCESS) LOG_FIRST(8, "vulkan: a shader module could not be made");
    return modules_[hash] = m;
}

VkShaderModule VulkanBackend::Module(uint64_t hash) {
    auto it = modules_.find(hash);
    return it == modules_.end() ? VK_NULL_HANDLE : it->second;
}

VkPipeline VulkanBackend::PassPipeline(const PassKey& k) {
    uint64_t h = Hash(&k, sizeof(k));
    auto it = pass_pipelines_.find(h);
    if (it != pass_pipelines_.end()) return it->second;
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, Module(k.vs), "main"};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, Module(k.ps), "main"};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = ds.depthWriteEnable = k.zmode == 1;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xF;
    if (k.blend == 1) {
        // premultiplied colour, the target keeps its alpha
        ba.blendEnable = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = k.color ? 1 : 0;
    cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkFormat color = k.color;
    VkPipelineRenderingCreateInfo rci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rci.colorAttachmentCount = k.color ? 1 : 0;
    rci.pColorAttachmentFormats = &color;
    rci.depthAttachmentFormat = k.depth;
    rci.stencilAttachmentFormat = k.depth;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext = &rci;
    pi.stageCount = 2;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy;
    pi.layout = k.rt ? rt_layout_ : layout_;
    VkPipeline p = VK_NULL_HANDLE;
    if (!st[0].module || !st[1].module || !pi.layout) return pass_pipelines_[h] = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev_, cache_, 1, &pi, nullptr, &p) != VK_SUCCESS)
        LOG_FIRST(8, "vulkan: a pass pipeline could not be made");
    return pass_pipelines_[h] = p;
}

// the full screen triangle with cb at b1, views from t0, the pass samplers
void VulkanBackend::FullScreen(VkPipeline p, const Attachments& a, const VkViewport& vp, const VkRect2D& sc,
                               const float* cb, uint32_t cb_floats, const VkImageView* views, uint32_t n_views,
                               VkAccelerationStructureKHR tlas) {
    if (!p) return;
    Begin(a);
    VkCommandBuffer c = cmd_;
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    VkViewport v = vp;  // D3D's way up
    v.y = vp.y + vp.height;
    v.height = -vp.height;
    vkCmdSetViewport(c, 0, 1, &v);
    vkCmdSetScissor(c, 0, 1, &sc);
    VkWriteDescriptorSet w[1 + 4 + 4 + 5]{};
    VkDescriptorBufferInfo bi{};
    VkDescriptorImageInfo ii[4]{}, si[4]{};
    uint32_t n = 0;
    float zero[4] = {};
    Stretch s = Stream(std::max<uint32_t>(cb_floats, 4) * sizeof(float));
    std::memcpy(s.p, cb ? cb : zero, std::max<uint32_t>(cb_floats, 4) * sizeof(float));
    bi = {s.buf, s.off, std::max<uint32_t>(cb_floats, 4) * sizeof(float)};
    w[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindDc, 0, 1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi, nullptr};
    n++;
    for (uint32_t i = 0; i < 4; i++) {
        VkImageView v2 = i < n_views ? views[i] : VK_NULL_HANDLE;
        if (!v2 && !null_descriptor_) v2 = dummy_.view;
        ii[i] = {VK_NULL_HANDLE, v2, VK_IMAGE_LAYOUT_GENERAL};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindTex + i, 0, 1,
                  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ii[i], nullptr, nullptr};
        si[i] = {pass_samplers_[i], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindSmp + i, 0, 1,
                  VK_DESCRIPTOR_TYPE_SAMPLER, &si[i], nullptr, nullptr};
    }
    VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    VkDescriptorBufferInfo rv{}, rtr{}, rm{}, rs{};
    if (tlas) {
        wa.accelerationStructureCount = 1;
        wa.pAccelerationStructures = &tlas;
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &wa, VK_NULL_HANDLE, kBindTlas, 0, 1,
                  VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr, nullptr};
        // the structure's own vertices and triangles (this frame's, as tlas is)
        const RtFrame& f = rt_[frame_slot_];
        rv = {f.verts.buf, 0, VK_WHOLE_SIZE};
        rtr = {f.tris.buf, 0, VK_WHOLE_SIZE};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindRtVerts, 0, 1,
                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &rv, nullptr};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindRtTris, 0, 1,
                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &rtr, nullptr};
        rm = {f.mats.buf, 0, VK_WHOLE_SIZE};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindRtMats, 0, 1,
                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &rm, nullptr};
        rs = {f.stats.buf ? f.stats.buf : f.mats.buf, 0, VK_WHOLE_SIZE};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindRtStats, 0, 1,
                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &rs, nullptr};
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, rt_layout_, 1, 1, &f.tex_set, 0, nullptr);
    }
    vkCmdPushDescriptorSetKHR(c, VK_PIPELINE_BIND_POINT_GRAPHICS, tlas ? rt_layout_ : layout_, 0, n, w);
    vkCmdDraw(c, 3, 1, 0, 0);
    ForgetDrawState();
}

// src over the viewport and scissor given
void VulkanBackend::Blit(VkImageView src, const Attachments& a, const VkViewport& vp, const VkRect2D& sc,
                         const float uvrect[4], const BlitOptions& o, VkFormat fmt) {
    uint64_t vh, ph;
    const app::Settings& st = app::Get();
    const bool final = o.final && (st.fxaa || st.sharpen);
    if (!Compile(kBlitHlsl, "vs", "vs_5_0", vh)) return;
    VkShaderModule ps = Compile(kBlitHlsl, final ? "psfinal" : "psmain", "ps_5_0", ph);
    bool use_final = final && ps;
    if (!ps) ps = Compile(kBlitHlsl, "psmain", "ps_5_0", ph);
    if (!ps) return;
    float c[8] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3], o.gamma ? 1.0f : 0.0f, o.alpha ? 1.0f : 0.0f,
                  float(o.taps), 1.0f};
    if (use_final) c[5] = st.fxaa ? 1.0f : 0.0f, c[6] = float(std::min<uint32_t>(st.sharpen, 100)) / 100.0f;
    VkPipeline p = PassPipeline({vh, ph, fmt, VK_FORMAT_UNDEFINED, o.over ? 1u : 0u, 0});
    VkImageView views[2] = {src, lut_.view};
    FullScreen(p, a, vp, sc, c, 8, views, 2);
}

// src over all of dst
void VulkanBackend::BlitWhole(VkImageView src, const Image& dst, const BlitOptions& o) {
    Attachments a{dst.att, VK_NULL_HANDLE, dst.w, dst.h};
    VkViewport vp{0, 0, float(dst.w), float(dst.h), 0, 1};
    VkRect2D sc{{0, 0}, {dst.w, dst.h}};
    float uv[4] = {0, 0, 1, 1};
    Blit(src, a, vp, sc, uv, o);
}

// a depth buffer drawn as colour into another depth buffer's memory: copied as depth
bool VulkanBackend::DepthCopy(const State& s) {
    TextureDesc d;
    if (!s.rt.header || !s.textures[0] || !Describe(s.textures[0], d) || Format(d.format).depth || d.cube ||
        d.depth != 1)
        return false;
    uint32_t gw, gh;
    SurfaceSize(s.rt, gw, gh);
    auto di = targets_.find(SizedTargetKey(mem::VirtualFromPhysical(s.rt.data), true, gw, gh));
    auto si = targets_.find(SizedTargetKey(d.data, true, d.width, d.height));
    if (di == targets_.end() || si == targets_.end() || di == si) return false;
    Target& dst = di->second;
    Target& src = si->second;
    uint64_t vh, ph;
    if (!Compile(kDepthHlsl, "vs", "vs_5_0", vh) || !Compile(kDepthHlsl, "psmain", "ps_5_0", ph)) return false;
    VkPipeline p = PassPipeline({vh, ph, VK_FORMAT_UNDEFINED, depth_fmt_, 0, 1});
    VkViewport vp{0, 0, float(dst.w), float(dst.h), 0, 1};
    VkRect2D sc{{0, 0}, {dst.w, dst.h}};
    VkImageView view = src.res.view;
    End();  // the source is read: its last writes go before
    FullScreen(p, {VK_NULL_HANDLE, dst.res.att, dst.w, dst.h}, vp, sc, nullptr, 0, &view, 1);
    Unbind();
    dst.drawn = true;
    return true;
}

// ---- clears

void VulkanBackend::ClearRects(Target& t, const std::vector<VkRect2D>& rs, uint32_t flags, const float colour[4],
                               float z, uint32_t stencil) {
    // into the open rendering instance when it draws to t, else one of t alone
    bool in_open = open_ && (t.depth ? cur_.depth == t.res.att : cur_.color == t.res.att);
    if (!in_open) Begin(t.depth ? Attachments{VK_NULL_HANDLE, t.res.att, t.w, t.h} : Attachments{t.res.att, VK_NULL_HANDLE, t.w, t.h});
    VkClearAttachment ca{};
    if (t.depth) {
        ca.aspectMask = ((flags & kClearZ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                        ((flags & kClearStencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        ca.clearValue.depthStencil = {z, stencil & 0xFF};
    } else {
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        std::memcpy(ca.clearValue.color.float32, colour, sizeof(float) * 4);
    }
    if (!ca.aspectMask) return;
    std::vector<VkClearRect> cr;
    for (const VkRect2D& r : rs)
        if (r.extent.width && r.extent.height) cr.push_back({r, 0, 1});
    if (!cr.empty()) vkCmdClearAttachments(cmd_, 1, &ca, uint32_t(cr.size()), cr.data());
}

void VulkanBackend::Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
                          uint32_t stencil) {
    Target* rt = (flags & kClearTarget) ? EnsureTarget(s.rt, false) : nullptr;
    Target* ds = (flags & (kClearZ | kClearStencil)) ? DepthTarget(s) : nullptr;
    std::vector<VkRect2D> rs;
    float col[4];
    Color(color, col);
    for (Target* t : {rt, ds}) {
        if (!t) continue;
        rs.clear();
        auto add = [&](RECT r) {
            if (r.right > r.left && r.bottom > r.top)
                rs.push_back({{r.left, r.top}, {uint32_t(r.right - r.left), uint32_t(r.bottom - r.top)}});
        };
        if (count && rects) {
            for (uint32_t i = 0; i < count; i++) {
                int32_t r[4];
                std::memcpy(r, rects + size_t(i) * sizeof(r), sizeof(r));
                add(ScaledRect(*t, r[0], r[1], r[2], r[3]));
            }
        } else {
            const auto& v = s.viewport;
            add(ScaledRect(*t, int(v.x), int(v.y), int(v.x + v.w), int(v.y + v.h)));
        }
        // pixels the CPU wrote before this clear go in first, a later upload would paint over the clear
        if (!t->depth && LatchCpuWrite(*t)) UploadTarget(*t, s.rt);
        // the scene's depth cleared after its traced draws (for the gun): kept for the ray traced passes
        if (t == ds && (flags & kClearZ) && t == scene_ds_ && !post_done_ &&
            ((rt_used_ && RtWanted()) || (scene_zwrites_ && KeptDepthWanted())))
            RtKeepDepth(*t);
        ClearRects(*t, rs, flags, col, std::clamp(z, 0.0f, 1.0f), stencil);
        t->drawn = true;
    }
}

// ---- copies

void VulkanBackend::CopyByDraw(Target& src_t, Target& dst_t, uint32_t count, uint32_t src_rects, uint32_t dst_points) {
    float src_scale = float(src_t.w) / src_t.gw;
    float dst_scale = float(dst_t.w) / dst_t.gw;
    float dst_scale_y = float(dst_t.h) / dst_t.gh;
    bool listed = count && src_rects;
    uint32_t n = listed ? count : 1;
    End();
    for (uint32_t i = 0; i < n; i++) {
        float whole_w = float(std::min(src_t.gw, dst_t.gw));
        float whole_h = float(std::min(src_t.gh, dst_t.gh));
        DrawnRect r{.x0 = 0, .y0 = 0, .x1 = whole_w, .y1 = whole_h};
        if (listed) r = ReadDrawnRect(src_rects, dst_points, i);
        if (r.x1 <= r.x0 || r.y1 <= r.y0) continue;
        VkViewport vp{r.dx * dst_scale, r.dy * dst_scale_y, (r.x1 - r.x0) * dst_scale, (r.y1 - r.y0) * dst_scale_y, 0, 1};
        int32_t x0 = std::max(0, int32_t(vp.x)), y0 = std::max(0, int32_t(vp.y));
        int32_t x1 = std::min(int32_t(vp.x + vp.width), int32_t(dst_t.w));
        int32_t y1 = std::min(int32_t(vp.y + vp.height), int32_t(dst_t.h));
        if (x1 <= x0 || y1 <= y0) continue;
        VkRect2D sc{{x0, y0}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}};
        float uv[4] = {r.x0 / src_t.gw, r.y0 / src_t.gh, (r.x1 - r.x0) / src_t.gw, (r.y1 - r.y0) / src_t.gh};
        uint32_t taps = uint32_t(std::ceil(std::max(src_scale / dst_scale, 1.0f) - kTapsSlack));
        Blit(src_t.res.view, {dst_t.res.att, VK_NULL_HANDLE, dst_t.w, dst_t.h}, vp, sc, uv, {.alpha = true, .taps = taps});
    }
    dst_t.drawn = true;
    Unbind();
}

void VulkanBackend::CopyByRegion(Target& src_t, Target& dst_t, uint32_t count, uint32_t src_rects, uint32_t dst_points) {
    float dst_scale = float(dst_t.w) / dst_t.gw;
    float dst_scale_y = float(dst_t.h) / dst_t.gh;
    bool listed = count && src_rects;
    uint32_t n = listed ? count : 1;
    std::vector<VkImageCopy> regions;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t left = 0, top = 0, right = std::min(src_t.w, dst_t.w), bottom = std::min(src_t.h, dst_t.h);
        uint32_t dx = 0, dy = 0;
        if (listed) {
            GuestRect r = ReadGuestRect(src_rects, i);
            left = ScaleUnsigned(r.left, dst_scale);
            top = ScaleUnsigned(r.top, dst_scale_y);
            right = std::min(ScaleUnsigned(r.right, dst_scale), src_t.w);
            bottom = std::min(ScaleUnsigned(r.bottom, dst_scale_y), src_t.h);
            dx = left;
            dy = top;
            if (dst_points) {
                GuestPoint p = ReadGuestPoint(dst_points, i);
                dx = ScaleUnsigned(p.x, dst_scale);
                dy = ScaleUnsigned(p.y, dst_scale_y);
            }
        }
        if (right <= left || bottom <= top || dx >= dst_t.w || dy >= dst_t.h) continue;
        right = std::min(right, left + dst_t.w - dx);
        bottom = std::min(bottom, top + dst_t.h - dy);
        VkImageCopy c{};
        c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.srcOffset = {int32_t(left), int32_t(top), 0};
        c.dstOffset = {int32_t(dx), int32_t(dy), 0};
        c.extent = {right - left, bottom - top, 1};
        regions.push_back(c);
    }
    if (!regions.empty()) {
        End();
        if (dirty_) Barrier();
        vkCmdCopyImage(Cmd(), src_t.res.img, VK_IMAGE_LAYOUT_GENERAL, dst_t.res.img, VK_IMAGE_LAYOUT_GENERAL,
                       uint32_t(regions.size()), regions.data());
        dirty_ = true;
    }
    dst_t.drawn = true;
}

void VulkanBackend::Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects, uint32_t dst_points) {
    Target* src_t = FindTarget(src, false);
    if (!src_t || !src_t->drawn) {
        if (!CopySurfaceMemory(src, dst, count, src_rects, dst_points)) {
            LOG_FIRST(8, "vulkan: CopyRects %08X to %08X skipped", src.data, dst.data);
        } else if (Target* dst_t = FindTarget(dst, false)) {
            UploadTarget(*dst_t, dst);
        }
        return;
    }
    Target* dst_t = EnsureTarget(dst, false);
    src_t = FindTarget(src, false);  // the same address at another size: EnsureTarget let the source go
    if (!dst_t || !src_t || src_t == dst_t) return;
    bool same_scale = src_t->w * dst_t->gw == dst_t->w * src_t->gw && src_t->h * dst_t->gh == dst_t->h * src_t->gh;
    if (!same_scale) CopyByDraw(*src_t, *dst_t, count, src_rects, dst_points);
    else CopyByRegion(*src_t, *dst_t, count, src_rects, dst_points);
}

// ---- textures

VkImageView VulkanBackend::BindTargetTexture(const State& s, int stage, const TextureDesc& d, Target& t,
                                             DrawTextures& stages) {
    TexInfo& info = stages.info[stage];
    if (&t == draw_ds_) return VK_NULL_HANDLE;
    stages.reads.rt[stage] = true;
    if (!t.depth && LatchCpuWrite(t)) {
        if (++reloads_[t.data] <= 3)
            LOGR("vulkan: sampled target %08X %ux%u reloaded from guest memory (frame %llu)", t.data, t.gw, t.gh,
                 (unsigned long long)presents_);
        Surface su;
        su.header = s.textures[size_t(stage)];
        UploadTarget(t, su);
    }
    info.kind = kTex2D;
    Target* rt = FindTarget(s.rt, false);
    float k = float(t.w) / t.gw;
    if (!t.depth && k > kUpscaledMin && rt && rt != &t && (t.gw > rt->gw || t.gh > rt->gh)) {
        stages.reads.wide[stage] = k;
        info.flags |= kTexWide;
    }
    if (NoAlphaFormat(d.format)) info.flags |= kTexOpaque;
    if (!t.depth) {
        switch (d.format) {
            case 0x00: case 0x13: case 0x01: case 0x1B: case 0x19: case 0x1F:
                LOG_FIRST(4, "vulkan: target %08X sampled as 8 bit format %X, read as BGRA", t.data, d.format);
                break;
            default: break;
        }
    }
    // the console reads the pixels it is about to overwrite: the draw gets a copy
    if (&t == draw_rt_) {
        if (!t.fb && !MakeImage(t.fb, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            return VK_NULL_HANDLE;
        End();
        if (dirty_) Barrier();
        VkImageCopy c{};
        c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.extent = {t.w, t.h, 1};
        vkCmdCopyImage(Cmd(), t.res.img, VK_IMAGE_LAYOUT_GENERAL, t.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        dirty_ = true;
        return t.fb.view;
    }
    if (t.depth) DepthRead(d.format, info);
    // sampled while it may still be drawn to: the draws before go first
    if (open_ && (cur_.color == t.res.att || cur_.depth == t.res.att)) End();
    return t.res.view;
}

bool VulkanBackend::CreateTexture(Tex& tx, const TextureDesc& d, const FormatInfo& fi) {
    DropImage(tx.img);
    tx = Tex{};
    tx.d = d;
    tx.fmt = VkFormatOf(HostFormat(fi.host));
    uint32_t w = d.width, h = d.height;
    tx.levels = d.levels;
    if (fi.compressed && ((w & 3) || (h & 3))) {
        w = (w + 3) & ~3u;
        h = (h + 3) & ~3u;
        tx.levels = 1;
    }
    // a level count past the size's chain is cut to it
    uint32_t chain = 1;
    for (uint32_t m = std::max({w, h, d.depth > 1 ? d.depth : 1u}); m > 1; m >>= 1) chain++;
    tx.levels = std::clamp(tx.levels, 1u, chain);
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (d.depth > 1)
        return MakeImage(tx.img, VK_IMAGE_TYPE_3D, tx.fmt, w, h, d.depth, tx.levels, 1, usage);
    return MakeImage(tx.img, VK_IMAGE_TYPE_2D, tx.fmt, w, h, 1, tx.levels, d.cube ? 6 : 1, usage, d.cube);
}

// one level of one layer from bytes packed as Vulkan's tight layout of the image's format
void VulkanBackend::UploadLevel(const Image& im, uint32_t level, uint32_t layer, uint32_t w, uint32_t h, uint32_t depth,
                                const void* p, size_t bytes) {
    Stretch s = Stream(bytes, 16);
    if (!s.p) return;
    std::memcpy(s.p, p, bytes);
    End();
    if (dirty_) Barrier();
    VkBufferImageCopy r{};
    r.bufferOffset = s.off;
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
    r.imageExtent = {w, h, depth};
    vkCmdCopyBufferToImage(Cmd(), s.buf, im.img, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    dirty_ = true;
}

void VulkanBackend::UploadTexture(Tex& tx, const TextureDesc& d, const uint8_t* p, const uint32_t* colours) {
    uint32_t faces = d.cube ? 6 : 1;
    uint32_t block = BlockBytes(tx.fmt);
    bool volume = d.depth > 1;
    uint32_t w0 = tx.img.w, h0 = tx.img.h;
    std::vector<uint8_t> px, rows;
    for (uint32_t f = 0; f < faces; f++) {
        for (uint32_t l = 0; l < tx.levels; l++) {
            uint32_t pitch = 0;
            DecodeLevel(d, l, f, p, colours, px, pitch);
            uint32_t lw = std::max(1u, w0 >> l), lh = std::max(1u, h0 >> l);
            if (f == 0 && l == 0) tx.glass = GlassScore(tx.fmt, px, pitch, lw, lh, tx.solid);
            uint32_t slices = volume ? std::max(1u, d.depth >> l) : 1;
            uint32_t row_bytes = block ? std::max(1u, (lw + 3) / 4) * block : lw * 4;
            uint32_t n_rows = block ? std::max(1u, (lh + 3) / 4) : lh;
            size_t src_rows = pitch ? px.size() / pitch / slices : 0;
            rows.assign(size_t(row_bytes) * n_rows * slices, 0);
            for (uint32_t z = 0; z < slices; z++) {
                for (uint32_t y = 0; y < n_rows && y < src_rows; y++)
                    std::memcpy(&rows[(size_t(z) * n_rows + y) * row_bytes], &px[(size_t(z) * src_rows + y) * pitch],
                                std::min(pitch, row_bytes));
            }
            UploadLevel(tx.img, l, f, lw, lh, slices, rows.data(), rows.size());
        }
    }
}

VkImageView VulkanBackend::BindGuestTexture(const State& s, int stage, const TextureDesc& d, const FormatInfo& fi,
                                            DrawTextures& stages) {
    if (!d.cube && d.depth == 1 && !fi.depth && !sky_keeps_game_) {
        auto r = FindReplacement(d.data);
        if (r && ((r->sharpened && !app::Get().sharp_2d) || !ReplacementFits(*r, d))) r = nullptr;
        if (VkImageView v = r ? BindReplacement(*r, d, stages.levels[stage]) : VK_NULL_HANDLE) {
            stages.replaced[stage] = true;
            // the game's picture it stands for (uploaded before the pack's was set): what reflections know it by
            if (auto it = textures_.find(d.data); stage == 0 && it != textures_.end())
                stage_key_ = it->second.key, stage_glass_ = it->second.glass, stage_solid_ = it->second.solid,
                stage_detail_ = it->second.detail, stage_seen_ = true;
            return v;
        }
    }
    if (draw_2d_) Sharpen2D(d);
    StagePalette palette;
    ReadStagePalette(s, stage, d, palette);
    const uint64_t key = uint64_t(palette.va) << 32 | d.data;
    Tex& tx = textures_[key];
    bool same = SameTexture(tx, d);
    if (!same && !CreateTexture(tx, d, fi)) {
        DropImage(tx.img);
        textures_.erase(key);
        return VK_NULL_HANDLE;
    }
    if (tx.frame != presents_ + 1 || !same || tx.palette != palette.hash) {
        tx.frame = presents_ + 1;
        tx.palette = palette.hash;
        uint64_t sig = SurfaceSig(d.data, TotalSize(d)) ^ DirtyEpoch(d.data) ^ palette.hash;
        if (sig != tx.sig || !same) {
            tx.sig = sig;
            UploadTexture(tx, d, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), palette.va ? palette.colours : nullptr);
            tx.key = TextureKey(d, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), palette.hash);
            tx.detail = kDetailUnknown;  // its kind found when first drawn with ground detail on
            TexturePackSeen(d, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), palette.va ? palette.colours : nullptr,
                            palette.hash);
        }
    }
    if (tx.detail == kDetailUnknown && Style().ground_detail && !draw_2d_)
        tx.detail = TextureDetail(tx.key, d, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), palette.va != 0);
    if (d.cube && !fi.depth) CopyCubeTargets(tx, d);
    stages.levels[stage] = tx.levels;
    if (fi.depth) stages.info[stage].shadow = ShadowDepthKind(d.format);
    if (stage == 0)
        stage_key_ = tx.key, stage_glass_ = tx.glass, stage_solid_ = tx.solid, stage_detail_ = tx.detail, stage_seen_ = true;
    return tx.img.view;
}

VkImageView VulkanBackend::BindReplacement(const Replacement& r, const TextureDesc& d, uint32_t& levels) {
    constexpr uint32_t kFmtA8R8G8B8 = 0x06;
    const uint64_t key = uint64_t(0xFFFFFFFFu) << 32 | d.data;
    Tex& tx = textures_[key];
    if (tx.frame != presents_ + 1 && !ReplacementHolds(r)) {
        DropImage(tx.img);
        textures_.erase(key);
        return VK_NULL_HANDLE;
    }
    tx.frame = presents_ + 1;
    if (!tx.img || tx.sig != r.version) {
        std::vector<std::vector<uint32_t>> pictures = ReplacementLevels(r);
        if (!tx.img || tx.d.width != r.width || tx.d.height != r.height || tx.levels != pictures.size()) {
            TextureDesc rd;
            rd.data = d.data;
            rd.format = kFmtA8R8G8B8;
            rd.width = r.width;
            rd.height = r.height;
            rd.levels = uint32_t(pictures.size());
            if (!CreateTexture(tx, rd, Format(kFmtA8R8G8B8))) {
                DropImage(tx.img);
                textures_.erase(key);
                return VK_NULL_HANDLE;
            }
            tx.frame = presents_ + 1;
        }
        uint32_t w = r.width, h = r.height;
        for (uint32_t l = 0; l < pictures.size() && l < tx.levels; l++) {
            UploadLevel(tx.img, l, 0, w, h, 1, pictures[l].data(), size_t(w) * h * 4);
            w = std::max(w / 2, 1u);
            h = std::max(h / 2, 1u);
        }
        tx.sig = r.version;
    }
    levels = tx.levels;
    return tx.img.view;
}

void VulkanBackend::CopyCubeTargets(Tex& tx, const TextureDesc& d) {
    if (tx.fmt != kBgra || tx.faces == presents_ + 1) return;
    tx.faces = presents_ + 1;
    for (uint32_t f = 0; f < 6; f++) {
        uint32_t face = d.data + f * FaceSize(d);
        auto it = targets_.find(TargetMapKey(face, false));
        if (it == targets_.end() || TargetStale(face)) continue;
        Target& t = it->second;
        if (t.depth || t.gw != d.width || t.gh != d.height) continue;
        Image shrunk;
        Image* src = PrepareCopySource(t, shrunk);
        if (src) {
            End();
            if (dirty_) Barrier();
            VkImageCopy c{};
            c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, f, 1};
            c.extent = {std::min(src->w, tx.img.w), std::min(src->h, tx.img.h), 1};
            vkCmdCopyImage(Cmd(), src->img, VK_IMAGE_LAYOUT_GENERAL, tx.img.img, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
            dirty_ = true;
        }
        DropImage(shrunk);
    }
}

VkImageView VulkanBackend::BindTexture(const State& s, int stage, DrawTextures& stages) {
    stages.info[stage] = {};
    stages.reads.rt[stage] = false;
    stages.reads.k[stage] = 1;
    stages.reads.wide[stage] = 0;
    stages.levels[stage] = 1;
    stages.replaced[stage] = false;
    uint32_t header = s.textures[size_t(stage)];
    TextureDesc d;
    if (!header || !Describe(header, d)) return VK_NULL_HANDLE;
    const FormatInfo& fi = Format(d.format);
    stages.info[stage].kind = TextureKind(d);
    stages.info[stage].rect = d.linear;
    auto tit = targets_.find(SizedTargetKey(d.data, fi.depth, d.width, d.height));
    if (tit == targets_.end() && !fi.depth && DepthBytesOrder(d.format))
        tit = targets_.find(SizedTargetKey(d.data, true, d.width, d.height));
    if (tit == targets_.end() && fi.depth && d.linear) {
        auto big = targets_.find(TargetMapKey(d.data, true));
        if (big != targets_.end() && big->second.gw >= d.width && big->second.gh >= d.height) {
            tit = big;
            stages.reads.size[stage][0] = big->second.gw;
            stages.reads.size[stage][1] = big->second.gh;
        }
    }
    if (tit != targets_.end() && !fi.depth && !tit->second.depth && d.linear &&
        (tit->second.gw > d.width || tit->second.gh > d.height) && tit->second.gw >= d.width &&
        tit->second.gh >= d.height) {
        stages.reads.size[stage][0] = tit->second.gw;
        stages.reads.size[stage][1] = tit->second.gh;
        static std::unordered_set<uint32_t> told;
        if (told.size() < 4 && told.insert(d.data).second)
            LOGR("vulkan: texture %08X %ux%u (pitch %u) read as the top left of target %ux%u", d.data, d.width,
                 d.height, d.pitch, tit->second.gw, tit->second.gh);
    }
    if (tit != targets_.end() && !d.cube && d.depth == 1 && !fi.compressed)
        return BindTargetTexture(s, stage, d, tit->second, stages);
    return BindGuestTexture(s, stage, d, fi, stages);
}

// ---- the draw

void VulkanBackend::EnsureVertexShader(const State& s, const Assembled& a, DrawShaders& sh) {
    auto vit = shader_src_.find(sh.vkey);
    if (vit != shader_src_.end()) {
        sh.vh = vit->second;
        sh.vs_code = Module(sh.vh) ? reinterpret_cast<ID3DBlob*>(1) : nullptr;  // only tested for null
        return;
    }
    std::string src = VertexHlsl(s, a, sh);
    VkShaderModule m = Compile(sh.quad ? QuadHlsl(std::move(src)) : src, "main", "vs_5_0", sh.vh);
    sh.vs_code = m ? reinterpret_cast<ID3DBlob*>(1) : nullptr;
    shader_src_[sh.vkey] = sh.vh;
}

void VulkanBackend::EnsurePixelShader(const PixelKey& pk, DrawShaders& sh) {
    auto pit = shader_src_.find(sh.pkey ^ kPixelKeySalt);
    if (pit != shader_src_.end()) {
        sh.ph = pit->second;
        sh.ps_code = Module(sh.ph) ? reinterpret_cast<ID3DBlob*>(1) : nullptr;
        return;
    }
    VkShaderModule m = Compile(PixelHlsl(pk), "main", "ps_5_0", sh.ph);
    sh.ps_code = m ? reinterpret_cast<ID3DBlob*>(1) : nullptr;
    shader_src_[sh.pkey ^ kPixelKeySalt] = sh.ph;
}

VkPipeline VulkanBackend::EnsurePipeline(const PsoKey& k) {
    uint64_t h = Hash(&k, sizeof(k));
    auto it = pipelines_.find(h);
    if (it != pipelines_.end()) return it->second;
    bool points = k.topo == uint32_t(Topology::Points);
    if (points && !gs_ok_) return pipelines_[h] = VK_NULL_HANDLE;  // not drawn, see the log
    if (points && !point_gs_) Compile(PointHlsl(), "main", "gs_5_0", point_gs_);
    VkPipelineShaderStageCreateInfo st[3]{};
    uint32_t n_st = 0;
    st[n_st++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, Module(k.vs), "main"};
    st[n_st++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, Module(k.ps), "main"};
    if (points) {
        if (!Module(point_gs_)) return pipelines_[h] = VK_NULL_HANDLE;
        st[n_st++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_GEOMETRY_BIT, Module(point_gs_), "main"};
    }
    // vertex input: register r at location r; strides are set with the buffers
    VkVertexInputAttributeDescription attrs[16];
    VkVertexInputBindingDescription binds[16];
    uint32_t n_attr = 0, n_bind = 0;
    uint32_t bound = 0;
    for (uint32_t r = 0; r < 16; r++) {
        if (!((k.regs >> r) & 1)) continue;
        uint32_t e = k.elements[r];
        uint32_t buf = (e >> 8) & 0xFF;
        attrs[n_attr++] = {r, buf, VkFormatOf(DXGI_FORMAT(e & 0xFF)), e >> 16};
        if (attrs[n_attr - 1].format == VK_FORMAT_UNDEFINED)
            LOG_FIRST(8, "vulkan: vertex format %u has no Vulkan equivalent here", e & 0xFF);
        if (!(bound >> buf & 1)) {
            bound |= 1u << buf;
            binds[n_bind++] = {buf, 0, VK_VERTEX_INPUT_RATE_VERTEX};
        }
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = n_bind;
    vi.pVertexBindingDescriptions = binds;
    vi.vertexAttributeDescriptionCount = n_attr;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                  : k.topo == uint32_t(Topology::Lines) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    // a point is drawn as its whole console pixel by a geometry shader, solid, front facing and never culled
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = k.fill == kFillWireframe && !points && fill_lines_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = k.cull && !points ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    // D3D's facing is that of the picture, which the flipped viewport keeps
    rs.frontFace = k.ccw && !points ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = k.zbias || k.zslope;
    rs.depthBiasConstantFactor = float(k.zbias);
    rs.depthBiasSlopeFactor = k.zslope;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = k.zen;
    ds.depthWriteEnable = k.zen && k.zwrite;
    ds.depthCompareOp = VkCmp(Cmp(k.zfunc));
    ds.stencilTestEnable = k.sten;
    VkStencilOpState so{VkStencil(StencilOp(k.sfail)), VkStencil(StencilOp(k.spass)), VkStencil(StencilOp(k.szfail)),
                        VkCmp(Cmp(k.sfunc)), k.smask & 0xFF, k.swmask & 0xFF, 0};
    ds.front = ds.back = so;
    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable = k.blend;
    ba.srcColorBlendFactor = VkBlend(Blend(k.src, false));
    ba.dstColorBlendFactor = VkBlend(Blend(k.dst, false));
    ba.colorBlendOp = VkBlendOperation(BlendOp(k.op));
    ba.srcAlphaBlendFactor = VkBlend(Blend(k.src, true));
    ba.dstAlphaBlendFactor = VkBlend(Blend(k.dst, true));
    ba.alphaBlendOp = ba.colorBlendOp;
    ba.colorWriteMask = k.wmask & 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = k.has_rt ? 1 : 0;
    cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                            VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
                            VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = uint32_t(std::size(dyn)) - (n_bind ? 0 : 1);  // the strides: only with vertex buffers
    dy.pDynamicStates = dyn;
    VkFormat color = k.rtfmt == kMaskRtFmt ? kMaskFormat : kBgra;
    VkPipelineRenderingCreateInfo rci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rci.colorAttachmentCount = k.has_rt ? 1 : 0;
    rci.pColorAttachmentFormats = &color;
    rci.depthAttachmentFormat = k.has_ds ? depth_fmt_ : VK_FORMAT_UNDEFINED;
    rci.stencilAttachmentFormat = rci.depthAttachmentFormat;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.pNext = &rci;
    pi.stageCount = n_st;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy;
    pi.layout = layout_;
    VkPipeline p = VK_NULL_HANDLE;
    if (!st[0].module || !st[1].module) return pipelines_[h] = VK_NULL_HANDLE;
    VkResult r = vkCreateGraphicsPipelines(dev_, cache_, 1, &pi, nullptr, &p);
    if (r != VK_SUCCESS) LOG_FIRST(8, "vulkan: a pipeline could not be made: %s", VkResultName(r));
    return pipelines_[h] = p;
}

void VulkanBackend::PushDescriptors(const Stretch& vsc, const Stretch& dc, const DrawTextures& stages, const VkSampler smp[4]) {
    VkWriteDescriptorSet w[kBindCapture + 1]{};
    VkDescriptorBufferInfo bi[2] = {{vsc.buf, vsc.off, sizeof(State::vsc)}, {dc.buf, dc.off, sizeof(DrawConstants)}};
    VkDescriptorImageInfo ii[4]{}, si[4]{};
    uint32_t n = 0;
    for (uint32_t b = 0; b < 2; b++)
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, b, 0, 1,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi[b], nullptr};
    for (uint32_t i = 0; i < 4; i++) {
        VkImageView v = stages.view[i];
        if (!v && !null_descriptor_) v = dummy_.view;
        ii[i] = {VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindTex + i, 0, 1,
                  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ii[i], nullptr, nullptr};
        si[i] = {smp[i], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindSmp + i, 0, 1,
                  VK_DESCRIPTOR_TYPE_SAMPLER, &si[i], nullptr, nullptr};
    }
    VkDescriptorBufferInfo cap{rt_[frame_slot_].verts.buf, 0, VK_WHOLE_SIZE};
    if (rt_capturing_)
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindCapture, 0, 1,
                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &cap, nullptr};
    vkCmdPushDescriptorSetKHR(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, n, w);
}

void VulkanBackend::DrawGeometry(const Assembled& a) {
    VkBuffer bufs[16];
    VkDeviceSize offsets[16], strides[16];
    for (uint32_t i = 0; i < a.buffer_count; i++) {
        const VertexBuffer& b = a.buffers[i];
        Stretch s = Stream(std::max<uint32_t>(b.bytes, 4));
        if (!s.p) return;
        std::memcpy(s.p, b.src, b.bytes);
        bufs[i] = s.buf;
        offsets[i] = s.off;
        strides[i] = b.stride;
    }
    if (a.buffer_count) vkCmdBindVertexBuffers2(cmd_, 0, a.buffer_count, bufs, offsets, nullptr, strides);
    int topo = int(VkTopology(PrimitiveTopology(a)));
    if (ds_.topo != topo) vkCmdSetPrimitiveTopology(cmd_, VkPrimitiveTopology(ds_.topo = topo));
    if (a.index_count) {
        uint32_t ibytes = a.index_count * (a.index16 ? 2 : 4);
        Stretch s = Stream(ibytes);
        if (!s.p) return;
        std::memcpy(s.p, a.index_src, ibytes);
        vkCmdBindIndexBuffer(cmd_, s.buf, s.off, a.index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd_, a.index_count, 1, 0, a.base_vertex, 0);
    } else {
        vkCmdDraw(cmd_, a.draw_count, 1, 0, 0);
    }
}

void VulkanBackend::Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) {
    PerfScope ps{perf_draw_};
    if (const State* sprites = PointSpriteStages(s, prim)) return Draw(*sprites, prim, start, count, indices);
    if (s.blank_format && s.textures[0] && (::kernel::R32(s.textures[0] + 12) >> 8 & 0xFF) == s.blank_format) {
        Clear(s, 0, nullptr, kClearTarget, 0xFF000000u, 0, 0);  // a black clear in its place (State::blank_format)
        return;
    }
    if (DepthCopy(s)) return;
    if (diag::SkippedDraw(s.vs_handle)) return;
    DrawAdjust adjust;
    if (g_draw_filter) g_draw_filter(s, adjust);
    if (adjust.skip) return;
    Assembled& a = asm_;
    if (!Assemble(s, prim, start, count, indices, a)) return;
    if (!BindTargets(s)) return;

    // vertex shader
    DrawShaders sh = ClassifyShaders(s, a, count);
    if (sh.prog) EnsureProgramHash(s);
    sh.vkey = VertexKey(s, a, sh);
    EnsureVertexShader(s, a, sh);
    // stage textures, which may draw copies, then the targets again
    DrawTextures stages;
    draw_rt_ = bound_rt_;
    draw_ds_ = bound_ds_;
    draw_2d_ = a.pretransformed || ScreenDraw(s);
    stage_seen_ = false;
    // the sky keeps the game's own pictures: a pack's are upscaled each apart, so a sky box's faces no longer meet
    // (a thin line at each edge of The Exchange's moon box)
    draw_seq_++;
    sky_keeps_game_ = !draw_2d_ && Style().texture_pack && SkyDraw(s, a, sh).sky;
    for (int i = 0; i < 4; i++) stages.view[i] = BindTexture(s, i, stages);
    draw_env_ = false;
    for (int i = 0; i < 4; i++) draw_env_ |= stages.view[i] && stages.info[i].kind == kTexCube;
    if (!BindTargets(s)) return;
    // the pixel shader made for the textures
    PixelKey pk = MakePixelKey(s, stages.info);
    pk.quad = sh.quad;
    static const bool wbuffer = [] {
        const char* e = std::getenv("XBE_WBUFFER");
        return !(e && *e == '0');
    }();
    pk.wbuf = wbuffer && s.rs[kRsZEnable] == 2 && bound_ds_ ? 1 : 0;
    if (!draw_2d_ && bound_ds_) scene_wbuf_ = pk.wbuf;
    sh.pkey = Hash(&pk, sizeof(pk));
    EnsurePixelShader(pk, sh);
    if (!sh.vs_code || !sh.ps_code) return;

    // pipeline state
    PsoKey k;
    PipelineTargets pt{.rt = bound_rt_ != nullptr, .rtfmt = bound_rt_ ? uint32_t(DXGI_FORMAT_B8G8R8A8_UNORM) : 0u,
                       .ds = bound_ds_ != nullptr, .ds_scale = bound_ds_ ? float(bound_ds_->h) / bound_ds_->gh : 1.0f,
                       .samples = 1};
    BuildPipelineKey(s, a, sh.vh, sh.ph, pt, k);
    if (!adjust.depth_test) k.zen = k.zwrite = 0;
    k.zbias += adjust.depth_bias;
    float wdepth[4] = {float(k.zbias) * s.zscale / kDepth24Max, k.zslope, 1.0f / (s.zscale + 1.0f),
                       sh.prog ? 1.0f : s.zscale * s.inv_wfar};
    if (pk.wbuf) k.zbias = 0, k.zslope = 0;
    // ray tracing: an opaque draw of the 3D scene writes its vertices' places (its variants and mask draw do the same)
    uint32_t rt_push[4] = {};
    rt_capturing_ = RtWanted() && RtCapture(s, a, sh, k, stages.view[0], rt_push);
    VkPipeline p = EnsurePipeline(k);
    if (!p) {
        if (rt_capturing_) {
            rt_tris_.resize(rt_undo_.tris), rt_cut_.resize(rt_undo_.cut), rt_mats_.resize(rt_undo_.mats);
            rt_cut_mats_.resize(rt_undo_.cut_mats), rt_gun_.resize(rt_undo_.gun), rt_gun_mats_.resize(rt_undo_.gun_mats);
            rt_used_ = rt_undo_.used;
        }
        rt_capturing_ = false;
        return;
    }

    // constants
    GuestExtent size;
    SurfaceSize(bound_rt_ ? s.rt : s.zs, size.w, size.h);
    size = DrawnExtent(s, size);
    DrawConstants c{};
    std::memcpy(c.wdepth, wdepth, sizeof(wdepth));
    float stretch = bound_rt_ ? XStretch(aspect_, guest_h_, bound_rt_->gw, bound_rt_->gh) : 0;
    c.hud[0] = HudSqueeze(s, a, aspect_, stretch);
    c.hud[2] = DepthTested(s) ? 1.0f : 0.0f;
    c.hud[3] = pk.wbuf ? 1.0f : 0.0f;
    if (bound_rt_)
        NoteSqueeze(s, a, bound_rt_->data, bound_rt_->gw, bound_rt_->gh, bound_rt_->w, aspect_, stretch, c.hud[0]);
    c.hud[1] = (bound_rt_ ? bound_rt_->gw : guest_w_) * 0.5f / s.aa_x;
    if (c.hud[0] != 1 && !s.squeeze_beyond) {
        Span span = DrawSpan(s, a, c.hud[1] * 2);
        if (span == kSpanNone) c.hud[0] = -c.hud[0];
        if (span == kSpanViewport) c.hud[0] = 1;
    }
    SqueezeBeyond(s, c.hud);
    FillSurfaceConstants(s, size, c);
    if (k.topo == uint32_t(Topology::Points)) {
        const Target* t = bound_rt_ ? bound_rt_ : bound_ds_;
        c.surf[3] = t && t->gh ? float(t->h) * s.aa_y / float(t->gh) : 1.0f;
    }
    FillFogConstants(s, sh.prog, c);
    FillCombinerConstants(s, c);
    FillTextureConstants(s, stages.reads, c);
    if (sh.fixed_transform) {
        FillTransformConstants(s, size, c);
        FillLightConstants(s, sh.fk, c);
    }
    // graphics styles: the 3D scene goes to a screen sized target with depth; the first 2D draw over it after that
    // (the HUD, menus) has the scene pass drawn first, so the 2D keeps the game's look
    const GraphicsStyle& st = Style();
    bool styled = st.mode != kStyleOriginal;
    if ((ScenePassWanted() || swap_hdr_) && bound_rt_ && bound_rt_->gw == guest_w_ && bound_rt_->gh == guest_h_) {
        if (!draw_2d_ && bound_ds_) {
            scene_rt_ = bound_rt_;
            scene_ds_ = bound_ds_;
            scene_zwrites_ += k.zwrite ? 1 : 0;
        } else if (draw_2d_ && !post_done_ && scene_rt_ == bound_rt_) {
            ScenePass(*scene_rt_, scene_ds_);
        }
    }
    if (styled && !draw_2d_ && st.mode == kStyleRetro && st.vertex_snap) {
        c.style[0] = float(size.w) / (2.0f * float(st.pixel));
        c.style[1] = float(size.h) / (2.0f * float(st.pixel));
    }
    // reflections: drawn into the mask after (DrawMask), which reads its strength from these constants
    const float refl = Reflectivity(k);
    c.style[2] = refl;
    c.style[3] = Opaque(k) || stage_solid_ ? 1.0f : 0.0f;
    // materials: a pack picture's normal and roughness, read from a stage the draw leaves free
    int mat_slot = -1;
    // not on the sky: its box's faces bumped and their edges seamed (The Exchange's moon box)
    const bool sky = !draw_2d_ && (st.materials || st.ground_detail) && SkyDraw(s, a, sh).sky;
    if (st.materials && st.mode != kStyleRetro && !draw_2d_ && !sky && stage_seen_ && stage_key_ && stages.replaced[0] &&
        bound_rt_ && k.zen && (Opaque(k) || stage_solid_)) {
        int slot = -1;
        for (int i = 3; i >= 1 && slot < 0; i--)
            if (!stages.view[i] && pk.tex[i].kind == kTexNone) slot = i;
        VkImageView mv = slot >= 0 ? MaterialView(stage_key_) : VK_NULL_HANDLE;
        uint64_t mh = mv ? MaterialShader(sh, pk, slot) : 0;
        if (mh) {
            PsoKey mk = k;
            mk.ps = mh;
            if (VkPipeline mp = EnsurePipeline(mk)) {
                p = mp;
                stages.view[slot] = mv;
                mat_slot = slot;
                c.pbr[0] = 1.0f / float(bound_rt_->w);
                c.pbr[1] = 1.0f / float(bound_rt_->h);
                c.pbr[2] = st.surface_depth;
                c.pbr[3] = st.material_strength;
                perf_mat_++;
            }
        }
    }
    // ground detail: a tagged texture's kind, read from a stage the draw leaves free (not over a pack's material)
    if (mat_slot < 0 && st.ground_detail && !draw_2d_ && stage_seen_ && stage_detail_ && stage_detail_ < kDetailKinds && bound_rt_ && k.zen) {
        perf_det_[0]++;
        if (!(Opaque(k) || stage_solid_)) perf_det_[1]++;
    }
    if (mat_slot < 0 && st.ground_detail && st.mode != kStyleRetro && !draw_2d_ && !sky && stage_seen_ && stage_detail_ &&
        stage_detail_ < kDetailKinds && bound_rt_ && k.zen && (Opaque(k) || stage_solid_)) {
        int slot = -1;
        for (int i = 3; i >= 1 && slot < 0; i--)
            if (!stages.view[i] && pk.tex[i].kind == kTexNone) slot = i;
        if (slot < 0) perf_det_[2]++;
        VkImageView dv = slot >= 0 ? DetailView(stage_detail_) : VK_NULL_HANDLE;
        uint64_t dh = dv ? DetailShader(sh, pk, slot) : 0;
        if (dh) {
            PsoKey dk = k;
            dk.ps = dh;
            if (VkPipeline dp = EnsurePipeline(dk)) {
                p = dp;
                stages.view[slot] = dv;
                mat_slot = slot;
                c.pbr[0] = 1.0f / float(bound_rt_->w);
                c.pbr[1] = 1.0f / float(bound_rt_->h);
                c.pbr[2] = st.detail_distance;
                // XBE_DETAIL_SPLIT=1 (a test): the detail on the right half of the screen only (as a negative strength)
                static const bool split = std::getenv("XBE_DETAIL_SPLIT") != nullptr;
                c.pbr[3] = split ? -st.detail_strength : st.detail_strength;
                perf_mat_++, perf_det_[3]++;
            }
        }
    }
    // soft particles: the variant shader reads the scene depth's copy from a stage the draw leaves free
    if (int soft = mat_slot < 0 ? SoftParticle(k, pk, refl) : 0) {
        int slot = -1;
        for (int i = 3; i >= 0 && slot < 0; i--)
            if (!stages.view[i] && pk.tex[i].kind == kTexNone) slot = i;
        uint64_t vh = slot >= 0 ? SoftShader(sh, pk, slot) : 0;
        VkImageView depth = vh ? SoftDepth(*bound_ds_) : VK_NULL_HANDLE;
        if (depth) {
            PsoKey sk = k;
            sk.ps = vh;
            if (VkPipeline sp = EnsurePipeline(sk)) {
                p = sp;
                stages.view[slot] = depth;
                c.style[2] = float(soft);
                c.style[3] = Style().particle_fade;
                perf_soft_++;
            }
        }
    }
    // samplers; a style's filtering for the 3D scene's textures
    VkSampler smp[4];
    bool clamp = UnitTexcoords(a);
    uint32_t aniso = app::Get().aniso;
    for (int i = 0; i < 4; i++) {
        D3D12_SAMPLER_DESC sd = StageSampler(s, i, stages.levels[i], stages.info[i].shadow, clamp, aniso, stages.replaced[i]);
        if (styled && !draw_2d_ && !stages.info[i].shadow && !stages.reads.rt[i]) {
            if (st.tex_filter == kFilterNearest) {
                sd.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                sd.MaxAnisotropy = 0;
            } else if (st.tex_filter == kFilterAnisotropic && stages.levels[i] > 1) {
                sd.Filter = D3D12_FILTER_ANISOTROPIC;
                sd.MaxAnisotropy = 16;
                sd.MaxLOD = D3D12_FLOAT32_MAX;
            }
        }
        smp[i] = Sampler(sd);
    }
    if (mat_slot >= 0) smp[mat_slot] = smp[0];  // the material repeats and filters as the picture it goes with

    // the rendering instance, then everything recorded inside it
    Begin(TargetAttachments(bound_rt_, bound_ds_));
    VkCommandBuffer cb = cmd_;
    // each constant buffer streamed again only when its contents changed, or a new submission is being recorded (the
    // stream chunk holding the last copy may be reused once the GPU is past the old one)
    bool fresh = last_serial_ != serial_ + 1;
    last_serial_ = serial_ + 1;
    if (fresh || vsc_last_.size() != sizeof(s.vsc) || std::memcmp(vsc_last_.data(), s.vsc.data(), sizeof(s.vsc))) {
        last_vsc_ = Stream(sizeof(s.vsc));
        if (!last_vsc_.p) {
            vsc_last_.clear();  // streamed again by the next draw
            return;
        }
        std::memcpy(last_vsc_.p, s.vsc.data(), sizeof(s.vsc));
        const uint8_t* b = reinterpret_cast<const uint8_t*>(s.vsc.data());
        vsc_last_.assign(b, b + sizeof(s.vsc));
    }
    if (fresh || dc_last_.size() != sizeof(c) || std::memcmp(dc_last_.data(), &c, sizeof(c))) {
        last_dc_ = Stream(sizeof(c));
        if (!last_dc_.p) {
            dc_last_.clear();
            return;
        }
        std::memcpy(last_dc_.p, &c, sizeof(c));
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&c);
        dc_last_.assign(b, b + sizeof(c));
    }
    if (ds_.pipeline != p) vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ds_.pipeline = p);
    PushDescriptors(last_vsc_, last_dc_, stages, smp);
    if (rt_capturing_) vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, rt_push);
    float bf[4];
    BlendFactor(s, bf);
    if (std::memcmp(ds_.bf, bf, sizeof(bf))) {
        vkCmdSetBlendConstants(cb, bf);
        std::memcpy(ds_.bf, bf, sizeof(bf));
    }
    uint32_t sref = SimpleState(s, kMStencilRef) & 0xFF;
    if (ds_.sref != sref) vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, ds_.sref = sref);

    // the viewport covers the target, the guest viewport is in the shader; the scissor clips to it
    const Target* b = bound_rt_ ? bound_rt_ : bound_ds_;
    float W = float(b->w), H = float(b->h);
    VkViewport vp{0, H, W, -H, 0, 1};
    float sx = W / size.w, sy = b->gh ? H * s.aa_y / b->gh : sx;
    LONG x0 = LONG(SqueezedX(c.hud, s.viewport.x) * sx);
    LONG y0 = LONG(s.viewport.y * sy);
    LONG x1 = LONG(SqueezedX(c.hud, s.viewport.x + s.viewport.w) * sx);
    LONG y1 = LONG((s.viewport.y + s.viewport.h) * sy);
    if (s.scissor_count) {
        x0 = std::max(x0, LONG(SqueezedX(c.hud, float(s.scissors[0])) * sx));
        y0 = std::max(y0, LONG(s.scissors[1] * sy));
        x1 = std::min(x1, LONG(SqueezedX(c.hud, float(s.scissors[2])) * sx));
        y1 = std::min(y1, LONG(s.scissors[3] * sy));
    }
    if (s.squeeze_margin) {
        const LONG edge = LONG(SqueezedX(c.hud, s.squeeze_margin < 0 ? 0.0f : c.hud[1] * 2) * sx);
        x0 = s.squeeze_margin < 0 ? 0 : edge;
        x1 = s.squeeze_margin < 0 ? edge : LONG(W);
    }
    x0 = std::clamp<LONG>(x0, 0, LONG(W));
    y0 = std::clamp<LONG>(y0, 0, LONG(H));
    x1 = std::clamp<LONG>(x1, x0, LONG(W));
    y1 = std::clamp<LONG>(y1, y0, LONG(H));
    VkRect2D sc{{int32_t(x0), int32_t(y0)}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}};
    if (!ds_.vp_ok || std::memcmp(&ds_.vp, &vp, sizeof(vp))) vkCmdSetViewport(cb, 0, 1, &vp);
    if (!ds_.vp_ok || std::memcmp(&ds_.sc, &sc, sizeof(sc))) vkCmdSetScissor(cb, 0, 1, &sc);
    ds_.vp = vp;
    ds_.sc = sc;
    ds_.vp_ok = true;

    // geometry, counted while the title runs a visibility test
    uint32_t q = 0;
    bool counting = s.simple[kMCountEnable >> 2] && queries_ && NewQuery(q);
    if (counting) vkCmdBeginQuery(cb, queries_, q, 0);
    DrawGeometry(a);
    perf_draws_++;
    if (draw_2d_) perf_2d_++;
    else if (sh.prog) perf_prog_++;
    else if (sh.fixed_transform) perf_fixed_++, perf_lit_ += (sh.fk.light & 1);
    if (counting) {
        vkCmdEndQuery(cb, queries_, q);
        counted_.push_back({q, serial_ + 1, SamplesPerPixel()});
    }
    if (refl > 0) DrawMask(a, k, sh, pk, stages, smp, bf, sref, vp, sc), perf_reflect_++;
    if (!draw_2d_) {
        if (solid_geom_frame_ != presents_ + 1) solid_geom_frame_ = presents_ + 1, solid_geom_.clear();
        const void* geom = a.index_src ? a.index_src : a.buffers[0].src;
        if (!k.blend && k.zwrite) {
            if (geom) solid_geom_.insert(geom);
        } else if (int em = Emitter(k); em && !(geom && solid_geom_.count(geom))) {
            DrawGlow(a, k, sh, pk, stages, smp, bf, sref, vp, sc, em);
        }
    }
    rt_capturing_ = false;
}

// ---- ray tracing

// a draw whose triangles go into this frame's acceleration structure: opaque, depth written, of the 3D scene, before
// its scene pass; its vertex shader swapped for the capture variant (k.vs), its triangles listed. push: the capture
// shader's base, count and least index, then (for the caller to undo) the indices listed
// a captured draw's material word: its texture's slot in the frame's (bits 0-11, 0xFFF none), the alpha test's
// reference (12-19) and comparison (20-22, Cmp() less 1), 23 a cut-out (alpha tested). The textures past
// kRtTextures in a frame get none: such a cut-out is not traced
uint32_t VulkanBackend::RtMaterial(const State& s, VkImageView tex, bool cut) {
    uint32_t slot = 0xFFF;
    if (tex) {
        auto it = rt_view_slot_.find(tex);
        if (it != rt_view_slot_.end()) {
            slot = it->second;
        } else if (rt_views_.size() < kRtTextures) {
            slot = uint32_t(rt_views_.size());
            rt_views_.push_back(tex);
            rt_view_slot_[tex] = slot;
        }
    }
    uint32_t ref = SimpleState(s, kMAlphaRef) & 0xFF, func = (uint32_t(Cmp(SimpleState(s, kMAlphaFunc))) - 1) & 7;
    return slot | ref << 12 | func << 20 | (cut ? 1u << 23 : 0u);
}

// a W-buffered scene's sky (NightFire's): a fixed function draw (the scene's own are vertex programs), its camera at
// the world's middle (drawn about the eye), fog off where the level is fogged (an outdoor level's dome and clouds,
// its far mountains' ring), or hundreds of units before and behind the eye. Its camera: c0-c3 a world place to the
// clip's, then the console's viewport (c-38, c-37, c21) to the screen, which the capture makes the ray traced view's
// (kCaptureMain: the screen's place times w, w). Worked out once a draw (the ray tracing capture and the materials ask)
VulkanBackend::SkyCheck VulkanBackend::SkyDraw(const State& s, const Assembled& a, const DrawShaders& sh) {
    if (sky_draw_ == draw_seq_) return sky_;
    sky_draw_ = draw_seq_;
    sky_ = {};
    if (!scene_wbuf_ || draw_2d_ || !bound_rt_) return sky_;
    if (!sh.prog) return sky_.sky = true, sky_;
    uint32_t n = a.index_count ? a.index_count : a.draw_count;
    const auto& v = s.vsc;
    const float sw = float(bound_rt_->gw), sh2 = float(bound_rt_->gh), unit = 4096.0f / (s.zscale + 1.0f);
    const float ox = v[59][0] + v[117][0], oy = v[59][1] + v[117][1];
    for (int i = 0; i < 4; i++) {
        sky_.cam[0][i] = unit * (2 * (v[58][0] * v[96][i] + ox * v[99][i]) / sw - v[99][i]);
        sky_.cam[1][i] = unit * (v[99][i] - 2 * (v[58][1] * v[97][i] + oy * v[99][i]) / sh2);
        sky_.cam[2][i] = unit * v[99][i];
    }
    sky_.cam_ok = true;
    if (std::abs(sky_.cam[0][3]) + std::abs(sky_.cam[1][3]) + std::abs(sky_.cam[2][3]) < 1e-3f * unit)
        return sky_.eye = sky_.sky = true, sky_;
    uint32_t fogged = s.rs[kRsFogEnable] ? 1 : 0;
    rt_fog_tris_[fogged] += n / 3;
    if (!fogged && rt_fogged_ > 0.7f) return sky_.sky = true, sky_;
    const VertexElement& el = a.elements[0];
    const VertexBuffer& vb = a.buffers[el.buffer];
    uint32_t regs = 0;
    for (uint32_t m = a.regs; m; m &= m - 1) regs++;
    const bool raw = a.vertices.empty() && el.format == DXGI_FORMAT_R32G32B32_FLOAT && vb.src;
    if (raw || !a.vertices.empty()) {
        float zmin = 1e30f, zmax = -1e30f;
        const uint32_t step = std::max<uint32_t>(1, n / 96);
        for (uint32_t i = 0; i < n; i += step) {
            int64_t vi = a.index_count ? int64_t(a.index16 ? static_cast<const uint16_t*>(a.index_src)[i]
                                                           : static_cast<const uint32_t*>(a.index_src)[i]) +
                                             a.base_vertex
                                       : int64_t(i);
            const float* q = nullptr;
            if (!raw && vi >= 0 && uint64_t(vi) < a.vertex_count) q = &a.vertices[size_t(vi) * regs * 4];
            else if (raw && vi >= 0 && uint64_t(vi) * vb.stride + el.offset + 12 <= vb.bytes)
                q = reinterpret_cast<const float*>(vb.src + size_t(vi) * vb.stride + el.offset);
            if (!q) continue;
            float z = sky_.cam[2][0] * q[0] + sky_.cam[2][1] * q[1] + sky_.cam[2][2] * q[2] + sky_.cam[2][3];
            zmin = std::min(zmin, z), zmax = std::max(zmax, z);
        }
        if (zmin < -300.0f && zmax > 300.0f) sky_.sky = true;
    }
    return sky_;
}

bool VulkanBackend::RtCapture(const State& s, const Assembled& a, const DrawShaders& sh, PsoKey& k, VkImageView tex,
                              uint32_t push[4]) {
    if (draw_2d_ || post_done_ || !bound_rt_ || !bound_ds_ || bound_rt_->gw != guest_w_ || bound_rt_->gh != guest_h_ ||
        bound_ds_->w != bound_rt_->w || bound_ds_->h != bound_rt_->h)
        return rt_skips_[0]++, false;
    // after the scene's depth was cleared mid-frame (RtKeepDepth): the gun and hands, an instance of their own
    const bool gun = bound_ds_->rt_depth_frame == presents_ + 1;
    if (!k.zen || !k.zwrite) return rt_skips_[1]++, false;
    if (a.topology != Topology::Triangles || sh.quad || a.pretransformed) return rt_skips_[2]++, false;
    // cut-outs (fences, leaves): alpha tested where a ray meets them, by their texture's alpha and the diffuse's
    bool cut = SimpleState(s, kMAlphaTestEnable) && !stage_solid_;
    if (!cut && !(Opaque(k) || stage_solid_)) return rt_skips_[3]++, false;  // blended: glass, smoke
    uint32_t mat = RtMaterial(s, tex, cut);
    if (cut && ((mat & 0xFFF) == 0xFFF || gun)) return rt_skips_[4]++, false;
    // the vertices it reads: SV_VertexID is an index plus the base vertex
    uint32_t n = a.index_count ? a.index_count : a.draw_count;
    if (n < 3) return false;
    // relit: the sky left out, so the sun's rays reach past it; a camera at the world's middle never (SkyDraw)
    if (!gun && scene_wbuf_) {
        const SkyCheck sky = SkyDraw(s, a, sh);
        if (sky.eye || (sky.sky && Style().rt_lighting)) return rt_skips_[2]++, false;
    }
    if (!gun && !sh.prog && Style().rt_lighting) return rt_skips_[2]++, false;
    if (!gun && sh.prog && scene_wbuf_) {
        const SkyCheck sky = SkyDraw(s, a, sh);
        RtCam cam;
        std::memcpy(cam.r, sky.cam, sizeof(cam.r));
        RtCam& seen = rt_cams_[Hash(cam.r, sizeof(cam.r))];
        if (!seen.tris) std::memcpy(seen.r, cam.r, sizeof(cam.r));
        seen.tris += n / 3;
        Color(s.rs[kRsFogColor], rt_fog_);
    }
    int64_t vmin = 0, vmax = int64_t(a.draw_count) - 1;
    auto index = [&](uint32_t i) -> int64_t {
        return int64_t(a.index16 ? static_cast<const uint16_t*>(a.index_src)[i] : static_cast<const uint32_t*>(a.index_src)[i]) +
               a.base_vertex;
    };
    if (a.index_count) {
        vmin = INT64_MAX, vmax = INT64_MIN;
        for (uint32_t i = 0; i < n; i++) {
            int64_t v = index(i);
            vmin = std::min(vmin, v), vmax = std::max(vmax, v);
        }
    }
    if (vmin < 0 || vmax < vmin) return false;
    uint64_t count = uint64_t(vmax - vmin + 1);
    RtFrame& f = rt_[frame_slot_];
    if (!rt_used_ && f.vert_cap < rt_want_) {
        // the frame's first: the slot's buffer made (again, larger after a frame ran out)
        DropBuffer(f.verts);
        f.vert_cap = 0;
        if (!MakeBuffer(f.verts, VkDeviceSize(rt_want_) * kRtVertBytes,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                        false)) {
            LOG_FIRST(1, "vulkan: no memory for ray tracing's %u vertices; the scene is not ray traced", rt_want_);
            return false;
        }
        f.vert_cap = rt_want_;
    }
    if (rt_used_ + count > f.vert_cap) {
        rt_full_ = true;
        return false;
    }
    // the capture variant of its vertex shader
    uint64_t key = sh.vkey ^ kCaptureKeySalt;
    uint64_t vh = 0;
    if (auto it = shader_src_.find(key); it != shader_src_.end()) {
        vh = it->second;
    } else {
        std::string src = VertexHlsl(s, a, sh);
        size_t at = src.find("VOut main(");
        size_t close = at == std::string::npos ? at : src.find(')', at);
        if (close != std::string::npos) {
            std::string params = src.substr(at + 10, close - at - 10);
            std::string arg = params.substr(params.find_last_of(' ') + 1);
            src.replace(at, 10, "VOut game_main(");
            char tail[2048];
            std::snprintf(tail, sizeof(tail), kCaptureMain, params.c_str(), arg.c_str());
            src += tail;
            if (!Compile(src, "main", "vs_5_0", vh)) vh = 0;
        }
        if (!vh) LOG_FIRST(4, "vulkan: a vertex shader's ray tracing variant could not be made; its draws are not traced");
        shader_src_[key] = vh;
    }
    if (!vh) return false;
    // its triangles, as indices into the frame's vertices
    rt_undo_ = {rt_tris_.size(), rt_cut_.size(), rt_mats_.size(), rt_cut_mats_.size(), rt_gun_.size(),
                rt_gun_mats_.size(), rt_used_};
    std::vector<uint32_t>& tris = gun ? rt_gun_ : cut ? rt_cut_ : rt_tris_;
    size_t before = tris.size();
    uint32_t base = rt_used_;
    auto at = [&](uint32_t i) { return base + uint32_t((a.index_count ? index(i) : int64_t(i)) - vmin); };
    if (a.strip) {
        for (uint32_t i = 0; i + 2 < n; i++) {
            uint32_t i0 = at(i), i1 = at(i + 1), i2 = at(i + 2);
            if (i0 == i1 || i1 == i2 || i0 == i2) continue;
            if (i & 1) std::swap(i0, i1);
            tris.insert(tris.end(), {i0, i1, i2});
        }
    } else {
        for (uint32_t i = 0; i + 2 < n; i += 3) tris.insert(tris.end(), {at(i), at(i + 1), at(i + 2)});
    }
    (gun ? rt_gun_mats_ : cut ? rt_cut_mats_ : rt_mats_).resize(tris.size() / 3, mat);
    k.vs = vh;
    rt_used_ += uint32_t(count);
    push[0] = base;
    push[1] = uint32_t(count);
    push[2] = uint32_t(vmin);
    push[3] = uint32_t(tris.size() - before);
    perf_rt_draws_++;
    return true;
}

// an acceleration structure's storage, made (again, larger) when it needs more
bool VulkanBackend::RtStorage(Buffer& mem, VkAccelerationStructureKHR& as, VkDeviceSize need,
                              VkAccelerationStructureTypeKHR type) {
    if (as && mem.size >= need) return true;
    if (as) {
        VkAccelerationStructureKHR old = as;
        Retire([this, old] { vkDestroyAccelerationStructureKHR(dev_, old, nullptr); });
        as = VK_NULL_HANDLE;
    }
    DropBuffer(mem);
    VkDeviceSize size = (need + need / 2 + 0xFFFF) & ~VkDeviceSize(0xFFFF);
    if (!MakeBuffer(mem, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    false))
        return false;
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = mem.buf;
    ci.size = size;
    ci.type = type;
    return vkCreateAccelerationStructureKHR(dev_, &ci, nullptr, &as) == VK_SUCCESS;
}

// this frame's triangles built into the slot's bottom level structure, one instance of it (x and y scaled by the
// assumed view's width) the top level; recorded after a barrier past the draws that wrote the places
bool VulkanBackend::RtBuild(float aspect) {
    RtFrame& f = rt_[frame_slot_];
    uint32_t opaque = uint32_t(rt_tris_.size() / 3), cut = uint32_t(rt_cut_.size() / 3),
             gun = uint32_t(rt_gun_.size() / 3), tris = opaque + cut + gun;
    if (!tris || !f.verts.buf || !rt_used_) return false;
    VkDeviceSize ibytes = VkDeviceSize(tris) * 12, mbytes = VkDeviceSize(tris + 2) * 4;
    const VkBufferUsageFlags input = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (f.tris.size < ibytes) {
        DropBuffer(f.tris);
        // read by the traced passes too (a hit's triangle)
        if (!MakeBuffer(f.tris, std::max<VkDeviceSize>(ibytes + ibytes / 2, 1u << 20),
                        input | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true))
            return false;
    }
    if (f.mats.size < mbytes) {
        DropBuffer(f.mats);
        if (!MakeBuffer(f.mats, std::max<VkDeviceSize>(mbytes + mbytes / 2, 1u << 18), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        true))
            return false;
    }
    if (!f.inst.buf && !MakeBuffer(f.inst, 2 * sizeof(VkAccelerationStructureInstanceKHR), input, true)) return false;
    if (!f.tris.mem.map || !f.mats.mem.map || !f.inst.mem.map) return false;
    // the opaque triangles, then the cut-outs, then the gun's; their materials after two counts (rt_tri)
    std::memcpy(f.tris.mem.map, rt_tris_.data(), rt_tris_.size() * 4);
    std::memcpy(f.tris.mem.map + rt_tris_.size() * 4, rt_cut_.data(), rt_cut_.size() * 4);
    std::memcpy(f.tris.mem.map + (rt_tris_.size() + rt_cut_.size()) * 4, rt_gun_.data(), rt_gun_.size() * 4);
    uint32_t* mw = reinterpret_cast<uint32_t*>(f.mats.mem.map);
    mw[0] = opaque, mw[1] = opaque + cut;
    std::memcpy(mw + 2, rt_mats_.data(), rt_mats_.size() * 4);
    std::memcpy(mw + 2 + opaque, rt_cut_mats_.data(), rt_cut_mats_.size() * 4);
    std::memcpy(mw + 2 + opaque + cut, rt_gun_mats_.data(), rt_gun_mats_.size() * 4);
    // the textures the materials name; those another frame wrote past them back to the dummy (they may be gone)
    {
        uint32_t used = uint32_t(rt_views_.size()), upto = std::max(used, f.tex_written);
        std::vector<VkDescriptorImageInfo> ii(upto);
        for (uint32_t i = 0; i < upto; i++) ii[i] = {VK_NULL_HANDLE, i < used ? rt_views_[i] : dummy_.view, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo si{rt_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        VkWriteDescriptorSet w[2]{};
        uint32_t n = 0;
        if (upto) w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.tex_set, 0, 0, upto,
                            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ii.data(), nullptr, nullptr};
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f.tex_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &si,
                  nullptr, nullptr};
        vkUpdateDescriptorSets(dev_, n, w, 0, nullptr);
        f.tex_written = used;
    }

    // two bottom levels: the world (its opaque triangles one geometry, the cut-outs, alpha tested by the passes'
    // queries, another) and the gun and hands (drawn after the mid-frame depth clear: an instance of its own, mask 2,
    // which rays from the world leave out, the gun reaching into walls)
    struct Level {
        VkAccelerationStructureGeometryKHR g[2];
        uint32_t counts[2], ng;
        VkAccelerationStructureBuildGeometryInfoKHR info;
        VkAccelerationStructureBuildSizesInfoKHR sizes;
    } lv[2]{};
    auto geometry = [&](Level& l, uint32_t first, uint32_t count, bool opaque_geo) {
        if (!count) return;
        VkAccelerationStructureGeometryKHR& gg = l.g[l.ng];
        gg.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        gg.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        gg.flags = opaque_geo ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
        VkAccelerationStructureGeometryTrianglesDataKHR& tr = gg.geometry.triangles;
        tr.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tr.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tr.vertexData.deviceAddress = Address(f.verts);
        tr.vertexStride = kRtVertBytes;
        tr.maxVertex = rt_used_ - 1;
        tr.indexType = VK_INDEX_TYPE_UINT32;
        tr.indexData.deviceAddress = Address(f.tris) + VkDeviceSize(first) * 12;
        l.counts[l.ng++] = count;
    };
    geometry(lv[0], 0, opaque, true);
    geometry(lv[0], opaque, cut, false);
    geometry(lv[1], opaque + cut, gun, true);
    for (Level& l : lv) {
        l.info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        l.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        l.info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        l.info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        l.info.geometryCount = l.ng;
        l.info.pGeometries = l.g;
        l.sizes = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        if (l.ng)
            vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &l.info,
                                                    l.counts, &l.sizes);
    }
    uint32_t instances = (lv[0].ng ? 1 : 0) + (lv[1].ng ? 1 : 0);
    VkAccelerationStructureGeometryKHR tg{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tg.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tg.flags = 0;  // the cut-outs geometry is not opaque
    tg.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tg.geometry.instances.data.deviceAddress = Address(f.inst);
    VkAccelerationStructureBuildGeometryInfoKHR tb = lv[0].info;
    tb.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tb.geometryCount = 1;
    tb.pGeometries = &tg;
    VkAccelerationStructureBuildSizesInfoKHR ts{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tb, &instances, &ts);

    // storage and scratch
    if ((lv[0].ng && !RtStorage(f.blas_mem, f.blas, lv[0].sizes.accelerationStructureSize,
                                VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)) ||
        (lv[1].ng && !RtStorage(f.gun_blas_mem, f.gun_blas, lv[1].sizes.accelerationStructureSize,
                                VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)) ||
        !RtStorage(f.tlas_mem, f.tlas, ts.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)) {
        LOG_FIRST(1, "vulkan: no memory for ray tracing's acceleration structures; the scene is not ray traced");
        return false;
    }
    VkDeviceSize scratch = std::max({lv[0].sizes.buildScratchSize, lv[1].sizes.buildScratchSize, ts.buildScratchSize}) +
                           rt_scratch_align_;
    if (f.scratch.size < scratch) {
        DropBuffer(f.scratch);
        if (!MakeBuffer(f.scratch, scratch + scratch / 2,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false))
            return false;
    }
    VkDeviceAddress sa = (Address(f.scratch) + rt_scratch_align_ - 1) & ~(rt_scratch_align_ - 1);

    // the instances: the world custom index 0, mask 1; the gun 1, mask 2
    VkAccelerationStructureInstanceKHR in[2]{};
    uint32_t ni = 0;
    for (uint32_t k = 0; k < 2; k++) {
        if (!lv[k].ng) continue;
        VkAccelerationStructureInstanceKHR& i = in[ni++];
        const float ty = 0.5774f;  // an assumed 60 degree vertical view, as the screen-space passes take it
        i.transform.matrix[0][0] = ty * aspect;
        i.transform.matrix[1][1] = ty;
        i.transform.matrix[2][2] = 1;
        i.instanceCustomIndex = k;
        i.mask = k ? 2 : 1;
        i.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        VkAccelerationStructureDeviceAddressInfoKHR ai{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        ai.accelerationStructure = k ? f.gun_blas : f.blas;
        i.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(dev_, &ai);
    }
    std::memcpy(f.inst.mem.map, in, sizeof(VkAccelerationStructureInstanceKHR) * ni);

    // the builds: the bottom levels once the draws wrote the places (one after the other: one scratch), the top level
    // once they are done
    End();
    Barrier();
    for (uint32_t k = 0; k < 2; k++) {
        if (!lv[k].ng) continue;
        lv[k].info.dstAccelerationStructure = k ? f.gun_blas : f.blas;
        lv[k].info.scratchData.deviceAddress = sa;
        VkAccelerationStructureBuildRangeInfoKHR br[2] = {{lv[k].counts[0], 0, 0, 0}, {lv[k].counts[1], 0, 0, 0}};
        const VkAccelerationStructureBuildRangeInfoKHR* pbr = br;
        vkCmdBuildAccelerationStructuresKHR(Cmd(), 1, &lv[k].info, &pbr);
        Barrier();
    }
    tb.dstAccelerationStructure = f.tlas;
    tb.scratchData.deviceAddress = sa;
    VkAccelerationStructureBuildRangeInfoKHR tr1{instances, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ptr = &tr1;
    vkCmdBuildAccelerationStructuresKHR(Cmd(), 1, &tb, &ptr);
    Barrier();
    perf_rt_tris_ += tris;
    return true;
}

// ray traced ambient occlusion over the scene (kRtHlsl): traced at half its size, then the scene darkened by it
void VulkanBackend::RtPass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    if (!ds || ds->w != rt.w || ds->h != rt.h) return;
    uint64_t vh, th, ch;
    static const std::string src = std::string(kRtCommonHlsl) + kRtHlsl;
    if (!Compile(src, "vs", "vs_6_5", vh) || !Compile(src, "trace", "ps_6_5", th) || !Compile(src, "comp", "ps_6_5", ch)) {
        LOG_FIRST(1, "vulkan: the ray traced passes did not compile; no ray tracing");
        return;
    }
    constexpr VkFormat kRtFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    PassKey tk{vh, th, kRtFormat, VK_FORMAT_UNDEFINED, 0, 0};
    tk.rt = 1;
    VkPipeline pt = PassPipeline(tk);
    PassKey ck{vh, ch, kBgra, VK_FORMAT_UNDEFINED, 0, 0};
    ck.rt = 1;
    VkPipeline pc = PassPipeline(ck);
    if (!pt || !pc) return;
    uint32_t hw = std::max<uint32_t>(rt.w / 2, 1), hh = std::max<uint32_t>(rt.h / 2, 1);
    if (rt.rt_ao.img && (rt.rt_ao.w != hw || rt.rt_ao.h != hh)) DropImage(rt.rt_ao);
    if (!rt.rt_ao.img && !MakeImage(rt.rt_ao, VK_IMAGE_TYPE_2D, kRtFormat, hw, hh, 1, 1, 1,
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return;
    if (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    float aspect = float(rt.w) / float(rt.h);
    static const int test = [] {
        const char* e = std::getenv("XBE_RT_TEST");
        return e && *e >= '1' && *e <= '3' ? *e - '0' : 0;
    }();
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    Barrier();
    bool kept = ds->rt_depth.img && ds->rt_depth_frame == presents_ + 1;
    float c[12] = {1.0f / rt.w, 1.0f / rt.h, 0.5774f, aspect,
                   st.rt_radius, st.rt_ao, float(st.rt_rays), scene_wbuf_ ? 1.0f : 0.0f,
                   float(test), kept ? 1.0f : 0.0f, 1.0f / hw, 1.0f / hh};
    VkImageView keptv = kept ? ds->rt_depth.view : ds->res.view;
    VkViewport hvp{0, 0, float(hw), float(hh), 0, 1};
    VkRect2D hsc{{0, 0}, {hw, hh}};
    VkImageView tv[4] = {rt.fb.view, ds->res.view, VK_NULL_HANDLE, keptv};
    FullScreen(pt, {rt.rt_ao.att, VK_NULL_HANDLE, hw, hh}, hvp, hsc, c, 12, tv, 4, rt_[frame_slot_].tlas);
    End();
    Barrier();
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    VkImageView views[4] = {rt.fb.view, ds->res.view, rt.rt_ao.view, keptv};
    FullScreen(pc, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, 12, views, 4, rt_[frame_slot_].tlas);
    End();
    rt_drawn_ = true;
    if (test && presents_ % 300 == 0)
        LOGR("vulkan: ray tracing: %u cut-out and %u gun triangles this frame, %u textures; %llu draws traced, %llu triangles; skipped: %llu not the scene, %llu no depth write, "
             "%llu not triangles, %llu blended, %llu cut-outs",
             uint32_t(rt_cut_.size() / 3), uint32_t(rt_gun_.size() / 3), uint32_t(rt_views_.size()),
             (unsigned long long)perf_rt_draws_,
             (unsigned long long)perf_rt_tris_, (unsigned long long)rt_skips_[0],
             (unsigned long long)rt_skips_[1], (unsigned long long)rt_skips_[2], (unsigned long long)rt_skips_[3],
             (unsigned long long)rt_skips_[4]);
    static bool said = false;
    if (!said) said = true, LOGR("vulkan: ray traced ambient occlusion drawn (%u triangles, %d rays, %.2f m)",
                                 uint32_t((rt_tris_.size() + rt_cut_.size() + rt_gun_.size()) / 3), st.rt_rays, st.rt_radius);
}

// ray traced reflections over the scene (kRtReflectHlsl), where this frame's reflective draws made the mask: in place
// of the style pass's screen-space ones
void VulkanBackend::RtReflectPass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    if (!ds || ds->w != rt.w || ds->h != rt.h || st.reflections == kReflectOff || st.mode == kStyleRetro ||
        !rt.mask.img || rt.mask_frame != presents_ + 1)
        return;
    static const std::string src = std::string(kRtCommonHlsl) + kRtReflectHlsl;
    uint64_t vh, ph;
    if (!Compile(src, "vs", "vs_6_5", vh) || !Compile(src, "ps", "ps_6_5", ph)) {
        LOG_FIRST(1, "vulkan: the ray traced reflections did not compile; the screen-space ones are drawn");
        return;
    }
    PassKey pk{vh, ph, kBgra, VK_FORMAT_UNDEFINED, 0, 0};
    pk.rt = 1;
    VkPipeline pipe = PassPipeline(pk);
    if (!pipe) return;
    if (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    static const int test = [] {
        const char* e = std::getenv("XBE_RT_TEST");
        return e && *e == '4' ? 4 : 0;
    }();
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    Barrier();
    bool kept = ds->rt_depth.img && ds->rt_depth_frame == presents_ + 1;
    float c[12] = {1.0f / rt.w, 1.0f / rt.h, 0.5774f, float(rt.w) / float(rt.h),
                   st.reflection_strength, scene_wbuf_ ? 1.0f : 0.0f, float(test), 2.0f,
                   kept ? 1.0f : 0.0f, 0, 0, 0};
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    VkImageView views[4] = {rt.fb.view, ds->res.view, rt.mask.view, kept ? ds->rt_depth.view : ds->res.view};
    FullScreen(pipe, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, 12, views, 4, rt_[frame_slot_].tlas);
    End();
    rt_reflected_ = true;
    static bool said = false;
    if (!said) said = true, LOGR("vulkan: ray traced reflections drawn (strength %.2f)", st.reflection_strength);
}

// the level lit again (kRtRelightHlsl): the sky's share eased into a 1 x 1 image, then the scene relit
void VulkanBackend::RtRelightPass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    if (!ds || ds->w != rt.w || ds->h != rt.h || !scene_wbuf_ || rt_cams_.empty()) return;
    static const std::string src = std::string(frag_stores_ ? "#define STATS 1\n" : "#define STATS 0\n") + kRtCommonHlsl + kRtRelightHlsl;
    uint64_t vh, ph;
    if (!Compile(src, "vs", "vs_6_5", vh) || !Compile(src, "ps", "ps_6_5", ph)) {
        LOG_FIRST(1, "vulkan: the ray traced level lighting did not compile; the level keeps its own");
        return;
    }
    PassKey kp{vh, ph, kBgra, VK_FORMAT_UNDEFINED, 0, 0};
    kp.rt = 1;
    VkPipeline pl = PassPipeline(kp);
    if (!pl) return;
    if (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    static const int test = [] {
        const char* e = std::getenv("XBE_RT_TEST");
        return e && *e >= '5' && *e <= '7' ? *e - '0' : 0;
    }();
    // the bounced light (rt_gi): traced at half the size into rt.rt_gi first
    constexpr VkFormat kGiFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr int kGiRays = 4;
    VkPipeline pgi = VK_NULL_HANDLE;
    const uint32_t gw = std::max<uint32_t>(rt.w / 2, 1), gh = std::max<uint32_t>(rt.h / 2, 1);
    if (st.rt_gi && st.rt_gi_strength > 0) {
        uint64_t gih;
        if (!Compile(src, "gi", "ps_6_5", gih)) {
            LOG_FIRST(1, "vulkan: the ray traced bounced light did not compile; none drawn");
        } else {
            PassKey kg{vh, gih, kGiFormat, VK_FORMAT_UNDEFINED, 0, 0};
            kg.rt = 1;
            pgi = PassPipeline(kg);
        }
        if (rt.rt_gi.img && (rt.rt_gi.w != gw || rt.rt_gi.h != gh)) DropImage(rt.rt_gi);
        if (pgi && !rt.rt_gi.img &&
            !MakeImage(rt.rt_gi, VK_IMAGE_TYPE_2D, kGiFormat, gw, gh, 1, 1, 1,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            pgi = VK_NULL_HANDLE;
    }
    rt_gi_drawn_ = pgi != VK_NULL_HANDLE;
    // the camera: the one most of the frame's triangles were drawn with (the level's)
    const RtCam* cam = nullptr;
    for (const auto& [h, c] : rt_cams_)
        if (!cam || c.tris > cam->tris) cam = &c;
    const float ty = 0.5774f, aspect = float(rt.w) / float(rt.h);
    const float scale[3] = {ty * aspect, ty, 1};  // the instance's (RtBuild)
    auto place = [&](const float* w, float one, float* out) {
        for (int r = 0; r < 3; r++)
            out[r] = scale[r] * (cam->r[r][0] * w[0] + cam->r[r][1] * w[1] + cam->r[r][2] * w[2] + cam->r[r][3] * one);
    };
    // a world unit in the view's: the camera's forward row (the distance) is a unit vector times the scale
    const float unit = std::sqrt(cam->r[2][0] * cam->r[2][0] + cam->r[2][1] * cam->r[2][1] + cam->r[2][2] * cam->r[2][2]);
    float c[(8 + 2 * kRtLights) * 4] = {};
    // the game's lights, the nearest kRtLights that reach before the camera
    static SceneLight all[256];
    uint32_t n = SceneLights(all, 256);
    struct Near { float key; float v[8]; };
    std::vector<Near> nearest;
    nearest.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        Near l{};
        place(all[i].pos, 1, l.v);
        l.v[3] = all[i].radius * unit * 3;  // the game's radius is where its light is full
        if (l.v[2] + l.v[3] < 0) continue;
        for (int k = 0; k < 3; k++) l.v[4 + k] = all[i].rgb[k];
        l.key = std::sqrt(l.v[0] * l.v[0] + l.v[1] * l.v[1] + l.v[2] * l.v[2]) - l.v[3];
        nearest.push_back(l);
    }
    std::sort(nearest.begin(), nearest.end(), [](const Near& a, const Near& b) { return a.key < b.key; });
    uint32_t lights = uint32_t(std::min<size_t>(nearest.size(), kRtLights));
    for (uint32_t i = 0; i < lights; i++) std::memcpy(&c[(8 + 2 * i) * 4], nearest[i].v, sizeof(nearest[i].v));
    // the sun (SunDirection, the world's +y up): warmer the lower it stands
    float sun_w[3], sun[3];
    SunDirection(sun_w);
    float wl = std::sqrt(sun_w[0] * sun_w[0] + sun_w[1] * sun_w[1] + sun_w[2] * sun_w[2]);
    float high = std::clamp(wl > 0 ? sun_w[1] / wl : 1.0f, 0.0f, 1.0f);
    float low = 1.0f - std::clamp((high - 0.35f) / 0.45f, 0.0f, 1.0f);
    place(sun_w, 0, sun);
    float sl = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
    for (float& v : sun) v = sl > 0 ? v / sl : 0;
    float up[3];
    const float y[3] = {0, 1, 0};
    place(y, 0, up);
    float ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    for (float& v : up) v = ul > 0 ? v / ul : 0;
    // the sun's colour from the level's outdoor baked light (what the shader added up kFramesInFlight frames ago,
    // eased): its hue, a little whiter, and dimmer where that light is dark (a night's moon); a low sun warmer
    RtFrame& fs = rt_[frame_slot_];
    if (!fs.stats.buf && frag_stores_) MakeBuffer(fs.stats, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    if (fs.stats.buf && fs.stats_frame != presents_ + 1) {
        uint32_t* sum = reinterpret_cast<uint32_t*>(fs.stats.mem.map);
        if (fs.stats_frame && sum[3] > 64) {
            float w = std::min(1.0f, rt_sky_[3] + 0.02f), ease = rt_sky_[3] < 0.02f ? 1.0f : 0.03f;
            for (int k = 0; k < 3; k++) rt_sky_[k] += (sum[k] / 64.0f / sum[3] - rt_sky_[k]) * ease;
            rt_sky_[3] = w;
        }
        std::memset(sum, 0, 16);
        fs.stats_frame = presents_ + 1;
    }
    auto ramp = [](float v, float a, float b) {
        float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    };
    float sunc[3] = {1.00f, 0.95f - 0.17f * low, 0.85f - 0.30f * low}, power = 1.0f;
    float bl = 0.299f * rt_sky_[0] + 0.587f * rt_sky_[1] + 0.114f * rt_sky_[2];
    float bm = std::max({rt_sky_[0], rt_sky_[1], rt_sky_[2], 1e-3f});
    float tint[3];
    for (int k = 0; k < 3; k++) tint[k] = 0.35f + 0.65f * rt_sky_[k] / bm;
    float known = rt_sky_[3] * ramp(bm, 0.01f, 0.05f);
    for (int k = 0; k < 3; k++) sunc[k] += (tint[k] - sunc[k]) * 0.7f * known;
    power = 1.0f - 0.6f * ramp(bl, 0.45f, 0.15f) * known;
    // logged: what the sun took (a night, dusk, snow, a day)
    int kind = bl < 0.3f ? 1 : tint[2] < 0.75f ? 2 : bl > 1.2f ? 3 : 0;
    static double next_log = 0;
    double now_s = double(presents_) / 60.0;
    if (rt_sky_[3] > 0.9f && (kind != rt_sky_kind_ || now_s > next_log)) {
        static const char* names[] = {"day", "night", "dusk", "snow"};
        LOGR("vulkan: the level's sun: %s (outdoor baked light %.2f %.2f %.2f, colour %.2f %.2f %.2f, strength %.2f)",
             names[kind], rt_sky_[0], rt_sky_[1], rt_sky_[2], sunc[0], sunc[1], sunc[2], power);
        rt_sky_kind_ = kind;
        next_log = now_s + 10;
    }
    bool kept = ds->rt_depth.img && ds->rt_depth_frame == presents_ + 1;
    const float p[8 * 4] = {1.0f / rt.w, 1.0f / rt.h, ty, aspect,
                            float(lights), kept ? 1.0f : 0.0f, float(test), fs.stats.buf ? 1.0f : 0.0f,
                            sun[0], sun[1], sun[2], st.rt_sun * power,
                            sunc[0], sunc[1], sunc[2], pgi ? 0.25f : 0.35f,
                            rt_fog_[0], rt_fog_[1], rt_fog_[2], 1.2f,
                            up[0], up[1], up[2], 0.6f,
                            0.06f, 0.06f, 0.06f, 0,
                            pgi ? st.rt_gi_strength : 0.0f, float(kGiRays), 1.0f / gw, 1.0f / gh};
    std::memcpy(c, p, sizeof(p));
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    Barrier();
    VkImageView keptv = kept ? ds->rt_depth.view : ds->res.view;
    const uint32_t floats = uint32_t(std::size(c));
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    if (pgi) {
        VkImageView gv[4] = {rt.fb.view, ds->res.view, keptv, dummy_.view};
        VkViewport gvp{0, 0, float(gw), float(gh), 0, 1};
        VkRect2D gsc{{0, 0}, {gw, gh}};
        FullScreen(pgi, {rt.rt_gi.att, VK_NULL_HANDLE, gw, gh}, gvp, gsc, c, floats, gv, 4, rt_[frame_slot_].tlas);
        End();
        Barrier();
        static bool said_gi = false;
        if (!said_gi) said_gi = true, LOGR("vulkan: ray traced bounced light drawn (%d rays at %ux%u, strength %.2f)", kGiRays, gw, gh, st.rt_gi_strength);
    }
    VkImageView views[4] = {rt.fb.view, ds->res.view, keptv, pgi ? rt.rt_gi.view : dummy_.view};
    FullScreen(pl, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, floats, views, 4, rt_[frame_slot_].tlas);
    End();
    static bool said = false;
    if (!said) {
        said = true;
        LOGR("vulkan: ray traced level lighting drawn (%u of the game's %u lights; a world unit %.3f; the world's +y "
             "in the view %.2f %.2f %.2f; %zu cameras)",
             lights, n, unit, up[0] * ul, up[1] * ul, up[2] * ul, rt_cams_.size());
    }
}

void VulkanBackend::RtKeepDepth(Target& ds) {
    if (ds.rt_depth.img && (ds.rt_depth.w != ds.w || ds.rt_depth.h != ds.h)) DropImage(ds.rt_depth);
    if (!ds.rt_depth.img && !MakeImage(ds.rt_depth, VK_IMAGE_TYPE_2D, depth_fmt_, ds.w, ds.h, 1, 1, 1,
                                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    if (ds.rt_depth_frame == presents_ + 1) return;  // the first clear's: the world's
    ds.rt_depth_frame = presents_ + 1;
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    cp.extent = {ds.w, ds.h, 1};
    vkCmdCopyImage(Cmd(), ds.res.img, VK_IMAGE_LAYOUT_GENERAL, ds.rt_depth.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
}

void VulkanBackend::DropRt(RtFrame& f) {
    if (f.blas) vkDestroyAccelerationStructureKHR(dev_, f.blas, nullptr);
    if (f.tlas) vkDestroyAccelerationStructureKHR(dev_, f.tlas, nullptr);
    if (f.gun_blas) vkDestroyAccelerationStructureKHR(dev_, f.gun_blas, nullptr);
    f.blas = f.tlas = f.gun_blas = VK_NULL_HANDLE;
    DropBuffer(f.gun_blas_mem);
    DropBuffer(f.verts), DropBuffer(f.tris), DropBuffer(f.mats), DropBuffer(f.inst), DropBuffer(f.scratch);
    DropBuffer(f.stats);
    DropBuffer(f.blas_mem), DropBuffer(f.tlas_mem);
    f.vert_cap = 0;
}

// a glowing effect, what lights its surroundings: added over the 3D scene (its colour, 2, or by its alpha, 1), depth
// tested, not written; 0 not one
int VulkanBackend::Emitter(const PsoKey& k) {
    const GraphicsStyle& st = Style();
    if (!st.effect_lights || st.mode == kStyleRetro || draw_2d_ || !bound_rt_ || !bound_ds_ ||
        bound_rt_->gw != guest_w_ || bound_rt_->gh != guest_h_ || bound_ds_->w != bound_rt_->w ||
        bound_ds_->h != bound_rt_->h || !k.blend || !k.zen || k.zwrite || k.topo == uint32_t(Topology::Points))
        return 0;
    constexpr uint32_t kOne = 1, kSrcAlpha = 0x302;  // GL values
    if (k.dst != kOne) return 0;
    return k.src == kOne ? 2 : k.src == kSrcAlpha ? 1 : 0;
}

// the draw again into the glow image, over the scene's depth
void VulkanBackend::DrawGlow(const Assembled& a, PsoKey k, const DrawShaders& sh, const PixelKey& pk,
                             const DrawTextures& stages, const VkSampler smp[4], const float bf[4], uint32_t sref,
                             const VkViewport& vp, const VkRect2D& sc, int mode) {
    Target& t = *bound_rt_;
    Target& d = *bound_ds_;
    if (t.glow.img && (t.glow.w != t.w || t.glow.h != t.h)) DropImage(t.glow);
    if (!t.glow.img && !MakeImage(t.glow, VK_IMAGE_TYPE_2D, kMaskFormat, t.w, t.h, 1, 1, 1,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        LOG_FIRST(1, "vulkan: no memory for the effect lights' image, effects light nothing");
        return;
    }
    uint64_t key = sh.pkey ^ kGlowKeySalt ^ uint64_t(mode) << 56;
    uint64_t gh = 0;
    if (auto it = shader_src_.find(key); it != shader_src_.end()) {
        gh = it->second;
    } else {
        std::string src = PixelHlsl(pk);
        size_t at = src.find("float4 main(");
        if (at != std::string::npos) {
            src.replace(at, 12, "float4 game_main(");
            char tail[1024];
            std::snprintf(tail, sizeof(tail), kGlowMain, pk.wbuf ? ", out float depth : SV_Depth" : "",
                          pk.wbuf ? ", depth" : "", mode == 2 ? "c.rgb" : "c.rgb * c.a",
                          pk.wbuf ? "(i.wlin * wdepth.w * wdepth.z * 4096)" : "(1 / max(1 - i.pos.z, 1e-6))");
            src += tail;
            if (!Compile(src, "main", "ps_5_0", gh)) gh = 0;
        }
        if (!gh) LOG_FIRST(4, "vulkan: an effect's light shader could not be made, it lights nothing");
        shader_src_[key] = gh;
    }
    if (!gh) return;
    k.ps = gh;
    k.blend = 1;
    k.src = k.dst = 1;  // GL ONE: summed
    k.wmask = 0xF;
    k.zen = 1, k.zwrite = 0;
    k.zfunc = 0x203;  // GL_LEQUAL
    k.sten = k.sfunc = k.smask = k.swmask = k.sfail = k.szfail = k.spass = 0;
    k.has_rt = k.has_ds = 1;
    k.rtfmt = kMaskRtFmt;
    VkPipeline p = EnsurePipeline(k);
    if (!p) return;
    Begin({t.glow.att, d.res.att, t.w, t.h});
    if (t.glow_frame != presents_ + 1) {
        t.glow_frame = presents_ + 1;
        VkClearAttachment ca{};
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VkClearRect cr{{{0, 0}, {t.w, t.h}}, 0, 1};
        vkCmdClearAttachments(cmd_, 1, &ca, 1, &cr);
    }
    VkCommandBuffer cb = cmd_;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ds_.pipeline = p);
    PushDescriptors(last_vsc_, last_dc_, stages, smp);
    vkCmdSetBlendConstants(cb, bf);
    std::memcpy(ds_.bf, bf, sizeof(ds_.bf));
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, ds_.sref = sref);
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    ds_.vp = vp;
    ds_.sc = sc;
    ds_.vp_ok = true;
    DrawGeometry(a);
    perf_glow_++;
}

// effect lights: the glow image made into a light a tile, the scene lit by them (before the style's pass grades it)
void VulkanBackend::LightPass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    if (!ds || ds->w != rt.w || ds->h != rt.h || !rt.glow.img || rt.glow_frame != presents_ + 1) return;
    uint64_t vh, ah, bh, kh, lh;
    if (!Compile(kLightsHlsl, "vs", "vs_5_0", vh) || !Compile(kLightsHlsl, "tilea", "ps_5_0", ah) ||
        !Compile(kLightsHlsl, "pick", "ps_5_0", kh) ||
        !Compile(kLightsHlsl, "tileb", "ps_5_0", bh) || !Compile(kLightsHlsl, "light", "ps_5_0", lh)) {
        LOG_FIRST(1, "vulkan: the effect lights' passes did not compile; effects light nothing");
        return;
    }
    // ray traced shadows: the light pass's traced variant
    bool traced = false;
    VkPipeline plt = VK_NULL_HANDLE;
    if (rt_built_ && st.rt_shadows) {
        static const std::string src = std::string("#define RT 1\n") + kRtCommonHlsl + kLightsHlsl;
        uint64_t tvh, th;
        if (Compile(src, "vs", "vs_6_5", tvh) && Compile(src, "light", "ps_6_5", th)) {
            PassKey k{tvh, th, kBgra, VK_FORMAT_UNDEFINED, 0, 0};
            k.rt = 1;
            plt = PassPipeline(k);
        }
        if (!plt) LOG_FIRST(1, "vulkan: the effect lights' traced shadows did not compile; lights cast none");
        traced = plt != VK_NULL_HANDLE;
    }
    constexpr VkFormat kTileFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkPipeline pa = PassPipeline({vh, ah, kTileFormat, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pb = PassPipeline({vh, bh, kTileFormat, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pk = PassPipeline({vh, kh, kTileFormat, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pl = traced ? plt : PassPipeline({vh, lh, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pa || !pb || !pk || !pl) return;
    const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((!rt.light_a.img && !MakeImage(rt.light_a, VK_IMAGE_TYPE_2D, kTileFormat, kLightTilesX, kLightTilesY, 1, 1, 1, u)) ||
        (!rt.light_b.img && !MakeImage(rt.light_b, VK_IMAGE_TYPE_2D, kTileFormat, kLightTilesX, kLightTilesY, 1, 1, 1, u)) ||
        (!rt.light_list.img && !MakeImage(rt.light_list, VK_IMAGE_TYPE_2D, kTileFormat, kLights * 2, 1, 1, 1, 1, u)) ||
        (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)))
        return;
    static const int test = [] {
        const char* e = std::getenv("XBE_LIGHT_TEST");
        return e && *e >= '1' && *e <= '9' ? *e - '0' : 0;
    }();
    float c[16] = {float(rt.w), float(rt.h), 0, float(kLights),
                   st.light_strength, 0.5774f, float(rt.w) / float(rt.h), scene_wbuf_ ? 1.0f : 0.0f,
                   float(kLightTilesX), float(kLightTilesY), 0.002f, float(test),
                   1.0f / rt.w, 1.0f / rt.h, 3000.0f, 0};
    bool kept = ds->rt_depth.img && ds->rt_depth_frame == presents_ + 1;
    c[15] = kept ? 1.0f : 0.0f;
    End();
    if (dirty_) Barrier();
    VkViewport tv{0, 0, float(kLightTilesX), float(kLightTilesY), 0, 1};
    VkRect2D ts{{0, 0}, {kLightTilesX, kLightTilesY}};
    FullScreen(pa, {rt.light_a.att, VK_NULL_HANDLE, kLightTilesX, kLightTilesY}, tv, ts, c, 16, &rt.glow.view, 1);
    End();
    FullScreen(pb, {rt.light_b.att, VK_NULL_HANDLE, kLightTilesX, kLightTilesY}, tv, ts, c, 16, &rt.glow.view, 1);
    End();
    Barrier();
    VkViewport lv{0, 0, float(kLights * 2), 1, 0, 1};
    VkRect2D ls{{0, 0}, {kLights * 2, 1}};
    VkImageView tiles[4] = {rt.glow.view, VK_NULL_HANDLE, rt.light_a.view, rt.light_b.view};
    FullScreen(pk, {rt.light_list.att, VK_NULL_HANDLE, kLights * 2, 1}, lv, ls, c, 16, tiles, 4);
    End();
    Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    VkImageView views[4] = {rt.fb.view, ds->res.view, rt.light_list.view, kept ? ds->rt_depth.view : ds->res.view};
    FullScreen(pl, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, 16, views, 4,
               traced ? rt_[frame_slot_].tlas : VK_NULL_HANDLE);
    End();
}

// a particle: blended over the 3D scene (alpha blended, or added), depth tested but not written, no reflective
// surface, no glass; how its blend is faded (1 its alpha, 2 its colour and alpha), 0 not one
int VulkanBackend::SoftParticle(const PsoKey& k, const PixelKey& pk, float refl) {
    const GraphicsStyle& st = Style();
    if (!st.soft_particles || st.mode == kStyleRetro || draw_2d_ || !bound_rt_ || !bound_ds_ ||
        bound_rt_->gw != guest_w_ || bound_rt_->gh != guest_h_ || bound_ds_->w != bound_rt_->w ||
        bound_ds_->h != bound_rt_->h || !k.blend || !k.zen || k.zwrite || refl > 0 || stage_glass_ > 0)
        return 0;
    (void)pk;
    constexpr uint32_t kOne = 1, kSrcAlpha = 0x302, kInvSrcAlpha = 0x303;  // GL values
    if (k.src == kSrcAlpha && (k.dst == kInvSrcAlpha || k.dst == kOne)) return 1;
    if (k.src == kOne && k.dst == kOne) return 2;
    return 0;
}

uint64_t VulkanBackend::SoftShader(const DrawShaders& sh, const PixelKey& pk, int slot) {
    uint64_t key = sh.pkey ^ kSoftKeySalt ^ uint64_t(slot) << 56;
    if (auto it = shader_src_.find(key); it != shader_src_.end()) return it->second;
    uint64_t h = 0;
    std::string src = PixelHlsl(pk);
    size_t at = src.find("float4 main(");
    if (at != std::string::npos) {
        src.replace(at, 12, "float4 game_main(");
        char tail[1024];
        // a W-buffer's depth is the distance (game_main writes it); else Z, made a distance as the scene pass does
        std::snprintf(tail, sizeof(tail), kSoftMain, pk.wbuf ? ", out float depth : SV_Depth" : "",
                      pk.wbuf ? ", depth" : "", pk.wbuf ? "depth" : "i.pos.z", slot,
                      pk.wbuf ? "fz" : "1 / max(1 - fz, 1e-6)", pk.wbuf ? "sz" : "1 / max(1 - sz, 1e-6)");
        src += tail;
        if (!Compile(src, "main", "ps_5_0", h)) h = 0;
    }
    if (!h) LOG_FIRST(4, "vulkan: a soft particle shader could not be made, the particle is drawn as the game's");
    shader_src_[key] = h;
    return h;
}

VkImageView VulkanBackend::MaterialView(uint64_t key) {
    auto it = materials_.find(key);
    if (it != materials_.end()) return it->second.view;  // null: the picture has none
    Image& im = materials_[key];
    std::vector<std::vector<uint32_t>> levels;
    uint32_t w = 0, h = 0;
    if (!TakePackMaterial(key, levels, w, h) || levels.empty()) return VK_NULL_HANDLE;
    if (!MakeImage(im, VK_IMAGE_TYPE_2D, kBgra, w, h, 1, uint32_t(levels.size()), 1,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        im = Image{};
        return VK_NULL_HANDLE;
    }
    for (uint32_t l = 0; l < levels.size(); l++)
        UploadLevel(im, l, 0, std::max(1u, w >> l), std::max(1u, h >> l), 1, levels[l].data(), levels[l].size() * 4);
    return im.view;
}

VkImageView VulkanBackend::DetailView(uint8_t kind) {
    Image& im = details_[kind];
    if (im.img) return im.view;
    std::vector<std::vector<uint32_t>> levels;
    DetailPicture(kind, levels);
    if (levels.empty() || !MakeImage(im, VK_IMAGE_TYPE_2D, kBgra, kDetailSize, kDetailSize, 1, uint32_t(levels.size()), 1,
                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        im = Image{};
        return VK_NULL_HANDLE;
    }
    for (uint32_t l = 0; l < levels.size(); l++) {
        uint32_t size = std::max(1u, kDetailSize >> l);
        UploadLevel(im, l, 0, size, size, 1, levels[l].data(), levels[l].size() * 4);
    }
    LOGR("vulkan: ground detail: the %s picture made", DetailName(kind));
    return im.view;
}

uint64_t VulkanBackend::DetailShader(const DrawShaders& sh, const PixelKey& pk, int slot) {
    uint64_t key = sh.pkey ^ kDetailKeySalt ^ uint64_t(slot) << 56;
    if (auto it = shader_src_.find(key); it != shader_src_.end()) return it->second;
    uint64_t h = 0;
    std::string src = PixelHlsl(pk);
    size_t at = src.find("float4 main(");
    if (at != std::string::npos) {
        src.replace(at, 12, "float4 game_main(");
        bool proj = pk.programmable || (pk.proj & 1);
        std::string uv = proj ? "i.tc0.xy / i.tc0.w" : "i.tc0.xy";
        uv = "(" + uv + ")" + (pk.tex[0].rect ? " / texsize[0].xy" : "") + " + texsize[0].zw";
        char tail[8192];
        std::snprintf(tail, sizeof(tail), kDetailMain, pk.wbuf ? ", out float depth : SV_Depth" : "", uv.c_str(),
                      pk.wbuf ? ", depth" : "", slot, slot);
        src += tail;
        if (!Compile(src, "main", "ps_5_0", h)) h = 0;
    }
    if (!h) LOG_FIRST(4, "vulkan: a ground detail shader could not be made, the surface is drawn as the game's");
    shader_src_[key] = h;
    return h;
}

uint64_t VulkanBackend::MaterialShader(const DrawShaders& sh, const PixelKey& pk, int slot) {
    uint64_t key = sh.pkey ^ kMatKeySalt ^ uint64_t(slot) << 56;
    if (auto it = shader_src_.find(key); it != shader_src_.end()) return it->second;
    uint64_t h = 0;
    std::string src = PixelHlsl(pk);
    size_t at = src.find("float4 main(");
    if (at != std::string::npos) {
        src.replace(at, 12, "float4 game_main(");
        // stage 0's coordinates as its fetch takes them (projected for a combiner's or a projected stage)
        bool proj = pk.programmable || (pk.proj & 1);
        std::string uv = proj ? "i.tc0.xy / i.tc0.w" : "i.tc0.xy";
        uv = "(" + uv + ")" + (pk.tex[0].rect ? " / texsize[0].xy" : "") + " + texsize[0].zw";
        // a move of uv as one of stage 0's coordinates (undoing the projection and a rectangle's scale)
        std::string scale = std::string("float2(1, 1)") + (proj ? " * i.tc0.w" : "") + (pk.tex[0].rect ? " * texsize[0].xy" : "");
        char tail[8192];
        std::snprintf(tail, sizeof(tail), kMatMain, pk.wbuf ? ", out float depth : SV_Depth" : "", uv.c_str(),
                      scale.c_str(), slot, slot, slot, slot, pk.wbuf ? ", depth" : "", slot, slot);
        src += tail;
        if (!Compile(src, "main", "ps_5_0", h)) h = 0;
    }
    if (!h) LOG_FIRST(4, "vulkan: a material shader could not be made, the surface is drawn as the game's");
    shader_src_[key] = h;
    return h;
}

// the depth target's copy, made at the frame's first soft particle (the particles that follow fade against it)
VkImageView VulkanBackend::SoftDepth(Target& ds) {
    if (ds.soft_depth.img && (ds.soft_depth.w != ds.w || ds.soft_depth.h != ds.h)) DropImage(ds.soft_depth);
    if (!ds.soft_depth.img && !MakeImage(ds.soft_depth, VK_IMAGE_TYPE_2D, depth_fmt_, ds.w, ds.h, 1, 1, 1,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        LOG_FIRST(1, "vulkan: no memory for soft particles' depth, particles are drawn as the game's");
        return VK_NULL_HANDLE;
    }
    if (ds.soft_frame != presents_ + 1) {
        ds.soft_frame = presents_ + 1;
        End();
        if (dirty_) Barrier();
        VkImageCopy cp{};
        cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        cp.extent = {ds.w, ds.h, 1};
        vkCmdCopyImage(Cmd(), ds.res.img, VK_IMAGE_LAYOUT_GENERAL, ds.soft_depth.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
        dirty_ = true;
    }
    return ds.soft_depth.view;
}

// ---- reflections

uint64_t g_ssr_diag[6];
// how much the draw reflects: what mods/reflections.toml gives its texture, else, found by itself, window glass (alpha
// blended over the 3D scene, depth tested, a texture whose alpha looks like glass); 0 none
float VulkanBackend::Reflectivity(const PsoKey& k) {
    const GraphicsStyle& st = Style();
    if (st.reflections != kReflectOff && !draw_2d_)
        g_ssr_diag[0]++, g_ssr_diag[1] += stage_seen_, g_ssr_diag[2] += bound_rt_ && bound_ds_,
            g_ssr_diag[3] += bound_rt_ && bound_rt_->gw == guest_w_ && bound_rt_->gh == guest_h_,
            g_ssr_diag[4] += Opaque(k) || stage_solid_, g_ssr_diag[5] += scene_wbuf_;
    if (st.reflections == kReflectOff || st.mode == kStyleRetro || draw_2d_ || !stage_seen_ || !bound_rt_ ||
        !bound_ds_ || bound_rt_->gw != guest_w_ || bound_rt_->gh != guest_h_)
        return 0;
    float listed = ReflectionOf(stage_key_);
    if (listed >= 0) return listed;
    // XBE_SSR_TEST=1: every opaque 3D surface reflects (the reflection pass checked without a list)
    static const bool test = [] {
        const char* e = std::getenv("XBE_SSR_TEST");
        return e && *e >= '1' && *e <= '4';
    }();
    const bool covers = Opaque(k) || stage_solid_;
    if (test && covers && k.zen) return 0.6f;
    if (!st.reflections_auto || !k.zen) return 0;
    // metal: the game gives it an environment cube map of its own; ours shows the real surroundings over it
    if (draw_env_ && covers) return 0.5f;
    // a pack picture with a roughness map (hd_pack --pbr): polished where it says so
    if (float r = PackRoughness(stage_key_); r >= 0 && r < 0.45f && covers) return (0.45f - r) / 0.45f * 0.8f;
    constexpr uint32_t kSrcAlpha = 0x302, kInvSrcAlpha = 0x303;  // GL values
    if (!k.blend || k.src != kSrcAlpha || k.dst != kInvSrcAlpha) return 0;
    return stage_glass_;
}

// the target's mask and its depth, cleared at its first use in a frame
bool VulkanBackend::EnsureMask(Target& t) {
    if (t.mask.img && (t.mask.w != t.w || t.mask.h != t.h)) DropImage(t.mask), DropImage(t.mask_ds);
    if (!t.mask.img) {
        const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!MakeImage(t.mask, VK_IMAGE_TYPE_2D, kMaskFormat, t.w, t.h, 1, 1, 1, u | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            !MakeImage(t.mask_ds, VK_IMAGE_TYPE_2D, depth_fmt_, t.w, t.h, 1, 1, 1,
                       u | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            LOG_FIRST(1, "vulkan: no memory for the reflection mask, reflections are not drawn");
            DropImage(t.mask), DropImage(t.mask_ds);
            return false;
        }
        t.mask_frame = 0;
    }
    if (t.mask_frame != presents_ + 1) {
        t.mask_frame = presents_ + 1;
        Begin({t.mask.att, t.mask_ds.att, t.w, t.h});
        VkClearAttachment ca[2]{};
        ca[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ca[0].clearValue.color = {{0, 0, 0, 0}};
        ca[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        ca[1].clearValue.depthStencil = {1.0f, 0};
        VkClearRect cr{{{0, 0}, {t.w, t.h}}, 0, 1};
        vkCmdClearAttachments(cmd_, 2, ca, 1, &cr);
    }
    return true;
}

// the draw again into the mask: its vertex shader, its pixel shader for the coverage, the nearest surface kept
void VulkanBackend::DrawMask(const Assembled& a, PsoKey k, const DrawShaders& sh, const PixelKey& pk,
                             const DrawTextures& stages, const VkSampler smp[4], const float bf[4], uint32_t sref,
                             const VkViewport& vp, const VkRect2D& sc) {
    if (!bound_rt_ || k.topo == uint32_t(Topology::Points)) return;
    Target& t = *bound_rt_;
    if (!EnsureMask(t)) return;
    uint64_t mh = 0;
    if (auto it = shader_src_.find(sh.pkey ^ kMaskKeySalt); it != shader_src_.end()) {
        mh = it->second;
    } else {
        std::string src = PixelHlsl(pk);
        size_t at = src.find("float4 main(");
        if (at != std::string::npos) {
            src.replace(at, 12, "float4 game_main(");
            src += pk.wbuf ? kMaskMainW : kMaskMain;
            if (!Compile(src, "main", "ps_5_0", mh)) mh = 0;
        }
        if (!mh) LOG_FIRST(4, "vulkan: a reflective draw's mask shader could not be made, it does not reflect");
        shader_src_[sh.pkey ^ kMaskKeySalt] = mh;
    }
    if (!mh) return;
    k.ps = mh;
    k.blend = k.src = k.dst = k.op = 0;
    k.wmask = 0xF;
    k.zen = k.zwrite = 1;
    k.zfunc = 0x203;  // GL_LEQUAL
    k.sten = k.sfunc = k.smask = k.swmask = k.sfail = k.szfail = k.spass = 0;
    k.has_rt = k.has_ds = 1;
    k.rtfmt = kMaskRtFmt;
    VkPipeline p = EnsurePipeline(k);
    if (!p) return;
    Begin({t.mask.att, t.mask_ds.att, t.w, t.h});
    VkCommandBuffer cb = cmd_;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ds_.pipeline = p);
    PushDescriptors(last_vsc_, last_dc_, stages, smp);
    vkCmdSetBlendConstants(cb, bf);
    std::memcpy(ds_.bf, bf, sizeof(ds_.bf));
    vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, ds_.sref = sref);
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    ds_.vp = vp;
    ds_.sc = sc;
    ds_.vp_ok = true;
    DrawGeometry(a);
}

// ---- visibility tests

bool VulkanBackend::NewQuery(uint32_t& index) {
    if (free_queries_.empty()) {
        LOG_FIRST(1, "vulkan: out of occlusion queries, a visibility test stays pending");
        return false;
    }
    index = free_queries_.back();
    free_queries_.pop_back();
    return true;
}

float VulkanBackend::SamplesPerPixel() const {
    const Target* t = bound_rt_ ? bound_rt_ : bound_ds_;
    if (!t || !t->gw || !t->gh) return 1;
    return float(t->w) * float(t->h) / (float(t->gw) * float(t->gh));
}

// counts no report holds go back to the pool, or they leak once counted_ forgets them
void VulkanBackend::ClearReport() {
    for (const Query& c : counted_) {
        bool held = false;
        for (size_t i = 0; i < reports_.size() && !held; i++)
            for (const Query& o : reports_[i].queries) held |= o.index == c.index;
        if (!held) dropped_.push_back(c);
    }
    counted_.clear();
}

void VulkanBackend::DeliverReports(bool wait) {
    constexpr uint32_t kTime = 0, kTimeHigh = 4, kCount = 8, kStatus = 12;
    // dropped counts are reset once the GPU is done with them
    uint64_t done = dropped_.empty() ? 0 : Completed();
    for (size_t i = 0; i < dropped_.size();) {
        if (dropped_[i].serial > done) {
            i++;
            continue;
        }
        uint32_t q = dropped_[i].index;
        if (std::find(free_queries_.begin(), free_queries_.end(), q) == free_queries_.end()) {
            vkResetQueryPool(dev_, queries_, q, 1);
            free_queries_.push_back(q);
        }
        dropped_[i] = dropped_.back();
        dropped_.pop_back();
    }
    while (!reports_.empty()) {
        Report& r = reports_.front();
        double pixels = 0;
        for (const Query& q : r.queries) {
            if (Completed() < q.serial) {
                if (!wait) return;  // not finished: this and every later report wait
                Wait(q.serial);
            }
            uint64_t n = 0;
            if (vkGetQueryPoolResults(dev_, queries_, q.index, 1, sizeof(n), &n, sizeof(n), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
                return;
            pixels += double(n) / q.samples_per_pixel;
        }
        kernel::W32(r.va + kTime, uint32_t(presents_));
        kernel::W32(r.va + kTimeHigh, 0);
        kernel::W32(r.va + kCount, uint32_t(std::lround(pixels)));
        std::atomic_thread_fence(std::memory_order_release);
        kernel::W32(r.va + kStatus, 0);
        // its queries are free again once no later report and no count in progress holds them
        for (const Query& q : r.queries) {
            bool held = false;
            for (size_t i = 1; i < reports_.size() && !held; i++)
                for (const Query& o : reports_[i].queries) held |= o.index == q.index;
            for (const Query& c : counted_) held |= c.index == q.index;
            if (!held && std::find(free_queries_.begin(), free_queries_.end(), q.index) == free_queries_.end()) {
                vkResetQueryPool(dev_, queries_, q.index, 1);
                free_queries_.push_back(q.index);
            }
        }
        reports_.pop_front();
    }
}

// ---- present

void VulkanBackend::UploadGammaRamp() {
    constexpr int kRed = 0, kGreen = 256, kBlue = 512;
    ramp_dirty_ = false;
    uint8_t px[256 * 4];
    bool identity = true;
    for (int i = 0; i < 256; i++) {
        px[i * 4] = ramp_[kBlue + i];
        px[i * 4 + 1] = ramp_[kGreen + i];
        px[i * 4 + 2] = ramp_[kRed + i];
        px[i * 4 + 3] = 255;
        identity &= ramp_[kRed + i] == i && ramp_[kGreen + i] == i && ramp_[kBlue + i] == i;
    }
    UploadLevel(lut_, 0, 0, 256, 1, 1, px, sizeof(px));
    gamma_ = !identity;
}

// the frame at its shape (16:9 or 4:3, ultrawide the screen's), centred on black
void VulkanBackend::BlitFrame(Target& t, VkImageView view, VkFormat fmt) {
    float aspect = app::FrameAspect();
    float w = float(bb_w_), h = float(bb_h_);
    float vw = std::min(w, h * aspect);
    float vh = vw / aspect;
    VkViewport vp{(w - vw) / 2, (h - vh) / 2, vw, vh, 0, 1};
    VkRect2D sc{{0, 0}, {bb_w_, bb_h_}};
    float uv[4] = {0, 0, 1, 1};
    // the swap chain image's rendering instance is already open (Present)
    open_ = true;
    cur_ = {view, VK_NULL_HANDLE, bb_w_, bb_h_};
    Blit(t.res.view, cur_, vp, sc, uv, {.gamma = gamma_, .final = true}, fmt);
}

// HDR (Settings::hdr, HdrOut): the frame and the overlay, drawn at the screen's size (HdrCompose), put together on an
// HDR10 chain: SDR's colours taken as gamma 2.2 at the SDR white Windows shows (paper), the 3D scene's highlights
// raised toward the screen's peak from a knee up (by luminance, so colours keep their hue); a pixel is the scene's
// where the presented frame still holds what the scene pass left there (hdr_scene: the HUD, menus and movies stay at
// SDR white), then BT.709 to BT.2020 and PQ. b1 p: [0] the frame's rectangle on the screen (pixels); [1] HDR10 (0: an
// SDR chain, the overlay's format alone differs), paper and peak nits, the scene known; [2] the game frame's size, the
// knee (linear). t0 the frame, t1 the overlay (premultiplied), t2 the presented game frame, t3 its scene
const char* kHdrHlsl = R"(
cbuffer P : register(b1) { float4 p[3]; };
Texture2D frame : register(t0);
Texture2D overlay : register(t1);
Texture2D game : register(t2);
Texture2D scene : register(t3);
struct O { float4 pos : SV_Position; };
O vs(uint id : SV_VertexID) {
    O o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float3 lin(float3 c) { return pow(saturate(c), 2.2); }
float3 pq(float3 nits) {
    float3 y = pow(saturate(nits / 10000), 0.1593017578125);
    return pow((0.8359375 + 18.8515625 * y) / (1 + 18.6875 * y), 78.84375);
}
float4 ps(O i) : SV_Target {
    int2 px = int2(i.pos.xy);
    float3 c = lin(frame.Load(int3(px, 0)).rgb);
    float2 uv = (i.pos.xy - p[0].xy) / p[0].zw;
    if (p[1].w > 0 && p[1].x > 0 && all(uv >= 0) && all(uv < 1)) {
        int3 g = int3(uv * p[2].xy, 0);
        float3 d = abs(game.Load(g).rgb - scene.Load(g).rgb);
        if (max(d.r, max(d.g, d.b)) < 1.5 / 255) {
            float y = dot(c, float3(0.2126, 0.7152, 0.0722));
            float t = saturate((y - p[2].z) / (1 - p[2].z));
            c *= 1 + (p[1].z / p[1].y - 1) * t * t;
            if (p[2].w > 0) c = float3(1, c.g * 0.3, c.b * 0.3);
        }
    }
    float4 u = overlay.Load(int3(px, 0));
    if (u.a > 0) c = lin(u.rgb / u.a) * u.a + c * (1 - u.a);
    if (p[1].x <= 0) return float4(pow(c, 1 / 2.2), 1);
    const float3x3 to2020 = {0.6274, 0.3293, 0.0433, 0.0691, 0.9195, 0.0114, 0.0164, 0.0880, 0.8956};
    return float4(pq(mul(to2020, c) * p[1].y), 1);
}
)";

// an HDR10 shot's pixels as SDR's (BGRA): PQ to nits, SDR white at paper, BT.2020 to BT.709, gamma 2.2, highlights
// clipped; A2B10G10R10 has red in the low bits, A2R10G10B10 blue
// XBE_HDR_TEST=1: the pixels taken as the scene's shown red
static bool HdrTest() {
    static const bool on = [] {
        const char* e = std::getenv("XBE_HDR_TEST");
        return e && *e == '1';
    }();
    return on;
}

static void HdrToSdr(std::vector<uint32_t>& px, bool red_low, float paper) {
    float lut[1024];
    for (int i = 0; i < 1024; i++) {
        float m = std::pow(float(i) / 1023.0f, 1.0f / 78.84375f);
        lut[i] = 10000.0f * std::pow(std::max(m - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * m), 1.0f / 0.1593017578125f) / paper;
    }
    auto enc = [](float v) { return uint32_t(std::lround(std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f)); };
    size_t over = 0;
    float top = 0;
    for (uint32_t& q : px) {
        float c[3] = {lut[q & 0x3FF], lut[q >> 10 & 0x3FF], lut[q >> 20 & 0x3FF]};
        if (!red_low) std::swap(c[0], c[2]);
        float r = 1.6605f * c[0] - 0.5876f * c[1] - 0.0728f * c[2];
        float g = -0.1246f * c[0] + 1.1329f * c[1] - 0.0083f * c[2];
        float b = -0.0182f * c[0] - 0.1006f * c[1] + 1.1187f * c[2];
        float m = std::max(r, std::max(g, b));
        over += m > 1.05f, top = std::max(top, m);
        q = 0xFF000000u | enc(r) << 16 | enc(g) << 8 | enc(b);
    }
    LOGR("vulkan: HDR shot: %.2f%% of the pixels above SDR white, the brightest %.0f nits", 100.0 * double(over) / double(px.size()),
         top * paper);
}

VulkanBackend::HdrNits VulkanBackend::HdrLevels() const {
    const app::Settings& s = app::Get();
    float paper = s.hdr_paper ? float(s.hdr_paper) : display_paper_ > 0 ? display_paper_ : 200.0f;
    float peak = s.hdr_peak ? float(s.hdr_peak) : display_peak_ > 0 ? display_peak_ : 1000.0f;
    paper = std::clamp(paper, 80.0f, 600.0f);
    return {paper, std::clamp(peak, paper, 10000.0f)};
}

// the frame at its shape (FXAA, sharpening, the gamma ramp, as on an SDR screen) and the overlay, premultiplied over
// nothing, each at the screen's size
void VulkanBackend::HdrCompose(Target* t) {
    for (Image* im : {&hdr_sdr_, &hdr_ui_})
        if (im->w != bb_w_ || im->h != bb_h_) {
            DropImage(*im);
            MakeImage(*im, VK_IMAGE_TYPE_2D, im == &hdr_ui_ ? ui_fmt_ : kBgra, bb_w_, bb_h_, 1, 1, 1,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        }
    if (!hdr_sdr_ || !hdr_ui_) return;
    VkClearAttachment ca{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
    VkClearRect cr{{{0, 0}, {bb_w_, bb_h_}}, 0, 1};
    Begin({hdr_sdr_.att, VK_NULL_HANDLE, bb_w_, bb_h_});
    ca.clearValue.color = {{0, 0, 0, 1}};
    vkCmdClearAttachments(cmd_, 1, &ca, 1, &cr);
    if (t) BlitFrame(*t, hdr_sdr_.att, kBgra);
    Begin({hdr_ui_.att, VK_NULL_HANDLE, bb_w_, bb_h_});
    ca.clearValue.color = {{0, 0, 0, 0}};
    vkCmdClearAttachments(cmd_, 1, &ca, 1, &cr);
    if (ui_) {
        VkViewport vp{0, 0, float(bb_w_), float(bb_h_), 0, 1};
        vkCmdSetViewport(cmd_, 0, 1, &vp);
        RenderUi();
    }
    End();
}

// the two put together on the swap chain image (its rendering instance already open: Present)
void VulkanBackend::HdrOut(Target* t, VkImageView view) {
    open_ = true;
    cur_ = {view, VK_NULL_HANDLE, bb_w_, bb_h_};
    uint64_t vh, ph;
    VkPipeline pipe = hdr_sdr_ && hdr_ui_ && Compile(kHdrHlsl, "vs", "vs_5_0", vh) && Compile(kHdrHlsl, "ps", "ps_5_0", ph)
                          ? PassPipeline({vh, ph, swap_fmt_, VK_FORMAT_UNDEFINED, 0, 0})
                          : VK_NULL_HANDLE;
    if (!pipe) {
        LOG_FIRST(1, "vulkan: the HDR pass did not compile or has no images; the frame is drawn as SDR's");
        if (t) BlitFrame(*t, view, swap_fmt_);
        return;
    }
    const float aspect = app::FrameAspect(), w = float(bb_w_), h = float(bb_h_);
    const float fw = std::min(w, h * aspect), fh = fw / aspect;
    const HdrNits n = HdrLevels();
    const bool scene = swap_hdr_ && t && t->hdr_frame == presents_ + 1 && t->hdr_scene.w == t->w && t->hdr_scene.h == t->h;
    const float c[12] = {(w - fw) / 2, (h - fh) / 2, fw, fh,
                         swap_hdr_ ? 1.0f : 0.0f, n.paper, n.peak, scene ? 1.0f : 0.0f,
                         t ? float(t->w) : 1.0f, t ? float(t->h) : 1.0f, 0.45f, HdrTest() ? 1.0f : 0.0f};
    VkViewport vp{0, 0, w, h, 0, 1};
    VkRect2D sc{{0, 0}, {bb_w_, bb_h_}};
    VkImageView views[4] = {hdr_sdr_.view, hdr_ui_.view, scene ? t->res.view : VK_NULL_HANDLE,
                            scene ? t->hdr_scene.view : VK_NULL_HANDLE};
    FullScreen(pipe, cur_, vp, sc, c, 12, views, 4);
    static bool told = false;
    if (swap_hdr_ && !told) told = true, LOGR("vulkan: HDR10 out, SDR white %.0f nits, peak %.0f", n.paper, n.peak);
}

void VulkanBackend::RenderUi() {
    std::lock_guard lock(app::UiLock());
    app::FeedPad();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    app::DrawHud(fps_);
    app::DrawOverlay();
    if (app::kGame.ui) app::kGame.ui(fps_);
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_);
    ForgetDrawState();
}

void VulkanBackend::ReleaseOldTextures() {
    constexpr uint64_t kSweepEvery = 60;
    constexpr size_t kTexturesKept = 2048;
    constexpr uint64_t kIdleFrames = 600;
    if (presents_ % kSweepEvery != 0 || textures_.size() <= kTexturesKept) return;
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->second.frame + kIdleFrames < presents_) {
            DropImage(it->second.img);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

void VulkanBackend::CountFps() {
    fps_n_++;
    auto now = std::chrono::steady_clock::now();
    double el = std::chrono::duration<double>(now - fps_t_).count();
    if (el >= 0.5) {
        fps_ = fps_n_ / el;
        fps_n_ = 0;
        fps_t_ = now;
    }
}

// the next swap chain image, the chain made again when the window changed size or the vsync setting changed
bool VulkanBackend::Acquire(uint32_t& index) {
    RECT rc;
    GetClientRect(static_cast<HWND>(app::Window()), &rc);
    uint32_t w = std::max<LONG>(rc.right - rc.left, 1), h = std::max<LONG>(rc.bottom - rc.top, 1);
    if (!swap_ || w != bb_w_ || h != bb_h_ || swap_vsync_ != app::Get().vsync || swap_hdr_asked_ != app::Get().hdr) {
        if (!CreateSwapchain()) return false;
        if (ui_) ImGui_ImplVulkan_SetMinImageCount(2);
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        VkResult r = vkAcquireNextImageKHR(dev_, swap_, UINT64_MAX, acquire_sem_[frame_slot_], VK_NULL_HANDLE, &index);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) return true;
        if (r != VK_ERROR_OUT_OF_DATE_KHR) {
            LOG_FIRST(8, "vulkan: vkAcquireNextImageKHR failed: %s", VkResultName(r));
            return false;
        }
        if (!CreateSwapchain()) return false;
    }
    return false;
}

void VulkanBackend::Present(const Surface& front) {
    PerfScope ps{perf_present_};
    Target* t = EnsureTarget(front, false);
    if (t && LatchCpuWrite(*t)) UploadTarget(*t, front);
    if (ramp_dirty_) UploadGammaRamp();
    if (t && (ScenePassWanted() || swap_hdr_) && !post_done_ && scene_rt_ == t) ScenePass(*t, scene_ds_);
    // XBE_PAD_CMD's diagnostic: a file named shot beside the exe holding a PNG path gets the window's picture, as
    // shown (the settings window too), there
    static const bool shots = [] {
        const char* e = std::getenv("XBE_PAD_CMD");
        return e && *e && *e != '0';
    }();
    std::wstring shot_path;
    if (shots && presents_ % 15 == 0) {
        std::filesystem::path req = app::ExeDir() / (app::Instance() > 1 ? "shot" + std::to_string(app::Instance()) : "shot");
        std::error_code ec;
        if (std::filesystem::exists(req, ec)) {
            if (std::wifstream in{req}) std::getline(in, shot_path);
            std::filesystem::remove(req, ec);
        }
    }
    scene_rt_ = scene_ds_ = nullptr;
    post_done_ = false;
    scene_zwrites_ = 0;
    // ray tracing: the next frame captures from its first vertex, into buffers twice as large after one ran out
    if (rt_full_ && rt_want_ < (16u << 20)) {
        rt_want_ *= 2;
        LOGR("vulkan: ray tracing's scene outgrew its buffers, now %u vertices", rt_want_);
    }
    rt_full_ = false;
    rt_used_ = 0;
    rt_tris_.clear(), rt_cut_.clear(), rt_mats_.clear(), rt_cut_mats_.clear(), rt_gun_.clear(), rt_gun_mats_.clear();
    rt_views_.clear(), rt_view_slot_.clear();
    rt_cams_.clear();
    if (uint64_t all = rt_fog_tris_[0] + rt_fog_tris_[1]; all > 500) rt_fogged_ = float(rt_fog_tris_[1]) / float(all);
    rt_fog_tris_[0] = rt_fog_tris_[1] = 0;
    rt_drawn_ = false;
    rt_reflected_ = false;
    rt_built_ = false;
    uint32_t index = 0;
    bool acquired;
    {
        PerfScope pa{perf_acquire_};
        acquired = Acquire(index);
    }
    // HDR (or an overlay format the chain lacks): the frame and the overlay drawn apart first, then put together
    const bool compose = swap_hdr_ || (ui_ && ui_fmt_ != swap_fmt_);
    if (acquired && compose) HdrCompose(t);
    if (acquired) {
        End();
        VkCommandBuffer cb = Cmd();
        if (dirty_) Barrier();
        VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        ib.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = swap_images_[index];
        ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &ib;
        vkCmdPipelineBarrier2(cb, &di);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = swap_views_[index];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0, 0, 0, 1}};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {bb_w_, bb_h_}};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        vkCmdBeginRendering(cb, &ri);
        if (compose) {
            HdrOut(t, swap_views_[index]);
        } else {
            if (t) BlitFrame(*t, swap_views_[index], swap_fmt_);
            VkViewport vp{0, 0, float(bb_w_), float(bb_h_), 0, 1};
            vkCmdSetViewport(cb, 0, 1, &vp);
            if (ui_) RenderUi();
        }
        vkCmdEndRendering(cb);
        open_ = false;
        cur_ = {};
        Stamp(1);
        bool shot = !shot_path.empty();
        VkDeviceSize shot_bytes = VkDeviceSize(bb_w_) * bb_h_ * 4;
        if (shot && readback_.size < shot_bytes) {
            DropBuffer(readback_);
            shot = MakeBuffer(readback_, shot_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        }
        ib.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        if (shot) {
            ib.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            ib.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            vkCmdPipelineBarrier2(cb, &di);
            VkBufferImageCopy r{};
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            r.imageExtent = {bb_w_, bb_h_, 1};
            vkCmdCopyImageToBuffer(cb, swap_images_[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buf, 1, &r);
            VkMemoryBarrier2 hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            hb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            hb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            hb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            hb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo hd{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            hd.memoryBarrierCount = 1;
            hd.pMemoryBarriers = &hb;
            vkCmdPipelineBarrier2(cb, &hd);
            ib.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            ib.srcAccessMask = 0;
            ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        } else {
            ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        ib.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        ib.dstAccessMask = 0;
        ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(cb, &di);
        Submit(acquire_sem_[frame_slot_], present_sem_[index]);
        if (shot) {
            Wait(serial_);
            std::vector<uint32_t> px(size_t(bb_w_) * bb_h_);
            std::memcpy(px.data(), readback_.mem.map, px.size() * 4);
            if (swap_fmt_ == VK_FORMAT_R8G8B8A8_UNORM)
                for (uint32_t& p : px) p = (p & 0xFF00FF00u) | (p >> 16 & 0xFF) | (p & 0xFF) << 16;
            if (swap_hdr_) HdrToSdr(px, swap_fmt_ == VK_FORMAT_A2B10G10R10_UNORM_PACK32, HdrLevels().paper);
            for (uint32_t& p : px) p |= 0xFF000000u;
            LOGR("vulkan: shot %ls %s", shot_path.c_str(), WritePngFile(shot_path, px, bb_w_, bb_h_) ? "written" : "failed");
        }
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &present_sem_[index];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap_;
        pi.pImageIndices = &index;
        VkResult r = vkQueuePresentKHR(queue_, &pi);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR && r != VK_ERROR_OUT_OF_DATE_KHR)
            LOG_FIRST(8, "vulkan: vkQueuePresentKHR failed: %s", VkResultName(r));
        frame_serial_[frame_slot_] = serial_;
        frame_slot_ = (frame_slot_ + 1) % kFramesInFlight;
    } else {
        Submit();
        frame_serial_[frame_slot_] = serial_;  // the slot is not advanced: its stamps and the throttle are this frame's
    }
    // at most kFramesInFlight frames ahead of the GPU; the slot's timings read before it is used again
    Wait(frame_serial_[frame_slot_]);
    ReadStamps(frame_slot_);
    frame_stamp_due_ = true;
    SaveStyleIfDue();
    Unbind();
    DeliverReports(false);
    Collect();
    presents_++;
    presents_seen_.store(presents_);
    ReleaseOldTextures();
    CountFps();
    ApplySettings();
    if (presents_ == 600 || presents_ % 18000 == 0) SavePipelineCache();
    if (diag::Perf()) LogPerf();
}

const std::string& VulkanBackend::ScenePassSource() {
    auto now = std::chrono::steady_clock::now();
    if (now - mod_post_checked_ >= std::chrono::seconds(1)) {
        mod_post_checked_ = now;
        std::filesystem::path f = app::ExeDir() / "mods" / "shaders" / "scene_post.hlsl";
        std::error_code ec;
        auto t = std::filesystem::last_write_time(f, ec);
        if (ec) {
            if (!mod_post_.empty()) LOGR("vulkan: mods/shaders/scene_post.hlsl gone, the built-in scene pass is used");
            mod_post_.clear();
        } else if (t != mod_post_time_ || mod_post_.empty()) {
            mod_post_time_ = t;
            std::ifstream in(f, std::ios::binary);
            mod_post_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            scene_src_hash_ = Hash(mod_post_.data(), mod_post_.size());
            LOGR("vulkan: scene pass from mods/shaders/scene_post.hlsl (%zu bytes)", mod_post_.size());
        }
    }
    return mod_post_.empty() ? builtin_post_ : mod_post_;
}

// the scene, copied, drawn back over itself through the style's pass
void VulkanBackend::ScenePass(Target& rt, Target* ds) {
    post_done_ = true;
    End();
    Stamp(2);
    rt_built_ = RtWanted() && ds && ds->w == rt.w && ds->h == rt.h && RtBuild(float(rt.w) / float(rt.h));
    rt_gi_drawn_ = false;
    if (rt_built_ && Style().rt_lighting) RtRelightPass(rt, ds);
    if (rt_built_ && Style().rt_ao > 0) RtPass(rt, ds);
    if (rt_built_) RtReflectPass(rt, ds);
    if (Style().effect_lights && Style().mode != kStyleRetro) LightPass(rt, ds);
    if (Style().bounce_light && Style().mode != kStyleRetro && !rt_gi_drawn_) BouncePass(rt, ds);
    if (Style().atmosphere && Style().mode != kStyleRetro) AtmospherePass(rt, ds);
    if (StylePassWanted()) StylePass(rt, ds);
    if (app::Get().smaa && Style().mode != kStyleRetro) SmaaPass(rt);
    if (DofWanted()) DofPass(rt, ds);
    End();
    // HDR: the finished scene before the HUD, which HdrOut compares the presented frame with
    if (swap_hdr_ && (rt.hdr_scene.w == rt.w && rt.hdr_scene.h == rt.h ||
                      (DropImage(rt.hdr_scene), MakeImage(rt.hdr_scene, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)))) {
        if (dirty_) Barrier();
        VkImageCopy cp{};
        cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.extent = {rt.w, rt.h, 1};
        vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.hdr_scene.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
        dirty_ = true;
        rt.hdr_frame = presents_ + 1;
    }
    Stamp(3);
    ForgetDrawState();  // the draw that asked for it goes on with its targets
}

void VulkanBackend::StylePass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    uint64_t vh, ph;
    const std::string& src = ScenePassSource();
    bool ok = Compile(src, "vs", "vs_5_0", vh) && Compile(src, "ps", "ps_5_0", ph);
    if (!ok && &src != &builtin_post_) {
        // a mod's shader that does not compile: the built-in one, until the file changes again
        static uint64_t told = 0;
        if (told != scene_src_hash_) LOGR("vulkan: mods/shaders/scene_post.hlsl did not compile (the log above says why), "
                                          "the built-in scene pass is used"), told = scene_src_hash_;
        ok = Compile(builtin_post_, "vs", "vs_5_0", vh) && Compile(builtin_post_, "ps", "ps_5_0", ph);
    }
    if (!ok) {
        LOG_FIRST(1, "vulkan: the style's scene pass did not compile; the scene is drawn as the game's");
        return;
    }
    VkPipeline pipe = PassPipeline({vh, ph, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pipe) return;
    if (!rt.fb && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    bool depth = ds && ds->w == rt.w && ds->h == rt.h;
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    float k = float(rt.h) / float(rt.gh);  // host pixels per console pixel
    float levels = float((1u << st.color_bits) - 1);
    // Original with quality's AO or bloom: Enhanced's path with its grading left as the game's
    GraphicsStyle g = st;
    if (g.mode == kStyleOriginal) {
        GraphicsStyle neutral;
        g.mode = kStyleEnhanced;
        g.exposure = neutral.exposure, g.tonemap = kToneOff, g.contrast = 1, g.saturation = 1, g.temperature = 0;
        g.sharpen = 0, g.vignette = 0, g.grain = 0, g.bloom_threshold = 0.85f;
    }
    float ao = rt_drawn_ ? 0.0f : QualityAo(st),
          bloom = st.mode == kStyleOriginal || st.mode == kStyleEnhanced ? QualityBloom(st) : 0.0f;
    if (st.mode == kStyleOriginal && st.quality_bloom) g.bloom_threshold = 0.85f;
    float c[44] = {float(g.mode), 1.0f / rt.w, 1.0f / rt.h, float(presents_ % 3600) / 60.0f,
                   g.exposure, float(g.tonemap), g.contrast, g.saturation,
                   g.temperature, g.sharpen, bloom, g.bloom_threshold,
                   ao, g.ao_radius, g.vignette, g.grain,
                   float(st.bands), st.outline * k, st.internal_edges ? 1.0f : 0.0f, st.edge_threshold,
                   st.outline_color[0], st.outline_color[1], st.outline_color[2], st.halftone,
                   float(st.pixel) * k, levels, st.dither ? 1.0f : 0.0f, st.retro_tint,
                   depth ? 1.0f : 0.0f, float(rt.w), float(rt.h), k};
    // reflections, when a reflective draw made this frame's mask; an assumed 60 degree vertical view
    bool mask = depth && rt.mask.img && rt.mask_frame == presents_ + 1 && st.reflections != kReflectOff &&
                st.mode != kStyleRetro && !rt_reflected_;
    c[32] = mask ? st.reflection_strength : 0.0f;
    c[33] = st.reflections == kReflectHigh ? 48.0f : 20.0f;
    c[34] = 0.5774f;
    c[35] = float(rt.w) / float(rt.h);
    c[36] = 0.04f;
    c[37] = 3.0f;
    static const int show_mask = [] {
        const char* e = std::getenv("XBE_SSR_TEST");
        return e && *e >= '2' && *e <= '4' ? *e - '1' : 0;
    }();
    c[38] = float(show_mask);
    c[39] = scene_wbuf_ ? 1.0f : 0.0f;
    // the scene's depth was cleared mid-frame and the world's kept (RtKeepDepth): what it holds now is the gun alone
    c[40] = depth && ds->rt_depth.img && ds->rt_depth_frame == presents_ + 1 ? 1.0f : 0.0f;
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    VkImageView views[4] = {rt.fb.view, depth ? ds->res.view : VK_NULL_HANDLE, mask ? rt.mask.view : VK_NULL_HANDLE,
                            mask ? rt.mask_ds.view : VK_NULL_HANDLE};
    for (VkImageView& v : views)
        if (!v && !null_descriptor_) v = dummy_.view;
    FullScreen(pipe, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, 44, views, 4);
    End();
}

// bounced light over the scene (kBounceHlsl): the light about each pixel gathered at a quarter size, then the scene lit
void VulkanBackend::BouncePass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    if (!ds || ds->w != rt.w || ds->h != rt.h) return;
    uint64_t vh, gh, ch;
    if (!Compile(kBounceHlsl, "vs", "vs_5_0", vh) || !Compile(kBounceHlsl, "gather", "ps_5_0", gh) ||
        !Compile(kBounceHlsl, "comp", "ps_5_0", ch)) {
        LOG_FIRST(1, "vulkan: the bounced light's passes did not compile; no bounced light");
        return;
    }
    constexpr VkFormat kBounceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkPipeline pg = PassPipeline({vh, gh, kBounceFormat, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pc = PassPipeline({vh, ch, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pg || !pc) return;
    uint32_t gw = std::max<uint32_t>(rt.w / 4, 1), gh2 = std::max<uint32_t>(rt.h / 4, 1);
    if (rt.bounce.img && (rt.bounce.w != gw || rt.bounce.h != gh2)) DropImage(rt.bounce);
    if (!rt.bounce.img && !MakeImage(rt.bounce, VK_IMAGE_TYPE_2D, kBounceFormat, gw, gh2, 1, 1, 1,
                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return;
    if (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    static const int test = [] {
        const char* e = std::getenv("XBE_GI_TEST");
        return e && *e >= '1' && *e <= '2' ? *e - '0' : 0;
    }();
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    Barrier();
    bool kept = ds->rt_depth.img && ds->rt_depth_frame == presents_ + 1;
    VkImageView keptv = kept ? ds->rt_depth.view : ds->res.view;
    float c[8] = {1.0f / rt.w, 1.0f / rt.h, st.bounce_strength, scene_wbuf_ ? 1.0f : 0.0f,
                  float(rt.w) / float(rt.h), float(presents_ % 3600) / 60.0f, float(test), kept ? 1.0f : 0.0f};
    VkImageView gv[4] = {rt.fb.view, ds->res.view, dummy_.view, keptv};
    VkViewport gvp{0, 0, float(gw), float(gh2), 0, 1};
    VkRect2D gsc{{0, 0}, {gw, gh2}};
    FullScreen(pg, {rt.bounce.att, VK_NULL_HANDLE, gw, gh2}, gvp, gsc, c, 8, gv, 4);
    End();
    Barrier();
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    VkImageView views[4] = {rt.fb.view, ds->res.view, rt.bounce.view, keptv};
    FullScreen(pc, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, 8, views, 4);
    End();
    static bool said = false;
    if (!said) said = true, LOGR("vulkan: bounced light drawn (strength %.2f)", st.bounce_strength);
}

// depth of field over the finished scene (kDofHlsl): the distance in focus eased in a 1 x 1 image, then the blur
void VulkanBackend::DofPass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    if (!ds || ds->w != rt.w || ds->h != rt.h) return;
    uint64_t vh, fh, bh;
    if (!Compile(kDofHlsl, "vs", "vs_5_0", vh) || !Compile(kDofHlsl, "focus", "ps_5_0", fh) ||
        !Compile(kDofHlsl, "blur", "ps_5_0", bh)) {
        LOG_FIRST(1, "vulkan: the depth of field's passes did not compile; no depth of field");
        return;
    }
    constexpr VkFormat kFocusFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkPipeline pf = PassPipeline({vh, fh, kFocusFormat, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pb = PassPipeline({vh, bh, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pf || !pb) return;
    for (Image& im : rt.dof_focus)
        if (!im.img && !MakeImage(im, VK_IMAGE_TYPE_2D, kFocusFormat, 1, 1, 1, 1, 1,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            return;
    if (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    static const int test = [] {
        const char* e = std::getenv("XBE_DOF_TEST");
        return e && *e >= '1' && *e <= '2' ? *e - '0' : 0;
    }();
    // aiming down the sights: the focus's blur eased in and out with the gun
    dof_aim_ += ((Aiming() ? 1.0f : 0.0f) - dof_aim_) * 0.15f;
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    Barrier();
    bool kept = ds->rt_depth.img && ds->rt_depth_frame == presents_ + 1;
    bool last = rt.dof_frame == presents_;  // drawn the frame before: its focus eases on
    rt.dof_frame = presents_ + 1;
    Image& now = rt.dof_focus[presents_ & 1];
    const Image& was = rt.dof_focus[(presents_ + 1) & 1];
    bool focus = st.dof == kDofFocus;
    float most = st.dof_strength * float(rt.h) * (focus ? 0.006f + 0.006f * dof_aim_ : 0.005f);
    float c[16] = {1.0f / rt.w, 1.0f / rt.h, float(st.dof), most,
                   20.0f, 120.0f, 0.15f - 0.10f * dof_aim_, 1.2f + 0.3f * dof_aim_,
                   scene_wbuf_ ? 1.0f : 0.0f, kept ? 1.0f : 0.0f, last ? 1.0f : 0.0f, 0.12f,
                   float(test), 0, 0, 0};
    VkImageView keptv = kept ? ds->rt_depth.view : ds->res.view;
    VkViewport fvp{0, 0, 1, 1, 0, 1};
    VkRect2D fsc{{0, 0}, {1, 1}};
    VkImageView fv[4] = {rt.fb.view, ds->res.view, keptv, last ? was.view : dummy_.view};
    FullScreen(pf, {now.att, VK_NULL_HANDLE, 1, 1}, fvp, fsc, c, 16, fv, 4);
    End();
    Barrier();
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    VkImageView views[4] = {rt.fb.view, ds->res.view, keptv, now.view};
    FullScreen(pb, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, 16, views, 4);
    End();
    static bool said = false;
    if (!said) said = true, LOGR("vulkan: depth of field drawn (%s, blur %.2f, %s)", focus ? "focus" : "distance",
                                 st.dof_strength, kept ? "the gun kept sharp" : "no mid-frame clear seen");
}

// light shafts and fog over the scene: what lights the air at a quarter size, halved four times to a field that
// points to the light, then the scene marched toward it and fogged (kAtmoHlsl)
void VulkanBackend::AtmospherePass(Target& rt, Target* ds) {
    const GraphicsStyle& st = Style();
    if (!ds || ds->w != rt.w || ds->h != rt.h) return;
    uint64_t vh, sh, dh, ch, kh;
    if (!Compile(kAtmoHlsl, "vs", "vs_5_0", vh) || !Compile(kAtmoHlsl, "src", "ps_5_0", sh) ||
        !Compile(kAtmoHlsl, "down", "ps_5_0", dh) || !Compile(kAtmoHlsl, "comp", "ps_5_0", ch) ||
        !Compile(kAtmoHlsl, "pick", "ps_5_0", kh)) {
        LOG_FIRST(1, "vulkan: the light shafts' passes did not compile; no shafts or fog");
        return;
    }
    constexpr VkFormat kAtmoFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkPipeline ps = PassPipeline({vh, sh, kAtmoFormat, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pd = PassPipeline({vh, dh, kAtmoFormat, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pc = PassPipeline({vh, ch, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pk = PassPipeline({vh, kh, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_UNDEFINED, 0, 0});
    if (!ps || !pd || !pc || !pk) return;
    if (!rt.atmo_pick.img && !MakeImage(rt.atmo_pick, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, 5, 1, 1, 1, 1,
                                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return;
    const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for (uint32_t k = 0; k < 5; k++) {
        uint32_t w = std::max<uint32_t>(rt.w >> (2 + k), 1), h = std::max<uint32_t>(rt.h >> (2 + k), 1);
        Image& im = rt.atmo[k];
        if (im.img && (im.w != w || im.h != h)) DropImage(im);
        if (!im.img && !MakeImage(im, VK_IMAGE_TYPE_2D, kAtmoFormat, w, h, 1, 1, 1, u)) return;
    }
    if (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    Barrier();
    const Image& field = rt.atmo[4];
    static const int test = [] {
        const char* e = std::getenv("XBE_ATMO_TEST");
        return e && *e >= '1' && *e <= '3' ? *e - '0' : 0;
    }();
    float c[16] = {1.0f / rt.w, 1.0f / rt.h, st.shaft_strength, st.fog_strength,
                   scene_wbuf_ ? 1.0f : 0.0f, float(presents_ % 3600) / 60.0f, 1.0f / field.w, 1.0f / field.h,
                   0.006f, 0.35f, 0.55f, float(rt.w) / float(rt.h), float(test), 0, 0, 0};
    auto pass = [&](VkPipeline p, const Image& dst, const VkImageView* views, uint32_t n, float pxw, float pxh) {
        float cc[16];
        std::memcpy(cc, c, sizeof cc);
        cc[0] = pxw, cc[1] = pxh;
        VkViewport vp{0, 0, float(dst.w), float(dst.h), 0, 1};
        VkRect2D sc{{0, 0}, {dst.w, dst.h}};
        FullScreen(p, {dst.att, VK_NULL_HANDLE, dst.w, dst.h}, vp, sc, cc, 16, views, n);
        End();
        Barrier();
    };
    VkImageView sv[2] = {rt.fb.view, ds->res.view};
    pass(ps, rt.atmo[0], sv, 2, 1.0f / rt.w, 1.0f / rt.h);
    for (int k = 1; k < 5; k++) pass(pd, rt.atmo[k], &rt.atmo[k - 1].view, 1, 1.0f / rt.atmo[k - 1].w, 1.0f / rt.atmo[k - 1].h);
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    VkImageView pv[3] = {field.view, ds->res.view, rt.fb.view};
    pass(pk, rt.atmo_pick, pv, 3, 1.0f / field.w, 1.0f / field.h);
    VkImageView views[4] = {rt.fb.view, ds->res.view, rt.atmo[0].view, rt.atmo_pick.view};
    FullScreen(pc, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, c, 16, views, 4);
    End();
    static bool said = false;
    if (!said) said = true, LOGR("vulkan: light shafts and fog drawn (shafts %.2f, fog %.2f)", st.shaft_strength, st.fog_strength);
    static int wbuf_said = -1;
    if (wbuf_said != int(scene_wbuf_)) wbuf_said = scene_wbuf_, LOGR("vulkan: fog over a %s-buffered scene", scene_wbuf_ ? "W" : "Z");
}

// SMAA's shaders and lookup textures, once
bool VulkanBackend::EnsureSmaa() {
    if (smaa_tried_) return smaa_ok_;
    smaa_tried_ = true;
    const std::string& src = SmaaHlsl();
    smaa_ok_ = Compile(src, "vs", "vs_5_0", smaa_h_[0]) && Compile(src, "edges", "ps_5_0", smaa_h_[1]) &&
               Compile(src, "weights", "ps_5_0", smaa_h_[2]) && Compile(src, "blend", "ps_5_0", smaa_h_[3]);
    uint32_t aw, ah, ap, sw, sh, sp;
    const uint8_t* area = SmaaAreaTex(aw, ah, ap);
    const uint8_t* search = SmaaSearchTex(sw, sh, sp);
    const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (smaa_ok_) {
        smaa_ok_ = MakeImage(smaa_area_, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8_UNORM, aw, ah, 1, 1, 1, u) &&
                   MakeImage(smaa_search_, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, sw, sh, 1, 1, 1, u);
    }
    if (smaa_ok_) {
        UploadLevel(smaa_area_, 0, 0, aw, ah, 1, area, size_t(ap) * ah);
        UploadLevel(smaa_search_, 0, 0, sw, sh, 1, search, size_t(sp) * sh);
    }
    LOGR("vulkan: SMAA %s", smaa_ok_ ? "ready" : "could not be made (the log above says why); the scene is drawn without it");
    return smaa_ok_;
}

// SMAA 1x over the scene target: its edges by luma, the blending weights along them, the scene blended by them
void VulkanBackend::SmaaPass(Target& rt) {
    if (!EnsureSmaa()) return;
    const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for (Image* im : {&rt.smaa_edges, &rt.smaa_weights})
        if (im->img && (im->w != rt.w || im->h != rt.h)) DropImage(*im);
    if ((!rt.smaa_edges.img && !MakeImage(rt.smaa_edges, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1, u)) ||
        (!rt.smaa_weights.img && !MakeImage(rt.smaa_weights, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1, u)) ||
        (!rt.fb.img && !MakeImage(rt.fb, VK_IMAGE_TYPE_2D, kBgra, rt.w, rt.h, 1, 1, 1,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))) {
        LOG_FIRST(1, "vulkan: no memory for SMAA's images, the scene is drawn without it");
        return;
    }
    VkPipeline edges = PassPipeline({smaa_h_[0], smaa_h_[1], kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline weights = PassPipeline({smaa_h_[0], smaa_h_[2], kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline blend = PassPipeline({smaa_h_[0], smaa_h_[3], kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!edges || !weights || !blend) return;
    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {rt.w, rt.h, 1};
    vkCmdCopyImage(Cmd(), rt.res.img, VK_IMAGE_LAYOUT_GENERAL, rt.fb.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;
    float m[4] = {1.0f / rt.w, 1.0f / rt.h, float(rt.w), float(rt.h)};
    VkViewport vp{0, 0, float(rt.w), float(rt.h), 0, 1};
    VkRect2D sc{{0, 0}, {rt.w, rt.h}};
    Attachments ea{rt.smaa_edges.att, VK_NULL_HANDLE, rt.w, rt.h};
    Begin(ea);
    VkClearAttachment ca{};
    ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkClearRect cr{{{0, 0}, {rt.w, rt.h}}, 0, 1};
    vkCmdClearAttachments(cmd_, 1, &ca, 1, &cr);
    FullScreen(edges, ea, vp, sc, m, 4, &rt.fb.view, 1);
    End();
    VkImageView wv[3] = {rt.smaa_edges.view, smaa_area_.view, smaa_search_.view};
    FullScreen(weights, {rt.smaa_weights.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, m, 4, wv, 3);
    End();
    VkImageView bv[2] = {rt.fb.view, rt.smaa_weights.view};
    FullScreen(blend, {rt.res.att, VK_NULL_HANDLE, rt.w, rt.h}, vp, sc, m, 4, bv, 2);
    End();
}

// a timestamp of the frame slot being recorded: 0 frame start, 1 frame end, 2 scene pass start, 3 its end
void VulkanBackend::Stamp(uint32_t which) {
    if (!stamps_ || (stamp_written_[frame_slot_] >> which & 1)) return;
    vkCmdWriteTimestamp2(Cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, stamps_, frame_slot_ * 4 + which);
    stamp_written_[frame_slot_] |= 1u << which;
}

// a finished slot's timings into the perf counters, its queries reset for the next frame
void VulkanBackend::ReadStamps(uint32_t slot) {
    if (!stamps_) return;
    uint32_t w = stamp_written_[slot];
    uint64_t t[4] = {};
    for (uint32_t i = 0; i < 4; i++)
        if (w >> i & 1) vkGetQueryPoolResults(dev_, stamps_, slot * 4 + i, 1, 8, &t[i], 8, VK_QUERY_RESULT_64_BIT);
    double ns = props_.limits.timestampPeriod;
    if ((w & 3) == 3 && t[1] > t[0]) gpu_frame_ms_ += double(t[1] - t[0]) * ns / 1e6, gpu_frames_++;
    if ((w & 12) == 12 && t[3] > t[2]) gpu_post_ms_ += double(t[3] - t[2]) * ns / 1e6, gpu_posts_++;
    vkResetQueryPool(dev_, stamps_, slot * 4, 4);
    stamp_written_[slot] = 0;
}

void VulkanBackend::LogPerf() {
    perf_frames_++;
    auto now = std::chrono::steady_clock::now();
    double el = std::chrono::duration<double>(now - perf_t_).count();
    if (el < 1) return;
    LOGR("perf: vulkan %.1f frames a second, %.2f ms a frame, GPU waits %.2f ms a frame, %.0f draws and %.1f "
         "submissions a frame, %zu pipelines, %zu textures, %zu targets",
         perf_frames_ / el, el * 1000 / perf_frames_, perf_wait_ / perf_frames_, double(perf_draws_) / perf_frames_,
         double(perf_submits_) / perf_frames_, pipelines_.size(), textures_.size(), targets_.size());
    double n = double(perf_frames_);
    LOGR("perf: vulkan per frame: draw %.2f ms, present %.2f (acquire %.2f), sync %.2f, queue submits %.2f | GPU frame "
         "%.2f ms, scene pass %.3f ms (%u of %u frames), style %s",
         perf_draw_ / n, perf_present_ / n, perf_acquire_ / n, perf_sync_ / n, perf_submit_ / n,
         gpu_frames_ ? gpu_frame_ms_ / gpu_frames_ : 0.0, gpu_posts_ ? gpu_post_ms_ / gpu_posts_ : 0.0, gpu_posts_,
         gpu_frames_, Style().mode == kStyleOriginal ? "original" : Style().mode == kStyleEnhanced ? "enhanced"
                      : Style().mode == kStyleToon ? "toon" : Style().mode == kStyleNoir ? "noir" : "retro");
    LOGR("perf: vulkan draws a frame: %.0f 2D, %.0f vertex programs, %.0f fixed function (%.0f lit)",
         perf_2d_ / n, perf_prog_ / n, perf_fixed_ / n, perf_lit_ / n);
    LOGR("perf: vulkan soft particles a frame: %.1f, material draws %.1f, glowing draws %.1f", perf_soft_ / n,
         perf_mat_ / n, perf_glow_ / n);
    LOGR("perf: vulkan ground detail a frame: %.1f tagged, %.1f not opaque, %.1f no free stage, %.1f drawn",
         perf_det_[0] / n, perf_det_[1] / n, perf_det_[2] / n, perf_det_[3] / n);
    for (uint64_t& d : perf_det_) d = 0;
    perf_soft_ = perf_mat_ = perf_glow_ = 0;
    LOGR("perf: vulkan reflective draws a frame: %.1f (3D %.0f, stage 0 seen %.0f, rt+ds %.0f, scene size %.0f, opaque %.0f, W-buffer %.0f)",
         perf_reflect_ / n, g_ssr_diag[0] / n, g_ssr_diag[1] / n, g_ssr_diag[2] / n, g_ssr_diag[3] / n, g_ssr_diag[4] / n,
         g_ssr_diag[5] / n);
    for (uint64_t& v : g_ssr_diag) v = 0;
    perf_prog_ = perf_fixed_ = perf_lit_ = perf_2d_ = perf_reflect_ = 0;
    gpu_frame_ms_ = gpu_post_ms_ = 0;
    gpu_frames_ = gpu_posts_ = 0;
    perf_draw_ = perf_present_ = perf_acquire_ = perf_sync_ = perf_submit_ = 0;
    LOGR("perf: title thread per frame: recording %.2f ms, waiting %.2f ms (%.1f waits: read-back %.2f resource %.2f "
         "fence %.2f flush %.2f copy %.2f, next frame %.2f)",
         g_perf_capture_ns.exchange(0) / n / 1e6, g_perf_wait_ns.exchange(0) / n / 1e6, g_perf_waits.exchange(0) / n,
         g_perf_wait_kind_ns[0].exchange(0) / n / 1e6, g_perf_wait_kind_ns[1].exchange(0) / n / 1e6,
         g_perf_wait_kind_ns[2].exchange(0) / n / 1e6, g_perf_wait_kind_ns[3].exchange(0) / n / 1e6,
         g_perf_wait_kind_ns[4].exchange(0) / n / 1e6, g_perf_wait_kind_ns[5].exchange(0) / n / 1e6);
    logx::Flush();
    perf_t_ = now;
    perf_frames_ = perf_submits_ = perf_draws_ = 0;
    perf_wait_ = 0;
}

void VulkanBackend::ApplySettings() {
    if (app::Get().msaa) LOG_FIRST(1, "vulkan: MSAA is not drawn yet under Vulkan, the scene is drawn without it");
    float aspect = app::FrameAspect();
    if (std::fabs(aspect - aspect_) > 1e-4f) {
        aspect_ = aspect;
        RescaleTargets();
    }
    float want = app::RenderScale(guest_h_);
    static float pending = 0;
    static std::chrono::steady_clock::time_point since;
    auto now = std::chrono::steady_clock::now();
    if (std::fabs(want - scale_) <= 1e-4f) return;
    if (std::fabs(want - pending) > 1e-4f) {
        pending = want;
        since = now;
        return;
    }
    if (now - since < std::chrono::milliseconds(500)) return;
    scale_ = want;
    RescaleTargets();
}

// a new render scale or shape: colour targets keep their pictures, scaled into new ones, depth targets start again
void VulkanBackend::RescaleTargets() {
    Unbind();
    scene_rt_ = scene_ds_ = nullptr;
    for (auto it = targets_.begin(); it != targets_.end();) {
        Target& old = it->second;
        if (old.depth) {
            DropImage(old.res);
            DropImage(old.fb), DropScene(old);
            DropImage(old.shrunk);
            it = targets_.erase(it);
            continue;
        }
        Target t;
        t.gw = old.gw;
        t.gh = old.gh;
        if (!CreateTarget(t)) {
            DropImage(old.res);
            DropImage(old.fb), DropScene(old);
            DropImage(old.shrunk);
            it = targets_.erase(it);
            continue;
        }
        BlitWhole(old.res.view, t.res, {.alpha = true});
        DropImage(old.res);
        DropImage(old.fb), DropScene(old);
        DropImage(old.shrunk);
        t.data = old.data;
        t.bytes = old.bytes;
        t.epoch = old.epoch;
        t.sig = old.sig;
        t.drawn = old.drawn;
        t.linear32 = old.linear32;
        t.swizzled32 = old.swizzled32;
        t.read = std::move(old.read);
        old = std::move(t);
        ++it;
    }
    std::lock_guard lock(late_lock_);
    for (auto& [k, l] : late_)
        for (Buffer& b : l.staging) DropBuffer(b);
    late_.clear();
    LOGR("vulkan: render scale %.2f, %zu targets kept", scale_, targets_.size());
}

}  // namespace

std::unique_ptr<Backend> CreateVulkanBackend() { return std::make_unique<VulkanBackend>(); }

}  // namespace gpu
