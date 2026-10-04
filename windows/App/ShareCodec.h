// Share links: a circuit's .cdl text, deflated (raw, no header) and written
// base64url, after "#c=":
//
//   https://cedarlogic.netlify.app/online-logic-gate-simulator/#c=<data>&n=<name>
//   cedarlogic://open#c=<data>&n=<name>
//
// The links the website's CedarLogic Online and the Mac and Linux apps make
// and open (public/assets/js/sim-share.js on the site). This is the format's
// half that is plain C++ (it uses Deflate.h and nothing of Windows, so
// windows/Tools/share-check.sh runs it on a Mac); ShareLink.h is the app's.

#ifndef CL_WINDOWS_SHARE_CODEC_H
#define CL_WINDOWS_SHARE_CODEC_H

#include <cstddef>
#include <string>

namespace sharelink {

extern const char* const kWebBase;
// Longer than this isn't a link to paste around (the website says so).
constexpr size_t kMaxWebLink = 8192;
// A circuit may inflate to this much.
constexpr size_t kMaxText = 20000000;

// The data part of a link; empty if there's nothing to make one of.
std::string encode(const std::string& text);
// The .cdl text a link's data holds; false (and why, in words for the person
// who pasted it) if it isn't one.
bool decode(const std::string& data, std::string& text, std::string& why);
// The data and the circuit's name out of a link ("#c=…&n=…", or after "?").
bool parse(const std::string& link, std::string& data, std::string& name);
// "c=<data>&n=<name>" (the name is cut to 80 characters, as it's read).
std::string fragment(const std::string& data, const std::string& name);

// --share-test, and share_check: the format's own checks -- round trips,
// links made by python's zlib (fixtures here, one of each kind of block),
// links that must be refused. Appends a PASS or FAIL line each to `report`;
// false if any failed. With `circuit` (a .cdl's text): its link is made too,
// returned in `*linkMade` (a check outside can inflate it with zlib); and
// `linkGiven` (a link some other program made of that circuit, "" for none)
// must decode to it.
bool selfTest(std::string& report, const std::string& circuit = std::string(), std::string* linkMade = nullptr,
              const std::string& linkGiven = std::string());

}  // namespace sharelink

#endif  // CL_WINDOWS_SHARE_CODEC_H
