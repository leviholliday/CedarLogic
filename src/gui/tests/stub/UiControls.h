// Stands in for include/gui/UiControls.h when building the gate-library test.
//
// LibraryParse shows its one message box through ui::Message, which the real
// header builds out of wxWidgets controls. stub/ comes first on the include
// path, so this wins and the test still builds with no wxWidgets present (see
// wx/msgdlg.h beside it).
#ifndef UICONTROLS_H_
#define UICONTROLS_H_

#include "wx/msgdlg.h"

namespace ui {
inline int Message(const char*, const char* = "Message", long = 0, void* = 0) {
	return 0;
}
}  // namespace ui

#endif
