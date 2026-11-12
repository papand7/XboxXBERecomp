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
# SDL3 for controllers, static so it links into the exe
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-3.4.16 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zydis tomlplusplus imgui SDL3)
# the Vulkan renderer: Khronos' headers (vulkan-1.dll is loaded at run time, nothing is linked) and DXC, which compiles
# the HLSL shaders to SPIR-V (its dxcompiler.dll goes beside each exe)
FetchContent_Declare(VulkanHeaders URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.tar.gz
    URL_HASH SHA256=e204e0b3c19f622d197df945737f5db913d6621830999b8578d34e80a7c90585)
FetchContent_Declare(dxc URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip
    URL_HASH SHA256=e2627f004f0f9424d8c71ea1314d04f38c5a5096884ae9217f1f18bd320267b5)
FetchContent_MakeAvailable(VulkanHeaders dxc)

# FSR 2 (MIT, AMD FidelityFX): the Vulkan renderer's temporal upscaler (gpu/upscale_fsr.cpp). Its library and Vulkan
# backend are built from the fetched sources (not its CMake files, which want the Vulkan SDK); the backend's GLSL compute
# shaders are compiled to SPIR-V at build time by tools/fsr2_shaders through glslang (BSD/Apache, Khronos; built from
# source, never part of the executable), which also writes each shader's bindings. The FidelityFX_SC.exe the FSR 2
# archive carries is never run.
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

# XBE_GAME_DLL, set by a game before including this: a release holds the runtime and the compiler, and the game's code
# is built on the player's PC into <exe>.dll (runtime/app/game_code.cpp, from TGP482's XboxXBE-Recomp, MIT). Without it
# the game's code links into its executable, as the launcher's whole build and the harness do.
if(XBE_GAME_DLL)
    # What the game needs to build its code on the player's PC (runtime/app/game_code.cpp), beside the executables: the
    # recompiler, clang and lld from the toolchain building this, the game's configs, the XDK layouts they include and the
    # headers the generated code includes
    set(XBE_COMPILER_DIR ${CMAKE_BINARY_DIR}/compiler)
    get_filename_component(XBE_LLVM_BIN ${CMAKE_CXX_COMPILER} DIRECTORY)
    # a game with a build per disc sets XBE_GAME_CONFIGS to this disc's (the hand-kept ones and what they include)
    if(NOT XBE_GAME_CONFIGS)
        file(GLOB XBE_GAME_CONFIGS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml)
    endif()
    set(XBE_COMPILER_HEADERS ${XBE_SDK_DIR}/runtime/cpu/x86_ops.h ${XBE_SDK_DIR}/runtime/cpu/x86_context.h)
    file(GLOB XBE_FREESTANDING_HEADERS CONFIGURE_DEPENDS ${XBE_SDK_DIR}/runtime/cpu/freestanding/*)
    file(GLOB XBE_LAYOUTS CONFIGURE_DEPENDS ${XBE_SDK_DIR}/xdk/*/layout.toml)
    set(XBE_COPY_LAYOUTS)
    foreach(layout ${XBE_LAYOUTS})
        get_filename_component(build_dir ${layout} DIRECTORY)
        get_filename_component(build ${build_dir} NAME)
        list(APPEND XBE_COPY_LAYOUTS COMMAND ${CMAKE_COMMAND} -E make_directory ${XBE_COMPILER_DIR}/xdk/${build}
             COMMAND ${CMAKE_COMMAND} -E copy_if_different ${layout} ${XBE_COMPILER_DIR}/xdk/${build})
    endforeach()
    add_custom_target(compiler_files ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${XBE_COMPILER_DIR}/config ${XBE_COMPILER_DIR}/include/cpu
                ${XBE_COMPILER_DIR}/include/freestanding
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_LLVM_BIN}/clang.exe ${XBE_LLVM_BIN}/lld-link.exe
                ${XBE_SDK_DIR}/tools/LLVM-LICENSE.txt ${XBE_COMPILER_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_GAME_CONFIGS} ${XBE_COMPILER_DIR}/config
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_COMPILER_HEADERS} ${XBE_COMPILER_DIR}/include/cpu
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XBE_FREESTANDING_HEADERS} ${XBE_COMPILER_DIR}/include/freestanding
        ${XBE_COPY_LAYOUTS}
        VERBATIM)
endif()

# x86 to C++ recompiler
file(GLOB XBE_RECOMP_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/tools/recomp/*.cpp)
add_executable(xbe_recomp ${XBE_RECOMP_SOURCES})
if(XBE_GAME_DLL)
    set_target_properties(xbe_recomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${XBE_COMPILER_DIR})
endif()
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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)

# Vulkan's entry points are loaded at run time (gpu/vulkan.cpp hands ImGui its loader)
set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp PROPERTIES
    COMPILE_DEFINITIONS IMGUI_IMPL_VULKAN_NO_PROTOTYPES INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include")
set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/vulkan.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include;${dxc_SOURCE_DIR}/inc")
set_source_files_properties(${XBE_SDK_DIR}/runtime/gpu/upscale_fsr.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${vulkanheaders_SOURCE_DIR}/include;${XBE_FSR2_API};${XBE_FSR2_API}/vk"
    COMPILE_DEFINITIONS "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS")

# the settings window reads controllers from the runtime's SDL input (app::FeedPad), not XInput
set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp PROPERTIES
    COMPILE_DEFINITIONS IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)

file(GLOB XBE_RUNTIME_SOURCES CONFIGURE_DEPENDS ${XBE_SDK_DIR}/runtime/*/*.cpp)
list(FILTER XBE_RUNTIME_SOURCES EXCLUDE REGEX "/runtime/gpu/d3d(11|12)\\.cpp$")
# SDL's headers go to the input code alone (it is linked LINK_ONLY), so the generated sources' compile line stays the same
set_source_files_properties(${XBE_SDK_DIR}/runtime/core/input.cpp PROPERTIES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")

# One executable per recompiled image: its code from config/<config> (partitions as that config sets them),
# the SDK runtime, XBE_HOST_SOURCES shared by every image of the game, and src/<game_dir> hooks only this image has.
function(add_xbe_image_dll target xbe config partitions game_dir)
    set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/generated/${game_dir})
    set(GEN_SOURCES ${GEN}/x86_func_mapping.cpp)
    math(EXPR last "${partitions} - 1")
    foreach(i RANGE ${last})
        list(APPEND GEN_SOURCES ${GEN}/x86_recomp.${i}.cpp)
    endforeach()
    file(GLOB configs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.toml ${XBE_SDK_DIR}/xdk/*/*.toml)
    add_custom_command(
        OUTPUT ${GEN}/codegen.stamp
        BYPRODUCTS ${GEN_SOURCES} ${GEN}/x86_recomp_shared.h ${GEN}/image_symbols.h ${GEN}/hook_exports.h
                   ${GEN}/exports.def ${GEN}/imports.def ${GEN}/code.lib
        COMMAND xbe_recomp config/${config} --xdk ${XBE_SDK_DIR}/xdk
        COMMAND ${XBE_LLVM_BIN}/lld-link.exe /lib /machine:x64 /def:${GEN}/imports.def /out:${GEN}/code.lib
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN}/codegen.stamp
        DEPENDS xbe_recomp ${configs} ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Recompiling ${xbe}"
        VERBATIM)
    add_custom_target(recompile_${target} DEPENDS ${GEN}/codegen.stamp)
    file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${game_dir}/*.cpp)
    # the xbe's SHA-1: the executable checks the game it finds is the release it was made from
    set(xbe_file ${CMAKE_CURRENT_SOURCE_DIR}/assets/${xbe})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${xbe_file})
    file(SHA1 ${xbe_file} xbe_sha1)
    set(XBE_HASH ${CMAKE_CURRENT_BINARY_DIR}/${target}_xbe.cpp)
    file(CONFIGURE OUTPUT ${XBE_HASH} CONTENT "extern const char kImageSha1[] = \"${xbe_sha1}\";\n")
    add_executable(${target} WIN32 ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES} ${XBE_IMGUI_SOURCES} ${XBE_HASH})
    add_dependencies(${target} recompile_${target} compiler_files)
    # the import library of what the executable exports to its DLL goes to the compiler folder
    set_target_properties(${target} PROPERTIES ENABLE_EXPORTS ON ARCHIVE_OUTPUT_DIRECTORY ${XBE_COMPILER_DIR})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${XBE_SDK_DIR}/runtime ${GEN})
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target} PRIVATE XBE_GAME_DLL UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> xbe_fsr2 dinput8 dxguid xaudio2 ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
    target_link_libraries(${target} PRIVATE ${GEN}/code.lib delayimp)
    target_link_options(${target} PRIVATE -fuse-ld=lld -g -Wl,/delayload:${target}.dll)

    # without a C runtime: the executable passes on what the code calls
    add_library(${target}_code SHARED ${GEN_SOURCES})
    set_target_properties(${target}_code PROPERTIES OUTPUT_NAME ${target} CXX_EXTENSIONS OFF)
    set_source_files_properties(${GEN_SOURCES} PROPERTIES GENERATED TRUE)
    add_dependencies(${target}_code recompile_${target})
    target_include_directories(${target}_code PRIVATE ${XBE_SDK_DIR}/runtime ${GEN})
    target_include_directories(${target}_code SYSTEM PRIVATE ${XBE_SDK_DIR}/runtime/cpu/freestanding)
    target_compile_definitions(${target}_code PRIVATE X86_GAME_CODE)
    target_compile_options(${target}_code PRIVATE -fms-compatibility-version=19.40 -O2 -fno-strict-aliasing -nostdinc
        -nostdinc++ -w)
    target_link_libraries(${target}_code PRIVATE ${target})
    target_link_options(${target}_code PRIVATE -fuse-ld=lld -Wl,/noentry,/nodefaultlib,/opt:noicf
        -Wl,/def:${GEN}/exports.def)
    # a stamp from an earlier build on the executable's first start would outrank this DLL
    add_custom_command(TARGET ${target}_code POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${XBE_COMPILER_DIR}/work/${target}/built.txt)
endfunction()

# Expects assets/<xbe> and the config's out = "generated/<game_dir>". At run time the game may be a disc image instead.
function(add_xbe_image target xbe config partitions game_dir)
    if(XBE_GAME_DLL)
        add_xbe_image_dll(${target} ${xbe} ${config} ${partitions} ${game_dir})
        return()
    endif()
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
    add_executable(${target} WIN32 ${XBE_RUNTIME_SOURCES} ${XBE_HOST_SOURCES} ${GAME_SOURCES} ${GEN_SOURCES} ${XBE_IMGUI_SOURCES} ${XBE_HASH})
    add_dependencies(${target} recompile_${target})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${XBE_SDK_DIR}/runtime ${GEN})
    target_include_directories(${target} SYSTEM PRIVATE ${imgui_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(${target} PRIVATE tomlplusplus::tomlplusplus $<LINK_ONLY:SDL3::SDL3-static> xbe_fsr2 dinput8 dxguid xaudio2 ole32 windowscodecs dbghelp winmm dwmapi ws2_32)
    # the Vulkan renderer's shader compiler, beside the exe
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll $<TARGET_FILE_DIR:${target}>)
    # lld keeps the recompiled weak functions overridable by the host ones
    target_link_options(${target} PRIVATE -fuse-ld=lld -g)
endfunction()
