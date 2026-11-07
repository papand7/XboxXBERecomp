// Ground detail: which of the scenery's textures get it, and the detail pictures themselves.
//
// A texture is tagged once, when it is first uploaded, from its own level 0: a solid (no see-through texels),
// power-of-two picture from 64 to 1024 texels a side whose opposite edges meet without a seam (it tiles: the
// scenery's ground and walls, not a character's or a sign's picture) is grass when green, snow when bright and pale,
// dirt when brown, rock when grey and uneven, concrete when grey and even. mods/details.toml's [textures] table
// (16 hex digits = "grass", "snow", "rock", "dirt", "concrete" or "none") overrides it.
//
// The detail pictures are made here, tileable, from noise: grass blades, snow's soft drifts and glints, rock's cracks,
// dirt's pebbles, concrete's grain and pits; each a height (its normal) and a brightness.
#include "gpu/detail.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "core/log.h"

namespace gpu {
namespace {

std::mutex g_lock;
std::unordered_map<uint64_t, uint8_t> g_kinds;   // found
std::unordered_map<uint64_t, uint8_t> g_listed;  // mods/details.toml
bool g_listed_read = false;
const char* const kNames[kDetailKinds] = {"none", "grass", "snow", "rock", "dirt", "concrete"};

void ReadList() {
    if (g_listed_read) return;
    g_listed_read = true;
    std::filesystem::path f = app::ExeDir() / "mods" / "details.toml";
    std::error_code ec;
    if (!std::filesystem::exists(f, ec)) return;
    try {
        toml::table tb = toml::parse_file(f.string());
        if (const toml::table* list = tb["textures"].as_table())
            for (auto&& [k, v] : *list) {
                std::string name(k.str());
                char* end = nullptr;
                uint64_t key = std::strtoull(name.c_str(), &end, 16);
                std::optional<std::string> kind = v.value<std::string>();
                int found = -1;
                for (int i = 0; kind && i < kDetailKinds; i++)
                    if (*kind == kNames[i]) found = i;
                if (name.size() != 16 || !end || *end || found < 0) {
                    LOGR("graphics: details.toml: '%s' is not a texture's 16 hex digits = a kind, ignored", name.c_str());
                    continue;
                }
                g_listed[key] = uint8_t(found);
            }
        LOGR("graphics: details.toml lists %zu textures", g_listed.size());
    } catch (const toml::parse_error& e) {
        LOGR("graphics: details.toml is not valid TOML (%s), not used", std::string(e.description()).c_str());
    }
}

bool Pow2(uint32_t v) { return v && !(v & (v - 1)); }

// the kind from B8G8R8A8 texels
uint8_t Classify(const std::vector<uint32_t>& px, uint32_t w, uint32_t h) {
    auto ch = [&](uint32_t x, uint32_t y, int c) { return float(px[size_t(y) * w + x] >> (16 - c * 8) & 0xFF) / 255.0f; };
    auto diff = [&](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
        float s = 0;
        for (int c = 0; c < 3; c++) s += std::fabs(ch(x0, y0, c) - ch(x1, y1, c));
        return s;
    };
    // solid: hardly a texel see-through
    size_t clear = 0;
    double sum[3] = {}, lum2 = 0, lum1 = 0;
    for (size_t i = 0; i < size_t(w) * h; i++) {
        uint32_t p = px[i];
        if ((p >> 24) < 250) clear++;
        float r = float(p >> 16 & 0xFF) / 255.0f, g = float(p >> 8 & 0xFF) / 255.0f, b = float(p & 0xFF) / 255.0f;
        sum[0] += r, sum[1] += g, sum[2] += b;
        float l = 0.299f * r + 0.587f * g + 0.114f * b;
        lum1 += l, lum2 += l * l;
    }
    size_t n = size_t(w) * h;
    if (clear * 50 > n) return kDetailNone;
    // tiling: its edges against each other no rougher than its texels against their neighbours
    double seam = 0, inner = 0;
    for (uint32_t y = 0; y < h; y++) seam += diff(0, y, w - 1, y);
    for (uint32_t x = 0; x < w; x++) seam += diff(x, 0, x, h - 1);
    seam /= double(w + h);
    uint32_t m = 0;
    for (uint32_t y = 1; y < h - 1; y += 3)
        for (uint32_t x = 1; x < w - 1; x += 3) inner += diff(x, y, x + 1, y) + diff(x, y, x, y + 1), m += 2;
    inner /= std::max(1u, m);
    if (seam > inner * 1.6 + 0.03) return kDetailNone;
    float r = float(sum[0] / n), g = float(sum[1] / n), b = float(sum[2] / n);
    float lum = float(lum1 / n), dev = float(std::sqrt(std::max(0.0, lum2 / n - (lum1 / n) * (lum1 / n))));
    float hi = std::max({r, g, b}), lo = std::min({r, g, b}), sat = hi > 0 ? (hi - lo) / hi : 0;
    float hue = 0;
    if (hi > lo) {
        float d = hi - lo;
        hue = hi == r ? std::fmod((g - b) / d + 6, 6.0f) : hi == g ? (b - r) / d + 2 : (r - g) / d + 4;
        hue /= 6;
    }
    if (lum > 0.58f && sat < 0.2f) return kDetailSnow;
    if (sat > 0.15f && hue > 0.16f && hue < 0.45f && g >= r && g >= b) return kDetailGrass;
    if (sat > 0.15f && sat < 0.75f && hue > 0.02f && hue < 0.15f && lum < 0.6f) return kDetailDirt;
    if (sat < 0.2f && lum > 0.12f) return dev > 0.08f ? kDetailRock : kDetailConcrete;
    return kDetailNone;
}

// ---- the pictures

uint32_t Hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float Rand(uint32_t x, uint32_t y, uint32_t seed) { return float(Hash(x, y, seed) & 0xFFFFFF) / float(0xFFFFFF); }

// value noise repeating every `period` cells over the picture (kDetailSize)
float Noise(float x, float y, uint32_t period, uint32_t seed) {
    float cell = float(kDetailSize) / float(period);
    float fx = x / cell, fy = y / cell;
    int ix = int(std::floor(fx)), iy = int(std::floor(fy));
    float tx = fx - float(ix), ty = fy - float(iy);
    tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
    auto at = [&](int cx, int cy) {
        return Rand(uint32_t((cx % int(period) + int(period)) % int(period)), uint32_t((cy % int(period) + int(period)) % int(period)), seed);
    };
    float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}
float Fbm(float x, float y, uint32_t period, int octaves, uint32_t seed) {
    float s = 0, amp = 0.5f, total = 0;
    for (int o = 0; o < octaves; o++, period *= 2, amp *= 0.5f) s += Noise(x, y, period, seed + o) * amp, total += amp;
    return s / total;
}

struct Field {
    std::vector<float> h, a;  // height, brightness (0.5 as it is)
    Field() : h(kDetailSize * kDetailSize, 0), a(kDetailSize * kDetailSize, 0.5f) {}
    size_t At(int x, int y) const {
        int n = int(kDetailSize);
        return size_t((y % n + n) % n) * kDetailSize + size_t((x % n + n) % n);
    }
};

// a soft dot (wrapping): height and brightness added by its falloff
void Dot(Field& f, float cx, float cy, float radius, float height, float bright) {
    int r = int(std::ceil(radius)) + 1;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            float dx = float(x) + std::floor(cx) - cx, dy = float(y) + std::floor(cy) - cy;
            float d = std::sqrt(dx * dx + dy * dy) / radius;
            if (d >= 1) continue;
            float k = std::sqrt(1 - d * d);
            size_t i = f.At(int(std::floor(cx)) + x, int(std::floor(cy)) + y);
            f.h[i] = std::max(f.h[i], f.h[i] * (1 - k) + height * k);
            f.a[i] += bright * k;
        }
}

Field Make(uint8_t kind) {
    Field f;
    const int n = int(kDetailSize);
    switch (kind) {
    case kDetailGrass: {
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) f.h[f.At(x, y)] = 0.25f * Fbm(float(x), float(y), 8, 4, 11);
        for (uint32_t i = 0; i < 2600; i++) {  // blades: thin strokes, darker at the root, lighter at the tip
            float x = Rand(i, 1, 21) * n, y = Rand(i, 2, 21) * n, ang = Rand(i, 3, 21) * 6.2831853f;
            float len = 5 + Rand(i, 4, 21) * 9, tone = (Rand(i, 5, 21) - 0.5f) * 0.12f;
            for (float t = 0; t <= 1; t += 0.5f / len)
                Dot(f, x + std::cos(ang) * len * t, y + std::sin(ang) * len * t, 0.9f, 0.35f + 0.6f * t,
                    (-0.05f + 0.1f * t + tone) * 0.35f);
        }
        break;
    }
    case kDetailSnow:
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                size_t i = f.At(x, y);
                f.h[i] = 0.75f * Fbm(float(x), float(y), 4, 5, 31);
                f.a[i] += (Fbm(float(x), float(y), 16, 3, 37) - 0.5f) * 0.08f;
            }
        for (uint32_t i = 0; i < 700; i++)  // glints: crystals catching the light
            f.a[f.At(int(Rand(i, 1, 41) * n), int(Rand(i, 2, 41) * n))] += 0.25f + Rand(i, 3, 41) * 0.25f;
        break;
    case kDetailRock: {
        const uint32_t cells = 8;
        float cell = float(n) / float(cells);
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                float best = 1e9f, second = 1e9f;
                int cx = int(float(x) / cell), cy = int(float(y) / cell);
                for (int j = -1; j <= 1; j++)
                    for (int k = -1; k <= 1; k++) {
                        int gx = cx + k, gy = cy + j;
                        uint32_t wx = uint32_t((gx % int(cells) + int(cells)) % int(cells));
                        uint32_t wy = uint32_t((gy % int(cells) + int(cells)) % int(cells));
                        float px = (float(gx) + Rand(wx, wy, 51)) * cell, py = (float(gy) + Rand(wx, wy, 52)) * cell;
                        float d = std::hypot(px - float(x), py - float(y));
                        if (d < best) second = best, best = d;
                        else if (d < second) second = d;
                    }
                float crack = std::clamp((second - best) / (cell * 0.12f), 0.0f, 1.0f);
                float rough = Fbm(float(x), float(y), 16, 4, 53);
                size_t i = f.At(x, y);
                f.h[i] = 0.55f * crack + 0.45f * rough;
                f.a[i] += (rough - 0.5f) * 0.25f - (1 - crack) * 0.18f;
            }
        break;
    }
    case kDetailDirt:
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                size_t i = f.At(x, y);
                float v = Fbm(float(x), float(y), 16, 4, 61);
                f.h[i] = 0.35f * v;
                f.a[i] += (v - 0.5f) * 0.2f;
            }
        for (uint32_t i = 0; i < 420; i++)  // pebbles
            Dot(f, Rand(i, 1, 71) * n, Rand(i, 2, 71) * n, 1.5f + Rand(i, 3, 71) * 3.5f, 0.6f + Rand(i, 4, 71) * 0.4f,
                (Rand(i, 5, 71) - 0.4f) * 0.25f);
        break;
    case kDetailConcrete:
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                size_t i = f.At(x, y);
                float grain = Rand(uint32_t(x), uint32_t(y), 81), soft = Fbm(float(x), float(y), 8, 3, 83);
                f.h[i] = 0.5f + 0.18f * grain + 0.25f * soft;
                f.a[i] += (grain - 0.5f) * 0.06f + (soft - 0.5f) * 0.12f;
            }
        for (uint32_t i = 0; i < 260; i++) {  // pits
            float x = Rand(i, 1, 91) * n, y = Rand(i, 2, 91) * n, r = 1 + Rand(i, 3, 91) * 1.8f;
            for (int j = -3; j <= 3; j++)
                for (int k = -3; k <= 3; k++) {
                    float d = std::hypot(float(k), float(j)) / r;
                    if (d >= 1) continue;
                    size_t at = f.At(int(x) + k, int(y) + j);
                    f.h[at] -= 0.35f * (1 - d), f.a[at] -= 0.12f * (1 - d);
                }
        }
        break;
    default:
        break;
    }
    return f;
}

}  // namespace

const char* DetailName(uint8_t kind) { return kind < kDetailKinds ? kNames[kind] : "?"; }

uint8_t TextureDetail(uint64_t key, const TextureDesc& d, const uint8_t* guest, bool paletted) {
    std::lock_guard lock(g_lock);
    ReadList();
    if (auto it = g_listed.find(key); it != g_listed.end()) return it->second;
    if (auto it = g_kinds.find(key); it != g_kinds.end()) return it->second;
    uint8_t kind = kDetailNone;
    std::vector<uint32_t> px;
    if (!paletted && !d.cube && d.depth == 1 && !Format(d.format).depth && Pow2(d.width) && Pow2(d.height) &&
        d.width >= 64 && d.height >= 64 && d.width <= 1024 && d.height <= 1024 && DecodeBgra(d, 0, guest, px) &&
        px.size() >= size_t(d.width) * d.height)
        kind = Classify(px, d.width, d.height);
    g_kinds[key] = kind;
    if (kind) {
        static int said = 0;
        if (said++ < 60) LOGR("graphics: ground detail: texture %016llx (%ux%u) is %s", (unsigned long long)key, d.width,
                              d.height, kNames[kind]);
    }
    return kind;
}

bool TextureIce(uint64_t key, const TextureDesc& d, const uint8_t* guest, bool paletted) {
    static std::unordered_map<uint64_t, bool> seen;
    std::lock_guard lock(g_lock);
    if (auto it = seen.find(key); it != seen.end()) return it->second;
    bool ice = false;
    std::vector<uint32_t> px;
    if (!paletted && !d.cube && d.depth == 1 && !Format(d.format).depth && d.width >= 32 && d.height >= 32 &&
        d.width <= 1024 && d.height <= 1024 && DecodeBgra(d, 0, guest, px) && px.size() >= size_t(d.width) * d.height) {
        size_t n = size_t(d.width) * d.height, clear = 0;
        double r = 0, g = 0, b = 0, l1 = 0, l2 = 0;
        for (size_t i = 0; i < n; i++) {
            uint32_t p = px[i];
            if ((p >> 24) < 250) clear++;
            float pr = float(p >> 16 & 0xFF) / 255.0f, pg = float(p >> 8 & 0xFF) / 255.0f, pb = float(p & 0xFF) / 255.0f;
            r += pr, g += pg, b += pb;
            float l = 0.299f * pr + 0.587f * pg + 0.114f * pb;
            l1 += l, l2 += l * l;
        }
        r /= double(n), g /= double(n), b /= double(n), l1 /= double(n);
        double dev = std::sqrt(std::max(0.0, l2 / double(n) - l1 * l1));
        double hi = std::max({r, g, b}), lo = std::min({r, g, b}), sat = hi > 0 ? (hi - lo) / hi : 0;
        ice = clear * 50 <= n && l1 > 0.35 && b >= g && b > r + 0.04 && sat > 0.06 && sat < 0.55 && dev < 0.085;
    }
    seen[key] = ice;
    if (ice) {
        static int said = 0;
        if (said++ < 20) LOGR("graphics: floor reflections: texture %016llx (%ux%u) is ice", (unsigned long long)key, d.width, d.height);
    }
    return ice;
}

bool TextureWater(uint64_t key, const TextureDesc& d, const uint8_t* guest, bool paletted) {
    static std::unordered_map<uint64_t, bool> seen;
    std::lock_guard lock(g_lock);
    if (auto it = seen.find(key); it != seen.end()) return it->second;
    bool water = false;
    bool candidate = false;
    std::vector<uint32_t> px;
    double r = 0, g = 0, b = 0, l1 = 0, dev = 0, clear = 0;
    if (!paletted && !d.cube && d.depth == 1 && !Format(d.format).depth && d.width >= 64 && d.height >= 64 &&
        d.width <= 1024 && d.height <= 1024 && DecodeBgra(d, 0, guest, px) && px.size() >= size_t(d.width) * d.height) {
        size_t n = size_t(d.width) * d.height;
        double l2 = 0;
        for (size_t i = 0; i < n; i++) {
            uint32_t p = px[i];
            if ((p >> 24) < 250) clear++;
            float pr = float(p >> 16 & 0xFF) / 255.0f, pg = float(p >> 8 & 0xFF) / 255.0f, pb = float(p & 0xFF) / 255.0f;
            r += pr, g += pg, b += pb;
            float l = 0.299f * pr + 0.587f * pg + 0.114f * pb;
            l1 += l, l2 += l * l;
        }
        r /= double(n), g /= double(n), b /= double(n), l1 /= double(n), clear /= double(n);
        dev = std::sqrt(std::max(0.0, l2 / double(n) - l1 * l1));
        double hi = std::max({r, g, b}), lo = std::min({r, g, b}), sat = hi > 0 ? (hi - lo) / hi : 0;
        // blue or green over red, not grey, not bright (ice and snow), even (not a patterned wall)
        candidate = std::max(b, g) > r + 0.05 && sat > 0.15 && l1 > 0.05 && l1 < 0.55 && dev < 0.12;
        // Conservative auto-detection: the survey also finds suits, carpet, sky and splash sprites. Keep only
        // small square, opaque, smoothly varying patterns; the renderer additionally requires an upward-facing
        // surface and excludes the sky. Fine checker patterns (carpet) have a large adjacent-pixel difference.
        if (candidate && d.width == d.height && d.width <= 128 && clear < 0.05 && dev > 0.005) {
            auto lum = [&](uint32_t x, uint32_t y) {
                const uint32_t p = px[size_t(y) * d.width + x];
                return (0.299 * double(p >> 16 & 255) + 0.587 * double(p >> 8 & 255) +
                        0.114 * double(p & 255)) / 255.0;
            };
            double rough = 0;
            for (uint32_t y = 1; y < d.height; y++)
                for (uint32_t x = 1; x < d.width; x++)
                    rough += std::abs(lum(x, y) - lum(x - 1, y)) + std::abs(lum(x, y) - lum(x, y - 1));
            rough /= double(2 * (d.width - 1) * (d.height - 1));
            water = rough / dev < 0.32;
        }
    }
    seen[key] = water;
    if (candidate) {
        static int said = 0;
        if (said++ < 60)
            LOGR("graphics: water? texture %016llx (%ux%u fmt %02X): mean %.2f %.2f %.2f, brightness %.2f, spread %.3f, %.0f%% see-through",
                 (unsigned long long)key, d.width, d.height, d.format, r, g, b, l1, dev, clear * 100);
        if (const char* dir = std::getenv("XBE_WATER_DUMP")) {
            char name[64];
            std::snprintf(name, sizeof(name), "/water_%016llx.tga", (unsigned long long)key);
            if (FILE* f = std::fopen((std::string(dir) + name).c_str(), "wb")) {
                uint8_t head[18] = {0, 0, 2};
                head[12] = uint8_t(d.width), head[13] = uint8_t(d.width >> 8);
                head[14] = uint8_t(d.height), head[15] = uint8_t(d.height >> 8);
                head[16] = 32, head[17] = 0x28;
                std::fwrite(head, 1, sizeof head, f);
                std::fwrite(px.data(), 4, size_t(d.width) * d.height, f);
                std::fclose(f);
            }
        }
    }
    return water;
}

void DetailPicture(uint8_t kind, std::vector<std::vector<uint32_t>>& levels) {
    Field f = Make(kind);
    const float strength[kDetailKinds] = {0, 5.0f, 3.0f, 6.0f, 5.0f, 4.0f};  // how steep the bumps are
    const int n = int(kDetailSize);
    std::vector<float> nx(size_t(n) * n), ny(size_t(n) * n), hh(f.h), aa(f.a);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float dx = (f.h[f.At(x + 1, y)] - f.h[f.At(x - 1, y)]) * strength[kind];
            float dy = (f.h[f.At(x, y + 1)] - f.h[f.At(x, y - 1)]) * strength[kind];
            float inv = 1.0f / std::sqrt(dx * dx + dy * dy + 1);
            nx[f.At(x, y)] = -dx * inv, ny[f.At(x, y)] = -dy * inv;
        }
    levels.clear();
    for (uint32_t size = kDetailSize; size >= 1; size /= 2) {
        std::vector<uint32_t> lv(size_t(size) * size);
        for (size_t i = 0; i < lv.size(); i++) {
            auto byte = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            lv[i] = byte(hh[i]) << 24 | byte(nx[i] * 0.5f + 0.5f) << 16 | byte(ny[i] * 0.5f + 0.5f) << 8 | byte(aa[i]);
        }
        levels.push_back(std::move(lv));
        if (size == 1) break;
        // the next level: each 2x2 averaged
        uint32_t half = size / 2;
        auto down = [&](std::vector<float>& v) {
            std::vector<float> o(size_t(half) * half);
            for (uint32_t y = 0; y < half; y++)
                for (uint32_t x = 0; x < half; x++)
                    o[size_t(y) * half + x] = (v[size_t(2 * y) * size + 2 * x] + v[size_t(2 * y) * size + 2 * x + 1] +
                                               v[size_t(2 * y + 1) * size + 2 * x] + v[size_t(2 * y + 1) * size + 2 * x + 1]) *
                                              0.25f;
            v.swap(o);
        };
        down(nx), down(ny), down(hh), down(aa);
    }
}

}  // namespace gpu
