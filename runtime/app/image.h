#pragma once
// picture files for the host (app/image.cpp): the boot splash, texture packs and their dumps. WIC on Windows;
// elsewhere stb_image once the Linux CMake branch fetches it (phase 2 of the Linux port), until then nothing reads
#include <cstdint>
#include <filesystem>
#include <vector>

namespace app {

// a picture file (PNG, BMP, JPEG ...) as 32 bit BGRA, row after row from the top, premultiplied by its alpha or not; a
// picture without alpha comes out opaque. max_side (0 none): a wider or taller one is refused. false: error has
// the reason's code (an HRESULT on Windows) for the caller's log
bool DecodeImage(const std::filesystem::path& path, std::vector<uint32_t>& bgra, uint32_t& width, uint32_t& height,
                 bool premultiplied = false, uint32_t max_side = 0, uint32_t* error = nullptr);
// 32 bit BGRA (straight alpha) to a PNG file
bool EncodePng(const std::filesystem::path& path, const std::vector<uint32_t>& bgra, uint32_t width, uint32_t height);

}  // namespace app
