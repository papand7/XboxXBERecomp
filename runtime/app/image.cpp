// PNG and other image files for the Windows build (see image.h).
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
namespace app {
bool DecodeImage(const std::filesystem::path&, std::vector<uint32_t>& bgra, uint32_t&, uint32_t&, bool, uint32_t, uint32_t* error) {
    bgra.clear();
    if (error) *error = 0x80004001u;
    return false;
}
bool EncodePng(const std::filesystem::path&, const std::vector<uint32_t>&, uint32_t, uint32_t) { return false; }
bool EncodeJpeg(const std::filesystem::path&, const std::vector<uint32_t>&, uint32_t, uint32_t, int) { return false; }
}  // namespace app
#endif