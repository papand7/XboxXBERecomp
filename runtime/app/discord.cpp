// Discord Rich Presence without Discord's SDK: its local RPC, as documented for game SDKs. The Discord client listens
// on a named pipe (Windows: \\.\pipe\discord-ipc-0 to -9; elsewhere a Unix socket of that name in $XDG_RUNTIME_DIR,
// $TMPDIR or /tmp). Each message is a frame: a little-endian opcode and length, then that many bytes of JSON. The
// first is a handshake (opcode 0: {"v":1,"client_id":"<the application's id>"}), answered by a READY dispatch; then
// SET_ACTIVITY commands (opcode 1) set what the profile shows. A background thread keeps the connection and sends the
// latest presence whenever it changes; without Discord, the setting off or no application id it does nothing (a
// connection is tried again every 15 s), and nothing is ever shown to the player.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

#include "app/app.h"
#include "core/log.h"

namespace app {
namespace {

struct Presence {
    std::string details, state;
    int64_t start = 0;
    bool operator==(const Presence&) const = default;
};
std::mutex g_lock;
Presence g_want;
uint64_t g_want_version = 0;
std::once_flag g_started;

std::string Escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (uint8_t(c) < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += c;
    }
    return o;
}

std::string Narrow(const wchar_t* w) {
    std::string o;
    for (; w && *w; w++) o += *w < 0x80 ? char(*w) : '?';
    return o;
}

// one connection to the client
class Pipe {
public:
    ~Pipe() { Close(); }
    bool Open() {
        Close();
        for (int i = 0; i < 10; i++) {
#ifdef _WIN32
            wchar_t name[64];
            std::swprintf(name, 64, L"\\\\.\\pipe\\discord-ipc-%d", i);
            HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                h_ = h;
                return true;
            }
#else
            const char* dir = std::getenv("XDG_RUNTIME_DIR");
            if (!dir || !*dir) dir = std::getenv("TMPDIR");
            if (!dir || !*dir) dir = "/tmp";
            sockaddr_un a{};
            a.sun_family = AF_UNIX;
            std::snprintf(a.sun_path, sizeof a.sun_path, "%s/discord-ipc-%d", dir, i);
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) return false;
            if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
                fd_ = fd;
                return true;
            }
            close(fd);
#endif
        }
        return false;
    }
    void Close() {
#ifdef _WIN32
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_), h_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ >= 0) close(fd_), fd_ = -1;
#endif
    }
    bool IsOpen() const {
#ifdef _WIN32
        return h_ != INVALID_HANDLE_VALUE;
#else
        return fd_ >= 0;
#endif
    }
    bool Send(uint32_t op, const std::string& json) {
        std::vector<uint8_t> b(8 + json.size());
        uint32_t n = uint32_t(json.size());
        std::memcpy(b.data(), &op, 4), std::memcpy(b.data() + 4, &n, 4);
        std::memcpy(b.data() + 8, json.data(), json.size());
#ifdef _WIN32
        DWORD done = 0;
        return WriteFile(h_, b.data(), DWORD(b.size()), &done, nullptr) && done == b.size();
#else
        return write(fd_, b.data(), b.size()) == ssize_t(b.size());
#endif
    }
    // a frame if one comes within ms: its opcode and JSON; false on none or a broken connection (broken set)
    bool Receive(uint32_t& op, std::string& json, int ms, bool& broken) {
        broken = false;
        uint8_t head[8];
        if (!ReadAll(head, 8, ms, broken)) return false;
        uint32_t n;
        std::memcpy(&op, head, 4), std::memcpy(&n, head + 4, 4);
        if (n > (1u << 20)) {
            broken = true;
            return false;
        }
        json.assign(n, '\0');
        return n == 0 || ReadAll(json.data(), n, 2000, broken);
    }

private:
    bool ReadAll(void* to, uint32_t n, int ms, bool& broken) {
        uint8_t* p = static_cast<uint8_t*>(to);
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (n) {
#ifdef _WIN32
            DWORD avail = 0;
            if (!PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr)) {
                broken = true;
                return false;
            }
            if (avail) {
                DWORD got = 0;
                if (!ReadFile(h_, p, std::min<DWORD>(avail, n), &got, nullptr) || !got) {
                    broken = true;
                    return false;
                }
                p += got, n -= got;
                continue;
            }
#else
            pollfd pf{fd_, POLLIN, 0};
            if (poll(&pf, 1, 10) > 0) {
                ssize_t got = read(fd_, p, n);
                if (got <= 0) {
                    broken = true;
                    return false;
                }
                p += got, n -= uint32_t(got);
                continue;
            }
#endif
            if (std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

std::string ActivityJson(const Presence& p, uint64_t nonce) {
    std::string a = "{";
    bool any = false;
    auto field = [&](const char* k, const std::string& v) {
        if (v.empty()) return;
        a += std::string(any ? "," : "") + "\"" + k + "\":\"" + Escape(v.substr(0, 127)) + "\"";
        any = true;
    };
    field("details", p.details);
    field("state", p.state);
    if (p.start > 0) a += std::string(any ? "," : "") + "\"timestamps\":{\"start\":" + std::to_string(p.start) + "}", any = true;
    // the application's art asset named "logo" (Discord's developer portal, Rich Presence > Art Assets), if it has one
    a += std::string(any ? "," : "") + "\"assets\":{\"large_image\":\"logo\",\"large_text\":\"" + Escape(Narrow(kGame.title)) + "\"}";
    a += "}";
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = (unsigned long)getpid();
#endif
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":" + a +
           "},\"nonce\":\"" + std::to_string(nonce) + "\"}";
}

void Run() {
    Pipe pipe;
    std::string connected_id;
    uint64_t sent_version = ~0ull, nonce = 0;
    auto next_try = std::chrono::steady_clock::now();
    bool said_none = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        bool on;
        std::string id;
        {
            std::lock_guard lock(UiLock());
            on = Get().discord;
            id = Get().discord_app_id;
        }
        if (!on || id.empty() || (pipe.IsOpen() && id != connected_id)) {
            if (pipe.IsOpen()) LOGR("discord: rich presence off"), pipe.Close();
            if (!on || id.empty()) continue;
        }
        auto now = std::chrono::steady_clock::now();
        if (!pipe.IsOpen()) {
            if (now < next_try) continue;
            next_try = now + std::chrono::seconds(15);
            if (!pipe.Open()) {
                if (!said_none) LOGR("discord: no Discord client running (rich presence waits for one)"), said_none = true;
                continue;
            }
            bool broken = false;
            uint32_t op = 0;
            std::string reply;
            if (!pipe.Send(0, "{\"v\":1,\"client_id\":\"" + Escape(id) + "\"}") || !pipe.Receive(op, reply, 3000, broken) ||
                reply.find("\"READY\"") == std::string::npos) {
                LOGR("discord: the handshake failed (%s); the application id %s may be wrong", reply.substr(0, 160).c_str(),
                     id.c_str());
                pipe.Close();
                next_try = now + std::chrono::seconds(60);
                continue;
            }
            connected_id = id;
            sent_version = ~0ull;
            said_none = false;
            LOGR("discord: connected (rich presence on)");
        }
        Presence want;
        uint64_t version;
        {
            std::lock_guard lock(g_lock);
            want = g_want, version = g_want_version;
        }
        bool broken = false;
        uint32_t op;
        std::string reply;
        while (pipe.Receive(op, reply, 0, broken)) {  // replies and events read as they come
            if (reply.find("\"ERROR\"") != std::string::npos) LOG_FIRST(4, "discord: %s", reply.substr(0, 200).c_str());
        }
        if (!broken && version != sent_version) {
            if (pipe.Send(1, ActivityJson(want, ++nonce))) {
                sent_version = version;
                LOGR("discord: presence \"%s\" / \"%s\"", want.details.c_str(), want.state.c_str());
            } else {
                broken = true;
            }
        }
        if (broken) {
            LOGR("discord: the connection closed");
            pipe.Close();
            next_try = now + std::chrono::seconds(15);
        }
    }
}

}  // namespace

void SetPresence(const std::string& details, const std::string& state, int64_t start) {
    {
        std::lock_guard lock(g_lock);
        Presence p{details, state, start};
        if (p == g_want && g_want_version) return;
        g_want = p;
        g_want_version++;
    }
    std::call_once(g_started, [] { std::thread(Run).detach(); });
}

}  // namespace app
