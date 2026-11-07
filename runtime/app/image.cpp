// picture files through WIC on Windows; elsewhere a stand in until stb_image comes (see image.h)
#include "app/image.h"

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

}  // namespace app

#else

namespace app {

// TODO(Linux port phase 2): stb_image / stb_image_write, fetched by the Linux CMake branch. Until then no splash, no
// texture pack and no texture dumps off Windows.
bool DecodeImage(const std::filesystem::path&, std::vector<uint32_t>& bgra, uint32_t&, uint32_t&, bool, uint32_t,
                 uint32_t* error) {
    bgra.clear();
    if (error) *error = 0x80004001u;  // E_NOTIMPL
    return false;
}

bool EncodePng(const std::filesystem::path&, const std::vector<uint32_t>&, uint32_t, uint32_t) { return false; }

}  // namespace app

#endif
