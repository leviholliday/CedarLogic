// Help (see Help.h): the pages, then the window -- a list down the left with
// a search box over it, and the page on the right, drawn with Direct2D.

#include "Help.h"
#include "Chrome.h"
#include "Commands.h"
#include "Window.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>

namespace help {

namespace {

// ---- Pages -------------------------------------------------------------------------

// In text, **bold** is bold.
struct Block {
	enum Kind { P, H, Keys, Tip } kind;
	std::string text;
	std::vector<std::pair<std::string, std::string>> keys;   // keys, what they do
};
struct Page {
	const char* id;
	const char* title;
	wchar_t icon;
	const char* line;
	std::vector<Block> blocks;
	bool started;   // Getting Started, else Using CedarLogic
};

Block p(const char* t) { return Block{ Block::P, t, {} }; }
Block h(const char* t) { return Block{ Block::H, t, {} }; }
Block tip(const char* t) { return Block{ Block::Tip, t, {} }; }
Block keys(std::vector<std::pair<std::string, std::string>> k) { return Block{ Block::Keys, "", std::move(k) }; }

const std::vector<Page>& pages() {
	static const std::vector<Page> all = {
		{ "welcome", "Welcome to CedarLogic", 0xE734, "Build logic circuits, run them live, and hand them in.",
		  { p("CedarLogic is a digital logic simulator. You put gates on a page, wire them together, and the circuit runs as you build it: "
		      "switches you can click, lights that show what's happening, wires that change colour with their signals."),
		    p("The window has three parts: the **side panel** of gates down the left (with a small map of the page under it), the "
		      "**canvas** where you build, and the **toolbar** across the top. Each circuit can have several **tabs** (pages), shown "
		      "above the canvas. Every menu is behind **•••** at the right of the toolbar (or press Alt)."),
		    tip("New here? Help ▸ Guided Tour shows you around, one part at a time.") },
		  true },
		{ "first", "Your First Circuit", 0xEA80, "Two switches, an AND gate and a light.",
		  { h("1. Two switches"),
		    p("Pick **Input and Output** in the menu at the top of the side panel and drag an **On / Off Toggle Switch** onto the canvas. "
		      "Drag a second one below it."),
		    h("2. The gate"),
		    p("Press **A**, type **and**, press Enter and move the pointer onto the canvas: the gate appears and follows the pointer. "
		      "Click to put it down to the right of the switches."),
		    h("3. The light"),
		    p("Add an **LED** (from Input and Output too) to the right of the gate."),
		    h("4. Wires"),
		    p("Drag from each switch's output pin to one of the gate's inputs, and from the gate's output to the light. A small square "
		      "shows the pin a wire will join."),
		    h("5. Try it"),
		    p("Click the switches. The light comes on only when both are on. Press **T** for the truth table.") },
		  true },
		{ "gates", "Adding Gates", 0xF0E2, "From the side panel, or by name.",
		  { p("**Drag** a gate out of the side panel and let go where you want it. Or **click** it: it follows the pointer until you "
		      "click the canvas."),
		    p("**Add a Gate (A)**: type part of a gate's name, press Enter, and move onto the canvas. The gate appears at the pointer; "
		      "click to put it down. Escape changes your mind. Your own parts are in it too."),
		    p("The menu at the top of the side panel picks a family of gates; the search box finds one by name."),
		    tip("Preferences (Ctrl+,) turns the names under the side panel's gates on or off.") },
		  false },
		{ "wiring", "Wiring", 0xE71B, "Pin to pin, or let C do it.",
		  { p("**Drag** from one pin to another. While you drag, a small square marks the pin the wire will join. Or **click** a pin, then "
		      "click the other end."),
		    p("**C while moving a gate** (dragging it, or with it on the pointer from the side panel, Add a Gate or a paste) connects its "
		      "free pins to the pins they're right next to, and keeps it moving. Escape takes back just those connections."),
		    p("**Straighten (S)** tidies the selected wires into clean routes. **Tidy Up (Shift+S)** tidies the whole page, showing you "
		      "first: Enter keeps it, Escape puts it back, Tab tries the other way."),
		    p("**Point at a wire** and every branch of it lights up, so you can follow it across the page."),
		    h("Wire colours"),
		    keys({ { "Silver", "0 (black on white when printed)" },
		           { "Red", "1. A bus is redder the more of its bits are 1" },
		           { "Green", "Nothing driving it (high-Z), like a tri-state buffer that's off" },
		           { "Blue", "Unknown: the simulation can't tell yet" },
		           { "Cyan", "Conflict: two outputs fighting over one wire" } }) },
		  false },
		{ "editing", "Selecting and Editing", 0xE8B0, "Move, copy, rotate and delete.",
		  { p("Click a gate or wire to select it; Shift-click to add more; drag across empty canvas to box-select. **Ctrl+A** selects "
		      "everything on the page."),
		    keys({ { "R", "Rotate the selection a quarter turn" },
		           { "D", "Duplicate: the copy follows the pointer" },
		           { "C", "Copy (when nothing's moving)" },
		           { "V", "Paste: it follows the pointer until you click" },
		           { "X", "Cut" },
		           { "Delete", "Delete the selection" },
		           { "Arrows", "Nudge the selection a square (Shift: five)" },
		           { "Ctrl+Z / Ctrl+Y", "Undo and redo" } }),
		    p("Double-click a gate for its settings (a clock's speed, a label's text). Double-click a RAM or ROM to see and change what's "
		      "in it.") },
		  false },
		{ "moving", "Moving Around", 0xE7C2, "Zoom, pan and the minimap.",
		  { keys({ { "Wheel", "Zoom or move, as set in Preferences (touchpads separately)" },
		           { "Ctrl + wheel", "Always zooms" },
		           { "Shift + wheel", "Moves sideways" },
		           { "Space", "Tap to fit the page; hold and drag to move around" },
		           { "Ctrl+= / Ctrl+-", "Zoom in and out" },
		           { "Ctrl+0", "Zoom to fit" },
		           { "Ctrl+1", "Actual size" },
		           { "Arrows", "Move around (when nothing's selected)" },
		           { "Ctrl+.", "Focus mode: the toolbar and side panel slide away" } }),
		    p("The minimap under the side panel shows the whole page; its box is what you can see. Click or drag in it to go there.") },
		  false },
		{ "tabs", "Tabs", 0xE8A4, "Pages of one circuit.",
		  { keys({ { "Ctrl+T", "New tab" },
		           { "Ctrl+W", "Close the tab (it asks if there's work on it)" },
		           { "Ctrl+Shift+T", "Reopen the tab you closed" },
		           { "Ctrl+Tab", "Switch tabs: tap for the last one, hold Ctrl for a picture of each" },
		           { "Ctrl+PgDn / PgUp", "Next and previous tab" },
		           { "Ctrl+Alt+S", "Split view: two tabs side by side" },
		           { "F6", "Switch side in a split view" },
		           { "Ctrl+Alt+W", "Close the split view" } }),
		    p("Double-click a tab to rename it on the tab (Enter keeps the name, Escape puts it back), and the empty strip for a new "
		      "tab. Drag a tab along the strip to put it in order, or down onto the canvas to split the view; drag it onto the other "
		      "side to move it there. The split closes when a side runs out of tabs.") },
		  false },
		{ "sim", "Running the Simulation", 0xE768, "It runs while you build.",
		  { p("The circuit runs all the time. The toolbar pauses and resumes it (so does **Space** on the canvas), **Ctrl+Shift+R** steps "
		      "once, and the speed slider sets how long each step takes."),
		    p("Click a switch to flip it; keypads and pulse generators take clicks too."),
		    p("**Simulation View (Ctrl+R)** is a dark, live presentation of the circuit: signals flow along the wires that are on, and the "
		      "bar at the bottom shows every switch and light. Point at a wire and all of it lights up. Space pauses, Escape leaves."),
		    p("To see what a wire carries, turn on **Show a wire's value when you rest on it** (Preferences ▸ Canvas): rest the "
		      "pointer on a wire and its value appears beside it, 1 in green and 0 in grey, a bus's bits and what they make, or "
		      "floating, a conflict or unknown."),
		    p("**Lock** (in the toolbar) stops edits, so a circuit can be shown and played with but not changed. Switches still work.") },
		  false },
		{ "analysis", "Truth Tables and the Oscilloscope", 0xE80A, "See what a circuit does.",
		  { p("**Truth table (T)** tries every combination of the page's switches and writes down its lights. Circuits with clocks or "
		      "flip-flops are read row by row, each after the circuit settles."),
		    p("Its other tabs: each light's **Karnaugh map** with the groups drawn on, and its **simplest sum of products** and **product "
		      "of sums**. Rows with no clear 0 or 1 count as don't-cares. Copy a formula, or **Build This as a Circuit** to make it again "
		      "as gates."),
		    p("**Oscilloscope (Ctrl+G)** records signals over time, under the canvas (drag the line over it to make it taller): every "
		      "**TO** label is a signal you can watch. Click or use the arrows "
		      "to move its time cursor (Alt+arrows jump to the next change), and the names column reads every signal there. Its share "
		      "button copies a **timing diagram** for a lab report, or saves it as a PNG (what's on screen, or the whole recording).") },
		  false },
		{ "formula", "Build from a Formula", 0xE943, "Type it, get the gates.",
		  { p("**Edit ▸ Build from Formula…** turns a formula into switches, gates and a light for each output, labelled and "
		      "wired, on a new page or beside what's there. One undo takes it back."),
		    p("Write NOT as **A'** or **~A**, AND as **AB**, **A·B** or **A*B**, OR as **A + B**, XOR as **A ⊕ B** or **A ^ B**. "
		      "One output per line: **S = A ^ B ^ Cin** then **Cout = AB + Cin(A ^ B)**. Or list minterms: **F(A,B,C) = Σm(1,3,5) + d(7)**."),
		    p("Build it **as written**, or as the **simplest sum of products** or **product of sums**; with **any gates**, **NAND only** "
		      "or **NOR only**; and with **only 2-input gates** if the exercise says so.") },
		  false },
		{ "find", "Finding Things", 0xE721, "Big circuits, found fast.",
		  { p("**Find (Ctrl+F)** searches every page for labels, **TO/FROM** names and parts (\"flip\", \"AND\", \"LED\"). Enter goes to "
		      "the next one; each is selected and brought to the middle."),
		    p("With a label or a TO/FROM selected, Find opens looking for its name, so a TO's FROMs are an Enter away.") },
		  false },
		{ "saving", "Saving, Your Circuits and Versions", 0xE81C, "It keeps itself.",
		  { p("Every circuit lives in **Your Circuits (Ctrl+O)** and saves itself a couple of seconds after each change. A new circuit "
		      "joins it once there's something on it. Open, rename or delete from there, or click the circuit's name at the top of the "
		      "window to rename it."),
		    p("**Files**: opening a .cdl file (from File Explorer, or **Ctrl+I**) brings in a copy to work on; the file itself isn't "
		      "touched, and opening it again finds the copy. To get a file out, **Export as CedarLogic File** saves a copy wherever you "
		      "like."),
		    p("**Versions**: **Ctrl+S** keeps a version. Version History (click the circuit's name) shows them with a picture of each; "
		      "restoring one keeps the current one too."),
		    keys({ { "Ctrl+N", "New circuit" },
		           { "Ctrl+I", "Open a .cdl file from anywhere" },
		           { "Ctrl+E", "Export an image, with your name under it" },
		           { "Ctrl+P", "Print" } }) },
		  false },
		{ "templates", "Templates and My Parts", 0xE8A5, "Start ahead, and reuse what you've built.",
		  { p("**File ▸ New from Template…** starts a circuit from a **Lab Page** (a title block with your name), a **4-Bit "
		      "Counter**, a **7-Segment Decoder Starter**, or one of your own. **File ▸ Save as Template…** keeps the circuit "
		      "you're in as one of yours."),
		    p("**My Parts**: select some gates, choose **Edit ▸ Save as Part…**, and name it. It's at the bottom of the side "
		      "panel's list (**My Parts**) to drag or click in, and in **Add a Gate (A)** under its name. It drops in as a copy of those "
		      "gates and wires, so any CedarLogic can open the circuit.") },
		  false },
		{ "settings", "Preferences", 0xE713, "Make it yours (Ctrl+,).",
		  { p("Your name (for exports and the Lab Page), the theme and accent colour, the grid, wires, the side panel, what the wheel "
		      "and touchpad do, right-click, duplicating, Tidy Up, and whether Ctrl+Q asks first."),
		    p("**Ctrl+Shift+D** switches between light and dark any time.") },
		  false },
		{ "feedback", "Sending Feedback", 0xED15, "It goes straight to the developer.",
		  { p("The **speech bubble** in the toolbar (or Help ▸ Send Feedback) sends a note: what happened, how much it matters, "
		      "screenshots of the window or pictures, and the circuit if you like. Tags are suggested from what you write."),
		    p("What's sent with it: the version, Windows, and the computer's model and screens. Never a file unless you add it.") },
		  false },
	};
	return all;
}

std::string lower(std::string s) {
	for (char& c : s) c = (char)tolower((unsigned char)c);
	return s;
}

std::string pageText(const Page& pg) {
	std::string t = std::string(pg.title) + " " + pg.line;
	for (const Block& b : pg.blocks) {
		t += " " + b.text;
		for (auto& k : b.keys) t += " " + k.first + " " + k.second;
	}
	std::string out;
	for (char c : t) if (c != '*') out += c;
	return lower(out);
}

// Text that wraps, **bold** in bold. Its height, drawn or not (rt null).
float rich(ID2D1RenderTarget* rt, const std::string& text, float x, float y, float width, float size, const D2D1_COLOR_F& color,
           bool allBold = false) {
	IDWriteFactory* dw = dwFactory();
	if (dw == nullptr) return 0;
	std::wstring plain;
	std::vector<std::pair<UINT32, UINT32>> bold;
	const std::wstring w = W(text);
	bool on = false;
	UINT32 start = 0;
	for (size_t i = 0; i < w.size(); i++) {
		if (w[i] == L'*' && i + 1 < w.size() && w[i + 1] == L'*') {
			if (on) bold.push_back({ start, (UINT32)plain.size() - start });
			else start = (UINT32)plain.size();
			on = !on;
			i++;
			continue;
		}
		// Key combos (Ctrl+A) stay on one line: word joiners around the +.
		if (w[i] == L'+' && i > 0 && i + 1 < w.size() && w[i - 1] != L' ' && w[i + 1] != L' ') { plain += L"\u2060+\u2060"; continue; }
		plain += w[i];
	}
	IDWriteTextFormat* f = nullptr;
	if (FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr, allBold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
	                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f)))
		return 0;
	IDWriteTextLayout* layout = nullptr;
	float height = 0;
	if (SUCCEEDED(dw->CreateTextLayout(plain.c_str(), (UINT32)plain.size(), f, width, 10000, &layout))) {
		layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, size * 1.45f, size * 1.12f);
		for (auto& b : bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{ b.first, b.second });
		DWRITE_TEXT_METRICS m = {};
		layout->GetMetrics(&m);
		height = m.height;
		if (rt) {
			ID2D1SolidColorBrush* br = nullptr;
			if (SUCCEEDED(rt->CreateSolidColorBrush(color, &br))) {
				rt->DrawTextLayout(D2D1::Point2F(x, y), layout, br);
				br->Release();
			}
		}
		layout->Release();
	}
	f->Release();
	return height;
}

// ---- The window ----------------------------------------------------------------------

const wchar_t* kClass = L"CedarLogicHelp";
const float kSide = 250, kRow = 30, kSearchH = 30;
enum Extra { kShortcuts = 1000, kClassic, kWhatsNew, kTour };

struct Row { int page = -1; int extra = 0; std::string heading; D2D1_RECT_F r{}; };

struct HelpWindow {
	HWND hwnd = nullptr, search = nullptr;
	WindowSurface surface;
	CircuitWindow* from = nullptr;
	int page = 0;
	float scroll = 0, contentH = 0, listScroll = 0, listH = 0;
	int hot = -1;
	std::vector<Row> rows;
	std::string query;
	HBRUSH editBrush = nullptr;

	void buildRows() {
		rows.clear();
		const std::vector<Page>& all = pages();
		const std::string q = lower(query);
		auto add = [&](bool started) {
			bool any = false;
			for (int i = 0; i < (int)all.size(); i++) {
				if (all[i].started != started) continue;
				if (!q.empty() && pageText(all[i]).find(q) == std::string::npos) continue;
				if (!any) rows.push_back(Row{ -1, 0, started ? "GETTING STARTED" : "USING CEDARLOGIC" });
				any = true;
				rows.push_back(Row{ i, 0, "" });
			}
		};
		add(true);
		add(false);
		// The rest of Help, found by name (and a few words for it) too.
		struct More { int extra; const char* name; const char* words; };
		const More more[] = { { kShortcuts, "Keyboard Shortcuts", "keys hotkeys" },
		                      { kTour, "Guided Tour", "tutorial walkthrough" },
		                      { kWhatsNew, "What's New", "whats new changes release notes" },
		                      { kClassic, "Classic Help", "old original" } };
		bool anyMore = false;
		for (const More& m : more) {
			if (!q.empty() && lower(std::string(m.name) + " " + m.words).find(q) == std::string::npos) continue;
			if (!anyMore) rows.push_back(Row{ -1, 0, "MORE" });
			anyMore = true;
			rows.push_back(Row{ -1, m.extra, m.name });
		}
		if (rows.empty()) rows.push_back(Row{ -1, 0, "NOTHING FOUND" });
		listScroll = 0;
	}

	void choose(const Row& r) {
		if (r.page >= 0) {
			page = r.page;
			scroll = 0;
			redraw();
			return;
		}
		bool alive = std::find(circuitWindows().begin(), circuitWindows().end(), from) != circuitWindows().end();
		if (!alive) from = circuitWindows().empty() ? nullptr : circuitWindows().front();
		switch (r.extra) {
		case kShortcuts: if (from) from->showShortcuts(); break;
		case kTour: if (from) { SetForegroundWindow(from->window()); from->run(CMD_TOUR); } break;
		case kWhatsNew: if (from) { SetForegroundWindow(from->window()); from->run(CMD_WHATS_NEW); } break;
		case kClassic: {
			const std::string file = resourcesDir() + "\\help\\Introduction.htm";
			if (fileExists(file)) openExternally(hwnd, file);
			else MessageBeep(MB_ICONWARNING);
			break;
		}
		default: break;
		}
	}

	void redraw() { InvalidateRect(hwnd, nullptr, FALSE); }

	void layoutSearch() {
		const UINT dpi = dpiOf(hwnd);
		SetWindowPos(search, nullptr, scaled(14, dpi), scaled(14, dpi), scaled((int)kSide - 28, dpi), scaled((int)kSearchH - 4, dpi),
		             SWP_NOZORDER | SWP_NOACTIVATE);
		SendMessageW(search, WM_SETFONT, (WPARAM)uiFont(dpi), TRUE);
	}

	void paint() {
		PAINTSTRUCT ps;
		BeginPaint(hwnd, &ps);
		ID2D1HwndRenderTarget* rt = surface.begin(hwnd);
		if (rt == nullptr) { EndPaint(hwnd, &ps); return; }
		const Chrome c = chrome();
		const bool dark = c.dark;
		const D2D1_COLOR_F ink = dark ? D2D1::ColorF(0.93f, 0.94f, 0.95f) : D2D1::ColorF(0.1f, 0.1f, 0.12f);
		const D2D1_COLOR_F dim = withAlpha(ink, 0.6f), accent = c.accent();
		RECT rc;
		GetClientRect(hwnd, &rc);
		const float s = (float)surface.scale(), w = rc.right / s, hgt = rc.bottom / s;
		rt->Clear(dark ? D2D1::ColorF(0.11f, 0.12f, 0.14f) : D2D1::ColorF(1, 1, 1));

		// The list.
		fillRect(rt, D2D1::RectF(0, 0, kSide, hgt), c.panel());
		fillRect(rt, D2D1::RectF(kSide - 1, 0, kSide, hgt), c.hairline());
		const float listTop = 14 + kSearchH + 8;
		rt->PushAxisAlignedClip(D2D1::RectF(0, listTop, kSide, hgt), D2D1_ANTIALIAS_MODE_ALIASED);
		float y = listTop - listScroll;
		for (int i = 0; i < (int)rows.size(); i++) {
			Row& r = rows[i];
			const bool heading = r.page < 0 && r.extra == 0;
			const float rh = heading ? 28 : kRow;
			r.r = D2D1::RectF(8, y, kSide - 8, y + rh);
			if (heading) {
				drawText(rt, r.heading, D2D1::RectF(18, y + 11, kSide - 10, y + rh), 10.5f, dim, TextAlign::Leading, true);
			} else {
				const bool sel = r.page >= 0 && r.page == page;
				if (sel) fillRound(rt, r.r, 7, withAlpha(accent, dark ? 0.28f : 0.16f));
				else if (i == hot) fillRound(rt, r.r, 7, withAlpha(ink, 0.06f));
				const wchar_t icon = r.page >= 0 ? pages()[r.page].icon
				                   : r.extra == kShortcuts ? 0xE765 : r.extra == kTour ? 0xE7C1 : r.extra == kWhatsNew ? 0xE734 : 0xE897;
				drawIcon(rt, icon, D2D1::RectF(r.r.left + 6, r.r.top, r.r.left + 30, r.r.bottom), 13, sel ? accent : withAlpha(ink, 0.7f));
				const std::string label = r.page >= 0 ? pages()[r.page].title : r.heading;
				drawText(rt, label, D2D1::RectF(r.r.left + 36, r.r.top + 7, r.r.right - 6, r.r.bottom), 12.5f, ink, TextAlign::Leading, sel);
				if (r.extra == kClassic) drawIcon(rt, 0xE8A7, D2D1::RectF(r.r.right - 24, r.r.top, r.r.right - 4, r.r.bottom), 10, dim);
			}
			y += rh;
		}
		listH = y + listScroll - listTop;
		rt->PopAxisAlignedClip();

		// The page.
		const Page& pg = pages()[std::max(0, std::min((int)pages().size() - 1, page))];
		const float left = kSide + 44, width = std::min(660.0f, w - left - 40);
		rt->PushAxisAlignedClip(D2D1::RectF(kSide, 0, w, hgt), D2D1_ANTIALIAS_MODE_ALIASED);
		y = 34 - scroll;
		fillRound(rt, D2D1::RectF(left, y, left + 44, y + 44), 11, withAlpha(accent, dark ? 0.22f : 0.13f));
		drawIcon(rt, pg.icon, D2D1::RectF(left, y, left + 44, y + 44), 20, accent);
		drawText(rt, pg.title, D2D1::RectF(left + 58, y - 2, left + width, y + 30), 24, ink, TextAlign::Leading, true);
		drawText(rt, pg.line, D2D1::RectF(left + 58, y + 27, left + width, y + 46), 13.5f, dim);
		y += 70;
		for (const Block& b : pg.blocks) {
			switch (b.kind) {
			case Block::P:
				y += rich(rt, b.text, left, y, width, 14, ink) + 12;
				break;
			case Block::H:
				y += 6;
				y += rich(rt, b.text, left, y, width, 15.5f, ink, true) + 6;
				break;
			case Block::Tip: {
				const float th = rich(nullptr, b.text, 0, 0, width - 56, 13.5f, ink);
				const D2D1_RECT_F box = D2D1::RectF(left, y, left + width, y + th + 24);
				fillRound(rt, box, 10, withAlpha(accent, dark ? 0.14f : 0.08f));
				drawIcon(rt, 0xEA80, D2D1::RectF(left + 12, y + 12, left + 34, y + 30), 14, accent);
				rich(rt, b.text, left + 42, y + 12, width - 56, 13.5f, ink);
				y += th + 24 + 14;
				break;
			}
			case Block::Keys: {
				float kw = 0;
				for (auto& k : b.keys) kw = std::max(kw, textWidth(k.first, 12.5f, true));
				kw = std::min(170.0f, kw + 22);
				const float top = y;
				for (size_t i = 0; i < b.keys.size(); i++) {
					const float rh = std::max(30.0f, rich(nullptr, b.keys[i].second, 0, 0, width - kw - 30, 13.5f, ink) + 12);
					if (i > 0) fillRect(rt, D2D1::RectF(left + 10, y, left + width - 10, y + 1), withAlpha(ink, 0.07f));
					const D2D1_RECT_F chip = D2D1::RectF(left + 10, y + 5, left + 10 + kw, y + 25);
					fillRound(rt, chip, 6, withAlpha(ink, dark ? 0.09f : 0.06f));
					drawText(rt, b.keys[i].first, D2D1::RectF(chip.left, chip.top + 2, chip.right, chip.bottom), 12.5f, ink, TextAlign::Center, true);
					rich(rt, b.keys[i].second, left + kw + 22, y + 5, width - kw - 30, 13.5f, ink);
					y += rh;
				}
				strokeRound(rt, D2D1::RectF(left, top, left + width, y), 10, withAlpha(ink, 0.1f));
				y += 16;
				break;
			}
			}
		}
		contentH = y + scroll + 40;
		rt->PopAxisAlignedClip();
		surface.end();
		EndPaint(hwnd, &ps);
	}

	int rowAt(float x, float y) const {
		if (x >= kSide || y < 14 + kSearchH + 8) return -1;
		for (int i = 0; i < (int)rows.size(); i++)
			if ((rows[i].page >= 0 || rows[i].extra) && inRect(rows[i].r, x, y)) return i;
		return -1;
	}

	LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
		const float s = dpiOf(hwnd) / 96.0f;
		const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
		switch (msg) {
		case WM_PAINT: paint(); return 0;
		case WM_ERASEBKGND: return 1;
		case WM_SIZE: layoutSearch(); redraw(); return 0;
		case WM_DPICHANGED: {
			const RECT* r = reinterpret_cast<const RECT*>(lp);
			SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}
		case WM_MOUSEMOVE: {
			TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&t);
			const int i = rowAt(x, y);
			if (i != hot) { hot = i; redraw(); }
			SetCursor(LoadCursor(nullptr, i >= 0 ? IDC_HAND : IDC_ARROW));
			return 0;
		}
		case WM_MOUSELEAVE: hot = -1; redraw(); return 0;
		case WM_LBUTTONUP: {
			const int i = rowAt(x, y);
			if (i >= 0) choose(rows[i]);
			return 0;
		}
		case WM_MOUSEWHEEL: {
			POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
			ScreenToClient(hwnd, &pt);
			RECT rc;
			GetClientRect(hwnd, &rc);
			const float view = rc.bottom / s, d = -GET_WHEEL_DELTA_WPARAM(wp) * 48.0f / WHEEL_DELTA;
			if (pt.x / s < kSide) listScroll = std::max(0.0f, std::min(listScroll + d, std::max(0.0f, listH - (view - 52))));
			else scroll = std::max(0.0f, std::min(scroll + d, std::max(0.0f, contentH - view)));
			redraw();
			return 0;
		}
		case WM_KEYDOWN: return key((UINT)wp) ? 0 : DefWindowProcW(hwnd, msg, wp, lp);
		case WM_COMMAND:
			if ((HWND)lp == search && HIWORD(wp) == EN_CHANGE) {
				query = windowText(search);
				buildRows();
				// The first page found, shown.
				for (const Row& r : rows) if (r.page >= 0) { page = r.page; scroll = 0; break; }
				redraw();
			}
			return 0;
		case WM_CTLCOLOREDIT:
			if (prefs().dark) {
				SetTextColor((HDC)wp, RGB(228, 232, 240));
				SetBkColor((HDC)wp, RGB(24, 26, 31));
				if (editBrush == nullptr) editBrush = CreateSolidBrush(RGB(24, 26, 31));
				return (LRESULT)editBrush;
			}
			break;
		case WM_CLOSE: DestroyWindow(hwnd); return 0;
		case WM_DESTROY: if (editBrush) DeleteObject(editBrush); editBrush = nullptr; break;
		}
		return DefWindowProcW(hwnd, msg, wp, lp);
	}

	// Up and Down move through the pages; Page keys and Home/End scroll.
	bool key(UINT vk) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		const float view = rc.bottom / (dpiOf(hwnd) / 96.0f);
		if (vk == VK_ESCAPE) {
			if (!query.empty()) SetWindowTextW(search, L"");
			else DestroyWindow(hwnd);
			return true;
		}
		if (vk == VK_UP || vk == VK_DOWN) {
			int at = -1;
			for (int i = 0; i < (int)rows.size(); i++) if (rows[i].page == page) at = i;
			for (int i = at + (vk == VK_UP ? -1 : 1); i >= 0 && i < (int)rows.size(); i += vk == VK_UP ? -1 : 1)
				if (rows[i].page >= 0) { page = rows[i].page; scroll = 0; redraw(); break; }
			return true;
		}
		float to = scroll;
		if (vk == VK_NEXT || vk == VK_SPACE) to += view - 60;
		else if (vk == VK_PRIOR) to -= view - 60;
		else if (vk == VK_HOME) to = 0;
		else if (vk == VK_END) to = contentH;
		else return false;
		scroll = std::max(0.0f, std::min(to, std::max(0.0f, contentH - view)));
		redraw();
		return true;
	}
};

HelpWindow* g_help = nullptr;

// The search box passes Up, Down, Escape and the Page keys to the window,
// and Home and End while there's nothing typed (else they move the caret).
LRESULT CALLBACK searchProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
	const bool mine = wp == VK_UP || wp == VK_DOWN || wp == VK_ESCAPE || wp == VK_PRIOR || wp == VK_NEXT ||
	                  ((wp == VK_HOME || wp == VK_END) && GetWindowTextLengthW(h) == 0);
	if (msg == WM_KEYDOWN && mine && g_help) { g_help->key((UINT)wp); return 0; }
	if (msg == WM_CHAR && (wp == VK_ESCAPE || wp == VK_RETURN)) return 0;
	return DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	HelpWindow* w = reinterpret_cast<HelpWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	if (w == nullptr) return DefWindowProcW(h, msg, wp, lp);
	LRESULT r = 0;
	bool ok = false;
	guarded("Help", [&] { r = w->handle(msg, wp, lp); ok = true; });
	if (msg == WM_NCDESTROY) {
		SetWindowLongPtrW(h, GWLP_USERDATA, 0);
		if (g_help == w) g_help = nullptr;
		delete w;
	}
	return ok ? r : DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

HWND window() { return g_help ? g_help->hwnd : nullptr; }

void show(CircuitWindow* from, const std::string& id) {
	int page = 0;
	for (int i = 0; i < (int)pages().size(); i++) if (id == pages()[i].id) page = i;
	if (g_help) {
		g_help->from = from;
		if (!id.empty()) { g_help->page = page; g_help->scroll = 0; g_help->redraw(); }
		if (IsIconic(g_help->hwnd)) ShowWindow(g_help->hwnd, SW_RESTORE);
		SetForegroundWindow(g_help->hwnd);
		return;
	}
	static bool registered = false;
	if (!registered) {
		registered = true;
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof wc;
		wc.lpfnWndProc = proc;
		wc.hInstance = appInstance();
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hIcon = LoadIconW(appInstance(), MAKEINTRESOURCEW(1));
		wc.lpszClassName = kClass;
		RegisterClassExW(&wc);
	}
	HelpWindow* w = new HelpWindow();
	g_help = w;
	w->from = from;
	w->page = page;
	w->buildRows();
	const UINT dpi = from ? dpiOf(from->window()) : 96;
	w->hwnd = CreateWindowExW(0, kClass, L"CedarLogic Help", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
	                          scaled(980, dpi), scaled(680, dpi), nullptr, nullptr, appInstance(), nullptr);
	SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w);
	w->search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, w->hwnd,
	                            nullptr, appInstance(), nullptr);
	SendMessageW(w->search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search Help");
	SetWindowSubclass(w->search, searchProc, 1, 0);
	setDarkTitleBar(w->hwnd, prefs().dark);
	if (prefs().dark) darkenControl(w->search, true, L"CFD");
	w->layoutSearch();
	ShowWindow(w->hwnd, SW_SHOWNORMAL);
	SetFocus(w->search);
}

}  // namespace help
