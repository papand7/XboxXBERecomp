# Linux CI: the whole runtime (window, Vulkan renderer, sound, ImGui) built and linked into one executable around the
# stub image (no recompiled code, no game files) and the game template's sources: a compile and link check.
#   cmake -S sdk/tests/linux -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build
cmake_minimum_required(VERSION 3.25)
project(xbe_linux_check LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)

set(TEMPLATE ${XBE_SDK_DIR}/templates/game)
file(GLOB XBE_HOST_SOURCES ${TEMPLATE}/src/common/*.cpp)
file(GLOB check_sources ${TEMPLATE}/src/game/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/stub/generated/game/*.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/check_xbe.cpp "extern const char kImageSha1[] = \"0000000000000000000000000000000000000000\";\n")
add_executable(xbe_runtime_check ${check_sources} ${CMAKE_CURRENT_BINARY_DIR}/check_xbe.cpp)
target_include_directories(xbe_runtime_check PRIVATE ${TEMPLATE}/src ${CMAKE_CURRENT_SOURCE_DIR}/stub/generated/game)
xbe_runtime_target(xbe_runtime_check)
