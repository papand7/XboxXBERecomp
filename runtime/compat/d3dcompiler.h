#pragma once
// off Windows: D3DCreateBlob for the shader cache (gpu/d3d_common.cpp), nothing of the HLSL compiler. This folder is on
// the include path only off Windows.
#include <atomic>
#include <cstdint>
#include <new>
#include <vector>

#include <d3dcommon.h>

inline HRESULT D3DCreateBlob(size_t size, ID3DBlob** out) {
    struct Blob final : ID3DBlob {
        std::atomic<unsigned long> refs{1};
        std::vector<uint8_t> bytes;
        unsigned long AddRef() override { return ++refs; }
        unsigned long Release() override {
            unsigned long n = --refs;
            if (!n) delete this;
            return n;
        }
        void* GetBufferPointer() override { return bytes.data(); }
        size_t GetBufferSize() override { return bytes.size(); }
    };
    if (!out) return E_FAIL;
    Blob* b = new (std::nothrow) Blob;
    if (!b) return E_FAIL;
    b->bytes.resize(size);
    *out = b;
    return S_OK;
}
