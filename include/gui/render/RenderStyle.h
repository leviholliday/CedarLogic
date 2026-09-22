// RenderStyle -- how a target paints the semantic scene (Workstream G).
//
// The Scene stores *what a thing is* (a wire and its logic state, a gate of a
// kind, a label), never a baked color. RenderStyle turns that semantics into
// paint, per output. Two styles, one scene:
//
//   screen(): color-by-state, live -- net colors, LED/7-seg hues, selection and
//             hover overlays, the grid. Built for a backlit display.
//   print():  white ground, solid black strokes at legible weight; no grid, no
//             selection; hierarchy by line weight (buses heavier than nets), not
//             hue; live signal state never reaches paper -- a printout is the
//             static schematic (topology), not a snapshot of the running sim.
//             A KiCad-style title block frames the page.
//
// This split is why unifying screen and export is not a recolored screenshot:
// export has historically printed poorly in black-and-white because meaning was
// encoded in color, which collapses to indistinguishable grey on paper. Here the
// print style is a first-class rendering intent.
//
// Header-only C++11; zero impact on the default build until draw sites consult a
// style (phase G1/G4).

#ifndef CL_RENDER_RENDERSTYLE_H
#define CL_RENDER_RENDERSTYLE_H

#include <string>

#include "Scene.h"

namespace cl {
namespace render {

// Logic value, carried semantically so the *style* -- not the draw site --
// picks the paint.
enum class WireState { Low, High, HiZ, Unknown, Conflict };

enum class GateKind { Generic, Input, Output, Junction, Label };

// KiCad-style page frame: title, sheet, revision, date, author, sheet N of M.
struct TitleBlock {
	bool enabled;
	std::string title;
	std::string sheet;
	std::string revision;
	std::string date;
	std::string author;
	int sheetNumber;
	int sheetCount;
	TitleBlock()
		: enabled(false), sheetNumber(1), sheetCount(1) {}
};

struct RenderStyle {
	bool showGrid;        // print: false
	bool showSelection;   // print: false (no hover/selection overlays)
	bool showLiveState;   // print: false (no signal colors on paper)
	bool colorOutput;     // print: false (black on white)
	bool darkMode;         // screen only; print/export never sets this (stays false)
	// 0..1 multiplier on the selection halo's alpha (see guiGate/guiWire
	// drawToScene) -- 1 once settled, ramping up from 0 right after a
	// selection change so the halo fades in instead of snapping to full
	// opacity. Screen only; print/export have no selection to begin with.
	// Set by GUICanvas::renderSkiaLive from its selectionChangedAt timestamp.
	float selectionFade;
	// User appearance choices, screen only (print/export keep the defaults):
	// which accent() color, and a multiplier on live wire widths.
	int accentIndex;
	float wireScale;
	TitleBlock titleBlock;

	RenderStyle()
		: showGrid(true), showSelection(true),
		  showLiveState(true), colorOutput(true), darkMode(false), selectionFade(1.0f),
		  accentIndex(0), wireScale(1.0f) {}

	// Paint for a wire of the given state. On screen, color by state and widen
	// buses; on paper, always solid black with weight carrying the bus/net
	// hierarchy and state ignored (topology only).
	Stroke wire(WireState state, bool isBus) const {
		const float netWidth = (isBus ? 3.0f : 1.0f) * wireScale;
		if (!colorOutput || !showLiveState) {
			// Print/thumbnail: topology only, hierarchy by weight. True paper
			// print (colorOutput false) is always black; a themed thumbnail
			// (colorOutput true, e.g. RenderStyle::thumbnail()) follows dark
			// mode the same way gateStroke() does.
			const Color c = !colorOutput ? Color(0, 0, 0, 1)
			              : darkMode     ? Color(0.90f, 0.90f, 0.92f, 1)
			                             : Color(0.05f, 0.05f, 0.05f, 1);
			return Stroke(c, isBus ? 2.4f : 1.4f);
		}
		Color c;
		switch (state) {
			case WireState::High:     c = Color(0.0f, 0.9f, 0.0f); break;  // active
			// Idle green: on a dark background the print-legible dark green
			// (0.35) all but disappears, so brighten it for screen+dark only.
			case WireState::Low:      c = darkMode ? Color(0.0f, 0.6f, 0.0f)
			                                       : Color(0.0f, 0.35f, 0.0f); break;
			// Hi-Z blue: same brightening as Low -- a saturated pure blue reads
			// dim against a near-black background.
			case WireState::HiZ:      c = darkMode ? Color(0.25f, 0.35f, 1.0f)
			                                       : Color(0.0f, 0.0f, 0.9f); break;
			case WireState::Conflict: c = Color(0.9f, 0.0f, 0.0f); break;  // conflict
			case WireState::Unknown:
			default:                  c = darkMode ? Color(0.65f, 0.65f, 0.65f)
			                                       : Color(0.5f, 0.5f, 0.5f); break;
		}
		return Stroke(c, netWidth);
	}

	// Stroke color for a gate body. Black on paper; near-black on a light
	// screen, near-white on a dark screen.
	Color gateStroke(GateKind /*kind*/) const {
		if (!colorOutput) return Color(0, 0, 0, 1);
		return darkMode ? Color(0.90f, 0.90f, 0.92f, 1) : Color(0.05f, 0.05f, 0.05f, 1);
	}

	// Page/canvas background. Print/export always white; the live screen
	// follows the theme. A cool, slightly-blue near-black rather than a flat
	// neutral grey -- the same family of dark as Linear/Figma/VS Code's dark
	// themes, which reads as designed rather than just "inverted white."
	Color background() const {
		if (!colorOutput) return Color(1, 1, 1, 1);
		return darkMode ? Color(0.075f, 0.082f, 0.098f, 1) : Color(1, 1, 1, 1);
	}

	// The one accent color for interactive UI chrome that isn't semantic state
	// (selection/drag-select box, hover fills) -- NOT the wire/LED state colors
	// above, which carry meaning and stay put. A single shared azure instead of
	// each call site inventing its own blue keeps that chrome visually
	// coherent across the canvas.
	// accentIndex picks one of the Preferences choices, each with a lighter
	// variant for dark backgrounds: Blue, Purple, Pink, Orange, Green, Graphite.
	Color accent() const {
		static const float light[6][3] = {
			{0.20f, 0.48f, 0.98f}, {0.55f, 0.32f, 0.93f}, {0.93f, 0.25f, 0.55f},
			{0.96f, 0.50f, 0.10f}, {0.16f, 0.66f, 0.33f}, {0.42f, 0.45f, 0.50f}};
		static const float dark[6][3] = {
			{0.42f, 0.62f, 1.00f}, {0.70f, 0.55f, 1.00f}, {1.00f, 0.48f, 0.70f},
			{1.00f, 0.66f, 0.32f}, {0.36f, 0.82f, 0.50f}, {0.66f, 0.69f, 0.74f}};
		const int i = (accentIndex >= 0 && accentIndex < 6) ? accentIndex : 0;
		const float* c = darkMode ? dark[i] : light[i];
		return Color(c[0], c[1], c[2], 1);
	}

	// Grid line color -- a faint tint of the theme's foreground against its
	// background, so it stays equally subtle in both themes instead of nearly
	// vanishing on dark (a fixed dark-blue-on-white value reads as near-black
	// on dark).
	Color gridColor(float intensity) const {
		return darkMode ? Color(1.0f, 1.0f, 1.0f, intensity)
		                : Color(0.0f, 0.0f, intensity, intensity);
	}

	// The live, color-by-state screen style. `dark` selects the theme for the
	// live canvas only -- print()/export ignore it entirely.
	static RenderStyle screen(bool dark = false) {
		RenderStyle s;  // other defaults are the screen style
		s.darkMode = dark;
		return s;
	}

	// The static, black-and-white, print-legible schematic style. Always
	// light, regardless of the app's on-screen theme -- printouts stay cheap
	// to print and legible in black-and-white.
	static RenderStyle print() {
		RenderStyle s;
		s.showGrid = false;
		s.showSelection = false;
		s.showLiveState = false;
		s.colorOutput = false;
		s.titleBlock.enabled = true;
		return s;
	}

	// Gate-library thumbnails (the palette panel): topology only, like print()
	// -- no live signal colors, so an LED or state-dependent gate always draws
	// the same neutral symbol regardless of whatever state a scratch circuit
	// gave it -- but UNLIKE print(), themed: colorOutput stays on so
	// background()/gateStroke() follow `dark` instead of pinning to
	// black-on-white paper. This is what lets the palette panel go dark with
	// the rest of the app instead of staying a fixed white surface.
	static RenderStyle thumbnail(bool dark) {
		RenderStyle s = print();
		s.colorOutput = true;
		s.darkMode = dark;
		return s;
	}
};

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_RENDERSTYLE_H
