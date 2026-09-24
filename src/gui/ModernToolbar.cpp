/*****************************************************************************
   Project: CEDAR Logic Simulator
   ModernToolbar: the custom-drawn toolbar and its styles.
*****************************************************************************/

#include "UiKit.h"
#include "ModernToolbar.h"
#include "MainFrame.h"
#include "MainApp.h"
#include "Settings.h"
#include "RenderMode.h"
#include "EmbeddedRes.h"
#include "render/RenderStyle.h"
#ifdef __APPLE__
#include "NativeIcons.h"
#include "MacAppearance.h"
#endif

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <map>
#include <cmath>

namespace cl {
namespace tb {

const char* styleName(int s) {
	switch (s) {
		case Classic:   return "Classic";
		case Segmented: return "Segmented";
		case Minimal:   return "Minimal";
		case Seamless:  return "Seamless";
	}
	return "";
}

const char* styleBlurb(int s) {
	switch (s) {
#ifdef __WXOSX__
		case Classic:   return "The standard macOS toolbar.";
#else
		case Classic:   return "The standard Windows toolbar.";
#endif
		case Segmented: return "Tools in tidy rounded groups, everything in reach.";
		case Minimal:   return "Just the essentials and your file name; the rest is behind the \u2022\u2022\u2022 menu.";
		case Seamless:  return "Blends into the canvas like one surface. Tools stay quiet until you point at them.";
	}
	return "";
}

const char* groupName(int g) {
	switch (g) {
		case GFile:      return "New, Open, Save";
		case GUndo:      return "Undo and Redo";
		case GClipboard: return "Copy and Paste";
		case GZoom:      return "Zoom";
		case GSim:       return "Pause, Step, Speed";
		case GRun:       return "Run (Simulation View)";
		case GLock:      return "Lock";
		case GTheme:     return "Dark mode";
		case GTab:       return "New tab";
	}
	return "";
}

bool State::operator==(const State& o) const {
	return dark == o.dark && paused == o.paused && locked == o.locked && simView == o.simView &&
	       canUndo == o.canUndo && canRedo == o.canRedo && zoomPct == o.zoomPct &&
	       stepMs == o.stepMs && accent == o.accent && title == o.title &&
	       subtitle == o.subtitle && canvas == o.canvas;
}

namespace {

const int BTN_W = 34, BTN_H = 30, RUN_W = 38, ZOOM_W = 48, SPEED_W = 96, MORE_W = 34;
const int GROUP_GAP = 12, EDGE = 12;
#ifdef __APPLE__
const int LEFT_EDGE = 86;   // clear of the red/yellow/green window buttons
#else
const int LEFT_EDGE = EDGE;
#endif

wxColour accentColour(const State& s) {
	cl::render::RenderStyle rs;
	rs.darkMode = s.dark;
	rs.accentIndex = s.accent;
	const cl::render::Color c = rs.accent();
	return wxColour((unsigned char)(c.r * 255), (unsigned char)(c.g * 255), (unsigned char)(c.b * 255));
}

wxColour withAlpha(const wxColour& c, double a) {
	return wxColour(c.Red(), c.Green(), c.Blue(), (unsigned char)std::lround(255 * a));
}

wxColour barColour(int style, const State& s) {
	if (style == Seamless) return s.canvas;
	if (style == Classic) return s.dark ? wxColour(44, 47, 54) : wxColour(236, 237, 240);
	return s.dark ? wxColour(28, 31, 37) : wxColour(246, 247, 249);
}

wxColour inkColour(const State& s) { return s.dark ? wxColour(210, 215, 224) : wxColour(52, 56, 64); }

// An icon from res/icons with its stroke recoloured, rasterised for this
// scale and cached.
wxBitmap icon(const char* name, const wxColour& c, int px, double scale) {
	static std::map<std::string, wxBitmap> cache;
	const std::string key = std::string(name) + "|" + c.GetAsString(wxC2S_HTML_SYNTAX).ToStdString() + "|" +
	                        std::to_string(c.Alpha()) + "|" + std::to_string(px) + "|" + std::to_string(scale);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	wxBitmap bmp;
#ifdef __APPLE__
	// Apple's own symbols, like the native toolbar uses, tinted to our colour.
	static const std::map<std::string, const char*> sf = {
		{ "new", "doc.badge.plus" }, { "open", "folder" }, { "save", "square.and.arrow.down" },
		{ "undo", "arrow.uturn.backward" }, { "redo", "arrow.uturn.forward" },
		{ "copy", "doc.on.doc" }, { "paste", "clipboard" },
		{ "zoomin", "plus.magnifyingglass" }, { "zoomout", "minus.magnifyingglass" },
		{ "pause", "pause.fill" }, { "play", "play.fill" }, { "step", "forward.frame.fill" },
		{ "run", "play.fill" }, { "stop", "stop.fill" }, { "speed", "gauge.with.dots.needle.50percent" },
		{ "locked", "lock.fill" }, { "unlocked", "lock.open" },
		{ "moon", "moon" }, { "sun", "sun.max" }, { "newtab", "plus.square.on.square" },
		{ "more", "ellipsis" } };
	auto m = sf.find(name);
	if (m != sf.end()) bmp = NativeIcon_TintedSFSymbol(m->second, px * 0.88, c.Red(), c.Green(), c.Blue(), c.Alpha(), scale);
	if (bmp.IsOk()) { cache[key] = bmp; return bmp; }
#endif
	wxString svg = cl::res::text(("icons/" + std::string(name) + ".svg").c_str());
	if (!svg.empty()) {
		svg.Replace("stroke=\"#333333\"", wxString::Format("stroke=\"%s\" stroke-opacity=\"%.3f\"",
			c.GetAsString(wxC2S_HTML_SYNTAX), c.Alpha() / 255.0));
		wxScopedCharBuffer buf = svg.utf8_str();
		const int dev = (int)std::lround(px * scale);
		bmp = wxBitmapBundle::FromSVG(buf.data(), wxSize(dev, dev)).GetBitmap(wxSize(dev, dev));
		bmp.SetScaleFactor(scale);
	}
	cache[key] = bmp;
	return bmp;
}

// Draw an icon centered in `r` at its own (logical) size.
void drawIcon(wxGraphicsContext* gc, const wxBitmap& b, const wxRect& r) {
	if (!b.IsOk()) return;
	const double s = b.GetScaleFactor() > 0 ? b.GetScaleFactor() : 1.0;
	const double w = b.GetWidth() / s, h = b.GetHeight() / s;
	gc->DrawBitmap(b, std::round(r.x + (r.width - w) / 2.0), std::round(r.y + (r.height - h) / 2.0), w, h);
}

}  // namespace

std::vector<Item> layout(int style, int hidden, const State& s, int W, int H) {
	std::vector<Item> out;
	auto shown = [hidden](int g) { return !(hidden & (1 << g)); };
	const int y = (H - BTN_H) / 2;
	auto add = [&](Item::Kind k, int id, const char* ic, const wxString& tip, int seg, int& x, int w, bool leftToRight) {
		Item it{ k, id, ic, tip, seg, wxRect() };
		if (leftToRight) { it.rect = wxRect(x, y, w, BTN_H); x += w; }
		else { x -= w; it.rect = wxRect(x, y, w, BTN_H); }
		out.push_back(it);
	};
#ifdef __WXOSX__
	const wxString mod = "Cmd+";
#else
	const wxString mod = "Ctrl+";
#endif

	if (style == Minimal) {
		int x = LEFT_EDGE;
		if (shown(GUndo)) {
			add(Item::Button, wxID_UNDO, "undo", "Undo (" + mod + "Z)", 1, x, BTN_W, true);
			add(Item::Button, wxID_REDO, "redo", "Redo (" + mod + "Shift+Z)", 1, x, BTN_W, true);
		}
		int r = W - EDGE;
		add(Item::More, 0, "more", "More", 9, r, MORE_W, false);
		r -= 6;
		if (shown(GRun)) add(Item::Run, Tool_SimView, "run", "Simulation View (" + mod + "R)", 8, r, RUN_W, false);
		r -= 6;
		if (shown(GSim)) add(Item::Toggle, Tool_Pause, nullptr, "Pause or resume the simulation", 7, r, BTN_W, false);
		// Centered on the window, like a document title in any Mac app.
		const int half = std::min(W / 2 - x, r - W / 2) - 16;
		const int tw = std::max(0, std::min(360, 2 * half));
		Item title{ Item::Title, 0, nullptr, "", 5, wxRect((W - tw) / 2, 0, tw, H) };
		if (tw > 60) out.push_back(title);
		return out;
	}

	int x = LEFT_EDGE;
	if (shown(GFile)) {
		add(Item::Button, wxID_NEW, "new", "New circuit (" + mod + "N)", 1, x, BTN_W, true);
		add(Item::Button, wxID_OPEN, "open", "Open (" + mod + "O)", 1, x, BTN_W, true);
		add(Item::Button, wxID_SAVE, "save", "Save (" + mod + "S)", 1, x, BTN_W, true);
		x += GROUP_GAP;
	}
	if (shown(GUndo)) {
		add(Item::Button, wxID_UNDO, "undo", "Undo (" + mod + "Z)", 2, x, BTN_W, true);
		add(Item::Button, wxID_REDO, "redo", "Redo (" + mod + "Shift+Z)", 2, x, BTN_W, true);
		x += GROUP_GAP;
	}
	if (shown(GClipboard)) {
		add(Item::Button, wxID_COPY, "copy", "Copy (" + mod + "C)", 3, x, BTN_W, true);
		add(Item::Button, wxID_PASTE, "paste", "Paste (" + mod + "V)", 3, x, BTN_W, true);
		x += GROUP_GAP;
	}
	if (shown(GZoom)) {
		add(Item::Button, Tool_ZoomOut, "zoomout", "Zoom out (" + mod + "-)", 4, x, BTN_W, true);
		add(Item::Zoom, View_ZoomActual, nullptr, "Zoom level. Click for 100% (" + mod + "1)", 4, x, ZOOM_W, true);
		add(Item::Button, Tool_ZoomIn, "zoomin", "Zoom in (" + mod + "=)", 4, x, BTN_W, true);
		x += GROUP_GAP;
	}

	int r = W - EDGE;
#ifdef __WXMSW__
	// Windows has no menu bar (see MainFrame::ShowAppMenu): this is the menu.
	add(Item::More, 0, "more", "Menu", 14, r, MORE_W, false);
	r -= GROUP_GAP;
#endif
	if (shown(GTab))   { add(Item::Button, Tool_NewTab, "newtab", "New tab (" + mod + "T)", 13, r, BTN_W, false); r -= GROUP_GAP; }
	if (shown(GTheme)) { add(Item::Toggle, Tool_ThemeToggle, nullptr, "Dark mode", 12, r, BTN_W, false); r -= GROUP_GAP; }
	if (shown(GLock))  { add(Item::Toggle, Tool_Lock, nullptr, "Lock the circuit so it can't be edited", 11, r, BTN_W, false); r -= GROUP_GAP; }
	if (shown(GRun))   { add(Item::Run, Tool_SimView, "run", "Simulation View (" + mod + "R)", 10, r, RUN_W, false); r -= GROUP_GAP; }
	if (shown(GSim) && r - SPEED_W - 2 * BTN_W > x) {
		add(Item::Speed, 0, "speed", "Simulation speed", 6, r, SPEED_W, false);
		add(Item::Button, Tool_Step, "step", "Step once", 6, r, BTN_W, false);
		add(Item::Toggle, Tool_Pause, nullptr, "Pause or resume the simulation", 6, r, BTN_W, false);
	}
	return out;
}

void paint(wxGraphicsContext* gc, int style, const std::vector<Item>& items, const State& s,
           int W, int H, int hover, int pressed, double scale) {
	const wxColour bar = barColour(style, s);
	const wxColour ink = inkColour(s);
	const wxColour accent = accentColour(s);
	const wxColour hoverFill = s.dark ? wxColour(255, 255, 255, 20) : wxColour(0, 0, 0, 13);
	const wxColour pressFill = s.dark ? wxColour(255, 255, 255, 34) : wxColour(0, 0, 0, 24);

	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(bar));
	gc->DrawRectangle(0, 0, W, H);
	if (style != Seamless) {
		gc->SetPen(wxPen(s.dark ? wxColour(0, 0, 0, 90) : wxColour(0, 0, 0, 20), 1));
		gc->StrokeLine(0, H - 0.5, W, H - 0.5);
	}

	// Segmented and Classic group their tools in soft capsules, the way the
	// macOS toolbar does; Seamless and Minimal leave them floating.
	if (style == Segmented || style == Classic) {
		std::map<int, wxRect> groups;
		for (const Item& it : items) {
			if (it.kind == Item::Title) continue;
			auto g = groups.find(it.segment);
			if (g == groups.end()) groups[it.segment] = it.rect;
			else g->second.Union(it.rect);
		}
		gc->SetPen(style == Classic ? wxPen(s.dark ? wxColour(255, 255, 255, 18) : wxColour(0, 0, 0, 16), 1)
		                            : *wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(s.dark ? wxColour(255, 255, 255, 16) : wxColour(0, 0, 0, 11)));
		for (auto& g : groups) {
			const wxRect& r = g.second;
			const double h = r.height + 2;
			gc->DrawRoundedRectangle(r.x - 3 + 0.5, r.y - 1 + 0.5, r.width + 6 - 1, h - 1, h / 2.0);
		}
	}

	for (size_t i = 0; i < items.size(); i++) {
		const Item& it = items[i];
		const wxRect& r = it.rect;
		const bool isHover = (int)i == hover, isPressed = (int)i == pressed && isHover;
		const bool disabled = (it.id == wxID_UNDO && !s.canUndo) || (it.id == wxID_REDO && !s.canRedo);

		if (it.kind == Item::Title) {
			gc->SetFont(wxFont(wxFontInfo(13).Bold()), ink);
			double tw, th;
			gc->GetTextExtent(s.title, &tw, &th);
			gc->DrawText(s.title, std::round(r.x + (r.width - tw) / 2), std::round(H / 2.0 - th + 1));
			gc->SetFont(wxFont(wxFontInfo(11)), withAlpha(ink, 0.5));
			double sw, sh;
			gc->GetTextExtent(s.subtitle, &sw, &sh);
			gc->DrawText(s.subtitle, std::round(r.x + (r.width - sw) / 2), std::round(H / 2.0 + 1));
			continue;
		}

		const bool toggledOn = (it.id == Tool_Pause && s.paused) || (it.id == Tool_Lock && s.locked) ||
		                       (it.kind == Item::Run && s.simView);
		const bool hoverable = it.kind != Item::Speed;
		if (hoverable && (toggledOn || isHover)) {
			gc->SetPen(*wxTRANSPARENT_PEN);
			const wxColour f = toggledOn ? withAlpha(accent, isPressed ? 0.30 : (isHover ? 0.24 : 0.18))
			                             : (isPressed ? pressFill : hoverFill);
			gc->SetBrush(wxBrush(f));
			gc->DrawRoundedRectangle(r.x + 2, r.y + 1, r.width - 4, r.height - 2, 7);
		}

		// Seamless keeps idle tools quiet; they come up to full strength on hover.
		const double alpha = disabled ? 0.3 : (style == Seamless && !isHover && !toggledOn ? 0.5 : 0.92);
		// Run is the one colored tool: it's what the toolbar is for.
		const wxColour fg = (toggledOn || it.kind == Item::Run) ? accent : withAlpha(ink, alpha);

		if (it.kind == Item::Zoom) {
			const wxString t = wxString::Format("%d%%", s.zoomPct);
			gc->SetFont(wxFont(wxFontInfo(12)), fg);
			double tw, th;
			gc->GetTextExtent(t, &tw, &th);
			gc->DrawText(t, std::round(r.x + (r.width - tw) / 2), std::round(r.y + (r.height - th) / 2));
			continue;
		}
		if (it.kind == Item::Speed) {
			// A gauge, then a slim slider. The exact step time is in the tooltip.
			drawIcon(gc, icon("speed", fg, 16, scale), wxRect(r.x, r.y, 24, r.height));
			const double tx = r.x + 30, tw = r.width - 38, ty = r.y + r.height / 2.0;
			const double f = 1.0 - std::min(1.0, std::max(0.0, std::log(std::max(1, s.stepMs)) / std::log(500.0)));
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(withAlpha(ink, 0.16)));
			gc->DrawRoundedRectangle(tx, ty - 1.5, tw, 3, 1.5);
			gc->SetBrush(wxBrush(withAlpha(ink, style == Seamless && !isHover ? 0.45 : 0.7)));
			gc->DrawRoundedRectangle(tx, ty - 1.5, std::max(3.0, tw * f), 3, 1.5);
			gc->SetBrush(wxBrush(s.dark ? wxColour(236, 239, 244) : *wxWHITE));
			gc->SetPen(wxPen(s.dark ? wxColour(0, 0, 0, 120) : wxColour(0, 0, 0, 50), 1));
			gc->DrawEllipse(tx + tw * f - 6.5, ty - 6.5, 13, 13);
			continue;
		}

		const char* name = it.icon;
		if (it.id == Tool_Pause) name = s.paused ? "play" : "pause";
		else if (it.id == Tool_Lock) name = s.locked ? "locked" : "unlocked";
		else if (it.id == Tool_ThemeToggle) name = s.dark ? "moon" : "sun";
		else if (it.kind == Item::Run) name = s.simView ? "stop" : "run";
		if (name == nullptr) continue;
		drawIcon(gc, icon(name, fg, 17, scale), r);
	}
}

}  // namespace tb
}  // namespace cl

// ---------------------------------------------------------------------------

using namespace cl::tb;

ModernToolbar::ModernToolbar(wxWindow* parent, MainFrame* frame)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, BarHeight()), wxBORDER_NONE),
	  frame(frame) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, BarHeight()));
	Bind(wxEVT_PAINT, &ModernToolbar::OnPaint, this);
	Bind(wxEVT_MOTION, &ModernToolbar::OnMotion, this);
	Bind(wxEVT_LEAVE_WINDOW, &ModernToolbar::OnLeave, this);
	Bind(wxEVT_LEFT_DOWN, &ModernToolbar::OnDown, this);
	Bind(wxEVT_LEFT_DCLICK, &ModernToolbar::OnDown, this);
	Bind(wxEVT_LEFT_UP, &ModernToolbar::OnUp, this);
	Bind(wxEVT_SIZE, &ModernToolbar::OnSize, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { draggingSpeed = false; pressed = -1; });
	state = readState();
}

wxColour ModernToolbar::BarColour() const {
	return barColour(appConfig().appSettings.toolbarStyle, readState());
}

wxColour ModernToolbar::InkColour() const { return inkColour(readState()); }

State ModernToolbar::readState() const {
	State s;
	s.dark = renderMode().darkMode;
	s.simView = renderMode().simView;
	s.accent = appConfig().appSettings.accentColor;
	if (frame) {
		s.paused = frame->IsSimPaused();
		s.locked = frame->IsLockToolOn();
		s.canUndo = frame->CanUndoCommand();
		s.canRedo = frame->CanRedoCommand();
		s.zoomPct = frame->GetZoomPercent();
		s.stepMs = frame->GetStepMs();
		s.title = frame->GetDocumentTitle();
		s.subtitle = frame->GetDocumentSubtitle();
	}
	s.canvas = s.simView ? wxColour(8, 10, 13) : (s.dark ? wxColour(19, 21, 25) : *wxWHITE);
	return s;
}

void ModernToolbar::relayout() {
	const wxSize sz = GetClientSize();
	items = layout(appConfig().appSettings.toolbarStyle, appConfig().appSettings.toolbarHidden, state, sz.x, sz.y);
	hover = pressed = -1;
}

void ModernToolbar::Reconfigure() {
	state = readState();
	relayout();
	Refresh();
}

void ModernToolbar::Poll() {
	if (!IsShown()) return;
	const State s = readState();
	if (s == state) return;
	state = s;
	Refresh();
}

void ModernToolbar::OnSize(wxSizeEvent& e) {
	relayout();
	Refresh();
	e.Skip();
}

void ModernToolbar::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
	if (!gc) return;
	const wxSize sz = GetClientSize();
	paint(gc.get(), appConfig().appSettings.toolbarStyle, items, state, sz.x, sz.y, hover, pressed,
	      GetContentScaleFactor());
}

int ModernToolbar::hitTest(const wxPoint& p) const {
	for (size_t i = 0; i < items.size(); i++)
		if (items[i].kind != Item::Title && items[i].rect.Contains(p)) return (int)i;
	return -1;
}

void ModernToolbar::OnMotion(wxMouseEvent& e) {
	if (draggingSpeed && pressed >= 0) {
		const wxRect& r = items[pressed].rect;
		setSpeedFromX(e.GetX(), wxRect(r.x + 30, r.y, r.width - 38, r.height));
		SetToolTip(wxString::Format("Simulation speed: %d ms per step", state.stepMs));
		return;
	}
	const int h = hitTest(e.GetPosition());
	if (h == hover) return;
	hover = h;
	if (h >= 0 && items[h].kind == Item::Speed)
		SetToolTip(wxString::Format("Simulation speed: %d ms per step", state.stepMs));
	else if (h >= 0 && !items[h].tip.empty()) SetToolTip(items[h].tip);
	else UnsetToolTip();
	Refresh();
}

void ModernToolbar::OnLeave(wxMouseEvent&) {
	if (draggingSpeed) return;
	hover = -1;
	Refresh();
}

void ModernToolbar::OnDown(wxMouseEvent& e) {
	pressed = hitTest(e.GetPosition());
#ifdef __APPLE__
	// Empty space on the bar behaves like a title bar.
	if (pressed < 0) {
		void* win = wxGetTopLevelParent(this)->MacGetTopLevelWindowRef();
		if (e.LeftDClick()) MacTitlebarDoubleClick(win);
		else MacDragWindow(win);
		return;
	}
#endif
	if (pressed >= 0 && items[pressed].kind == Item::Speed) {
		draggingSpeed = true;
		CaptureMouse();
		const wxRect& r = items[pressed].rect;
		setSpeedFromX(e.GetX(), wxRect(r.x + 30, r.y, r.width - 38, r.height));
	}
	Refresh();
}

void ModernToolbar::OnUp(wxMouseEvent& e) {
	if (draggingSpeed) {
		draggingSpeed = false;
		if (HasCapture()) ReleaseMouse();
		pressed = -1;
		Refresh();
		return;
	}
	const int h = hitTest(e.GetPosition());
	const int p = pressed;
	pressed = -1;
	Refresh();
	if (p >= 0 && p == h) activate(items[p]);
}

void ModernToolbar::setSpeedFromX(int x, const wxRect& track) {
	const double f = std::min(1.0, std::max(0.0, (x - track.x) / (double)track.width));
	frame->SetStepMs((int)std::lround(std::pow(500.0, 1.0 - f)));
	Poll();
}

void ModernToolbar::activate(const Item& it) {
	auto send = [this](int id) {
		wxCommandEvent ev(wxEVT_MENU, id);
		ev.SetEventObject(frame);
		frame->ProcessWindowEvent(ev);
	};
	switch (it.kind) {
		case Item::Run:  frame->SetSimView(!renderMode().simView); break;
		case Item::More: {
#ifdef __WXMSW__
			// The whole menu bar lives here on Windows, where the bar is hidden.
			frame->ShowAppMenu(this, it.rect.GetBottomLeft(),
			                   appConfig().appSettings.toolbarStyle == cl::tb::Minimal);
			break;
#endif
			wxMenu menu;
			menu.Append(wxID_NEW, "New");
			menu.Append(wxID_OPEN, "Open...");
			menu.Append(wxID_SAVE, "Save");
			menu.AppendSeparator();
			menu.Append(wxID_COPY, "Copy");
			menu.Append(wxID_PASTE, "Paste");
			menu.Append(Edit_Duplicate, "Duplicate");
			menu.AppendSeparator();
			menu.Append(Tool_ZoomIn, "Zoom In");
			menu.Append(Tool_ZoomOut, "Zoom Out");
			menu.Append(View_ZoomFit, "Zoom to Fit");
			menu.AppendSeparator();
			menu.Append(Tool_Step, "Step Once");
			menu.AppendCheckItem(Tool_Lock, "Lock")->Check(state.locked);
			menu.AppendCheckItem(Tool_ThemeToggle, "Dark Mode")->Check(state.dark);
			menu.AppendSeparator();
			menu.Append(View_TruthTable, "Truth Table...");
			menu.Append(Tool_NewTab, "New Tab");
			const int chosen = GetPopupMenuSelectionFromUser(menu, it.rect.GetBottomLeft());
			if (chosen == wxID_NONE) break;
			Item picked{ Item::Button, chosen, nullptr, "", 0, wxRect() };
			if (chosen == Tool_Lock || chosen == Tool_ThemeToggle || chosen == Tool_Step) picked.kind = Item::Toggle;
			activate(picked);
			break;
		}
		default:
			switch (it.id) {
				case Tool_Pause:       frame->SetSimPaused(!frame->IsSimPaused()); break;
				case Tool_Step:        frame->StepSimOnce(); break;
				case Tool_Lock:        frame->SetLockTool(!frame->IsLockToolOn()); break;
				case Tool_ThemeToggle: frame->ToggleDarkMode(); break;
				case 0: break;
				default:               send(it.id); break;
			}
	}
	Poll();
}

wxBitmap ModernToolbar::RenderPreview(int style, bool dark, int width, double scale) {
	State s;
	s.dark = dark;
	s.accent = appConfig().appSettings.accentColor;
	s.title = wxString::FromUTF8("Lab 5 \u2014 Full Adder");
	s.subtitle = wxString::FromUTF8("Page 1 \u00B7 Saved");
	s.canvas = dark ? wxColour(19, 21, 25) : *wxWHITE;
	s.canRedo = false;
	const int H = BarHeight();
	// The preview shows the toolbar you would actually get, hidden tools and
	// all -- it used to ignore the checkboxes below it and draw every tool.
	const std::vector<Item> items = layout(style, appConfig().appSettings.toolbarHidden, s, width, H);
	wxBitmap bmp((int)std::lround(width * scale), (int)std::lround(H * scale), 24);
	{
		wxMemoryDC dc(bmp);
		std::unique_ptr<wxGraphicsContext> gc(ui::graphics(dc));
		if (gc) {
			gc->Scale(scale, scale);
			paint(gc.get(), style, items, s, width, H, -1, -1, scale);
#ifdef __APPLE__
			// Stand-ins for the window's own buttons, so the picture reads as
			// the top of a real window.
			const wxColour lights[3] = { wxColour(255, 95, 87), wxColour(254, 188, 46), wxColour(40, 200, 64) };
			gc->SetPen(*wxTRANSPARENT_PEN);
			for (int i = 0; i < 3; i++) {
				gc->SetBrush(wxBrush(lights[i]));
				gc->DrawEllipse(18 + i * 20, H / 2.0 - 6, 12, 12);
			}
#endif
		}
	}
	bmp.SetScaleFactor(scale);
	return bmp;
}
