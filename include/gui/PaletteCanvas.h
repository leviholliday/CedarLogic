/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   PaletteCanvas: Renders the gateImage objects in a palette
*****************************************************************************/

#ifndef PALETTECANVAS_H
#define PALETTECANVAS_H

#include "MainApp.h"
#include "gateImage.h"
#include <vector>

using namespace std;

class PaletteCanvas : public wxScrolledWindow {
public:
	PaletteCanvas( wxWindow *parent, wxWindowID, wxString &libName, const wxPoint &pos, const wxSize &size );
virtual	~PaletteCanvas();

    void OnPaint( wxPaintEvent &event );
    void OnSize( wxSizeEvent &event );
	void Activate( void );
	// Re-theme this section's panel background and every already-built tile in
	// it (built lazily on first view, see OnPaint's `init` flag -- an
	// unbuilt/never-viewed section needs no call here, its tiles pick up the
	// current theme when they're first constructed).
	void ApplyTheme();
	// Re-column and re-size the tiles for the current width and the
	// paletteGateSize setting. Called on resize and when that setting changes.
	void UpdateTileLayout();
	
private:
	wxGridSizer* gateSizer;
	int tileSide;   // current tile edge in logical px; see OnSize
	vector< gateImage* > gates;
	string libraryName;
	bool init;
	bool activate;
	
	DECLARE_EVENT_TABLE()
};

#endif /*PALETTECANVAS_H*/
