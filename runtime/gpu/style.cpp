// Graphics styles (style.h): the values, the presets, graphics.toml and the settings window's tab
#include "gpu/style.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"

namespace gpu {

namespace {

GraphicsStyle g_style;
std::atomic<uint64_t> g_version{1};
uint64_t g_saved = 1;
std::chrono::steady_clock::time_point g_changed;
std::vector<StylePreset> g_presets;
std::atomic<bool> g_vulkan{false};
std::atomic<bool> g_ray_tracing{false};
std::atomic<bool> g_aiming{false};
std::filesystem::file_time_type g_file_time{};  // graphics.toml as last read or written
std::chrono::steady_clock::time_point g_checked;

const char* kModes[] = {"original", "enhanced", "toon", "retro", "noir"};
const char* kModeLabels[] = {"Original", "Enhanced", "Toon", "Retro", "Noir"};
const char* kFilters[] = {"game", "nearest", "anisotropic"};
const char* kTonemaps[] = {"off", "filmic", "reinhard"};
const char* kReflectNames[] = {"off", "low", "high"};
const char* kDofNames[] = {"off", "distance", "focus"};
const char* kHdrToneNames[] = {"off", "agx", "aces"};
const char* kUpscalerNames[] = {"off", "fsr", "dlss"};
const char* kUpscaleQualityNames[] = {"native", "quality", "balanced", "performance", "ultra_performance"};

std::filesystem::path StyleFile() { return app::ExeDir() / "graphics.toml"; }

// every value in range: a hand-edited file or a preset cannot hand the renderer anything it would trip on
void Clamp(GraphicsStyle& s) {
    s.mode = std::clamp(s.mode, 0, int(kStyleModes) - 1);
    s.exposure = std::clamp(s.exposure, 0.25f, 4.0f);
    s.tonemap = std::clamp(s.tonemap, 0, 2);
    s.contrast = std::clamp(s.contrast, 0.5f, 2.0f);
    s.saturation = std::clamp(s.saturation, 0.0f, 2.5f);
    s.temperature = std::clamp(s.temperature, -1.0f, 1.0f);
    s.sharpen = std::clamp(s.sharpen, 0.0f, 1.0f);
    s.bloom = std::clamp(s.bloom, 0.0f, 2.0f);
    s.bloom_threshold = std::clamp(s.bloom_threshold, 0.0f, 1.0f);
    s.ao = std::clamp(s.ao, 0.0f, 2.0f);
    s.ao_radius = std::clamp(s.ao_radius, 0.25f, 4.0f);
    s.vignette = std::clamp(s.vignette, 0.0f, 1.0f);
    s.grain = std::clamp(s.grain, 0.0f, 1.0f);
    s.tex_filter = std::clamp(s.tex_filter, 0, 2);
    s.bands = std::clamp(s.bands, 2, 12);
    s.outline = std::clamp(s.outline, 0.0f, 6.0f);
    for (float& c : s.outline_color) c = std::clamp(c, 0.0f, 1.0f);
    s.edge_threshold = std::clamp(s.edge_threshold, 0.02f, 1.0f);
    s.halftone = std::clamp(s.halftone, 0.0f, 1.0f);
    s.pixel = std::clamp(s.pixel, 1, 8);
    s.color_bits = std::clamp(s.color_bits, 2, 8);
    s.retro_tint = std::clamp(s.retro_tint, 0.0f, 1.0f);
    s.quality = std::clamp(s.quality, 0, int(kQualities) - 1);
    s.quality_ao = std::clamp(s.quality_ao, 0, 2);
    s.reflections = std::clamp(s.reflections, 0, int(kReflectLevels) - 1);
    s.reflection_strength = std::clamp(s.reflection_strength, 0.0f, 1.0f);
    s.particle_fade = std::clamp(s.particle_fade, 0.005f, 0.2f);
    s.light_strength = std::clamp(s.light_strength, 0.0f, 3.0f);
    s.material_strength = std::clamp(s.material_strength, 0.0f, 1.5f);
    s.surface_depth = std::clamp(s.surface_depth, 0.0f, 2.0f);
    s.displace_depth = std::clamp(s.displace_depth, 0.0f, 3.0f);
    s.detail_strength = std::clamp(s.detail_strength, 0.0f, 1.0f);
    s.detail_distance = std::clamp(s.detail_distance, 2.0f, 60.0f);
    s.rt_ao = std::clamp(s.rt_ao, 0.0f, 2.0f);
    s.rt_radius = std::clamp(s.rt_radius, 0.25f, 6.0f);
    s.rt_rays = std::clamp(s.rt_rays, 1, 16);
    s.rt_sun = std::clamp(s.rt_sun, 0.0f, 2.0f);
    s.rt_gi_strength = std::clamp(s.rt_gi_strength, 0.0f, 2.0f);
    s.rt_vol_strength = std::clamp(s.rt_vol_strength, 0.0f, 3.0f);
    s.weather = std::clamp(s.weather, 0, 2);
    s.snow_amount = std::clamp(s.snow_amount, 0.25f, 3.0f);
    s.dof = std::clamp(s.dof, 0, int(kDofModes) - 1);
    s.dof_strength = std::clamp(s.dof_strength, 0.0f, 2.0f);
    s.hdr_tonemap = std::clamp(s.hdr_tonemap, 0, int(kHdrTones) - 1);
    s.motion_blur_strength = std::clamp(s.motion_blur_strength, 0.0f, 2.0f);
    s.floor_reflections = std::clamp(s.floor_reflections, 0, 2);
    s.upscaler = std::clamp(s.upscaler, 0, int(kUpscalers) - 1);
    s.upscale_quality = std::clamp(s.upscale_quality, 0, int(kUpscaleQualities) - 1);
    s.upscale_sharpness = std::clamp(s.upscale_sharpness, 0.0f, 1.0f);
}

int Index(const std::string& v, const char* const* names, int n, int fallback) {
    for (int i = 0; i < n; i++)
        if (v == names[i]) return i;
    return fallback;
}

// a table's known keys into s; unknown keys and wrong types are logged and left out (where says which file)
void Read(const toml::table& t, GraphicsStyle& s, const std::string& where) {
    static const char* known[] = {"name", "base", "preset", "mode", "exposure", "tonemap", "contrast", "saturation",
                                  "temperature", "sharpen", "bloom", "bloom_threshold", "ambient_occlusion", "ao_radius",
                                  "vignette", "grain", "texture_filter", "bands", "outline", "outline_color",
                                  "internal_edges", "edge_threshold", "halftone", "pixel", "color_bits", "dither",
                                  "retro_tint", "vertex_snap", "texture_pack", "dump_textures", "quality",
                                  "quality_ao", "quality_bloom", "reflections", "reflection_strength",
                                  "reflections_auto", "soft_particles", "particle_fade",
                                  "effect_lights", "light_strength", "atmosphere", "shaft_strength", "fog_strength", "bounce_light", "bounce_strength",
                                  "materials", "material_strength", "surface_depth", "relief_high", "displacement", "displace_depth", "wall_displacement",
                                  "ground_detail", "detail_strength", "detail_distance",
                                  "ray_tracing", "rt_ao", "rt_radius", "rt_rays", "rt_shadows", "rt_lighting", "rt_sun", "rt_gi", "rt_gi_strength", "rt_volumetric", "rt_vol_strength", "weather", "snow_amount", "footprints", "snow_sparkle", "movie_upscale", "movie_43", "taa", "upscaler", "upscale_quality", "upscale_sharpness", "rt_denoise", "particles", "blood",
                                  "depth_of_field", "dof_strength", "hdr_tonemap", "auto_exposure",
                                  "motion_blur", "motion_blur_strength",
                                  "smooth_turn", "contact_shadows", "floor_reflections", "water"};
    for (auto&& [k, v] : t) {
        bool ok = false;
        for (const char* n : known) ok |= k.str() == n;
        if (!ok) LOGR("graphics: %s: unknown key '%s' ignored", where.c_str(), std::string(k.str()).c_str());
    }
    auto num = [&](const char* key, float& out) {
        if (auto v = t[key].value<double>()) out = float(*v);
        else if (t.contains(key)) LOGR("graphics: %s: '%s' is not a number, ignored", where.c_str(), key);
    };
    auto integer = [&](const char* key, int& out) {
        if (auto v = t[key].value<int64_t>()) out = int(*v);
        else if (t.contains(key)) LOGR("graphics: %s: '%s' is not a whole number, ignored", where.c_str(), key);
    };
    auto flag = [&](const char* key, bool& out) {
        if (auto v = t[key].value<bool>()) out = *v;
        else if (t.contains(key)) LOGR("graphics: %s: '%s' is not true or false, ignored", where.c_str(), key);
    };
    auto named = [&](const char* key, int& out, const char* const* names, int n) {
        if (auto v = t[key].value<std::string>()) {
            int i = Index(*v, names, n, -1);
            if (i < 0) LOGR("graphics: %s: '%s' = '%s' is not one of the choices, ignored", where.c_str(), key, v->c_str());
            else out = i;
        }
    };
    named("mode", s.mode, kModes, kStyleModes);
    num("exposure", s.exposure);
    named("tonemap", s.tonemap, kTonemaps, 3);
    num("contrast", s.contrast);
    num("saturation", s.saturation);
    num("temperature", s.temperature);
    num("sharpen", s.sharpen);
    num("bloom", s.bloom);
    num("bloom_threshold", s.bloom_threshold);
    num("ambient_occlusion", s.ao);
    num("ao_radius", s.ao_radius);
    num("vignette", s.vignette);
    num("grain", s.grain);
    named("texture_filter", s.tex_filter, kFilters, 3);
    integer("bands", s.bands);
    num("outline", s.outline);
    if (auto* a = t["outline_color"].as_array(); a && a->size() == 3) {
        for (size_t i = 0; i < 3; i++) s.outline_color[i] = float((*a)[i].value_or(0.0));
    }
    flag("internal_edges", s.internal_edges);
    num("edge_threshold", s.edge_threshold);
    num("halftone", s.halftone);
    integer("pixel", s.pixel);
    integer("color_bits", s.color_bits);
    flag("dither", s.dither);
    num("retro_tint", s.retro_tint);
    flag("vertex_snap", s.vertex_snap);
    flag("texture_pack", s.texture_pack);
    flag("dump_textures", s.dump_textures);
    static const char* quality_names[] = {"low", "medium", "high", "ultra", "custom"};
    named("quality", s.quality, quality_names, 5);
    integer("quality_ao", s.quality_ao);
    flag("quality_bloom", s.quality_bloom);
    named("reflections", s.reflections, kReflectNames, 3);
    num("reflection_strength", s.reflection_strength);
    flag("reflections_auto", s.reflections_auto);
    flag("soft_particles", s.soft_particles);
    num("particle_fade", s.particle_fade);
    flag("effect_lights", s.effect_lights);
    num("light_strength", s.light_strength);
    flag("atmosphere", s.atmosphere);
    num("shaft_strength", s.shaft_strength);
    num("fog_strength", s.fog_strength);
    flag("bounce_light", s.bounce_light);
    num("bounce_strength", s.bounce_strength);
    flag("materials", s.materials);
    num("material_strength", s.material_strength);
    num("surface_depth", s.surface_depth);
    flag("relief_high", s.relief_high);
    flag("displacement", s.displacement);
    num("displace_depth", s.displace_depth);
    flag("wall_displacement", s.wall_displacement);
    flag("ground_detail", s.ground_detail);
    num("detail_strength", s.detail_strength);
    num("detail_distance", s.detail_distance);
    flag("ray_tracing", s.ray_tracing);
    num("rt_ao", s.rt_ao);
    num("rt_radius", s.rt_radius);
    integer("rt_rays", s.rt_rays);
    flag("rt_shadows", s.rt_shadows);
    flag("rt_lighting", s.rt_lighting);
    num("rt_sun", s.rt_sun);
    flag("rt_gi", s.rt_gi);
    num("rt_gi_strength", s.rt_gi_strength);
    flag("rt_volumetric", s.rt_volumetric);
    num("rt_vol_strength", s.rt_vol_strength);
    integer("weather", s.weather);
    num("snow_amount", s.snow_amount);
    flag("footprints", s.footprints);
    flag("snow_sparkle", s.snow_sparkle);
    flag("movie_upscale", s.movie_upscale);
    flag("movie_43", s.movie_43);
    flag("taa", s.taa);
    // Intel XeSS was a choice once (never built in, drawn as off): an old file's 'xess' is off
    if (auto v = t["upscaler"].value<std::string>(); v && *v == "xess") s.upscaler = kUpscalerOff;
    else named("upscaler", s.upscaler, kUpscalerNames, int(kUpscalers));
    named("upscale_quality", s.upscale_quality, kUpscaleQualityNames, int(kUpscaleQualities));
    num("upscale_sharpness", s.upscale_sharpness);
    flag("rt_denoise", s.rt_denoise);
    flag("particles", s.particles);
    flag("blood", s.blood);
    named("depth_of_field", s.dof, kDofNames, 3);
    num("dof_strength", s.dof_strength);
    named("hdr_tonemap", s.hdr_tonemap, kHdrToneNames, 3);
    flag("auto_exposure", s.auto_exposure);
    flag("motion_blur", s.motion_blur);
    num("motion_blur_strength", s.motion_blur_strength);
    flag("smooth_turn", s.smooth_turn);
    flag("contact_shadows", s.contact_shadows);
    integer("floor_reflections", s.floor_reflections);
    flag("water", s.water);
    Clamp(s);
}

toml::table Write(const GraphicsStyle& s) {
    return toml::table{{"preset", s.preset},
                       {"mode", kModes[s.mode]},
                       {"exposure", double(s.exposure)},
                       {"tonemap", kTonemaps[s.tonemap]},
                       {"contrast", double(s.contrast)},
                       {"saturation", double(s.saturation)},
                       {"temperature", double(s.temperature)},
                       {"sharpen", double(s.sharpen)},
                       {"bloom", double(s.bloom)},
                       {"bloom_threshold", double(s.bloom_threshold)},
                       {"ambient_occlusion", double(s.ao)},
                       {"ao_radius", double(s.ao_radius)},
                       {"vignette", double(s.vignette)},
                       {"grain", double(s.grain)},
                       {"texture_filter", kFilters[s.tex_filter]},
                       {"bands", int64_t(s.bands)},
                       {"outline", double(s.outline)},
                       {"outline_color", toml::array{double(s.outline_color[0]), double(s.outline_color[1]),
                                                     double(s.outline_color[2])}},
                       {"internal_edges", s.internal_edges},
                       {"edge_threshold", double(s.edge_threshold)},
                       {"halftone", double(s.halftone)},
                       {"pixel", int64_t(s.pixel)},
                       {"color_bits", int64_t(s.color_bits)},
                       {"dither", s.dither},
                       {"retro_tint", double(s.retro_tint)},
                       {"vertex_snap", s.vertex_snap},
                       {"texture_pack", s.texture_pack},
                       {"dump_textures", s.dump_textures},
                       {"quality", std::array<const char*, 5>{"low", "medium", "high", "ultra", "custom"}[size_t(s.quality)]},
                       {"quality_ao", int64_t(s.quality_ao)},
                       {"quality_bloom", s.quality_bloom},
                       {"reflections", kReflectNames[s.reflections]},
                       {"reflection_strength", double(s.reflection_strength)},
                       {"reflections_auto", s.reflections_auto},
                       {"soft_particles", s.soft_particles},
                       {"particle_fade", double(s.particle_fade)},
                       {"effect_lights", s.effect_lights},
                       {"light_strength", double(s.light_strength)},
                       {"atmosphere", s.atmosphere},
                       {"shaft_strength", double(s.shaft_strength)},
                       {"fog_strength", double(s.fog_strength)},
                       {"bounce_light", s.bounce_light},
                       {"bounce_strength", double(s.bounce_strength)},
                       {"materials", s.materials},
                       {"material_strength", double(s.material_strength)},
                       {"surface_depth", double(s.surface_depth)},
                       {"relief_high", s.relief_high},
                       {"displacement", s.displacement},
                       {"displace_depth", double(s.displace_depth)},
                       {"wall_displacement", s.wall_displacement},
                       {"ground_detail", s.ground_detail},
                       {"detail_strength", double(s.detail_strength)},
                       {"detail_distance", double(s.detail_distance)},
                       {"ray_tracing", s.ray_tracing},
                       {"rt_ao", double(s.rt_ao)},
                       {"rt_radius", double(s.rt_radius)},
                       {"rt_rays", int64_t(s.rt_rays)},
                       {"rt_shadows", s.rt_shadows},
                       {"rt_lighting", s.rt_lighting},
                       {"rt_sun", double(s.rt_sun)},
                       {"rt_gi", s.rt_gi},
                       {"rt_gi_strength", double(s.rt_gi_strength)},
                       {"rt_volumetric", s.rt_volumetric},
                       {"rt_vol_strength", double(s.rt_vol_strength)},
                       {"weather", s.weather},
                       {"snow_amount", double(s.snow_amount)},
                       {"footprints", s.footprints},
                       {"snow_sparkle", s.snow_sparkle},
                       {"movie_upscale", s.movie_upscale},
                       {"movie_43", s.movie_43},
                       {"taa", s.taa},
                       {"upscaler", kUpscalerNames[s.upscaler]},
                       {"upscale_quality", kUpscaleQualityNames[s.upscale_quality]},
                       {"upscale_sharpness", double(s.upscale_sharpness)},
                       {"rt_denoise", s.rt_denoise},
                       {"particles", s.particles},
                       {"blood", s.blood},
                       {"depth_of_field", kDofNames[s.dof]},
                       {"dof_strength", double(s.dof_strength)},
                       {"hdr_tonemap", kHdrToneNames[s.hdr_tonemap]},
                       {"auto_exposure", s.auto_exposure},
                       {"motion_blur", s.motion_blur},
                       {"motion_blur_strength", double(s.motion_blur_strength)},
                       {"smooth_turn", s.smooth_turn},
                       {"contact_shadows", s.contact_shadows},
                       {"floor_reflections", s.floor_reflections},
                       {"water", s.water}};
}

// the built-in presets
std::vector<StylePreset> BuiltIn() {
    std::vector<StylePreset> v;
    GraphicsStyle o;  // the defaults are Original's
    v.push_back({"original", "Original", "", o});
    GraphicsStyle e;
    e.preset = "enhanced";
    e.mode = kStyleEnhanced;
    e.exposure = 1.05f;
    e.tonemap = kToneFilmic;
    e.contrast = 1.08f;
    e.saturation = 1.1f;
    e.temperature = 0.05f;
    e.sharpen = 0.35f;
    e.bloom = 0.2f;
    e.bloom_threshold = 0.85f;
    e.ao = 0.8f;
    e.vignette = 0.2f;
    e.tex_filter = kFilterAnisotropic;
    v.push_back({"enhanced", "Enhanced", "", e});
    GraphicsStyle t;
    t.preset = "toon";
    t.mode = kStyleToon;
    t.saturation = 1.3f;
    t.contrast = 1.05f;
    t.bands = 5;
    t.outline = 1.5f;
    t.halftone = 0.0f;
    t.tex_filter = kFilterAnisotropic;
    v.push_back({"toon", "Toon", "", t});
    GraphicsStyle r;
    r.preset = "retro";
    r.mode = kStyleRetro;
    r.pixel = 3;
    r.color_bits = 5;
    r.dither = true;
    r.retro_tint = 0.4f;
    r.vertex_snap = true;
    r.tex_filter = kFilterNearest;
    v.push_back({"retro", "Retro", "", r});
    GraphicsStyle n;  // toon's bands and outlines in black, white and red
    n.preset = "noir";
    n.mode = kStyleNoir;
    n.contrast = 1.15f;
    n.bands = 4;
    n.outline = 1.5f;
    n.outline_color[0] = n.outline_color[1] = n.outline_color[2] = 0.0f;
    n.halftone = 0.3f;
    n.tex_filter = kFilterAnisotropic;
    v.push_back({"noir", "Noir", "", n});
    return v;
}

}  // namespace

GraphicsStyle& Style() { return g_style; }

namespace {
std::mutex g_handoff;
GraphicsStyle g_snapshot;   // the style as the last frame drew it
GraphicsStyle g_requested;  // a menu's, taken at the next frame
bool g_has_request = false;

// graphics.toml through a temporary file: a failed write leaves the old one and is not counted as saved
bool WriteStyleFile(const GraphicsStyle& s, const char* header) {
    std::filesystem::path f = StyleFile(), tmp = f;
    tmp += ".tmp";
    std::error_code ec;
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << header << Write(s) << "\n";
        out.close();
        if (!out) {
            std::filesystem::remove(tmp, ec);
            LOG_FIRST(4, "graphics: graphics.toml could not be written");
            return false;
        }
    }
    std::filesystem::rename(tmp, f, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        LOG_FIRST(4, "graphics: graphics.toml could not be replaced");
        return false;
    }
    return true;
}
}  // namespace

// without the Vulkan renderer nothing draws from the style: the menu's thread reads and writes it at once
GraphicsStyle StyleSnapshot() {
    LoadStyle();
    std::lock_guard lock(g_handoff);
    return g_vulkan ? g_snapshot : g_style;
}
void RequestStyle(const GraphicsStyle& s) {
    std::lock_guard lock(g_handoff);
    if (!g_vulkan) {
        g_style = s;
        StyleChanged();
        g_changed = {};  // saved at once below
        g_saved = 0;
    } else {
        g_requested = s;
        g_has_request = true;
        return;
    }
    // (outside the render thread's frame loop: written here)
    if (WriteStyleFile(g_style, "# graphics styles (docs/graphics_styles.md); written by the game's settings\n"))
        g_saved = g_version.load();
}
void TakeRequestedStyle() {
    std::lock_guard lock(g_handoff);
    if (g_has_request) {
        g_has_request = false;
        g_style = g_requested;
        StyleChanged();
    }
    g_snapshot = g_style;
}
uint64_t StyleVersion() { return g_version.load(); }
void StyleChanged() {
    Clamp(g_style);
    g_version++;
    g_changed = std::chrono::steady_clock::now();
}
const std::vector<StylePreset>& StylePresets() { return g_presets; }
bool StyleRendererActive() { return g_vulkan.load(); }
void SetStyleRendererActive(bool on) { g_vulkan = on; }
bool RayTracingSupported() { return g_ray_tracing.load(); }
void SetRayTracingSupported(bool on) { g_ray_tracing = on; }
bool Aiming() { return g_aiming.load(std::memory_order_relaxed); }
void SetAiming(bool on) { g_aiming.store(on, std::memory_order_relaxed); }

namespace {
std::mutex g_lights_lock;
std::vector<SceneLight> g_lights;
float g_sun[3] = {0.42f, 0.80f, 0.43f};  // high and to one side
}  // namespace
void SetSunDirection(const float dir[3]) {
    std::lock_guard<std::mutex> lock(g_lights_lock);
    std::copy(dir, dir + 3, g_sun);
}
void SunDirection(float out[3]) {
    std::lock_guard<std::mutex> lock(g_lights_lock);
    std::copy(g_sun, g_sun + 3, out);
}
namespace {
std::vector<Walker> g_walkers;
std::atomic<uint32_t> g_print_epoch{0};
}  // namespace
void SetWalkers(const Walker* walkers, uint32_t count) {
    std::lock_guard<std::mutex> lock(g_lights_lock);
    g_walkers.assign(walkers, walkers + count);
}
uint32_t Walkers(Walker* out, uint32_t max) {
    std::lock_guard<std::mutex> lock(g_lights_lock);
    uint32_t n = std::min<uint32_t>(max, uint32_t(g_walkers.size()));
    std::copy(g_walkers.begin(), g_walkers.begin() + n, out);
    return n;
}
void ClearFootprints() { g_print_epoch.fetch_add(1); }
uint32_t FootprintEpoch() { return g_print_epoch.load(); }
namespace {
std::mutex g_particles_lock;
std::vector<ParticleEvent> g_particles;
double ParticleClock() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace
void EmitParticles(uint32_t kind, const float pos[3], const float normal[3], float strength, const float rgb[3]) {
    ParticleEvent e{kind, {pos[0], pos[1], pos[2]}, {0, 1, 0}, strength, {0.5f, 0.5f, 0.5f}, ParticleClock()};
    if (normal) std::copy(normal, normal + 3, e.normal);
    if (rgb) std::copy(rgb, rgb + 3, e.rgb);
    std::lock_guard<std::mutex> lock(g_particles_lock);
    if (g_particles.size() < 256) g_particles.push_back(e);
}
uint32_t TakeParticles(ParticleEvent* out, uint32_t max) {
    const double now = ParticleClock();
    std::lock_guard<std::mutex> lock(g_particles_lock);
    uint32_t n = 0;
    for (const ParticleEvent& e : g_particles)
        if (n < max && now - e.time < 0.25) out[n++] = e;
    g_particles.clear();
    return n;
}
void SetSceneLights(const SceneLight* lights, uint32_t count) {
    std::lock_guard<std::mutex> lock(g_lights_lock);
    g_lights.assign(lights, lights + count);
}
uint32_t SceneLights(SceneLight* out, uint32_t max) {
    std::lock_guard<std::mutex> lock(g_lights_lock);
    uint32_t n = std::min<uint32_t>(max, uint32_t(g_lights.size()));
    std::copy(g_lights.begin(), g_lights.begin() + n, out);
    return n;
}

namespace {
void LoadStyleOnce() {
    g_presets = BuiltIn();
    // mods/presets/*.toml: a name, a base preset, then any of graphics.toml's keys; no code runs from them
    std::error_code ec;
    std::filesystem::path dir = app::ExeDir() / "mods" / "presets";
    std::vector<std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".toml") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        std::string where = f.filename().string();
        try {
            toml::table t = toml::parse_file(f.string());
            StylePreset p;
            p.file = f.string();
            p.label = t["name"].value_or(f.stem().string());
            std::string base = t["base"].value_or(std::string("original"));
            int b = Index(base, kModes, kStyleModes, -1);
            if (b < 0) {
                LOGR("graphics: %s: base '%s' is not original, enhanced, toon, retro or noir; preset left out", where.c_str(),
                     base.c_str());
                continue;
            }
            p.values = g_presets[size_t(b)].values;
            Read(t, p.values, where);
            p.name = "custom:" + f.stem().string();
            p.values.preset = p.name;
            g_presets.push_back(std::move(p));
            LOGR("graphics: preset '%s' from %s", g_presets.back().label.c_str(), where.c_str());
        } catch (const toml::parse_error& e) {
            LOGR("graphics: %s is not valid TOML (%s), left out", where.c_str(), std::string(e.description()).c_str());
        }
    }
    try {
        if (std::filesystem::exists(StyleFile(), ec)) {
            toml::table t = toml::parse_file(StyleFile().string());
            Read(t, g_style, "graphics.toml");
            g_style.preset = t["preset"].value_or(std::string());
        }
    } catch (const toml::parse_error& e) {
        LOGR("graphics: graphics.toml is not valid TOML (%s), defaults used", std::string(e.description()).c_str());
        g_style = GraphicsStyle{};
    }
    g_saved = g_version.load();
    g_file_time = std::filesystem::last_write_time(StyleFile(), ec);
    g_snapshot = g_style;
    LOGR("graphics: style %s (%s), %zu presets", kModes[g_style.mode], g_style.preset.empty() ? "custom" : g_style.preset.c_str(),
         g_presets.size());
}
}  // namespace

void LoadStyle() {
    // the menu's thread (StyleSnapshot) and the render thread both ask: one loads, the other waits for it
    static std::once_flag loaded;
    std::call_once(loaded, LoadStyleOnce);
}

void ApplyPreset(const StylePreset& p) {
    // texture settings and quality are not a look: kept
    std::lock_guard lock(g_handoff);
    GraphicsStyle keep = g_style;
    g_style = p.values;
    g_style.preset = p.name;
    g_style.texture_pack = keep.texture_pack;
    g_style.dump_textures = keep.dump_textures;
    g_style.quality = keep.quality;
    g_style.quality_ao = keep.quality_ao;
    g_style.quality_bloom = keep.quality_bloom;
    g_style.reflections = keep.reflections;
    g_style.reflection_strength = keep.reflection_strength;
    g_style.reflections_auto = keep.reflections_auto;
    g_style.soft_particles = keep.soft_particles;
    g_style.particle_fade = keep.particle_fade;
    g_style.effect_lights = keep.effect_lights;
    g_style.light_strength = keep.light_strength;
    g_style.atmosphere = keep.atmosphere;
    g_style.shaft_strength = keep.shaft_strength;
    g_style.fog_strength = keep.fog_strength;
    g_style.bounce_light = keep.bounce_light;
    g_style.bounce_strength = keep.bounce_strength;
    g_style.materials = keep.materials;
    g_style.material_strength = keep.material_strength;
    g_style.surface_depth = keep.surface_depth;
    g_style.relief_high = keep.relief_high;
    g_style.displacement = keep.displacement;
    g_style.displace_depth = keep.displace_depth;
    g_style.wall_displacement = keep.wall_displacement;
    g_style.ground_detail = keep.ground_detail;
    g_style.detail_strength = keep.detail_strength;
    g_style.detail_distance = keep.detail_distance;
    g_style.ray_tracing = keep.ray_tracing;
    g_style.rt_ao = keep.rt_ao;
    g_style.rt_radius = keep.rt_radius;
    g_style.rt_rays = keep.rt_rays;
    g_style.rt_shadows = keep.rt_shadows;
    g_style.rt_lighting = keep.rt_lighting;
    g_style.rt_sun = keep.rt_sun;
    g_style.rt_gi = keep.rt_gi;
    g_style.rt_gi_strength = keep.rt_gi_strength;
    g_style.rt_volumetric = keep.rt_volumetric;
    g_style.rt_vol_strength = keep.rt_vol_strength;
    g_style.weather = keep.weather;
    g_style.snow_amount = keep.snow_amount;
    g_style.footprints = keep.footprints;
    g_style.snow_sparkle = keep.snow_sparkle;
    g_style.movie_upscale = keep.movie_upscale;
    g_style.movie_43 = keep.movie_43;
    g_style.taa = keep.taa;
    g_style.upscaler = keep.upscaler;
    g_style.upscale_quality = keep.upscale_quality;
    g_style.upscale_sharpness = keep.upscale_sharpness;
    g_style.rt_denoise = keep.rt_denoise;
    g_style.particles = keep.particles;
    g_style.blood = keep.blood;
    g_style.dof = keep.dof;
    g_style.dof_strength = keep.dof_strength;
    g_style.hdr_tonemap = keep.hdr_tonemap;
    g_style.auto_exposure = keep.auto_exposure;
    g_style.motion_blur = keep.motion_blur;
    g_style.motion_blur_strength = keep.motion_blur_strength;
    g_style.smooth_turn = keep.smooth_turn;
    g_style.contact_shadows = keep.contact_shadows;
    g_style.floor_reflections = keep.floor_reflections;
    g_style.water = keep.water;
    StyleChanged();
}

namespace {
// the presets' values: anisotropic filtering (0 as the game asks), FXAA, SMAA, AO, bloom
struct QualitySet {
    uint32_t aniso;
    bool fxaa;
    bool smaa;
    int ao;
    bool bloom;
};
constexpr QualitySet kQuality[4] = {
    {0, false, false, 0, false}, {4, true, false, 0, true}, {8, false, true, 1, true}, {16, false, true, 2, true}};
// a renderer without SMAA takes FXAA in its place
QualitySet Drawn(const QualitySet& v) {
    QualitySet d = v;
    if (!g_vulkan && d.smaa) d.fxaa = true, d.smaa = false;
    return d;
}
}  // namespace

void ApplyQuality(int q) {
    if (q < 0 || q >= kQualityCustom) {
        std::lock_guard lock(g_handoff);
        g_style.quality = kQualityCustom;
        StyleChanged();
        return;
    }
    const QualitySet v = Drawn(kQuality[q]);
    {
        std::lock_guard<std::recursive_mutex> lock(app::UiLock());
        app::Settings& s = app::Get();
        s.aniso = v.aniso;
        s.fxaa = v.fxaa;
        s.smaa = v.smaa;
        app::Save();
    }
    std::lock_guard lock(g_handoff);
    g_style.quality = q;
    g_style.quality_ao = v.ao;
    g_style.quality_bloom = v.bloom;
    StyleChanged();
}

int MatchQuality(const GraphicsStyle& st, uint32_t aniso, bool fxaa, bool smaa) {
    if (!g_vulkan) smaa = false;  // kept in the settings, not drawn
    for (int q = 0; q < kQualityCustom; q++) {
        const QualitySet v = Drawn(kQuality[q]);
        if (aniso == v.aniso && fxaa == v.fxaa && smaa == v.smaa && st.quality_ao == v.ao &&
            st.quality_bloom == v.bloom)
            return q;
    }
    return kQualityCustom;
}
int MatchQuality() { return MatchQuality(g_style, app::Get().aniso, app::Get().fxaa, app::Get().smaa); }
void QualityValues(int q, uint32_t& aniso, bool& fxaa, bool& smaa, int& ao, bool& bloom) {
    const QualitySet v = Drawn(kQuality[std::clamp(q, 0, 3)]);
    aniso = v.aniso, fxaa = v.fxaa, smaa = v.smaa, ao = v.ao, bloom = v.bloom;
}

std::atomic<bool> g_water_drawn{false};
void SetWaterDrawn(bool drawn) { g_water_drawn.store(drawn); }
bool WaterDrawn() { return g_water_drawn.load(); }

bool StylePassWanted() {
    return g_style.mode != kStyleOriginal || g_style.quality_ao > 0 || g_style.quality_bloom ||
           ((g_style.reflections != kReflectOff || (g_style.water && WaterDrawn())) && g_style.mode != kStyleRetro);
}
std::atomic<int> g_views{1};
void SetViewCount(int n) {
    if (g_views.exchange(n) != n) LOGR("graphics: %d views on screen%s", n, n > 1 ? " (one-camera effects left out)" : "");
}
int ViewCount() { return g_views.load(); }

bool ScenePassWanted() {
    return StylePassWanted() ||
           ((app::Get().smaa || g_style.effect_lights || g_style.atmosphere || g_style.bounce_light || g_style.dof ||
             g_style.weather || g_style.particles || g_style.hdr_tonemap || g_style.motion_blur || g_style.smooth_turn || g_style.contact_shadows ||
             g_style.upscaler || (g_style.ray_tracing && g_ray_tracing.load())) &&
            g_style.mode != kStyleRetro);
}
float QualityAo(const GraphicsStyle& s) {
    static const float levels[3] = {0.0f, 0.5f, 0.9f};
    return std::max(s.mode == kStyleEnhanced ? s.ao : 0.0f, levels[s.quality_ao]);
}
float QualityBloom(const GraphicsStyle& s) {
    return std::max(s.mode == kStyleEnhanced ? s.bloom : 0.0f, s.quality_bloom ? 0.2f : 0.0f);
}

// ---- mods/reflections.toml

namespace {
std::unordered_map<uint64_t, float> g_reflect;
std::unordered_map<uint64_t, bool> g_water_mod;  // [water]
std::unordered_set<uint64_t> g_water_game;       // AddWaterTextures
std::filesystem::file_time_type g_reflect_time{};
std::chrono::steady_clock::time_point g_reflect_checked;
bool g_reflect_read = false;

void ReadReflectionList() {
    std::filesystem::path f = app::ExeDir() / "mods" / "reflections.toml";
    std::error_code ec;
    auto t = std::filesystem::last_write_time(f, ec);
    if (g_reflect_read && (ec ? g_reflect.empty() : t == g_reflect_time)) return;
    g_reflect_read = true;
    g_reflect_time = ec ? std::filesystem::file_time_type{} : t;
    g_reflect.clear();
    g_water_mod.clear();
    if (ec) return;
    try {
        toml::table tb = toml::parse_file(f.string());
        if (const toml::table* list = tb["textures"].as_table())
            for (auto&& [k, v] : *list) {
                std::string name(k.str());
                char* end = nullptr;
                uint64_t key = std::strtoull(name.c_str(), &end, 16);
                std::optional<double> s = v.value<double>();
                if (name.size() != 16 || !end || *end || !s) {
                    LOGR("graphics: reflections.toml: '%s' is not a texture's 16 hex digits = a strength, ignored",
                         name.c_str());
                    continue;
                }
                g_reflect[key] = std::clamp(float(*s), 0.0f, 1.0f);
            }
        if (const toml::table* list = tb["water"].as_table())
            for (auto&& [k, v] : *list) {
                std::string name(k.str());
                char* end = nullptr;
                uint64_t key = std::strtoull(name.c_str(), &end, 16);
                std::optional<bool> on = v.value<bool>();
                if (!on)
                    if (std::optional<double> d = v.value<double>()) on = *d != 0;
                if (name.size() != 16 || !end || *end || !on) {
                    LOGR("graphics: reflections.toml: [water] '%s' is not a texture's 16 hex digits = true or false, ignored",
                         name.c_str());
                    continue;
                }
                g_water_mod[key] = *on;
            }
        LOGR("graphics: reflections.toml lists %zu textures, %zu water", g_reflect.size(), g_water_mod.size());
    } catch (const toml::parse_error& e) {
        LOGR("graphics: reflections.toml is not valid TOML (%s), not used", std::string(e.description()).c_str());
    }
}
}  // namespace

float ReflectionOf(uint64_t texture_key) {
    auto it = g_reflect.find(texture_key);
    return it == g_reflect.end() ? -1.0f : it->second;
}

void AddWaterTextures(const uint64_t* keys, size_t n) { g_water_game.insert(keys, keys + n); }

int WaterTexture(uint64_t texture_key) {
    if (auto it = g_water_mod.find(texture_key); it != g_water_mod.end()) return it->second ? 1 : 0;
    return g_water_game.count(texture_key) ? 1 : -1;
}

void CheckReflectionList() {
    auto now = std::chrono::steady_clock::now();
    if (g_reflect_read && now - g_reflect_checked < std::chrono::seconds(1)) return;
    g_reflect_checked = now;
    ReadReflectionList();
}

void SaveStyleIfDue() {
    TakeRequestedStyle();
    CheckReflectionList();
    // graphics.toml edited outside the game (a mod author tuning a look): read again, once a second at most
    auto now = std::chrono::steady_clock::now();
    if (now - g_checked >= std::chrono::seconds(1)) {
        g_checked = now;
        std::error_code ec;
        auto t = std::filesystem::last_write_time(StyleFile(), ec);
        if (!ec && t != g_file_time) {
            g_file_time = t;
            try {
                GraphicsStyle s = g_style;
                toml::table tb = toml::parse_file(StyleFile().string());
                Read(tb, s, "graphics.toml");
                s.preset = tb["preset"].value_or(std::string());
                std::lock_guard lock(g_handoff);
                g_style = s;
                g_version++;
                g_saved = g_version.load();
                LOGR("graphics: graphics.toml read again, style %s", kModes[g_style.mode]);
            } catch (const toml::parse_error& e) {
                LOGR("graphics: graphics.toml is not valid TOML (%s), kept the style", std::string(e.description()).c_str());
            }
        }
    }
    if (g_version.load() == g_saved) return;
    if (std::chrono::steady_clock::now() - g_changed < std::chrono::seconds(1)) return;
    uint64_t version = g_version.load();
    GraphicsStyle s;
    {
        std::lock_guard lock(g_handoff);
        s = g_style;
    }
    if (!WriteStyleFile(s, "# graphics styles (docs/graphics_styles.md); written by the game's settings window\n")) {
        g_changed = std::chrono::steady_clock::now();  // tried again in a second
        return;
    }
    g_saved = version;
    std::error_code ec;
    g_file_time = std::filesystem::last_write_time(StyleFile(), ec);
}

// ---- the settings window

void DrawStyleSettings(bool vulkan) {
    LoadStyle();
    // edited as a copy and stored under the handoff lock: the menu's thread reads and writes the style too
    GraphicsStyle s;
    {
        std::lock_guard lock(g_handoff);
        s = g_style;
    }
    const StylePreset* picked = nullptr;
    bool reset = false;
    bool changed = false;
    if (!vulkan)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
                           "Styles are drawn by the Vulkan renderer (Graphics > Renderer); this one draws Original.");
    // presets
    std::string current = "Custom (edited)";
    for (auto& p : g_presets)
        if (p.name == s.preset) current = p.label;
    if (ImGui::BeginCombo("Style", current.c_str())) {
        for (auto& p : g_presets) {
            bool sel = p.name == s.preset;
            std::string label = p.file.empty() ? p.label : p.label + " (mod)";
            if (ImGui::Selectable(label.c_str(), sel)) picked = &p;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        bool pack = s.texture_pack;
        s = GraphicsStyle{};
        s.texture_pack = pack;
        reset = true;
    }
    ImGui::TextDisabled("Switches at once, in play. Original is the console's picture; the others add a scene pass\n"
                        "drawn before the HUD, so menus' and HUD's pixels keep the game's look.");
    int mode = s.mode;
    if (ImGui::Combo("Rendering", &mode, kModeLabels, kStyleModes)) s.mode = mode, changed = true;

    if (s.mode != kStyleOriginal && ImGui::CollapsingHeader("Post-processing", ImGuiTreeNodeFlags_DefaultOpen)) {
        int tm = s.tonemap;
        if (ImGui::Combo("Tone mapping", &tm, "Off\0Filmic\0Reinhard\0")) s.tonemap = tm, changed = true;
        changed |= ImGui::SliderFloat("Exposure", &s.exposure, 0.25f, 4.0f, "%.2f");
        changed |= ImGui::SliderFloat("Contrast", &s.contrast, 0.5f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Saturation", &s.saturation, 0.0f, 2.5f, "%.2f");
        changed |= ImGui::SliderFloat("Temperature", &s.temperature, -1.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Ambient occlusion", &s.ao, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("AO radius", &s.ao_radius, 0.25f, 4.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom", &s.bloom, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom threshold", &s.bloom_threshold, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Sharpening", &s.sharpen, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Vignette", &s.vignette, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Film grain", &s.grain, 0.0f, 1.0f, "%.2f");
        int f = s.tex_filter;
        if (ImGui::Combo("Scene texture filtering", &f, "As the game\0Nearest (pixelated)\0Anisotropic 16x\0"))
            s.tex_filter = f, changed = true;
    }
    if ((s.mode == kStyleToon || s.mode == kStyleNoir) && ImGui::CollapsingHeader("Toon", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt("Lighting bands", &s.bands, 2, 12);
        changed |= ImGui::SliderFloat("Outline thickness", &s.outline, 0.0f, 6.0f, "%.1f px");
        changed |= ImGui::ColorEdit3("Outline color", s.outline_color);
        changed |= ImGui::Checkbox("Internal edges", &s.internal_edges);
        changed |= ImGui::SliderFloat("Edge threshold", &s.edge_threshold, 0.02f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Halftone", &s.halftone, 0.0f, 1.0f, "%.2f");
    }
    if (s.mode == kStyleRetro && ImGui::CollapsingHeader("Retro", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderInt("Pixel size (internal resolution)", &s.pixel, 1, 8);
        changed |= ImGui::SliderInt("Color precision (bits)", &s.color_bits, 2, 8);
        changed |= ImGui::Checkbox("Ordered dithering", &s.dither);
        changed |= ImGui::SliderFloat("Retro grading", &s.retro_tint, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::Checkbox("Vertex snapping", &s.vertex_snap);
    }
    if (ImGui::CollapsingHeader("Reflections", ImGuiTreeNodeFlags_DefaultOpen)) {
        int r = s.reflections;
        if (ImGui::Combo("Reflections", &r, "Off\0Low\0High\0")) s.reflections = r, changed = true;
        changed |= ImGui::SliderFloat("Reflection strength", &s.reflection_strength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::Checkbox("Find glass by itself", &s.reflections_auto);
        ImGui::TextDisabled("Screen-space: what is on screen is mirrored in glass, and in the textures\n"
                            "mods/reflections.toml lists (docs/graphics_styles.md). Not drawn in Retro.");
    }
    if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Soft particles", &s.soft_particles);
        changed |= ImGui::SliderFloat("Fade depth", &s.particle_fade, 0.005f, 0.2f, "%.3f");
        ImGui::TextDisabled("Smoke, flashes and sparks fade where they meet walls and floors. Not drawn in Retro.");
        changed |= ImGui::Checkbox("Effect lights", &s.effect_lights);
        changed |= ImGui::SliderFloat("Light strength", &s.light_strength, 0.0f, 3.0f, "%.2f");
        ImGui::TextDisabled("Flashes, fire and lamps' glows light walls and characters near them (Vulkan).");
    }
    if (ImGui::CollapsingHeader("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Bounced light", &s.bounce_light);
        changed |= ImGui::SliderFloat("Bounce", &s.bounce_strength, 0.0f, 3.0f, "%.2f");
        changed |= ImGui::Checkbox("Fog (and light shafts)", &s.atmosphere);
        changed |= ImGui::SliderFloat("Shafts", &s.shaft_strength, 0.0f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Fog", &s.fog_strength, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("Rays from the sky and bright openings, distance fog in the sky's colour (Vulkan).");
    }
    if (ImGui::CollapsingHeader("Depth of field", ImGuiTreeNodeFlags_DefaultOpen)) {
        int d = s.dof;
        if (ImGui::Combo("Depth of field", &d, "Off\0Distance\0Focus\0")) s.dof = d, changed = true;
        changed |= ImGui::SliderFloat("Blur", &s.dof_strength, 0.0f, 2.0f, "%.2f");
        ImGui::TextDisabled("Distance: far scenery soft. Focus: what the crosshair is on sharp, nearer and farther\n"
                            "soft, more while aiming down the sights. The gun stays sharp (Vulkan; not in Retro).");
    }
    if (ImGui::CollapsingHeader("Motion blur", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Motion blur", &s.motion_blur);
        changed |= ImGui::SliderFloat("Amount##mb", &s.motion_blur_strength, 0.0f, 2.0f, "%.2f");
        ImGui::TextDisabled("A little blur as the camera turns and moves; the gun and HUD stay sharp (Vulkan; not in Retro).");
    }
    changed |= ImGui::Checkbox("Smooth turning (a frame in between, turned by the mouse; 100 Hz screens and up)",
                               &s.smooth_turn);
    changed |= ImGui::Checkbox("Contact shadows (short shadows under things, without ray traced lighting)",
                               &s.contact_shadows);
    {
        int f = s.floor_reflections;
        if (ImGui::Combo("Floor reflections", &f, "Off\0Ice\0Ice and polished floors\0")) s.floor_reflections = f, changed = true;
        changed |= ImGui::Checkbox("Water", &s.water);
    }
    if (ImGui::CollapsingHeader("HDR tone mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
        int h = s.hdr_tonemap;
        if (ImGui::Combo("Tone mapping (HDR)", &h, "Off\0AgX\0ACES\0")) s.hdr_tonemap = h, changed = true;
        changed |= ImGui::Checkbox("Auto exposure", &s.auto_exposure);
        ImGui::TextDisabled("The effects' light kept past white, then rolled off by AgX or ACES; auto exposure\n"
                            "adapts to the scene's brightness (Vulkan; not in Retro).");
    }
    changed |= ImGui::Checkbox("Blood where shots hit, and at the view's edges when you are hit", &s.blood);
    if (ImGui::CollapsingHeader("Ray tracing", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool rt = RayTracingSupported();
        if (!rt) ImGui::BeginDisabled();
        changed |= ImGui::Checkbox("Ray-traced ambient occlusion", &s.ray_tracing);
        changed |= ImGui::SliderFloat("Occlusion strength", &s.rt_ao, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Ray reach", &s.rt_radius, 0.25f, 6.0f, "%.2f m");
        changed |= ImGui::SliderInt("Rays per pixel", &s.rt_rays, 1, 16);
        changed |= ImGui::Checkbox("Ray-traced shadows from effect lights", &s.rt_shadows);
        changed |= ImGui::Checkbox("Ray-traced level lighting (the game's lights and a sun)", &s.rt_lighting);
        changed |= ImGui::SliderFloat("Sun strength", &s.rt_sun, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::Checkbox("Ray-traced global illumination (bounced light, with level lighting)", &s.rt_gi);
        changed |= ImGui::SliderFloat("Bounced light strength", &s.rt_gi_strength, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::Checkbox("Ray-traced volumetric light (light shafts, with level lighting)", &s.rt_volumetric);
        changed |= ImGui::SliderFloat("Volumetric strength", &s.rt_vol_strength, 0.0f, 3.0f, "%.2f");
        if (!rt) ImGui::EndDisabled();
        ImGui::TextDisabled(rt ? "Traced against the scene's own triangles, so corners, crevices and what stands behind\n"
                                 "or beside the view shade too. Replaces the screen-space AO. Not drawn in Retro."
                               : "Needs the Vulkan renderer on a GPU with ray queries (NVIDIA RTX, AMD RX 6000 or\n"
                                 "later, Intel Arc). The screen-space ambient occlusion stands in for it.");
    }
    if (ImGui::CollapsingHeader("Weather", ImGuiTreeNodeFlags_DefaultOpen)) {
        int w = s.weather;
        if (ImGui::Combo("Snow", &w, "Off\0Snowy levels\0Everywhere outdoors\0")) s.weather = w, changed = true;
        changed |= ImGui::SliderFloat("Snowfall", &s.snow_amount, 0.25f, 3.0f, "%.2f");
        changed |= ImGui::Checkbox("Footprints in the snow", &s.footprints);
        changed |= ImGui::Checkbox("Snow sparkle", &s.snow_sparkle);
        ImGui::TextDisabled("Falling snow and footprints (Vulkan; kept from under roofs with ray tracing).");
    }
    if (ImGui::CollapsingHeader("Movies", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Upscale movies", &s.movie_upscale);
        changed |= ImGui::Checkbox("Movies in 4:3 (black at the sides)", &s.movie_43);
        ImGui::TextDisabled("The 640x480 movies enlarged to the screen with edge-aware filtering and sharpening (Vulkan).");
    }
    if (ImGui::CollapsingHeader("Temporal", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("TAA (temporal anti-aliasing)", &s.taa);
        changed |= ImGui::Checkbox("Ray tracing denoise (built up over frames)", &s.rt_denoise);
        ImGui::TextDisabled("Vulkan. TAA smooths and steadies edges (SMAA is left out with it).");
    }
    if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("GPU particles (sparks, debris, smoke, explosions)", &s.particles);
        ImGui::TextDisabled("Vulkan. Simulated on the GPU in the world: sparks bounce, smoke rises and drifts.\n"
                            "Not drawn in split screen or Retro.");
    }
    if (ImGui::CollapsingHeader("Materials (PBR)", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Normal and roughness maps", &s.materials);
        changed |= ImGui::SliderFloat("Material strength", &s.material_strength, 0.0f, 1.5f, "%.2f");
        changed |= ImGui::SliderFloat("Relief depth (0 off)", &s.surface_depth, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::Checkbox("Relief High (more steps, self-shadowing)", &s.relief_high);
        changed |= ImGui::Checkbox("Ground geometry (the ground cut finer, lumps and hollows)", &s.displacement);
        changed |= ImGui::Checkbox("Wall geometry (rock and concrete walls made uneven)", &s.wall_displacement);
        changed |= ImGui::SliderFloat("Geometry depth", &s.displace_depth, 0.0f, 3.0f, "%.2f");
        ImGui::TextDisabled("A texture pack's maps (tools\\hd_pack.exe --pbr) over the game's lighting: bumps and a\n"
                            "specular sheen. Turning them on takes effect at the next start. Not drawn in Retro.");
    }
    if (ImGui::CollapsingHeader("Textures")) {
        changed |= ImGui::Checkbox("Texture packs (mods/textures)", &s.texture_pack);
        changed |= ImGui::Checkbox("Dump the game's textures (mods/textures/dump)", &s.dump_textures);
        ImGui::TextDisabled("A pack is <hash>.png files named as the dump names them, any size (2x, 4x, 8x).");
    }
    if (ImGui::CollapsingHeader("Not available yet")) {
        ImGui::BeginDisabled();
        bool off = false;
        int zero = 0;
        ImGui::Combo("Lighting", &zero, "Original\0");
        ImGui::Combo("Shadows", &zero, "Original\0");
        ImGui::Checkbox("Normal mapping / PBR materials", &off);
        ImGui::Checkbox("Height maps / parallax", &off);
        ImGui::Checkbox("Tessellation", &off);
        ImGui::Checkbox("Temporal anti-aliasing", &off);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Need the scene's normals, lights and positions, which the console's vertex programs do\n"
                            "not hand the renderer yet (docs/graphics_styles.md).");
    }
    if (picked) {
        ApplyPreset(*picked);
    } else if (changed || reset) {
        if (changed) s.preset.clear();
        std::lock_guard lock(g_handoff);
        g_style = s;
        StyleChanged();
    }
}

}  // namespace gpu
