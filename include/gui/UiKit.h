/*****************************************************************************
   Project: CEDAR Logic Simulator
   UiKit: the few drawing pieces the hand-drawn windows share -- the welcome,
   the guided tour and the keyboard shortcuts sheet -- so they look like one
   app: its paper and ink for the current theme, the accent, a rounded
   button, and a key drawn as a key.
*****************************************************************************/

#ifndef UIKIT_H_
#define UIKIT_H_

#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <vector>

class wxGraphicsContext;

namespace ui {

bool isDark();
wxColour withAlpha(const wxColour& c, double alpha);
wxColour accent();                  // the user's accent, for the current theme
wxColour accentFor(int index);      // one of the Preferences choices
wxColour paper();                   // window background
wxColour ink();                     // primary text
wxColour dim();                     // secondary text
wxColour success();                 // the green of "done"

// A pill-shaped button. `filled` is the primary (accent) kind.
void drawButton(wxGraphicsContext* gc, const wxRect& r, const wxString& label,
                bool filled, bool hot);

// One key cap: a raised rounded box with the key's name on it. Returns its
// width, so a row of them can be laid out; `measureOnly` just measures.
double drawKeyCap(wxGraphicsContext* gc, double x, double y, double h,
                  const wxString& text, bool measureOnly = false);

// Prose that names Mac keys ("Cmd+S", "Option", "press Return") in the
// platform's words: unchanged on a Mac, "Ctrl+S", "Alt", "press Enter" elsewhere.
wxString platformKeys(const wxString& text);

// A shortcut such as {"Cmd", "Shift", "T"} as a row of caps, in the
// platform's own spelling (⌘ ⇧ ⌥ ⌃ on a Mac). Returns the total width.
double drawKeys(wxGraphicsContext* gc, double x, double y, double h,
                const std::vector<wxString>& keys, bool measureOnly = false);

// The name a key token is shown by on this platform ("Cmd" -> "⌘").
wxString keyGlyph(const wxString& token);

// Wrap `text` to `width` using the context's current font. Returns the lines.
std::vector<wxString> wrap(wxGraphicsContext* gc, const wxString& text, double width);

}  // namespace ui

#endif
