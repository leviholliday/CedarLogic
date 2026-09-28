/*****************************************************************************
   Project: CEDAR Logic Simulator
   StatusStrip: the status bar along the bottom of the window, drawn by us.

   Windows' own status bar is a white strip with sunken borders that ignores
   the app's theme. This is the same control -- same fields, same
   SetStatusText -- with the painting taken over: the app's colours, a
   hairline on top, quiet text, no borders and no grip.
*****************************************************************************/

#ifndef STATUSSTRIP_H_
#define STATUSSTRIP_H_

#include <wx/statusbr.h>

class StatusStrip : public wxStatusBar {
public:
	StatusStrip(wxWindow* parent, wxWindowID id, long style, const wxString& name);

private:
	void OnPaint(wxPaintEvent&);
};

#endif
