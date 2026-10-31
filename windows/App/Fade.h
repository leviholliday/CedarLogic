// Short fades, as the Mac's SwiftUI transitions (.opacity, about 0.15 to
// 0.2 s, ease out), for the parts of the app made of ordinary windows that
// can't fade themselves: Settings opening and turning its pages, focus mode,
// Simulation View, the side panel showing or hiding and changing category.
//
// A cross-fade works by picture: what's on the screen over a window (or a
// part of it) is copied into a layered window laid exactly on top of it,
// the change is made underneath, and the picture fades away -- so the old
// look dissolves into the new without anything under it having to know.
// The picture lets the pointer through and never takes the keyboard.
//
// With Windows' "Animation effects" off (SPI_GETCLIENTAREAANIMATION) none
// of this happens: changes are immediate, as before.

#ifndef CL_WINDOWS_FADE_H
#define CL_WINDOWS_FADE_H

#include "App.h"

namespace fade {

// Windows' animation effects are on.
bool animations();

// Lays a picture of `area` (in `w`'s client pixels; null: all of its client
// area) over it, and fades it out over `ms`. Call it just before the
// change. Returns the picture's window (null when there's no fade: the
// animations are off, or `w` isn't on the screen), for clipTo.
HWND crossfade(HWND w, const RECT* area = nullptr, int ms = 170);

// The window under a cross-fade changed size: the picture is cut to `w`'s
// client area now, so none of it hangs outside a window that shrank.
void clipTo(HWND picture, HWND w);

// A top-level window coming up fades in from clear over `ms`. Call it before
// it shows (a dialog's WM_INITDIALOG); it's layered only for the fade.
void fadeIn(HWND top, int ms = 160);

// A layered window drawn with UpdateLayeredWindow fades from `alpha` to
// clear over `ms`, then is destroyed. Its window procedure must be done with
// it already (nothing of the owner's is called again).
void fadeOutAndDestroy(HWND layered, BYTE alpha, int ms = 110);

}  // namespace fade

#endif  // CL_WINDOWS_FADE_H
