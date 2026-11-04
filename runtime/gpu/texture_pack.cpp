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

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <unordered_map>
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
std::unordered_map<uint64_t, float> g_rough;
// pictures' materials read at load, taken by the renderer (TakePackMaterial): R normal x, G normal y, B roughness,
// A height, level 0 first
struct Material {
    uint32_t w, h;
    std::vector<std::vector<uint32_t>> levels;
};
std::unordered_map<uint64_t, Material> g_materials;

// a level half the size, each texel the average of four
std::vector<uint32_t> Halve(const std::vector<uint32_t>& px, uint32_t w, uint32_t h) {
    uint32_t hw = std::max(1u, w / 2), hh = std::max(1u, h / 2);
    std::vector<uint32_t> o(size_t(hw) * hh);
    for (uint32_t y = 0; y < hh; y++)
        for (uint32_t x = 0; x < hw; x++) {
            uint32_t sum[4] = {};
            for (uint32_t dy = 0; dy < 2; dy++)
                for (uint32_t dx = 0; dx < 2; dx++) {
                    uint32_t c = px[size_t(std::min(y * 2 + dy, h - 1)) * w + std::min(x * 2 + dx, w - 1)];
                    for (int k = 0; k < 4; k++) sum[k] += c >> (k * 8) & 0xFF;
                }
            uint32_t c = 0;
            for (int k = 0; k < 4; k++) c |= ((sum[k] + 2) / 4) << (k * 8);
            o[size_t(y) * hw + x] = c;
        }
    return o;
}  // a pack picture's mean roughness, from its <key>_r.png (hd_pack --pbr)
// pictures by where they sit: one whose contents change again within a second (a movie's frames, an animated
// screen) is no texture to repaint and is not dumped; a level's textures loaded where another's were are
struct Seen {
    uint64_t key;
    std::chrono::steady_clock::time_point changed;
};
std::unordered_map<uint32_t, Seen> g_seen_at;
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

bool TakePackMaterial(uint64_t key, std::vector<std::vector<uint32_t>>& levels, uint32_t& w, uint32_t& h) {
    std::lock_guard lock(g_lock);
    auto it = g_materials.find(key);
    if (it == g_materials.end()) return false;
    w = it->second.w, h = it->second.h;
    levels = std::move(it->second.levels);
    g_materials.erase(it);
    return true;
}

float PackRoughness(uint64_t key) {
    std::lock_guard lock(g_lock);
    auto it = g_rough.find(key);
    return it == g_rough.end() ? -1.0f : it->second;
}

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
    bool moving = false;
    if (st.dump_textures) {
        auto now = std::chrono::steady_clock::now();
        auto [it, fresh] = g_seen_at.try_emplace(d.data, Seen{key, now - std::chrono::seconds(10)});
        if (!fresh && it->second.key != key) {
            moving = now - it->second.changed < std::chrono::seconds(1);
            if (moving) PACK_LOG(8, "texture pack: the picture at %08X changes as it plays (a movie?), not dumped", d.data);
            it->second = {key, now};
        }
    }
    if (st.dump_textures && !moving && g_dumped.insert(key).second) {
        std::error_code ec;
        std::filesystem::path dir = PackDir() / "dump";
        std::filesystem::create_directories(dir, ec);
        std::vector<uint32_t> px;
        if (!std::filesystem::exists(dir / name, ec) && DecodeBgra(d, 0, guest, px) &&
            px.size() >= size_t(d.width) * d.height) {
            // through a temporary file: a half-written picture would stop every later dump of it
            std::filesystem::path tmp = dir / (std::string(name) + ".tmp");
            bool ok = WritePng(tmp, px, d.width, d.height);
            if (ok) std::filesystem::rename(tmp, dir / name, ec);
            if (ok && !ec) PACK_LOG(4, "texture pack: dumping to %s", dir.string().c_str());
            else std::filesystem::remove(tmp, ec);
        }
    }
    if (st.texture_pack && g_files.count(key) && !FindReplacement(d.data)) {
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        if (ReadPng(PackDir() / name, px, w, h)) {
            SetReplacement(d.data, LevelSize(d, 0), w, h, std::move(px));
            char rname[40];
            std::snprintf(rname, sizeof(rname), "%016llx_r.png", (unsigned long long)key);
            std::vector<uint32_t> rp;
            uint32_t rw = 0, rh = 0;
            std::error_code ec;
            if (std::filesystem::exists(PackDir() / rname, ec) && ReadPng(PackDir() / rname, rp, rw, rh) && !rp.empty()) {
                double sum = 0;
                for (uint32_t c : rp) sum += (c >> 8 & 0xFF) / 255.0;
                g_rough[key] = float(sum / double(rp.size()));
            }
            // the material: the normal map with the roughness in its blue (its z is made again from x and y)
            char nname[40];
            std::snprintf(nname, sizeof(nname), "%016llx_n.png", (unsigned long long)key);
            std::vector<uint32_t> np;
            uint32_t nw = 0, nh = 0;
            if (Style().materials && std::filesystem::exists(PackDir() / nname, ec) &&
                ReadPng(PackDir() / nname, np, nw, nh) && !np.empty()) {
                bool rough = rw == nw && rh == nh && rp.size() == np.size();
                for (size_t i = 0; i < np.size(); i++)
                    np[i] = (np[i] & 0xFFFFFF00u) | (rough ? rp[i] & 0xFF : 0x80u);
                Material m{nw, nh, {}};
                m.levels.push_back(std::move(np));
                for (uint32_t lw = nw, lh = nh; lw > 1 || lh > 1;) {
                    m.levels.push_back(Halve(m.levels.back(), lw, lh));
                    lw = std::max(1u, lw / 2), lh = std::max(1u, lh / 2);
                }
                g_materials[key] = std::move(m);
            }
            PACK_LOG(32, "texture pack: %s in place of %08X (%ux%u, %ux%u)", name, d.data, d.width, d.height, w, h);
        }
    }
}

}  // namespace gpu
