/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   PaletteFrame: Organizes PaletteCanvas objects
*****************************************************************************/

#ifndef PALETTEFRAME_H_
#define PALETTEFRAME_H_

#include "MainApp.h"
#include "PaletteCanvas.h"
#include "gateImage.h"
#include <vector>

using namespace std;

#define ID_LISTBOX 6666

class PaletteFrame : public wxPanel {
public:
	PaletteFrame( wxWindow *parent, wxWindowID, const wxPoint &pos, const wxSize &size );
	virtual ~PaletteFrame();

	void OnListSelect( wxCommandEvent& evt );
	void OnPaint( wxPaintEvent& evt );
	// Re-theme every section's panel, visible or not (all of them are built up
	// front; see the ctor).
	void ApplyTheme();
	// Jump straight to the idx-th section (0-based, matching the dropdown's
	// order) -- for the Shift+1..9 shortcut in MainFrame. No-op if idx is out
	// of range (fewer than idx+1 sections in the library).
	void SelectSectionByIndex(unsigned int idx);
	// Apply a new paletteGateSize to every section.
	void ApplyGateSize();

private:
	wxBoxSizer* paletteSizer;
	wxChoice* sectionChoice;
	wxArrayString strings;
	PaletteCanvas* currentPalette;
	map < wxString, PaletteCanvas* > pcanvases;
	DECLARE_EVENT_TABLE()
};

#endif /*PALETTEFRAME_H_*/
