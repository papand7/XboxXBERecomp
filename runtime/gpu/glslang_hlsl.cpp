// HLSL to SPIR-V through glslang's HLSL front end (BSD/Apache, Khronos), the alternative to DXC: macOS has no DXC
// build, and glslang is built from source with the runtime. The options mirror DXC's in VulkanBackend::Compile:
// register shifts (b 0, t 2, s 6, u 10), DX packing offsets, position.w for pixel shaders, the entry point named
// "main". Shader model 6.5 (ray queries) is DXC's alone: MoltenVK has no ray tracing.
#include "gpu/glslang_hlsl.h"

#ifdef XBE_GLSLANG
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <mutex>
#endif

#include <cstdlib>
#include <cstring>

namespace gpu {

bool GlslangAvailable() {
#ifdef XBE_GLSLANG
    return true;
#else
    return false;
#endif
}

bool GlslangWanted() {
#if defined(XBE_GLSLANG) && defined(__APPLE__)
    return true;  // the Mac has no DXC
#elif defined(XBE_GLSLANG)
    static const bool on = [] {
        const char* e = std::getenv("XBE_SHADER_COMPILER");
        return e && !std::strcmp(e, "glslang");
    }();
    return on;
#else
    return false;
#endif
}

bool CompileHlslGlslang(const std::string& source, const char* entry, const char* profile, std::vector<uint32_t>& spirv,
                        std::string& errors) {
    spirv.clear();
    errors.clear();
#ifdef XBE_GLSLANG
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
    const EShLanguage stage = profile[0] == 'v' ? EShLangVertex : profile[0] == 'g' ? EShLangGeometry : EShLangFragment;
    const char* text = source.c_str();
    glslang::TShader sh(stage);
    sh.setStrings(&text, 1);
    sh.setEntryPoint("main");
    sh.setSourceEntryPoint(entry);
    sh.setEnvInput(glslang::EShSourceHlsl, stage, glslang::EShClientVulkan, 100);
    sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    sh.setShiftBinding(glslang::EResUbo, 0);
    sh.setShiftBinding(glslang::EResTexture, 2);
    sh.setShiftBinding(glslang::EResSampler, 6);
    sh.setShiftBinding(glslang::EResUav, 10);
    sh.setAutoMapBindings(true);
    sh.setAutoMapLocations(true);
    if (stage == EShLangFragment) sh.setDxPositionW(true);
    const EShMessages msgs = EShMessages(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl | EShMsgHlslOffsets);
    if (!sh.parse(GetDefaultResources(), 100, false, msgs)) {
        errors = sh.getInfoLog();
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&sh);
    if (!prog.link(msgs) || !prog.mapIO()) {
        errors = prog.getInfoLog();
        return false;
    }
    glslang::SpvOptions opt;
    glslang::GlslangToSpv(*prog.getIntermediate(stage), spirv, &opt);
    if (spirv.empty()) errors = "glslang made no SPIR-V";
    return !spirv.empty();
#else
    (void)source, (void)entry, (void)profile;
    errors = "glslang is not built in";
    return false;
#endif
}

}  // namespace gpu
