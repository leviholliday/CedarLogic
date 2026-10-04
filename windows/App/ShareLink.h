// File > Share Link…, and cedarlogic:// links coming in (the format is
// ShareCodec.h's).
//
// Sharing puts the website's address for the circuit on the clipboard
// (CedarLogic Online opens it, and offers the app). Going the other way, a
// cedarlogic://open#c=… link -- the website's Open in the App, a link in a
// chat -- reaches the app as an argument, as a file does: its circuit is
// written to a file in the cache (%LOCALAPPDATA%\CedarLogic\links), which
// then opens as any file handed over does -- in the CedarLogic already
// running, if there is one -- coming in as a new circuit in Your Circuits, as
// File > Import does. The scheme is registered for this user by the installer
// and by Integration.cpp.

#ifndef CL_WINDOWS_SHARE_LINK_H
#define CL_WINDOWS_SHARE_LINK_H

#include "App.h"
#include "ShareCodec.h"
#include <string>
#include <vector>

class CircuitWindow;

namespace sharelink {

// File > Share Link…: the link for a circuit with this .cdl text and name on
// the clipboard, with a note in `window`; too long for a link says so.
void copyLink(CircuitWindow* window, const std::string& text, const std::string& name);

// A cedarlogic: link (an argument that is one, not a file).
bool isLink(const std::string& arg);
// A link with no circuit in it (the website's Open in the App on an empty
// board: cedarlogic://open): it only asks for the app, which comes forward.
bool isBareLink(const std::string& link);
// The link's circuit as a .cdl file in the cache, named for the circuit and in
// a folder of its own for this data (so the same link opens the same copy);
// the file's path, or empty and why (in a few words, for problem()).
std::string fileForLink(const std::string& link, std::string& why);
// A file fileForLink made (not one of the person's: where Import should not
// remember having looked).
bool isCacheFile(const std::string& path);
// The card that says a link couldn't be opened.
void showProblem(HWND parent, const std::string& why);

// --share-test [circuit.cdl [where to write that circuit's link [a link another
// program made of it]]] (CI): the format's checks (ShareCodec.h) and the
// cache's. A PASS or FAIL line each in `report`; false if any failed.
bool runSelfTest(std::string& report, const std::vector<std::string>& paths);

}  // namespace sharelink

#endif  // CL_WINDOWS_SHARE_LINK_H
