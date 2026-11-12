#pragma once
// LAN play for a title without a network mode of its own: every copy runs the whole game in lockstep, and only the
// controllers travel (UDP). Each frame waits for every player's controller of that frame; the title's clocks count
// frames instead of time, so each copy computes the same game from the same inputs. Player N plays controller N.
//
// Who hosts and who joins: XBE_NETPLAY (the environment, first) or the settings' netplay. "host[:port]" waits for
// XBE_NETPLAY_PLAYERS / netplay_players (2 to 4, default 2) players; "join[:port]" joins the settings' join_ip;
// "address[:port]" joins that host (port 27015 by default). XBE_NETPLAY_DELAY / netplay_delay: frames of input delay
// (default 2). XBE_NETPLAY_SIM=delay,jitter,loss (ms, ms, %; diagnostic) holds and drops this copy's outgoing packets
// as a slow network would. Both copies must start from the same saved data for the menus to match.
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

bool Configured();  // XBE_NETPLAY or the settings' netplay set
// before the title starts (a thread of its own): the lobby; false when it failed (playing alone)
bool Start();
// before relaunching into another image: the socket closed (the next process holds the lobby again)
void Stop();
// Title rules selected before Start; the host's snapshot is carried in the handshake (each copy then plays by the
// host's: a title reads SessionRules once Active).
void SetSessionRules(uint32_t rules);
uint32_t SessionRules();
bool Active();
int Players();
int Me();  // this copy's player, 0 the host
// the controller on that port this frame, the same on every copy
input::Pad Pad(int port);
// the title's frame ended (Present, paced at rate frames a second): this copy's controller sent, the next frame's of
// every player waited for; the title's clocks then move on by 1/rate of a second
void Frame(uint32_t rate = 60);
uint64_t FrameCount();
// the title's clocks while active, in units of freq a second: the frame's start, plus a little each read on this
// thread (a loop waiting on the clock still sees it move), never going back
uint64_t Clock(uint64_t freq);
// the same, the frame's start alone (what the title reads from memory: the tick count, the kernel's time records)
uint64_t FrameClock(uint64_t freq);
// fn runs on the title's thread as each frame starts (after Frame took every controller): the kernel writes its time
// records there, where every copy's title reads the same
void OnFrame(void (*fn)());
// fn runs once Start has met every player (SessionRules the host's), before the title's first instruction
void OnStart(void (*fn)());
// a title's hash of its game state, compared between the copies once a second (a desync is logged)
void SetStateHash(uint64_t (*hash)());
bool Desynced();  // the copies' hashes have differed once

}  // namespace netplay
