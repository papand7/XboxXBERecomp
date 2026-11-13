// The picture's look kept as settings of their own (app::Settings::fx, color_style, bloom ...: Agent Under Fire's
// menus set them) handed to the Vulkan renderer's gpu::GraphicsStyle, for a title with GameInfo::style_from_settings.
// The render thread calls StyleFromSettings once a frame (SaveStyleIfDue): a changed setting, or a graphics.toml read
// again, puts the settings' values back over the style. A title without it keeps graphics.toml and the Graphics Style
// tab (NightFire). Also the level's sun a title's hooks report (gpu/rt.h) as the ray traced lighting's sun.
#include <algorithm>
#include <cstring>
#include <mutex>

#include "app/app.h"
#include "gpu/rt.h"
#include "gpu/style.h"

namespace gpu {

namespace {

// the settings' look fields hashed (FNV-1a): applied again only when one changed
uint64_t SettingsSignature(const app::Settings& s) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    };
    mix(s.fx, sizeof(int32_t) * app::kFxCount);
    const uint32_t v[] = {s.color_style, s.bloom, s.light_shafts, s.atmospheric_fog, s.ao_radius,
                          s.ambient_occlusion, s.depth_of_field, s.focus_distance, s.dof_blur, s.motion_blur,
                          s.motion_blur_strength, s.vignette, s.film_grain, s.movie_43, s.movie_upscale, s.exposure,
                          s.contrast, s.saturation, uint32_t(s.temperature), s.temporal_aa, s.upscaler,
                          s.upscale_quality, s.upscale_sharpness, s.auto_exposure, s.texture_pack, s.dump_textures,
                          s.sharpen};
    mix(v, sizeof(v));
    return h;
}

float Pct(int32_t v) { return float(v) / 100.0f; }

}  // namespace

void StyleFromSettings() {
    if (!app::kGame.style_from_settings) return;
    static uint64_t applied_sig = 0, applied_version = ~0ull;
    const app::Settings& s = app::Get();
    const uint64_t sig = SettingsSignature(s);
    if (sig == applied_sig && StyleVersion() == applied_version) return;
    GraphicsStyle& g = Style();
    auto fx = [&](int id) { return s.fx[id]; };
    g.preset.clear();
    // the shader style (fx) first, else the colour style; both Enhanced styles are the renderer's Enhanced
    const int shader = fx(app::kFxStyle);
    g.mode = shader ? shader : s.color_style == 4 ? kStyleNoir : int(std::min<uint32_t>(s.color_style, 3));
    g.tonemap = fx(app::kFxTonemap);
    g.sharpen = std::max(Pct(int32_t(s.sharpen)), g.mode == kStyleEnhanced ? Pct(fx(app::kFxClarity)) : 0.0f);
    g.bands = fx(app::kFxToonBands);
    g.outline = float(fx(app::kFxToonOutline)) / 10.0f;
    g.internal_edges = fx(app::kFxToonInner) != 0;
    g.edge_threshold = Pct(fx(app::kFxToonThreshold));
    g.halftone = Pct(fx(app::kFxToonHalftone));
    g.pixel = fx(app::kFxRetroPixel);
    g.color_bits = fx(app::kFxRetroBits);
    g.dither = fx(app::kFxRetroDither) != 0;
    g.retro_tint = Pct(fx(app::kFxRetroTint));
    // grading
    g.exposure = Pct(int32_t(s.exposure));
    g.contrast = Pct(int32_t(s.contrast));
    g.saturation = Pct(int32_t(s.saturation));
    g.temperature = Pct(s.temperature);
    g.vignette = Pct(int32_t(s.vignette));
    g.grain = Pct(int32_t(s.film_grain));
    g.bloom = Pct(int32_t(s.bloom)) * 2.0f;
    g.quality_bloom = s.bloom > 0;
    // ambient occlusion: the screen-space one at high, its reach; traced where ray tracing is on
    g.quality_ao = s.ambient_occlusion ? 2 : 0;
    g.ao = s.ambient_occlusion ? 1.0f : 0.0f;
    g.ao_radius = Pct(int32_t(s.ao_radius));
    // light shafts and fog
    g.shaft_strength = Pct(int32_t(s.light_shafts)) * 3.0f;
    g.fog_strength = Pct(int32_t(s.atmospheric_fog));
    g.atmosphere = s.light_shafts > 0 || s.atmospheric_fog > 0;
    // depth of field: the focus distance blurred about (the renderer's Focus mode), its strength from the blur's size
    g.dof = s.depth_of_field ? kDofFocus : kDofOff;
    g.dof_strength = std::clamp(float(s.dof_blur) / 6.0f, 0.0f, 2.0f);
    g.motion_blur = s.motion_blur;
    g.motion_blur_strength = float(s.motion_blur_strength) / 60.0f;
    g.movie_43 = s.movie_43;
    g.movie_upscale = s.movie_upscale;
    g.taa = s.temporal_aa;
    g.upscaler = s.upscaler ? kUpscalerFsr : kUpscalerOff;
    g.upscale_quality = int(s.upscale_quality) + kUpscaleQuality;  // the settings count from Quality, not Native
    g.upscale_sharpness = Pct(int32_t(s.upscale_sharpness));
    g.auto_exposure = s.auto_exposure;
    g.texture_pack = s.texture_pack;
    g.dump_textures = s.dump_textures;
    // ray tracing; the renderer's traced sun, bounced and volumetric light come with its traced level lighting
    g.ray_tracing = fx(app::kFxRayTracing) != 0;
    g.rt_ao = Pct(fx(app::kFxRtAo));
    g.rt_radius = Pct(fx(app::kFxRtRadius));
    g.rt_rays = fx(app::kFxRtRays);
    g.reflections = fx(app::kFxReflections) ? kReflectHigh : kReflectOff;
    g.floor_reflections = fx(app::kFxReflections) >= 2 ? 2 : 0;
    g.reflection_strength = Pct(fx(app::kFxReflectionStrength));
    g.rt_shadows = fx(app::kFxRtShadows) != 0;
    g.rt_gi = fx(app::kFxRtGi) != 0;
    g.rt_gi_strength = Pct(fx(app::kFxRtGiStrength));
    g.rt_volumetric = fx(app::kFxRtVolumetric) != 0;
    g.rt_vol_strength = Pct(fx(app::kFxRtVolStrength));
    // the traced level lighting (its sun, rt_sun) under bounced or volumetric light, which need it; the dynamic lights
    // row is the renderer's effect lights, traced shadows with rt_shadows
    g.rt_lighting = g.rt_gi || g.rt_volumetric;
    g.effect_lights = fx(app::kFxRtLighting) != 0;
    g.rt_sun = Pct(fx(app::kFxRtSun));
    g.rt_denoise = fx(app::kFxRtDenoise) != 0;
    // what the settings have no row for and would change the title's look: held off
    g.bounce_light = false;
    g.ground_detail = false;
    StyleChanged();
    applied_sig = sig;
    applied_version = StyleVersion();
}

// ---- the level's sun (gpu/rt.h)

namespace {
std::mutex g_sun_lock;
RtSun g_rt_sun{};
bool g_rt_sun_set = false;
}  // namespace

void SetRtSun(const RtSun* sun) {
    {
        std::lock_guard lock(g_sun_lock);
        g_rt_sun_set = sun != nullptr;
        if (sun) g_rt_sun = *sun;
    }
    if (sun) SetSunDirection(sun->dir);
}

bool GetRtSun(RtSun& out) {
    std::lock_guard lock(g_sun_lock);
    if (g_rt_sun_set) out = g_rt_sun;
    return g_rt_sun_set;
}

}  // namespace gpu
