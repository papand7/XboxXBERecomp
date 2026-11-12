// fsr2_shaders: FSR 2's Vulkan compute shaders (the fetched FidelityFX-FSR2 sources' GLSL passes) compiled to SPIR-V
// by glslang at build time, with the resources each declares in set 1 (sampled images, storage images, uniform
// buffers: their names and bindings, read back from the SPIR-V), written as one C++ include that blobs_vk.cpp hands
// FSR 2's Vulkan backend (fsr2GetPermutationBlobByIndexVK). In place of the FidelityFX_SC.exe the FSR 2 repository
// ships: only what the runtime asks for is built (render-sized motion vectors, not jittered, depth not inverted, no
// 16-bit types) in each pass's 8 permutations (reprojection's Lanczos table or not, HDR input or not, sharpening).
//   fsr2_shaders <FSR 2's src/ffx-fsr2-api/shaders> <output .inc>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path g_dir;

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// #include "x" from the shaders' folder (they include each other by name alone)
class Includer : public glslang::TShader::Includer {
public:
    IncludeResult* includeLocal(const char* name, const char*, size_t) override { return Load(name); }
    IncludeResult* includeSystem(const char* name, const char*, size_t) override { return Load(name); }
    void releaseInclude(IncludeResult* r) override {
        if (!r) return;
        delete static_cast<std::string*>(r->userData);
        delete r;
    }

private:
    IncludeResult* Load(const char* name) {
        std::filesystem::path p = g_dir / name;
        if (!std::filesystem::exists(p)) return nullptr;
        auto* text = new std::string(ReadFile(p));
        return new IncludeResult(p.generic_string(), text->data(), text->size(), text);
    }
};

struct Binding {
    std::string name;
    uint32_t binding;
};
struct Reflected {
    std::vector<Binding> srv, uav, cb;
};

// set 1's resources from the SPIR-V: OpName, OpDecorate (DescriptorSet 34, Binding 33), OpVariable, OpTypePointer,
// OpTypeImage (its Sampled operand: 1 sampled, 2 storage), OpTypeStruct (a uniform buffer)
Reflected Reflect(const std::vector<uint32_t>& spv) {
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, uint32_t> set, binding, pointee, image_sampled;
    std::set<uint32_t> structs, used;
    std::vector<std::pair<uint32_t, uint32_t>> vars;  // id, pointer type
    for (size_t i = 5; i < spv.size();) {
        uint32_t op = spv[i] & 0xFFFF, n = spv[i] >> 16;
        if (!n) break;
        const uint32_t* w = &spv[i];
        if (op == 5 && n >= 3) names[w[1]] = reinterpret_cast<const char*>(&w[2]);
        if (op == 71 && n >= 4 && w[2] == 34) set[w[1]] = w[3];
        if (op == 71 && n >= 4 && w[2] == 33) binding[w[1]] = w[3];
        if (op == 32 && n >= 4) pointee[w[1]] = w[3];
        if (op == 25 && n >= 9) image_sampled[w[1]] = w[7];
        if (op == 30) structs.insert(w[1]);
        if (op == 59 && n >= 4) vars.push_back({w[2], w[1]});
        // what the code reads or writes through: OpLoad, OpAccessChain, OpInBoundsAccessChain, OpImageTexelPointer
        // (their base), OpStore (its pointer), OpFunctionCall (its arguments)
        if ((op == 61 || op == 65 || op == 66 || op == 60) && n >= 4) used.insert(w[3]);
        if (op == 62 && n >= 2) used.insert(w[1]);
        if (op == 57)
            for (uint32_t k = 4; k < n; k++) used.insert(w[k]);
        i += n;
    }
    Reflected r;
    for (auto [id, ptr] : vars) {
        auto s = set.find(id);
        auto b = binding.find(id);
        // declared but never used: left out, as FidelityFX_SC's reflection leaves it (FSR 2 binds one resource as a
        // pass's input and output only where the pass uses one of them)
        if (s == set.end() || b == binding.end() || s->second != 1 || !used.count(id)) continue;
        uint32_t type = pointee.count(ptr) ? pointee[ptr] : 0;
        Binding e{names[id], b->second};
        if (image_sampled.count(type)) {
            (image_sampled[type] == 2 ? r.uav : r.srv).push_back(e);
        } else if (structs.count(type)) {
            if (e.name.empty()) e.name = names[type];
            r.cb.push_back(e);
        } else {
            std::fprintf(stderr, "fsr2_shaders: resource %s (binding %u) of an unknown kind\n", e.name.c_str(), e.binding);
        }
    }
    return r;
}

bool Compile(const std::string& file, const std::string& defines, std::vector<uint32_t>& spv) {
    std::string src = ReadFile(g_dir / file);
    if (src.empty()) {
        std::fprintf(stderr, "fsr2_shaders: %s not found\n", file.c_str());
        return false;
    }
    // the defines go after #version, which must come first
    size_t v = src.find("#version");
    size_t eol = v == std::string::npos ? 0 : src.find('\n', v) + 1;
    src.insert(eol, defines + "#line 2\n");
    glslang::TShader sh(EShLangCompute);
    const char* text = src.c_str();
    const char* name = file.c_str();
    sh.setStringsWithLengthsAndNames(&text, nullptr, &name, 1);
    sh.setEntryPoint("main");
    sh.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 100);
    sh.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    Includer inc;
    const EShMessages msgs = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    if (!sh.parse(GetDefaultResources(), 450, false, msgs, inc)) {
        std::fprintf(stderr, "fsr2_shaders: %s:\n%s\n%s\n", file.c_str(), sh.getInfoLog(), sh.getInfoDebugLog());
        return false;
    }
    glslang::TProgram prog;
    prog.addShader(&sh);
    if (!prog.link(msgs)) {
        std::fprintf(stderr, "fsr2_shaders: %s (link):\n%s\n", file.c_str(), prog.getInfoLog());
        return false;
    }
    glslang::SpvOptions opt;
    opt.disableOptimizer = true;
    spv.clear();
    glslang::GlslangToSpv(*prog.getIntermediate(EShLangCompute), spv, &opt);
    return !spv.empty();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: fsr2_shaders <FSR 2 shaders folder> <output .inc>\n");
        return 2;
    }
    g_dir = argv[1];
    // the passes, in blobs_vk.cpp's order
    const char* passes[] = {"ffx_fsr2_depth_clip_pass",        "ffx_fsr2_reconstruct_previous_depth_pass",
                            "ffx_fsr2_lock_pass",              "ffx_fsr2_accumulate_pass",
                            "ffx_fsr2_rcas_pass",              "ffx_fsr2_compute_luminance_pyramid_pass",
                            "ffx_fsr2_autogen_reactive_pass",  "ffx_fsr2_tcr_autogen_pass"};
    glslang::InitializeProcess();
    std::ostringstream out;
    out << "// generated by fsr2_shaders from FSR 2's GLSL passes (MIT, AMD); do not edit\n";
    std::ostringstream table;
    int count = 0;
    for (int p = 0; p < 8; p++) {
        for (uint32_t perm = 0; perm < 8; perm++) {
            const bool lanczos = perm & 1, hdr = perm & 2, sharpen = perm & 4;
            std::string d =
                "#define FFX_GPU 1\n#define FFX_GLSL 1\n#define FFX_HALF 0\n"
                "#define FFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF 0\n"
                "#define FFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF 0\n"
                "#define FFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF 1\n"
                "#define FFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF 0\n"
                "#define FFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE 2\n"
                "#define FFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS 1\n"
                "#define FFX_FSR2_OPTION_JITTERED_MOTION_VECTORS 0\n"
                "#define FFX_FSR2_OPTION_INVERTED_DEPTH 0\n";
            d += std::string("#define FFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE ") + (lanczos ? "1" : "0") + "\n";
            d += std::string("#define FFX_FSR2_OPTION_HDR_COLOR_INPUT ") + (hdr ? "1" : "0") + "\n";
            d += std::string("#define FFX_FSR2_OPTION_APPLY_SHARPENING ") + (sharpen ? "1" : "0") + "\n";
            std::vector<uint32_t> spv;
            if (!Compile(std::string(passes[p]) + ".glsl", d, spv)) return 1;
            Reflected r = Reflect(spv);
            std::string id = "k" + std::to_string(p) + "_" + std::to_string(perm);
            out << "static const uint32_t " << id << "_code[] = {";
            for (size_t i = 0; i < spv.size(); i++) out << (i ? "," : "") << (i % 12 ? "" : "\n") << spv[i];
            out << "};\n";
            auto list = [&](const char* kind, const std::vector<Binding>& v) {
                out << "static const char* " << id << "_" << kind << "_names[] = {";
                for (const Binding& b : v) out << "\"" << b.name << "\",";
                out << (v.empty() ? "nullptr" : "") << "};\n";
                out << "static const uint32_t " << id << "_" << kind << "_binds[] = {";
                for (const Binding& b : v) out << b.binding << ",";
                out << (v.empty() ? "0" : "") << "};\n";
            };
            list("srv", r.srv);
            list("uav", r.uav);
            list("cb", r.cb);
            table << "    {" << p << ", " << perm << ", " << id << "_code, sizeof(" << id << "_code), " << r.uav.size()
                  << ", " << r.srv.size() << ", " << r.cb.size() << ", " << id << "_uav_names, " << id << "_uav_binds, "
                  << id << "_srv_names, " << id << "_srv_binds, " << id << "_cb_names, " << id << "_cb_binds},\n";
            count++;
        }
    }
    glslang::FinalizeProcess();
    out << "static const Fsr2Blob kFsr2Blobs[] = {\n" << table.str() << "};\n";
    std::string text = out.str();
    // written only when it changed, so what includes it is not built again for nothing
    if (ReadFile(argv[2]) != text) {
        std::filesystem::create_directories(std::filesystem::path(argv[2]).parent_path());
        std::ofstream(argv[2], std::ios::binary) << text;
    }
    std::printf("fsr2_shaders: %d shaders\n", count);
    return 0;
}
