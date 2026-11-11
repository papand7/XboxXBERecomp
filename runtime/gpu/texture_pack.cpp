// Content-keyed PNG replacements and texture dumps from the Nightfire SDK.
#include "gpu/texture_pack.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>

#include "app/app.h"
#include "app/image.h"
#include "core/log.h"
#include "gpu/shaders.h"

namespace gpu {
#define PACK_LOG(n, ...) do { static int count = 0; if (count++ < (n)) LOGR(__VA_ARGS__); } while (0)
namespace {
std::filesystem::path PackDir() { return app::ExeDir() / "mods" / "textures"; }
std::mutex g_lock;
bool g_scanned = false;
std::unordered_set<uint64_t> g_files, g_dumped;
struct Seen { uint64_t key; std::chrono::steady_clock::time_point changed; };
std::unordered_map<uint32_t, Seen> g_seen_at;

void Scan() {
    if (g_scanned) return;
    g_scanned = true;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(PackDir(), ec)) {
        if (entry.path().extension() != ".png") continue;
        const std::string stem = entry.path().stem().string();
        if (stem.size() != 16) continue;
        char* end = nullptr;
        const uint64_t key = std::strtoull(stem.c_str(), &end, 16);
        if (end && !*end) g_files.insert(key);
    }
    if (!g_files.empty()) LOGR("texture pack: %zu replacement pictures found", g_files.size());
}

bool ReadPng(const std::filesystem::path& path, std::vector<uint32_t>& px, uint32_t& w, uint32_t& h) {
    uint32_t error = 0;
    if (app::DecodeImage(path, px, w, h, false, 8192, &error)) return true;
    PACK_LOG(16, "texture pack: %s could not be read (%08X)", path.string().c_str(), unsigned(error));
    return false;
}
}

uint64_t TextureKey(const TextureDesc& d, const uint8_t* guest, uint64_t palette_hash) {
    const uint64_t seed = uint64_t(d.format) << 48 ^ uint64_t(d.width) << 24 ^ d.height ^ palette_hash;
    return Hash(guest, LevelSize(d, 0), seed);
}

void TexturePackSeen(const TextureDesc& d, const uint8_t* guest, const uint32_t* palette, uint64_t palette_hash) {
    const app::Settings& settings = app::Get();
    if ((!settings.texture_pack && !settings.dump_textures) || !guest || d.cube || d.depth > 1 ||
        Format(d.format).depth || palette) return;
    std::lock_guard lock(g_lock);
    Scan();
    if (!settings.dump_textures && g_files.empty()) return;
    const uint64_t key = TextureKey(d, guest, palette_hash);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.png", (unsigned long long)key);
    bool moving = false;
    if (settings.dump_textures) {
        const auto now = std::chrono::steady_clock::now();
        auto [it, fresh] = g_seen_at.try_emplace(d.data, Seen{key, now - std::chrono::seconds(10)});
        if (!fresh && it->second.key != key) {
            moving = now - it->second.changed < std::chrono::seconds(1);
            it->second = {key, now};
        }
        if (!moving && g_dumped.insert(key).second) {
            std::error_code ec;
            const auto dir = PackDir() / "dump";
            std::filesystem::create_directories(dir, ec);
            std::vector<uint32_t> px;
            if (!ec && !std::filesystem::exists(dir / name, ec) && DecodeBgra(d, 0, guest, px) &&
                px.size() >= size_t(d.width) * d.height) {
                const auto tmp = dir / (std::string(name) + ".tmp");
                bool ok = app::EncodePng(tmp, px, d.width, d.height);
                if (ok) std::filesystem::rename(tmp, dir / name, ec);
                if (!ok || ec) std::filesystem::remove(tmp, ec);
                else PACK_LOG(4, "texture pack: dumped original textures to mods/textures/dump");
            }
        }
    }
    if (settings.texture_pack && !moving && g_files.count(key) && !FindReplacement(d.data)) {
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        if (ReadPng(PackDir() / name, px, w, h)) {
            SetReplacement(d.data, LevelSize(d, 0), w, h, std::move(px));
            PACK_LOG(32, "texture pack: replaced texture %016llx (%ux%u -> %ux%u)",
                 (unsigned long long)key, d.width, d.height, w, h);
        }
    }
}
}  // namespace gpu