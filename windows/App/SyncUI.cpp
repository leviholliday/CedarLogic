// Sync's screens (see SyncApp.h): the sheets for the code, linking and turning
// off, and the Sync page of Settings. The words are SYNC.md's section 5.1, the
// same on every platform ("this PC" for "this Mac").

#include "SyncApp.h"
#include "Alert.h"
#include "Chrome.h"
#include "Dialogs.h"
#include "Library.h"
#include "SyncPlatform.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace syncapp {

namespace {

const char* const kHowTitle = "How sync keeps your circuits private";

std::string dashed(const std::string& canonical) { return clsync::groupCode(canonical); }

// "••••-••••-••••-••••-••••-••••-1YZ4": the code, hidden but for its end.
std::string masked(const std::string& canonical) {
	if (canonical.size() < 4) return std::string();
	std::string out;
	for (int g = 0; g < 6; g++) out += "••••-";
	return out + canonical.substr(canonical.size() - 4);
}

// A note's height in lines at a width of about `chars` characters.
int linesFor(const std::string& text, int chars) {
	int lines = 0, run = 0;
	for (size_t i = 0; i <= text.size(); i++) {
		if (i == text.size() || text[i] == '\n') {
			lines += std::max(1, (run + chars - 1) / chars);
			run = 0;
		} else if (text[i] != '\r') {
			run++;
		}
	}
	return std::max(1, lines);
}

FormField note(const std::string& text, int chars) {
	FormField x;
	x.kind = FormField::Note;
	x.label = text;
	x.lines = linesFor(text, chars);
	return x;
}

FormField button(const std::string& label, bool beside = false, const std::string& side = std::string(), const std::string& tip = std::string()) {
	FormField x;
	x.kind = FormField::Button;
	x.label = label;
	x.beside = beside;
	x.side = side;
	x.tip = tip;
	return x;
}

// Text in a fixed-width face, for the code.
void monoText(ID2D1RenderTarget* rt, const std::string& text, const D2D1_RECT_F& box, float size, const D2D1_COLOR_F& color, bool bold) {
	IDWriteFactory* dw = dwFactory();
	if (dw == nullptr) return;
	IDWriteTextFormat* f = nullptr;
	if (FAILED(dw->CreateTextFormat(L"Consolas", nullptr, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
	                                DWRITE_FONT_STRETCH_NORMAL, size, L"", &f)))
		return;
	f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
	ID2D1SolidColorBrush* b = nullptr;
	if (SUCCEEDED(rt->CreateSolidColorBrush(color, &b))) {
		const std::wstring w = W(text);
		rt->DrawText(w.c_str(), (UINT32)w.size(), f, box, b);
		b->Release();
	}
	f->Release();
}

// The code's QR (the website's link), black on a white square with its quiet
// zone, in either theme: a camera wants it so.
struct Qr {
	std::vector<bool> modules;
	int size = 0;
	explicit Qr(const std::string& canonical) { modules = clsync::qr(clsync::webLink(canonical), size); }
	static const int kModule = 4;     // points; version 5 (the usual) is 37 modules: 180 points with the border
	float side() const { return (float)(size + 8) * kModule; }
	void paint(ID2D1RenderTarget* rt, float w, float) const {
		const float total = side(), left = std::floor((w - total) / 2), top = 0;
		fillRound(rt, D2D1::RectF(left, top, left + total, top + total), 6, D2D1::ColorF(1, 1, 1));
		ID2D1SolidColorBrush* b = nullptr;
		if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &b))) return;
		const D2D1_ANTIALIAS_MODE was = rt->GetAntialiasMode();
		rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);   // modules edge to edge, no seams
		for (int y = 0; y < size; y++)
			for (int x = 0; x < size; x++)
				if (modules[(size_t)y * size + x]) {
					const float mx = left + (x + 4) * kModule, my = top + (y + 4) * kModule;
					rt->FillRectangle(D2D1::RectF(mx, my, mx + kModule, my + kModule), b);
				}
		rt->SetAntialiasMode(was);
		b->Release();
	}
};

// ---- Your sync code ---------------------------------------------------------------------

void codeSheet(HWND parent, const std::string& canonical, bool afterStartOver) {
	if (canonical.empty()) return;
	Qr qr(canonical);
	const std::string grouped = dashed(canonical);
	Form f;
	f.title = afterStartOver ? "Your new sync code" : "Your sync code";
	f.width = 480;
	f.okText = "Done";
	f.cancelText = "";
	FormField head;
	head.kind = FormField::Picture;
	head.height = 74;
	head.paint = [grouped, afterStartOver](ID2D1RenderTarget* rt, float w, float h) {
		const FormLook look = formLook();
		drawText(rt, afterStartOver ? "Your new sync code" : "Your sync code", D2D1::RectF(0, 0, w, 22), 15, look.ink, TextAlign::Leading, true);
		const D2D1_RECT_F box = D2D1::RectF(0, 30, w, h);
		fillRound(rt, box, 9, look.field);
		strokeRound(rt, box, 9, look.line);
		monoText(rt, grouped, box, 20, look.ink, true);
	};
	f.add(head);
	FormField pic;
	pic.kind = FormField::Picture;
	pic.height = (int)qr.side() + 4;
	pic.paint = [&qr](ID2D1RenderTarget* rt, float w, float h) { qr.paint(rt, w, h); };
	f.add(pic);
	std::string how = "On your other computer: Settings › Sync › I Have a Code. On your phone: scan this with the camera, or open "
	                  "cedarlogic.netlify.app/app, then Your Circuits › Sync › Scan Code.";
	if (afterStartOver)
		how += "\r\nEvery other device stops syncing until you link it again with this new code.";
	f.add(note(how, 68));
	f.add(note("Keep it private: anyone with this code can see and change your synced circuits. Keep a copy somewhere safe: if you lose every "
	           "device that has it and this code, nobody can open the synced circuits — not even CedarLogic’s website.", 68));
	const int copyCode = f.add(button("Copy Code"));
	const int copyLink = f.add(button("Copy Link", true));
	FormField said;
	said.kind = FormField::Note;
	said.lines = 1;
	const int saidNote = f.add(said);
	f.onChange = [&](Form& form, int field) {
		const bool code = field == copyCode;
		if (!code && field != copyLink) return;
		const std::string text = code ? grouped : clsync::webLink(canonical);
		form.setText(saidNote, setClipboardText(form.dialog, text) ? (code ? "Code copied." : "Link copied. Anyone who opens it can link their devices.")
		                                                            : "Windows wouldn’t let CedarLogic use the clipboard just now.");
	};
	f.run(parent);
}

// ---- What linking asks ------------------------------------------------------------------------

// The confirmation: what the code holds, and that anyone with it can see and
// change it. True to link.
bool confirmLink(HWND parent, const std::string& sentence) {
	Alert a;
	a.title = "Link this PC";
	a.heading = "Link this PC?";
	a.text = sentence;
	a.badge = 1;
	a.buttons = { { "Link", 1, 1 }, { "Cancel", 0, 0 } };
	a.escape = 0;
	a.enter = 1;
	return runAlert(parent, a) == 1;
}

}  // namespace

void showCode(HWND parent, bool afterStartOver) { codeSheet(parent, code(), afterStartOver); }

void showCodeSample(HWND parent) { codeSheet(parent, code(), false); }

void showLinkSample(HWND parent) {
	confirmLink(parent,
	            "This code has 14 circuits from Bob’s laptop and Chrome on Android, last changed yesterday: “ALU”, "
	            "“Lab 3 adder”, “Traffic light”, “Mux”, “Register file”, …. Linking adds your 23 "
	            "circuits here to them. Circuits that are already the same aren’t doubled. Anyone with this code can see and change all "
	            "of them. Only link with a code you made yourself.");
}

// ---- Turning on -------------------------------------------------------------------------------

namespace {
bool gSettingUp = false;   // a code is being made (Settings says so)
}

void turnOn(HWND) {
	clsync::Engine* e = engine();
	if (e == nullptr) {
		showMessage(frontWindow(), Tone::Info, "Sync isn’t running", "Start CedarLogic normally to turn on sync.");
		return;
	}
	gSettingUp = true;
	e->turnOn([](bool ok, const std::string& message) {
		// (Asked outside the engine's call: the sheet waits for the person.)
		defer([ok, message] {
			gSettingUp = false;
			if (ok) {
				showCode(frontWindow());
			} else {
				showMessage(frontWindow(), Tone::Warning, "Sync couldn’t be turned on", message);
				stateChanged();
			}
		});
	});
}

// ---- I Have a Code ----------------------------------------------------------------------------

bool haveCode(HWND parent, const std::string& prefill, bool run) {
	clsync::Engine* e = engine();
	if (e == nullptr) {
		showMessage(parent, Tone::Info, "Sync isn’t running", "Start CedarLogic normally to link this PC.");
		return false;
	}
	// What the sheet and the engine's answer share (the answer can come after the sheet is gone).
	struct Shared {
		Form* form = nullptr;
		bool busy = false, got = false;
		std::string code;
		clsync::Preview preview;
	};
	auto st = std::make_shared<Shared>();
	Form f;
	f.title = "I Have a Code";
	f.width = 480;
	f.okText = "Continue";
	f.cancelText = "Cancel";
	f.add(note("Enter the code from your other device: type it, or paste it, or paste a sync link.", 68));
	FormField box;
	box.kind = FormField::Text;
	box.mono = true;
	box.placeholder = "000G-40R4-0M30-E209-185G-R38E-1YZ4";
	std::string shown = prefill;
	{
		std::string c, why;
		if (!prefill.empty() && clsync::parseCode(syncplat::crypto(), prefill, c, why)) shown = dashed(c);
	}
	box.value = shown;
	const int text = f.add(box);
	FormField said = note(" ", 68);
	said.lines = 2;   // (a sentence about the code, once it's typed)
	const int verdict = f.add(said);
	f.onChange = [text, verdict](Form& form, int field) {
		if (field != text) return;
		const std::string typed = form.text(text);
		std::string c, why;
		if (typed.find_first_not_of(" \t\r\n") == std::string::npos) form.setText(verdict, "");
		else if (clsync::parseCode(syncplat::crypto(), typed, c, why)) form.setText(verdict, "✓ That’s a valid code.");
		else form.setText(verdict, clsync::whyText(why, typed));
		form.setProblem("");
	};
	f.validate = [st, text, verdict, e](Form& form) -> std::string {
		if (st->busy) return "\n";
		const std::string typed = form.text(text);
		std::string c, why;
		if (!clsync::parseCode(syncplat::crypto(), typed, c, why)) return clsync::whyText(why, typed);
		st->busy = true;
		st->form = &form;
		form.setProblem("");
		form.setText(verdict, "Checking the code…");
		form.enable(text, false);
		e->preview(c, [st, c, text, verdict](bool ok, const std::string& message, clsync::Preview pv) {
			st->busy = false;
			Form* fm = st->form;
			if (fm == nullptr || fm->dialog == nullptr) return;   // the sheet was closed meanwhile
			if (!ok) {
				fm->enable(text, true);
				SetFocus(fm->fields[text].hwnd);
				fm->setText(verdict, "");
				fm->setProblem(message);
				return;
			}
			st->got = true;
			st->code = c;
			st->preview = pv;
			EndDialog(fm->dialog, IDOK);
		});
		return "\n";
	};
	f.onInit = [run](Form& form) {
		// A link that brought the code: the check starts at once.
		if (run) PostMessageW(form.dialog, WM_COMMAND, IDOK, 0);
	};
	const int r = f.run(parent);
	st->form = nullptr;
	if (r != IDOK || !st->got) return false;
	if (!confirmLink(parent, st->preview.sentence)) return false;
	e->link(st->code, [](bool ok, const std::string& message) {
		if (!ok) defer([message] { showMessage(frontWindow(), Tone::Warning, "This PC couldn’t be linked", message); });
	});
	return true;
}

// ---- Turning off, deleting, starting over -----------------------------------------------

void turnOff(HWND parent) {
	clsync::Engine* e = engine();
	if (e == nullptr) return;
	Form f;
	f.title = "Turn off sync on this PC?";
	f.width = 440;
	f.okText = "Turn Off";
	f.cancelText = "Cancel";
	f.add(note("Your circuits stay on this PC. Your other devices keep syncing with each other.", 62));
	FormField remove;
	remove.kind = FormField::Check;
	remove.label = "Also remove the synced circuits from this PC (they go to the library’s Trash folder)";
	const int removeField = f.add(remove);
	if (f.run(parent) != IDOK) return;
	e->turnOff(f.checked(removeField));
}

void deleteCopy(HWND parent) {
	clsync::Engine* e = engine();
	if (e == nullptr) return;
	if (!askConfirm(parent, "Delete the synced copy?",
	                "Sync stops on every device. Each device keeps the circuits it has now. This can’t be undone.", "Delete", "Cancel", true))
		return;
	e->deleteSyncedCopy([](bool ok, const std::string& message) {
		if (!ok) defer([message] { showMessage(frontWindow(), Tone::Warning, "The synced copy couldn’t be deleted", message); });
		else defer([] { stateChanged(); });
	});
}

void startOver(HWND parent) {
	clsync::Engine* e = engine();
	if (e == nullptr) return;
	if (!askConfirm(parent, "Start over with a new code?",
	                "The synced copy is deleted and a new code is made. Your circuits here are sent with the new code. Every other device stops "
	                "syncing until you link it again with the new code. Use this if someone else may have your code.",
	                "Start Over", "Cancel", true))
		return;
	e->startOver([](bool ok, const std::string& message) {
		defer([ok, message] {
			if (ok) showCode(frontWindow(), true);
			else {
				showMessage(frontWindow(), Tone::Warning, "Sync couldn’t start over", message);
				stateChanged();
			}
		});
	});
}

void howItWorks(HWND parent) {
	Form f;
	f.title = kHowTitle;
	f.width = 520;
	f.okText = "Done";
	f.cancelText = "";
	const char* const paragraphs[] = {
		"There’s no account. Turning on sync makes a secret code. Your devices use it to lock each circuit before it leaves the device, and "
		"only a device with the code can unlock it. CedarLogic’s website stores the locked circuits so your devices can find them, but it "
		"can’t read them: it never gets the code, so it doesn’t know your circuits’ names or what’s in them.",
		"What the website (and its host) does see: that a set of synced circuits exists, how many there are and roughly how big, when they "
		"change, and the internet addresses your devices sync from — which can show, say, that they’re used at the same school or home. "
		"CedarLogic uses addresses only to stop abuse and doesn’t store them; the website’s host keeps ordinary request logs.",
		"Keep the code private — anyone who has it can see and change your synced circuits. If someone else may have it, use Start Over with "
		"a New Code. Keep a copy somewhere safe: if you lose every device that has it, and the code, nobody can unlock the synced circuits. "
		"Not even us.",
		"Turn off sync any time; your circuits stay on your devices. “Delete Synced Copy” removes them from the website for good. If "
		"none of your devices syncs for a year, the synced copy is removed.",
	};
	for (const char* p : paragraphs) f.add(note(p, 80));
	f.run(parent);
}

// ---- Settings > Sync ----------------------------------------------------------------------

namespace {

const char kBlurb[] =
	"Keep Your Circuits the same on this PC, your other computers, your phone and CedarLogic Online. There’s no account: a secret code "
	"links your devices, and circuits are encrypted on this PC before they’re sent, so only your devices can read them.\r\n\r\n"
	"Free. Up to 1,000 circuits. If none of your devices syncs for a year, the synced copy is removed (your devices keep theirs).";

std::string offTip(const clsync::Status& st) {
	// Stopped from elsewhere: why, then what to do.
	if (st.kind == clsync::Status::Gone && !st.text.empty()) return st.text + "\r\n\r\n" + kBlurb;
	return kBlurb;
}

std::string onTip(const clsync::Status& st) {
	std::string line = statusLine(st);
	if (line.empty()) line = "Syncing…";
	const std::string n = recentNotice();
	if (!n.empty()) line += "\r\n" + n;
	return line;
}

}  // namespace

void SettingsPage::add(Form& form_) {
	Form& fm = form_;
	const bool on = enabled();
	const clsync::Status st = status();
	wasOn = on;
	// Off: the one thing to do, what it means, and the two ways in.
	f.turnOn = fm.add(button("Turn On Sync", false, "Sync:", offTip(st)));
	fm.fields[f.turnOn].tipLines = 8;
	f.haveCode = fm.add(button("I Have a Code…", true));
	f.how = fm.add(button("How Sync Works…", true));
	// On: its state, what this PC is called, the devices, the code, and the ways out.
	f.syncNow = fm.add(button("Sync Now", false, "Sync:", onTip(st)));
	fm.fields[f.syncNow].tipLines = 3;
	FormField name;
	name.kind = FormField::Text;
	name.label = "This PC’s name:";
	name.value = deviceName();
	name.widthPt = 260;
	name.tip = "Shown on your other devices when a circuit comes from here.";
	f.name = fm.add(name);
	FormField list;
	list.kind = FormField::Picture;
	list.side = "Devices:";
	list.height = 62;
	list.tip = "Don’t recognise one? Start over with a new code.";
	list.paint = [](ID2D1RenderTarget* rt, float w, float h) {
		const FormLook look = formLook();
		std::vector<std::pair<std::string, int64_t>> all = devices();
		const std::string me = deviceName();
		const int shownRows = (int)all.size() > 3 ? 2 : (int)all.size();
		for (int i = 0; i < shownRows; i++) {
			const float y = i * 20.0f;
			const bool mine = all[i].first == me;
			const std::string when = mine ? std::string("this PC") : "synced " + library::agoText((double)all[i].second / 1000.0);
			const float tw = textWidth(when, 11.5f);
			drawText(rt, all[i].first, D2D1::RectF(0, y, std::max(10.0f, w - tw - 14), y + 18), 12.5f, look.ink, TextAlign::Leading, mine);
			drawText(rt, when, D2D1::RectF(std::max(10.0f, w - tw - 8), y + 1, w, y + 19), 11.5f, look.dim, TextAlign::Trailing);
		}
		if ((int)all.size() > 3) {
			drawText(rt, strf("and %d more", (int)all.size() - 2), D2D1::RectF(0, 2 * 20.0f, w, 2 * 20.0f + 18), 12, look.dim);
		}
		(void)h;
	};
	f.devices = fm.add(list);
	// (A tip to start with, so there's a line to put the code's end in.)
	f.showCode = fm.add(button("Show Code…", false, "Sync code:", code().empty() ? masked("0000") : masked(code())));
	f.turnOff = fm.add(button("Turn Off Sync…"));
	f.startOver = fm.add(button("Start Over with a New Code…", true));
	f.deleteCopy = fm.add(button("Delete Synced Copy…"));
	f.how2 = fm.add(button("How Sync Works…", true));
	for (int off : { f.turnOn, f.haveCode, f.how }) fm.fields[off].hidden = on;
	for (int onField : { f.syncNow, f.name, f.devices, f.showCode, f.turnOff, f.startOver, f.deleteCopy, f.how2 }) fm.fields[onField].hidden = !on;
}

void SettingsPage::init(Form& fm) {
	form = &fm;
	listener = listen([this](Event) { refresh(); });
	refresh();
}

bool SettingsPage::handles(int field) const {
	for (int x : { f.turnOn, f.haveCode, f.how, f.syncNow, f.name, f.showCode, f.turnOff, f.startOver, f.deleteCopy, f.how2 })
		if (x == field) return true;
	return false;
}

void SettingsPage::changed(Form& fm, int field) {
	if (field == f.turnOn) {
		fm.setTip(f.turnOn, "Setting up…");
		turnOn(fm.dialog);
		return;   // (it says so until the code is made)
	} else if (field == f.haveCode) {
		haveCode(fm.dialog);
	} else if (field == f.how || field == f.how2) {
		howItWorks(fm.dialog);
	} else if (field == f.syncNow) {
		syncNow();
	} else if (field == f.showCode) {
		showCode(fm.dialog);
	} else if (field == f.turnOff) {
		turnOff(fm.dialog);
	} else if (field == f.startOver) {
		startOver(fm.dialog);
	} else if (field == f.deleteCopy) {
		deleteCopy(fm.dialog);
	}
	refresh();
}

// The name, once it's been typed.
void SettingsPage::left(Form& fm, int field) {
	if (field == f.name) setDeviceName(fm.text(f.name));
}

void SettingsPage::tick(Form&) { refresh(); }

void SettingsPage::detach() {
	if (listener) unlisten(listener);
	listener = 0;
	// A name typed and not left yet (Escape, the close box).
	if (form && f.name >= 0 && f.name < (int)form->fields.size()) setDeviceName(form->fields[f.name].value);
	form = nullptr;
}

void SettingsPage::show(bool on) {
	if (form == nullptr || form->dialog == nullptr) return;
	for (int x : { f.turnOn, f.haveCode, f.how }) form->fields[x].hidden = on;
	for (int x : { f.syncNow, f.name, f.devices, f.showCode, f.turnOff, f.startOver, f.deleteCopy, f.how2 }) form->fields[x].hidden = !on;
	form->hiddenChanged(form->fields[f.turnOn].page);
}

void SettingsPage::refresh() {
	if (form == nullptr || form->dialog == nullptr) return;
	const bool on = enabled();
	const clsync::Status st = status();
	if (on != wasOn) {
		wasOn = on;
		show(on);
	}
	if (on) {
		form->setTip(f.syncNow, onTip(st));
		form->setTip(f.showCode, masked(code()));
		form->refresh(f.devices);
		// The name as the engine has it, unless it's being typed.
		if (GetFocus() != form->fields[f.name].hwnd && form->text(f.name) != deviceName()) form->setText(f.name, deviceName());
	} else {
		form->setTip(f.turnOn, gSettingUp ? std::string("Setting up…") : offTip(st));
	}
}

}  // namespace syncapp
