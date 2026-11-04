#pragma once
// Texture packs (texture_pack.cpp): mods/textures/<key>.png in place of a game texture, and dumping the game's
#include <cstdint>
#include <string>
#include <vector>

#include "gpu/texture.h"

namespace gpu {

// a texture's pack name: its level 0 bytes, format and size (and a P8 texture's palette) hashed
uint64_t TextureKey(const TextureDesc& d, const uint8_t* guest, uint64_t palette_hash);
// B8G8R8A8 rows to a PNG file (also the Vulkan renderer's shot diagnostic)
bool WritePngFile(const std::wstring& path, const std::vector<uint32_t>& px, uint32_t w, uint32_t h);
// a renderer uploaded d's bytes: dumped when asked, and its pack picture set as its replacement when there is one
// (the next bind draws it)
// a pack picture's mean roughness (0 polished to 1 rough) from its <key>_r.png, -1 when it has none
float PackRoughness(uint64_t key);
void TexturePackSeen(const TextureDesc& d, const uint8_t* guest, const uint32_t* palette, uint64_t palette_hash);

}  // namespace gpu
