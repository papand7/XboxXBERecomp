#pragma once
#include <cstdint>
#include "gpu/texture.h"

namespace gpu {
uint64_t TextureKey(const TextureDesc& d, const uint8_t* guest, uint64_t palette_hash);
void TexturePackSeen(const TextureDesc& d, const uint8_t* guest, const uint32_t* palette, uint64_t palette_hash);
}