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
// graphics quality, apart from the style: Low, Medium, High, Ultra set the values below and the settings' anisotropic
// filtering and FXAA together; Custom is any other mix (a value changed by itself)
enum Quality : int { kQualityLow, kQualityMedium, kQualityHigh, kQualityUltra, kQualityCustom, kQualities };
// screen-space reflections on glass and the surfaces mods/reflections.toml lists: off, low (fewer ray steps), high
enum Reflections : int { kReflectOff, kReflectLow, kReflectHigh, kReflectLevels };

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
    // quality: ambient occlusion 0 off, 1 low, 2 high; bloom; drawn whatever the style (Vulkan)
    int quality = kQualityCustom;
    int quality_ao = 0;
    bool quality_bloom = false;
    // reflections (Vulkan; every style but Retro): the quality, how strong, glass found by itself (see ReflectionOf)
    int reflections = kReflectOff;
    float reflection_strength = 0.5f;
    bool reflections_auto = true;
    // soft particles (Vulkan; every style but Retro): smoke, flashes and sparks fade where they meet the scene
    // instead of cutting through it; the fade's depth in parts of the particle's distance
    bool soft_particles = false;
    float particle_fade = 0.03f;
    bool effect_lights = false;   // glowing effects (flashes, fire, lamps' glows) light what is near them (Vulkan)
    float light_strength = 1.0f;  // 0 to 3
    // light shafts and fog (Vulkan; every style but Retro): rays from the sky and bright openings through the air, and
    // distance fog in the sky's colour
    bool atmosphere = true;       // fog (the shafts' strength 0: they were too faint for their cost)
    float shaft_strength = 0.0f;  // 0 to 3
    float fog_strength = 0.5f;    // 0 to 1
    // bounced light (Vulkan; every style but Retro): screen-space, what surrounds a surface at its distance lights it
    // in its colour: rooms take on their lights' and walls' colour
    bool bounce_light = true;
    float bounce_strength = 1.0f;  // 0 to 3
    // materials (Vulkan; every style but Retro): a texture pack's normal and roughness maps (hd_pack --pbr) drawn over
    // the game's own lighting: bumps and a specular sheen; at the next start (the maps are read with the pack)
    bool materials = false;
    float material_strength = 0.7f;
    float surface_depth = 0.0f;  // relief: parallax occlusion mapping of the height map, 0 off to 2
    // ground detail (Vulkan; every style but Retro): the scenery's tiling textures get fine detail and bumps up close
    // by their kind (grass, snow, rock, dirt, concrete: gpu/detail.h), fading out by detail_distance metres
    bool ground_detail = true;
    float detail_strength = 0.6f;   // 0 to 1
    float detail_distance = 14.0f;  // metres
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

// a quality preset: its values set (the settings' anisotropic filtering and FXAA too, saved); the preset these
// values make, or Custom
void ApplyQuality(int q);
int MatchQuality();
int MatchQuality(const GraphicsStyle& s, uint32_t aniso, bool fxaa, bool smaa);
// Low to Ultra's; High and Ultra take SMAA under the Vulkan renderer, FXAA under the others (which have no SMAA)
void QualityValues(int q, uint32_t& aniso, bool& fxaa, bool& smaa, int& ao, bool& bloom);

// from another thread (the game's own menus): the style as of the last frame, and one to take at the next frame
GraphicsStyle StyleSnapshot();
void RequestStyle(const GraphicsStyle& s);
void TakeRequestedStyle();  // the render thread, once a frame (SaveStyleIfDue does it)
// the scene pass is drawn: a style other than Original, quality's AO or bloom, reflections or SMAA
bool ScenePassWanted();
// the style's own full screen pass is drawn (all of the above but SMAA alone)
bool StylePassWanted();
// mods/reflections.toml (read again when it changes): a texture's strength by its pack key (as mods/textures names
// it), 0 never; -1 when it is not listed
float ReflectionOf(uint64_t texture_key);
void CheckReflectionList();  // the render thread, once a frame (SaveStyleIfDue does it)
// what the scene pass draws for each style's values with quality's AO and bloom over them
float QualityAo(const GraphicsStyle& s);
float QualityBloom(const GraphicsStyle& s);

// the settings window's Graphics Style tab; vulkan says whether the running renderer draws styles
void DrawStyleSettings(bool vulkan);
bool StyleRendererActive();  // the Vulkan renderer runs (set by it)
void SetStyleRendererActive(bool on);

}  // namespace gpu
