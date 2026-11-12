// CedarMessage -- a message from CedarLogic, shown until it's answered (the first: a year after
// 0.4, a thank-you with a link to a short form). The apps read message.json from the releases repo
// with their daily update check (only while update checks are on, and only when that check found no
// update) and ask this which message, if any, is due. Plain C, so the Mac app sees it through
// CedarCore.h; the rules are AppMessage.h's.

#ifndef CEDARMESSAGE_H
#define CEDARMESSAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where the file is: next to the update feeds, in the public releases repo.
#define CL_MESSAGE_URL "https://raw.githubusercontent.com/leviholliday/CedarLogic-Releases/main/message.json"

// A message that's due: malloc'd UTF-8, freed together by cl_message_free.
typedef struct CLAppMessage {
	char *id;
	char *eyebrow;  // the small line over the title ("" for none)
	char *title;
	char *text;
	char *note;     // a faint line by the buttons ("" for none)
	char *button;   // the button that opens `link`
	char *link;     // https://cedarlogic.netlify.app/... with ?from=<platform>&v=<version>
	int later;      // Remind Me Later: shown again this many days on
} CLAppMessage;

// The first message in `json` (the file's bytes) that's due on `today` (the computer's own date,
// "YYYY-MM-DD"), is for `platform` ("mac", "windows" or "linux"), and that `shown` (what
// cl_message_remember keeps; NULL or "" for nothing yet) doesn't hold back. NULL when none is,
// or the file isn't right.
CLAppMessage *cl_message_pick(const char *json, size_t length, const char *today, const char *platform, const char *version,
                              const char *shown);
void cl_message_free(CLAppMessage *m);

// `shown` with the answer to message `id` kept: laterDays 0 for done with it (the form, or No
// Thanks), else not before `today` + laterDays (Remind Me Later). malloc'd: free() it.
char *cl_message_remember(const char *shown, const char *id, const char *today, int laterDays);

#ifdef __cplusplus
}
#endif

#endif
