// The title's Direct3D 8 calls drawn with Vulkan 1.3 (renderer = "vulkan" in the settings file). The console path of
// d3d11.cpp on Vulkan, with what does not depend on the API shared through d3d_common (its D3D12 enums translated
// here). The shaders are d3d_common's and shaders.cpp's HLSL, compiled to SPIR-V by DXC (dxcompiler.dll beside the
// exe). Vulkan is loaded at run time (vulkan-1.dll): without it Init fails and the game closes cleanly.
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
#include <wrl/client.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <dxcapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
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
#include "gpu/texture_pack.h"
#include "gpu/texture.h"
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
    X(vkGetPhysicalDeviceProperties2)            \
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
    X(vkCmdPushConstants)           \
    X(vkCmdCopyBufferToImage)       \
    X(vkCmdCopyImageToBuffer)       \
    X(vkCmdCopyImage)               \
    X(vkCmdClearAttachments)        \
    X(vkCmdBeginQuery)              \
    X(vkCmdEndQuery)

#define VK_RT_DEVICE_FUNCS(X)                  \
    X(vkGetBufferDeviceAddress)                 \
    X(vkCreateAccelerationStructureKHR)         \
    X(vkDestroyAccelerationStructureKHR)        \
    X(vkGetAccelerationStructureBuildSizesKHR)  \
    X(vkCmdBuildAccelerationStructuresKHR)       \
    X(vkGetAccelerationStructureDeviceAddressKHR)
#define VK_DECLARE(f) PFN_##f f = nullptr;
VK_GLOBAL_FUNCS(VK_DECLARE)
VK_INSTANCE_FUNCS(VK_DECLARE)
VK_DEBUG_FUNCS(VK_DECLARE)
VK_DEVICE_FUNCS(VK_DECLARE)
VK_RT_DEVICE_FUNCS(VK_DECLARE)
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
// set 0 has b0-b1, t0-t3, s0-s3, u0 capture storage and t9 ray-query scene
constexpr uint32_t kBindVsc = 0, kBindDc = 1, kBindTex = 2, kBindSmp = 6, kBindCapture = 10,
                   kBindTlas = 11, kBindings = 12;
constexpr uint32_t kRtCaptureCapacity = 1u << 20;
constexpr uint64_t kRtCaptureShaderSalt = 0xD1B54A32D192ED03ull;

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
               Mem& out, bool device_address = false) {
        int type = Type(r.memoryTypeBits, want | prefer);
        if (type < 0) type = Type(r.memoryTypeBits, want);
        if (type < 0) return false;
        bool host = (props_.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        if (r.size > kBlockSize / 4) return Dedicated(r.size, uint32_t(type), host, out, device_address);
        for (size_t b = 0; b < blocks_.size(); b++) {
            Block& bl = blocks_[b];
            if (bl.type != uint32_t(type) || bl.linear != linear || bl.addressable != device_address) continue;
            if (Carve(bl, r.size, r.alignment, out)) {
                out.block = int(b);
                return true;
            }
        }
        Block bl;
        bl.type = uint32_t(type);
        bl.linear = linear;
        bl.addressable = device_address;
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, device_address ? &flags : nullptr,
                                kBlockSize, uint32_t(type)};
        if (vkAllocateMemory(dev_, &ai, nullptr, &bl.mem) != VK_SUCCESS)
            return Dedicated(r.size, uint32_t(type), host, out, device_address);
        if (host) vkMapMemory(dev_, bl.mem, 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&bl.map));
        bl.free[0] = kBlockSize;
        blocks_.push_back(std::move(bl));
        if (!Carve(blocks_.back(), r.size, r.alignment, out)) return false;
        out.block = int(blocks_.size() - 1);
        return true;
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
        bool addressable = false;
        std::map<VkDeviceSize, VkDeviceSize> free;  // offset to size
    };
    int Type(uint32_t bits, VkMemoryPropertyFlags want) const {
        for (uint32_t i = 0; i < props_.memoryTypeCount; i++)
            if ((bits >> i & 1) && (props_.memoryTypes[i].propertyFlags & want) == want) return int(i);
        return -1;
    }
    bool Dedicated(VkDeviceSize size, uint32_t type, bool host, Mem& out, bool device_address) {
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, device_address ? &flags : nullptr, size, type};
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
    VkDeviceAddress address = 0;  // cached after binding for acceleration-structure inputs
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
};

struct MovieUp {
    Image up, out;
    uint64_t sig = 0, frame = 0;
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
    VkImageView depth = VK_NULL_HANDLE;  // matching scene depth, for depth-aware screen styles
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
    void ClearReport() override { counted_.clear(); }
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
    VkDeviceAddress BufferAddress(const Buffer& b) const { return b.address; }
    VkDeviceAddress QueryBufferAddress(const Buffer& b) const;
    void DropBuffer(Buffer& b);
    Stretch Stream(VkDeviceSize bytes, VkDeviceSize align = 16);
    VkSampler Sampler(const D3D12_SAMPLER_DESC& d);
    // Present's steps
    bool Acquire(uint32_t& index);
    void UploadGammaRamp();
    void BlitFrame(Target& t, VkImageView view, VkImageView source, VkImageView bloom);
    VkImageView Bloom(Target& t, VkImageView source);
    VkImageView LightShafts(Target& t, VkImageView source, VkImageView depth);
    VkImageView AmbientOcclusion(Target& t, VkImageView source, VkImageView depth);
    VkImageView DepthOfField(Target& t, VkImageView source, VkImageView depth);
    VkImageView MotionBlur(Target& t, VkImageView source, VkImageView depth);
    bool RememberCamera(const State& s);
    VkImageView TemporalAA(Target& t, VkImageView source, VkImageView depth);
    VkImageView AutoExposure(Target& t, VkImageView source);
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
    bool EnsureSmaa();
    VkImageView UpscaleMovie(Tex& tx, const TextureDesc& d);
    VkImageView SmaaFrame(Target& t);
    void FullScreen(VkPipeline p, const Attachments& a, const VkViewport& vp, const VkRect2D& sc, const float* cb,
                    uint32_t cb_floats, const VkImageView* views, uint32_t n_views,
                    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE);
    void Blit(VkImageView src, const Attachments& a, const VkViewport& vp, const VkRect2D& sc, const float uvrect[4],
              const BlitOptions& o, VkFormat fmt = kBgra, VkImageView bloom = VK_NULL_HANDLE);
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
    void PushDescriptors(const Stretch& vsc, const Stretch& dc, const DrawTextures& stages, const VkSampler smp[4],
                         const Buffer* capture = nullptr);
    void CaptureGeometry(const State& s, const Assembled& a, const DrawShaders& sh, const PsoKey& key,
                         const Stretch& vsc, const Stretch& dc, const DrawTextures& stages, const VkSampler smp[4]);
    bool BuildRtStructures();
    bool DrawGeometry(const Assembled& a);
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
    bool rt_ok_ = false;  // ray-query Vulkan prerequisites are available
    struct RtCaptureSlot {
        Buffer vertices;
        uint32_t used = 0;
        std::vector<uint32_t> triangles;
        Buffer indices, instance, blas_store, tlas_store, scratch;
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE, tlas = VK_NULL_HANDLE;
        VkDeviceSize blas_size = 0, tlas_size = 0, scratch_size = 0;
        uint32_t blas_primitives = 0;
        bool built = false;
        uint64_t frame = 0;
    };
    RtCaptureSlot rt_capture_[kFramesInFlight]{};
    VkDeviceSize rt_scratch_align_ = 0;
    bool null_descriptor_ = false;
    bool custom_border_ = false;
    bool mirror_once_ = false;
    bool host_reset_ = false;
    bool fill_lines_ = false;
    bool aniso_ok_ = false;
    bool open_ = false;  // a rendering instance is open
    Attachments cur_;
    bool dirty_ = false;  // writes since the last barrier
    // fixed objects
    VkSampler pass_samplers_[4] = {};  // linear, then point; all clamp
    Image lut_;
    Image dummy_;  // 1x1 white, for stages without a texture where null descriptors are missing
    Image smaa_area_, smaa_search_, smaa_copy_, smaa_edges_, smaa_weights_, smaa_result_;
    uint32_t smaa_w_ = 0, smaa_h_ = 0;
    bool smaa_tried_ = false, smaa_ready_ = false;
    uint64_t smaa_edge_vs_ = 0, smaa_edge_ps_ = 0, smaa_weight_vs_ = 0, smaa_weight_ps_ = 0;
    uint64_t smaa_blend_vs_ = 0, smaa_blend_ps_ = 0;
    Image ao_half_, ao_scene_;
    uint32_t ao_w_ = 0, ao_h_ = 0;
    Image dof_scene_;
    uint32_t dof_w_ = 0, dof_h_ = 0;
    Image motion_scene_;
    uint32_t motion_w_ = 0, motion_h_ = 0;
    Image bloom_down_[5], bloom_up_[4];
    Image shafts_field_[5], shafts_pick_, shafts_quarter_, shafts_scene_;
    uint32_t shafts_w_ = 0, shafts_h_ = 0;
    Image taa_history_[2];
    uint32_t taa_w_ = 0, taa_h_ = 0;
    uint64_t taa_frame_ = 0;
    Image exposure_meter_, exposure_history_[2], exposure_scene_;
    uint32_t exposure_w_ = 0, exposure_h_ = 0;
    uint64_t exposure_frame_ = 0;
    std::chrono::steady_clock::time_point exposure_time_{};
    float camera_view_[16]{}, camera_projection_[16]{}, previous_view_[16]{}, previous_projection_[16]{};
    uint64_t camera_frame_ = 0, previous_camera_frame_ = 0;
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
    std::unordered_map<uint32_t, MovieUp> movie_up_;
    std::unordered_map<uint64_t, Tex> textures_;          // by data address, a P8 texture's with its palette's above
    std::unordered_map<uint32_t, uint32_t> reloads_;      // targets reloaded from guest memory, for the log
    std::unordered_map<uint32_t, Late> late_;
    std::mutex late_lock_;                    // DeliverLate runs on the title's thread
    std::atomic<uint64_t> presents_seen_{0};  // presents_ for DeliverLate
    std::vector<uint32_t> free_queries_;
    std::vector<Query> counted_;  // since the last ClearReport
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
    VkImageView scene_depth_view_ = VK_NULL_HANDLE;
    uint32_t scene_depth_w_ = 0, scene_depth_h_ = 0, scene_color_data_ = 0;
    uint64_t scene_depth_frame_ = 0;
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
    // XBE_PERF: a line a second
    double perf_wait_ = 0;  // ms the render thread waited for the GPU
    double perf_draw_ = 0, perf_present_ = 0, perf_acquire_ = 0, perf_sync_ = 0, perf_submit_ = 0;  // ms in each
    struct PerfScope {
        double& acc;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~PerfScope() { acc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
    };
    uint64_t perf_frames_ = 0, perf_submits_ = 0, perf_draws_ = 0;
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
    bool rt_exts = HasExtension(ext, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
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
    fill_lines_ = f2.features.fillModeNonSolid;
    aniso_ok_ = f2.features.samplerAnisotropy;
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
    if (rt_ok_) {
#define VK_LOAD_RT(f) f = reinterpret_cast<PFN_##f>(vkGetDeviceProcAddr(dev_, #f));
        VK_RT_DEVICE_FUNCS(VK_LOAD_RT)
#undef VK_LOAD_RT
        rt_ok_ = vkGetBufferDeviceAddress && vkCreateAccelerationStructureKHR && vkDestroyAccelerationStructureKHR &&
                 vkGetAccelerationStructureBuildSizesKHR && vkCmdBuildAccelerationStructuresKHR &&
                 vkGetAccelerationStructureDeviceAddressKHR;
    }
#define VK_CHECK_LOADED(f)                                   \
    if (!f) {                                                \
        LOGE("vulkan: %s missing from the driver", #f);      \
        return false;                                        \
    }
    VK_DEVICE_FUNCS(VK_CHECK_LOADED)
#undef VK_CHECK_LOADED
    vkGetDeviceQueue(dev_, family_, 0, &queue_);
    alloc_.Init(pd_, dev_);
    // D24S8 where the GPU has it (it keeps the console's depth bias units), else D32 with stencil
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(pd_, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    depth_fmt_ = (fp.optimalTilingFeatures & need) == need ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
    LOGR("vulkan: depth %s, null descriptors %s, custom border colours %s, ray queries %s",
         depth_fmt_ == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8", null_descriptor_ ? "yes" : "no",
         custom_border_ ? "yes" : "no", rt_ok_ ? "available" : "unavailable");
    return true;
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

    // one layout for every draw and pass: two constants, four textures, four samplers, a capture buffer and a TLAS
    VkDescriptorSetLayoutBinding b[kBindings]{};
    const uint32_t binding_count = rt_ok_ ? kBindings : kBindings - 1;
    for (uint32_t i = 0; i < binding_count; i++) {
        b[i].binding = i;
        b[i].descriptorCount = 1;
        b[i].stageFlags = i == kBindCapture ? VK_SHADER_STAGE_VERTEX_BIT :
                          i == kBindTlas ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_ALL_GRAPHICS;
        b[i].descriptorType = i < kBindTex ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                              : i < kBindSmp ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                              : i < kBindCapture ? VK_DESCRIPTOR_TYPE_SAMPLER
                              : i == kBindTlas ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
                                               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = binding_count;
    dl.pBindings = b;
    if (vkCreateDescriptorSetLayout(dev_, &dl, nullptr, &set_layout_) != VK_SUCCESS) return false;
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};  // per-draw geometry-capture range
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &set_layout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(dev_, &pl, nullptr, &layout_) != VK_SUCCESS) return false;

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
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swap_fmt_;
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
    for (auto& [k, t] : targets_) DropImage(t.res), DropImage(t.fb), DropImage(t.shrunk);
    targets_.clear();
    for (auto& [k, t] : textures_) DropImage(t.img);
    textures_.clear();
    DropImage(ao_half_);
    DropImage(ao_scene_);
    DropImage(dof_scene_);
    DropImage(motion_scene_);
    for (Image& im : bloom_down_) DropImage(im);
    for (Image& im : bloom_up_) DropImage(im);
    for (Image& im : shafts_field_) DropImage(im);
    DropImage(shafts_pick_);
    DropImage(shafts_quarter_);
    DropImage(shafts_scene_);
    for (Image& im : taa_history_) DropImage(im);
    DropImage(exposure_meter_);
    for (Image& im : exposure_history_) DropImage(im);
    DropImage(exposure_scene_);
    for (auto& [k, m] : movie_up_) DropImage(m.up), DropImage(m.out);
    movie_up_.clear();
    DropImage(lut_);
    DropImage(dummy_);
    for (Image* im : {&smaa_area_, &smaa_search_, &smaa_copy_, &smaa_edges_, &smaa_weights_, &smaa_result_}) DropImage(*im);
    for (Chunk& c : chunks_) DropBuffer(c.b);
    DropBuffer(readback_);
    for (auto& [k, l] : late_)
        for (Buffer& b : l.staging) DropBuffer(b);
    for (RtCaptureSlot& slot : rt_capture_) {
        if (slot.blas) vkDestroyAccelerationStructureKHR(dev_, slot.blas, nullptr);
        if (slot.tlas) vkDestroyAccelerationStructureKHR(dev_, slot.tlas, nullptr);
        DropBuffer(slot.vertices);
        DropBuffer(slot.indices);
        DropBuffer(slot.instance);
        DropBuffer(slot.blas_store);
        DropBuffer(slot.tlas_store);
        DropBuffer(slot.scratch);
    }
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
    if (ui_pool_) vkDestroyDescriptorPool(dev_, ui_pool_, nullptr);
    vkDestroyPipelineCache(dev_, cache_, nullptr);
    vkDestroyPipelineLayout(dev_, layout_, nullptr);
    vkDestroyDescriptorSetLayout(dev_, set_layout_, nullptr);
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
    dirty_ = false;
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
    const bool device_address = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
    if (!alloc_.Alloc(req, want, prefer, true, b.mem, device_address) ||
        vkBindBufferMemory(dev_, b.buf, b.mem.mem, b.mem.off) != VK_SUCCESS) {
        vkDestroyBuffer(dev_, b.buf, nullptr);
        b.buf = VK_NULL_HANDLE;
        alloc_.Free(b.mem);
        return false;
    }
    b.size = size;
    if (device_address) {
        b.address = QueryBufferAddress(b);
        if (!b.address) {
            vkDestroyBuffer(dev_, b.buf, nullptr);
            alloc_.Free(b.mem);
            b = Buffer{};
            return false;
        }
    }
    return true;
}

VkDeviceAddress VulkanBackend::QueryBufferAddress(const Buffer& b) const {
    if (!rt_ok_ || !vkGetBufferDeviceAddress || !b.buf) return 0;
    VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, b.buf};
    return vkGetBufferDeviceAddress(dev_, &ai);
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
    if (scene_depth_view_ == it->second.res.view) {
        scene_depth_view_ = VK_NULL_HANDLE;
        scene_depth_w_ = scene_depth_h_ = scene_color_data_ = 0;
        scene_depth_frame_ = 0;
    }
    if (bound_rt_ == &it->second || bound_ds_ == &it->second) Unbind();
    DropImage(it->second.res);
    DropImage(it->second.fb);
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
    t.read = std::move(px);
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
        const bool sm65 = std::strstr(profile, "6_5") != nullptr;
        std::wstring target = sm65 ? std::wstring(profile, profile + std::strlen(profile))
                                   : std::wstring(1, wchar_t(profile[0])) + L"s_6_0";
        const wchar_t* spirv_env = sm65 ? L"-fspv-target-env=vulkan1.2" : L"-fspv-target-env=vulkan1.1";
        std::vector<const wchar_t*> args = {L"-E", wentry.c_str(), L"-T", target.c_str(), L"-spirv", L"-HV", L"2018",
                                            spirv_env, L"-fvk-use-dx-layout", L"-O1",
                                            L"-fvk-b-shift", L"0", L"0", L"-fvk-t-shift", L"2", L"0",
                                            L"-fvk-s-shift", L"6", L"0", L"-fvk-u-shift", L"10", L"0", L"-fspv-entrypoint-name=main"};
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
        keep.Attach(NewShaderBlob(code.size() * 4));
        if (keep.Get()) {
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
    pi.layout = layout_;
    VkPipeline p = VK_NULL_HANDLE;
    if (!st[0].module || !st[1].module) return pass_pipelines_[h] = VK_NULL_HANDLE;
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
    VkWriteDescriptorSet w[1 + 4 + 4 + 1]{};
    VkDescriptorBufferInfo bi{};
    VkDescriptorImageInfo ii[4]{}, si[4]{};
    VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
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
    if (tlas) {
        wa.accelerationStructureCount = 1;
        wa.pAccelerationStructures = &tlas;
        w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &wa, VK_NULL_HANDLE, kBindTlas, 0, 1,
                  VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr, nullptr};
    }
    vkCmdPushDescriptorSetKHR(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, n, w);
    vkCmdDraw(c, 3, 1, 0, 0);
    ForgetDrawState();
}

// src over the viewport and scissor given
void VulkanBackend::Blit(VkImageView src, const Attachments& a, const VkViewport& vp, const VkRect2D& sc,
                         const float uvrect[4], const BlitOptions& o, VkFormat fmt, VkImageView bloom_view) {
    uint64_t vh, ph;
    const app::Settings& st = app::Get();
    const bool fxaa = st.fxaa && !st.smaa;
    const bool final = o.final && (fxaa || st.sharpen || st.color_style || st.bloom || st.vignette || st.film_grain ||
                              st.scale_filter || st.exposure != 100 || st.contrast != 100 ||
                              st.saturation != 100 || st.temperature != 0);
    if (!Compile(kBlitHlsl, "vs", "vs_5_0", vh)) return;
    VkShaderModule ps = Compile(kBlitHlsl, final ? "psfinal" : "psmain", "ps_5_0", ph);
    bool use_final = final && ps;
    if (!ps) ps = Compile(kBlitHlsl, "psmain", "ps_5_0", ph);
    if (!ps) return;
    float c[8] = {uvrect[0], uvrect[1], uvrect[2], uvrect[3], o.gamma ? 1.0f : 0.0f, o.alpha ? 1.0f : 0.0f,
                  float(o.taps), 1.0f};
    if (use_final) {
        const uint32_t bloom = (std::min<uint32_t>(st.bloom, 100) * 63 + 50) / 100;
        const uint32_t vignette = (std::min<uint32_t>(st.vignette, 100) * 63 + 50) / 100;
        const uint32_t grain = (std::min<uint32_t>(st.film_grain, 100) * 63 + 50) / 100;
        uint32_t mode = (fxaa ? 1u : 0u) | (st.scale_filter ? 2u : 0u) | ((st.color_style & 7u) << 2) |
                              ((bloom & 63u) << 5) | ((vignette & 63u) << 11) | ((grain & 63u) << 17);
        if (o.depth) mode |= 1u << 23;
        c[5] = float(mode);
        c[4] = float((o.gamma ? 1u : 0u) | (bloom_view ? 2u : 0u));
        const uint32_t sharp = std::min<uint32_t>(st.sharpen, 100) / 5;
        const uint32_t contrast = (std::clamp<uint32_t>(st.contrast, 50, 150) - 50) / 5;
        const uint32_t saturation = std::min<uint32_t>(st.saturation, 200) / 5;
        const uint32_t temperature = uint32_t(std::clamp<int32_t>(st.temperature, -100, 100) + 100) / 5;
        c[6] = float((sharp & 31u) | ((contrast & 31u) << 5) | ((saturation & 63u) << 10) |
                     ((temperature & 63u) << 16));
        c[7] = float(std::clamp<uint32_t>(st.exposure, 50, 150)) + float(uint32_t(presents_ & 255u)) / 256.0f;
    }
    VkPipeline p = PassPipeline({vh, ph, fmt, VK_FORMAT_UNDEFINED, o.over ? 1u : 0u, 0});
    VkImageView views[4] = {src, lut_.view, bloom_view, o.depth};
    FullScreen(p, a, vp, sc, c, 8, views, 4);
}

// src over all of dst
void VulkanBackend::BlitWhole(VkImageView src, const Image& dst, const BlitOptions& o) {
    Attachments a{dst.att, VK_NULL_HANDLE, dst.w, dst.h};
    VkViewport vp{0, 0, float(dst.w), float(dst.h), 0, 1};
    VkRect2D sc{{0, 0}, {dst.w, dst.h}};
    float uv[4] = {0, 0, 1, 1};
    Blit(src, a, vp, sc, uv, o);
}

bool VulkanBackend::EnsureSmaa() {
    if (smaa_tried_) return smaa_ready_;
    smaa_tried_ = true;
    if (!Compile(kSmaaHlsl, "vsedge", "vs_5_0", smaa_edge_vs_) ||
        !Compile(kSmaaHlsl, "psedge", "ps_5_0", smaa_edge_ps_) ||
        !Compile(kSmaaHlsl, "vsweight", "vs_5_0", smaa_weight_vs_) ||
        !Compile(kSmaaHlsl, "psweight", "ps_5_0", smaa_weight_ps_) ||
        !Compile(kSmaaHlsl, "vsblend", "vs_5_0", smaa_blend_vs_) ||
        !Compile(kSmaaHlsl, "psblend", "ps_5_0", smaa_blend_ps_)) {
        LOGR("vulkan: SMAA shaders could not be compiled; drawing without SMAA");
        return false;
    }
    const VkImageUsageFlags sampled = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!MakeImage(smaa_area_, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8_UNORM, kSmaaAreaWidth, kSmaaAreaHeight, 1, 1, 1, sampled) ||
        !MakeImage(smaa_search_, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, kSmaaSearchWidth, kSmaaSearchHeight, 1, 1, 1, sampled)) {
        DropImage(smaa_area_);
        DropImage(smaa_search_);
        LOGR("vulkan: SMAA lookup images could not be made; drawing without SMAA");
        return false;
    }
    UploadLevel(smaa_area_, 0, 0, kSmaaAreaWidth, kSmaaAreaHeight, 1, kSmaaArea,
                size_t(kSmaaAreaWidth) * kSmaaAreaHeight * 2);
    UploadLevel(smaa_search_, 0, 0, kSmaaSearchWidth, kSmaaSearchHeight, 1, kSmaaSearch,
                size_t(kSmaaSearchWidth) * kSmaaSearchHeight);
    smaa_ready_ = true;
    LOGR("vulkan: SMAA 1x ready");
    return true;
}

VkImageView VulkanBackend::SmaaFrame(Target& t) {
    if (!EnsureSmaa()) return t.res.view;
    if (smaa_w_ != t.w || smaa_h_ != t.h) {
        for (Image* im : {&smaa_copy_, &smaa_edges_, &smaa_weights_, &smaa_result_}) DropImage(*im);
        const VkImageUsageFlags sampled = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const VkImageUsageFlags render = sampled | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (!MakeImage(smaa_copy_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1, sampled) ||
            !MakeImage(smaa_edges_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1, render) ||
            !MakeImage(smaa_weights_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1, render) ||
            !MakeImage(smaa_result_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1, render)) {
            for (Image* im : {&smaa_copy_, &smaa_edges_, &smaa_weights_, &smaa_result_}) DropImage(*im);
            smaa_w_ = smaa_h_ = 0;
            LOG_FIRST(8, "vulkan: no SMAA targets at %ux%u; drawing without SMAA", t.w, t.h);
            return t.res.view;
        }
        smaa_w_ = t.w;
        smaa_h_ = t.h;
    }
    const VkPipeline edges = PassPipeline({smaa_edge_vs_, smaa_edge_ps_, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    const VkPipeline weights = PassPipeline({smaa_weight_vs_, smaa_weight_ps_, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    const VkPipeline blend = PassPipeline({smaa_blend_vs_, smaa_blend_ps_, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!edges || !weights || !blend) return t.res.view;

    End();
    if (dirty_) Barrier();
    VkImageCopy cp{};
    cp.srcSubresource = cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {t.w, t.h, 1};
    vkCmdCopyImage(Cmd(), t.res.img, VK_IMAGE_LAYOUT_GENERAL, smaa_copy_.img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    dirty_ = true;

    const float constants[8] = {0, 0, 1, 1, 1.0f / t.w, 1.0f / t.h, float(t.w), float(t.h)};
    const VkViewport vp{0, 0, float(t.w), float(t.h), 0, 1};
    const VkRect2D sc{{0, 0}, {t.w, t.h}};
    VkImageView edge_src = smaa_copy_.view;
    FullScreen(edges, {smaa_edges_.att, VK_NULL_HANDLE, t.w, t.h}, vp, sc, constants, 8, &edge_src, 1);
    End();
    const VkImageView weight_src[3] = {smaa_edges_.view, smaa_area_.view, smaa_search_.view};
    FullScreen(weights, {smaa_weights_.att, VK_NULL_HANDLE, t.w, t.h}, vp, sc, constants, 8, weight_src, 3);
    End();
    const VkImageView blend_src[2] = {smaa_copy_.view, smaa_weights_.view};
    FullScreen(blend, {smaa_result_.att, VK_NULL_HANDLE, t.w, t.h}, vp, sc, constants, 8, blend_src, 2);
    End();
    return smaa_result_.view;
}
VkImageView VulkanBackend::AmbientOcclusion(Target& t, VkImageView source, VkImageView depth) {
    if (!source || !depth) return source;
    static const std::string hlsl = R"(
cbuffer P : register(b1) { float4 p[2]; };
Texture2D<float> depthtex : register(t1);
Texture2D scene : register(t0);
Texture2D ao_tex : register(t2);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o;
}
float z_at(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[1].x > 0.5 ? d * 4096.0 : rcp(max(1.0 - d, 1e-5));
}
float3 pos_at(float2 uv) {
    float z = z_at(uv);
    return float3((uv * 2 - 1) * float2(p[0].w, -1) * p[0].z * z, z);
}
float4 ao(O i) : SV_Target {
    float d = depthtex.SampleLevel(pt, i.uv, 0);
    if (d >= 0.99999) return float4(1, 1, 1, 1);
    float3 P = pos_at(i.uv);
    float3 dx = pos_at(i.uv + float2(p[0].x, 0)) - pos_at(i.uv - float2(p[0].x, 0));
    float3 dy = pos_at(i.uv + float2(0, p[0].y)) - pos_at(i.uv - float2(0, p[0].y));
    float3 N = normalize(cross(dx, dy));
    if (dot(N, P) > 0) N = -N;
    float radius = max(p[1].y, 0.05);
    float2 radius_uv = min(radius / max(P.z * p[0].z * 2, 1e-3), 0.2) * float2(1 / p[0].w, 1);
    float occlusion = 0;
    [unroll] for (int s = 0; s < 8; ++s) {
        float a = (s + frac(i.pos.x * 0.067 + i.pos.y * 0.113)) * 0.785398163;
        float2 dir = float2(cos(a), sin(a));
        [unroll] for (int k = 1; k <= 3; ++k) {
            float2 uv = i.uv + dir * radius_uv * (float(k) / 3.0);
            float3 delta = pos_at(uv) - P;
            float dist = length(delta);
            float facing = max(dot(N, delta / max(dist, 1e-4)) - 0.08, 0);
            occlusion += facing * saturate(1.0 - dist / radius);
        }
    }
    float visibility = saturate(1.0 - p[1].z * occlusion / 24.0);
    return float4(visibility, visibility, visibility, 1);
}
float4 composite(O i) : SV_Target {
    float4 c = scene.SampleLevel(ls, i.uv, 0);
    if (depthtex.SampleLevel(pt, i.uv, 0) >= 0.99999) return c;
    float visibility = ao_tex.SampleLevel(ls, i.uv, 0).r;
    c.rgb *= visibility;
    return c;
}
)";
    static const std::string rt_hlsl = R"(
cbuffer P : register(b1) { float4 p[2]; };
Texture2D<float> depthtex : register(t1);
RaytracingAccelerationStructure scene : register(t9);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o;
}
float z_at(float2 uv) {
    float d = depthtex.SampleLevel(pt, uv, 0);
    return p[1].x > 0.5 ? d * 4096.0 : rcp(max(1.0 - d, 1e-5));
}
float3 pos_at(float2 uv) {
    float z = z_at(uv);
    return float3((uv * 2 - 1) * float2(p[0].w, -1) * p[0].z * z, z);
}
float4 trace(O i) : SV_Target {
    float d = depthtex.SampleLevel(pt, i.uv, 0);
    if (d >= 0.99999) return float4(1, 1, 1, 1);
    float3 P = pos_at(i.uv);
    float3 dx = pos_at(i.uv + float2(p[0].x, 0)) - pos_at(i.uv - float2(p[0].x, 0));
    float3 dy = pos_at(i.uv + float2(0, p[0].y)) - pos_at(i.uv - float2(0, p[0].y));
    float3 N = normalize(cross(dx, dy));
    if (dot(N, P) > 0) N = -N;
    float3 up = abs(N.y) < 0.95 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N)), B = cross(N, T);
    float occ = 0;
    float noise = frac(sin(dot(i.pos.xy, float2(12.9898, 78.233))) * 43758.5453);
    [unroll] for (int k = 0; k < 4; ++k) {
        float a = (k + noise) * 1.570796327;
        float r = sqrt((k + 0.5) * 0.25);
        float3 dir = normalize(N + r * (T * cos(a) + B * sin(a)));
        RayDesc ray;
        ray.Origin = P + N * 0.025;
        ray.Direction = dir;
        ray.TMin = 0.001;
        ray.TMax = p[1].y;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        q.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
        while (q.Proceed()) {}
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            occ += 1.0 - q.CommittedRayT() / p[1].y;
    }
    float visibility = saturate(1.0 - p[1].z * occ * 0.25);
    return float4(visibility, visibility, visibility, 1);
}
)";
    uint64_t vs_hash, ao_hash, comp_hash;
    if (!Compile(hlsl, "vs", "vs_5_0", vs_hash) || !Compile(hlsl, "ao", "ps_5_0", ao_hash) ||
        !Compile(hlsl, "composite", "ps_5_0", comp_hash)) {
        LOG_FIRST(1, "vulkan: ambient occlusion shaders did not compile; drawing without AO");
        return source;
    }
    const uint32_t aw = std::max<uint32_t>(t.w / 2, 1), ah = std::max<uint32_t>(t.h / 2, 1);
    if (ao_half_.img && (ao_w_ != t.w || ao_h_ != t.h)) DropImage(ao_half_), DropImage(ao_scene_);
    if (!ao_half_.img || !ao_scene_.img) {
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (!MakeImage(ao_half_, VK_IMAGE_TYPE_2D, kBgra, aw, ah, 1, 1, 1, usage) ||
            !MakeImage(ao_scene_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1, usage)) {
            DropImage(ao_half_);
            DropImage(ao_scene_);
            LOG_FIRST(1, "vulkan: ambient occlusion targets could not be made; drawing without AO");
            return source;
        }
        ao_w_ = t.w;
        ao_h_ = t.h;
    }
    VkPipeline pao = PassPipeline({vs_hash, ao_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pc = PassPipeline({vs_hash, comp_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pao || !pc) return source;
    static const bool wbuffer = [] {
        const char* e = std::getenv("XBE_WBUFFER");
        return !(e && *e == '0');
    }();
    float constants[8] = {1.0f / t.w, 1.0f / t.h, 0.5774f, float(t.w) / float(t.h),
                          wbuffer ? 1.0f : 0.0f, 0.75f, 0.85f, 0.0f};
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    if (rt_ok_ && rt_capture_[frame_slot_].built) {
        uint64_t rv, rp;
        if (Compile(rt_hlsl, "vs", "vs_6_5", rv) && Compile(rt_hlsl, "trace", "ps_6_5", rp)) {
            VkPipeline rt_pipeline = PassPipeline({rv, rp, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
            if (rt_pipeline) {
                pao = rt_pipeline;
                tlas = rt_capture_[frame_slot_].tlas;
            }
        }
    }
    VkViewport avp{0, 0, float(aw), float(ah), 0, 1};
    VkRect2D asc{{0, 0}, {aw, ah}};
    VkImageView av[4] = {VK_NULL_HANDLE, depth, VK_NULL_HANDLE, VK_NULL_HANDLE};
    FullScreen(pao, {ao_half_.att, VK_NULL_HANDLE, aw, ah}, avp, asc, constants, 8, av, 2, tlas);
    End();
    Barrier();
    VkViewport vp{0, 0, float(t.w), float(t.h), 0, 1};
    VkRect2D sc{{0, 0}, {t.w, t.h}};
    VkImageView cv[4] = {source, depth, ao_half_.view, VK_NULL_HANDLE};
    FullScreen(pc, {ao_scene_.att, VK_NULL_HANDLE, t.w, t.h}, vp, sc, constants, 8, cv, 3);
    End();
    Barrier();
    return ao_scene_.view;
}

VkImageView VulkanBackend::DepthOfField(Target& t, VkImageView source, VkImageView depth) {
    if (!source || !depth || !app::Get().dof_blur) return source;
    static const std::string hlsl = R"(
cbuffer P : register(b1) { float4 p[2]; };
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o;
}
float viewz(float d) {
    return d >= 0.99999 ? 10000 : p[0].w > 0.5 ? d * 4096.0 : rcp(max(1.0 - d, 1e-6));
}
float blur(float z) {
    return saturate(abs(1.0 - p[1].x / max(z, 0.1)) * 1.5) * p[1].y;
}
float4 dof(O i) : SV_Target {
    float2 uv = i.uv;
    float3 centre = scene.SampleLevel(pt, uv, 0).rgb;
    float z0 = viewz(depthtex.SampleLevel(pt, uv, 0));
    float r0 = blur(z0);
    if (r0 < 0.5 || z0 >= 9999) return float4(centre, 1);
    float2 px = p[0].xy;
    float3 sum = centre;
    float weight_sum = 1;
    [unroll] for (int k = 0; k < 16; ++k) {
        float rr = sqrt((k + 0.5) / 16.0);
        float a = k * 2.39996323;
        float2 q = uv + float2(cos(a), sin(a)) * (r0 * rr) * px;
        if (any(q < 0) || any(q > 1)) continue;
        float z = viewz(depthtex.SampleLevel(pt, q, 0));
        float w = z < z0 * 0.97 ? saturate(blur(z) - rr * r0 + 1) : 1;
        sum += scene.SampleLevel(ls, q, 0).rgb * w;
        weight_sum += w;
    }
    return float4(sum / weight_sum, 1);
}
)";
    uint64_t vs_hash, ps_hash;
    if (!Compile(hlsl, "vs", "vs_5_0", vs_hash) || !Compile(hlsl, "dof", "ps_5_0", ps_hash)) {
        LOG_FIRST(1, "vulkan: depth-of-field shader did not compile; drawing without depth of field");
        return source;
    }
    if (dof_scene_.img && (dof_w_ != t.w || dof_h_ != t.h)) DropImage(dof_scene_);
    if (!dof_scene_.img) {
        if (!MakeImage(dof_scene_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
            LOG_FIRST(1, "vulkan: depth-of-field target could not be made; drawing without depth of field");
            return source;
        }
        dof_w_ = t.w;
        dof_h_ = t.h;
    }
    VkPipeline pipeline = PassPipeline({vs_hash, ps_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pipeline) return source;
    static const bool wbuffer = [] {
        const char* e = std::getenv("XBE_WBUFFER");
        return !(e && *e == '0');
    }();
    const app::Settings& st = app::Get();
    float constants[8] = {1.0f / t.w, 1.0f / t.h, float(t.w) / float(t.h), wbuffer ? 1.0f : 0.0f,
                          float(st.focus_distance), float(st.dof_blur), 0.0f, 0.0f};
    VkViewport vp{0, 0, float(t.w), float(t.h), 0, 1};
    VkRect2D sc{{0, 0}, {t.w, t.h}};
    VkImageView views[4] = {source, depth, VK_NULL_HANDLE, VK_NULL_HANDLE};
    FullScreen(pipeline, {dof_scene_.att, VK_NULL_HANDLE, t.w, t.h}, vp, sc, constants, 8, views, 2);
    End();
    Barrier();
    return dof_scene_.view;
}

bool VulkanBackend::RememberCamera(const State& s) {
    if ((!app::Get().motion_blur && !app::Get().temporal_aa) || draw_2d_ || !bound_rt_ || !bound_ds_ ||
        bound_rt_->gw != guest_w_ || bound_rt_->gh != guest_h_ || bound_ds_->w != bound_rt_->w ||
        bound_ds_->h != bound_rt_->h || !s.rs[kRsZEnable])
        return false;
    if (camera_frame_ == presents_ + 1) return true;
    const auto& view = s.transforms[2];
    GuestExtent size;
    SurfaceSize(bound_rt_ ? s.rt : s.zs, size.w, size.h);
    size = DrawnExtent(s, size);
    if (!size.w || !size.h) return false;
    const auto& viewport = s.viewport;
    const float sx = float(viewport.w) / size.w, sy = float(viewport.h) / size.h;
    const float ox = (2.0f * viewport.x + viewport.w) / size.w - 1;
    const float oy = 1 - (2.0f * viewport.y + viewport.h) / size.h;
    const float vpm[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, viewport.max_z - viewport.min_z, 0,
                           ox, oy, viewport.min_z, 1};
    float raw_projection[16];
    std::memcpy(raw_projection, s.transforms[3].data(), sizeof(raw_projection));
    if (app::Get().temporal_aa) {
        auto halton = [](uint32_t i, uint32_t base) {
            float f = 1, r = 0;
            for (; i; i /= base) f /= float(base), r += f * float(i % base);
            return r;
        };
        const uint32_t n = uint32_t(presents_ % 8) + 1;
        const float jx = (halton(n, 2) - 0.5f) * 2.0f / float(bound_rt_->w);
        const float jy = (halton(n, 3) - 0.5f) * 2.0f / float(bound_rt_->h);
        for (int r = 0; r < 4; r++) {
            raw_projection[r * 4] += jx * raw_projection[r * 4 + 3];
            raw_projection[r * 4 + 1] += jy * raw_projection[r * 4 + 3];
        }
    }
    float projection[16]{};
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++) projection[r * 4 + c] += raw_projection[r * 4 + k] * vpm[k * 4 + c];
    if (!std::isfinite(projection[0]) || !std::isfinite(projection[5]) || !std::isfinite(projection[11]) ||
        std::abs(projection[0]) < 1e-5f || std::abs(projection[5]) < 1e-5f || std::abs(projection[11]) < 1e-5f)
        return false;
    std::memcpy(camera_view_, view.data(), sizeof(camera_view_));
    std::memcpy(camera_projection_, projection, sizeof(camera_projection_));
    camera_frame_ = presents_ + 1;
    return true;
}

VkImageView VulkanBackend::MotionBlur(Target& t, VkImageView source, VkImageView depth) {
    if (!source || !depth || camera_frame_ != presents_ + 1 || !previous_camera_frame_ ||
        previous_camera_frame_ != presents_ || !app::Get().motion_blur || !app::Get().motion_blur_strength)
        return source;
    static const std::string hlsl = R"(
cbuffer P : register(b1) {
    row_major float4x4 inverse_view;
    row_major float4x4 inverse_projection;
    row_major float4x4 previous_view_projection;
    float4 p[2];
};
Texture2D scene : register(t0);
Texture2D<float> depthtex : register(t1);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = t; return o;
}
float3 world_at(float2 uv, float d) {
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float4 v;
    if (p[0].x > 0.5) {
        v = mul(float4(ndc, 1, 1), inverse_projection);
        v.xyz /= max(abs(v.w), 1e-6) * sign(v.w);
        float z = d * 4096.0;
        v.xyz *= z / max(abs(v.z), 1e-6) * sign(v.z);
        v.w = 1;
    } else {
        v = mul(float4(ndc, d, 1), inverse_projection);
        v /= max(abs(v.w), 1e-6) * sign(v.w);
    }
    float4 w = mul(float4(v.xyz, 1), inverse_view);
    return w.xyz / max(abs(w.w), 1e-6) * sign(w.w);
}
float2 previous_uv(float2 uv, float d) {
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float4 previous;
    if (d >= 0.99999) {
        float4 ray = mul(float4(ndc, 1, 1), inverse_projection);
        ray.xyz /= max(abs(ray.w), 1e-6) * sign(ray.w);
        float3 direction = mul(float4(normalize(ray.xyz), 0), inverse_view).xyz;
        previous = mul(float4(direction, 0), previous_view_projection);
    } else {
        float3 world = world_at(uv, d);
        previous = mul(float4(world, 1), previous_view_projection);
    }
    if (previous.w <= 1e-5) return uv + 1;
    return float2(previous.x / previous.w * 0.5 + 0.5, 0.5 - previous.y / previous.w * 0.5);
}
float viewz(float d) { return p[0].x > 0.5 ? d * 4096.0 : rcp(max(1.0 - d, 1e-6)); }
float4 blur(O i) : SV_Target {
    float4 centre = scene.SampleLevel(pt, i.uv, 0);
    float d = depthtex.SampleLevel(pt, i.uv, 0);
    float2 old_uv = previous_uv(i.uv, d);
    if (any(old_uv < 0) || any(old_uv > 1)) return centre;
    float2 velocity = (i.uv - old_uv) * p[0].y;
    float2 pixel_scale = float2(p[0].w, p[1].x);
    float length_px = length(velocity / pixel_scale);
    if (length_px < 0.5 || length_px > 0.25 * max(p[1].y, p[1].z)) return centre;
    velocity *= min(1.0, p[0].z / length_px);
    float z0 = viewz(d), noise = frac(sin(dot(i.pos.xy, float2(12.9898, 78.233))) * 43758.5453);
    float3 sum = centre.rgb;
    float weight = 1;
    [unroll] for (int k = 0; k < 8; ++k) {
        float2 q = i.uv + velocity * ((k + noise) / 8.0 - 0.5);
        if (any(q < 0) || any(q > 1)) continue;
        float z = viewz(depthtex.SampleLevel(pt, q, 0));
        if (d < 0.99999 && abs(z - z0) > max(0.1, z0 * 0.12)) continue;
        sum += scene.SampleLevel(ls, q, 0).rgb;
        weight += 1;
    }
    return float4(sum / weight, centre.a);
}
)";
    uint64_t vs_hash, ps_hash;
    if (!Compile(hlsl, "vs", "vs_5_0", vs_hash) || !Compile(hlsl, "blur", "ps_5_0", ps_hash)) {
        LOG_FIRST(1, "vulkan: motion-blur shader did not compile; drawing without motion blur");
        return source;
    }
    if (motion_scene_.img && (motion_w_ != t.w || motion_h_ != t.h)) DropImage(motion_scene_);
    if (!motion_scene_.img) {
        if (!MakeImage(motion_scene_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
            LOG_FIRST(1, "vulkan: motion-blur target could not be made; drawing without motion blur");
            return source;
        }
        motion_w_ = t.w;
        motion_h_ = t.h;
    }
    VkPipeline pipeline = PassPipeline({vs_hash, ps_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pipeline) return source;
    auto inverse = [](const float* in, float* out) {
        double a[4][8]{};
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) a[r][c] = in[r * 4 + c];
            a[r][r + 4] = 1;
        }
        for (int c = 0; c < 4; c++) {
            int pivot = c;
            for (int r = c + 1; r < 4; r++) if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
            if (std::abs(a[pivot][c]) < 1e-10) return false;
            if (pivot != c) for (int k = 0; k < 8; k++) std::swap(a[c][k], a[pivot][k]);
            double scale = a[c][c];
            for (int k = 0; k < 8; k++) a[c][k] /= scale;
            for (int r = 0; r < 4; r++) if (r != c) {
                double f = a[r][c];
                for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k];
            }
        }
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out[r * 4 + c] = float(a[r][c + 4]);
        return true;
    };
    auto multiply = [](const float* a, const float* b, float* out) {
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = 0;
            for (int k = 0; k < 4; k++) out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
        }
    };
    float constants[56]{};
    if (!inverse(camera_view_, constants) || !inverse(camera_projection_, constants + 16)) return source;
    float previous_view_projection[16];
    multiply(previous_view_, previous_projection_, previous_view_projection);
    std::memcpy(constants + 32, previous_view_projection, sizeof(previous_view_projection));
    const app::Settings& st = app::Get();
    const char* e = std::getenv("XBE_WBUFFER");
    constants[48] = !(e && *e == '0') ? 1.0f : 0.0f;
    constants[49] = float(st.motion_blur_strength) / 100.0f;
    constants[50] = float(st.motion_blur_limit);
    constants[51] = 1.0f / t.w;
    constants[52] = 1.0f / t.h;
    constants[53] = float(t.w);
    constants[54] = float(t.h);
    VkViewport vp{0, 0, float(t.w), float(t.h), 0, 1};
    VkRect2D sc{{0, 0}, {t.w, t.h}};
    VkImageView views[4] = {source, depth, VK_NULL_HANDLE, VK_NULL_HANDLE};
    FullScreen(pipeline, {motion_scene_.att, VK_NULL_HANDLE, t.w, t.h}, vp, sc, constants, 56, views, 2);
    End();
    Barrier();
    return motion_scene_.view;
}

// Meter the SDR frame, adapt exposure from the previous presentation, then tone map before the final blit.
VkImageView VulkanBackend::AutoExposure(Target& t, VkImageView source) {
    if (!source || !app::Get().auto_exposure) return source;
    static const std::string hlsl = R"(
cbuffer P : register(b1) { float4 p[2]; };
Texture2D scene : register(t0);
Texture2D<float> meter : register(t1);
Texture2D<float> history : register(t2);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 q=float2((id<<1)&2,id&2); o.pos=float4(q*float2(2,-2)+float2(-1,1),0,1); o.uv=q; return o;
}
float luma(float3 c) { return dot(c,float3(0.2126,0.7152,0.0722)); }
float4 meter_ps(O i) : SV_Target {
    float3 c=scene.SampleLevel(pt,i.uv,0).rgb;
    return float4(log2(max(luma(pow(max(c,0),2.2)),1e-4)),0,0,1);
}
float4 adapt_ps(O i) : SV_Target {
    float sum=0, weights=0;
    [loop] for(int y=0;y<int(p[0].y);y++) [loop] for(int x=0;x<int(p[0].x);x++) {
        float2 uv=(float2(x,y)+0.5)/p[0].xy;
        float w=max(1.5-2*length((uv-0.5)*float2(1,0.8)),0.2);
        float m=meter.Load(int3(x,y,0));
        if(m<-9.0) continue;  // near black (letterbox bars, the sky's void): metered, they brighten everything else
        sum+=m*w; weights+=w;
    }
    float mean=weights>0 ? sum/weights : log2(0.18);
    // an SDR frame the game already exposed: a gentle correction around mid grey, never a full re-exposure
    float want=clamp(0.18/exp2(mean),0.85,1.5);
    float was=p[0].z>0.5 ? history.Load(int3(0,0,0)) : want;
    float e=exp(lerp(log(max(was,1e-3)),log(want),p[0].w));
    return float4(e,0,0,1);
}
float4 tone_ps(O i) : SV_Target {
    float3 c=pow(max(scene.SampleLevel(ls,i.uv,0).rgb,0),2.2);
    c*=history.Load(int3(0,0,0));
    // tones below the knee stay as the game drew them; brighter ones roll off smoothly to white instead of clipping
    const float k=0.75;
    float3 over=max(c-k,0);
    c=min(c,k)+(1-k)*(1-exp(-over/(1-k)));
    return float4(pow(c,1.0/2.2),1);
}
)";
    uint64_t vs_hash, meter_hash, adapt_hash, tone_hash;
    if (!Compile(hlsl,"vs","vs_5_0",vs_hash) || !Compile(hlsl,"meter_ps","ps_5_0",meter_hash) ||
        !Compile(hlsl,"adapt_ps","ps_5_0",adapt_hash) || !Compile(hlsl,"tone_ps","ps_5_0",tone_hash)) {
        LOG_FIRST(1,"vulkan: automatic-exposure shaders did not compile; drawing without exposure adaptation");
        return source;
    }
    constexpr uint32_t mw=32,mh=18;
    constexpr VkFormat mf=VK_FORMAT_R16G16B16A16_SFLOAT, ef=VK_FORMAT_R32G32B32A32_SFLOAT;
    const VkImageUsageFlags usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (exposure_w_!=t.w || exposure_h_!=t.h) {
        DropImage(exposure_meter_); for(Image& im:exposure_history_) DropImage(im); DropImage(exposure_scene_);
        exposure_w_=t.w; exposure_h_=t.h; exposure_frame_=0;
    }
    if (!exposure_meter_.img && !MakeImage(exposure_meter_,VK_IMAGE_TYPE_2D,mf,mw,mh,1,1,1,usage)) return source;
    for(Image& im:exposure_history_) if(!im.img && !MakeImage(im,VK_IMAGE_TYPE_2D,ef,1,1,1,1,1,usage)) return source;
    if(!exposure_scene_.img && !MakeImage(exposure_scene_,VK_IMAGE_TYPE_2D,kBgra,t.w,t.h,1,1,1,usage)) return source;
    VkPipeline pm=PassPipeline({vs_hash,meter_hash,mf,VK_FORMAT_UNDEFINED,0,0});
    VkPipeline pa=PassPipeline({vs_hash,adapt_hash,ef,VK_FORMAT_UNDEFINED,0,0});
    VkPipeline pt=PassPipeline({vs_hash,tone_hash,kBgra,VK_FORMAT_UNDEFINED,0,0});
    if(!pm||!pa||!pt) return source;
    const bool valid=exposure_frame_==presents_;
    Image& now=exposure_history_[presents_&1];
    const Image& was=exposure_history_[(presents_+1)&1];
    float mc[4]={1.0f/t.w,1.0f/t.h,0,0};
    VkViewport mvp{0,0,float(mw),float(mh),0,1}; VkRect2D msc{{0,0},{mw,mh}};
    VkImageView mv[1]={source}; FullScreen(pm,{exposure_meter_.att,VK_NULL_HANDLE,mw,mh},mvp,msc,mc,4,mv,1); End(); Barrier();
    const auto now_time=std::chrono::steady_clock::now();
    const float dt=exposure_time_.time_since_epoch().count()==0 ? 0.0f :
        std::clamp(std::chrono::duration<float>(now_time-exposure_time_).count(),0.0f,0.25f);
    exposure_time_=now_time;
    float ac[8]={float(mw),float(mh),valid?1.0f:0.0f,1.0f-std::exp(-dt*1.5f),0,0,0,0};
    VkViewport one{0,0,1,1,0,1}; VkRect2D one_sc{{0,0},{1,1}};
    VkImageView av[3]={dummy_.view,exposure_meter_.view,valid?was.view:dummy_.view};
    FullScreen(pa,{now.att,VK_NULL_HANDLE,1,1},one,one_sc,ac,8,av,3); End(); Barrier();
    float tc[4]={0,0,0,0}; VkViewport vp{0,0,float(t.w),float(t.h),0,1}; VkRect2D sc{{0,0},{t.w,t.h}};
    VkImageView tv[3]={source,dummy_.view,now.view};
    FullScreen(pt,{exposure_scene_.att,VK_NULL_HANDLE,t.w,t.h},vp,sc,tc,4,tv,3); End(); Barrier();
    exposure_frame_=presents_+1;
    return exposure_scene_.view;
}

// Jittered temporal AA: reproject the depth-tested world, reject disocclusions by previous view depth, and clamp history in YCoCg.
VkImageView VulkanBackend::TemporalAA(Target& t, VkImageView source, VkImageView depth) {
    if (!source || !depth || camera_frame_ != presents_ + 1 || !app::Get().temporal_aa) return source;
    static const std::string hlsl = R"(
cbuffer P : register(b1) {
    row_major float4x4 inverse_view;
    row_major float4x4 inverse_projection;
    row_major float4x4 previous_view_projection;
    row_major float4x4 previous_view;
    float4 p[2];
};
Texture2D scene : register(t0);
Texture2D history : register(t1);
Texture2D<float> depthtex : register(t2);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 q = float2((id << 1) & 2, id & 2);
    o.pos = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = q; return o;
}
float3 view_at(float2 uv, float d) {
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float4 v;
    if (p[0].x > 0.5) {
        v = mul(float4(ndc, 1, 1), inverse_projection);
        v.xyz /= max(abs(v.w), 1e-6) * sign(v.w);
        float z = d * 4096.0;
        v.xyz *= z / (max(abs(v.z), 1e-6) * sign(v.z));
    } else {
        v = mul(float4(ndc, d, 1), inverse_projection);
        v.xyz /= max(abs(v.w), 1e-6) * sign(v.w);
    }
    return v.xyz;
}
float3 view_ray(float2 uv) {
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float4 v = mul(float4(ndc, 1, 1), inverse_projection);
    return normalize(v.xyz);
}
float3 world_at(float3 v) {
    float4 w = mul(float4(v, 1), inverse_view);
    return w.xyz / (max(abs(w.w), 1e-6) * sign(w.w));
}
float2 old_uv(float2 uv, float d, bool sky, out float old_z) {
    float4 old;
    if (sky) {
        float3 dir = mul(float4(view_ray(uv), 0), inverse_view).xyz;
        old = mul(float4(dir, 0), previous_view_projection);
        old_z = 4096;
    } else {
        float3 v = view_at(uv, d);
        float3 world = world_at(v);
        float4 pv = mul(float4(world, 1), previous_view);
        old_z = abs(pv.z);
        old = mul(float4(world, 1), previous_view_projection);
    }
    if (old.w <= 1e-5) return uv + 2;
    return float2(old.x / old.w * 0.5 + 0.5, 0.5 - old.y / old.w * 0.5);
}
float3 to_ycocg(float3 c) { return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25))); }
float3 from_ycocg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
float4 taa(O i) : SV_Target {
    float2 uv = i.uv, px = p[0].zw;
    float3 c = scene.SampleLevel(pt, uv, 0).rgb;
    float d = depthtex.SampleLevel(pt, uv, 0); bool sky = d >= 0.99999;
    float z = 4096;
    if (!sky) z = abs(view_at(uv, d).z);
    if (p[0].y > 0.5) {
        float old_z;
        float2 prev = old_uv(uv, d, sky, old_z);
        if (all(prev >= 0) && all(prev <= 1)) {
            float4 h = history.SampleLevel(pt, prev, 0);
            bool valid = sky || (h.a > 0 && abs(h.a - old_z) < max(0.05, old_z * 0.04));
            if (valid) {
                float3 m1 = 0, m2 = 0;
                [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
                    float3 n = to_ycocg(scene.SampleLevel(pt, uv + float2(x, y) * px, 0).rgb);
                    m1 += n; m2 += n * n;
                }
                m1 /= 9; m2 /= 9;
                float3 sd = sqrt(max(m2 - m1 * m1, 0));
                float3 lo = m1 - sd * 1.25, hi = m1 + sd * 1.25;
                h.rgb = from_ycocg(clamp(to_ycocg(h.rgb), lo, hi));
                c = lerp(c, h.rgb, p[1].x);
            }
        }
    }
    return float4(c, z);
}
)";
    uint64_t vs_hash, ps_hash;
    if (!Compile(hlsl, "vs", "vs_5_0", vs_hash) || !Compile(hlsl, "taa", "ps_5_0", ps_hash)) {
        LOG_FIRST(1, "vulkan: temporal AA shaders did not compile; drawing without TAA");
        return source;
    }
    VkPipeline pipeline = PassPipeline({vs_hash, ps_hash, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pipeline) return source;
    if (taa_w_ != t.w || taa_h_ != t.h) {
        for (Image& im : taa_history_) DropImage(im);
        taa_w_ = t.w; taa_h_ = t.h; taa_frame_ = 0;
    }
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for (Image& im : taa_history_)
        if (!im.img && !MakeImage(im, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, t.w, t.h, 1, 1, 1, usage))
            return source;
    const bool history_valid = taa_frame_ == presents_ && previous_camera_frame_ == presents_;
    float constants[72]{};
    auto inverse = [](const float* in, float* out) {
        double a[4][8]{};
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) a[r][c] = in[r * 4 + c];
            a[r][r + 4] = 1;
        }
        for (int c = 0; c < 4; c++) {
            int pivot = c;
            for (int r = c + 1; r < 4; r++) if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
            if (std::abs(a[pivot][c]) < 1e-10) return false;
            if (pivot != c) for (int k = 0; k < 8; k++) std::swap(a[c][k], a[pivot][k]);
            const double scale = a[c][c];
            for (int k = 0; k < 8; k++) a[c][k] /= scale;
            for (int r = 0; r < 4; r++) if (r != c) {
                const double f = a[r][c];
                for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k];
            }
        }
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out[r * 4 + c] = float(a[r][c + 4]);
        return true;
    };
    auto multiply = [](const float* a, const float* b, float* out) {
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = 0;
            for (int k = 0; k < 4; k++) out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
        }
    };
    if (!inverse(camera_view_, constants) || !inverse(camera_projection_, constants + 16)) return source;
    float previous_vp[16];
    multiply(previous_view_, previous_projection_, previous_vp);
    std::memcpy(constants + 32, previous_vp, sizeof(previous_vp));
    std::memcpy(constants + 48, previous_view_, sizeof(previous_view_));
    const char* wbuf = std::getenv("XBE_WBUFFER");
    constants[64] = !(wbuf && *wbuf == '0') ? 1.0f : 0.0f;
    constants[65] = history_valid ? 1.0f : 0.0f;
    constants[66] = 1.0f / t.w;
    constants[67] = 1.0f / t.h;
    constants[68] = 0.9f;
    Image& out = taa_history_[presents_ & 1];
    const Image& history = taa_history_[(presents_ + 1) & 1];
    VkViewport vp{0, 0, float(t.w), float(t.h), 0, 1};
    VkRect2D sc{{0, 0}, {t.w, t.h}};
    VkImageView views[3] = {source, history_valid ? history.view : dummy_.view, depth};
    FullScreen(pipeline, {out.att, VK_NULL_HANDLE, t.w, t.h}, vp, sc, constants, 72, views, 3);
    End();
    Barrier();
    taa_frame_ = presents_ + 1;
    return out.view;
}

VkImageView VulkanBackend::UpscaleMovie(Tex& tx, const TextureDesc& d) {
    for (auto it = movie_up_.begin(); it != movie_up_.end();) {
        if (it->second.frame + 300 < presents_ + 1) {
            DropImage(it->second.up);
            DropImage(it->second.out);
            it = movie_up_.erase(it);
        } else {
            ++it;
        }
    }
    uint64_t vh, uh, sh;
    if (!tx.img || !Compile(kMovieHlsl, "vs", "vs_5_0", vh) || !Compile(kMovieHlsl, "up", "ps_5_0", uh) ||
        !Compile(kMovieHlsl, "sharp", "ps_5_0", sh)) {
        LOG_FIRST(1, "vulkan: movie upscaling did not compile; drawing movies unchanged");
        return VK_NULL_HANDLE;
    }
    VkPipeline pu = PassPipeline({vh, uh, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline ps = PassPipeline({vh, sh, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pu || !ps) return VK_NULL_HANDLE;
    const float scale = std::clamp(float(bb_h_ ? bb_h_ : 1080) / float(d.height), 1.5f, 4.0f);
    const uint32_t w = uint32_t(float(d.width) * scale + 0.5f);
    const uint32_t h = uint32_t(float(d.height) * scale + 0.5f);
    MovieUp& m = movie_up_[d.data];
    m.frame = presents_ + 1;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for (Image* im : {&m.up, &m.out}) {
        if (im->img && (im->w != w || im->h != h)) DropImage(*im), m.sig = 0;
        if (!im->img && !MakeImage(*im, VK_IMAGE_TYPE_2D, kBgra, w, h, 1, 1, 1, usage)) {
            DropImage(m.up);
            DropImage(m.out);
            movie_up_.erase(d.data);
            return VK_NULL_HANDLE;
        }
    }
    if (m.sig != tx.sig) {
        m.sig = tx.sig;
        End();
        Barrier();
        const float constants[8] = {1.0f / float(d.width), 1.0f / float(d.height), float(d.width), float(d.height),
                                    1.0f / float(w), 1.0f / float(h), 0.8f, 0};
        const VkViewport vp{0, 0, float(w), float(h), 0, 1};
        const VkRect2D sc{{0, 0}, {w, h}};
        VkImageView source = tx.img.view;
        FullScreen(pu, {m.up.att, VK_NULL_HANDLE, w, h}, vp, sc, constants, 8, &source, 1);
        End();
        Barrier();
        source = m.up.view;
        FullScreen(ps, {m.out.att, VK_NULL_HANDLE, w, h}, vp, sc, constants, 8, &source, 1);
        End();
        Barrier();
    }
    return m.out.view;
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
    if (!dst_t || src_t == dst_t) return;
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
    if (!d.cube && d.depth == 1 && !fi.depth) {
        auto r = FindReplacement(d.data);
        if (r && ((r->sharpened && !app::Get().sharp_2d) || !ReplacementFits(*r, d))) r = nullptr;
        if (VkImageView v = r ? BindReplacement(*r, d, stages.levels[stage]) : VK_NULL_HANDLE) {
            stages.replaced[stage] = true;
            return v;
        }
    }
    if (draw_2d_) Sharpen2D(d);
    StagePalette palette;
    ReadStagePalette(s, stage, d, palette);
    const uint64_t key = uint64_t(palette.va) << 32 | d.data;
    Tex& tx = textures_[key];
    bool same = SameTexture(tx, d);
    const uint8_t* guest = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
    if (tx.frame != presents_ + 1 || !same || tx.palette != palette.hash) {
        TexturePackSeen(d, guest, palette.va ? palette.colours : nullptr, palette.hash);
        if (auto replacement = FindReplacement(d.data); replacement && ReplacementFits(*replacement, d)) {
            stages.replaced[stage] = true;
            return BindReplacement(*replacement, d, stages.levels[stage]);
        }
    }
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
            UploadTexture(tx, d, guest, palette.va ? palette.colours : nullptr);
        }
    }
    if (d.cube && !fi.depth) CopyCubeTargets(tx, d);
    // YUY2 (Xbox format 0x24) has already been converted to BGRA by UploadTexture.
    if (d.format == 0x24 && d.width >= 320 && d.height >= 200 && app::Get().movie_upscale && !palette.va) {
        if (VkImageView v = UpscaleMovie(tx, d)) {
            stages.levels[stage] = 1;
            return v;
        }
    }
    stages.levels[stage] = tx.levels;
    if (fi.depth) stages.info[stage].shadow = ShadowDepthKind(d.format);
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
    VkFormat color = kBgra;
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

void VulkanBackend::PushDescriptors(const Stretch& vsc, const Stretch& dc, const DrawTextures& stages, const VkSampler smp[4],
                                     const Buffer* capture) {
    VkWriteDescriptorSet w[kBindings]{};
    VkDescriptorBufferInfo bi[2] = {{vsc.buf, vsc.off, sizeof(State::vsc)}, {dc.buf, dc.off, sizeof(DrawConstants)}};
    VkDescriptorBufferInfo cap_bi{capture ? capture->buf : VK_NULL_HANDLE, 0, capture ? capture->size : 0};
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
    if (capture) w[n++] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, VK_NULL_HANDLE, kBindCapture, 0, 1,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &cap_bi, nullptr};
    vkCmdPushDescriptorSetKHR(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, n, w);
}

bool VulkanBackend::DrawGeometry(const Assembled& a) {
    VkBuffer bufs[16];
    VkDeviceSize offsets[16], strides[16];
    for (uint32_t i = 0; i < a.buffer_count; i++) {
        const VertexBuffer& b = a.buffers[i];
        Stretch s = Stream(std::max<uint32_t>(b.bytes, 4));
        if (!s.p) return false;
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
        if (!s.p) return false;
        std::memcpy(s.p, a.index_src, ibytes);
        vkCmdBindIndexBuffer(cmd_, s.buf, s.off, a.index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd_, a.index_count, 1, 0, a.base_vertex, 0);
    } else {
        vkCmdDraw(cmd_, a.draw_count, 1, 0, 0);
    }
    return true;
}

void VulkanBackend::CaptureGeometry(const State& s, const Assembled& a, const DrawShaders& sh, const PsoKey& key,
                                    const Stretch& vsc, const Stretch& dc, const DrawTextures& stages,
                                    const VkSampler smp[4]) {
    const char* capture_env = std::getenv("XBE_RT_CAPTURE");
    const bool enabled = app::Get().ambient_occlusion || (capture_env && *capture_env == '1');
    if (!enabled || !rt_ok_ || draw_2d_ || !bound_rt_ || !bound_ds_ || bound_rt_->gw != guest_w_ ||
        bound_rt_->gh != guest_h_ || bound_ds_->w != bound_rt_->w || bound_ds_->h != bound_rt_->h || !key.zen ||
        !key.zwrite || key.blend || a.topology != Topology::Triangles || a.pretransformed || sh.quad ||
        SimpleState(s, kMAlphaTestEnable))
        return;

    const uint32_t n = a.index_count ? a.index_count : a.draw_count;
    if (n < 3) return;
    int64_t vmin = 0, vmax = int64_t(n) - 1;
    if (a.index_count) {
        vmin = INT64_MAX;
        vmax = INT64_MIN;
        for (uint32_t i = 0; i < n; i++) {
            int64_t v = int64_t(a.index16 ? static_cast<const uint16_t*>(a.index_src)[i]
                                          : static_cast<const uint32_t*>(a.index_src)[i]) + a.base_vertex;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
    }
    if (vmin < 0 || vmax < vmin || vmax > INT32_MAX) return;
    const uint64_t span = uint64_t(vmax - vmin + 1);
    if (span > kRtCaptureCapacity) return;

    RtCaptureSlot& slot = rt_capture_[frame_slot_];
    const uint64_t frame = presents_ + 1;
    if (slot.frame != frame) {
        // Present waits too late for mapped/frame-owned resources; wait before recording the first capture write.
        Wait(frame_serial_[frame_slot_]);
        slot.frame = frame;
        slot.used = 0;
        slot.triangles.clear();
        slot.built = false;
    }
    if (!slot.vertices.buf &&
        !MakeBuffer(slot.vertices, VkDeviceSize(kRtCaptureCapacity) * 16,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                    false)) {
        LOG_FIRST(1, "vulkan: RT capture buffer allocation failed; scene capture is disabled");
        return;
    }
    if (uint64_t(slot.used) + span > kRtCaptureCapacity) {
        LOG_FIRST(1, "vulkan: RT capture reached its per-frame %u-vertex limit", kRtCaptureCapacity);
        return;
    }

    const uint64_t shader_key = sh.vkey ^ kRtCaptureShaderSalt;
    uint64_t capture_vs = 0;
    if (auto it = shader_src_.find(shader_key); it != shader_src_.end()) {
        capture_vs = it->second;
    } else {
        std::string source = VertexHlsl(s, a, sh);
        const size_t at = source.find("VOut main(");
        const size_t close = at == std::string::npos ? at : source.find(')', at);
        if (close == std::string::npos) return;
        const std::string params = source.substr(at + 10, close - at - 10);
        const size_t last_space = params.find_last_of(' ');
        if (last_space == std::string::npos) return;
        const std::string arg = params.substr(last_space + 1);
        source.replace(at, 10, "VOut game_main(");
        char tail[2048];
        std::snprintf(tail, sizeof(tail),
                      "\nRWByteAddressBuffer rt_cap : register(u0);\n"
                      "struct RtCapturePC { uint base; uint count; int vmin; uint wbuf; };\n"
                      "[[vk::push_constant]] RtCapturePC rt_pc;\n"
                      "VOut main(%s, uint rt_vid : SV_VertexID) {\n"
                      "    VOut o = game_main(%s);\n"
                      "    int slot = int(rt_vid) - rt_pc.vmin;\n"
                      "    if (slot >= 0 && uint(slot) < rt_pc.count) {\n"
                      "        float w = abs(o.pos.w) > 1e-8 ? o.pos.w : 1e-8;\n"
                      "        float3 p;\n"
                      "        if (rt_pc.wbuf != 0) p = float3(o.pos.xy / w * o.wlin, o.wlin);\n"
                      "        else { float z = 1 / max(1 - o.pos.z / w, 1e-6); p = float3(o.pos.xy / w * z, z); }\n"
                      "        rt_cap.Store4((rt_pc.base + uint(slot)) * 16, asuint(float4(p, 1)));\n"
                      "    }\n"
                      "    return o;\n"
                      "}\n",
                      params.c_str(), arg.c_str());
        source += tail;
        if (!Compile(source, "main", "vs_5_0", capture_vs)) capture_vs = 0;
        shader_src_[shader_key] = capture_vs;
    }
    if (!capture_vs) return;

    PsoKey capture_key = key;
    capture_key.vs = capture_vs;
    capture_key.blend = 0;
    capture_key.wmask = 0;
    capture_key.zen = 0;
    capture_key.zwrite = 0;
    capture_key.sten = 0;
    capture_key.fill = 0;
    VkPipeline pipeline = EnsurePipeline(capture_key);
    if (!pipeline) return;

    uint32_t wbuf = s.rs[kRsZEnable] == 2;
    static const bool wbuffer_enabled = [] {
        const char* e = std::getenv("XBE_WBUFFER");
        return !(e && *e == '0');
    }();
    wbuf = wbuf && wbuffer_enabled;
    if (slot.triangles.size() + size_t(n) > (6u << 20)) return;
    const uint32_t base = slot.used;
    const uint32_t push[4] = {base, uint32_t(span), uint32_t(int32_t(vmin)), wbuf};
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, ds_.pipeline = pipeline);
    PushDescriptors(vsc, dc, stages, smp, &slot.vertices);
    vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
    if (!DrawGeometry(a)) return;
    auto vertex = [&](uint32_t i) -> uint32_t {
        const int64_t raw = a.index_count
                                ? int64_t(a.index16 ? static_cast<const uint16_t*>(a.index_src)[i]
                                                    : static_cast<const uint32_t*>(a.index_src)[i]) + a.base_vertex
                                : int64_t(i);
        return base + uint32_t(raw - vmin);
    };
    auto add_triangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        if (i0 != i1 && i1 != i2 && i0 != i2) slot.triangles.insert(slot.triangles.end(), {i0, i1, i2});
    };
    if (a.strip) {
        for (uint32_t i = 0; i + 2 < n; i++) {
            uint32_t i0 = vertex(i), i1 = vertex(i + 1), i2 = vertex(i + 2);
            if (i & 1) std::swap(i0, i1);
            add_triangle(i0, i1, i2);
        }
    } else {
        for (uint32_t i = 0; i + 2 < n; i += 3) add_triangle(vertex(i), vertex(i + 1), vertex(i + 2));
    }
    slot.used += uint32_t(span);
}
bool VulkanBackend::BuildRtStructures() {
    if (!rt_ok_) return false;
    const char* capture_env = std::getenv("XBE_RT_CAPTURE");
    const bool enabled = app::Get().ambient_occlusion || (capture_env && *capture_env == '1');
    RtCaptureSlot& slot = rt_capture_[frame_slot_];
    slot.built = false;
    if (!enabled || slot.frame != presents_ + 1 || !slot.used || slot.triangles.empty()) return false;

    const VkDeviceSize index_bytes = VkDeviceSize(slot.triangles.size()) * sizeof(uint32_t);
    const VkBufferUsageFlags input_usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!slot.indices.buf || slot.indices.size < index_bytes) {
        DropBuffer(slot.indices);
        if (!MakeBuffer(slot.indices, std::max<VkDeviceSize>(index_bytes, 4), input_usage, true)) return false;
    }
    if (!slot.indices.mem.map) return false;
    std::memcpy(slot.indices.mem.map, slot.triangles.data(), size_t(index_bytes));

    if (!rt_scratch_align_) {
        VkPhysicalDeviceAccelerationStructurePropertiesKHR as_props{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &as_props};
        vkGetPhysicalDeviceProperties2(pd_, &props);
        rt_scratch_align_ = std::max<VkDeviceSize>(as_props.minAccelerationStructureScratchOffsetAlignment, 1);
    }

    VkAccelerationStructureGeometryTrianglesDataKHR tri{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = BufferAddress(slot.vertices);
    tri.vertexStride = 16;
    tri.maxVertex = slot.used - 1;
    tri.indexType = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress = BufferAddress(slot.indices);
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = tri;

    const uint32_t primitive_count = uint32_t(slot.triangles.size() / 3);
    VkAccelerationStructureBuildGeometryInfoKHR blas_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    blas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    blas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    blas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    blas_info.geometryCount = 1;
    blas_info.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR blas_sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_info,
                                            &primitive_count, &blas_sizes);

    auto ensure_as = [&](VkAccelerationStructureKHR& as, Buffer& storage, VkDeviceSize& capacity, VkDeviceSize need,
                         VkAccelerationStructureTypeKHR type) {
        if (as && capacity >= need) return true;
        if (as) vkDestroyAccelerationStructureKHR(dev_, as, nullptr);
        as = VK_NULL_HANDLE;
        DropBuffer(storage);
        if (!MakeBuffer(storage, need, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        false))
            return false;
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        ci.buffer = storage.buf;
        ci.size = need;
        ci.type = type;
        if (vkCreateAccelerationStructureKHR(dev_, &ci, nullptr, &as) != VK_SUCCESS) return false;
        capacity = need;
        return true;
    };
    if (slot.blas && slot.blas_primitives < primitive_count) slot.blas_size = 0;
    if (!ensure_as(slot.blas, slot.blas_store, slot.blas_size, blas_sizes.accelerationStructureSize,
                   VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR))
        return false;
    slot.blas_primitives = std::max(slot.blas_primitives, primitive_count);

    if (!slot.instance.buf && !MakeBuffer(slot.instance, sizeof(VkAccelerationStructureInstanceKHR), input_usage, true))
        return false;
    if (!slot.instance.mem.map) return false;
    VkAccelerationStructureDeviceAddressInfoKHR blas_address_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    blas_address_info.accelerationStructure = slot.blas;
    VkAccelerationStructureInstanceKHR instance{};
    instance.transform.matrix[0][0] = 1;
    instance.transform.matrix[1][1] = 1;
    instance.transform.matrix[2][2] = 1;
    instance.mask = 0xFF;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(dev_, &blas_address_info);
    if (!instance.accelerationStructureReference) return false;
    std::memcpy(slot.instance.mem.map, &instance, sizeof(instance));

    VkAccelerationStructureGeometryInstancesDataKHR instances{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    instances.arrayOfPointers = VK_FALSE;
    instances.data.deviceAddress = BufferAddress(slot.instance);
    VkAccelerationStructureGeometryKHR tlas_geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tlas_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlas_geometry.geometry.instances = instances;
    VkAccelerationStructureBuildGeometryInfoKHR tlas_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tlas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlas_info.geometryCount = 1;
    tlas_info.pGeometries = &tlas_geometry;
    const uint32_t instance_count = 1;
    VkAccelerationStructureBuildSizesInfoKHR tlas_sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_info,
                                            &instance_count, &tlas_sizes);
    if (!ensure_as(slot.tlas, slot.tlas_store, slot.tlas_size, tlas_sizes.accelerationStructureSize,
                   VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR))
        return false;

    const VkDeviceSize scratch_need = std::max(blas_sizes.buildScratchSize, tlas_sizes.buildScratchSize) +
                                      rt_scratch_align_;
    if (!slot.scratch.buf || slot.scratch_size < scratch_need) {
        DropBuffer(slot.scratch);
        if (!MakeBuffer(slot.scratch, scratch_need, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                        false))
            return false;
        slot.scratch_size = scratch_need;
    }
    VkDeviceAddress scratch = BufferAddress(slot.scratch);
    scratch = (scratch + rt_scratch_align_ - 1) & ~(rt_scratch_align_ - 1);
    if (!scratch) return false;

    End();
    Barrier();
    blas_info.dstAccelerationStructure = slot.blas;
    blas_info.scratchData.deviceAddress = scratch;
    VkAccelerationStructureBuildRangeInfoKHR blas_range{primitive_count, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* blas_ranges = &blas_range;
    vkCmdBuildAccelerationStructuresKHR(Cmd(), 1, &blas_info, &blas_ranges);
    Barrier();

    tlas_info.dstAccelerationStructure = slot.tlas;
    tlas_info.scratchData.deviceAddress = scratch;
    VkAccelerationStructureBuildRangeInfoKHR tlas_range{1, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* tlas_ranges = &tlas_range;
    vkCmdBuildAccelerationStructuresKHR(Cmd(), 1, &tlas_info, &tlas_ranges);
    Barrier();
    slot.built = true;
    return true;
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
    for (int i = 0; i < 4; i++) stages.view[i] = BindTexture(s, i, stages);
    if (!BindTargets(s)) return;
    // the pixel shader made for the textures
    PixelKey pk = MakePixelKey(s, stages.info);
    pk.quad = sh.quad;
    static const bool wbuffer = [] {
        const char* e = std::getenv("XBE_WBUFFER");
        return !(e && *e == '0');
    }();
    pk.wbuf = wbuffer && s.rs[kRsZEnable] == 2 && bound_ds_ ? 1 : 0;
    const bool camera_ok = RememberCamera(s);
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
    VkPipeline p = EnsurePipeline(k);
    if (!p) return;

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
    if (camera_ok && app::Get().temporal_aa && !draw_2d_ && bound_rt_) {
        auto halton = [](uint32_t i, uint32_t base) {
            float f = 1, r = 0;
            for (; i; i /= base) f /= float(base), r += f * float(i % base);
            return r;
        };
        const uint32_t n = uint32_t(presents_ % 8) + 1;
        c.taa[0] = (halton(n, 2) - 0.5f) * 2.0f / float(bound_rt_->w);
        c.taa[1] = (halton(n, 3) - 0.5f) * 2.0f / float(bound_rt_->h);
    }
    // samplers
    VkSampler smp[4];
    bool clamp = UnitTexcoords(a);
    uint32_t aniso = app::Get().aniso;
    for (int i = 0; i < 4; i++)
        smp[i] = Sampler(StageSampler(s, i, stages.levels[i], stages.info[i].shadow, clamp, aniso, stages.replaced[i]));

    // the rendering instance, then everything recorded inside it
    Begin(TargetAttachments(bound_rt_, bound_ds_));
    VkCommandBuffer cb = cmd_;
    // each constant buffer streamed again only when its contents changed, or a new submission is being recorded (the
    // stream chunk holding the last copy may be reused once the GPU is past the old one)
    bool fresh = last_serial_ != serial_ + 1;
    last_serial_ = serial_ + 1;
    if (fresh || vsc_last_.size() != sizeof(s.vsc) || std::memcmp(vsc_last_.data(), s.vsc.data(), sizeof(s.vsc))) {
        last_vsc_ = Stream(sizeof(s.vsc));
        if (!last_vsc_.p) return;
        std::memcpy(last_vsc_.p, s.vsc.data(), sizeof(s.vsc));
        const uint8_t* b = reinterpret_cast<const uint8_t*>(s.vsc.data());
        vsc_last_.assign(b, b + sizeof(s.vsc));
    }
    if (fresh || dc_last_.size() != sizeof(c) || std::memcmp(dc_last_.data(), &c, sizeof(c))) {
        last_dc_ = Stream(sizeof(c));
        if (!last_dc_.p) return;
        std::memcpy(last_dc_.p, &c, sizeof(c));
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&c);
        dc_last_.assign(b, b + sizeof(c));
    }
    if (ds_.pipeline != p) vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ds_.pipeline = p);
    PushDescriptors(last_vsc_, last_dc_, stages, smp);
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
    if (!draw_2d_ && k.zwrite && bound_rt_ && bound_ds_ && bound_rt_->gw == guest_w_ &&
        bound_rt_->gh == guest_h_ && bound_rt_->w == bound_ds_->w && bound_rt_->h == bound_ds_->h) {
        scene_depth_view_ = bound_ds_->res.view;
        scene_depth_w_ = bound_ds_->w;
        scene_depth_h_ = bound_ds_->h;
        scene_color_data_ = bound_rt_->data;
        scene_depth_frame_ = presents_ + 1;
    }
    perf_draws_++;
    if (counting) {
        vkCmdEndQuery(cb, queries_, q);
        counted_.push_back({q, serial_ + 1, SamplesPerPixel()});
    }
    CaptureGeometry(s, a, sh, k, last_vsc_, last_dc_, stages, smp);
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

void VulkanBackend::DeliverReports(bool wait) {
    constexpr uint32_t kTime = 0, kTimeHigh = 4, kCount = 8, kStatus = 12;
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
void VulkanBackend::BlitFrame(Target& t, VkImageView view, VkImageView source, VkImageView bloom) {
    float aspect = app::FrameAspect();
    float w = float(bb_w_), h = float(bb_h_);
    float vw = std::min(w, h * aspect);
    float vh = vw / aspect;
    VkViewport vp{(w - vw) / 2, (h - vh) / 2, vw, vh, 0, 1};
    VkRect2D sc{{0, 0}, {bb_w_, bb_h_}};
    float uv[4] = {0, 0, 1, 1};
    VkImageView depth = VK_NULL_HANDLE;
    if (scene_depth_frame_ == presents_ + 1 && scene_color_data_ == t.data && scene_depth_view_ &&
        scene_depth_w_ == t.w && scene_depth_h_ == t.h)
        depth = scene_depth_view_;
    // the swap chain image's rendering instance is already open (Present)
    open_ = true;
    cur_ = {view, VK_NULL_HANDLE, bb_w_, bb_h_};
    Blit(source, cur_, vp, sc, uv, {.gamma = gamma_, .final = true, .depth = depth}, swap_fmt_, bloom);
}

// Five thresholded downsample levels and a tent-filtered upsample make the existing SDR glow broad and multi-scale.
// This deliberately uses the 8-bit game frame; it is not Nightfire's HDR bloom, which starts from a float scene.
VkImageView VulkanBackend::Bloom(Target& t, VkImageView source) {
    if (!source || !app::Get().bloom) return VK_NULL_HANDLE;
    static const std::string hlsl = R"(
cbuffer P : register(b1) { float4 p[2]; };
Texture2D src : register(t0);
Texture2D own : register(t1);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 q = float2((id << 1) & 2, id & 2);
    o.pos = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = q; return o;
}
float lum(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float4 down0(O i) : SV_Target {
    float2 d = p[0].xy * 0.5;
    float3 c = src.SampleLevel(pt, i.uv + d * float2(-1, -1), 0).rgb;
    c += src.SampleLevel(pt, i.uv + d * float2(1, -1), 0).rgb;
    c += src.SampleLevel(pt, i.uv + d * float2(-1, 1), 0).rgb;
    c += src.SampleLevel(pt, i.uv + d * float2(1, 1), 0).rgb;
    c *= 0.25;
    return float4(c * smoothstep(0.70, 0.92, lum(c)), 1);
}
float4 down(O i) : SV_Target {
    float2 d = p[0].xy * 0.5;
    float3 c = src.SampleLevel(ls, i.uv + d * float2(-1, -1), 0).rgb;
    c += src.SampleLevel(ls, i.uv + d * float2(1, -1), 0).rgb;
    c += src.SampleLevel(ls, i.uv + d * float2(-1, 1), 0).rgb;
    c += src.SampleLevel(ls, i.uv + d * float2(1, 1), 0).rgb;
    return float4(c * 0.25, 1);
}
float4 up(O i) : SV_Target {
    float2 d = p[0].xy;
    float3 c = src.SampleLevel(ls, i.uv, 0).rgb * 4;
    c += (src.SampleLevel(ls, i.uv + float2(d.x, 0), 0).rgb +
          src.SampleLevel(ls, i.uv - float2(d.x, 0), 0).rgb +
          src.SampleLevel(ls, i.uv + float2(0, d.y), 0).rgb +
          src.SampleLevel(ls, i.uv - float2(0, d.y), 0).rgb) * 2;
    c += src.SampleLevel(ls, i.uv + d, 0).rgb + src.SampleLevel(ls, i.uv - d, 0).rgb +
         src.SampleLevel(ls, i.uv + d * float2(1, -1), 0).rgb +
         src.SampleLevel(ls, i.uv + d * float2(-1, 1), 0).rgb;
    c /= 16;
    return float4(saturate(own.SampleLevel(pt, i.uv, 0).rgb * 0.55 + c * 0.45), 1);
}
)";
    uint64_t vs_hash, down0_hash, down_hash, up_hash;
    if (!Compile(hlsl, "vs", "vs_5_0", vs_hash) || !Compile(hlsl, "down0", "ps_5_0", down0_hash) ||
        !Compile(hlsl, "down", "ps_5_0", down_hash) || !Compile(hlsl, "up", "ps_5_0", up_hash)) {
        LOG_FIRST(1, "vulkan: bloom pyramid shaders did not compile; drawing without multilevel bloom");
        return VK_NULL_HANDLE;
    }
    const uint32_t w[5] = {std::max(1u, t.w / 2), std::max(1u, t.w / 4), std::max(1u, t.w / 8),
                           std::max(1u, t.w / 16), std::max(1u, t.w / 32)};
    const uint32_t h[5] = {std::max(1u, t.h / 2), std::max(1u, t.h / 4), std::max(1u, t.h / 8),
                           std::max(1u, t.h / 16), std::max(1u, t.h / 32)};
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for (int i = 0; i < 5; i++) {
        if (bloom_down_[i].img && (bloom_down_[i].w != w[i] || bloom_down_[i].h != h[i])) DropImage(bloom_down_[i]);
        if (!bloom_down_[i].img && !MakeImage(bloom_down_[i], VK_IMAGE_TYPE_2D, kBgra, w[i], h[i], 1, 1, 1, usage))
            return VK_NULL_HANDLE;
    }
    for (int i = 0; i < 4; i++) {
        if (bloom_up_[i].img && (bloom_up_[i].w != w[i] || bloom_up_[i].h != h[i])) DropImage(bloom_up_[i]);
        if (!bloom_up_[i].img && !MakeImage(bloom_up_[i], VK_IMAGE_TYPE_2D, kBgra, w[i], h[i], 1, 1, 1, usage))
            return VK_NULL_HANDLE;
    }
    auto pass = [&](VkPipeline pipeline, Image& dst, VkImageView a, VkImageView b, uint32_t sw, uint32_t sh) {
        float constants[8] = {1.0f / sw, 1.0f / sh, 0, 0, 0, 0, 0, 0};
        VkViewport vp{0, 0, float(dst.w), float(dst.h), 0, 1};
        VkRect2D sc{{0, 0}, {dst.w, dst.h}};
        VkImageView views[2] = {a, b};
        FullScreen(pipeline, {dst.att, VK_NULL_HANDLE, dst.w, dst.h}, vp, sc, constants, 8, views, 2);
        End();
        Barrier();
    };
    VkPipeline pdown0 = PassPipeline({vs_hash, down0_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pdown = PassPipeline({vs_hash, down_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pup = PassPipeline({vs_hash, up_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!pdown0 || !pdown || !pup) return VK_NULL_HANDLE;
    pass(pdown0, bloom_down_[0], source, dummy_.view, t.w, t.h);
    for (int i = 1; i < 5; i++) pass(pdown, bloom_down_[i], bloom_down_[i - 1].view, dummy_.view, w[i - 1], h[i - 1]);
    for (int i = 3; i >= 0; i--) {
        VkImageView low = i == 3 ? bloom_down_[4].view : bloom_up_[i + 1].view;
        pass(pup, bloom_up_[i], low, bloom_down_[i].view, w[i + 1], h[i + 1]);
    }
    return bloom_up_[0].view;
}

// Screen-space shafts use bright, locally contrasting regions as sources and depth to keep them from bleeding across surfaces.
VkImageView VulkanBackend::LightShafts(Target& t, VkImageView source, VkImageView depth) {
    const float strength = float(app::Get().light_shafts) / 100.0f;
    const float fog_strength = float(app::Get().atmospheric_fog) / 100.0f;
    if (!source || !depth || (strength <= 0 && fog_strength <= 0)) return source;
    static const std::string hlsl = R"(
cbuffer P : register(b1) { float4 p[4]; };
Texture2D t0 : register(t0);
Texture2D<float> depthtex : register(t1);
Texture2D scene : register(t2);
Texture2D field : register(t3);
SamplerState ls : register(s0);
SamplerState pt : register(s1);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o; float2 q = float2((id << 1) & 2, id & 2);
    o.pos = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = q; return o;
}
float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float viewz(float d) { return p[0].w != 0 ? d * 4096.0 : rcp(max(1.0 - d, 1e-6)); }
float noise(float2 v) { return frac(sin(dot(v, float2(12.9898, 78.233))) * 43758.5453); }
float4 sources(O i) : SV_Target {
    float2 px = p[0].xy; float4 acc = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        float2 uv = i.uv + (float2(k & 1, k >> 1) - 0.5) * 2 * px;
        float3 c = t0.SampleLevel(ls, uv, 0).rgb;
        float d = depthtex.SampleLevel(pt, uv, 0), y = luma(c), surround = 0;
        [unroll] for (int n = 0; n < 4; n++) {
            float2 off = n == 0 ? float2(12, 0) : n == 1 ? float2(-12, 0) : n == 2 ? float2(0, 12) : float2(0, -12);
            surround += luma(t0.SampleLevel(ls, uv + off * px, 0).rgb);
        }
        float local = smoothstep(0.08, 0.3, y - surround * 0.25);
        float w = d >= 0.99999 ? smoothstep(p[2].y + 0.1, p[2].y + 0.6, y) : 0.5 * smoothstep(0.85, 1.05, y) * local;
        acc += float4(c * w, w);
    }
    return acc * 0.25;
}
float4 down(O i) : SV_Target {
    float2 px = p[0].xy;
    float4 s = 0;
    [unroll] for (int k = 0; k < 4; k++) s += t0.SampleLevel(ls, i.uv + (float2(k & 1, k >> 1) - 0.5) * px, 0);
    return s * 0.25;
}
float4 pick(O i) : SV_Target {
    int k = int(i.pos.x);
    uint fw = uint(1 / p[1].x + 0.5), fh = uint(1 / p[1].y + 0.5);
    float4 acc = 0, skyacc = 0, all = 0;
    float2 best = 0.5; float most = 0;
    for (uint y = 0; y < fh; y++) for (uint x = 0; x < fw; x++) {
        float2 uv = (float2(x, y) + 0.5) * p[1].xy;
        if (k == 4) {
            float3 c = scene.SampleLevel(ls, uv, 0).rgb; all += float4(c, 1);
            if (depthtex.SampleLevel(pt, uv, 0) >= 0.99999) skyacc += float4(c, 1);
            continue;
        }
        if (uint(uv.x >= 0.5) != uint(k & 1) || uint(uv.y >= 0.5) != uint(k >> 1)) continue;
        float4 s = t0.Load(int3(x, y, 0)); acc += s;
        if (s.a > most) most = s.a, best = uv;
    }
    if (k == 4) return float4(skyacc.a > all.a * 0.02 ? skyacc.rgb / skyacc.a : all.rgb / max(all.a, 1), skyacc.a / max(all.a, 1));
    float2 centre = 0; float weight = 0;
    for (uint y = 0; y < fh; y++) for (uint x = 0; x < fw; x++) {
        float2 uv = (float2(x, y) + 0.5) * p[1].xy;
        float2 delta = (uv - best) * float2(p[2].w, 1);
        float w = pow(saturate(t0.Load(int3(x, y, 0)).a / max(most, 1e-4)), 4) * saturate(1 - dot(delta, delta) / 0.0225);
        centre += uv * w; weight += w;
    }
    return float4(weight > 0 ? centre / weight : best, most, 0);
}
float4 shafts(O i) : SV_Target {
    float2 uv = i.uv; float3 sh = 0;
    float j = noise(floor(i.pos.xy)) * 0.5;
    [unroll] for (int k = 0; k < 4; k++) {
        float4 L = field.Load(int3(k, 0, 0));
        if (L.z < 0.025) continue;
        float2 distance = (uv - L.xy) * float2(p[2].w, 1);
        float reach = saturate(1 - length(distance) / p[2].z);
        if (reach <= 0) continue;
        float2 delta = (uv - L.xy) * (0.85 / 24), q = uv - delta * j;
        float3 acc = 0; float decay = 1, weight = 0;
        [loop] for (int n = 0; n < 24; n++) {
            q -= delta; acc += scene.SampleLevel(ls, q, 0).rgb * decay;
            weight += decay; decay *= 0.94;
        }
        sh += acc / max(weight, 1e-4) * smoothstep(0.025, 0.15, L.z) * reach * reach;
    }
    return float4(sh * p[0].z * 0.8, depthtex.SampleLevel(pt, uv, 0));
}
float4 depthonly(O i) : SV_Target { return float4(0, 0, 0, depthtex.SampleLevel(pt, i.uv, 0)); }
float4 comp(O i) : SV_Target {
    float2 uv = i.uv; float3 c = t0.SampleLevel(pt, uv, 0).rgb;
    float d = depthtex.SampleLevel(pt, uv, 0); bool sky = d >= 0.99999;
    float3 skyc = field.Load(int3(4, 0, 0)).rgb;
    float3 sh = 0;
    float2 size = p[3].zw, pixel = uv * size - 0.5;
    int2 base = int2(floor(pixel)); float2 f = frac(pixel); float weights = 0;
    [unroll] for (int y = 0; y < 2; y++) [unroll] for (int x = 0; x < 2; x++) {
        int2 at = clamp(base + int2(x, y), int2(0, 0), int2(size) - 1);
        float4 v = scene.Load(int3(at, 0)); bool sampleSky = v.a >= 0.99999;
        float w = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y);
        w *= sampleSky == sky ? exp(-abs(viewz(v.a) - viewz(d)) / max(viewz(d) * 0.1, 0.05)) : 0;
        sh += v.rgb * w; weights += w;
    }
    sh = weights > 1e-4 ? sh / weights : 0;
    float z = sky ? 0 : viewz(d);
    float fog = sky || p[0].w == 0 ? 0 : (1 - exp(-z * p[2].x)) * p[0].w;
    c = lerp(c, skyc, saturate(fog));
    c += sh * (sky ? 0.35 : 1.0) * (1 - saturate(luma(c)) * 0.8);
    return float4(max(c, 0), 1);
}
)";
    uint64_t vs_hash, src_hash, down_hash, pick_hash, shafts_hash, depth_hash, comp_hash;
    if (!Compile(hlsl, "vs", "vs_5_0", vs_hash) || !Compile(hlsl, "sources", "ps_5_0", src_hash) ||
        !Compile(hlsl, "down", "ps_5_0", down_hash) || !Compile(hlsl, "pick", "ps_5_0", pick_hash) ||
        !Compile(hlsl, "shafts", "ps_5_0", shafts_hash) || !Compile(hlsl, "depthonly", "ps_5_0", depth_hash) ||
        !Compile(hlsl, "comp", "ps_5_0", comp_hash)) {
        LOG_FIRST(1, "vulkan: light-shafts shaders did not compile; drawing without shafts");
        return source;
    }
    const uint32_t qw = std::max(1u, t.w >> 2), qh = std::max(1u, t.h >> 2);
    uint32_t fw = qw, fh = qh;
    for (int i = 0; i < 4; i++) fw = std::max(1u, fw >> 1), fh = std::max(1u, fh >> 1);
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for (int i = 0; i < 5; i++) {
        uint32_t w = std::max(1u, t.w >> (2 + i)), h = std::max(1u, t.h >> (2 + i));
        if (shafts_field_[i].img && (shafts_field_[i].w != w || shafts_field_[i].h != h)) DropImage(shafts_field_[i]);
        if (!shafts_field_[i].img && !MakeImage(shafts_field_[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                w, h, 1, 1, 1, usage)) return source;
    }
    if (!shafts_pick_.img && !MakeImage(shafts_pick_, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                                        5, 1, 1, 1, 1, usage)) return source;
    if (shafts_quarter_.img && (shafts_quarter_.w != qw || shafts_quarter_.h != qh)) DropImage(shafts_quarter_);
    if (!shafts_quarter_.img && !MakeImage(shafts_quarter_, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
                                           qw, qh, 1, 1, 1, usage)) return source;
    if (shafts_scene_.img && (shafts_scene_.w != t.w || shafts_scene_.h != t.h)) DropImage(shafts_scene_);
    if (!shafts_scene_.img && !MakeImage(shafts_scene_, VK_IMAGE_TYPE_2D, kBgra, t.w, t.h, 1, 1, 1, usage)) return source;
    VkPipeline ps = PassPipeline({vs_hash, src_hash, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pd = PassPipeline({vs_hash, down_hash, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pk = PassPipeline({vs_hash, pick_hash, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pr = PassPipeline({vs_hash, shafts_hash, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pdepth = PassPipeline({vs_hash, depth_hash, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_UNDEFINED, 0, 0});
    VkPipeline pc = PassPipeline({vs_hash, comp_hash, kBgra, VK_FORMAT_UNDEFINED, 0, 0});
    if (!ps || !pd || !pk || (strength > 0 && !pr) || (strength <= 0 && !pdepth) || !pc) return source;
    const char* wbuf = std::getenv("XBE_WBUFFER");
    float c[16] = {1.0f / t.w, 1.0f / t.h, strength, fog_strength, !(wbuf && *wbuf == '0') ? 1.0f : 0.0f,
                   0, 1.0f / fw, 1.0f / fh, 0.006f, 0.35f, 0.55f, float(t.w) / t.h,
                   0, 0, 1.0f / qw, 1.0f / qh};
    auto pass = [&](VkPipeline pipeline, Image& dst, const VkImageView* views, uint32_t n, float* constants) {
        VkViewport vp{0, 0, float(dst.w), float(dst.h), 0, 1};
        VkRect2D sc{{0, 0}, {dst.w, dst.h}};
        FullScreen(pipeline, {dst.att, VK_NULL_HANDLE, dst.w, dst.h}, vp, sc, constants, 16, views, n);
        End();
        Barrier();
    };
    c[0] = 1.0f / t.w; c[1] = 1.0f / t.h;
    VkImageView sv[2] = {source, depth};
    pass(ps, shafts_field_[0], sv, 2, c);
    for (int i = 1; i < 5; i++) {
        c[0] = 1.0f / shafts_field_[i - 1].w; c[1] = 1.0f / shafts_field_[i - 1].h;
        pass(pd, shafts_field_[i], &shafts_field_[i - 1].view, 1, c);
    }
    c[4] = 1.0f / fw; c[5] = 1.0f / fh;
    VkImageView kv[3] = {shafts_field_[4].view, depth, source};
    pass(pk, shafts_pick_, kv, 3, c);
    c[0] = 1.0f / t.w; c[1] = 1.0f / t.h;
    VkImageView rv[4] = {source, depth, source, shafts_pick_.view};
    if (strength > 0) pass(pr, shafts_quarter_, rv, 4, c);
    else pass(pdepth, shafts_quarter_, rv, 4, c);
    c[0] = 1.0f / t.w; c[1] = 1.0f / t.h;
    VkImageView cv[4] = {source, depth, shafts_quarter_.view, shafts_pick_.view};
    pass(pc, shafts_scene_, cv, 4, c);
    return shafts_scene_.view;
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
    if (!swap_ || w != bb_w_ || h != bb_h_ || swap_vsync_ != app::Get().vsync) {
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
    // at most kFramesInFlight frames ahead of the GPU
    Wait(frame_serial_[frame_slot_]);
    uint32_t index = 0;
    bool acquired;
    {
        PerfScope pa{perf_acquire_};
        acquired = Acquire(index);
    }
    VkImageView frame_source = t ? t->res.view : VK_NULL_HANDLE;
    if (acquired && t && app::Get().smaa) frame_source = SmaaFrame(*t);
    if (acquired) {
        End();
        BuildRtStructures();
        VkCommandBuffer cb = Cmd();
        if (dirty_) Barrier();
        if (t && app::Get().ambient_occlusion && scene_depth_frame_ == presents_ + 1 &&
            scene_color_data_ == t->data && scene_depth_view_ && scene_depth_w_ == t->w && scene_depth_h_ == t->h)
            frame_source = AmbientOcclusion(*t, frame_source, scene_depth_view_);
        if (t && app::Get().depth_of_field && scene_depth_frame_ == presents_ + 1 &&
            scene_color_data_ == t->data && scene_depth_view_ && scene_depth_w_ == t->w && scene_depth_h_ == t->h)
            frame_source = DepthOfField(*t, frame_source, scene_depth_view_);
        if (t && app::Get().temporal_aa && scene_depth_frame_ == presents_ + 1 &&
            scene_color_data_ == t->data && scene_depth_view_ && scene_depth_w_ == t->w && scene_depth_h_ == t->h)
            frame_source = TemporalAA(*t, frame_source, scene_depth_view_);
        if (t && app::Get().motion_blur && scene_depth_frame_ == presents_ + 1 &&
            scene_color_data_ == t->data && scene_depth_view_ && scene_depth_w_ == t->w && scene_depth_h_ == t->h)
            frame_source = MotionBlur(*t, frame_source, scene_depth_view_);
        if (t && (app::Get().light_shafts || app::Get().atmospheric_fog) && scene_depth_frame_ == presents_ + 1 &&
            scene_color_data_ == t->data && scene_depth_view_ && scene_depth_w_ == t->w && scene_depth_h_ == t->h)
            frame_source = LightShafts(*t, frame_source, scene_depth_view_);
        if (t && app::Get().auto_exposure) frame_source = AutoExposure(*t, frame_source);
        VkImageView bloom = t && app::Get().bloom ? Bloom(*t, frame_source) : VK_NULL_HANDLE;
        cb = Cmd();
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
        if (t) BlitFrame(*t, swap_views_[index], frame_source, bloom);
        VkViewport vp{0, 0, float(bb_w_), float(bb_h_), 0, 1};
        vkCmdSetViewport(cb, 0, 1, &vp);
        if (ui_) RenderUi();
        vkCmdEndRendering(cb);
        open_ = false;
        cur_ = {};
        ib.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        ib.dstAccessMask = 0;
        ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(cb, &di);
        Submit(acquire_sem_[frame_slot_], present_sem_[index]);
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &present_sem_[index];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swap_;
        pi.pImageIndices = &index;
        VkResult r = vkQueuePresentKHR(queue_, &pi);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR && r != VK_ERROR_OUT_OF_DATE_KHR)
            LOG_FIRST(8, "vulkan: vkQueuePresentKHR failed: %s", VkResultName(r));
        if ((r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) && camera_frame_ == presents_ + 1) {
            std::memcpy(previous_view_, camera_view_, sizeof(previous_view_));
            std::memcpy(previous_projection_, camera_projection_, sizeof(previous_projection_));
            previous_camera_frame_ = presents_ + 1;
        }
        frame_serial_[frame_slot_] = serial_;
        frame_slot_ = (frame_slot_ + 1) % kFramesInFlight;
    } else {
        Submit();
        frame_serial_[frame_slot_] = serial_;
    }
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
    LOGR("perf: vulkan per frame: draw %.2f ms, present %.2f (acquire %.2f), sync %.2f, queue submits %.2f",
         perf_draw_ / n, perf_present_ / n, perf_acquire_ / n, perf_sync_ / n, perf_submit_ / n);
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
    for (auto it = targets_.begin(); it != targets_.end();) {
        Target& old = it->second;
        if (old.depth) {
            DropImage(old.res);
            DropImage(old.fb);
            DropImage(old.shrunk);
            it = targets_.erase(it);
            continue;
        }
        Target t;
        t.gw = old.gw;
        t.gh = old.gh;
        if (!CreateTarget(t)) {
            DropImage(old.res);
            DropImage(old.fb);
            it = targets_.erase(it);
            continue;
        }
        BlitWhole(old.res.view, t.res, {.alpha = true});
        DropImage(old.res);
        DropImage(old.fb);
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

std::unique_ptr<Backend> CreateBackend() { return CreateVulkanBackend(); }

}  // namespace gpu
