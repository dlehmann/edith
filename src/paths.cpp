// File path helpers and base64 (see paths.h).

#include "paths.h"

#include <cstdlib>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

std::string getCwd() {
    char b[4096];
    if (getcwd(b, sizeof b)) return b;
    return "/";
}

std::string normPath(const std::string& p) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string s = p.substr(i, j - i);
        if (s == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!s.empty() && s != ".") {
            parts.push_back(s);
        }
        i = j + 1;
    }
    std::string r;
    for (auto& s : parts) r += "/" + s;
    return r.empty() ? "/" : r;
}

std::string absPath(std::string p) {
    if (!p.empty() && p[0] == '~' && (p.size() == 1 || p[1] == '/')) {  // "~user" is not expanded
        const char* home = getenv("HOME");
        if (home) p = std::string(home) + p.substr(1);
    }
    if (p.empty() || p[0] != '/') p = getCwd() + "/" + p;
    return normPath(p);
}

std::string dirOf(const std::string& p) {
    size_t s = p.rfind('/');
    if (s == std::string::npos) return getCwd();
    return s == 0 ? "/" : p.substr(0, s);
}

std::string baseOf(const std::string& p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
    }
    return o;
}
