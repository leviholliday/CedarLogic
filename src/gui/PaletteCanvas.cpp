/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   PaletteCanvas: Renders the gateImage objects in a palette
*****************************************************************************/

#include "PaletteCanvas.h"
#include "GateLibrary.h"
#include "logic_values.h"
#include "RenderMode.h"
#include <wx/settings.h>
#ifdef __APPLE__
#include "MacAppearance.h"
#endif

using namespace std;

DECLARE_APP(MainApp)


BEGIN_EVENT_TABLE(PaletteCanvas, wxScrolledWindow)
  EVT_PAINT(PaletteCanvas::OnPaint)
  EVT_SIZE(PaletteCanvas::OnSize)
END_EVENT_TABLE()

// How many tiles fit across `width` pixels of client area, at least one.
static int gateSizeSetting() {
	return wxMax(32, wxMin(128, appConfig().appSettings.paletteGateSize));
}

static int columnsForWidth( int width ) {
	int cols = width / gateSizeSetting();
	return cols > 0 ? cols : 1;
}


PaletteCanvas::PaletteCanvas( wxWindow *parent, wxWindowID id, wxString &libName, const wxPoint &pos, const wxSize &size )
	: wxScrolledWindow( parent, id, pos, size, wxSUNKEN_BORDER|wxVSCROLL|wxFULL_REPAINT_ON_RESIZE ) {
    SetBackgroundColour(renderMode().darkMode ? wxColour(19, 21, 25) : *wxWHITE);
    SetCursor(wxCursor(wxCURSOR_ARROW));

#ifdef __APPLE__
	// The palette's native scrollbar is an NSScroller, which follows whatever
	// appearance this view has. Pin it explicitly to match the panel's OWN
	// background (light or dark, per the theme right now) rather than letting
	// it inherit the app-wide appearance -- those can briefly disagree (e.g. a
	// tile still mid-redraw right after a toggle), and a mismatched scroller
	// knob is a light-on-light or dark-on-dark invisible one either way.
	MacSetViewAppearance(GetHandle(), renderMode().darkMode ? 2 : 1);
#endif

#ifdef __WXMSW__
	// On Windows the native vertical scrollbar is carved out of the client area,
	// so without room set aside it lands on top of the last column of gates.
	// Reserve its width on top of three columns' worth, which is the narrowest
	// the palette should ever get. (macOS overlay scrollbars take no layout
	// space, so this is Windows-only.)
	int sbWidth = wxSystemSettings::GetMetric( wxSYS_VSCROLL_X );
	if ( sbWidth <= 0 ) sbWidth = 17;
	const int contentW = 3 * ( IMAGESIZE + 2 ) + 2;  // 3 bordered columns + row border
	SetMinClientSize( wxSize( contentW + sbWidth, -1 ) );
#endif

	libraryName = libName.ToStdString();
	gateSizer = NULL;
	tileSide = -1;   // forces the first UpdateTileLayout to size the tiles
	
	init = false;
	activate = true;
}

PaletteCanvas::~PaletteCanvas() {
	for (unsigned int i = 0; i < gates.size(); i++) delete gates[i];
}

void PaletteCanvas::OnPaint( wxPaintEvent &event ) {
	wxPaintDC dc(this);
	if (!init) {
	   	map < string, LibraryGate >::iterator gateWalk = gateLibrary().libraries[libraryName].begin();
		// A grid sizer whose tiles fill their cells. The panel is as wide as the
		// section dropdown above it, which is not a whole number of tiles, and
		// the remainder used to pile up as a bare strip down the right-hand side
		// -- what the scrollbar sat in front of until it hid itself. Spending it
		// on bigger art beats spending it on gaps: the thumbnails are vector
		// drawings, so they gain detail rather than just scale up.
		gateSizer = new wxGridSizer( columnsForWidth( GetClientSize().x ), 0, 0 );
		while (gateWalk != gateLibrary().libraries[libraryName].end()) {
			gateImage* newGate = new gateImage((gateWalk->first), this, wxID_ANY, wxDefaultPosition, wxSize(IMAGESIZE, IMAGESIZE));
			gates.push_back(newGate);
			gateSizer->Add( newGate, wxSizerFlags(1).Expand() );
			gateWalk++;
		}
		// The grid takes only the height its rows need; anything left over collects
		// under the last row instead of being shared out between the rows (which
		// stretches the tiles and pushes their art off centre).
		wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );
		// Centered, not stretched: tiles are exactly the Gate size setting, so
		// the slider scales them smoothly instead of in whole-column jumps.
		outer->Add( gateSizer, wxSizerFlags(0).CenterHorizontal() );
		outer->AddStretchSpacer( 1 );
		this->SetSizer( outer );
		// Fine scroll rate: the scroll unit is the smallest step the wheel and
		// the scrollbar thumb move by. It used to be a whole gate row
		// (IMAGESIZE+1), which made scrolling lurch a row at a time; a small step
		// gives smooth wheel scrolling and a fine-grained drag.
		this->SetScrollRate(0, 16);
		Layout();          // the window's sizer, which now wraps the grid
		init = true;
		UpdateTileLayout();
	}
	if (activate) {
		this->FitInside();
		this->Scroll(0,0); // reset position because the sizer is dumb
		Layout();
		activate = false;
	}
}
// Re-column on resize, and keep the tiles square: the grid divides the width
// between its columns, and each tile's height has to follow that width or the
// cells stop being square and the art letterboxes inside them.
void PaletteCanvas::OnSize( wxSizeEvent &event ) {
	UpdateTileLayout();
	event.Skip();
}

void PaletteCanvas::UpdateTileLayout() {
	if ( gateSizer == NULL || gates.empty() ) return;
	const int width = GetClientSize().x;
	const int cols = columnsForWidth( width );
	const int side = gateSizeSetting();
	if ( cols != gateSizer->GetCols() || side != tileSide ) {
		tileSide = side;
		gateSizer->SetCols( cols );
		for ( unsigned int i = 0; i < gates.size(); i++ )
			gates[i]->SetMinSize( wxSize( side, side ) );
		Layout();
		FitInside();
	}
}

void PaletteCanvas::Activate() {
	activate = true;
}

void PaletteCanvas::ApplyTheme() {
	SetBackgroundColour(renderMode().darkMode ? wxColour(19, 21, 25) : *wxWHITE);
#ifdef __APPLE__
	MacSetViewAppearance(GetHandle(), renderMode().darkMode ? 2 : 1);
#endif
	for (unsigned int i = 0; i < gates.size(); i++) gates[i]->ApplyTheme();
	Refresh();
}
