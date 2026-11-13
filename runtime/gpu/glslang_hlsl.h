#pragma once
// HLSL to SPIR-V through glslang (gpu/glslang_hlsl.cpp): DXC's stand-in on macOS, and on the other platforms with
// XBE_SHADER_COMPILER=glslang (a test). Shader model 5 and 6.0 profiles; 6.5 (ray tracing) stays DXC's.
#include <cstdint>
#include <string>
#include <vector>

namespace gpu {

bool GlslangAvailable();  // built in (the runtime links glslang)
bool GlslangWanted();     // to be used instead of DXC (macOS; XBE_SHADER_COMPILER=glslang)
bool CompileHlslGlslang(const std::string& source, const char* entry, const char* profile, std::vector<uint32_t>& spirv,
                        std::string& errors);

}  // namespace gpu
