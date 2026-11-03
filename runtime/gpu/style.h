#pragma once
// Graphics styles: the picture's look on top of the console's rendering, switched while playing. Original draws as
// the console does; Enhanced, Toon and Retro add a scene pass (before the HUD) and per-draw overrides; custom presets
// come from mods/presets/*.toml beside the exe. The values live in graphics.toml beside the exe. The Vulkan renderer
// draws them; the Direct3D ones draw Original whatever is chosen. Format: docs/graphics_styles.md.
#include <cstdint>
#include <string>
#include <vector>

namespace gpu {

enum StyleMode : int { kStyleOriginal, kStyleEnhanced, kStyleToon, kStyleRetro, kStyleModes };
enum TexFilter : int { kFilterGame, kFilterNearest, kFilterAnisotropic };
enum Tonemap : int { kToneOff, kToneFilmic, kToneReinhard };

struct GraphicsStyle {
    std::string preset = "original";  // the preset the values came from ("" once edited)
    int mode = kStyleOriginal;
    // scene post-processing (Enhanced; Toon and Retro use the grading too)
    float exposure = 1.0f;
    int tonemap = kToneOff;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float temperature = 0.0f;  // -1 cool to 1 warm
    float sharpen = 0.0f;
    float bloom = 0.0f;
    float bloom_threshold = 0.75f;
    float ao = 0.0f;  // screen-space ambient occlusion strength
    float ao_radius = 1.0f;
    float vignette = 0.0f;
    float grain = 0.0f;
    int tex_filter = kFilterGame;  // the 3D scene's textures; 2D keeps the game's
    // toon
    int bands = 4;
    float outline = 1.5f;  // console pixels
    float outline_color[3] = {0.04f, 0.04f, 0.06f};
    bool internal_edges = true;
    float edge_threshold = 0.2f;
    float halftone = 0.0f;
    // retro
    int pixel = 3;          // console pixels per block
    int color_bits = 5;     // per channel
    bool dither = true;
    float retro_tint = 0.4f;
    bool vertex_snap = true;
    // texture packs: mods/textures/<hash>.png in place of the game's, and dumping the game's there
    bool texture_pack = true;
    bool dump_textures = false;
};

// the current values (render thread; the settings window runs there too) and a count bumped on each change
GraphicsStyle& Style();
uint64_t StyleVersion();
void StyleChanged();  // saved to graphics.toml shortly after

// the presets: the four built in, then mods/presets/*.toml in name order
struct StylePreset {
    std::string name, label, file;
    GraphicsStyle values;
};
const std::vector<StylePreset>& StylePresets();
void LoadStyle();       // graphics.toml and the presets, once at start
void ApplyPreset(const StylePreset& p);
void SaveStyleIfDue();  // a changed style written once it has held for a second

// the settings window's Graphics Style tab; vulkan says whether the running renderer draws styles
void DrawStyleSettings(bool vulkan);
bool StyleRendererActive();  // the Vulkan renderer runs (set by it)
void SetStyleRendererActive(bool on);

}  // namespace gpu
