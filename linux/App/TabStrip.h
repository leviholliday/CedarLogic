// A side's tabs, drawn as the Mac app's (CLTabStrip): a glassy strip with
// the tab you're in as a card of the canvas's own colour and an accent dot,
// a close cross on the active and hovered tab, and + at the end. Drag a tab
// along the strip to reorder it (the tabs it passes slide aside, an outline
// marks where it'll land); down over the canvas to split the view; over the
// other side of a split to move it there. Double-click a tab to rename it in
// place, the empty strip for a new tab; middle-click closes. Each side of a
// split has its own strip: the side you're in has an accent rail under it,
// the other steps back. In focus mode the strips are the window's top row.

#ifndef CL_LINUX_TABSTRIP_H
#define CL_LINUX_TABSTRIP_H

#include "Anim.h"
#include "Drawn.h"
#include "TitleButtons.h"
#include <cstdint>
#include <map>
#include <vector>

class CircuitWindow;

class TabStrip : public Drawn {
public:
	TabStrip(CircuitWindow* window, int pane);
	static float stripHeight() { return 36; }   // points
	// The strip with its rename field over it.
	GtkWidget* outer() const { return overlay; }
	void setTitleRow(bool on);
	void beginRename(int page);
	// End a rename in progress: the typed name, if `keep`, or the old one.
	void commitRename(bool keep);

protected:
	void paint(cairo_t* cr, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick, GdkEventButton* e) override;
	void mouseUp(int button, float x, float y) override;
	bool animating() override;
	void sizeChanged() override { buttons.load(); }

private:
	CircuitWindow* win;
	int pane;
	GtkWidget* overlay = nullptr;
	GtkWidget* entry = nullptr;
	uint64_t renamingKey = 0;
	bool titleRow = false;
	TitleButtons buttons;
	// This frame's layout.
	std::vector<int> pages;
	float tw = 160, leftInset = 8, rightInset = 0;
	// Where each tab is drawn, easing to its place (by page key).
	std::map<uint64_t, anim::Spring> xs;
	anim::HoverFade hover;        // tabs by place, the plus as kPlus
	anim::HoverFade closeHover;
	anim::HoverFade card;         // the active card, crossfading as it moves
	anim::Tween stepBack;         // the other side of a split, dimmed
	// A tab held.
	uint64_t heldKey = 0;
	float pressX = 0, pressY = 0, dragDX = 0;
	bool moving = false;
	int hotClose = -1;
	static constexpr int kPlus = 10000;

	void layout(float w);
	float xAt(int k) const { return leftInset + k * (tw + 2); }
	int heldIndex() const;
	int dropSlot() const;
	float makeRoom(int k) const;
	int tabAt(float x, float y) const;
	bool onClose(int k, float x) const;
	bool activeSide() const;
	void paintClassic(cairo_t* cr, float w, float h);
	std::vector<RectF> classicSegments(float w) const;
};

#endif  // CL_LINUX_TABSTRIP_H
