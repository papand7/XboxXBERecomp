// LAN lockstep (netplay.h). The host relays: every packet goes to the host, which passes the clients' on to each
// other, so a client needs no one's address but the host's. An input packet carries the sender's controllers from the
// oldest frame some copy still lacks (its acks, at most 64 frames) to its newest, so a lost packet costs nothing more
// than the next one's arrival.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "core/netplay.h"
#include "app/app.h"
#include "core/log.h"

#pragma comment(lib, "ws2_32.lib")

namespace netplay {
namespace {

constexpr uint32_t kMagic = 0x504E464E;  // "NFNP"
constexpr uint16_t kDefaultPort = 27015;
constexpr int kMaxPlayers = 4, kRing = 256, kMaxSend = 64;
enum : uint8_t { kHello = 1, kWelcome, kStart, kInput, kHash, kBye };

#pragma pack(push, 1)
struct WirePad {
    uint16_t buttons;
    uint8_t analog[8];
    int16_t lx, ly, rx, ry;
};
struct Header {
    uint32_t magic;
    uint8_t type, from, players, delay;
};
struct InputHeader {
    Header h;
    uint32_t acks[kMaxPlayers];  // the sender has every player's frames below these
    uint32_t first;
    uint8_t count;
};
struct HashPacket {
    Header h;
    uint32_t frame;
    uint64_t hash;
};
#pragma pack(pop)

SOCKET g_sock = INVALID_SOCKET;
bool g_configured = false, g_host = false;
std::atomic<bool> g_active{false};
int g_players = 1, g_me = 0, g_delay = 2;
sockaddr_in g_host_addr{};
sockaddr_in g_peer[kMaxPlayers]{};  // the host's: each client's address
bool g_joined[kMaxPlayers] = {};

std::mutex g_lock;  // the frame's pads, read from any thread
WirePad g_ring[kMaxPlayers][kRing] = {};
uint32_t g_have[kMaxPlayers] = {};               // frames below this held, per player
uint32_t g_acks[kMaxPlayers][kMaxPlayers] = {};  // [copy][player]: what each copy holds
std::atomic<uint64_t> g_frame{0};
input::Pad g_now[kMaxPlayers];
bool g_dead[kMaxPlayers] = {};
uint64_t (*g_hash)() = nullptr;
struct Hashes {
    uint32_t frame = ~0u;
    uint64_t mine = 0, theirs = 0;
    bool have_mine = false, have_theirs = false;
    int from = 0;
} g_hashes[kRing];
bool g_desync = false;

void Compare(Hashes& h) {
    if (!h.have_mine || !h.have_theirs || g_desync) return;
    if (h.mine == h.theirs) {
        static uint32_t agreed = 0;
        if (++agreed % 60 == 1) LOGR("netplay: the copies agree at frame %u", h.frame);  // once a minute
        return;
    }
    g_desync = true;
    LOGR("netplay: DESYNC at frame %u: player %d's game differs from this copy's (%016llX, here %016llX)", h.frame,
         h.from + 1, (unsigned long long)h.theirs, (unsigned long long)h.mine);
}

Hashes& HashSlot(uint32_t frame) {
    Hashes& h = g_hashes[frame % kRing];
    if (h.frame != frame) h = {}, h.frame = frame;
    return h;
}

std::string Env(const char* name) {
    const char* e = std::getenv(name);
    return e ? e : "";
}

Header Head(uint8_t type) { return {kMagic, type, uint8_t(g_me), uint8_t(g_players), uint8_t(g_delay)}; }

void SendTo(const sockaddr_in& to, const void* data, int len) {
    sendto(g_sock, static_cast<const char*>(data), len, 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
}

// to every other copy: the host to each client, a client to the host
void SendAll(const void* data, int len, int except = -1) {
    if (!g_host) return SendTo(g_host_addr, data, len);
    for (int p = 1; p < g_players; p++)
        if (p != except && g_joined[p]) SendTo(g_peer[p], data, len);
}

int Recv(void* buf, int len, sockaddr_in& from, int wait_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(g_sock, &set);
    timeval tv{0, wait_ms * 1000};
    if (select(0, &set, nullptr, nullptr, &tv) <= 0) return 0;
    int fl = sizeof from;
    int n = recvfrom(g_sock, static_cast<char*>(buf), len, 0, reinterpret_cast<sockaddr*>(&from), &fl);
    if (n < int(sizeof(Header)) || static_cast<Header*>(buf)->magic != kMagic) return 0;
    return n;
}

WirePad ToWire(const input::Pad& p) {
    WirePad w{};
    w.buttons = p.buttons;
    std::memcpy(w.analog, p.analog, 8);
    w.lx = p.lx, w.ly = p.ly, w.rx = p.rx, w.ry = p.ry;
    return w;
}

input::Pad FromWire(const WirePad& w) {
    input::Pad p;
    p.connected = true;
    p.buttons = w.buttons;
    std::memcpy(p.analog, w.analog, 8);
    p.lx = w.lx, p.ly = w.ly, p.rx = w.rx, p.ry = w.ry;
    return p;
}

// this copy's frames from the oldest some copy lacks
void SendInputs() {
    uint32_t newest = g_have[g_me], oldest = newest;
    for (int c = 0; c < g_players; c++)
        if (c != g_me && !g_dead[c]) oldest = std::min(oldest, g_acks[c][g_me]);
    oldest = std::max(oldest, newest > kMaxSend ? newest - kMaxSend : 0u);
    char buf[sizeof(InputHeader) + kMaxSend * sizeof(WirePad)];
    InputHeader& ih = *reinterpret_cast<InputHeader*>(buf);
    ih.h = Head(kInput);
    for (int p = 0; p < kMaxPlayers; p++) ih.acks[p] = g_have[p];
    ih.first = oldest;
    ih.count = uint8_t(newest - oldest);
    for (uint32_t f = oldest; f < newest; f++)
        reinterpret_cast<WirePad*>(buf + sizeof(InputHeader))[f - oldest] = g_ring[g_me][f % kRing];
    SendAll(buf, int(sizeof(InputHeader) + ih.count * sizeof(WirePad)));
}

void OnInput(const char* buf, int n) {
    const InputHeader& ih = *reinterpret_cast<const InputHeader*>(buf);
    int from = ih.h.from;
    if (n < int(sizeof ih) || from >= g_players || from == g_me ||
        n < int(sizeof ih + ih.count * sizeof(WirePad)))
        return;
    std::memcpy(g_acks[from], ih.acks, sizeof ih.acks);
    const WirePad* pads = reinterpret_cast<const WirePad*>(buf + sizeof ih);
    for (uint32_t i = 0; i < ih.count; i++) {
        uint32_t f = ih.first + i;
        if (f == g_have[from] && f < g_have[g_me] + kRing / 2) g_ring[from][f % kRing] = pads[i], g_have[from]++;
    }
    if (g_host) SendAll(buf, n, from);  // passed on to the other clients
}

void OnHash(const char* buf, int n) {
    if (n < int(sizeof(HashPacket))) return;
    const HashPacket& hp = *reinterpret_cast<const HashPacket*>(buf);
    if (g_host) SendAll(buf, n, hp.h.from);
    if (hp.frame + kRing / 2 <= g_frame.load()) return;  // long gone
    Hashes& h = HashSlot(hp.frame);
    h.theirs = hp.hash, h.have_theirs = true, h.from = hp.h.from;
    Compare(h);
}

// whatever came in, without waiting longer than wait_ms for the first
void Pump(int wait_ms) {
    char buf[1500];
    sockaddr_in from{};
    for (int n; (n = Recv(buf, sizeof buf, from, wait_ms)) > 0; wait_ms = 0) {
        const Header& h = *reinterpret_cast<Header*>(buf);
        if (h.type == kInput) OnInput(buf, n);
        else if (h.type == kHash) OnHash(buf, n);
        else if (h.type == kBye && h.from < g_players && !g_dead[h.from]) {
            g_dead[h.from] = true;
            LOGR("netplay: player %d left; their controller lets go", h.from + 1);
            if (g_host) SendAll(buf, n, h.from);
        } else if (h.type == kHello && g_host) {  // a client that missed its start
            for (int p = 1; p < g_players; p++)
                if (g_joined[p] && std::memcmp(&g_peer[p], &from, sizeof from) == 0) {
                    Header s = Head(kStart);
                    s.from = uint8_t(p);
                    SendTo(from, &s, sizeof s);
                }
        }
    }
}

bool Lobby() {
    char buf[1500];
    sockaddr_in from{};
    auto said = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    if (g_host) {
        int joined = 1;
        while (joined < g_players) {
            if (Recv(buf, sizeof buf, from, 20) < int(sizeof(Header))) continue;
            const Header& h = *reinterpret_cast<Header*>(buf);
            if (h.type != kHello) continue;
            int id = 0;
            for (int p = 1; p < g_players; p++)
                if (g_joined[p] && std::memcmp(&g_peer[p], &from, sizeof from) == 0) id = p;
            if (!id) {
                id = joined++;
                g_joined[id] = true;
                g_peer[id] = from;
                char ip[64];
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                LOGR("netplay: player %d joined from %s:%u (%d of %d)", id + 1, ip, ntohs(from.sin_port), joined,
                     g_players);
            }
            Header w = Head(kWelcome);
            w.from = uint8_t(id);
            SendTo(from, &w, sizeof w);
        }
        // everyone is told twice, and a late hello is answered while playing
        for (int i = 0; i < 2; i++)
            for (int p = 1; p < g_players; p++) {
                Header s = Head(kStart);
                s.from = uint8_t(p);
                SendTo(g_peer[p], &s, sizeof s);
            }
        return true;
    }
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (now - said > std::chrono::seconds(1)) {
            Header h = Head(kHello);
            SendTo(g_host_addr, &h, sizeof h);
            said = now;
        }
        if (Recv(buf, sizeof buf, from, 20) < int(sizeof(Header))) continue;
        const Header& h = *reinterpret_cast<Header*>(buf);
        if (h.type == kWelcome || h.type == kStart) {
            g_me = h.from, g_players = h.players, g_delay = h.delay;
            if (h.type == kStart) return true;
        }
    }
}

}  // namespace

bool Configured() {
    static const bool on = !Env("XBE_NETPLAY").empty();
    return on;
}

bool Start() {
    if (!Configured()) return false;
    g_configured = true;
    std::string spec = Env("XBE_NETPLAY"), addr = spec;
    uint16_t port = kDefaultPort;
    if (size_t c = spec.rfind(':'); c != std::string::npos) {
        addr = spec.substr(0, c);
        port = uint16_t(std::atoi(spec.c_str() + c + 1));
    }
    g_host = addr == "host";
    if (std::string d = Env("XBE_NETPLAY_DELAY"); !d.empty()) g_delay = std::clamp(std::atoi(d.c_str()), 0, 30);
    g_players = g_host ? std::clamp(std::atoi(Env("XBE_NETPLAY_PLAYERS").c_str()), 2, kMaxPlayers) : 2;
    if (g_host && Env("XBE_NETPLAY_PLAYERS").empty()) g_players = 2;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port = g_host ? htons(port) : 0;
    if (g_sock == INVALID_SOCKET || bind(g_sock, reinterpret_cast<sockaddr*>(&me), sizeof me) != 0) {
        LOGR("netplay: UDP port %u could not be opened (%d); playing alone", port, WSAGetLastError());
        return false;
    }
    if (!g_host) {
        g_host_addr.sin_family = AF_INET;
        g_host_addr.sin_port = htons(port);
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(addr.c_str(), nullptr, &hints, &res) != 0 || !res) {
            LOGR("netplay: the host '%s' was not found; playing alone", addr.c_str());
            return false;
        }
        g_host_addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    if (g_host) LOGR("netplay: hosting on UDP port %u, waiting for %d players", port, g_players);
    else LOGR("netplay: joining %s:%u", addr.c_str(), port);
    if (!Lobby()) return false;
    // the first frames, before anyone's controller can arrive: nothing held
    for (int p = 0; p < g_players; p++) {
        g_have[p] = uint32_t(g_delay);
        g_now[p].connected = true;
    }
    g_active = true;
    if (HWND w = static_cast<HWND>(app::Window()))
        SetWindowTextW(w, (std::wstring(app::kGame.title) + L" - player " + std::to_wstring(g_me + 1)).c_str());
    LOGR("netplay: started as player %d of %d, %d frames of input delay", g_me + 1, g_players, g_delay);
    return true;
}

void Stop() {
    if (g_sock == INVALID_SOCKET) return;
    g_active = false;
    closesocket(g_sock);
    g_sock = INVALID_SOCKET;
}

bool Active() { return g_active.load(std::memory_order_relaxed); }
int Players() { return g_players; }
int Me() { return g_me; }
uint64_t FrameCount() { return g_frame.load(); }

input::Pad Pad(int port) {
    if (port < 0 || port >= g_players) return {};
    std::lock_guard lock(g_lock);
    return g_now[port];
}

void SetStateHash(uint64_t (*hash)()) { g_hash = hash; }

void Frame() {
    if (!Active()) return;
    uint64_t f = g_frame.load();  // the frame that just ended
    if (g_hash && f % 60 == 0) {
        Hashes& h = HashSlot(uint32_t(f));
        h.mine = g_hash(), h.have_mine = true;
        HashPacket hp{Head(kHash), uint32_t(f), h.mine};
        SendAll(&hp, sizeof hp);
        Compare(h);
    }
    // this copy's controller for frame f + 1 + delay
    g_ring[g_me][g_have[g_me] % kRing] = ToWire(input::Poll(0));
    g_have[g_me]++;
    uint32_t next = uint32_t(f + 1);
    auto start = std::chrono::steady_clock::now(), sent = start - std::chrono::seconds(1), said = start;
    for (;;) {
        Pump(0);
        bool ready = true;
        for (int p = 0; p < g_players; p++) ready &= g_dead[p] || g_have[p] > next;
        auto now = std::chrono::steady_clock::now();
        if (now - sent > std::chrono::milliseconds(ready ? 0 : 8)) SendInputs(), sent = now;
        if (ready) break;
        if (now - said > std::chrono::seconds(5)) {
            said = now;
            for (int p = 0; p < g_players; p++)
                if (!g_dead[p] && g_have[p] <= next)
                    LOGR("netplay: waiting for player %d at frame %u", p + 1, next);
        }
        if (now - start > std::chrono::seconds(20)) {
            for (int p = 0; p < g_players; p++)
                if (!g_dead[p] && g_have[p] <= next) {
                    g_dead[p] = true;
                    LOGR("netplay: player %d stopped answering; their controller lets go", p + 1);
                }
            break;
        }
        Pump(1);
    }
    {
        std::lock_guard lock(g_lock);
        for (int p = 0; p < g_players; p++) {
            g_now[p] = g_dead[p] ? input::Pad{} : FromWire(g_ring[p][next % kRing]);
            g_now[p].connected = true;
        }
    }
    g_frame = next;
}

uint64_t Clock(uint64_t freq) {
    // steps of a 4096th of a frame: the frame's start, a step more each read on this thread (4095 at most), never back
    thread_local uint64_t last_frame = ~0ull, reads = 0, prev = 0;
    uint64_t f = g_frame.load(std::memory_order_relaxed);
    if (f != last_frame) last_frame = f, reads = 0;
    uint64_t s = std::max(f * 4096 + std::min(++reads, uint64_t(4095)), prev + 1);
    prev = s;
    uint64_t per = freq / 60;
    return s / 4096 * per + s % 4096 * per / 4096;
}

}  // namespace netplay
