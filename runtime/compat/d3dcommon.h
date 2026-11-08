#pragma once
// off Windows: the primitive topologies (values from Microsoft's DirectX-Headers v1.619.4, include/directx/d3dcommon.h,
// MIT licence, Copyright (C) Microsoft Corporation) and a blob of bytes in ID3DBlob's shape, for the renderer code
// shared with Vulkan (gpu/d3d_common.h). This folder is on the include path only off Windows.
#include <cstddef>

#include <windows.h>

typedef enum D3D_PRIMITIVE_TOPOLOGY {
    D3D_PRIMITIVE_TOPOLOGY_UNDEFINED = 0,
    D3D_PRIMITIVE_TOPOLOGY_POINTLIST = 1,
    D3D_PRIMITIVE_TOPOLOGY_LINELIST = 2,
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP = 3,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN = 6,
} D3D_PRIMITIVE_TOPOLOGY;

// reference counted bytes (d3dcompiler.h's D3DCreateBlob makes one)
struct ID3D10Blob {
    virtual unsigned long AddRef() = 0;
    virtual unsigned long Release() = 0;
    virtual void* GetBufferPointer() = 0;
    virtual size_t GetBufferSize() = 0;

protected:
    virtual ~ID3D10Blob() = default;
};
typedef ID3D10Blob ID3DBlob;
