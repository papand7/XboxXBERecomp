// hd_pack: a texture pack made on the player's own PC from the textures the game dumped (Graphics Style > Textures >
// Dump the game's textures, or the launcher's Collect button: mods/textures/dump/<key>.png beside the exe). Each
// picture is upscaled 4x by Real-ESRGAN (realesrgan-ncnn-vulkan, any Vulkan GPU, beside this tool in tools/realesrgan)
// and written as mods/textures/<key>.png, which the runtime's texture pack loads in place of the game's
// (gpu/texture_pack.cpp). Nothing of the game's leaves the PC: the tool ships no game data, and the pack it makes is
// the player's own copy, not to be shared. Windows, Linux and macOS (pictures through app/image.cpp).
//
//   hd_pack [--scale 2|4] [--pbr] [--force] [--model x4plus|anime] [--dir <the game's folder>]
//   hd_pack --movies [--movie-model fast|quality] [--force] [--dir <the game's folder>]
//
// - Seams: game textures repeat, so each is padded with its own opposite edges before upscaling and cropped after.
// - Alpha: upscaled as a picture of its own (grey), so cut-outs and glass keep their shape.
// - 2x: the 4x result halved (a 2x2 average), sharper than a 2x model's.
// - --pbr: <key>_n.png, a tangent-space normal map (height in alpha) from the upscaled picture's brightness, and
//   <key>_r.png, roughness (white rough, black polished) from its local contrast and brightness. Generated, so an
//   approximation: the runtime reads roughness for reflections.
// - Done pictures are skipped (--force does them again): play more of the game with the dump on, run it again.
// - --movies (gpu/movie_pack.h), a developer run, not in the launcher: about 4 hours on a GPU where only x4plus draws right: the frames a title collected in mods/movies/dump/ (<key>.png,
//   <movie>.txt their order) upscaled 2x into mods/movies/<key>.jpg, a hundred at a time so the work folder stays
//   small; each collected frame is removed once its picture is written (the orders are kept beside them). fast (the
//   default): realesr-animevideov3, a 2x video model, several times quicker; quality: x4plus's 4x halved. Some GPUs
//   (an RTX 5060 here) make black or broken pictures with the fast model: its first frames are checked against the
//   collected ones (shrunk back, they must match), and quality is used instead when they do not.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app/image.h"

namespace fs = std::filesystem;

namespace {

struct Picture {
    uint32_t w = 0, h = 0;
    std::vector<uint32_t> px;  // B8G8R8A8
    uint32_t& at(uint32_t x, uint32_t y) { return px[size_t(y) * w + x]; }
    uint32_t at(uint32_t x, uint32_t y) const { return px[size_t(y) * w + x]; }
};

std::string U8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
fs::path FromU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool Read(const fs::path& path, Picture& p) {
    return app::DecodeImage(path, p.px, p.w, p.h, false, 16384) && p.w && p.h;
}

// through a temporary file: the game never reads a half-written picture
bool Write(const fs::path& path, const Picture& p) {
    fs::path tmp = path;
    tmp += ".tmp";
    std::error_code ec;
    bool ok = app::EncodePng(tmp, p.px, p.w, p.h);
    if (ok) fs::rename(tmp, path, ec);
    if (!ok || ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

uint32_t Margin(const Picture& p) { return std::clamp(std::min(p.w, p.h) / 8, 2u, 16u); }

// the picture with its opposite edges around it (a repeating texture's neighbours)
Picture Wrapped(const Picture& p, uint32_t m, bool alpha) {
    Picture o;
    o.w = p.w + 2 * m, o.h = p.h + 2 * m;
    o.px.resize(size_t(o.w) * o.h);
    for (uint32_t y = 0; y < o.h; y++)
        for (uint32_t x = 0; x < o.w; x++) {
            uint32_t c = p.at((x + p.w - m % p.w) % p.w, (y + p.h - m % p.h) % p.h);
            uint32_t a = c >> 24;
            o.at(x, y) = alpha ? 0xFF000000u | a << 16 | a << 8 | a : c | 0xFF000000u;
        }
    return o;
}

Picture Crop(const Picture& p, uint32_t m, uint32_t w, uint32_t h) {
    Picture o;
    o.w = w, o.h = h;
    o.px.resize(size_t(w) * h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) o.at(x, y) = p.at(std::min(x + m, p.w - 1), std::min(y + m, p.h - 1));
    return o;
}

Picture Half(const Picture& p) {
    Picture o;
    o.w = std::max(1u, p.w / 2), o.h = std::max(1u, p.h / 2);
    o.px.resize(size_t(o.w) * o.h);
    for (uint32_t y = 0; y < o.h; y++)
        for (uint32_t x = 0; x < o.w; x++) {
            uint32_t sum[4] = {};
            for (uint32_t dy = 0; dy < 2; dy++)
                for (uint32_t dx = 0; dx < 2; dx++) {
                    uint32_t c = p.at(std::min(x * 2 + dx, p.w - 1), std::min(y * 2 + dy, p.h - 1));
                    for (int k = 0; k < 4; k++) sum[k] += c >> (k * 8) & 0xFF;
                }
            uint32_t c = 0;
            for (int k = 0; k < 4; k++) c |= ((sum[k] + 2) / 4) << (k * 8);
            o.at(x, y) = c;
        }
    return o;
}

float Luma(uint32_t c) {
    return (0.114f * float(c & 0xFF) + 0.587f * float(c >> 8 & 0xFF) + 0.299f * float(c >> 16 & 0xFF)) / 255.0f;
}

// a normal map (height in alpha) and a roughness map from the picture, its edges wrapping
void Pbr(const Picture& p, Picture& normal, Picture& rough) {
    const uint32_t w = p.w, h = p.h;
    std::vector<float> hgt(size_t(w) * h), blur(size_t(w) * h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) hgt[size_t(y) * w + x] = Luma(p.at(x, y));
    auto H = [&](const std::vector<float>& v, int x, int y) {
        return v[size_t((y % int(h) + int(h)) % int(h)) * w + size_t((x % int(w) + int(w)) % int(w))];
    };
    // a small blur: the height without the picture's noise
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            float s = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) s += H(hgt, int(x) + dx, int(y) + dy) * (dx || dy ? 1.0f : 4.0f);
            blur[size_t(y) * w + x] = s / 12;
        }
    normal = rough = Picture{w, h, std::vector<uint32_t>(size_t(w) * h)};
    const float strength = 2.0f * float(std::max(w, h)) / 256.0f;  // steeper on a bigger picture of the same surface
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            int X = int(x), Y = int(y);
            float gx = (H(blur, X + 1, Y - 1) + 2 * H(blur, X + 1, Y) + H(blur, X + 1, Y + 1)) -
                       (H(blur, X - 1, Y - 1) + 2 * H(blur, X - 1, Y) + H(blur, X - 1, Y + 1));
            float gy = (H(blur, X - 1, Y + 1) + 2 * H(blur, X, Y + 1) + H(blur, X + 1, Y + 1)) -
                       (H(blur, X - 1, Y - 1) + 2 * H(blur, X, Y - 1) + H(blur, X + 1, Y - 1));
            float nx = -gx * strength, ny = -gy * strength, nz = 1;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            auto u8 = [](float v) { return uint32_t(std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f); };
            uint32_t height = uint32_t(std::clamp(H(blur, X, Y), 0.0f, 1.0f) * 255.0f + 0.5f);
            normal.at(x, y) = height << 24 | u8(nx / len) << 16 | u8(ny / len) << 8 | u8(nz / len);
            // rough where the surface is busy (its detail scatters light), smoother where it is even and bright
            float contrast = std::fabs(H(hgt, X, Y) - H(blur, X, Y)) * 6;
            float r = std::clamp(0.55f + contrast - 0.25f * H(blur, X, Y), 0.05f, 1.0f);
            uint32_t g = uint32_t(r * 255.0f + 0.5f);
            rough.at(x, y) = 0xFF000000u | g << 16 | g << 8 | g;
        }
}

bool Opaque(const Picture& p) {
    for (uint32_t c : p.px)
        if ((c >> 24) < 255) return false;
    return true;
}

int Run(const fs::path& exe, const std::vector<std::string>& args) {
#ifdef _WIN32
    std::wstring line = L"\"" + exe.wstring() + L"\"";
    for (const std::string& a : args) line += L" \"" + FromU8(a).wstring() + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return int(code);
#else
    std::error_code ec;  // a copy of the program may have lost its execute bit
    fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    std::string path = U8(exe);
    std::vector<std::string> copy = args;
    std::vector<char*> argv{path.data()};
    for (std::string& a : copy) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv.data(), environ) != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

fs::path ExeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return fs::current_path();
    std::error_code ec;
    return fs::canonical(buf, ec).parent_path();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

#ifdef _WIN32
const char* const kUpscaler = "realesrgan-ncnn-vulkan.exe";
#else
const char* const kUpscaler = "realesrgan-ncnn-vulkan";
#endif

// an upscaled frame shrunk back to its source's size matches it (a real picture, not black or noise)
bool Matches(const Picture& src, const Picture& up) {
    if (!src.w || up.w != src.w * 2 || up.h != src.h * 2) return false;
    const Picture back = Half(up);
    double diff = 0;
    for (size_t i = 0; i < src.px.size(); i++)
        for (int k = 0; k < 3; k++) diff += std::abs(int(src.px[i] >> (k * 8) & 0xFF) - int(back.px[i] >> (k * 8) & 0xFF));
    return diff / (double(src.px.size()) * 3) < 12;
}

// the movies' frames: mods/movies/dump/<key>.png to mods/movies/<key>.jpg, 2x; false when one failed
bool Movies(const fs::path& dir, const fs::path& esr, bool fast, bool force) {
    const fs::path dump = dir / "mods" / "movies" / "dump", out = dir / "mods" / "movies";
    std::error_code ec;
    if (!fs::exists(dump, ec)) return true;
    for (auto& e : fs::directory_iterator(dump, ec))
        if (e.path().extension() == ".txt") fs::copy_file(e.path(), out / e.path().filename(), fs::copy_options::overwrite_existing, ec);
    std::vector<fs::path> todo;
    for (auto& e : fs::directory_iterator(dump, ec)) {
        std::string stem = U8(e.path().stem());
        if (e.path().extension() != ".png" || stem.size() != 16) continue;
        if (force || !fs::exists(out / (stem + ".jpg"), ec)) todo.push_back(e.path());
        else fs::remove(e.path(), ec);  // done before
    }
    std::sort(todo.begin(), todo.end());
    std::printf("hd_pack: %zu movie frames to upscale 2x\n", todo.size());
    std::fflush(stdout);
    if (todo.empty()) return true;
    const fs::path work = out / "hd_pack_work", in = work / "in", up = work / "out";
    constexpr size_t kBatch = 100;
    size_t done = 0;
    for (size_t first = 0; first < todo.size(); first += kBatch) {
        fs::remove_all(work, ec);
        fs::create_directories(in, ec);
        fs::create_directories(up, ec);
        const size_t end = std::min(todo.size(), first + kBatch);
        for (size_t i = first; i < end; i++) {
            ec.clear();
            fs::create_hard_link(todo[i], in / todo[i].filename(), ec);  // the same drive: no copy
            if (ec) fs::copy_file(todo[i], in / todo[i].filename(), ec);
        }
        auto upscale = [&] {
            return fast ? Run(esr, {"-i", U8(in), "-o", U8(up), "-n", "realesr-animevideov3", "-s", "2", "-f", "png"})
                        : Run(esr, {"-i", U8(in), "-o", U8(up), "-n", "realesrgan-x4plus", "-s", "4", "-f", "png"});
        };
        int code = upscale();
        if (code == 0 && fast && first == 0) {  // the fast model's first frames checked: some GPUs make black or noise
            bool good = true;
            for (size_t i = first; i < std::min(end, first + 3) && good; i++) {
                Picture src, out;
                good = Read(todo[i], src) && Read(up / todo[i].filename(), out) && Matches(src, out);
            }
            if (!good) {
                std::printf("hd_pack: the fast model makes broken pictures on this GPU: using the quality model (slower)\n");
                std::fflush(stdout);
                fast = false;
                fs::remove_all(up, ec);
                fs::create_directories(up, ec);
                code = upscale();
            }
        }
        if (code != 0) {
            std::printf("hd_pack: the upscaler failed on the movie frames (exit %d)\n", code);
            fs::remove_all(work, ec);
            return false;
        }
        for (size_t i = first; i < end; i++) {
            const std::string stem = U8(todo[i].stem());
            Picture big;
            if (!Read(up / (stem + ".png"), big)) {
                std::printf("  %s: no upscaled frame\n", stem.c_str());
                continue;
            }
            const Picture half = fast ? big : Half(big);
            fs::path tmp = out / (stem + ".jpg.tmp");
            if (!app::EncodeJpeg(tmp, half.px, half.w, half.h, 90)) {
                fs::remove(tmp, ec);
                std::printf("  %s: could not be written\n", stem.c_str());
                continue;
            }
            fs::rename(tmp, out / (stem + ".jpg"), ec);
            if (!ec) fs::remove(todo[i], ec), done++;
        }
        std::printf("hd_pack: movie frames %zu of %zu\n", done, todo.size());
        std::fflush(stdout);
    }
    fs::remove_all(work, ec);
    std::printf("hd_pack: %zu of %zu movie frames written to %s\n", done, todo.size(), U8(out).c_str());
    return done == todo.size();
}

int Main(const std::vector<std::string>& argv) {
    uint32_t scale = 4;
    bool pbr = false, force = false, movies = false, fast = true;
    std::string model = "realesrgan-x4plus";
    fs::path dir = ExeDir();
    if (dir.filename() == "tools") dir = dir.parent_path();  // beside the game as tools/hd_pack
    for (size_t i = 0; i < argv.size(); i++) {
        const std::string& a = argv[i];
        if (a == "--scale" && i + 1 < argv.size()) scale = argv[++i] == "2" ? 2 : 4;
        else if (a == "--pbr") pbr = true;
        else if (a == "--force") force = true;
        else if (a == "--model" && i + 1 < argv.size())
            model = argv[++i] == "anime" ? "realesrgan-x4plus-anime" : "realesrgan-x4plus";
        else if (a == "--dir" && i + 1 < argv.size()) dir = FromU8(argv[++i]);
        else if (a == "--movies") movies = true;
        else if (a == "--movie-model" && i + 1 < argv.size()) fast = argv[++i] != "quality";
        else {
            std::printf("hd_pack [--scale 2|4] [--pbr] [--force] [--model x4plus|anime] [--dir <game folder>]\n"
                        "hd_pack --movies [--movie-model fast|quality] [--force] [--dir <game folder>]\n");
            return 2;
        }
    }
    fs::path dump = dir / "mods" / "textures" / "dump", out = dir / "mods" / "textures";
    fs::path esr = ExeDir() / "realesrgan" / kUpscaler;
    if (!fs::exists(esr)) esr = dir / "tools" / "realesrgan" / kUpscaler;
    std::error_code ec;
    if (!fs::exists(esr, ec)) {
        std::printf("hd_pack: the upscaler is missing (tools/realesrgan/%s)\n", kUpscaler);
        return 1;
    }
    if (movies) {
        if (!fs::exists(dir / "mods" / "movies" / "dump", ec)) {
            std::printf("hd_pack: no movie frames collected in %s: collect them first (the launcher's HD Movies)\n",
                        U8(dir / "mods" / "movies" / "dump").c_str());
            return 1;
        }
        return Movies(dir, esr, fast, force) ? 0 : 1;
    }
    if (!fs::exists(dump, ec)) {
        std::printf("hd_pack: no dumped textures in %s: collect the textures first (the launcher's Collect button, "
                    "or turn on Dump the game's textures and play)\n", U8(dump).c_str());
        return 1;
    }
    // the pictures not done yet
    std::vector<fs::path> todo;
    for (auto& e : fs::directory_iterator(dump, ec)) {
        std::string stem = U8(e.path().stem());
        if (e.path().extension() != ".png" || stem.size() != 16) continue;
        if (force || !fs::exists(out / e.path().filename(), ec)) todo.push_back(e.path());
    }
    std::sort(todo.begin(), todo.end());
    std::printf("hd_pack: %zu textures to upscale %ux%s\n", todo.size(), scale, pbr ? " with normal and roughness maps" : "");
    if (todo.empty()) return 0;
    fs::path work = dir / "mods" / "textures" / "hd_pack_work", in = work / "in", up = work / "out";
    fs::remove_all(work, ec);
    fs::create_directories(in, ec);
    fs::create_directories(up, ec);
    struct Item {
        fs::path src;
        uint32_t w, h, m;
        bool alpha;
    };
    std::vector<Item> items;
    for (const fs::path& f : todo) {
        Picture p;
        if (!Read(f, p)) {
            std::printf("  %s: not readable, left out\n", U8(f.filename()).c_str());
            continue;
        }
        uint32_t m = Margin(p);
        bool alpha = !Opaque(p);
        std::string stem = U8(f.stem());
        if (!Write(in / (stem + ".png"), Wrapped(p, m, false)) ||
            (alpha && !Write(in / (stem + "_a.png"), Wrapped(p, m, true)))) {
            std::printf("  %s: could not be prepared, left out\n", U8(f.filename()).c_str());
            continue;
        }
        items.push_back({f, p.w, p.h, m, alpha});
    }
    // one run over the folder: the model is loaded once
    std::printf("hd_pack: upscaling %zu pictures on the GPU (Real-ESRGAN)...\n", items.size());
    std::fflush(stdout);
    if (int code = Run(esr, {"-i", U8(in), "-o", U8(up), "-n", model, "-s", "4", "-f", "png"}); code != 0) {
        std::printf("hd_pack: the upscaler failed (exit %d); is the GPU's Vulkan driver installed?\n", code);
        return 1;
    }
    size_t done = 0;
    for (const Item& it : items) {
        std::string stem = U8(it.src.stem());
        Picture big, a;
        if (!Read(up / (stem + ".png"), big) || (it.alpha && !Read(up / (stem + "_a.png"), a))) {
            std::printf("  %s: no upscaled picture\n", stem.c_str());
            continue;
        }
        Picture rgb = Crop(big, it.m * 4, it.w * 4, it.h * 4);
        if (it.alpha) {
            Picture al = Crop(a, it.m * 4, it.w * 4, it.h * 4);
            for (size_t i = 0; i < rgb.px.size(); i++) rgb.px[i] = (rgb.px[i] & 0xFFFFFF) | (al.px[i] & 0xFF) << 24;
        }
        if (scale == 2) rgb = Half(rgb);
        if (!Write(out / (stem + ".png"), rgb)) {
            std::printf("  %s: could not be written\n", stem.c_str());
            continue;
        }
        if (pbr) {
            Picture n, r;
            Pbr(rgb, n, r);
            Write(out / (stem + "_n.png"), n);
            Write(out / (stem + "_r.png"), r);
        }
        done++;
    }
    fs::remove_all(work, ec);
    std::printf("hd_pack: %zu of %zu textures written to %s (the game loads them at its next start)\n", done,
                todo.size(), U8(out).c_str());
    std::printf("hd_pack: the pack is made from your copy of the game: keep it to yourself, do not share it\n");
    return done == items.size() ? 0 : 1;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(size_t(std::max(n, 1)) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(s);
    }
    return Main(args);
}
#else
int main(int argc, char** argv) { return Main(std::vector<std::string>(argv + 1, argv + argc)); }
#endif
