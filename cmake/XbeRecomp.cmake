# Original Xbox static recompilation: xbe_recomp, the host runtime and add_xbe_image()
include_guard(GLOBAL)

get_filename_component(XBE_SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
# build scripts find the SDK's xdk folder and THIRD_PARTY.txt through this
file(TO_NATIVE_PATH "${XBE_SDK_DIR}" XBE_SDK_DIR_NATIVE)
file(WRITE ${CMAKE_BINARY_DIR}/sdk_dir.txt "${XBE_SDK_DIR_NATIVE}\n")

include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis GIT_REPOSITORY https://github.com/zyantific/zydis.git GIT_TAG v4.1.1 GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.0 GIT_SHALLOW TRUE)
# SDL3 for controllers (on Linux also the window and sound), static so it links into the exe
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zydis tomlplusplus imgui SDL3)
# the Vulkan renderer: Khronos' headers (vulkan-1.dll / libvulkan.so.1 is loaded at run time, nothing is linked) and
# DXC, which compiles the HLSL shaders to SPIR-V (its dxcompiler.dll / libdxcompiler.so goes beside each exe, loaded at
# run time too)
FetchContent_Declare(VulkanHeaders URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.tar.gz
    URL_HASH SHA256=e204e0b3c19f622d197df945737f5db913d6621830999b8578d34e80a7c90585)
if(WIN32)
    FetchContent_Declare(dxc URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip
        URL_HASH SHA256=e2627f004f0f9424d8c71ea1314d04f38c5a5096884ae9217f1f18bd320267b5)
else()
    FetchContent_Declare(dxc URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/linux_dxc_2024_07_31.x86_64.tar.gz
        URL_HASH SHA256=b58725ac191df0686994fb9d54d27ee8dd3f180b023d562733112a499ea1195c)
endif()
FetchContent_MakeAvailable(VulkanHeaders dxc)
if(WIN32)
    set(XBE_DXC_INCLUDE ${dxc_SOURCE_DIR}/inc)
    set(XBE_DXC_LIBS ${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll)
else()
    set(XBE_DXC_INCLUDE ${dxc_SOURCE_DIR}/include/dxc)
    set(XBE_DXC_LIBS ${dxc_SOURCE_DIR}/lib/libdxcompiler.so ${dxc_SOURCE_DIR}/lib/libdxil.so)
    # stb_image / stb_image_write (public domain or MIT): pictures off Windows (app/image.cpp), WIC's work on Windows
    FetchContent_Declare(stb URL https://github.com/nothings/stb/archive/4e9b368bfc071e97206ea5c6adba68dd0cbf8897.tar.gz
        URL_HASH SHA256=e60706ab084e0cae35ee7282deb4a7f283e4a28be1896d097437205da01a0683)
    FetchContent_MakeAvailable(stb)
    set_source_files_properties(${XBE_SDK_DIR}/runtime/app/image.cpp PROPERTIES INCLUDE_DIRECTORIES "${stb_SOURCE_DIR}")
endif()
# SMAA (MIT, Jimenez et al.): the Vulkan renderer's scene antialiasing; its shader source as a string, its lookup
# textures' bytes from its headers (gpu/smaa.cpp)
FetchContent_Declare(smaa URL https://github.com/iryoku/smaa/archive/71c806a838bdd7d517df19192a20f0c61b3ca29d.tar.gz
    URL_HASH SHA256=046a435f5713f8bfe8148fe6305c2abf1409fc2122bee6070061c675e1e55f57)
FetchContent_MakeAvailable(smaa)
file(READ ${smaa_SOURCE_DIR}/SMAA.hlsl XBE_SMAA_HLSL)
file(WRITE ${CMAKE_BINARY_DIR}/smaa/smaa_hlsl.inc.new "R\"smaa(${XBE_SMAA_HLSL})smaa\"\n")
configure_file(${CMAKE_BINARY_DIR}/smaa/smaa_hlsl.inc.new ${CMAKE_BINARY_DIR}/smaa/smaa_hlsl.inc COPYONLY)

# FSR 2 (MIT, AMD FidelityFX): the Vulkan renderer's upscaler (gpu/upscale_fsr.cpp). Its library and Vulkan backend
# are built from the fetched sources (not its CMake files, which want the Vulkan SDK); the backend's GLSL compute
# shaders are compiled to SPIR-V at build time by tools/fsr2_shaders through glslang (BSD/Apache, Khronos; built from
# source), which also writes each shader's bindings. The FidelityFX_SC.exe the FSR 2 archive carries is never run.
FetchContent_Declare(fsr2 URL https://github.com/GPUOpen-Effects/FidelityFX-FSR2/archive/refs/tags/v2.2.1.tar.gz
    URL_HASH SHA256=4d7261ef054b071de83c7d201f6850c274226bd3025f4a2274ef4968343bd534 SOURCE_SUBDIR none)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang URL https://github.com/KhronosGroup/glslang/archive/refs/tags/15.4.0.tar.gz
    URL_HASH SHA256=b16c78e7604b9be9f546ee35ad8b6db6f39bbbbfb19e8d038b6fe2ea5bba4ff4 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(fsr2 glslang)
set(XBE_FSR2_API ${fsr2_SOURCE_DIR}/src/ffx-fsr2-api)
add_executable(fsr2_shaders EXCLUDE_FROM_ALL ${XBE_SDK_DIR}/tools/fsr2_shaders/main.cpp)
target_link_libraries(fsr2_shaders PRIVATE glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/fsr2/fsr2_blobs.inc
    COMMAND fsr2_shaders ${XBE_FSR2_API}/shaders ${CMAKE_BINARY_DIR}/fsr2/fsr2_blobs.inc
    DEPENDS fsr2_shaders
    COMMENT "FSR 2's Vulkan shaders to SPIR-V"
    VERBATIM)
add_library(xbe_fsr2 STATIC EXCLUDE_FROM_ALL
    ${XBE_FSR2_API}/ffx_fsr2.cpp
    ${XBE_FSR2_API}/ffx_assert.cpp
    ${XBE_FSR2_API}/vk/ffx_fsr2_vk.cpp
    ${XBE_SDK_DIR}/tools/fsr2_shaders/blobs_vk.cpp
    ${CMAKE_BINARY_DIR}/fsr2/fsr2_blobs.inc)
target_include_directories(xbe_fsr2 PRIVATE ${vulkanheaders_SOURCE_DIR}/include ${XBE_FSR2_API}/vk/shaders
    ${CMAKE_BINARY_DIR}/fsr2)
target_compile_definitions(xbe_fsr2 PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS _SILENCE_ALL_CXX20_DEPRECATION_WARNINGS)
target_compile_options(xbe_fsr2 PRIVATE -w)
# off Windows: FSR 2's GCC branch (no __declspec) and the MSVC names its sources use (_countof, wcscpy_s)
set(XBE_FSR2_POSIX ${XBE_SDK_DIR}/tools/fsr2_shaders/posix_compat.h)
if(NOT WIN32)
    target_compile_definitions(xbe_fsr2 PRIVATE FFX_GCC)
    target_compile_options(xbe_fsr2 PRIVATE -include ${XBE_FSR2_POSIX})
endif()

# NVIDIA DLSS Super Resolution (gpu/upscale_dlss.cpp): NVIDIA's NGX SDK under the NVIDIA RTX SDKs licence (its
# terms in sdk/THIRD_PARTY.txt; building with it accepts them). Its headers, static loader library and the
# nvngx_dlss.dll runtime are downloaded one by one from NVIDIA's DLSS repository at the tag's commit (the tag's archive
# carries every platform's binaries, about 1 GB), each checked against its SHA-256, into the build folder: never
# committed. The dll goes beside each exe. XBE_DLSS=OFF, or a failed download, builds without it (DLSS not available).
option(XBE_DLSS "Fetch NVIDIA's DLSS SDK (NVIDIA RTX SDKs licence) and build the DLSS upscaler" ON)
set(XBE_DLSS_FOUND OFF)
if(XBE_DLSS AND WIN32)
    set(XBE_DLSS_TAG v310.9.16)
    set(XBE_DLSS_COMMIT 9981c6350dc58c0ce21f024fd65e31e851c31689)
    set(XBE_DLSS_DIR ${CMAKE_BINARY_DIR}/_deps/dlss-${XBE_DLSS_TAG})
    set(XBE_DLSS_FILES
        include/nvsdk_ngx.h dc38e7467cf415379c9d12ae1b6e4a494c453ed92720fb53e92aecb523e7b848
        include/nvsdk_ngx_defs.h ea23f33497cd274860d1c25a97644fce807dcb0037c594547203343103fad03e
        include/nvsdk_ngx_defs_vk.h 0a24d0861ace7d6b9362a67b7f08bea1b33ea123c5ce730b19133de8a891d031
        include/nvsdk_ngx_helpers.h 5bcbadfe7478b802cf6d3aca4dc5ddd7d0889b99726e69c63f9e9bd555f44471
        include/nvsdk_ngx_helpers_cuda.h f2851a76107bdf4fbcb7b261f3c35573c312d8ecb4bc18fb02d9e2cec7705042
        include/nvsdk_ngx_helpers_d3d.h ec75224f36ed6580baaf250fa83620405ad81a1d98c2c436654a0c9cf6a6b8ba
        include/nvsdk_ngx_helpers_vk.h c192bff72138f12f770db48e12c1d8f712dfc81ea0e8e055ebb27d8dd1b31623
        include/nvsdk_ngx_loader.h abdf5f66a692620538357d29fdf78e3e2a677ee8140b7a638962ecb766585e75
        include/nvsdk_ngx_params.h 943bc8cc5cdae03b6303016fbad3183636f2335ae27a2d18776798c3b4efabbc
        include/nvsdk_ngx_vk.h 2d364ce7132881eb669e9498fd570d74cba563b1fbebc82c235f8e1ad2dd8b6d
        lib/Windows_x86_64/x64/nvsdk_ngx_d.lib bc43df9ec032004fde681d3e3afd01cde239e03a0125b822e88674c84287f99c
        lib/Windows_x86_64/rel/nvngx_dlss.dll d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e
        LICENSE.txt d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0)
    set(XBE_DLSS_FOUND ON)
    list(LENGTH XBE_DLSS_FILES n)
    math(EXPR last "${n} - 1")
    foreach(i RANGE 0 ${last} 2)
        math(EXPR j "${i} + 1")
        list(GET XBE_DLSS_FILES ${i} f)
        list(GET XBE_DLSS_FILES ${j} want)
        set(dst ${XBE_DLSS_DIR}/${f})
        set(have "")
        if(EXISTS ${dst})
            file(SHA256 ${dst} have)
        endif()
        if(NOT have STREQUAL want)
            message(STATUS "DLSS SDK ${XBE_DLSS_TAG}: ${f}")
            file(DOWNLOAD https://raw.githubusercontent.com/NVIDIA/DLSS/${XBE_DLSS_COMMIT}/${f} ${dst}.part
                STATUS st TLS_VERIFY ON)
            list(GET st 0 code)
            set(have "")
            if(code EQUAL 0)
                file(SHA256 ${dst}.part have)
            endif()
            if(have STREQUAL want)
                file(RENAME ${dst}.part ${dst})
            else()
                file(REMOVE ${dst}.part)
                message(WARNING "DLSS SDK: ${f} could not be fetched or did not match its hash (${st}); built without DLSS")
                set(XBE_DLSS_FOUND OFF)
                break()
            endif()
        endif()
    endforeach()
    if(XBE_DLSS_FOUND)
        set(XBE_DLSS_LIB ${XBE_DLSS_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib)
        set(XBE_DLSS_DLL ${XBE_DLSS_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll)
        message(STATUS "DLSS SDK ${XBE_DLSS_TAG} (NVIDIA RTX SDKs licence): DLSS built in")
    endif()
endif()
if(XBE_DLSS_FOUND)
    set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/upscale_dlss.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include;${XBE_DLSS_DIR}/include"
        COMPILE_DEFINITIONS "XBE_DLSS=1")
else()
    set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/upscale_dlss.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include")
endif()

# hd_pack (tools/hd_pack): a texture pack upscaled on the player's PC by Real-ESRGAN (BSD-3; its ncnn Vulkan build,
# MIT, with the realesrgan-x4plus models), copied beside each game exe in tools/
if(WIN32)
    FetchContent_Declare(realesrgan URL https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip
        URL_HASH SHA256=abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d)
    FetchContent_MakeAvailable(realesrgan)
    add_executable(hd_pack ${XBE_SDK_DIR}/tools/hd_pack/main.cpp)
    target_compile_definitions(hd_pack PRIVATE UNICODE _UNICODE)
    target_link_libraries(hd_pack PRIVATE windowscodecs ole32)
endif()

# x86 to C++ recompiler
file(GLOB XBE_RECOMP_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/tools/recomp/*.cpp)
add_executable(xbe_recomp ${XBE_RECOMP_SOURCES})
target_include_directories(xbe_recomp PRIVATE ${XBE_SDK_DIR}/runtime ${XBE_SDK_DIR}/tools/recomp)
target_compile_definitions(xbe_recomp PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_recomp PRIVATE Zydis tomlplusplus::tomlplusplus)

# XDK signature scanner that writes a new title's config
add_executable(xbe_symbols ${XBE_SDK_DIR}/tools/symbols/main.cpp)
target_include_directories(xbe_symbols PRIVATE ${XBE_SDK_DIR}/runtime)
target_compile_definitions(xbe_symbols PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_symbols PRIVATE Zydis tomlplusplus::tomlplusplus)

set(XBE_IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
if(WIN32)
    list(APPEND XBE_IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
else()
    # off Windows the window is SDL's (app/main_sdl.cpp); imgui 1.91.0's backend still names SDL_TRUE / SDL_FALSE
    list(APPEND XBE_IMGUI_SOURCES ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
    set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp PROPERTIES
        COMPILE_DEFINITIONS SDL_ENABLE_OLD_NAMES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")
endif()

# Vulkan's entry points are loaded at run time (gpu/vulkan.cpp hands ImGui its loader)
set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp PROPERTIES
    COMPILE_DEFINITIONS IMGUI_IMPL_VULKAN_NO_PROTOTYPES INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include")
set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/vulkan.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include")
set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/dxc.cpp PROPERTIES INCLUDE_DIRECTORIES "${XBE_DXC_INCLUDE}")

# the settings window reads controllers from the runtime's SDL input (app::FeedPad), not XInput
set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp PROPERTIES
    COMPILE_DEFINITIONS IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)

file(GLOB XBE_RUNTIME_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/runtime/*/*.cpp)
if(NOT WIN32)
    # Windows only: the Direct3D renderers, XAudio2, the DirectInput wheel (input.cpp stubs it) and the Win32 window
    list(FILTER XBE_RUNTIME_SOURCES EXCLUDE REGEX "/runtime/(gpu/d3d12|gpu/d3d11|audio/xaudio2|core/wheel|app/main)\\.cpp$")
endif()
# SDL's headers go to the files that use SDL alone (it is linked LINK_ONLY), so the generated sources' compile line stays
# the same
foreach(f core/input.cpp app/main_sdl.cpp audio/sdl.cpp)
    set_source_files_properties(${XBE_SDK_DIR}/runtime/${f} PROPERTIES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")
endforeach()
if(WIN32)
    set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/upscale_fsr.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include;${XBE_FSR2_API};${XBE_FSR2_API}/vk"
        COMPILE_DEFINITIONS "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS")
else()
    set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/upscale_fsr.cpp PROPERTIES
        INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include;${XBE_FSR2_API};${XBE_FSR2_API}/vk"
        COMPILE_DEFINITIONS "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS;FFX_GCC" COMPILE_OPTIONS "-include;${XBE_FSR2_POSIX}")
endif()
set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/smaa.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${smaa_SOURCE_DIR}/Textures;${CMAKE_BINARY_DIR}/smaa")

# One executable per recompiled image: its code from config/<config> (partitions as that config sets them),
# the SDK runtime, XBE_HOST_SOURCES shared by every image of the game, and src/<game_dir> hooks only this image has.
# Expects assets/<xbe> and the config's out = "generated/<game_dir>". At run time the game may be a disc image instead.
function(add_xbe_image target xbe config partitions game_dir)
    set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/generated/${game_dir})
    set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
    math(EXPR last "${partitions} - 1")
    foreach(i RANGE ${last})
        list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
    endforeach()
    file(GLOB configs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml ${XBE_SDK_DIR}/xdk/*/*.toml)
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h
        COMMAND xbe_recomp config/${config} --xdk ${XBE_SDK_DIR}/xdk
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${configs} ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${xbe}"
        VERBATIM)
    add_custom_target(recompile_${target} DEPENDS ${GEN}/codegen.stamp)
    set_source_files_properties(${GEN_SOURCES} PROPERTIES GENERATED TRUE
        COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-Wno-unused-label;-Wno-parentheses-equality;-Wno-unused-variable")
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    # the xbe's SHA-1: the executable checks the game it finds is the release it was made from
    set(xbe_file ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xbe_file})
    file(SHA1 ${xbe_file} xbe_sha1)
    set(XBE_HASH ${CMAKE_CURRENT_BINARY_DIR}/${target}_xbe.cpp)
    file(CONFIGURE OUTPUT ${XBE_HASH} CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
    add_executable(${target} WIN32 ${GAME_SOURCES} ${GEN_SOURCES} ${XBE_HASH})
    add_dependencies(${target} recompile_${target})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GEN})
    xbe_runtime_target(${target})
endfunction()

# The runtime, ImGui and XBE_HOST_SOURCES added to a game executable (add_xbe_image's, or the Linux CI's around a stub
# image in tests/linux), with what it links and the files that go beside it
function(xbe_runtime_target target)
    target_sources(${target} PRIVATE ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${XBE_IMGUI_SOURCES})
    target_include_directories(${target} PRIVATE ${XBE_SDK_DIR}/runtime)
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> xbe_fsr2)
    if(WIN32)
        target_link_libraries(${target} PRIVATE d3d12 d3d11 dinput8 dxgi dxguid d3dcompiler xaudio2 ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
    else()
        # the few Win32 and Winsock names portable sources use, and the D3D ones the shared renderer code names
        target_include_directories(${target} PRIVATE ${XBE_SDK_DIR}/runtime/compat)
        target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS} pthread)
    endif()
    # the Vulkan renderer's shader compiler, beside the exe
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_DXC_LIBS} $<TARGET_FILE_DIR:${target}>)
    # DLSS: NGX's loader linked in, NVIDIA's runtime beside the exe (the launcher copies the build's dlls to play\)
    if(XBE_DLSS_FOUND)
        target_link_libraries(${target} PRIVATE ${XBE_DLSS_LIB})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_DLSS_DLL} $<TARGET_FILE_DIR:${target}>)
    endif()
    # the texture pack tool and its upscaler, in tools\ beside the exe
    if(WIN32 AND TARGET hd_pack)
        add_dependencies(${target} hd_pack)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/tools/realesrgan/models
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:hd_pack> $<TARGET_FILE_DIR:${target}>/tools
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${realesrgan_SOURCE_DIR}/realesrgan-ncnn-vulkan.exe
                    ${realesrgan_SOURCE_DIR}/vcomp140.dll $<TARGET_FILE_DIR:${target}>/tools/realesrgan
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${realesrgan_SOURCE_DIR}/models/realesrgan-x4plus.bin
                    ${realesrgan_SOURCE_DIR}/models/realesrgan-x4plus.param ${realesrgan_SOURCE_DIR}/models/realesrgan-x4plus-anime.bin
                    ${realesrgan_SOURCE_DIR}/models/realesrgan-x4plus-anime.param $<TARGET_FILE_DIR:${target}>/tools/realesrgan/models)
    endif()
    # lld keeps the recompiled weak functions overridable by the host ones
    target_link_options(${target} PRIVATE -fuse-ld=lld -g)
endfunction()
