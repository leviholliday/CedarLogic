// Your Circuits (Ctrl+O) and Version History, drawn as the Mac app's
// LibraryView: a heading, a quiet line under it, a search field, and a list
// of tall rows each with a gate tile, a name and a second line, then the
// buttons. Version History puts a picture of the chosen version beside it.

#ifndef CL_WINDOWS_LIBRARYWINDOW_H
#define CL_WINDOWS_LIBRARYWINDOW_H

#include "App.h"

class CircuitWindow;

void showYourCircuits(CircuitWindow* from);
void showVersionHistory(CircuitWindow* window);

#endif  // CL_WINDOWS_LIBRARYWINDOW_H
