// DXC loaded at run time (gpu/dxc.h). Its own headers: on Windows the SDK's COM, elsewhere DXC's WinAdapter.h
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#else
#include <dlfcn.h>
#endif
#include <dxcapi.h>

#include <cstring>
#include <filesystem>
#include <mutex>

#include "app/app.h"
#include "core/log.h"
#include "gpu/dxc.h"

namespace gpu::dxc {

namespace {

#ifdef _WIN32
template <class T>
using Ptr = Microsoft::WRL::ComPtr<T>;
#else
template <class T>
using Ptr = CComPtr<T>;
#endif

#ifdef _WIN32
constexpr const char* kLibrary = "dxcompiler.dll";
#else
constexpr const char* kLibrary = "libdxcompiler.so";
#endif

std::mutex g_lock;
Ptr<IDxcCompiler3> g_compiler;
bool g_tried = false;

DxcCreateInstanceProc FindCreate() {
    std::filesystem::path beside = app::ExeDir() / kLibrary;
#ifdef _WIN32
    HMODULE m = LoadLibraryW(beside.wstring().c_str());
    if (!m) m = LoadLibraryA(kLibrary);
    return m ? reinterpret_cast<DxcCreateInstanceProc>(reinterpret_cast<void*>(GetProcAddress(m, "DxcCreateInstance")))
             : nullptr;
#else
    void* m = dlopen(beside.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m) m = dlopen(kLibrary, RTLD_NOW | RTLD_LOCAL);
    return m ? reinterpret_cast<DxcCreateInstanceProc>(dlsym(m, "DxcCreateInstance")) : nullptr;
#endif
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace

bool Load() {
    std::lock_guard lock(g_lock);
    if (g_tried) return g_compiler != nullptr;
    g_tried = true;
    DxcCreateInstanceProc create = FindCreate();
    if (!create) {
        LOGE("vulkan: %s not found beside the exe; the shaders cannot be compiled", kLibrary);
        return false;
    }
    Ptr<IDxcCompiler3> c;
    if (FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&c))) || !c) {
        LOGE("vulkan: %s has no compiler", kLibrary);
        return false;
    }
    g_compiler = c;
    return true;
}

bool Compile(const std::string& source, const std::vector<std::string>& args, std::vector<uint32_t>& spirv,
             std::string& errors) {
    spirv.clear();
    errors.clear();
    if (!Load()) {
        errors = "no compiler";
        return false;
    }
    std::vector<std::wstring> wide;
    wide.reserve(args.size());
    for (const std::string& a : args) wide.push_back(Wide(a));
    std::vector<LPCWSTR> argv;
    for (const std::wstring& w : wide) argv.push_back(w.c_str());
    DxcBuffer buf{source.data(), source.size(), DXC_CP_UTF8};
    Ptr<IDxcResult> res;
    HRESULT status = E_FAIL;
    if (SUCCEEDED(g_compiler->Compile(&buf, argv.data(), UINT32(argv.size()), nullptr, IID_PPV_ARGS(&res))) && res)
        res->GetStatus(&status);
    Ptr<IDxcBlob> obj;
    if (SUCCEEDED(status)) res->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&obj), nullptr);
    if (FAILED(status) || !obj || !obj->GetBufferSize()) {
        Ptr<IDxcBlobUtf8> err;
        if (res) res->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&err), nullptr);
        errors = err && err->GetStringLength() ? std::string(err->GetStringPointer(), err->GetStringLength()) : "?";
        return false;
    }
    spirv.resize(obj->GetBufferSize() / 4);
    std::memcpy(spirv.data(), obj->GetBufferPointer(), spirv.size() * 4);
    return true;
}

}  // namespace gpu::dxc
