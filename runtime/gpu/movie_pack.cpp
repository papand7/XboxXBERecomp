// Movie packs: see movie_pack.h. Collecting hands each new frame to a writer thread (the title's frame is not held
// up: a movie player keeps time and would drop frames). Playing keeps up to kKept pictures read ahead of the frame
// shown, in the order its movie plays them.
#include "gpu/movie_pack.h"

#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app/app.h"
#include "app/image.h"
#include "core/log.h"
#include "gpu/style.h"
#include "gpu/texture.h"
#include "gpu/texture_pack.h"
#include "kernel/kernel.h"

namespace gpu {

namespace {
namespace fs = std::filesystem;

fs::path PackDir() { return app::ExeDir() / "mods" / "movies"; }

std::string Name(uint64_t key, const char* ext) {
    char b[40];
    std::snprintf(b, sizeof b, "%016llx%s", (unsigned long long)key, ext);
    return b;
}

// ---- collecting
struct Dump {
    uint64_t key;
    uint32_t w, h;
    std::vector<uint32_t> px;
};
std::mutex g_dump_lock;
std::condition_variable g_dump_cv;
std::deque<Dump> g_dumps;
std::unordered_set<uint64_t> g_dumped;
std::set<std::string> g_listed;  // movies whose order file this run started afresh
uint64_t g_last_key = 0;

void Writer() {
    for (;;) {
        Dump d;
        {
            std::unique_lock lock(g_dump_lock);
            g_dump_cv.wait(lock, [] { return !g_dumps.empty(); });
            d = std::move(g_dumps.front());
            g_dumps.pop_front();
        }
        const fs::path dir = PackDir() / "dump", name = dir / Name(d.key, ".png");
        std::error_code ec;
        if (fs::exists(name, ec)) continue;
        fs::path tmp = name;
        tmp += ".tmp";
        if (app::EncodePng(tmp, d.px, d.w, d.h)) fs::rename(tmp, name, ec);
        else fs::remove(tmp, ec);
    }
}

// ---- playing
constexpr size_t kAhead = 12, kKept = 40;
struct Picture {
    uint32_t w = 0, h = 0;
    std::vector<uint32_t> px;
};
struct Want {
    uint64_t key;
    uint32_t mode;
    void (*recolor)(uint32_t*, uint32_t, uint32_t, uint32_t);
};
std::mutex g_lock;
std::condition_variable g_cv;
bool g_scanned = false;
std::unordered_set<uint64_t> g_have;                         // the pack's pictures
std::vector<std::vector<uint64_t>> g_orders;                 // each movie's frames in order
std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> g_at;  // a frame: its movie and place there (the first)
std::map<std::pair<uint64_t, uint32_t>, std::shared_ptr<const Picture>> g_ready;  // by key and mode
std::deque<std::pair<uint64_t, uint32_t>> g_ready_order;     // oldest first, for dropping
std::set<std::pair<uint64_t, uint32_t>> g_busy;              // asked for or being read
std::deque<Want> g_wants;
uint64_t g_shown = 0;

void Reader() {
    for (;;) {
        Want w;
        {
            std::unique_lock lock(g_lock);
            g_cv.wait(lock, [] { return !g_wants.empty(); });
            w = g_wants.front();
            g_wants.pop_front();
        }
        auto p = std::make_shared<Picture>();
        if (app::DecodeImage(PackDir() / Name(w.key, ".jpg"), p->px, p->w, p->h, false, 8192)) {
            if (w.mode && w.recolor) w.recolor(p->px.data(), p->w, p->h, w.mode);
        } else {
            p->w = p->h = 0;  // unreadable: asked again no more (an empty picture is never shown)
        }
        std::lock_guard lock(g_lock);
        const auto id = std::make_pair(w.key, w.mode);
        g_busy.erase(id);
        g_ready[id] = std::move(p);
        g_ready_order.push_back(id);
        while (g_ready_order.size() > kKept) {
            g_ready.erase(g_ready_order.front());
            g_ready_order.pop_front();
        }
    }
}

void Scan() {
    if (g_scanned) return;
    g_scanned = true;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(PackDir(), ec)) {
        const std::string stem = e.path().stem().string(), ext = e.path().extension().string();
        if (ext == ".jpg" && stem.size() == 16) {
            char* end = nullptr;
            uint64_t key = std::strtoull(stem.c_str(), &end, 16);
            if (end && !*end) g_have.insert(key);
        } else if (ext == ".txt") {
            std::vector<uint64_t> order;
            if (FILE* f = std::fopen(e.path().string().c_str(), "r")) {
                char line[64];
                while (std::fgets(line, sizeof line, f)) order.push_back(std::strtoull(line, nullptr, 16));
                std::fclose(f);
            }
            for (uint32_t i = 0; i < order.size(); i++) g_at.try_emplace(order[i], uint32_t(g_orders.size()), i);
            g_orders.push_back(std::move(order));
        }
    }
    if (g_have.empty()) return;
    LOGR("movie pack: %zu frames of %zu movies in %s", g_have.size(), g_orders.size(), PackDir().string().c_str());
    for (int i = 0; i < 2; i++) std::thread(Reader).detach();
}

// under g_lock: read it (and the frames after it) unless it is in memory or on its way
void Ask(uint64_t key, uint32_t mode, void (*recolor)(uint32_t*, uint32_t, uint32_t, uint32_t)) {
    if (!g_have.count(key)) return;
    const auto id = std::make_pair(key, mode);
    if (g_ready.count(id) || !g_busy.insert(id).second) return;
    g_wants.push_back({key, mode, recolor});
    g_cv.notify_one();
}
}  // namespace

bool MovieCollect(uint32_t header, const char* movie) {
    TextureDesc d;
    if (!header || !Describe(header, d) || d.cube || d.depth > 1 || d.width < 64 || d.height < 64) return false;
    const auto* guest = static_cast<const uint8_t*>(kernel::HostPtr(d.data));
    const uint64_t key = TextureKey(d, guest, 0);
    std::vector<uint32_t> px;
    {
        std::lock_guard lock(g_dump_lock);
        if (key == g_last_key) return false;
        g_last_key = key;
        std::error_code ec;
        const fs::path dir = PackDir() / "dump";
        fs::create_directories(dir, ec);
        const bool fresh = g_listed.insert(movie).second;
        if (FILE* f = std::fopen((dir / (std::string(movie) + ".txt")).string().c_str(), fresh ? "w" : "a")) {
            std::fprintf(f, "%016llx\n", (unsigned long long)key);
            std::fclose(f);
        }
        if (!g_dumped.insert(key).second) return false;
        static bool started = false;
        if (!started) started = true, std::thread(Writer).detach();
    }
    if (!DecodeBgra(d, 0, guest, px) || px.size() < size_t(d.width) * d.height) return false;
    std::lock_guard lock(g_dump_lock);
    g_dumps.push_back({key, d.width, d.height, std::move(px)});
    g_dump_cv.notify_one();
    return true;
}

uint64_t MovieKey(uint32_t header) {
    // off unless NF_MOVIE_PACK=1: making the frames takes hours on some GPUs (see PROGRESS.md), so it is no feature yet
    static const bool on = std::getenv("NF_MOVIE_PACK") != nullptr;
    if (!on || !Style().texture_pack) return 0;
    {
        std::lock_guard lock(g_lock);
        Scan();
        if (g_have.empty()) return 0;
    }
    TextureDesc d;
    if (!header || !Describe(header, d) || d.cube || d.depth > 1) return 0;
    const uint64_t key = TextureKey(d, static_cast<const uint8_t*>(kernel::HostPtr(d.data)), 0);
    std::lock_guard lock(g_lock);
    return g_have.count(key) ? key : 0;
}

void MovieShow(uint32_t header, uint64_t key, uint32_t mode,
               void (*recolor)(uint32_t* bgra, uint32_t width, uint32_t height, uint32_t mode)) {
    TextureDesc d;
    if (!key || !header || !Describe(header, d)) return;
    std::shared_ptr<const Picture> p;
    {
        std::lock_guard lock(g_lock);
        Ask(key, mode, recolor);
        if (auto at = g_at.find(key); at != g_at.end()) {
            const auto& order = g_orders[at->second.first];
            for (size_t i = 1; i <= kAhead; i++) Ask(order[(at->second.second + i) % order.size()], mode, recolor);
        }
        if (auto it = g_ready.find({key, mode}); it != g_ready.end()) p = it->second;
        if (p && p->w && key == g_shown) return;  // the same frame again: its replacement stands
        if (p && p->w) g_shown = key;
    }
    static uint32_t shown = 0, missed = 0;
    if (!p || !p->w) {  // not read yet: the game's own frame this time
        missed++;
        return;
    }
    SetReplacement(d.data, LevelSize(d, 0), p->w, p->h, p->px, true);
    if (++shown % 900 == 0) LOGR("movie pack: %u frames shown from the pack, %u not read in time", shown, missed);
}

}  // namespace gpu
