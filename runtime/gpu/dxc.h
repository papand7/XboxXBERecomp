#pragma once
// DXC, the HLSL compiler the Vulkan renderer turns its shaders into SPIR-V with: dxcompiler.dll (Windows) or
// libdxcompiler.so (elsewhere) loaded at run time from beside the exe. Its headers stay in gpu/dxc.cpp, so the
// renderer needs neither them nor the COM names they bring.
#include <cstdint>
#include <string>
#include <vector>

namespace gpu::dxc {

// the compiler loaded (tried once; false logged)
bool Load();
// source to SPIR-V with DXC's command line arguments; false: errors has what DXC said
bool Compile(const std::string& source, const std::vector<std::string>& args, std::vector<uint32_t>& spirv,
             std::string& errors);

}  // namespace gpu::dxc
