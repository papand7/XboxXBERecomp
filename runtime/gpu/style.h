#pragma once
// Graphics styles: the picture's look on top of the console's rendering, switched while playing. Original draws as
// the console does; Enhanced, Toon and Retro add a scene pass (before the HUD) and per-draw overrides; custom presets
// come from mods/presets/*.toml beside the exe. The values live in graphics.toml beside the exe. The Vulkan renderer
// draws them; the Direct3D ones draw Original whatever is chosen. Format: docs/graphics_styles.md.
#include <cstdint>
#include <string>
#include <vector>

namespace gpu {

enum StyleMode : int { kStyleOriginal, kStyleEnhanced, kStyleToon, kStyleRetro, kStyleNoir, kStyleModes };
enum TexFilter : int { kFilterGame, kFilterNearest, kFilterAnisotropic };
enum Tonemap : int { kToneOff, kToneFilmic, kToneReinhard };
// graphics quality, apart from the style: Low, Medium, High, Ultra set the values below and the settings' anisotropic
// filtering and FXAA together; Custom is any other mix (a value changed by itself)
enum Quality : int { kQualityLow, kQualityMedium, kQualityHigh, kQualityUltra, kQualityCustom, kQualities };
// screen-space reflections on glass and the surfaces mods/reflections.toml lists: off, low (fewer ray steps), high
enum Reflections : int { kReflectOff, kReflectLow, kReflectHigh, kReflectLevels };
// depth of field: off, the distance blurred, focus on what is in the middle of the view
enum DepthOfField : int { kDofOff, kDofDistance, kDofFocus, kDofModes };
enum HdrTonemap : int { kHdrToneOff, kHdrToneAgx, kHdrToneAces, kHdrTones };
// GraphicsStyle::upscaler and upscale_quality (gpu/upscale.h)
enum Upscaler : int { kUpscalerOff, kUpscalerFsr, kUpscalerDlss, kUpscalers };
enum UpscaleQuality : int {
    kUpscaleNative,
    kUpscaleQuality,
    kUpscaleBalanced,
    kUpscalePerformance,
    kUpscaleUltraPerformance,
    kUpscaleQualities
};

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
    bool relief_high = false;    // relief High: twice the steps and the surface's own shadows (Medium without)
    // ground displacement (Vulkan; every style but Retro; a prototype): the ground's draws (textures ground detail
    // finds grass, snow, rock or dirt in) cut into more triangles and pushed in by their kind's height, so the ground
    // has real lumps and hollows (seen against the sky, in the ray traced scene, its shadows); its edges stay put
    bool displacement = false;
    float displace_depth = 1.0f;  // 0 to 3, the push's scale (1: about 8 cm at the lowest, 3.5 cm on walls)
    // wall geometry (Vulkan; every style but Retro): rock and concrete walls, cliffs and floors cut the same way and
    // pushed in a little (displace_depth's scale), their corners and edges kept. Both shade the new lumps on the
    // vertex colour's baked light, refine within about 30 m and leave moving draws alone (gpu/displace.h)
    bool wall_displacement = false;
    // ground detail (Vulkan; every style but Retro): the scenery's tiling textures get fine detail and bumps up close
    // by their kind (grass, snow, rock, dirt, concrete: gpu/detail.h), fading out by detail_distance metres
    bool ground_detail = true;
    float detail_strength = 0.6f;   // 0 to 1
    float detail_distance = 14.0f;  // metres
    // ray tracing (Vulkan, a GPU with ray queries: NVIDIA RTX, AMD RX 6000 and later, Intel Arc; every style but
    // Retro): the 3D scene's triangles, as the vertex shaders place them, built into an acceleration structure each
    // frame; ambient occlusion traced against it in place of the screen-space one, so what is hidden or off screen
    // still shades
    bool ray_tracing = false;
    float rt_ao = 1.0f;      // 0 to 2
    float rt_radius = 1.5f;  // metres an occlusion ray reaches
    int rt_rays = 4;         // per pixel, 1 to 16
    bool rt_shadows = true;  // effect lights (effect_lights) cast traced shadows
    // the level lit again (Vulkan, ray tracing on): its baked light (the vertex colours) taken out and the game's own
    // lights (SetSceneLights) and a sun where the sky shows put in, each with traced shadows
    bool rt_lighting = false;
    float rt_sun = 1.0f;  // the sun's strength, 0 to 2
    // global illumination (with rt_lighting): light bounced once off the surfaces about a pixel, traced (on screen or
    // not), in their colour; in place of the screen-space bounce
    bool rt_gi = false;
    float rt_gi_strength = 1.0f;  // 0 to 2
    // volumetric light (with rt_lighting): the air lit by the sun and the game's lights, with traced shadows, so light
    // falls through windows and past walls in shafts and lamps glow in the air
    bool rt_volumetric = false;
    float rt_vol_strength = 1.0f;  // 0 to 3
    // snow (Vulkan; every style but Retro): snow falling about the camera, in the world (it stays where it is as the
    // view turns), blown by a gusting wind, a pale haze in the distance, kept out from under roofs (a traced ray up
    // from each flake with ray tracing; without, only while the ground in view is snow), and footprints where the
    // players walk on snow, which the snow covers again in a few minutes
    int weather = 0;           // 0 off, 1 on snowy levels (their ground mostly snow), 2 everywhere outdoors
    float snow_amount = 1.0f;  // 0.25 to 3 (3 a blizzard)
    bool footprints = true;
    bool snow_sparkle = true;  // tiny glints on the snowy ground near the view, twinkling as it moves
    // movies (Vulkan): a movie's 640x480 frame enlarged to the screen's size on the GPU before it is drawn, edge-aware
    // (Lanczos, kept within its neighbours so edges do not ring) and sharpened, in place of a plain stretch
    bool movie_upscale = true;
    bool movie_43 = true;
    // TAA (Vulkan): the 3D scene jittered by a sub-pixel each frame and blended with the frames before (reprojected by
    // the camera, held within each pixel's neighbours' range): smooth, steady edges; SMAA is left out meanwhile
    bool taa = false;
    // temporal upscaler (Vulkan; every style but Retro; one view; gpu/upscale.h): 0 off, 1 AMD FSR 2, 2 NVIDIA DLSS
    // (where built in and the GPU runs it, else FSR 2; an old 'xess' reads as off). The 3D scene drawn at the
    // screen's size over the quality's ratio (0 Native AA / DLAA 1x, 1 Quality 1.5x, 2 Balanced 1.7x, 3 Performance
    // 2x, 4 Ultra Performance 3x), jittered, and brought to the screen with the frames before (camera motion from the
    // depth) before the HUD; TAA and SMAA are left out meanwhile
    int upscaler = 0;
    int upscale_quality = 0;
    float upscale_sharpness = 0.3f;  // 0 to 1 (FSR's RCAS; DLSS has no sharpening of its own since 2.5.1)
    // ray tracing denoise (Vulkan): the traced bounced and volumetric light built up over frames (reprojected), less grain
    bool rt_denoise = true;  // movies shown 4:3 in the middle of a wide screen (black at the sides), not stretched
    // GPU particles (Vulkan; every style but Retro; one view): sparks, dust and chips where shots hit, fire, smoke,
    // sparks and debris from explosions (EmitParticles), simulated on the GPU in the world (gravity, drag, wind, smoke
    // and fire rising, sparks and chips bouncing off the scene's depth), drawn over the scene before its tone mapping
    bool particles = true;
    // Blood: world-space character-hit particles (game hooks) and fading red view edges when hurt (app/blood.cpp).
    bool blood = true;
    // depth of field (Vulkan; every style but Retro): the scene blurred by its distance from the focus, the gun and
    // hands kept sharp; Distance: the far scenery alone, Focus: what is in the middle of the view sharp, nearer and
    // farther blurred, more while aiming down the sights (SetAiming)
    int dof = kDofOff;
    float dof_strength = 1.0f;  // 0 to 2
    // HDR tone mapping (Vulkan; every style but Retro): the scene's effects (ray traced and effect lights, bounce,
    // fog, snow) drawn in 16 bit float, so what they light past white is kept, then brought to the screen by AgX or
    // ACES (the Enhanced style's own tone map then left out); auto exposure eases the exposure toward the scene's
    // brightness, as an eye adapts (only partly: a night stays darker than a day)
    int hdr_tonemap = kHdrToneOff;
    bool auto_exposure = false;
    // motion blur (Vulkan; every style but Retro): a little blur along the way the camera's turn and movement carried
    // each pixel since the last frame (from the depth), the gun, hands and HUD sharp
    bool motion_blur = false;
    float motion_blur_strength = 1.0f;  // 0 to 2
    // smooth turning (Vulkan, a screen of 100 Hz or more, SDR): between two of the title's frames one of the
    // renderer's, the last frame turned by the mouse the title has not read yet (GameInfo::view_turn), the HUD and the
    // gun where they were: turning the view looks twice as smooth, the world still moves at the title's rate
    bool smooth_turn = false;
    // contact shadows (Vulkan; not under ray traced lighting, which shadows for real; not in Retro): a short march from
    // each pixel toward the light above it through the depth, something just above shading it (under props, feet,
    // ledges): the crisp contact the baked lighting lacks
    bool contact_shadows = false;
    // floor reflections (Vulkan, with Reflections on): 1 ice (a bright, bluish, smooth texture), 2 also polished floors
    // (a smooth grey one): reflecting where the surface faces up, more at a grazing view
    int floor_reflections = 0;
    bool water = true;  // the water shader (Vulkan): waves, reflections, refraction, depth colour and edge foam on the
                        // textures a game names as water (AddWaterTextures, mods/reflections.toml [water])
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
// water (GraphicsStyle::water): a game's own water textures by their pack keys (GameInfo::init, before the first
// frame), and mods/reflections.toml's [water] table ("key = true" adds one, false takes one out). 1 water, 0 listed
// as not water, -1 not listed
void AddWaterTextures(const uint64_t* keys, size_t n);
int WaterTexture(uint64_t texture_key);
// the renderer drew water in its last frame (set at each present): the water's passes run only then
void SetWaterDrawn(bool drawn);
bool WaterDrawn();
// what the scene pass draws for each style's values with quality's AO and bloom over them
float QualityAo(const GraphicsStyle& s);
float QualityBloom(const GraphicsStyle& s);

// the settings window's Graphics Style tab; vulkan says whether the running renderer draws styles
void DrawStyleSettings(bool vulkan);
bool StyleRendererActive();  // the Vulkan renderer runs (set by it)
void SetStyleRendererActive(bool on);
// the title's views on screen (a split screen's players): over one, what takes one full screen camera (ray tracing,
// snow, motion blur, contact shadows, depth of field, GTAO, effect lights, reflections, smooth turning) is left out
void SetViewCount(int n);
int ViewCount();
bool RayTracingSupported();  // the Vulkan renderer's GPU has ray queries (set by it)
bool UpscalerAvailable(int upscaler);  // built in and usable: off, FSR 2, DLSS where its runtime runs on the GPU
float UpscaleRatio(int quality);      // the screen's size over the scene's, each way: 1, 1.5, 1.7, 2, 3
void SetRayTracingSupported(bool on);
// the player aims down the sights (the game's thread, each frame): depth of field's focus blurs more
bool Aiming();
void SetAiming(bool on);
// the game's lights (the game's thread, each frame): in its world's units, the frame's camera's world; lifetime 0 a
// light placed in the level, else a passing one (a muzzle flash)
struct SceneLight {
    float pos[3];
    float radius;
    float rgb[3];  // 1 the full colour
    float lifetime;
};
void SetSceneLights(const SceneLight* lights, uint32_t count);
uint32_t SceneLights(SceneLight* out, uint32_t max);  // the latest set, at most max
// the sun's direction for that lighting (towards it, the world's, +y up; a game may pick one per level)
void SetSunDirection(const float dir[3]);
void SunDirection(float out[3]);
// the players' places (the game's thread, each frame; the world's, as the lights'): footprints where they walk on snow
struct Walker {
    float pos[3];
    uint32_t id;
};
void SetWalkers(const Walker* walkers, uint32_t count);
uint32_t Walkers(Walker* out, uint32_t max);
void ClearFootprints();         // a new level
// GPU particles (GraphicsStyle::particles): the game's effects (its thread), in the world's units as the lights'; at
// most 256 wait for the renderer, which takes them once a frame (older than a quarter second dropped: a menu, a load)
enum ParticleKind : uint32_t {
    kParticleImpact,     // a shot's hit: normal the surface's, strength 1 a bullet's, rgb the surface's dust
    kParticleExplosion,  // strength the radius (world units)
    kParticleSmoke,      // a puff of smoke rising (strength its size, world units)
    kParticleFire,       // a lick of flame (strength its size, world units)
    kParticleSparks,     // sparks alone (strength 1 a bullet's)
};
struct ParticleEvent {
    uint32_t kind;
    float pos[3];
    float normal[3];
    float strength;
    float rgb[3];
    double time;  // seconds (steady clock) it was emitted
};
void EmitParticles(uint32_t kind, const float pos[3], const float normal[3], float strength, const float rgb[3]);
uint32_t TakeParticles(ParticleEvent* out, uint32_t max);  // the renderer, once a frame: the queue emptied
uint32_t FootprintEpoch();      // counts the clears
// blood that stays (GraphicsStyle::blood): a hit's stain in the world's units (the game's thread); at most 64 wait for
// the renderer, which takes them once a frame and keeps them on the level for a minute and a half
struct BloodStain {
    float pos[3];
    float normal[3];  // the surface's at the hit, up when unknown
    float strength;   // 1 a bullet's
    double time;      // seconds (steady clock) it was made
};
void AddBloodStain(const float pos[3], const float normal[3], float strength);
uint32_t TakeBloodStains(BloodStain* out, uint32_t max);

// screenshots (frame_info.cpp): the next frame as shown (the overlays drawn over it included) written as a PNG at path
// by the Vulkan renderer; any thread. The renderer takes the request at its next present and reports it written
void RequestScreenshot(const std::wstring& path);
bool ScreenshotPending();                                  // asked for, not yet written
bool TakeScreenshotRequest(std::wstring& path);            // the renderer, at each present
void ScreenshotWritten(const std::wstring& path, bool ok);  // the renderer, once it is on disk (or failed)
// the last screenshot finished: its path and whether it was written; a count bumped by each (0 none yet)
uint32_t ScreenshotResult(std::wstring& path, bool& ok);
// the renderer's last frame, for a performance overlay: the GPU's time on it (ms, from its timestamps; 0 unknown), the
// 3D scene's size as drawn (smaller than the output while an upscaler rebuilds it), the output's, the upscaler
// running (kUpscalerOff when none), and a count of the frames
struct FrameStats {
    float gpu_ms = 0;
    uint32_t scene_w = 0, scene_h = 0, out_w = 0, out_h = 0;
    int upscaler = 0;
    uint64_t frame = 0;
};
void SetFrameStats(const FrameStats& s);  // the renderer, at each present
FrameStats LastFrameStats();              // any thread

}  // namespace gpu
