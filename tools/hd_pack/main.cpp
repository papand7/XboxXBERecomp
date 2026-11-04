// hd_pack: a texture pack made on the player's own PC from the textures the game dumped (Graphics Style > Textures >
// Dump the game's textures: mods/textures/dump/<key>.png beside the exe). Each picture is upscaled 4x by Real-ESRGAN
// (realesrgan-ncnn-vulkan, any Vulkan GPU, beside this tool in tools/realesrgan) and written as mods/textures/<key>.png,
// which the runtime's texture pack loads in place of the game's (gpu/texture_pack.cpp). Nothing of the game's leaves
// the PC: the tool ships no game data, and the pack it makes is the player's own copy, not to be shared.
//
//   hd_pack [--scale 2|4] [--pbr] [--force] [--model x4plus|anime] [--dir <the game's folder>]
//
// - Seams: game textures repeat, so each is padded with its own opposite edges before upscaling and cropped after.
// - Alpha: upscaled as a picture of its own (grey), so cut-outs and glass keep their shape.
// - 2x: the 4x result halved (a 2x2 average), sharper than a 2x model's.
// - --pbr: <key>_n.png, a tangent-space normal map (height in alpha) from the upscaled picture's brightness, and
//   <key>_r.png, roughness (white rough, black polished) from its local contrast and brightness. Generated, so an
//   approximation: the runtime reads roughness for reflections.
// - Done pictures are skipped (--force does them again): play more of the game with the dump on, run it again.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

struct Picture {
    uint32_t w = 0, h = 0;
    std::vector<uint32_t> px;  // B8G8R8A8
    uint32_t& at(uint32_t x, uint32_t y) { return px[size_t(y) * w + x]; }
    uint32_t at(uint32_t x, uint32_t y) const { return px[size_t(y) * w + x]; }
};

IWICImagingFactory* Wic() {
    static ComPtr<IWICImagingFactory> f = [] {
        ComPtr<IWICImagingFactory> w;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&w));
        return w;
    }();
    return f.Get();
}

bool Read(const fs::path& path, Picture& p) {
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    HRESULT hr = Wic() ? Wic()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                          WICDecodeMetadataCacheOnLoad, &dec)
                       : E_FAIL;
    if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = Wic()->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr))
        hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                              WICBitmapPaletteTypeCustom);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = conv->GetSize(&w, &h);
    if (SUCCEEDED(hr) && (!w || !h || w > 16384 || h > 16384)) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        p.w = w, p.h = h;
        p.px.resize(size_t(w) * h);
        hr = conv->CopyPixels(nullptr, w * 4, UINT(p.px.size() * 4), reinterpret_cast<BYTE*>(p.px.data()));
    }
    return SUCCEEDED(hr);
}

// through a temporary file: the game never reads a half-written picture
bool Write(const fs::path& path, const Picture& p) {
    fs::path tmp = path;
    tmp += L".tmp";
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr = Wic() ? Wic()->CreateStream(&stream) : E_FAIL;
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(tmp.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = Wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(p.w, p.h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr))
        hr = frame->WritePixels(p.h, p.w * 4, UINT(p.px.size() * 4),
                                reinterpret_cast<BYTE*>(const_cast<uint32_t*>(p.px.data())));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    stream.Reset(), enc.Reset(), frame.Reset();
    std::error_code ec;
    if (SUCCEEDED(hr)) fs::rename(tmp, path, ec);
    if (FAILED(hr) || ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

uint32_t Margin(const Picture& p) { return std::clamp(std::min(p.w, p.h) / 8, 2u, 16u); }

// the picture with its opposite edges around it (a repeating texture's neighbours)
Picture Wrapped(const Picture& p, uint32_t m, bool alpha) {
    Picture o;
    o.w = p.w + 2 * m, o.h = p.h + 2 * m;
    o.px.resize(size_t(o.w) * o.h);
    for (uint32_t y = 0; y < o.h; y++)
        for (uint32_t x = 0; x < o.w; x++) {
            uint32_t c = p.at((x + p.w - m % p.w) % p.w, (y + p.h - m % p.h) % p.h);
            uint32_t a = c >> 24;
            o.at(x, y) = alpha ? 0xFF000000u | a << 16 | a << 8 | a : c | 0xFF000000u;
        }
    return o;
}

Picture Crop(const Picture& p, uint32_t m, uint32_t w, uint32_t h) {
    Picture o;
    o.w = w, o.h = h;
    o.px.resize(size_t(w) * h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) o.at(x, y) = p.at(std::min(x + m, p.w - 1), std::min(y + m, p.h - 1));
    return o;
}

Picture Half(const Picture& p) {
    Picture o;
    o.w = std::max(1u, p.w / 2), o.h = std::max(1u, p.h / 2);
    o.px.resize(size_t(o.w) * o.h);
    for (uint32_t y = 0; y < o.h; y++)
        for (uint32_t x = 0; x < o.w; x++) {
            uint32_t sum[4] = {};
            for (uint32_t dy = 0; dy < 2; dy++)
                for (uint32_t dx = 0; dx < 2; dx++) {
                    uint32_t c = p.at(std::min(x * 2 + dx, p.w - 1), std::min(y * 2 + dy, p.h - 1));
                    for (int k = 0; k < 4; k++) sum[k] += c >> (k * 8) & 0xFF;
                }
            uint32_t c = 0;
            for (int k = 0; k < 4; k++) c |= ((sum[k] + 2) / 4) << (k * 8);
            o.at(x, y) = c;
        }
    return o;
}

float Luma(uint32_t c) {
    return (0.114f * float(c & 0xFF) + 0.587f * float(c >> 8 & 0xFF) + 0.299f * float(c >> 16 & 0xFF)) / 255.0f;
}

// a normal map (height in alpha) and a roughness map from the picture, its edges wrapping
void Pbr(const Picture& p, Picture& normal, Picture& rough) {
    const uint32_t w = p.w, h = p.h;
    std::vector<float> hgt(size_t(w) * h), blur(size_t(w) * h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) hgt[size_t(y) * w + x] = Luma(p.at(x, y));
    auto H = [&](const std::vector<float>& v, int x, int y) {
        return v[size_t((y % int(h) + int(h)) % int(h)) * w + size_t((x % int(w) + int(w)) % int(w))];
    };
    // a small blur: the height without the picture's noise
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            float s = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) s += H(hgt, int(x) + dx, int(y) + dy) * (dx || dy ? 1.0f : 4.0f);
            blur[size_t(y) * w + x] = s / 12;
        }
    normal = rough = Picture{w, h, std::vector<uint32_t>(size_t(w) * h)};
    const float strength = 2.0f * float(std::max(w, h)) / 256.0f;  // steeper on a bigger picture of the same surface
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            int X = int(x), Y = int(y);
            float gx = (H(blur, X + 1, Y - 1) + 2 * H(blur, X + 1, Y) + H(blur, X + 1, Y + 1)) -
                       (H(blur, X - 1, Y - 1) + 2 * H(blur, X - 1, Y) + H(blur, X - 1, Y + 1));
            float gy = (H(blur, X - 1, Y + 1) + 2 * H(blur, X, Y + 1) + H(blur, X + 1, Y + 1)) -
                       (H(blur, X - 1, Y - 1) + 2 * H(blur, X, Y - 1) + H(blur, X + 1, Y - 1));
            float nx = -gx * strength, ny = -gy * strength, nz = 1;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            auto u8 = [](float v) { return uint32_t(std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f); };
            uint32_t height = uint32_t(std::clamp(H(blur, X, Y), 0.0f, 1.0f) * 255.0f + 0.5f);
            normal.at(x, y) = height << 24 | u8(nx / len) << 16 | u8(ny / len) << 8 | u8(nz / len);
            // rough where the surface is busy (its detail scatters light), smoother where it is even and bright
            float contrast = std::fabs(H(hgt, X, Y) - H(blur, X, Y)) * 6;
            float r = std::clamp(0.55f + contrast - 0.25f * H(blur, X, Y), 0.05f, 1.0f);
            uint32_t g = uint32_t(r * 255.0f + 0.5f);
            rough.at(x, y) = 0xFF000000u | g << 16 | g << 8 | g;
        }
}

bool Opaque(const Picture& p) {
    for (uint32_t c : p.px)
        if ((c >> 24) < 255) return false;
    return true;
}

int Run(const std::wstring& cmd) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring line = cmd;
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return int(code);
}

fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    uint32_t scale = 4;
    bool pbr = false, force = false;
    std::wstring model = L"realesrgan-x4plus";
    fs::path dir = ExeDir();
    if (dir.filename() == L"tools") dir = dir.parent_path();  // beside the game as tools\hd_pack.exe
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--scale" && i + 1 < argc) scale = std::wstring(argv[++i]) == L"2" ? 2 : 4;
        else if (a == L"--pbr") pbr = true;
        else if (a == L"--force") force = true;
        else if (a == L"--model" && i + 1 < argc) model = std::wstring(argv[++i]) == L"anime" ? L"realesrgan-x4plus-anime"
                                                                                               : L"realesrgan-x4plus";
        else if (a == L"--dir" && i + 1 < argc) dir = argv[++i];
        else {
            std::printf("hd_pack [--scale 2|4] [--pbr] [--force] [--model x4plus|anime] [--dir <game folder>]\n");
            return 2;
        }
    }
    fs::path dump = dir / "mods" / "textures" / "dump", out = dir / "mods" / "textures";
    fs::path esr = ExeDir() / "realesrgan" / "realesrgan-ncnn-vulkan.exe";
    if (!fs::exists(esr)) esr = dir / "tools" / "realesrgan" / "realesrgan-ncnn-vulkan.exe";
    std::error_code ec;
    if (!fs::exists(esr, ec)) {
        std::printf("hd_pack: the upscaler is missing (tools\\realesrgan\\realesrgan-ncnn-vulkan.exe)\n");
        return 1;
    }
    if (!fs::exists(dump, ec)) {
        std::printf("hd_pack: no dumped textures in %s: turn on Dump the game's textures and play first\n",
                    dump.string().c_str());
        return 1;
    }
    // the pictures not done yet
    std::vector<fs::path> todo;
    for (auto& e : fs::directory_iterator(dump, ec)) {
        std::wstring stem = e.path().stem().wstring();
        if (e.path().extension() != L".png" || stem.size() != 16) continue;
        if (force || !fs::exists(out / e.path().filename(), ec)) todo.push_back(e.path());
    }
    std::sort(todo.begin(), todo.end());
    std::printf("hd_pack: %zu textures to upscale %ux%s\n", todo.size(), scale, pbr ? " with normal and roughness maps" : "");
    if (todo.empty()) return 0;
    fs::path work = dir / "mods" / "textures" / "hd_pack_work", in = work / "in", up = work / "out";
    fs::remove_all(work, ec);
    fs::create_directories(in, ec);
    fs::create_directories(up, ec);
    struct Item {
        fs::path src;
        uint32_t w, h, m;
        bool alpha;
    };
    std::vector<Item> items;
    for (const fs::path& f : todo) {
        Picture p;
        if (!Read(f, p)) {
            std::printf("  %s: not readable, left out\n", f.filename().string().c_str());
            continue;
        }
        uint32_t m = Margin(p);
        bool alpha = !Opaque(p);
        std::wstring stem = f.stem().wstring();
        if (!Write(in / (stem + L".png"), Wrapped(p, m, false)) ||
            (alpha && !Write(in / (stem + L"_a.png"), Wrapped(p, m, true)))) {
            std::printf("  %s: could not be prepared, left out\n", f.filename().string().c_str());
            continue;
        }
        items.push_back({f, p.w, p.h, m, alpha});
    }
    // one run over the folder: the model is loaded once
    std::wstring cmd = L"\"" + esr.wstring() + L"\" -i \"" + in.wstring() + L"\" -o \"" + up.wstring() + L"\" -n " +
                       model + L" -s 4 -f png";
    std::printf("hd_pack: upscaling %zu pictures on the GPU (Real-ESRGAN)...\n", items.size());
    std::fflush(stdout);
    if (int code = Run(cmd); code != 0) {
        std::printf("hd_pack: the upscaler failed (exit %d); is the GPU's Vulkan driver installed?\n", code);
        return 1;
    }
    size_t done = 0;
    for (const Item& it : items) {
        std::wstring stem = it.src.stem().wstring();
        Picture big, a;
        if (!Read(up / (stem + L".png"), big) || (it.alpha && !Read(up / (stem + L"_a.png"), a))) {
            std::printf("  %ls: no upscaled picture\n", stem.c_str());
            continue;
        }
        Picture rgb = Crop(big, it.m * 4, it.w * 4, it.h * 4);
        if (it.alpha) {
            Picture al = Crop(a, it.m * 4, it.w * 4, it.h * 4);
            for (size_t i = 0; i < rgb.px.size(); i++) rgb.px[i] = (rgb.px[i] & 0xFFFFFF) | (al.px[i] & 0xFF) << 24;
        }
        if (scale == 2) rgb = Half(rgb);
        if (!Write(out / (stem + L".png"), rgb)) {
            std::printf("  %ls: could not be written\n", stem.c_str());
            continue;
        }
        if (pbr) {
            Picture n, r;
            Pbr(rgb, n, r);
            Write(out / (stem + L"_n.png"), n);
            Write(out / (stem + L"_r.png"), r);
        }
        done++;
    }
    fs::remove_all(work, ec);
    std::printf("hd_pack: %zu of %zu textures written to %s (the game loads them at its next start)\n", done,
                todo.size(), out.string().c_str());
    std::printf("hd_pack: the pack is made from your copy of the game: keep it to yourself, do not share it\n");
    return done == items.size() ? 0 : 1;
}
