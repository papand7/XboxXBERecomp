#pragma once
#include <string>

namespace http {
// GET of a URL through the system's curl (Windows 10 and later ship curl.exe; macOS and Linux have it), its body
// into *body. False when curl is missing, the request fails or times out after 8 s. Blocks: call it off the game's
// thread (a background thread of the caller's).
bool Get(const std::string& url, std::string* body);
}  // namespace http
