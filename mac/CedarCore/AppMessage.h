// A message from CedarLogic (include/CedarMessage.h): which one is due, from message.json, and
// what's kept once it's answered.
//
// The file: { "messages": [ { "id", "from", "until"?, "platforms"?, "eyebrow"?, "title", "text",
//                             "note"?, "button"?, "later"?, "url" } ] }
//   id         a-z, 0-9 and "-", at most 40
//   from       "YYYY-MM-DD": shown from that day on, by the computer's own date
//   until      "YYYY-MM-DD", optional: not after that day
//   platforms  optional: some of "mac", "windows", "linux" (all when it's missing)
//   eyebrow    at most 40 characters; title at most 80; text at most 600; note at most 90;
//              button at most 30 ("Open" when missing)
//   later      days Remind Me Later waits, 1 to 60 (3 when missing)
//   url        https://cedarlogic.netlify.app/ and a path: never anywhere else
// The first one that's due wins. Anything wrong in a message (a field too long, an odd date, a
// url elsewhere) skips that message; a file over 16 KB, or not strict JSON, shows nothing.
//
// What's kept (one line of text, in the app's settings): the answered messages, comma-separated:
// "id" when it's done with (the form opened, or No Thanks), "id@YYYY-MM-DD" when it waits till
// that day (Remind Me Later).
#pragma once

#include <string>

namespace clmessage {

struct Message {
	std::string id, eyebrow, title, text, note, button, url;
	int later = 3;
};

constexpr size_t kMaxFile = 16 * 1024;
constexpr const char* kSite = "https://cedarlogic.netlify.app/";

// See above. `today` is "YYYY-MM-DD"; `shown` is what remember() keeps.
bool pick(const std::string& json, const std::string& today, const std::string& platform, const std::string& shown, Message& out);

// `shown` with message `id` answered: laterDays 0 for done with it, else waiting till today + laterDays.
std::string remember(const std::string& shown, const std::string& id, const std::string& today, int laterDays);

// The url the button opens: the message's, with ?from=<platform>&v=<version> (an empty or odd
// version is left out).
std::string link(const Message& m, const std::string& platform, const std::string& version);

// "YYYY-MM-DD" plus `days` (the proleptic Gregorian calendar); "" for a date that isn't one.
std::string addDays(const std::string& date, int days);

}  // namespace clmessage
