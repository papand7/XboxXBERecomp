// picture files through WIC on Windows, stb_image elsewhere (see image.h)
#include "app/image.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace app {

namespace {
// one factory for every thread (the multithreaded apartment)
ComPtr<IWICImagingFactory> Wic() {
    static ComPtr<IWICImagingFactory> wic = [] {
        ComPtr<IWICImagingFactory> f;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        return f;
    }();
    return wic;
}
}  // namespace

bool DecodeImage(const std::filesystem::path& path, std::vector<uint32_t>& bgra, uint32_t& width, uint32_t& height,
                 bool premultiplied, uint32_t max_side, uint32_t* error) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    HRESULT hr = wic ? wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)
                     : E_FAIL;
    if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr))
        hr = conv->Initialize(frame.Get(), premultiplied ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = conv->GetSize(&w, &h);
    if (SUCCEEDED(hr) && (!w || !h || (max_side && (w > max_side || h > max_side)))) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) {
        bgra.resize(size_t(w) * h);
        hr = conv->CopyPixels(nullptr, w * 4, UINT(bgra.size() * 4), reinterpret_cast<BYTE*>(bgra.data()));
    }
    if (FAILED(hr)) {
        if (error) *error = uint32_t(hr);
        bgra.clear();
        return false;
    }
    width = w;
    height = h;
    return true;
}

bool EncodePng(const std::filesystem::path& path, const std::vector<uint32_t>& bgra, uint32_t width, uint32_t height) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
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
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr))
        hr = frame->WritePixels(height, width * 4, UINT(bgra.size() * 4),
                                reinterpret_cast<BYTE*>(const_cast<uint32_t*>(bgra.data())));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    return SUCCEEDED(hr);
}

bool EncodeJpeg(const std::filesystem::path& path, const std::vector<uint32_t>& bgra, uint32_t width, uint32_t height,
                int quality) {
    if (bgra.size() < size_t(width) * height) return false;
    std::vector<uint8_t> bgr(size_t(width) * height * 3);
    for (size_t i = 0; i < size_t(width) * height; i++) std::memcpy(&bgr[i * 3], &bgra[i], 3);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    HRESULT hr = wic ? wic->CreateStream(&stream) : E_FAIL;
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc);
    if (SUCCEEDED(hr)) hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr) && props) {
        PROPBAG2 name{};
        name.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = std::clamp(quality, 1, 100) / 100.0f;
        props->Write(1, &name, &v);
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr) && fmt != GUID_WICPixelFormat24bppBGR) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = frame->WritePixels(height, width * 3, UINT(bgr.size()), bgr.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = enc->Commit();
    return SUCCEEDED(hr);
}

}  // namespace app

#else

// stb_image and stb_image_write (public domain / MIT, Sean Barrett), fetched by CMake's Linux branch
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_NO_STDIO_FILENAME_HELPERS
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstdio>
#include <cstring>

namespace app {

bool DecodeImage(const std::filesystem::path& path, std::vector<uint32_t>& bgra, uint32_t& width, uint32_t& height,
                 bool premultiplied, uint32_t max_side, uint32_t* error) {
    bgra.clear();
    int w = 0, h = 0, n = 0;
    stbi_uc* px = nullptr;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        px = stbi_load_from_file(f, &w, &h, &n, 4);
        std::fclose(f);
    }
    if (!px || w <= 0 || h <= 0 || (max_side && (uint32_t(w) > max_side || uint32_t(h) > max_side))) {
        if (px) stbi_image_free(px);
        if (error) *error = px ? 0x80070057u : 0x80004005u;  // E_INVALIDARG for a size refused, else E_FAIL
        return false;
    }
    bgra.resize(size_t(w) * h);
    for (size_t i = 0; i < bgra.size(); i++) {
        uint32_t r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2], a = px[i * 4 + 3];
        if (premultiplied && a != 255) {
            r = (r * a + 127) / 255;
            g = (g * a + 127) / 255;
            b = (b * a + 127) / 255;
        }
        bgra[i] = b | g << 8 | r << 16 | a << 24;
    }
    stbi_image_free(px);
    width = uint32_t(w);
    height = uint32_t(h);
    return true;
}

bool EncodePng(const std::filesystem::path& path, const std::vector<uint32_t>& bgra, uint32_t width, uint32_t height) {
    if (bgra.size() < size_t(width) * height) return false;
    std::vector<uint8_t> rgba(size_t(width) * height * 4);
    for (size_t i = 0; i < size_t(width) * height; i++) {
        uint32_t v = bgra[i];
        rgba[i * 4] = uint8_t(v >> 16);
        rgba[i * 4 + 1] = uint8_t(v >> 8);
        rgba[i * 4 + 2] = uint8_t(v);
        rgba[i * 4 + 3] = uint8_t(v >> 24);
    }
    return stbi_write_png(path.c_str(), int(width), int(height), 4, rgba.data(), int(width * 4)) != 0;
}

bool EncodeJpeg(const std::filesystem::path& path, const std::vector<uint32_t>& bgra, uint32_t width, uint32_t height,
                int quality) {
    if (bgra.size() < size_t(width) * height) return false;
    std::vector<uint8_t> rgb(size_t(width) * height * 3);
    for (size_t i = 0; i < size_t(width) * height; i++) {
        uint32_t v = bgra[i];
        rgb[i * 3] = uint8_t(v >> 16), rgb[i * 3 + 1] = uint8_t(v >> 8), rgb[i * 3 + 2] = uint8_t(v);
    }
    return stbi_write_jpg(path.c_str(), int(width), int(height), 3, rgb.data(), std::clamp(quality, 1, 100)) != 0;
}

}  // namespace app

#endif
