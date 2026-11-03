// Texture packs: a picture of any size in place of one of the game's textures, keyed by the texture's own contents
// (a hash of level 0's guest bytes, its format and size), so the same texture is found wherever the game loads it.
// mods/textures/<16 hex digits>.png beside the exe, B8G8R8A8 after loading; mods/textures/dump/ gets the game's
// textures under the same names (GraphicsStyle::dump_textures) for upscaling or repainting. The replacement goes
// through SetReplacement, so every renderer that honours those draws it; its levels are made from it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <unordered_set>

#include "app/app.h"
#include "core/log.h"
#include "gpu/shaders.h"
#include "gpu/style.h"
#include "gpu/texture.h"
#include "gpu/texture_pack.h"

using Microsoft::WRL::ComPtr;

namespace gpu {

namespace {

// report level (a player's log shows what the pack did), the first n of each
#define PACK_LOG(n, ...)                    do {                                        static int count_ = 0;                  if (count_++ < (n)) LOGR(__VA_ARGS__);     } while (0)

std::filesystem::path PackDir() { return app::ExeDir() / "mods" / "textures"; }

ComPtr<IWICImagingFactory> Wic() {
    static ComPtr<IWICImagingFactory> wic = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IWICImagingFactory> f;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }();
    return wic;
}

bool ReadPng(const std::filesystem::path& path, std::vector<uint32_t>& px, uint32_t& w, uint32_t& h) {
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    HRESULT hr = wic ? wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)
                     : E_FAIL;
    if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr))
        hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                              WICBitmapPaletteTypeCustom);
    UINT uw = 0, uh = 0;
    if (SUCCEEDED(hr)) hr = conv->GetSize(&uw, &uh);
    // at most 8192 across: 8x a 1024 texture
    if (SUCCEEDED(hr) && (!uw || !uh || uw > 8192 || uh > 8192)) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        px.resize(size_t(uw) * uh);
        hr = conv->CopyPixels(nullptr, uw * 4, UINT(px.size() * 4), reinterpret_cast<BYTE*>(px.data()));
    }
    if (FAILED(hr)) {
        PACK_LOG(16, "texture pack: %s could not be read (%08X)", path.string().c_str(), unsigned(hr));
        return false;
    }
    w = uw;
    h = uh;
    return true;
}

bool WritePng(const std::filesystem::path& path, const std::vector<uint32_t>& px, uint32_t w, uint32_t h) {
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr = wic ? wic->CreateStream(&stream) : E_FAIL;
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr))
        hr = frame->WritePixels(h, w * 4, UINT(px.size() * 4), reinterpret_cast<BYTE*>(const_cast<uint32_t*>(px.data())));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    return SUCCEEDED(hr);
}

std::mutex g_lock;
std::unordered_set<uint64_t> g_files;   // keys with a picture in the pack
std::unordered_set<uint64_t> g_dumped;  // keys written this run
bool g_scanned = false;

void Scan() {
    if (g_scanned) return;
    g_scanned = true;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(PackDir(), ec)) {
        if (e.path().extension() != ".png") continue;
        std::string stem = e.path().stem().string();
        if (stem.size() != 16) continue;
        char* end = nullptr;
        uint64_t key = std::strtoull(stem.c_str(), &end, 16);
        if (end && !*end) g_files.insert(key);
    }
    if (!g_files.empty()) LOGR("texture pack: %zu pictures in %s", g_files.size(), PackDir().string().c_str());
}

}  // namespace

bool WritePngFile(const std::wstring& path, const std::vector<uint32_t>& px, uint32_t w, uint32_t h) {
    return WritePng(path, px, w, h);
}

uint64_t TextureKey(const TextureDesc& d, const uint8_t* guest, uint64_t palette_hash) {
    uint64_t seed = uint64_t(d.format) << 48 ^ uint64_t(d.width) << 24 ^ d.height ^ palette_hash;
    return Hash(guest, LevelSize(d, 0), seed);
}

void TexturePackSeen(const TextureDesc& d, const uint8_t* guest, const uint32_t* palette, uint64_t palette_hash) {
    const GraphicsStyle& st = Style();
    if (!st.texture_pack && !st.dump_textures) return;
    if (d.cube || d.depth > 1 || Format(d.format).depth || palette) return;  // 2D pictures only, not P8 (yet)
    std::lock_guard lock(g_lock);
    Scan();
    if (!st.dump_textures && g_files.empty()) return;
    uint64_t key = TextureKey(d, guest, palette_hash);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.png", (unsigned long long)key);
    if (st.dump_textures && g_dumped.insert(key).second) {
        std::error_code ec;
        std::filesystem::path dir = PackDir() / "dump";
        std::filesystem::create_directories(dir, ec);
        std::vector<uint32_t> px;
        if (!std::filesystem::exists(dir / name, ec) && DecodeBgra(d, 0, guest, px) &&
            px.size() >= size_t(d.width) * d.height) {
            if (WritePng(dir / name, px, d.width, d.height)) PACK_LOG(4, "texture pack: dumping to %s", dir.string().c_str());
        }
    }
    if (st.texture_pack && g_files.count(key) && !FindReplacement(d.data)) {
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        if (ReadPng(PackDir() / name, px, w, h)) {
            SetReplacement(d.data, LevelSize(d, 0), w, h, std::move(px));
            PACK_LOG(32, "texture pack: %s in place of %08X (%ux%u, %ux%u)", name, d.data, d.width, d.height, w, h);
        }
    }
}

}  // namespace gpu
