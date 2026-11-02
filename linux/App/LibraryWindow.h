// Your Circuits and Version History, as the Mac app's LibraryView (and the
// Windows app's): Your Circuits lists every circuit, newest first, to open,
// rename, delete or import into; Version History shows a circuit's versions
// with a picture of each, to restore or save a copy of.

#ifndef CL_LINUX_LIBRARYWINDOW_H
#define CL_LINUX_LIBRARYWINDOW_H

class CircuitWindow;

void showYourCircuits(CircuitWindow* from);
void showVersionHistory(CircuitWindow* window);

#endif  // CL_LINUX_LIBRARYWINDOW_H
