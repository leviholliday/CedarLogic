// Share links: a circuit's .cdl text, deflated (raw, no header) and written
// base64url, after "#c=":
//
//   https://cedarlogic.netlify.app/online-logic-gate-simulator/#c=<data>&n=<name>
//   cedarlogic://open#c=<data>&n=<name>
//
// The links the website's CedarLogic Online and the Mac app make and open
// (public/assets/js/sim-share.js on the site). GLib only, so Tools/
// share_check.cpp runs it anywhere.
#pragma once
#include <string>

namespace sharelink {

// Longer than this isn't a link to paste around (the website says so).
constexpr size_t kMaxWebLink = 8192;
// A circuit may inflate to this much.
constexpr size_t kMaxText = 20000000;
extern const char* const kWebBase;

// The data part of a link; empty if it couldn't be made.
std::string encode(const std::string& text);
// The .cdl text a link's data holds; false (and why) if it isn't one.
bool decode(const std::string& data, std::string& text, std::string& why);
// The data and the circuit's name out of a link ("#c=…&n=…", or after "?").
bool parse(const std::string& link, std::string& data, std::string& name);
// "c=<data>&n=<name>".
std::string fragment(const std::string& data, const std::string& name);

// A cedarlogic:// link as a .cdl file in the cache (named by the circuit, in
// a folder of its own for this data, so the same link opens the same copy);
// the file's path, or empty (and why).
std::string fileForLink(const std::string& link, std::string& why);

}  // namespace sharelink
