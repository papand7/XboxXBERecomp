#pragma once
// Downloads over HTTPS (WinHTTP on Windows, curl for the Linux test tool), resumed and checked
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "core/archive.h"

namespace launcher {

class Http {
public:
    virtual ~Http() = default;
    // GET from a byte offset (a Range request past 0), redirects followed; sink takes each piece and returns false to
    // stop. total: the whole file's size when the server says (offset + the length sent)
    virtual bool Get(const std::string& url, uint64_t offset, const std::function<bool(const uint8_t*, size_t)>& sink,
                     uint64_t* total, std::string& error) = 0;
};

std::unique_ptr<Http> MakeHttp();  // the platform's

// a small file into memory
bool FetchText(Http& http, const std::string& url, std::string& out, std::string& error);

// the fallback for a GitHub address when GitHub cannot be reached: the same path on the mirror
// (https://github.com/x -> https://mirror.dankelsoft.dk/github.com/x, raw.githubusercontent.com likewise, the API's
// latest release -> the mirror's latest.json); empty for any other address. FetchText and Download try it by themselves
std::string MirrorUrl(const std::string& url);

// url to path: a .part file resumed if a run was stopped, then its SHA-256 compared (when given) before it is
// renamed into place; a file already there with the right hash is kept
bool Download(Http& http, const std::string& url, const std::filesystem::path& path, const std::string& sha256,
              uint64_t size, const Progress& progress, const std::atomic<bool>* cancel, std::string& error);

}  // namespace launcher
