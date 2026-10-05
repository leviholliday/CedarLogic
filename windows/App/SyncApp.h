// Your Circuits on every device, the app's side (docs/SYNC.md): the shared
// engine (mac/CedarCore/Sync.h) running beside the windows, the questions it
// asks, Settings > Sync, the code and link sheets, cedarlogic://sync links,
// and the line in Your Circuits. What Windows gives the engine -- the
// cryptography, HTTPS, the code at rest -- is SyncPlatform.h.
//
// The engine runs on a thread of its own; everything that touches the
// circuits, a window or the screen runs on the UI thread, which it reaches by
// posting a message to a hidden window and waiting (onMain), so a sync never
// races the app's own autosave. A remote change to a circuit that's open and
// has no unsaved work reloads it in place; one with unsaved work is held
// until it is saved (the engine checks, windowState).

#ifndef CL_WINDOWS_SYNC_APP_H
#define CL_WINDOWS_SYNC_APP_H

#include "App.h"
#include "Sync.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

class Form;
class CircuitWindow;

namespace syncapp {

// ---- Life ----------------------------------------------------------------------

// Makes the engine, the hidden window it posts to and the status it keeps. Not
// for the runs that make pictures or reports (they sync nothing).
void init();
// Syncs, if a code is stored. Once the first window is up.
void start();
// Stops the thread and lets go of the lock (after the message loop).
void shutdown();
// A sample state for the pictures (--sync-demo): on, with a few devices and a
// code, and no engine. (--sync-demo off shows the off page.)
void demo(bool on);
bool isDemo();

// The window a question goes over: the one in front, over whatever is open on it.
HWND frontWindow();

// The engine (null in the runs that make pictures).
clsync::Engine* engine();

// ---- What the screens show --------------------------------------------------------

bool enabled();
clsync::Status status();
// "Synced just now · 42 circuits", "Syncing…": the sentence for the sync line.
std::string statusLine(const clsync::Status& s);
std::string code();                      // 28 symbols, "" when off
std::string deviceName();
void setDeviceName(const std::string& name);
// The name and the time each device last synced (this one included), newest first.
std::vector<std::pair<std::string, int64_t>> devices();
// The engine's note of something that happened, and when (ms since 1970); empty after a minute.
std::string recentNotice();

// Called on the UI thread when the status changes, or when circuits arrived,
// were renamed or went (the lists should be read again).
enum class Event { Status, Library };
int listen(std::function<void(Event)> fn);
void unlisten(int id);

// Something to do on the UI thread a moment from now, outside whatever is
// running (a question that waits for the person must not hold the engine).
void defer(std::function<void()> task);
// The screens that show the state read it again.
void stateChanged();

// ---- Triggers (from the windows and the library) ---------------------------------

void appActivated(bool active);          // WM_ACTIVATEAPP
void userActive();                       // a key or a click in one of the windows
void libraryChanged();                   // a circuit was saved, renamed, made, deleted or restored
void syncNow();
// The last window closed: sends what's left, for up to 5 seconds, while the
// messages are pumped. Then the app can end.
void quitting();

// ---- cedarlogic://sync links -------------------------------------------------------

// cedarlogic://sync#k=... (or ?k=...).
bool isSyncLink(const std::string& arg);
// Opens the link: checks the code, shows what it holds, and links this PC
// only when the person presses Link.
void openLink(HWND parent, const std::string& link);

// ---- Sheets -----------------------------------------------------------------------

void showCode(HWND parent, bool afterStartOver = false);   // the code, its QR and how to use it
void turnOn(HWND parent);                                  // a new code, then its sheet
// "I Have a Code": the code (or a link) typed or pasted, what it holds, Link.
// `prefill` fills the box (and with `run`, goes on at once). True if linked.
bool haveCode(HWND parent, const std::string& prefill = std::string(), bool run = false);
void turnOff(HWND parent);
void deleteCopy(HWND parent);
void startOver(HWND parent);
void howItWorks(HWND parent);
// The questions the engine asks (shown over the front window).
void askMassDelete(int count, std::function<void(bool)> answer);
void askIncomingDeletes(int count, const std::string& from, std::function<void(bool)> answer);

// --dialog sync-code, sync-link: the sheets as they look, for the pictures.
void showCodeSample(HWND parent);
void showLinkSample(HWND parent);

// ---- Settings > Sync ----------------------------------------------------------------

// The page's fields in a Form (Settings.cpp adds it as its last page).
class SettingsPage {
public:
	~SettingsPage() { detach(); }
	void add(Form& f);                       // the fields, on the page f.adding names
	void init(Form& f);                      // from onInit: it listens for the status
	bool handles(int field) const;
	void changed(Form& f, int field);        // from onChange
	void left(Form& f, int field);           // from onLeave
	void tick(Form& f);                      // from onTimer (the minutes since the last sync change)
	void detach();                           // once the dialog is closed

private:
	struct Fields {
		int turnOn = -1, haveCode = -1, how = -1;                                   // off
		int syncNow = -1, name = -1, devices = -1, showCode = -1, turnOff = -1,
		    startOver = -1, deleteCopy = -1, how2 = -1;                             // on
	} f;
	Form* form = nullptr;
	int listener = 0;
	bool wasOn = false;
	void refresh();
	void show(bool on);
};

}  // namespace syncapp

#endif  // CL_WINDOWS_SYNC_APP_H
