// Help (F1), as the Mac app's (Help.swift) and the Windows app's: pages on
// how this app works, with one search over them all; the shortcut list; and
// Classic Help -- the original CedarLogic Help, opened in the browser as it
// always was. A list down the left with a search box over it, and the page
// on the right, drawn with Cairo.

#include "Help.h"
#include "Chrome.h"
#include "Window.h"

#include <pango/pangocairo.h>

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
	const char* icon;
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
		{ "welcome", "Welcome to CedarLogic", "starred-symbolic", "Build logic circuits, run them live, and hand them in.",
		  { p("CedarLogic is a digital logic simulator. You put gates on a page, wire them together, and the circuit runs as you build it: "
		      "switches you can click, lights that show what's happening, wires that change colour with their signals."),
		    p("The window has three parts: the **side panel** of gates down the left (with a small map of the page under it), the "
		      "**canvas** where you build, and the **toolbar** across the top. Each circuit can have several **tabs** (pages), shown "
		      "above the canvas. Every menu is behind **•••** at the right of the toolbar."),
		    tip("New here? Help ▸ Guided Tour shows you around, one part at a time.") },
		  true },
		{ "first", "Your First Circuit", "dialog-information-symbolic", "Two switches, an AND gate and a light.",
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
		{ "gates", "Adding Gates", "list-add-symbolic", "From the side panel, or by name.",
		  { p("**Drag** a gate out of the side panel and let go where you want it. Or **click** it: it follows the pointer until you "
		      "click the canvas."),
		    p("**Add a Gate (A)**: type part of a gate's name, press Enter, and move onto the canvas. The gate appears at the pointer; "
		      "click to put it down. Escape changes your mind. Your own parts are in it too."),
		    p("The menu at the top of the side panel picks a family of gates; the search box finds one by name."),
		    tip("Preferences (Ctrl+,) turns the names under the side panel's gates on or off.") },
		  false },
		{ "wiring", "Wiring", "network-wired-symbolic", "Pin to pin, or let C do it.",
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
		{ "editing", "Selecting and Editing", "edit-select-all-symbolic", "Move, copy, rotate and delete.",
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
		{ "moving", "Moving Around", "zoom-fit-best-symbolic", "Zoom, pan and the minimap.",
		  { keys({ { "Wheel", "Zoom or move, as set in Preferences (touchpads separately)" },
		           { "Ctrl + wheel", "Always zooms" },
		           { "Shift + wheel", "Moves sideways" },
		           { "Space", "Tap to fit the page; hold and drag to move around" },
		           { "Ctrl+= / Ctrl+-", "Zoom in and out" },
		           { "Ctrl+0", "Zoom to fit" },
		           { "Ctrl+1", "Actual size" },
		           { "Arrows", "Move around (when nothing's selected)" },
		           { "Ctrl+.", "Show or hide the side panel" } }),
		    p("The minimap under the side panel shows the whole page; its box is what you can see. Click or drag in it to go there.") },
		  false },
		{ "tabs", "Tabs", "tab-new-symbolic", "Pages of one circuit.",
		  { keys({ { "Ctrl+T", "New tab" },
		           { "Ctrl+W", "Close the tab (it asks if there's work on it)" },
		           { "Ctrl+Shift+T", "Reopen the tab you closed" },
		           { "Ctrl+Tab", "Switch tabs: tap for the last one, hold Ctrl for a picture of each" },
		           { "Ctrl+PgDn / PgUp", "Next and previous tab" } }),
		    p("Double-click a tab to rename it; drag it along the strip to put it in order.") },
		  false },
		{ "sim", "Running the Simulation", "media-playback-start-symbolic", "It runs while you build.",
		  { p("The circuit runs all the time. The toolbar pauses and resumes it (so does **Space** on the canvas), **Ctrl+Shift+R** steps "
		      "once, and the speed slider sets how long each step takes."),
		    p("Click a switch to flip it; keypads and pulse generators take clicks too."),
		    p("**Simulation View (Ctrl+R)** is a dark, live presentation of the circuit: signals flow along the wires that are on, and the "
		      "bar at the bottom shows every switch and light. Space pauses, Escape leaves."),
		    p("**Lock** (in the toolbar) stops edits, so a circuit can be shown and played with but not changed. Switches still work.") },
		  false },
		{ "analysis", "Truth Tables and the Oscilloscope", "view-grid-symbolic", "See what a circuit does.",
		  { p("**Truth table (T)** tries every combination of the page's switches and writes down its lights. Circuits with clocks or "
		      "flip-flops are read row by row, each after the circuit settles."),
		    p("Its other tabs: each light's **Karnaugh map** with the groups drawn on, and its **simplest sum of products** and **product "
		      "of sums**. Rows with no clear 0 or 1 count as don't-cares. Copy a formula, or **Build This as a Circuit** to make it again "
		      "as gates."),
		    p("**Check my circuit (Shift+T)** compares the lights with what the assignment asks for: type the formulas (**S = A ^ B ^ Cin**, "
		      "then **Cout = AB + Cin(A ^ B)**), a minterm list (**F(A,B,C) = Σm(1,3,5) + d(7)**), or paste the truth table you were "
		      "given. Switches and lights are matched by name (pick by hand under **Names** when they differ), don't-cares are skipped, "
		      "and wrong rows are shown with what was asked for and what the circuit gave. The last check is kept for each circuit."),
		    p("**Oscilloscope (Ctrl+G)** records signals over time: every **TO** label is a signal you can watch. Click or use the arrows "
		      "to move its time cursor (Alt+arrows jump to the next change), and the names column reads every signal there. Its share "
		      "button copies a **timing diagram** for a lab report, or saves it as a PNG (what's on screen, or the whole recording).") },
		  false },
		{ "formula", "Build from a Formula", "accessories-calculator-symbolic", "Type it, get the gates.",
		  { p("**Edit ▸ Build from Formula…** turns a formula into switches, gates and a light for each output, labelled and "
		      "wired, on a new page or beside what's there. One undo takes it back."),
		    p("Write NOT as **A'** or **~A**, AND as **AB**, **A·B** or **A*B**, OR as **A + B**, XOR as **A ⊕ B** or **A ^ B**. "
		      "One output per line: **S = A ^ B ^ Cin** then **Cout = AB + Cin(A ^ B)**. Or list minterms: **F(A,B,C) = Σm(1,3,5) + d(7)**."),
		    p("Build it **as written**, or as the **simplest sum of products** or **product of sums**; with **any gates**, **NAND only** "
		      "or **NOR only**; and with **only 2-input gates** if the exercise says so.") },
		  false },
		{ "find", "Finding Things", "edit-find-symbolic", "Big circuits, found fast.",
		  { p("**Find (Ctrl+F)** searches every page for labels, **TO/FROM** names and parts (\"flip\", \"AND\", \"LED\"). Enter goes to "
		      "the next one; each is selected and brought to the middle."),
		    p("With a label or a TO/FROM selected, Find opens looking for its name, so a TO's FROMs are an Enter away.") },
		  false },
		{ "saving", "Saving, Your Circuits and Versions", "document-open-recent-symbolic", "It keeps itself.",
		  { p("Every circuit lives in **Your Circuits (Ctrl+O)** and saves itself a couple of seconds after each change. A new circuit "
		      "joins it once there's something on it. Open, rename or delete from there, or click the circuit's name at the top of the "
		      "window to rename it."),
		    p("**Files**: opening a .cdl file (from the Files app, or **Ctrl+I**) brings in a copy to work on; the file itself isn't "
		      "touched, and opening it again finds the copy. To get a file out, **Export as CedarLogic File** saves a copy wherever you "
		      "like."),
		    p("**Versions**: **Ctrl+S** keeps a version. Version History (click the circuit's name) shows them with a picture of each; "
		      "restoring one keeps the current one too."),
		    keys({ { "Ctrl+N", "New circuit" },
		           { "Ctrl+I", "Open a .cdl file from anywhere" },
		           { "Ctrl+E", "Export an image, with your name under it" },
		           { "Ctrl+P", "Print" } }) },
		  false },
		{ "templates", "Templates and My Parts", "document-new-symbolic", "Start ahead, and reuse what you've built.",
		  { p("**File ▸ New from Template…** starts a circuit from a **Lab Page** (a title block with your name), a **4-Bit "
		      "Counter**, a **7-Segment Decoder Starter**, or one of your own. **File ▸ Save as Template…** keeps the circuit "
		      "you're in as one of yours."),
		    p("**My Parts**: select some gates, choose **Edit ▸ Save as Part…**, and name it. It's at the bottom of the side "
		      "panel's list (**My Parts**) to drag or click in, and in **Add a Gate (A)** under its name. It drops in as a copy of those "
		      "gates and wires, so any CedarLogic can open the circuit.") },
		  false },
		{ "settings", "Preferences", "preferences-system-symbolic", "Make it yours (Ctrl+,).",
		  { p("Your name (for exports and the Lab Page), the theme and accent colour, the grid, wires, the side panel, what the wheel "
		      "and touchpad do, right-click, duplicating, Tidy Up, and whether Ctrl+Q asks first."),
		    p("**Ctrl+Shift+D** switches between light and dark any time.") },
		  false },
		{ "feedback", "Sending Feedback", "mail-message-new-symbolic", "It goes straight to the developer.",
		  { p("The **speech bubble** in the toolbar (or Help ▸ Send Feedback) sends a note: what happened, how much it matters, "
		      "screenshots of the window or pictures, and the circuit if you like. Tags are suggested from what you write."),
		    p("What's sent with it: the version, the Linux you run, and the computer's model and screens. Never a file unless you add it.") },
		  false },
	};
	return all;
}

std::string lower(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
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

// Text that wraps, **bold** in bold. Its height, drawn or not (cr null).
float rich(cairo_t* cr, const std::string& text, float x, float y, float width, float size, const Color& color, bool allBold = false) {
	std::string markup;
	bool on = false;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '*') {
			markup += on ? "</b>" : "<b>";
			on = !on;
			i++;
			continue;
		}
		// Key combos (Ctrl+A) stay on one line: word joiners around the +.
		if (text[i] == '+' && i > 0 && i + 1 < text.size() && text[i - 1] != ' ' && text[i + 1] != ' ') { markup += "⁠+⁠"; continue; }
		if (text[i] == '<') markup += "&lt;";
		else if (text[i] == '>') markup += "&gt;";
		else if (text[i] == '&') markup += "&amp;";
		else markup += text[i];
	}
	if (on) markup += "</b>";
	static cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	static cairo_t* measure = cairo_create(scratch);
	PangoLayout* l = pango_cairo_create_layout(cr ? cr : measure);
	PangoFontDescription* d = pango_font_description_from_string("Sans");
	if (GtkSettings* s = gtk_settings_get_default()) {
		gchar* name = nullptr;
		g_object_get(s, "gtk-font-name", &name, nullptr);
		if (name) {
			PangoFontDescription* sys = pango_font_description_from_string(name);
			if (const char* fam = pango_font_description_get_family(sys)) pango_font_description_set_family(d, fam);
			pango_font_description_free(sys);
			g_free(name);
		}
	}
	pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
	pango_font_description_set_weight(d, allBold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
	pango_layout_set_font_description(l, d);
	pango_font_description_free(d);
	pango_layout_set_width(l, (int)(width * PANGO_SCALE));
	pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_spacing(l, (int)(size * 0.3f * PANGO_SCALE));
	pango_layout_set_markup(l, markup.c_str(), -1);
	PangoRectangle logical;
	pango_layout_get_extents(l, nullptr, &logical);
	if (cr) {
		cairo_save(cr);
		setColor(cr, color);
		cairo_move_to(cr, x, y);
		pango_cairo_show_layout(cr, l);
		cairo_restore(cr);
	}
	g_object_unref(l);
	return logical.height / (float)PANGO_SCALE;
}

// ---- The window ----------------------------------------------------------------------

const float kSide = 250, kRow = 30, kSearchH = 30;
enum Extra { kShortcuts = 1000, kClassic, kWhatsNew, kTour };

struct Row { int page = -1; int extra = 0; std::string heading; RectF r{}; };

struct HelpWindow {
	GtkWidget* win = nullptr;
	GtkWidget* area = nullptr;
	GtkWidget* search = nullptr;
	CircuitWindow* from = nullptr;
	int page = 0;
	float scroll = 0, contentH = 0, listScroll = 0, listH = 0;
	int hot = -1;
	std::vector<Row> rows;
	std::string query;

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
		if (q.empty()) {
			rows.push_back(Row{ -1, 0, "MORE" });
			rows.push_back(Row{ -1, kShortcuts, "Keyboard Shortcuts" });
			rows.push_back(Row{ -1, kTour, "Guided Tour" });
			rows.push_back(Row{ -1, kWhatsNew, "What's New" });
			rows.push_back(Row{ -1, kClassic, "Classic Help" });
		} else if (rows.empty()) {
			rows.push_back(Row{ -1, 0, "NOTHING FOUND" });
		}
		listScroll = 0;
	}

	void redraw() { if (area) gtk_widget_queue_draw(area); }

	void choose(const Row& r) {
		if (r.page >= 0) {
			page = r.page;
			scroll = 0;
			redraw();
			return;
		}
		const bool alive = std::find(circuitWindows().begin(), circuitWindows().end(), from) != circuitWindows().end();
		if (!alive) from = circuitWindows().empty() ? nullptr : circuitWindows().front();
		switch (r.extra) {
		case kShortcuts: if (from) from->showShortcuts(); break;
		case kTour: if (from) { gtk_window_present(from->window()); from->runAction("app.guided-tour"); } break;
		case kWhatsNew: if (from) { gtk_window_present(from->window()); from->runAction("win.whats-new"); } break;
		case kClassic: {
			const std::string file = resourcesDir() + "/help/Introduction.htm";
			gchar* uri = g_file_test(file.c_str(), G_FILE_TEST_EXISTS) ? g_filename_to_uri(file.c_str(), nullptr, nullptr) : nullptr;
			if (uri) { openExternally(GTK_WINDOW(win), uri); g_free(uri); }
			else gtk_widget_error_bell(win);
			break;
		}
		default: break;
		}
	}

	const char* rowIcon(const Row& r) const {
		if (r.page >= 0) return pages()[r.page].icon;
		switch (r.extra) {
		case kShortcuts: return "input-keyboard-symbolic";
		case kTour: return "find-location-symbolic";
		case kWhatsNew: return "starred-symbolic";
		default: return "help-browser-symbolic";
		}
	}

	void paint(cairo_t* cr, float w, float hgt) {
		const Chrome c = chrome();
		const bool dark = c.dark;
		const Color ink = dark ? colorF(0.93f, 0.94f, 0.95f) : colorF(0.1f, 0.1f, 0.12f);
		const Color dim = withAlpha(ink, 0.6f), accent = c.accent();
		fillRect(cr, rectF(0, 0, w, hgt), dark ? colorF(0.11f, 0.12f, 0.14f) : colorF(1, 1, 1));

		// The list.
		fillRect(cr, rectF(0, 0, kSide, hgt), c.panel());
		fillRect(cr, rectF(kSide - 1, 0, kSide, hgt), c.hairline());
		const float listTop = 14 + kSearchH + 8;
		cairo_save(cr);
		cairo_rectangle(cr, 0, listTop, kSide, hgt - listTop);
		cairo_clip(cr);
		float y = listTop - listScroll;
		for (int i = 0; i < (int)rows.size(); i++) {
			Row& r = rows[i];
			const bool heading = r.page < 0 && r.extra == 0;
			const float rh = heading ? 28 : kRow;
			r.r = rectF(8, y, kSide - 8, y + rh);
			if (heading) {
				drawText(cr, r.heading, rectF(18, y + 11, kSide - 10, y + rh), 10.5f, dim, TextAlign::Leading, true);
			} else {
				const bool sel = r.page >= 0 && r.page == page;
				if (sel) fillRound(cr, r.r, 7, withAlpha(accent, dark ? 0.28f : 0.16f));
				else if (i == hot) fillRound(cr, r.r, 7, withAlpha(ink, 0.06f));
				drawIcon(cr, rowIcon(r), rectF(r.r.left + 6, r.r.top, r.r.left + 30, r.r.bottom), 14, sel ? accent : withAlpha(ink, 0.7f));
				const std::string label = r.page >= 0 ? pages()[r.page].title : r.heading;
				drawTextMid(cr, label, rectF(r.r.left + 36, r.r.top, r.r.right - 6, r.r.bottom), 12.5f, ink, TextAlign::Leading, sel);
				if (r.extra == kClassic)
					drawIcon(cr, "send-to-symbolic", rectF(r.r.right - 24, r.r.top, r.r.right - 4, r.r.bottom), 11, dim);
			}
			y += rh;
		}
		listH = y + listScroll - listTop;
		cairo_restore(cr);

		// The page.
		const Page& pg = pages()[std::max(0, std::min((int)pages().size() - 1, page))];
		const float left = kSide + 44, width = std::min(660.0f, w - left - 40);
		cairo_save(cr);
		cairo_rectangle(cr, kSide, 0, w - kSide, hgt);
		cairo_clip(cr);
		y = 34 - scroll;
		fillRound(cr, rectF(left, y, left + 44, y + 44), 11, withAlpha(accent, dark ? 0.22f : 0.13f));
		drawIcon(cr, pg.icon, rectF(left, y, left + 44, y + 44), 20, accent);
		drawText(cr, pg.title, rectF(left + 58, y - 2, left + width, y + 30), 24, ink, TextAlign::Leading, true);
		drawText(cr, pg.line, rectF(left + 58, y + 29, left + width, y + 48), 13.5f, dim);
		y += 70;
		for (const Block& b : pg.blocks) {
			switch (b.kind) {
			case Block::P: y += rich(cr, b.text, left, y, width, 14, ink) + 12; break;
			case Block::H:
				y += 6;
				y += rich(cr, b.text, left, y, width, 15.5f, ink, true) + 6;
				break;
			case Block::Tip: {
				const float th = rich(nullptr, b.text, 0, 0, width - 56, 13.5f, ink);
				const RectF box = rectF(left, y, left + width, y + th + 24);
				fillRound(cr, box, 10, withAlpha(accent, dark ? 0.14f : 0.08f));
				drawIcon(cr, "dialog-information-symbolic", rectF(left + 12, y + 12, left + 34, y + 30), 14, accent);
				rich(cr, b.text, left + 42, y + 12, width - 56, 13.5f, ink);
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
					if (i > 0) fillRect(cr, rectF(left + 10, y, left + width - 10, y + 1), withAlpha(ink, 0.07f));
					const RectF chip = rectF(left + 10, y + 5, left + 10 + kw, y + 25);
					fillRound(cr, chip, 6, withAlpha(ink, dark ? 0.09f : 0.06f));
					drawTextMid(cr, b.keys[i].first, chip, 12.5f, ink, TextAlign::Center, true);
					rich(cr, b.keys[i].second, left + kw + 22, y + 5, width - kw - 30, 13.5f, ink);
					y += rh;
				}
				strokeRound(cr, rectF(left, top, left + width, y), 10, withAlpha(ink, 0.1f));
				y += 16;
				break;
			}
			}
		}
		contentH = y + scroll + 40;
		cairo_restore(cr);
	}

	int rowAt(float x, float y) const {
		if (x >= kSide || y < 14 + kSearchH + 8) return -1;
		for (int i = 0; i < (int)rows.size(); i++)
			if ((rows[i].page >= 0 || rows[i].extra) && inRect(rows[i].r, x, y)) return i;
		return -1;
	}

	float viewH() const { return area ? (float)gtk_widget_get_allocated_height(area) : 600; }

	// Up and Down move through the pages; Page keys and Home/End scroll.
	bool key(guint k) {
		const float view = viewH();
		if (k == GDK_KEY_Escape) {
			if (!query.empty()) gtk_entry_set_text(GTK_ENTRY(search), "");
			else gtk_widget_destroy(win);
			return true;
		}
		if (k == GDK_KEY_Up || k == GDK_KEY_Down) {
			int at = -1;
			for (int i = 0; i < (int)rows.size(); i++) if (rows[i].page == page) at = i;
			const int step = k == GDK_KEY_Up ? -1 : 1;
			for (int i = at + step; i >= 0 && i < (int)rows.size(); i += step)
				if (rows[i].page >= 0) { page = rows[i].page; scroll = 0; redraw(); break; }
			return true;
		}
		float to = scroll;
		if (k == GDK_KEY_Page_Down) to += view - 60;
		else if (k == GDK_KEY_Page_Up) to -= view - 60;
		else if (k == GDK_KEY_Home) to = 0;
		else if (k == GDK_KEY_End) to = contentH;
		else return false;
		scroll = std::max(0.0f, std::min(to, std::max(0.0f, contentH - view)));
		redraw();
		return true;
	}
};

HelpWindow* g_help = nullptr;

}  // namespace

GtkWidget* window() { return g_help ? g_help->win : nullptr; }

void show(CircuitWindow* from, const std::string& id) {
	int page = 0;
	for (int i = 0; i < (int)pages().size(); i++) if (id == pages()[i].id) page = i;
	if (g_help) {
		g_help->from = from;
		if (!id.empty()) { g_help->page = page; g_help->scroll = 0; g_help->redraw(); }
		gtk_window_present(GTK_WINDOW(g_help->win));
		return;
	}
	HelpWindow* w = new HelpWindow();
	g_help = w;
	w->from = from;
	w->page = page;
	w->buildRows();
	w->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(w->win), "CedarLogic Help");
	gtk_window_set_default_size(GTK_WINDOW(w->win), 980, 680);
	gtk_window_set_icon_name(GTK_WINDOW(w->win), "cedarlogic");
	GtkWidget* over = gtk_overlay_new();
	w->area = gtk_drawing_area_new();
	gtk_widget_add_events(w->area, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK |
	                                   GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
	gtk_container_add(GTK_CONTAINER(over), w->area);
	w->search = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(w->search), "Search Help");
	gtk_widget_set_halign(w->search, GTK_ALIGN_START);
	gtk_widget_set_valign(w->search, GTK_ALIGN_START);
	gtk_widget_set_margin_start(w->search, 14);
	gtk_widget_set_margin_top(w->search, 14);
	gtk_widget_set_size_request(w->search, (int)kSide - 28, (int)kSearchH);
	gtk_overlay_add_overlay(GTK_OVERLAY(over), w->search);
	gtk_container_add(GTK_CONTAINER(w->win), over);

	g_signal_connect(w->area, "draw", CL_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer data) -> gboolean {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		guarded("Help", [&] { hw->paint(cr, (float)gtk_widget_get_allocated_width(a), (float)gtk_widget_get_allocated_height(a)); });
		return TRUE;
	}), w);
	g_signal_connect(w->area, "motion-notify-event", CL_CALLBACK(+[](GtkWidget* a, GdkEventMotion* e, gpointer data) -> gboolean {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		const int i = hw->rowAt((float)e->x, (float)e->y);
		if (i != hw->hot) {
			hw->hot = i;
			GdkWindow* gw = gtk_widget_get_window(a);
			GdkCursor* cur = i >= 0 ? gdk_cursor_new_from_name(gdk_window_get_display(gw), "pointer") : nullptr;
			gdk_window_set_cursor(gw, cur);
			if (cur) g_object_unref(cur);
			hw->redraw();
		}
		return TRUE;
	}), w);
	g_signal_connect(w->area, "leave-notify-event", CL_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, gpointer data) -> gboolean {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		hw->hot = -1;
		hw->redraw();
		return FALSE;
	}), w);
	g_signal_connect(w->area, "button-release-event", CL_CALLBACK(+[](GtkWidget*, GdkEventButton* e, gpointer data) -> gboolean {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		const int i = hw->rowAt((float)e->x, (float)e->y);
		if (i >= 0 && e->button == 1) guarded("Help", [&] { hw->choose(hw->rows[i]); });
		return TRUE;
	}), w);
	g_signal_connect(w->area, "scroll-event", CL_CALLBACK(+[](GtkWidget*, GdkEventScroll* e, gpointer data) -> gboolean {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		double d = 0;
		if (e->direction == GDK_SCROLL_SMOOTH) { double dx = 0; gdk_event_get_scroll_deltas((GdkEvent*)e, &dx, &d); d *= 40; }
		else if (e->direction == GDK_SCROLL_UP) d = -48;
		else if (e->direction == GDK_SCROLL_DOWN) d = 48;
		const float view = hw->viewH();
		if (e->x < kSide) hw->listScroll = std::max(0.0f, std::min(hw->listScroll + (float)d, std::max(0.0f, hw->listH - (view - 52))));
		else hw->scroll = std::max(0.0f, std::min(hw->scroll + (float)d, std::max(0.0f, hw->contentH - view)));
		hw->redraw();
		return TRUE;
	}), w);
	g_signal_connect(w->search, "search-changed", CL_CALLBACK(+[](GtkSearchEntry* s, gpointer data) {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		hw->query = gtk_entry_get_text(GTK_ENTRY(s));
		hw->buildRows();
		for (const Row& r : hw->rows) if (r.page >= 0) { hw->page = r.page; hw->scroll = 0; break; }
		hw->redraw();
	}), w);
	g_signal_connect(w->win, "key-press-event", CL_CALLBACK(+[](GtkWidget*, GdkEventKey* e, gpointer data) -> gboolean {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		const guint k = e->keyval;
		// The search box keeps its own keys, but Up, Down and Escape are the window's.
		if (gtk_widget_has_focus(hw->search) && k != GDK_KEY_Up && k != GDK_KEY_Down && k != GDK_KEY_Escape) return FALSE;
		return hw->key(k) ? TRUE : FALSE;
	}), w);
	g_signal_connect(w->win, "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer data) {
		HelpWindow* hw = static_cast<HelpWindow*>(data);
		if (g_help == hw) g_help = nullptr;
		delete hw;
	}), w);
	gtk_widget_show_all(w->win);
	gtk_widget_grab_focus(w->search);
}

}  // namespace help
