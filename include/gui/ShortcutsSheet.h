/*****************************************************************************
   Project: CEDAR Logic Simulator
   ShortcutsSheet: Help > Keyboard Shortcuts (and the ? key).

   Every shortcut in the app, grouped, drawn as keys, searchable, and
   scrolling -- the list outgrew a plain grid, which ran off the bottom of
   the screen. A shortcut that is also a menu command can be clicked (or
   picked with the arrows and Return) to do it on the spot.
*****************************************************************************/

#ifndef SHORTCUTSSHEET_H_
#define SHORTCUTSSHEET_H_

class MainFrame;

void ShowShortcutsSheet(MainFrame* frame);

// Test hook (headless --render-ui): the list at w x h, filtered by `query`,
// written to a PNG.
class wxString;
bool RenderShortcutsSnapshot(MainFrame* frame, const wxString& path, int w, int h,
                             const wxString& query);

#endif
