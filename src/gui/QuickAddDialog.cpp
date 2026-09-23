/*****************************************************************************
   Project: CEDAR Logic Simulator
   QuickAddDialog: the gate picker the A key opens.

   A search field over a list of results, each drawn with the gate's own
   shape. Built like the Your Circuits window so the app has one idea of what
   a picker looks like.
*****************************************************************************/

#include "QuickAddDialog.h"
#include "GateLibrary.h"
#include "Settings.h"
#include "RenderMode.h"
#include "render/RenderStyle.h"
#include "wx/srchctrl.h"
#include "wx/scrolwin.h"
#include "wx/dcbuffer.h"
#include "wx/dcmemory.h"
#include "wx/sizer.h"
#include "wx/stattext.h"
#include "wx/graphics.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <functional>

DECLARE_APP(MainApp)

namespace {

const int ROW_H = 54, THUMB = 40;

bool isDark() { return renderMode().darkMode; }

wxColour accentColour() {
	cl::render::RenderStyle rs;
	rs.darkMode = isDark();
	rs.accentIndex = appConfig().appSettings.accentColor;
	const cl::render::Color c = rs.accent();
	return wxColour((unsigned char)(c.r * 255), (unsigned char)(c.g * 255), (unsigned char)(c.b * 255));
}

wxColour withAlpha(const wxColour& c, double a) {
	return wxColour(c.Red(), c.Green(), c.Blue(), (unsigned char)std::lround(255 * a));
}

wxColour paperColour() { return isDark() ? wxColour(28, 31, 37) : wxColour(250, 250, 252); }
wxColour inkColour()   { return isDark() ? wxColour(226, 230, 238) : wxColour(30, 33, 40); }
wxColour dimColour()   { return withAlpha(inkColour(), 0.55); }

}  // namespace

// A white-cleared bitmap. Two gotchas rolled into one helper: plain
// `wxBitmap(w,h)` is uninitialized (garbage, often black), and the default depth
// gives it a 32-bit alpha channel that GDI drawing never fills -- so the alpha
// stays 0 (fully transparent) and the image blanks out the moment the static
// control repaints through its alpha path. Force 24-bit (no alpha) and clear.
static wxBitmap blankPreview(int width, int height) {
	wxBitmap bmp(width, height, 24);
	wxMemoryDC dc(bmp);
	dc.SetBackground(wxBrush(paperColour()));
	dc.Clear();
	return bmp;
}

// The results, drawn by hand: the gate's picture, its name, and where it
// lives in the library.
class GateResultList : public wxScrolledCanvas {
public:
	struct Row {
		wxString caption;
		wxString detail;      // the gate's own name and library
		std::string gateName;
	};

	GateResultList(wxWindow* parent, QuickAddDialog* owner)
		: wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		                   wxBORDER_NONE | wxVSCROLL), owner(owner) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetBackgroundColour(paperColour());
		SetScrollRate(0, 1);
		Bind(wxEVT_PAINT, &GateResultList::OnPaint, this);
		Bind(wxEVT_MOTION, &GateResultList::OnMotion, this);
		Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover = -1; Refresh(); });
		Bind(wxEVT_LEFT_DOWN, &GateResultList::OnDown, this);
		Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& e) {
			const int i = RowAt(e.GetPosition());
			if (i >= 0) { Select(i); if (onActivate) onActivate(); }
		});
		glide.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Step(); });
		Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) {
			if (e.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL) { e.Skip(); return; }
			const double lines = (double)e.GetWheelRotation() /
			                     (e.GetWheelDelta() ? e.GetWheelDelta() : 120);
			GlideTo(target - lines * 3 * 18);
		});
	}

	void SetRows(std::vector<Row> r) {
		rows = std::move(r);
		selection = rows.empty() ? -1 : 0;
		SetVirtualSize(0, (int)rows.size() * ROW_H);
		glide.Stop();
		here = target = 0;
		Scroll(0, 0);
		Refresh();
	}
	int Count() const { return (int)rows.size(); }
	int Selection() const { return selection; }
	std::string SelectedGate() const {
		return (selection >= 0 && selection < (int)rows.size()) ? rows[selection].gateName : std::string();
	}
	void Select(int i) {
		if (rows.empty()) return;
		selection = std::max(0, std::min((int)rows.size() - 1, i));
		ScrollIntoView();
		Refresh();
	}
	void Move(int delta) { Select(selection + delta); }

	std::function<void()> onActivate;

private:
	int MaxScroll() const { return std::max(0, (int)rows.size() * ROW_H - GetClientSize().y); }
	void GlideTo(double y) {
		target = std::max(0.0, std::min((double)MaxScroll(), y));
		if (!glide.IsRunning()) glide.Start(16);
	}
	void Step() {
		const double d = target - here;
		if (std::fabs(d) < 0.5) { here = target; glide.Stop(); }
		else here += d * 0.28;
		Scroll(0, (int)std::lround(here));
	}
	void ScrollIntoView() {
		const int h = GetClientSize().y;
		const int top = selection * ROW_H, bottom = top + ROW_H;
		if (top < target) GlideTo(top - 4);
		else if (bottom > target + h) GlideTo(bottom - h + 4);
	}
	int RowAt(const wxPoint& p) const {
		const int y = p.y + GetViewStart().y;
		const int i = y / ROW_H;
		return (y >= 0 && i < (int)rows.size()) ? i : -1;
	}
	void OnMotion(wxMouseEvent& e) {
		const int h = RowAt(e.GetPosition());
		if (h != hover) { hover = h; Refresh(); }
	}
	void OnDown(wxMouseEvent& e) {
		const int i = RowAt(e.GetPosition());
		if (i >= 0) Select(i);
	}

	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(paperColour()));
		dc.Clear();
		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
		if (!gc) return;
		gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

		const int top = GetViewStart().y;
		const int w = GetClientSize().x;
		const wxColour accent = accentColour(), ink = inkColour();

		if (rows.empty()) {
			gc->SetFont(wxFont(wxFontInfo(13)), dimColour());
			const wxString msg = "No gates match that.";
			double tw, th;
			gc->GetTextExtent(msg, &tw, &th);
			gc->DrawText(msg, (w - tw) / 2.0, 30);
			return;
		}

		gc->Translate(0, -top);
		const int first = std::max(0, top / ROW_H);
		const int last = std::min((int)rows.size() - 1, (top + GetClientSize().y) / ROW_H);
		for (int i = first; i <= last; i++) {
			const Row& row = rows[i];
			const int y = i * ROW_H;
			const bool sel = (i == selection), hot = (i == hover);

			if (sel || hot) {
				gc->SetBrush(wxBrush(sel ? withAlpha(accent, isDark() ? 0.26 : 0.16)
				                         : withAlpha(ink, 0.06)));
				gc->SetPen(*wxTRANSPARENT_PEN);
				gc->DrawRoundedRectangle(8, y + 3, w - 16, ROW_H - 6, 11);
			}

			const wxBitmap thumb = owner->previewFor(row.gateName, THUMB);
			if (thumb.IsOk()) gc->DrawBitmap(thumb, 20, y + (ROW_H - THUMB) / 2.0, THUMB, THUMB);

			gc->SetFont(wxFont(wxFontInfo(13).Bold()), ink);
			gc->DrawText(row.caption, 76, y + 10);
			gc->SetFont(wxFont(wxFontInfo(10.5)), dimColour());
			gc->DrawText(row.detail, 76, y + 29);
		}
	}

	QuickAddDialog* owner;
	std::vector<Row> rows;
	int selection = -1, hover = -1;
	wxTimer glide;
	double here = 0, target = 0;
};

QuickAddDialog::QuickAddDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Add a Gate", wxDefaultPosition, wxSize(520, 520),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	SetBackgroundColour(paperColour());

	// Every gate in every library.
	auto& libraries = gateLibrary().libraries;
	for (auto& libPair : libraries) {
		for (auto& gatePair : libPair.second) {
			GateEntry entry;
			entry.gateName = gatePair.first;
			entry.caption = gatePair.second.caption;
			entry.libraryName = libPair.first;
			allGates.push_back(entry);
		}
	}

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* head = new wxStaticText(this, wxID_ANY, "Add a Gate");
	head->SetFont(wxFont(wxFontInfo(19).Bold()));
	head->SetForegroundColour(inkColour());
	topSizer->Add(head, 0, wxLEFT | wxRIGHT | wxTOP, 22);

	wxStaticText* hint = new wxStaticText(this, wxID_ANY,
		"Type to search, then press Return. The gate follows your mouse onto the canvas.");
	hint->SetForegroundColour(dimColour());
	topSizer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, 22);

	searchField = new wxSearchCtrl(this, wxID_ANY);
	searchField->ShowCancelButton(true);
	searchField->SetDescriptiveText("Search gates");
	topSizer->Add(searchField, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 22);

	resultList = new GateResultList(this, this);
	topSizer->Add(resultList, 1, wxALL | wxEXPAND, 14);

	SetSizer(topSizer);

	updateList("");

	resultList->onActivate = [this]() { confirm(); };
	searchField->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
		updateList(searchField->GetValue().ToStdString());
	});

	// Arrows move the highlight while you keep typing; Return picks.
	Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
		switch (e.GetKeyCode()) {
			case WXK_DOWN:     resultList->Move(1); return;
			case WXK_UP:       resultList->Move(-1); return;
			case WXK_PAGEDOWN: resultList->Move(8); return;
			case WXK_PAGEUP:   resultList->Move(-8); return;
			case WXK_RETURN:
			case WXK_NUMPAD_ENTER: confirm(); return;
			case WXK_ESCAPE:   EndModal(wxID_CANCEL); return;
			default: e.Skip();
		}
	});

	CentreOnParent();
	searchField->SetFocus();
}

wxBitmap QuickAddDialog::previewFor(const string& gateName, int size) {
	auto it = previewCache.find(gateName);
	if (it == previewCache.end())
		it = previewCache.emplace(gateName, renderGatePreview(gateName, size, size)).first;
	return it->second;
}

wxBitmap QuickAddDialog::renderGatePreview(const string& gateName, int width, int height) {
	string libName = gateLibrary().gateNameToLibrary[gateName];
	if (libName.empty()) return blankPreview(width, height);

	LibraryGate& gateDef = gateLibrary().libraries[libName][gateName];

	// Flatten every stroke -- straight lines plus the structured arcs and circles
	// (Workstream G) -- into segments in gate space. The old preview drew only
	// gateDef.shape, so curved bodies and inversion bubbles went missing, and a
	// curve-only gate (empty shape) rendered as a garbage bitmap.
	struct Seg { float x1, y1, x2, y2; };
	vector<Seg> segs;

	for (auto& line : gateDef.shape)
		segs.push_back({ line.x1, line.y1, line.x2, line.y2 });

	// Arc/circle tessellation matches guiGate's GL path: angle in degrees from
	// +Y increasing clockwise toward +X, point = (cx + r*sin, cy + r*cos).
	const float DEG = 3.14159265358979323846f / 180.0f;

	for (auto& a : gateDef.arcs) {
		const int N = 48;
		float px = a.cx + a.r * sinf(a.startDeg * DEG);
		float py = a.cy + a.r * cosf(a.startDeg * DEG);
		for (int i = 1; i <= N; i++) {
			float d = (a.startDeg + a.sweepDeg * (float)i / (float)N) * DEG;
			float x = a.cx + a.r * sinf(d), y = a.cy + a.r * cosf(d);
			segs.push_back({ px, py, x, y });
			px = x; py = y;
		}
	}

	for (auto& c : gateDef.circles) {
		int N = c.segs > 0 ? c.segs : 12;
		float px = c.cx, py = c.cy + c.r;  // start at the top, as the GL path does
		for (int i = 1; i <= N; i++) {
			float d = (360.0f * (float)i / (float)N) * DEG;
			float x = c.cx + c.r * sinf(d), y = c.cy + c.r * cosf(d);
			segs.push_back({ px, py, x, y });
			px = x; py = y;
		}
	}

	if (segs.empty()) return blankPreview(width, height);

	// Frame by the true extent of every stroke, not just the lines.
	float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
	for (auto& s : segs) {
		minX = min({minX, s.x1, s.x2});
		minY = min({minY, s.y1, s.y2});
		maxX = max({maxX, s.x1, s.x2});
		maxY = max({maxY, s.y1, s.y2});
	}

	float shapeW = maxX - minX;
	float shapeH = maxY - minY;
	if (shapeW < 0.001f) shapeW = 1.0f;
	if (shapeH < 0.001f) shapeH = 1.0f;

	// Supersampled anti-aliasing: plain GDI lines are crisp but jagged, while a
	// straight anti-aliased stroke at this size looks soft/blurry. So render at
	// SS times the resolution with a wxGraphicsContext (GDI+/Direct2D smooths the
	// edges), then downscale with a high-quality filter -- crisp AND smooth.
	const int SS = 3;
	const int W = width * SS, H = height * SS;
	const int margin = 12 * SS;
	const int drawW = W - 2 * margin;
	const int drawH = H - 2 * margin;

	float scale = min((float)drawW / shapeW, (float)drawH / shapeH);
	float offsetX = margin + (drawW - shapeW * scale) / 2.0f;
	float offsetY = margin + (drawH - shapeH * scale) / 2.0f;

	wxBitmap big(W, H, 24);  // 24-bit: no alpha channel (see blankPreview)
	wxMemoryDC dc(big);
	dc.SetBackground(wxBrush(paperColour()));
	dc.Clear();

	wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
	if (gc) {
		gc->SetPen(wxPen(inkColour(), 2.0 * SS));  // ~2px once downscaled
		wxGraphicsPath path = gc->CreatePath();
		for (auto& s : segs) {
			path.MoveToPoint(offsetX + (s.x1 - minX) * scale, offsetY + (maxY - s.y1) * scale);
			path.AddLineToPoint(offsetX + (s.x2 - minX) * scale, offsetY + (maxY - s.y2) * scale);
		}
		gc->StrokePath(path);
		delete gc;  // flush the drawing into the bitmap before it's read back
	} else {
		dc.SetPen(wxPen(inkColour(), 2 * SS));
		for (auto& s : segs) {
			dc.DrawLine((int)(offsetX + (s.x1 - minX) * scale), (int)(offsetY + (maxY - s.y1) * scale),
			            (int)(offsetX + (s.x2 - minX) * scale), (int)(offsetY + (maxY - s.y2) * scale));
		}
	}

	wxImage img = big.ConvertToImage();
	img.Rescale(width, height, wxIMAGE_QUALITY_HIGH);
	return wxBitmap(img);
}

int QuickAddDialog::fuzzyScore(const string& query, const string& target) {
	if (query.empty()) return 0;

	// Through unsigned char: tolower() of a negative char (any byte of a
	// non-ASCII character someone types) is undefined.
	string lowerQuery, lowerTarget;
	for (char c : query) lowerQuery += (char)tolower((unsigned char)c);
	for (char c : target) lowerTarget += (char)tolower((unsigned char)c);

	// Exact substring match gets highest score
	if (lowerTarget.find(lowerQuery) != string::npos) {
		// Prefer matches at the start
		if (lowerTarget.find(lowerQuery) == 0) return 100;
		return 80;
	}

	// Fuzzy: all query chars must appear in order
	int qi = 0;
	int score = 0;
	int lastMatch = -1;
	for (int ti = 0; ti < (int)lowerTarget.size() && qi < (int)lowerQuery.size(); ti++) {
		if (lowerTarget[ti] == lowerQuery[qi]) {
			score += 10;
			// Bonus for consecutive matches
			if (lastMatch == ti - 1) score += 5;
			// Bonus for matching at word boundaries
			if (ti == 0 || lowerTarget[ti - 1] == ' ' || lowerTarget[ti - 1] == '-' || lowerTarget[ti - 1] == '_')
				score += 5;
			lastMatch = ti;
			qi++;
		}
	}

	// All query chars must match
	if (qi < (int)lowerQuery.size()) return -1;
	return score;
}


void QuickAddDialog::updateList(const string& query) {
	struct ScoredEntry {
		int score;
		GateEntry entry;
	};
	vector<ScoredEntry> scored;

	for (auto& entry : allGates) {
		// Score against both caption and gate name
		int captionScore = fuzzyScore(query, entry.caption);
		int nameScore = fuzzyScore(query, entry.gateName);
		int bestScore = max(captionScore, nameScore);
		if (query.empty() || bestScore > 0) scored.push_back({bestScore, entry});
	}

	// Stable, so equal scores keep library order -- with nothing typed every
	// score is 0, and a plain sort shuffled the whole list.
	stable_sort(scored.begin(), scored.end(), [](const ScoredEntry& a, const ScoredEntry& b) {
		return a.score > b.score;
	});

	vector<GateResultList::Row> rows;
	for (auto& s : scored) {
		GateResultList::Row row;
		row.caption = s.entry.caption.empty() ? s.entry.gateName : s.entry.caption;
		row.detail = s.entry.libraryName;
		if (s.entry.caption != s.entry.gateName && !s.entry.caption.empty())
			row.detail = wxString(s.entry.gateName) + wxString::FromUTF8("  ·  ") + s.entry.libraryName;
		row.gateName = s.entry.gateName;
		rows.push_back(row);
	}
	resultList->SetRows(rows);
}

void QuickAddDialog::confirm() {
	const string gate = resultList->SelectedGate();
	if (gate.empty()) return;
	selectedGate = gate;
	EndModal(wxID_OK);
}
