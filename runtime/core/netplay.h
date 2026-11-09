#pragma once
// LAN play for a title without a network mode of its own: every copy runs the whole game in lockstep, and only the
// controllers travel (UDP). Each frame waits for every player's controller of that frame; the title's clocks count
// frames instead of time, so each copy computes the same game from the same inputs. Player N plays controller N.
//
// XBE_NETPLAY=host[:port] waits for XBE_NETPLAY_PLAYERS (2 to 4, default 2) players; XBE_NETPLAY=address[:port]
// joins one (port 27015 by default). XBE_NETPLAY_DELAY: frames of input delay (default 2). Both copies must start
// from the same saved data for the menus to match.
#include <cstdint>
#include <string>
#include <vector>

#include "core/input.h"

namespace netplay {

constexpr uint16_t kPort = 27015;  // the host's UDP port without one in XBE_NETPLAY

// LAN discovery: a host waiting in its lobby answers a "who hosts?" broadcast on its port
struct FoundHost {
    std::string name;     // the host PC's name
    std::string address;  // its IPv4 address, as heard (dotted)
    uint16_t port = kPort;
    int players = 2, joined = 1;  // the players it waits for, those in (itself counted)
};
// the hosts waiting in their lobby on this network, heard in the last three seconds; the first call starts the search
// (a broadcast each second, on a thread of its own), which runs until StopFindingHosts
std::vector<FoundHost> FindHosts();
void StopFindingHosts();
// this PC's IPv4 addresses on its networks (no loopback), the one with a gateway (the router's network) first
std::vector<std::string> LocalAddresses();

bool Configured();  // XBE_NETPLAY set
// before the title starts (a thread of its own): the lobby; false when it failed (playing alone)
bool Start();
// before relaunching into another image: the socket closed (the next process holds the lobby again)
void Stop();
// Title rules selected before Start; the host's snapshot is carried in the handshake.
void SetSessionRules(uint32_t rules);
uint32_t SessionRules();
bool Active();
int Players();
int Me();  // this copy's player, 0 the host
// the controller on that port this frame, the same on every copy
input::Pad Pad(int port);
// the title's frame ended (Present): this copy's controller sent, the next frame's of every player waited for
void Frame();
uint64_t FrameCount();
// the title's clocks while active, in units of freq a second: the frame's start, plus a little each read on this
// thread (a loop waiting on the clock still sees it move), never going back
uint64_t Clock(uint64_t freq);
// a title's hash of its game state, compared between the copies once a second (a desync is logged)
void SetStateHash(uint64_t (*hash)());

}  // namespace netplay
