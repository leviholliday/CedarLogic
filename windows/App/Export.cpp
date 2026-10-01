// Export as Image, as the Mac app's (ExportImageView.swift, after the wx
// app's): a preview, the grid if wanted, and a strip under the circuit with
// your name and whether it works -- what a grader looks for first -- in
// colour or black and white, at 2x, 4x or 6x. Copied, or saved as a PNG.

#include "Chrome.h"
#include "Dialogs.h"
#include "Images.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace {

const float kPointsPerUnit = 12, kMargin = 24;
const float kPad = 22, kBody = 17, kSmall = 11, kGap = 8;

struct ExportInfo {
	bool enabled = false;
	std::string name, why, fileName;
	bool works = true;
};

FormField choiceField(const char* label, std::vector<std::string> items, int active) {
	FormField x;
	x.kind = FormField::Choice;
	x.label = label;
	x.choices = std::move(items);
	x.value = std::to_string(active);
	return x;
}

std::string trimmed(const std::string& s) {
	const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// The strip's lines and height for a width.
std::vector<std::string> stripLines(const ExportInfo& info, float width, float& height) {
	std::string statement = info.works ? "My circuit works properly." : "My circuit does not work because " + trimmed(info.why);
	for (char& c : statement) if (c == '\r' || c == '\n') c = ' ';
	std::vector<std::string> lines;
	std::string line;
	size_t i = 0;
	while (i < statement.size()) {
		const size_t j = statement.find(' ', i);
		const std::string word = statement.substr(i, j == std::string::npos ? std::string::npos : j - i);
		i = j == std::string::npos ? statement.size() : j + 1;
		if (word.empty()) continue;
		const std::string trial = line.empty() ? word : line + " " + word;
		if (!line.empty() && textWidth(trial, kBody) > width - 2 * kPad) { lines.push_back(line); line = word; }
		else line = trial;
	}
	if (!line.empty()) lines.push_back(line);
	height = kPad + kBody + kGap * 1.6f + lines.size() * (kBody + kGap) + kPad * 0.5f;
	return lines;
}

// The whole image's size in points; false for an empty page.
bool imageSize(CLDocument* doc, int page, const ExportInfo& info, float& w, float& h) {
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) return false;
	w = std::max((float)(r - l) * kPointsPerUnit + 2 * kMargin, info.enabled ? 420.0f : 0.0f);
	h = (float)(t - b) * kPointsPerUnit + 2 * kMargin;
	if (info.enabled) {
		float sh = 0;
		stripLines(info, w, sh);
		h += sh;
	}
	w = std::ceil(w);
	h = std::ceil(h);
	return true;
}

// Into a target in points, y down, `w` x `h` points; `scale` is pixels a point.
void drawImage(ID2D1RenderTarget* rt, CLDocument* doc, int page, float w, float h, double scale, bool color, bool grid,
               const ExportInfo& info) {
	float stripH = 0;
	std::vector<std::string> lines;
	if (info.enabled) lines = stripLines(info, w, stripH);
	const float circuitH = h - stripH;
	fillRect(rt, D2D1::RectF(0, 0, w, h), D2D1::ColorF(1, 1, 1));
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) return;
	const double midX = (l + r) / 2, midY = (b + t) / 2;
	rt->PushAxisAlignedClip(D2D1::RectF(0, 0, w, circuitH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	if (grid) {
		const double upp = 1 / kPointsPerUnit;
		const double ox = midX - w / 2 * upp, oy = midY + circuitH / 2 * upp;
		const D2D1_COLOR_F line = D2D1::ColorF(0, 0, 0.2f, 0.2f);
		ID2D1SolidColorBrush* br = nullptr;
		if (SUCCEEDED(rt->CreateSolidColorBrush(line, &br))) {
			for (double x = std::ceil(ox); x < ox + w * upp; x += 1) {
				const float sx = (float)((x - ox) / upp);
				rt->DrawLine(D2D1::Point2F(sx, 0), D2D1::Point2F(sx, circuitH), br, 0.5f);
			}
			for (double y = std::floor(oy); y > oy - circuitH * upp; y -= 1) {
				const float sy = (float)((oy - y) / upp);
				rt->DrawLine(D2D1::Point2F(0, sy), D2D1::Point2F(w, sy), br, 0.5f);
			}
			br->Release();
		}
	}
	if (!color) {
		cl_document_draw_fitted(doc, page, rt, w, circuitH, kMargin, scale, CL_STYLE_PRINT);
	} else {
		const double upp = std::max((r - l) / (w - 2 * kMargin), (t - b) / (circuitH - 2 * kMargin));
		CLDrawOptions o = {};
		o.dark = false;
		o.accent = prefs().accent;
		o.wireScale = 1;
		o.simView = false;
		o.thumbnail = false;
		o.showSelection = false;
		o.selectionFade = 1;
		cl_document_draw_ex(doc, page, rt, scale, midX - w / 2 * upp, midY + circuitH / 2 * upp, upp, &o);
	}
	rt->PopAxisAlignedClip();
	if (!info.enabled) return;

	// The name-and-result strip, black on white so it prints.
	const D2D1_COLOR_F ink = D2D1::ColorF(0, 0, 0), gray = D2D1::ColorF(0.4f, 0.4f, 0.4f);
	fillRect(rt, D2D1::RectF(kPad, circuitH, w - kPad, circuitH + 1), ink);
	float y = circuitH + kPad;
	auto text = [&](const std::string& s, float x, float top, float size, const D2D1_COLOR_F& c, TextAlign a = TextAlign::Leading) {
		const D2D1_RECT_F box = a == TextAlign::Trailing ? D2D1::RectF(kPad, top, x, top + size * 1.6f) : D2D1::RectF(x, top, w, top + size * 1.6f);
		drawText(rt, s, box, size, c, a);
	};
	const std::string label = "Name:";
	text(label, kPad, y, kBody, gray);
	const std::string name = trimmed(info.name);
	text(name.empty() ? "________________" : name, kPad + textWidth(label, kBody) + 8, y, kBody, ink);
	char date[64];
	const time_t now = time(nullptr);
	struct tm lt;
	localtime_s(&lt, &now);
	strftime(date, sizeof date, "%b %d, %Y", &lt);
	const std::string meta = info.fileName.empty() ? std::string(date) : info.fileName + "   " + date;
	text(meta, w - kPad, y + kBody - kSmall, kSmall, gray, TextAlign::Trailing);
	y += kBody + kGap * 1.6f;
	for (const std::string& line : lines) { text(line, kPad, y, kBody, ink); y += kBody + kGap; }
}

IWICBitmap* makeImage(CLDocument* doc, int page, double multiplier, bool color, bool grid, const ExportInfo& info) {
	float w, h;
	if (!imageSize(doc, page, info, w, h)) return nullptr;
	double scale = multiplier;
	if (std::max(w, h) * scale > 12000) scale = 12000 / std::max(w, h);
	return images::render(w, h, scale, true, [&](ID2D1RenderTarget* rt) { drawImage(rt, doc, page, w, h, scale, color, grid, info); });
}

}  // namespace

void showExportImage(CircuitWindow* win, int page) {
	CLDocument* doc = win->document();
	double l, b, r, t;
	if (!cl_document_page_bounds(doc, page, &l, &b, &r, &t)) { win->note("This page is empty: nothing to export."); return; }
	Prefs& p = prefs();
	std::string fileName = win->titleText();
	if (cl_document_page_count(doc) > 1) fileName += " - " + win->pageName(page);

	Form f;
	f.title = "Export as Image";
	f.width = 560;
	f.okText = "Export to File…";
	f.buttons = { "Copy to Clipboard" };

	FormField preview;
	preview.kind = FormField::Picture;
	preview.height = 220;
	const int previewField = f.add(preview);
	FormField gridBox;
	gridBox.kind = FormField::Check;
	gridBox.label = "Include grid lines";
	gridBox.value = p.exportGrid ? "1" : "";
	const int gridField = f.add(gridBox);
	FormField heading;
	heading.kind = FormField::Note;
	heading.label = "Name and result";
	f.add(heading);
	FormField infoBox;
	infoBox.kind = FormField::Check;
	infoBox.label = "Add my name and whether the circuit works";
	infoBox.value = p.exportInfo ? "1" : "";
	const int infoField = f.add(infoBox);
	FormField nameBox;
	nameBox.kind = FormField::Text;
	nameBox.label = "Your name";
	nameBox.value = p.studentName;
	const int nameField = f.add(nameBox);
	const int worksField = f.add(choiceField("Result", { "My circuit works properly", "My circuit does not work because…" }, p.exportWorks ? 0 : 1));
	FormField whyBox;
	whyBox.kind = FormField::Text;
	whyBox.label = "Because";
	whyBox.lines = 2;
	whyBox.value = p.exportProblem;
	whyBox.tip = "Explain what doesn't work (required)";
	const int whyField = f.add(whyBox);
	const int styleField = f.add(choiceField("Output style", { "Color", "Black & White" }, p.exportColor ? 0 : 1));
	const int resField = f.add(choiceField("Resolution", { "Screen (2×)", "Print (4×)", "High Quality (6×)" },
	                                       p.exportScale <= 2 ? 0 : p.exportScale >= 6 ? 2 : 1));

	// The choices as they stand in the open dialog.
	struct State { ExportInfo info; bool color, grid; int multiplier; };
	auto state = [&](Form& form) {
		State s;
		s.grid = form.checked(gridField);
		s.info.enabled = form.checked(infoField);
		s.info.name = form.text(nameField);
		s.info.works = form.choice(worksField) == 0;
		s.info.why = form.text(whyField);
		s.info.fileName = fileName;
		s.color = form.choice(styleField) == 0;
		const int res = form.choice(resField);
		s.multiplier = res == 0 ? 2 : res == 2 ? 6 : 4;
		return s;
	};
	auto keep = [&](const State& s) {
		p.exportGrid = s.grid;
		p.exportInfo = s.info.enabled;
		p.studentName = trimmed(s.info.name);
		p.exportWorks = s.info.works;
		p.exportProblem = s.info.why;
		p.exportColor = s.color;
		p.exportScale = s.multiplier;
		p.save();
	};
	// A "does not work" answer needs its reason before anything goes out.
	auto problem = [&](const State& s) -> std::string {
		if (s.info.enabled && !s.info.works && trimmed(s.info.why).empty()) return "Say what doesn't work, or choose “works properly”.";
		return "";
	};
	auto enableFields = [&](Form& form) {
		const bool on = form.checked(infoField);
		form.enable(nameField, on);
		form.enable(worksField, on);
		form.enable(whyField, on && form.choice(worksField) == 1);
	};

	f.fields[previewField].paint = [&](ID2D1RenderTarget* rt, float pw, float ph) {
		if (f.dialog == nullptr) return;
		const State s = state(f);
		float w, h;
		if (!imageSize(doc, page, s.info, w, h)) return;
		// The image, fitted, on a soft card.
		const float inset = 8, fit = std::min({ 1.0f, (pw - 2 * inset) / w, (ph - 2 * inset) / h });
		const float x = (pw - w * fit) / 2, y = (ph - h * fit) / 2;
		fillRound(rt, D2D1::RectF(0, 0, pw, ph), 8, withAlpha(chrome().barInk(), 0.08f));
		D2D1_MATRIX_3X2_F was;
		rt->GetTransform(&was);
		rt->SetTransform(D2D1::Matrix3x2F::Scale(fit, fit) * D2D1::Matrix3x2F::Translation(x, y) * was);
		drawImage(rt, doc, page, w, h, fit * dpiOf(f.dialog) / 96.0f, s.color, s.grid, s.info);
		rt->SetTransform(was);
		strokeRound(rt, D2D1::RectF(x, y, x + w * fit, y + h * fit), 1, D2D1::ColorF(0, 0, 0, 0.15f), 0.5f);
	};
	f.onInit = [&](Form& form) { enableFields(form); };
	f.onChange = [&](Form& form, int field) {
		if (field == infoField || field == worksField) enableFields(form);
		form.setProblem("");
		form.refresh(previewField);
	};
	f.validate = [&](Form& form) { return problem(state(form)); };
	f.onButton = [&](Form& form, int) {
		const State s = state(form);
		const std::string bad = problem(s);
		if (!bad.empty()) { form.setProblem(bad); MessageBeep(MB_ICONWARNING); return false; }
		keep(s);
		IWICBitmap* bmp = makeImage(doc, page, s.multiplier, s.color, s.grid, s.info);
		const bool ok = bmp && images::copyToClipboard(form.dialog, bmp);
		if (bmp) bmp->Release();
		if (!ok) { form.setProblem("The image couldn't be copied."); return false; }
		win->note("Image copied.");
		return true;
	};
	const int result = f.run(win->window());
	if (result != IDOK) return;
	State s;
	s.grid = !f.fields[gridField].value.empty();
	s.info.enabled = !f.fields[infoField].value.empty();
	s.info.name = f.fields[nameField].value;
	s.info.works = f.fields[worksField].value == "0";
	s.info.why = f.fields[whyField].value;
	s.info.fileName = fileName;
	s.color = f.fields[styleField].value == "0";
	const int res = atoi(f.fields[resField].value.c_str());
	s.multiplier = res == 0 ? 2 : res == 2 ? 6 : 4;
	keep(s);
	const std::string file = chooseSaveFile(win->window(), "Export as Image", fileName + ".png", { { "PNG pictures (*.png)", "*.png" } }, ".png");
	if (file.empty()) return;
	IWICBitmap* bmp = makeImage(doc, page, s.multiplier, s.color, s.grid, s.info);
	const bool ok = bmp && images::savePng(bmp, file);
	if (bmp) bmp->Release();
	if (!ok) { showMessage(win->window(), Tone::Error, "The image couldn't be saved", file); return; }
	win->note("Exported " + baseName(file) + ".");
}
