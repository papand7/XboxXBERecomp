#pragma once
// Ground detail (detail.cpp, GraphicsStyle::ground_detail; the Vulkan renderer draws it): fine detail and bumps over
// the scenery's tiling textures up close, by what each texture looks like: grass, snow, rock, dirt or concrete
#include <cstdint>
#include <vector>

#include "gpu/texture.h"

namespace gpu {

enum DetailKind : uint8_t { kDetailNone, kDetailGrass, kDetailSnow, kDetailRock, kDetailDirt, kDetailConcrete, kDetailKinds };
constexpr uint8_t kDetailUnknown = 0xFF;  // a texture not looked at yet

// a texture's kind, found once from its level 0 (key: its TextureKey) or as mods/details.toml lists it; none for one
// that is not a seamless tiling, solid picture of one of the kinds
uint8_t TextureDetail(uint64_t key, const TextureDesc& d, const uint8_t* guest, bool paletted);
// a kind's detail picture, made here (no files): kDetailSize square B8G8R8A8 levels, R and G the bumps' normal x and
// y, B the brightness (0.5 as it is), A the height
constexpr uint32_t kDetailSize = 256;
void DetailPicture(uint8_t kind, std::vector<std::vector<uint32_t>>& levels);
const char* DetailName(uint8_t kind);

}  // namespace gpu
