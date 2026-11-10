#pragma once
// The launcher's work, step by step, under one folder (root):
//   tools\    clang, CMake, Ninja, MinGit, the C++ libraries, the Windows SDK (portable, nothing installed); on Linux
//             clang, CMake and Ninja when the system has none new enough (its git, C++ library and the window's and
//             sound's headers are the system's: CheckSystem says what to install)
//   source\   the game's repository (git), its xbe in source\assets for the build
//   play\     the built exe, its settings, saves and logs; play\assets is the player's game (a junction to their
//             folder, or their disc image linked or copied)
//   launcher.json   the player's choices (their game's path)
// Each step can run again: what is already done is kept (tools by their stamp, the source by git).
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include "core/game.h"
#include "core/net.h"
#include "core/plan.h"
#include "core/platform.h"

namespace launcher {

struct Status {
    std::string step;    // "Downloading Clang 19.1.7"
    std::string detail;  // "312 of 845 MB"
    double fraction = -1;  // 0..1, -1 when unknown
};

class Builder {
public:
    Builder(const GameConfig& game, const std::filesystem::path& root, Http& http, Platform& platform);

    // where messages go: a status for the window, each log line (also written to root\launcher.log)
    std::function<void(const Status&)> on_status;
    std::function<void(const std::string&)> on_log;
    std::atomic<bool> cancel{false};
    std::string fallback_toolchain;  // toolchain.json as the launcher was built with, when neither download nor copy

    const std::filesystem::path& Root() const { return root_; }
    std::filesystem::path PlayDir() const { return root_ / "play"; }
    std::filesystem::path GameExe() const;
    bool Built() const;  // the exe is in play

    // the player's copy: checked, remembered, linked into play\assets
    bool SetGame(const GameCheck& copy, std::string& error);
    std::filesystem::path SavedGame() const;  // the copy remembered from last time, empty if none

    // the whole build: tools, source, xbe, compile, install. Returns false with the reason (also in launcher.log)
    bool BuildAll(std::string& error);
    // the launcher's folder is too deep for the Windows SDK: msiexec (which unpacks it) fails past 260 characters, and
    // the SDK's deepest headers lie about 130 below tools\; message: what to do
    static constexpr size_t kMaxRootLength = 80;
    bool PathTooLong(std::string& message) const;
    // a build of another version is in play: a release launcher's own tag not built yet (no network asked), a dev
    // launcher's source behind its branch's head
    bool UpdateAvailable(bool& available, std::string& error);
    std::string BuiltVersion() const;  // the launcher version the game in play was built by, empty if not known
    bool Launch(std::string& error);
    uint64_t BuildFilesBytes() const;  // source\out, removable once built
    bool RemoveBuildFiles(std::string& error);
    // the downloaded source and its build removed (a download that broke, a build gone wrong): the next Install starts
    // from a clean copy; the build tools, the game copy and the saves (play/) are kept
    bool StartOver(std::string& error);
    // Linux: the packages CheckSystem found missing installed through pkexec (the system's own password prompt), with
    // the command it would have shown (install_command, empty when nothing is missing or the distribution is unknown)
    bool InstallRequirements(std::string& error);
    std::string install_command;
    bool SourceFromMirror(const std::filesystem::path& src, const std::string& ref, std::string& error);
    std::string RepoName() const;
    std::filesystem::path ShippedBundle(const std::string& name) const;
    bool CloneBundle(const std::filesystem::path& bundle, const std::filesystem::path& src, const std::string& ref,
                     std::string& error);
    bool SubmoduleFromBundle(const std::filesystem::path& src, const std::filesystem::path& bundle, std::string& error);
    bool SubmoduleFromMirror(const std::filesystem::path& src, std::string& error);

    // the game's Xbox custom soundtracks (GameConfig::music) from a folder of the player's music: each file converted
    // to WMA with ffmpeg (fetched the first time), ST.DB written, the folder remembered; summary: what was done
    bool ImportMusic(const std::filesystem::path& folder, std::string& summary, std::string& error);
    std::filesystem::path SavedMusic() const;  // the folder imported last, empty if none
    std::filesystem::path MusicDir() const;    // where the game finds them: play\hdd\E\TDATA\FFFE0000\MUSIC

    // steps, for the test tool
    bool LoadToolchain(Plan& plan, std::string& error);
    bool InstallTool(const Tool& tool, std::string& error);
    // off Windows: the system's own programs stand in for the tool (Tool::system), found is what was found
    bool SystemTool(const Tool& tool, std::string& found);
    // off Windows: what the build needs of the system and cannot fetch (git, the C++ library, the window's and sound's
    // development headers); false with what to install, as the distribution's package manager names it
    bool CheckSystem(std::string& error);
    bool CheckSystemMac(std::string& error);
    bool GetSource(std::string& error);
    bool PutXbe(std::string& error);
    bool Compile(std::string& error);
    bool Install(std::string& error);
    std::map<std::string, std::string> BuildEnv(const Plan& plan) const;

    // a line in launcher.log and the window's log (thread safe)
    void Log(const std::string& line);

private:
    void Remember(const std::string& key, const std::string& value);  // an entry of launcher.json, the others kept
    void Say(const std::string& step, const std::string& detail = {}, double fraction = -1);
    // git, its --progress lines shown as git_step_'s detail ("Receiving objects 120 of 2741, 12 MB, 2.1 MB/s")
    int Git(const std::vector<std::string>& args, std::string* out, std::string& error);
    bool MirrorBundle(const std::string& name, const std::filesystem::path& path, std::string& error);
    std::filesystem::path ToolPath(const std::string& id) const { return root_ / "tools" / id; }

    GameConfig game_;
    std::filesystem::path root_;
    Http& http_;
    Platform& platform_;
    Plan plan_;
    std::map<std::string, std::string> system_tools_;  // a tool id: the system's version used in its place
    std::string git_step_ = "Downloading the game's source";  // the status step git's progress goes under
    std::mutex log_lock_;
};

std::string Megabytes(uint64_t bytes);

}  // namespace launcher
