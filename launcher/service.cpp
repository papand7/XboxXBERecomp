// The launcher's work without a window, for a launcher drawn by another program (the game's Electron launcher): the
// same Builder as the ImGui launcher (win/main.cpp), driven by JSON lines on stdin, its status and log as JSON lines on
// stdout. One command a line: {"cmd": "state"}, {"cmd": "scan"}, {"cmd": "set_game", "path": ...}, {"cmd": "agree"},
// {"cmd": "build"}, {"cmd": "cancel"}, {"cmd": "play"}, {"cmd": "remove_build"}, {"cmd": "music", "path": ...},
// {"cmd": "check_update"}. Out: {"ev": "state", ...}, {"ev": "status", "step", "detail", "fraction"},
// {"ev": "log", "line"}, {"ev": "done", "job", "ok", "message"}, {"ev": "error", "message"}.
//   <short>LauncherService(.exe) --root <work folder> --games <folder whose .iso files are the player's game>
// Windows and Linux alike (the Linux one: posix/platform_posix.cpp, the system's own compilers where they do)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/builder.h"
#include "core/update.h"
#include "core/util.h"
#include "embedded.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace launcher;

namespace {

// Visual Studio Build Tools: the C++ libraries and the Windows SDK the Windows build fetches (a Linux build fetches
// nothing of Microsoft's: no license to accept)
#ifdef _WIN32
constexpr const char* kLicenseUrl = "https://go.microsoft.com/fwlink/?LinkId=2179911";
#else
constexpr const char* kLicenseUrl = "";
#endif

std::mutex g_out;
void Send(const json& j) {
    std::lock_guard l(g_out);
    std::string s = j.dump(-1, ' ', false, json::error_handler_t::replace);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

uint64_t FreeBytes(const fs::path& dir) {
    fs::path d = dir;
    std::error_code ec;
    while (!d.empty() && !fs::exists(d, ec)) d = d.parent_path();
#ifdef _WIN32
    ULARGE_INTEGER avail{};
    GetDiskFreeSpaceExW(d.c_str(), &avail, nullptr, nullptr);
    return avail.QuadPart;
#else
    fs::space_info s = fs::space(d, ec);
    return ec ? 0 : uint64_t(s.available);
#endif
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

struct Service {
    GameConfig game;
    std::unique_ptr<Http> http = MakeHttp();
    std::unique_ptr<Platform> platform = MakePlatform();
    std::unique_ptr<Builder> builder;
    fs::path root, games;
    GameCheck copy;
    std::atomic<bool> busy{false}, update{false};
    std::string job_name;
    std::thread worker;
    std::mutex lock;
    Release release;
    bool release_newer = false;

    json State() {
        std::string too_long;
        bool built = builder->Built();
        std::error_code ec;
        json j = {{"ev", "state"},
                  {"title", game.title},
                  {"version", game.version},
                  {"root", U8(root)},
                  {"play_dir", U8(builder->PlayDir())},
                  {"games_dir", U8(games)},
                  {"built", built},
                  {"built_version", builder->BuiltVersion()},
                  {"busy", bool(busy)},
                  {"job", job_name},
                  {"update", bool(update)},
                  {"agreed", !*kLicenseUrl || fs::exists(root / "agreed.txt", ec)},
                  {"os", platform->Os()},
                  {"license_url", kLicenseUrl},
                  {"free_bytes", FreeBytes(root)},
                  {"build_bytes", builder->BuildFilesBytes()},
                  {"path_too_long", !built && builder->PathTooLong(too_long)},
                  {"path_message", too_long},
                  {"install_command", busy ? std::string() : builder->install_command},  // written by jobs only
                  {"music", game.music},
                  {"music_dir", U8(builder->SavedMusic())},
                  {"game", {{"ok", copy.ok}, {"iso", copy.iso}, {"release", copy.release}, {"path", U8(copy.path)},
                            {"error", copy.error}}}};
        std::lock_guard l(lock);
        if (release_newer) j["release"] = {{"tag", release.tag}, {"name", release.name}, {"page", release.page}};
        return j;
    }

    void Start(const std::string& name, std::function<bool(std::string&, std::string&)> work) {
        if (busy) {
            Send({{"ev", "error"}, {"message", "Busy with " + job_name + "."}});
            return;
        }
        if (worker.joinable()) worker.join();
        busy = true;
        job_name = name;
        builder->cancel = false;
        worker = std::thread([this, name, work] {
            std::string error, message;
            bool ok = work(error, message);
            if (!ok && error == "cancelled") error = "Stopped. It carries on where it left off next time.";
            busy = false;
            Send({{"ev", "done"}, {"job", name}, {"ok", ok}, {"message", ok ? message : error}});
            Send(State());
        });
    }

    // the player's game: the copy chosen before while it is still there, else the first good .iso in games
    void Scan() {
        fs::path saved = builder->SavedGame();
        if (!saved.empty()) {
            GameCheck c = CheckGame(game, saved);
            if (c.ok) {
                copy = c;
                return;
            }
        }
        std::vector<fs::path> isos;
        std::error_code ec;
        for (auto& e : fs::directory_iterator(games, ec))
            if (e.is_regular_file(ec) && Lower(U8(e.path().extension())) == ".iso") isos.push_back(e.path());
        std::sort(isos.begin(), isos.end());
        GameCheck first_bad;
        for (const fs::path& iso : isos) {
            GameCheck c = CheckGame(game, iso);
            builder->Log("game: " + U8(iso.filename()) + (c.ok ? ": " + c.release : ": " + c.error));
            if (!c.ok) {
                if (first_bad.error.empty()) first_bad = c;
                continue;
            }
            SetGame(c);
            return;
        }
        copy = isos.empty() ? GameCheck{} : first_bad;
        if (isos.empty())
            copy.error = "Put your disc image (.iso) of the game in " + U8(games) + ", beside the launcher.";
    }

    void SetGame(GameCheck c) {
        std::string e;
        if (c.ok && !builder->SetGame(c, e)) c.ok = false, c.error = e;
        copy = c;
    }

    void Command(const json& j) {
        std::string cmd = j.value("cmd", "");
        if (cmd == "state") {
            Send(State());
        } else if (cmd == "scan") {
            if (!busy) Scan();
            Send(State());
        } else if (cmd == "set_game") {
            if (!busy) SetGame(CheckGame(game, PathOf(j.value("path", ""))));
            Send(State());
        } else if (cmd == "agree") {
            std::ofstream(root / "agreed.txt") << kLicenseUrl;
            Send(State());
        } else if (cmd == "build") {
            if (!copy.ok) {
                Send({{"ev", "error"}, {"message", copy.error.empty() ? "No game copy yet." : copy.error}});
                return;
            }
            update = false;
            Start("build", [this](std::string& e, std::string& m) {
                m = "The game is built. Press Play.";
                return builder->BuildAll(e);
            });
        } else if (cmd == "cancel") {
            builder->cancel = true;
        } else if (cmd == "play") {
            std::string e;
            if (busy) e = "Busy with " + job_name + ".";
            else if (!builder->Built()) e = "The game is not built yet.";
            else if (!copy.ok) e = copy.error.empty() ? "No game copy." : copy.error;
            if (e.empty() && builder->Launch(e)) Send({{"ev", "done"}, {"job", "play"}, {"ok", true}, {"message", ""}});
            else Send({{"ev", "done"}, {"job", "play"}, {"ok", false}, {"message", e}});
        } else if (cmd == "start_over") {
            Start("start_over", [this](std::string& e, std::string& m) {
                m = "Cleaned up. Press Install to download and build again.";
                return builder->StartOver(e);
            });
        } else if (cmd == "install_requirements") {
            Start("install_requirements", [this](std::string& e, std::string& m) {
                m = "The build packages are installed. Press Install.";
                return builder->InstallRequirements(e);
            });
        } else if (cmd == "remove_build") {
            Start("remove_build", [this](std::string& e, std::string& m) {
                m = "Build files removed. An update builds everything again.";
                return builder->RemoveBuildFiles(e);
            });
        } else if (cmd == "music") {
            fs::path folder = PathOf(j.value("path", ""));
            Start("music", [this, folder](std::string& e, std::string& m) {
                std::string summary;
                bool ok = builder->ImportMusic(folder, summary, e);
                m = "Your music is ready. " + summary;
                return ok;
            });
        } else if (cmd == "check_update") {
            Start("check_update", [this](std::string& err, std::string&) {
                if (IsReleaseVersion(game.version)) {
                    Release r;
                    std::string e;
                    if (!LatestRelease(*http, game, r, e)) {
                        builder->Log("update check: " + (e.empty() ? std::string("no answer") : e));
                    } else {
                        bool newer = CompareVersions(r.tag, game.version) > 0;
                        builder->Log("update check: the latest release is " + r.tag + ", this launcher is " + game.version);
                        std::lock_guard l(lock);
                        release = r;
                        release_newer = newer;
                    }
                }
                bool available = false;
                if (builder->Built() && builder->UpdateAvailable(available, err)) update = available;
                err.clear();  // offline: no news is fine
                return true;
            });
        } else {
            Send({{"ev", "error"}, {"message", "unknown command " + cmd}});
        }
    }
};

#ifdef _WIN32
using Char = wchar_t;
#define ARG(s) L##s
#else
using Char = char;
#define ARG(s) s
#endif
fs::path Arg(int argc, Char** argv, const Char* name) {
    for (int i = 1; i + 1 < argc; i++)
        if (std::basic_string<Char>(argv[i]) == name) return fs::path(argv[i + 1]);
    return {};
}

// this program's own file
fs::path Self() {
#ifdef _WIN32
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return self;
#elif defined(__APPLE__)
    char self[4096];
    uint32_t size = sizeof self;
    std::error_code ec;
    if (_NSGetExecutablePath(self, &size) != 0) return {};
    return fs::canonical(self, ec);
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec);
#endif
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
#else
int main(int argc, char** argv) {
#endif
    std::setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
    Service s;
    std::string e;
    if (!ParseGameConfig(kGameJson, s.game, e)) {
        Send({{"ev", "error"}, {"message", e}});
        return 1;
    }
    s.game.version = kLauncherVersion;
    s.games = Arg(argc, argv, ARG("--games"));
    if (s.games.empty()) s.games = Self().parent_path();
    s.root = Arg(argc, argv, ARG("--root"));
    if (s.root.empty()) s.root = s.games / s.game.short_name;
    std::error_code ec;
    fs::create_directories(s.root, ec);
    s.builder = std::make_unique<Builder>(s.game, s.root, *s.http, *s.platform);
    s.builder->fallback_toolchain = kToolchainJson;
    s.builder->on_status = [](const Status& st) {
        Send({{"ev", "status"}, {"step", st.step}, {"detail", st.detail}, {"fraction", st.fraction}});
    };
    s.builder->on_log = [](const std::string& line) { Send({{"ev", "log"}, {"line", line}}); };
    s.Scan();
    Send(s.State());

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            Send({{"ev", "error"}, {"message", "not a command: " + line}});
            continue;
        }
        if (j.value("cmd", "") == "quit") break;
        s.Command(j);
    }
    // stdin closed (the launcher window went away): a build stops where it is, to carry on next time
    s.builder->cancel = true;
    if (s.worker.joinable()) s.worker.join();
    return 0;
}
