/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   gateImage: Generates a bitmap for a gate in the library, used in palette
*****************************************************************************/

#ifndef GATEIMAGE_H_
#define GATEIMAGE_H_

class gateImage;

#include "MainApp.h"
#include "wx/glcanvas.h"
#include "guiGate.h"
#include "logic_values.h"
#include "wx/generic/dragimgg.h"
#include "GUICircuit.h"
#define wxDragImage wxGenericDragImage
#include <string>

// Engine-neutral rendering seam (Workstream G); defined in gui/render/.
namespace cl { namespace render { class Scene; struct RenderStyle; struct Transform; } }

#define GATEIMAGESIZE 46
#define IMAGESIZE 48

using namespace std;

class gateImage : public wxWindow {
public:
    gateImage( string gateName, wxWindow *parent, wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0, const wxString& name = "" );

    virtual ~gateImage();

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    // Skia thumbnail render (replaces the offscreen-GL readback); returns false
    // if the raster surface could not be produced.
    bool generateImageSkia();
    cl::render::Transform thumbnailTransform(guiGate* gate, int size) const;
    void mouseCallback(wxMouseEvent& event);
    void OnEnterWindow(wxMouseEvent& event) { if (!(event.LeftIsDown())) inImage = true; Refresh(); };
    void OnLeaveWindow(wxMouseEvent& event) { inImage = false; Refresh(); };

	void OnEraseBackground( wxEraseEvent& event );

	string getGateName() { return gateName; };

	// Re-theme this tile: background colour plus a forced re-render of the
	// thumbnail (RenderStyle::thumbnail() bakes the theme into the bitmap, so a
	// toggle can't just recolor pixels -- it has to redraw). Called by
	// PaletteCanvas::ApplyTheme for every already-constructed tile; a tile
	// created after the toggle picks up the current theme at construction and
	// needs no call here.
	void ApplyTheme();

private:
	void update();
	
	string gateName;
	bool inImage;
	wxImage gImage;
	wxBitmap gBitmap;   // gImage at the display's scale factor
	int renderedPx;     // device-pixel size gBitmap was drawn at
	bool renderedDark = false;   // theme gBitmap was drawn in
	
	wxDragImage* m_dragImage;
	bool m_init;
	GUICircuit* tempcir;
	
	DECLARE_EVENT_TABLE()
};

#endif /*GATEIMAGE_H_*/
