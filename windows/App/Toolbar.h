// The window's title bar and toolbar, in two rows.
//
// The top row is a Windows 11 title bar with the menu bar in it: the app's
// icon, File, Edit, Circuit, View, Simulate, Window and Help (the window's
// menus, CircuitWindow::buildMenus), and the window's own minimize, maximize
// and close buttons at the right. Each menu is a real Win32 popup; the bar
// behaves as Windows' own: a click opens a menu, pointing at another while
// one is open moves to it, Left and Right step along, Alt or F10 puts the
// bar under the keys (Alt+letter opens a menu), Escape steps back.
//
// The row under it is the toolbar, drawn as the Mac app's (CLToolbar) in its
// three styles: Seamless (the default: one surface with the canvas, tools
// quiet until you point at them), Classic (tools in soft capsules on their
// own bar) and Minimal (the essentials and the circuit's name). The
// circuit's name with its menu, the tools, the zoom and the simulation speed,
// and a gear for Settings (also File > Settings, Ctrl+,). Settings > Toolbar
// picks the style and hides groups of tools; everything is in the menus too.
//
// Where nothing is under the pointer, both rows are the window's title bar:
// they drag the window, double-clicks maximize, and right-clicks bring up the
// window menu. The maximize button answers as Windows' own (HTMAXBUTTON), so
// Windows 11 shows its snap layouts over it.

#ifndef CL_WINDOWS_TOOLBAR_H
#define CL_WINDOWS_TOOLBAR_H

#include "Drawn.h"
#include <string>
#include <vector>

class CircuitWindow;
struct IWICBitmap;

// Tool groups that can be hidden, one bit each in prefs().toolbarHidden (the
// Mac's ToolGroup).
enum ToolGroup { TGFile, TGUndo, TGClipboard, TGZoom, TGSim, TGRun, TGLock, TGTheme, TGTab, TGFeedback, TGDraw, TGCount };
const char* toolGroupName(int g);
// The styles (prefs().toolbarStyle), as the Mac numbers them.
enum ToolbarStyle { TSClassic = 0, TSMinimal = 2, TSSeamless = 3 };

class Toolbar : public Drawn {
public:
	Toolbar(CircuitWindow* window, HWND parent);
	~Toolbar() override;
	// In points: the title row with the menus, the tools' row, and both.
	static float menuHeight() { return 32; }
	static float toolsHeight() { return 48; }
	static float barHeight() { return menuHeight() + toolsHeight(); }

	// For the frame: where the maximize button is (window client pixels;
	// empty when hidden), and whether the pointer is over it.
	RECT maximizeRect() const;
	void setMaximizeHot(bool hot, bool pressed);
	void layoutNow() { relayout = true; redraw(); }
	// Where a command's button is, on the screen (the guided tour points at it).
	RECT commandRect(int command) const;
	// For --click-test: the middle of a button, in the bar's pixels -- a
	// command's, or kMinimize or kClose for the window's own. False when
	// it isn't shown (the bar left it out at this width).
	static const int kMinimize = -1, kClose = -2;
	bool buttonPoint(int command, POINT& p);
	// Whether the style in use has that button at all (Minimal leaves most
	// to the menus), shown at this width or not.
	bool hasButton(int command);
	// For Settings' pictures of each style: drawn in `style`, w x h points,
	// without the window's buttons and not clickable.
	void paintPicture(ID2D1RenderTarget* rt, float w, float h, int style);

	// The menu bar. A menu (by its place in the bar) opened under its title,
	// by the pointer or the keys (then its first item is picked). Returns
	// once the menus are closed; a command chosen is posted to the window.
	void openMenu(int index, bool keyboard);
	// Alt+letter: the menu with that letter, opened. False if none has it.
	bool openMnemonic(wchar_t letter);
	// Alt or F10 alone: the bar takes the keys, File picked (in focus mode,
	// where the bar has slid away, File opens). Again, or Escape: let go.
	void startMenuKeys();
	void endMenuKeys();
	bool menuKeysActive() const { return keyMenu >= 0 || tracking; }
	int openMenuIndex() const { return openIndex; }   // for --menu-test
	int keyMenuIndex() const { return keyMenu; }
	// WM_MENUSELECT, from the window (which owns the popups): what's picked,
	// so Left and Right know whether they're in a submenu.
	void menuSelect(HMENU menu, UINT flags);
	// For --menu: where a menu's title is, on the screen.
	RECT menuTitleRect(int index);
	// For the menus' hook (Toolbar.cpp): a message on its way to an open menu.
	bool menuMessage(const MSG& m);

protected:
	void paint(ID2D1RenderTarget* rt, float w, float h) override;
	void mouseMove(float x, float y) override;
	void mouseLeave() override;
	void mouseDown(int button, float x, float y, bool doubleClick) override;
	void mouseUp(int button, float x, float y) override;
	void captureLost() override;
	LRESULT hitTest(float x, float y) override;
	LRESULT message(UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;

private:
	enum Kind { Button, Zoom, Speed, Title, CaptionMin, CaptionMax, CaptionClose, MenuTitle, AppIcon };
	struct Item {
		Kind kind;
		int command;           // for a Button
		int group;             // its ToolGroup: hidden together, a capsule in Classic (-1: always there)
		bool right;            // laid out from the right edge
		D2D1_RECT_F rect{};
		bool shown = true;
	};

	CircuitWindow* win;
	std::vector<Item> items;
	int hot = -1, pressed = -1;
	bool maxHot = false, maxPressed = false;
	bool draggingSpeed = false;
	bool relayout = true;
	float lastW = 0;
	int laidStyle = -1;
	std::string laidTitle;   // the name (and Minimal's page) it was laid out for
	bool laidClock = false;  // whether the page had a Step Clock clock then (the button's tip says)

	// The menu bar's state: the menu open (by its place), the one the keys
	// are on, whether the menus are up, and what the hook needs.
	int openIndex = -1, keyMenu = -1;
	bool tracking = false;
	int nextMenu = -1;            // the menu to open when this one has closed
	bool nextKeyboard = false;    //   with its first item picked
	bool escaped = false;         // closed by Escape: back to the bar's keys
	bool swallowKeyUp = false;    // the Alt or F10 that let go of the bar: no menu on its release
	HMENU trackTop = nullptr, selMenu = nullptr;
	bool selPopup = false;        // what's picked opens a submenu
	POINT lastHookPoint{ -1, -1 };
	HWND keysFrom = nullptr;      // what had the keys before the bar took them
	IWICBitmap* icon = nullptr;   // the app's icon, for the title row
	std::wstring menuLabel(int index) const;   // with its &
	int menuCount() const;
	int menuItemAt(POINT screen) const;   // the menu title there (-1: none)
	bool cuesShown() const;       // the menus' letters underlined (Alt held, or the bar has the keys)

	void build(int style, bool live);
	void layout(float w, float h, int style, bool live = true);
	void draw(ID2D1RenderTarget* rt, float w, float h, int style, bool live);
	int itemAt(float x, float y) const;
	wchar_t iconFor(const Item& it) const;
	std::string tipFor(const Item& it) const;
	bool isOn(const Item& it) const;
	bool isEnabled(const Item& it) const;
	D2D1_RECT_F speedTrack(const Item& it) const;
	void setSpeedAt(const Item& it, float x);
	void activate(int index);
};

#endif  // CL_WINDOWS_TOOLBAR_H
