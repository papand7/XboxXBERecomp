#pragma once
// Movie packs (movie_pack.cpp): a movie's frames each in a larger picture of its own, made on the player's PC (the
// HD texture pack: hd_pack upscales the frames a title collected). mods/movies/<key>.jpg beside the exe stands for the
// game's frame whose bytes hash to key (TextureKey, as texture packs), read ahead on worker threads in the order each
// movie plays them (mods/movies/<movie>.txt, a key a line) so it is in memory before it is shown; a frame not read
// yet shows the game's own. mods/movies/dump/ gets the frames as collected (<key>.png, <movie>.txt).
#include <cstdint>

namespace gpu {

// collecting: the frame in the texture at header (a movie's, any format the renderers decode), saved under its key
// once and listed in movie's order file; false when it is not a new frame
bool MovieCollect(uint32_t header, const char* movie);
// playing (GraphicsStyle::texture_pack): the key of the frame at header when the pack has a picture for it, else 0;
// asked before a title changes the frame's bytes in place
uint64_t MovieKey(uint32_t header);
// that picture as the texture's replacement (single level), once the title's own changes to the bytes are made. A
// title that changes the frame (recolours it) passes the same change for the picture: recolor(bgra, w, h, mode),
// run once per key and mode on the worker. Its picture is shown when read, else the game's frame
void MovieShow(uint32_t header, uint64_t key, uint32_t mode,
               void (*recolor)(uint32_t* bgra, uint32_t width, uint32_t height, uint32_t mode));

}  // namespace gpu
