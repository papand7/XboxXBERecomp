// NVIDIA DLSS Super Resolution for the Vulkan renderer (gpu/upscale.h), through NVIDIA's NGX SDK: its headers and
// static loader library fetched by CMake (XBE_DLSS, NVIDIA RTX SDKs licence, sdk/THIRD_PARTY.txt), the model in
// nvngx_dlss.dll beside the exe, run by the NVIDIA driver on RTX GPUs. Without the SDK (XBE_DLSS off or its download
// failed) this file builds the stubs below and DLSS shows as not available.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <atomic>
#include <chrono>

#include "core/log.h"
#include "gpu/upscale.h"

#if XBE_DLSS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <iterator>

#include "nvsdk_ngx_helpers_vk.h"
#include "nvsdk_ngx_vk.h"
#endif

namespace gpu {

namespace {
std::atomic<bool> g_dlss_ok{false};
}  // namespace

bool DlssAvailable() { return g_dlss_ok.load(); }

#if !XBE_DLSS

bool DlssBuiltIn() { return false; }
std::vector<std::string> DlssInstanceExtensions() { return {}; }
std::vector<std::string> DlssDeviceExtensions(VkInstance, VkPhysicalDevice) { return {}; }
bool Dlss::Init(VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr) {
    return false;
}
bool Dlss::Dispatch(const FsrDispatch&, uint32_t, uint32_t, int) { return false; }
void Dlss::Destroy() {}
void Dlss::Shutdown() {}

#else

namespace {
// NGX asks for a GUID-like project id where NVIDIA gave the application none (NVSDK_NGX_ENGINE_TYPE_CUSTOM)
constexpr char kProjectId[] = "5f0c2d7e-8a41-4b3e-9d6a-1c7e0f3b2a59";
constexpr char kEngineVersion[] = "1.0";

// the exe's folder: where nvngx_dlss.dll is looked for, and NGX's data path
const std::wstring& ExeDir() {
    static const std::wstring dir = [] {
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
        std::wstring s(buf, n);
        size_t slash = s.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring(L".") : s.substr(0, slash);
    }();
    return dir;
}

void NVSDK_CONV NgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    LOG_FIRST(64, "dlss: ngx: %s", message);
}

// NGX's own log through ours when XBE_DLSS_LOG is set; else none, and no files (NGX refuses to start with no sink
// at all: the callback is always given)
const NVSDK_NGX_FeatureCommonInfo* CommonInfo() {
    static const wchar_t* paths[1];
    static NVSDK_NGX_FeatureCommonInfo info = [] {
        NVSDK_NGX_FeatureCommonInfo i{};
        paths[0] = ExeDir().c_str();
        i.PathListInfo.Path = paths;
        i.PathListInfo.Length = 1;
        const char* e = std::getenv("XBE_DLSS_LOG");
        const bool on = e && *e && *e != '0';
        i.LoggingInfo.LoggingCallback = NgxLog;
        i.LoggingInfo.MinimumLoggingLevel = on ? NVSDK_NGX_LOGGING_LEVEL_ON : NVSDK_NGX_LOGGING_LEVEL_OFF;
        i.LoggingInfo.DisableOtherLoggingSinks = true;
        return i;
    }();
    return &info;
}

NVSDK_NGX_FeatureDiscoveryInfo Discovery() {
    NVSDK_NGX_FeatureDiscoveryInfo d{};
    d.SDKVersion = NVSDK_NGX_Version_API;
    d.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    d.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d.Identifier.v.ProjectDesc.ProjectId = kProjectId;
    d.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    d.Identifier.v.ProjectDesc.EngineVersion = kEngineVersion;
    d.ApplicationDataPath = ExeDir().c_str();
    d.FeatureInfo = CommonInfo();
    return d;
}

std::vector<std::string> Names(uint32_t n, const VkExtensionProperties* p) {
    std::vector<std::string> out;
    for (uint32_t i = 0; p && i < n; i++) out.emplace_back(p[i].extensionName);
    return out;
}

NVSDK_NGX_PerfQuality_Value PerfQuality(int quality) {
    switch (quality) {
        case kUpscaleQuality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case kUpscaleBalanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
        case kUpscalePerformance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case kUpscaleUltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        default: return NVSDK_NGX_PerfQuality_Value_DLAA;
    }
}

const char* QualityName(int quality) {
    static const char* names[] = {"DLAA", "Quality", "Balanced", "Performance", "Ultra Performance"};
    return quality >= 0 && quality < int(std::size(names)) ? names[quality] : "?";
}

NVSDK_NGX_Resource_VK Res(const FsrImage& im, bool write) {
    return NVSDK_NGX_Create_ImageView_Resource_VK(im.view, im.img, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, im.fmt,
                                                  im.w, im.h, write);
}
}  // namespace

bool DlssBuiltIn() { return true; }

std::vector<std::string> DlssInstanceExtensions() {
    NVSDK_NGX_FeatureDiscoveryInfo d = Discovery();
    uint32_t n = 0;
    VkExtensionProperties* p = nullptr;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d, &n, &p))) return {};
    return Names(n, p);
}

std::vector<std::string> DlssDeviceExtensions(VkInstance instance, VkPhysicalDevice pd) {
    NVSDK_NGX_FeatureDiscoveryInfo d = Discovery();
    uint32_t n = 0;
    VkExtensionProperties* p = nullptr;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, pd, &d, &n, &p))) return {};
    return Names(n, p);
}

bool Dlss::Init(VkInstance instance, VkPhysicalDevice pd, VkDevice dev, PFN_vkGetInstanceProcAddr gipa,
                PFN_vkGetDeviceProcAddr gdpa) {
    dev_ = dev;
    gdpa_ = gdpa;
    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
    // the feature's own check first: the GPU (RTX), the driver and the OS, before NGX is set up on the device
    NVSDK_NGX_FeatureDiscoveryInfo disc = Discovery();
    NVSDK_NGX_FeatureRequirement req{};
    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, pd, &disc, &req);
    if (NVSDK_NGX_FAILED(r) || req.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported) {
        LOGR("dlss: not supported on this GPU, driver or OS (result %08X, flags %d: 2 driver, 4 GPU, 8 OS); not "
             "available", unsigned(r), int(req.FeatureSupported));
        return false;
    }
    r = NVSDK_NGX_VULKAN_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion,
                                             ExeDir().c_str(), instance, pd, dev, gipa, gdpa, CommonInfo());
    if (NVSDK_NGX_FAILED(r)) {
        LOGR("dlss: NGX did not start (%08X); not available", unsigned(r));
        return false;
    }
    NVSDK_NGX_Parameter* params = nullptr;
    r = NVSDK_NGX_VULKAN_GetCapabilityParameters(&params);
    if (NVSDK_NGX_FAILED(r) || !params) {
        LOGR("dlss: no NGX capability parameters (%08X); not available", unsigned(r));
        NVSDK_NGX_VULKAN_Shutdown1(dev);
        return false;
    }
    params_ = params;
    int available = 0, needs_driver = 0;
    unsigned int drv_major = 0, drv_minor = 0;
    NVSDK_NGX_Parameter_GetI(params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    NVSDK_NGX_Parameter_GetI(params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
    if (!available) {
        int init_result = 0;
        NVSDK_NGX_Parameter_GetI(params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
        if (needs_driver) {
            NVSDK_NGX_Parameter_GetUI(params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &drv_major);
            NVSDK_NGX_Parameter_GetUI(params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &drv_minor);
            LOGR("dlss: the NVIDIA driver is too old (%u.%u or later needed); not available", drv_major, drv_minor);
        } else {
            LOGR("dlss: not available (nvngx_dlss.dll missing beside the exe, or the GPU lacks it: %08X)",
                 unsigned(init_result));
        }
        Shutdown();
        return false;
    }
    g_dlss_ok = true;
    LOGR("dlss: NVIDIA DLSS available (NGX started in %.0f ms)", ms());
    return true;
}

bool Dlss::Ensure(VkCommandBuffer cmd, uint32_t rw, uint32_t rh, uint32_t dw, uint32_t dh, int quality) {
    if (handle_ && rw == rw_ && rh == rh_ && dw == dw_ && dh == dh_ && quality == quality_) return true;
    Destroy();
    if (failed_ || !params_) return false;
    auto* params = static_cast<NVSDK_NGX_Parameter*>(params_);
    NVSDK_NGX_DLSS_Create_Params cp{};
    cp.Feature.InWidth = rw;
    cp.Feature.InHeight = rh;
    cp.Feature.InTargetWidth = dw;
    cp.Feature.InTargetHeight = dh;
    cp.Feature.InPerfQualityValue = PerfQuality(quality);
    // LDR colour (after the tone mapping), motion vectors at the render size and not jittered, depth 0 near
    cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    NVSDK_NGX_Handle* h = nullptr;
    NVSDK_NGX_Result r = NGX_VULKAN_CREATE_DLSS_EXT1(dev_, cmd, 1, 1, &h, params, &cp);
    if (NVSDK_NGX_FAILED(r) || !h) {
        LOGE("dlss: the feature could not be made (%08X); upscaling with FSR 2", unsigned(r));
        failed_ = true;
        g_dlss_ok = false;
        return false;
    }
    handle_ = h;
    rw_ = rw, rh_ = rh, dw_ = dw, dh_ = dh, quality_ = quality;
    LOGR("dlss: DLSS %s %ux%u -> %ux%u", QualityName(quality), rw, rh, dw, dh);
    return true;
}

bool Dlss::Dispatch(const FsrDispatch& d, uint32_t display_w, uint32_t display_h, int quality) {
    if (!Ensure(d.cmd, d.color.w, d.color.h, display_w, display_h, quality)) return false;
    static PFN_vkCmdPipelineBarrier barrier =
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(gdpa_(dev_, "vkCmdPipelineBarrier"));
    if (!barrier) return false;
    // the inputs read by NGX's shaders in SHADER_READ_ONLY_OPTIMAL, the output written in GENERAL; the renderer keeps
    // them in GENERAL, back there afterwards
    const FsrImage* ins[4] = {&d.color, &d.depth, &d.motion, &d.reactive};
    const uint32_t n_in = d.reactive.img ? 4 : 3;
    auto move = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags ss,
                    VkPipelineStageFlags ds) {
        VkImageMemoryBarrier ib[4]{};
        for (uint32_t i = 0; i < n_in; i++) {
            ib[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ib[i].srcAccessMask = src;
            ib[i].dstAccessMask = dst;
            ib[i].oldLayout = from;
            ib[i].newLayout = to;
            ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib[i].image = ins[i]->img;
            ib[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }
        barrier(d.cmd, ss, ds, 0, 0, nullptr, 0, nullptr, n_in, ib);
    };
    move(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
         VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    NVSDK_NGX_Resource_VK color = Res(d.color, false), depth = Res(d.depth, false), motion = Res(d.motion, false),
                          output = Res(d.output, true), reactive = Res(d.reactive, false);
    NVSDK_NGX_VK_DLSS_Eval_Params ep{};
    ep.Feature.pInColor = &color;
    ep.Feature.pInOutput = &output;
    ep.pInDepth = &depth;
    ep.pInMotionVectors = &motion;
    if (d.reactive.img) ep.pInBiasCurrentColorMask = &reactive;
    // the same offsets FSR 2 takes (render pixels); XBE_DLSS_JITTER=x, y or xy turns that axis round (a test)
    static const char* flip = std::getenv("XBE_DLSS_JITTER");
    const bool fx = flip && std::strchr(flip, 'x'), fy = flip && std::strchr(flip, 'y');
    ep.InJitterOffsetX = fx ? -d.jitter_x : d.jitter_x;
    ep.InJitterOffsetY = fy ? -d.jitter_y : d.jitter_y;
    ep.InRenderSubrectDimensions = {d.render_w, d.render_h};
    ep.InReset = d.reset ? 1 : 0;
    ep.InMVScaleX = float(d.render_w);
    ep.InMVScaleY = float(d.render_h);
    ep.InFrameTimeDeltaInMsec = d.frame_ms;
    NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT(d.cmd, static_cast<NVSDK_NGX_Handle*>(handle_),
                                                      static_cast<NVSDK_NGX_Parameter*>(params_), &ep);
    move(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
         VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    if (NVSDK_NGX_FAILED(r)) {
        LOG_FIRST(4, "dlss: evaluation failed (%08X)", unsigned(r));
        return false;
    }
    return true;
}

void Dlss::Destroy() {
    if (handle_) NVSDK_NGX_VULKAN_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(handle_));
    handle_ = nullptr;
    rw_ = rh_ = dw_ = dh_ = 0;
    quality_ = -1;
}

void Dlss::Shutdown() {
    Destroy();
    if (params_) NVSDK_NGX_VULKAN_DestroyParameters(static_cast<NVSDK_NGX_Parameter*>(params_));
    if (params_ && dev_) NVSDK_NGX_VULKAN_Shutdown1(dev_);
    params_ = nullptr;
    g_dlss_ok = false;
}

#endif  // XBE_DLSS

}  // namespace gpu
