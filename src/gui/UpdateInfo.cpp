/*****************************************************************************
   Project: CEDAR Logic Simulator

   UpdateInfo: see UpdateInfo.h
*****************************************************************************/

#include "UpdateInfo.h"

#include <cctype>
#include <cstdlib>
#include <cwchar>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#endif

#ifdef __APPLE__
// Implemented in SparkleUpdater.mm: NSURLSession has no C++ binding worth
// writing by hand, and that file is already Objective-C++.
//
// Declared weak so this translation unit still links in targets that compile it
// without SparkleUpdater.mm -- the logic test suite, which exercises the pure
// parsing below and has no business pulling in Foundation. When the real
// definition is linked it wins over the fallback here.
__attribute__((weak)) std::string cl_update_fetch_appcast_mac(const std::string &url) {
    (void)url;
    return std::string();
}
#endif

namespace cl {
namespace update {

Version parseVersion(const std::string &s) {
    Version v;
    size_t i = 0;
    while (i < s.size() && !isdigit((unsigned char)s[i])) i++;
    if (i == s.size()) return v; // no digits anywhere: not a version

    int part = 0;
    bool sawDigit = false;
    v.valid = true;
    while (i <= s.size()) {
        char c = i < s.size() ? s[i] : '.'; // trailing '.' flushes the last part
        if (isdigit((unsigned char)c)) {
            part = part * 10 + (c - '0');
            sawDigit = true;
        } else if (c == '.' || c == '-' || c == '+') {
            if (sawDigit && v.count < 4) v.parts[v.count++] = part;
            part = 0;
            sawDigit = false;
            if (c != '.') break; // pre-release suffix ends the comparison
        } else {
            break; // anything else (whitespace, 'v') ends it
        }
        i++;
    }
    return v;
}

bool newerThan(const Version &a, const Version &b) {
    for (int i = 0; i < 4; i++) {
        int x = i < a.count ? a.parts[i] : 0;
        int y = i < b.count ? b.parts[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

bool splitUrl(const std::string &url, std::string &host, std::string &path) {
    host.clear();
    path.clear();
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return false;
    size_t h = scheme + 3;
    size_t slash = url.find('/', h);
    if (slash == std::string::npos) {
        host = url.substr(h);
        path = "/";
    } else {
        host = url.substr(h, slash - h);
        path = url.substr(slash);
    }
    return !host.empty();
}

// The value of one attribute on one tag, or empty if absent. Written as a scan
// rather than an XML parse on purpose: the feed is ours, it is a few hundred
// bytes, and a crash-recovery path should not carry a parser.
static std::string attrOf(const std::string &tag, const std::string &name) {
    std::string needle = name + "=\"";
    size_t at = tag.find(needle);
    if (at == std::string::npos) return std::string();
    at += needle.size();
    size_t end = tag.find('"', at);
    return end == std::string::npos ? std::string() : tag.substr(at, end - at);
}

// The text of <name>...</name> within a range, or empty if absent.
static std::string elemText(const std::string &xml, size_t from, size_t to,
                            const std::string &name) {
    std::string open = "<" + name + ">", close = "</" + name + ">";
    size_t at = xml.find(open, from);
    if (at == std::string::npos || at >= to) return std::string();
    at += open.size();
    size_t end = xml.find(close, at);
    if (end == std::string::npos || end > to) return std::string();
    return xml.substr(at, end - at);
}

bool appcastLatest(const std::string &xml, const std::string &os, Version &out) {
    bool found = false;
    size_t at = 0;
    while ((at = xml.find("<item", at)) != std::string::npos) {
        size_t itemEnd = xml.find("</item>", at);
        if (itemEnd == std::string::npos) itemEnd = xml.size();
        std::string item = xml.substr(at, itemEnd - at);

        size_t enc = item.find("<enclosure");
        if (enc == std::string::npos) { at = itemEnd; continue; }
        size_t encEnd = item.find('>', enc);
        std::string tag = item.substr(enc, encEnd == std::string::npos
                                                 ? std::string::npos : encEnd - enc + 1);
        at = itemEnd;
        if (attrOf(tag, "sparkle:os") != os) continue;

        // The version is an attribute on <enclosure> in some feeds and a
        // <sparkle:version> element on the <item> in others -- ours is the
        // latter, which is what scripts/update-appcast.sh writes. Accept both,
        // the way WinSparkle's own appcast parser does.
        Version v = parseVersion(attrOf(tag, "sparkle:version"));
        if (!v.valid) v = parseVersion(elemText(item, 0, item.size(), "sparkle:version"));
        if (!v.valid) continue;
        if (!found || newerThan(v, out)) {
            out = v;
            found = true;
        }
    }
    return found;
}

std::string fetchAppcast(const std::string &url) {
    std::string host, path;
    if (!splitUrl(url, host, path)) return std::string();

#ifdef _WIN32
    // WinINet: already loaded by WinSparkle, and the crash path needs the
    // fewest moving parts it can get. One redirect is followed by hand so a
    // moved feed still answers. `url` is const, so the redirect target lands in
    // a local copy.
    std::string target = url;
    for (int hop = 0; hop < 2; hop++) {
        HINTERNET inet = InternetOpenA("CedarLogic", INTERNET_OPEN_TYPE_PRECONFIG,
                                       NULL, NULL, 0);
        if (!inet) return std::string();
        HINTERNET req = InternetOpenUrlA(inet, target.c_str(), NULL, 0,
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!req) { InternetCloseHandle(inet); return std::string(); }

        DWORD status = 0, len = sizeof(status);
        HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &status, &len, NULL);
        if (status == 301 || status == 302 || status == 307 || status == 308) {
            char buf[2048];
            DWORD blen = sizeof(buf);
            if (HttpQueryInfoA(req, HTTP_QUERY_LOCATION, buf, &blen, NULL) && blen) {
                target.assign(buf, blen);
                InternetCloseHandle(req);
                InternetCloseHandle(inet);
                continue;
            }
        }
        if (status != 200) {
            InternetCloseHandle(req);
            InternetCloseHandle(inet);
            return std::string();
        }

        std::string body;
        char chunk[8192];
        DWORD got = 0;
        while (body.size() < (1u << 20) &&
               InternetReadFile(req, chunk, sizeof(chunk), &got) && got > 0) {
            body.append(chunk, got);
        }
        InternetCloseHandle(req);
        InternetCloseHandle(inet);
        return body;
    }
    return std::string();
#elif defined(__APPLE__)
    return cl_update_fetch_appcast_mac(url);
#else
    // Linux: curl, or wget where curl is missing (a stock Ubuntu desktop has
    // only wget). The URL is ours and quoted, and a quote in it is refused.
    if (url.find('\'') != std::string::npos) return std::string();
    std::string body;
    const std::string cmds[] = {
        "curl -fsSL --max-time 15 '" + url + "' 2>/dev/null",
        "wget -q -T 15 -O - '" + url + "' 2>/dev/null",
    };
    for (const std::string &cmd : cmds) {
        FILE *p = popen(cmd.c_str(), "r");
        if (!p) continue;
        body.clear();
        char chunk[8192];
        size_t got;
        while (body.size() < (1u << 20) && (got = fread(chunk, 1, sizeof(chunk), p)) > 0)
            body.append(chunk, got);
        if (pclose(p) == 0 && !body.empty()) return body;
    }
    return std::string();
#endif
}

#if !defined(_WIN32) && !defined(__APPLE__)
bool downloadFile(const std::string &url, const std::string &dest) {
    if (url.find('\'') != std::string::npos || dest.find('\'') != std::string::npos)
        return false;
    const std::string cmds[] = {
        "curl -fsSL --max-time 600 -o '" + dest + "' '" + url + "' 2>/dev/null",
        "wget -q -T 60 -O '" + dest + "' '" + url + "' 2>/dev/null",
    };
    for (const std::string &cmd : cmds) {
        if (std::system(cmd.c_str()) == 0) return true;
        std::remove(dest.c_str());
    }
    return false;
}
#endif

bool appcastLatestItem(const std::string &xml, const std::string &os,
                       const std::string &arch, FeedItem &out) {
    bool found = false;
    size_t at = 0;
    while ((at = xml.find("<item", at)) != std::string::npos) {
        size_t itemEnd = xml.find("</item>", at);
        if (itemEnd == std::string::npos) itemEnd = xml.size();
        std::string item = xml.substr(at, itemEnd - at);
        at = itemEnd;

        size_t enc = item.find("<enclosure");
        if (enc == std::string::npos) continue;
        size_t encEnd = item.find('>', enc);
        std::string tag = item.substr(enc, encEnd == std::string::npos
                                                 ? std::string::npos : encEnd - enc + 1);
        if (attrOf(tag, "sparkle:os") != os) continue;
        if (!arch.empty() && attrOf(tag, "cedarlogic:arch") != arch) continue;

        FeedItem it;
        it.version = parseVersion(attrOf(tag, "sparkle:version"));
        if (!it.version.valid)
            it.version = parseVersion(elemText(item, 0, item.size(), "sparkle:version"));
        if (!it.version.valid) continue;
        it.url = attrOf(tag, "url");
        it.sha256 = attrOf(tag, "cedarlogic:sha256");
        it.length = std::atoll(attrOf(tag, "length").c_str());
        it.shortVersion = elemText(item, 0, item.size(), "sparkle:shortVersionString");
        if (it.url.empty()) continue;
        if (!found || newerThan(it.version, out.version)) {
            out = it;
            found = true;
        }
    }
    return found;
}

// ---- SHA-256 (FIPS 180-4), small and dependency-free ------------------------

namespace {

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64];
    size_t bufLen = 0;
    uint64_t total = 0;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const unsigned char *p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
                   (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const unsigned char *p, size_t n) {
        total += n;
        while (n > 0) {
            size_t take = 64 - bufLen < n ? 64 - bufLen : n;
            for (size_t i = 0; i < take; i++) buf[bufLen + i] = p[i];
            bufLen += take; p += take; n -= take;
            if (bufLen == 64) { block(buf); bufLen = 0; }
        }
    }

    std::string hex() {
        uint64_t bits = total * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (bufLen != 56) update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
        update(len, 8);
        static const char *digits = "0123456789abcdef";
        std::string out;
        for (uint32_t v : h)
            for (int s = 28; s >= 0; s -= 4) out += digits[(v >> s) & 0xf];
        return out;
    }
};

}  // namespace

std::string sha256Hex(const std::string &data) {
    Sha256 s;
    s.update(reinterpret_cast<const unsigned char *>(data.data()), data.size());
    return s.hex();
}

std::string sha256File(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    Sha256 s;
    char chunk[65536];
    while (in.read(chunk, sizeof(chunk)) || in.gcount() > 0)
        s.update(reinterpret_cast<const unsigned char *>(chunk), (size_t)in.gcount());
    return s.hex();
}

#ifdef _WIN32
// One registry read from an explicitly named view, so a caller can ask for the
// 64-bit tree rather than accepting whatever the redirector hands a 32-bit
// process. Accepts REG_DWORD or REG_SZ: administrators write both, and
// rejecting one of them leaves a policy that silently does nothing.
//
// `typeOut` and `dataOut` report what was actually stored, for --update-status.
static bool readPolicyFlag(REGSAM view, const wchar_t *subkey,
                           const wchar_t *value, bool &out,
                           std::string *typeOut = nullptr,
                           std::string *dataOut = nullptr) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | view, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, size = 0;
    bool ok = false;
    if (RegQueryValueExW(key, value, NULL, &type, NULL, &size) == ERROR_SUCCESS) {
        if (type == REG_DWORD) {
            DWORD data = 0;
            size = sizeof(data);
            if (RegQueryValueExW(key, value, NULL, &type,
                                 reinterpret_cast<BYTE *>(&data),
                                 &size) == ERROR_SUCCESS) {
                out = data != 0;
                ok = true;
                if (typeOut) *typeOut = "REG_DWORD";
                if (dataOut) *dataOut = std::to_string(data);
            }
        } else if (type == REG_SZ || type == REG_EXPAND_SZ) {
            std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
            DWORD bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
            if (RegQueryValueExW(key, value, NULL, &type,
                                 reinterpret_cast<BYTE *>(&buf[0]),
                                 &bytes) == ERROR_SUCCESS) {
                std::string text;
                for (wchar_t c : buf) {
                    if (c == L'\0') break;
                    text += static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                // Everything an administrator plausibly types for "yes".
                // Anything else counts as "no" rather than as an unreadable
                // value: this policy can only ever turn checking off.
                out = (text == "1" || text == "true" || text == "yes" ||
                       text == "on");
                ok = true;
                if (typeOut) *typeOut = "REG_SZ";
                if (dataOut) *dataOut = text;
            }
        }
    }
    RegCloseKey(key);
    return ok;
}

static const wchar_t *const kPolicyKey =
    L"SOFTWARE\\Policies\\Cedarville University\\CedarLogic";
static const wchar_t *const kPolicyValue = L"DisableUpdateChecks";
static const wchar_t *const kSparkleKey =
    L"Software\\Cedarville University\\CedarLogic\\WinSparkle";

// WinSparkle opens the registry with no view flag, so from this 32-bit program
// it only ever sees SOFTWARE\WOW6432Node. An administrator setting a
// machine-wide default with 64-bit tools writes the plain path, which
// WinSparkle then never finds. Look in both views, keeping WinSparkle's own
// precedence: the user's setting first, the machine's only as a default.
bool readWinSparkleSetting(const char *name, std::wstring &out,
                           std::string *whereFound) {
    std::wstring wide;
    for (const char *p = name; p && *p; ++p) wide += static_cast<wchar_t>(*p);

    struct Source { HKEY root; REGSAM view; const char *label; };
    // HKCU's 32-bit view comes first because that is where WinSparkle itself
    // writes, so a setting the user already has keeps winning.
    static const Source kSources[] = {
        {HKEY_CURRENT_USER,  KEY_WOW64_32KEY, "HKCU (WOW6432Node)"},
        {HKEY_CURRENT_USER,  KEY_WOW64_64KEY, "HKCU"},
        {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, "HKLM"},
        {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, "HKLM (WOW6432Node)"},
    };

    for (const Source &src : kSources) {
        HKEY key = NULL;
        if (RegOpenKeyExW(src.root, kSparkleKey, 0, KEY_QUERY_VALUE | src.view,
                          &key) != ERROR_SUCCESS) {
            continue;
        }
        DWORD type = 0, size = 0;
        bool ok = false;
        // REG_SZ only, matching WinSparkle, which stores every setting as a
        // string and ignores a value of any other type.
        if (RegQueryValueExW(key, wide.c_str(), NULL, &type, NULL, &size) ==
                ERROR_SUCCESS &&
            type == REG_SZ) {
            std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
            DWORD bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
            if (RegQueryValueExW(key, wide.c_str(), NULL, &type,
                                 reinterpret_cast<BYTE *>(&buf[0]),
                                 &bytes) == ERROR_SUCCESS) {
                buf.resize(wcslen(buf.c_str()));
                out = buf;
                ok = true;
            }
        }
        RegCloseKey(key);
        if (ok) {
            if (whereFound) *whereFound = src.label;
            return true;
        }
    }
    return false;
}
#endif

PolicyStatus describeUpdatePolicy() {
    PolicyStatus st;
#ifdef _WIN32
    // The 64-bit view first: that is the plain path an administrator writes.
    // The 32-bit view second, for anyone who set the policy from a 32-bit tool
    // or on a 32-bit machine.
    bool disabled = false;
    if (readPolicyFlag(KEY_WOW64_64KEY, kPolicyKey, kPolicyValue, disabled,
                       &st.policyType, &st.policyData)) {
        st.policyFound = true;
        st.policyView = "64-bit";
    } else if (readPolicyFlag(KEY_WOW64_32KEY, kPolicyKey, kPolicyValue,
                              disabled, &st.policyType, &st.policyData)) {
        st.policyFound = true;
        st.policyView = "32-bit (WOW6432Node)";
    }
    st.disabled = st.policyFound && disabled;

    std::wstring sparkle;
    std::string where;
    if (readWinSparkleSetting("CheckForUpdates", sparkle, &where)) {
        st.sparkleFound = true;
        st.sparkleWhere = where;
        for (wchar_t c : sparkle) st.sparkleValue += static_cast<char>(c);
    }
#else
    st.platformNote =
        "No update policy mechanism on this platform. macOS deployments "
        "configure Sparkle through a configuration profile instead.";
#endif
    return st;
}

bool checksDisabled() {
    return describeUpdatePolicy().disabled;
}

}  // namespace update
}  // namespace cl
