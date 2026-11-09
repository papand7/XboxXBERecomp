#include "core/net.h"

#include <cctype>
#include <fstream>

#include "core/hash.h"

#include "core/util.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

constexpr const char* kMirror = "https://mirror.dankelsoft.dk/";

std::string MirrorUrl(const std::string& url) {
    const std::string https = "https://";
    if (url.rfind(https, 0) != 0) return {};
    std::string rest = url.substr(https.size());
    if (rest.rfind("api.github.com/repos/", 0) == 0 && rest.find("/releases/latest") != std::string::npos)
        return std::string(kMirror) + "latest.json";
    if (rest.rfind("github.com/", 0) == 0 || rest.rfind("raw.githubusercontent.com/", 0) == 0) return kMirror + rest;
    return {};
}

bool FetchText(Http& http, const std::string& url, std::string& out, std::string& error) {
    for (int attempt = 0; attempt < 3; attempt++) {
        out.clear();
        if (http.Get(url, 0, [&](const uint8_t* p, size_t n) {
                out.append(reinterpret_cast<const char*>(p), n);
                return true;
            }, nullptr, error))
            return true;
    }
    std::string mirror = MirrorUrl(url), first = error;
    if (mirror.empty()) return false;
    out.clear();
    if (http.Get(mirror, 0, [&](const uint8_t* p, size_t n) {
            out.append(reinterpret_cast<const char*>(p), n);
            return true;
        }, nullptr, error))
        return true;
    error = first + " (the mirror too: " + error + ")";
    return false;
}

namespace {
bool DownloadFrom(Http& http, const std::string& url, const fs::path& path, const std::string& sha256, uint64_t size,
                  const Progress& progress, const std::atomic<bool>* cancel, std::string& error);
}  // namespace

bool Download(Http& http, const std::string& url, const fs::path& path, const std::string& sha256, uint64_t size,
              const Progress& progress, const std::atomic<bool>* cancel, std::string& error) {
    if (DownloadFrom(http, url, path, sha256, size, progress, cancel, error)) return true;
    std::string mirror = MirrorUrl(url), first = error;
    if (mirror.empty() || (cancel && cancel->load())) return false;
    std::error_code ec;
    fs::path part = path;
    part += ".part";
    fs::remove(part, ec);  // the mirror's copy from its start
    if (DownloadFrom(http, mirror, path, sha256, size, progress, cancel, error)) return true;
    error = first + " (the mirror too: " + error + ")";
    return false;
}

namespace {
bool DownloadFrom(Http& http, const std::string& url, const fs::path& path, const std::string& sha256, uint64_t size,
                  const Progress& progress, const std::atomic<bool>* cancel, std::string& error) {
    std::error_code ec;
    std::string want = Lower(sha256);
    if (fs::exists(path, ec) && (want.empty() ? fs::file_size(path, ec) == size || !size : FileSha256(path) == want))
        return true;
    fs::create_directories(path.parent_path(), ec);
    fs::path part = path;
    part += ".part";
    // a stopped download carries on from where it was; up to five tries, each from the current length
    for (int attempt = 0; attempt < 5; attempt++) {
        uint64_t have = fs::exists(part, ec) ? fs::file_size(part, ec) : 0;
        if (size && have > size) fs::remove(part, ec), have = 0;
        if (!size || have < size) {
            std::ofstream out(part, std::ios::binary | std::ios::app);
            if (!out) {
                error = "cannot write " + U8(part);
                return false;
            }
            uint64_t total = size, done = have;
            bool ok = http.Get(url, have, [&](const uint8_t* p, size_t n) {
                    if (cancel && cancel->load()) return false;
                    out.write(reinterpret_cast<const char*>(p), std::streamsize(n));
                    done += n;
                    if (progress) progress(done, total ? total : done);
                    return bool(out);
                }, &total, error);
            out.close();
            if (cancel && cancel->load()) {
                error = "cancelled";
                return false;
            }
            if (!ok) continue;
            if (!size) size = total;
        }
        if (!want.empty() && FileSha256(part) != want) {
            error = "the download of " + U8(path.filename()) + " is damaged (checksum), trying again";
            fs::remove(part, ec);
            continue;
        }
        fs::rename(part, path, ec);
        if (ec) {
            error = "cannot rename " + U8(part) + ": " + ec.message();
            return false;
        }
        return true;
    }
    if (error.empty()) error = "the download of " + url + " failed";
    return false;
}
}  // namespace

}  // namespace launcher
