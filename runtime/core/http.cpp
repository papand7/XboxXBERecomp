// The system's curl runs the request (no web library linked in). On Windows its console is kept hidden.
#include "core/http.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace http {
namespace {
// the URL is quoted for the shell: none of ours carries a quote, and only the characters a query string uses pass
bool SafeUrl(const std::string& url) {
    for (char c : url)
        if (!(isalnum(static_cast<unsigned char>(c)) || std::string("-._~:/?&=%+").find(c) != std::string::npos)) return false;
    return !url.empty();
}
}  // namespace

bool Get(const std::string& url, std::string* body) {
    if (!SafeUrl(url)) return false;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"curl.exe -s -f -m 8 \"";
    for (char c : url) cmd += wchar_t(c);
    cmd += L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return false;
    }
    body->clear();
    char chunk[1024];
    DWORD got = 0;
    while (ReadFile(rd, chunk, sizeof chunk, &got, nullptr) && got) body->append(chunk, got);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
#else
    FILE* p = popen(("curl -s -f -m 8 '" + url + "'").c_str(), "r");
    if (!p) return false;
    body->clear();
    char chunk[1024];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, p)) > 0) body->append(chunk, got);
    return pclose(p) == 0;
#endif
}
}  // namespace http
