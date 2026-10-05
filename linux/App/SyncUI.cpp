// What people see of sync (see SyncUI.h).

#include "SyncUI.h"
#include "Alert.h"
#include "Anim.h"
#include "Brand.h"
#include "Library.h"
#include "Settings.h"
#include "Sheet.h"
#include "SyncApp.h"
#include "SyncPlatform.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace syncui {

namespace {

// ---- A card, in the app's alert look, with what sync's questions need: a code (selectable), its QR
// code, a field that checks as you type, a tick box, and buttons that may keep it open -------------------

struct CardButton {
	std::string label;
	int answer = 0;
	int kind = 0;              // 0 plain, 1 the default (neon), 2 destructive
	bool apart = false;        // on the left, away from the others
	bool keepOpen = false;     // runs `action` and stays (Copy Code)
	std::function<bool()> enabled;
	std::function<void(CardButton&)> action;
};

struct Card {
	std::string title, heading;
	std::vector<std::string> paragraphs;
	std::string code;          // the sync code, big and selectable
	std::string qrText;        // its QR code is drawn
	bool field = false;
	std::string value, placeholder;
	// Under the field, as it's typed: the sentence to show, and whether the text is good.
	std::function<std::string(const std::string&, bool&)> check;
	bool checkbox = false;
	std::string checkboxLabel;
	bool checked = false;
	std::vector<CardButton> buttons;
	int escape = 0, enter = 1;
	float width = 480;
	// While it runs.
	std::string checkText;
	bool checkGood = false;
};

// Runs modally over `parent`; returns the answer of the button that closed it.
int runCard(GtkWindow* parent, Card& c) {
	GdkScreen* screen = gdk_screen_get_default();
	const bool glass = screen && gdk_screen_get_rgba_visual(screen) && gdk_screen_is_composited(screen);
	const float pad = glass ? 40 : 0, inset = 22, W = c.width, textW = W - 2 * inset;

	int qrSize = 0;
	std::vector<bool> qrBits;
	if (!c.qrText.empty()) qrBits = clsync::qr(c.qrText, qrSize);
	const float qrScale = qrSize > 0 ? std::max(3.0f, std::ceil(176.0f / (qrSize + 8))) : 0;
	const float qrBox = qrSize > 0 ? (qrSize + 8) * qrScale : 0;

	// Layout, from the card's top.
	const float headH = brand::text(nullptr, c.heading, 0, 0, 16, brand::Bold, brand::kPrimary, textW);
	const float headTop = inset + 52;
	float y = headTop + headH + 7;
	std::vector<float> paraTop, paraH;
	for (const std::string& p : c.paragraphs) {
		const float h = brand::text(nullptr, p, 0, 0, 12.5f, brand::Normal, brand::kPrimary, textW);
		paraTop.push_back(y);
		paraH.push_back(h);
		y += h + 9;
	}
	float codeTop = 0, qrTop = 0, fieldTop = 0, msgTop = 0, checkTop = 0, checkH = 0;
	const float codeH = 46, fieldH = 34;
	if (!c.code.empty()) { codeTop = y + 2; y = codeTop + codeH + 12; }
	if (qrSize > 0) { qrTop = y; y = qrTop + qrBox + 14; }
	if (c.field) { fieldTop = y + 2; msgTop = fieldTop + fieldH + 5; y = msgTop + 22; }
	if (c.checkbox) {
		checkH = std::max(20.0f, brand::text(nullptr, c.checkboxLabel, 0, 0, 12.5f, brand::Normal, brand::kPrimary, textW - 28));
		checkTop = y + 2;
		y = checkTop + checkH + 10;
	}
	const float buttonsTop = y + 6, cardH = buttonsTop + 34 + inset;

	int answer = c.escape;
	const double shown = anim::now();
	auto drop = [&] { return (float)(1 - anim::easeOut((anim::now() - shown) / 0.35)); };
	auto appear = [&] { return (float)anim::easeOut((anim::now() - shown) / 0.14); };
	GtkWidget *fieldEntry = nullptr, *codeEntry = nullptr;
	Sheet s;
	s.title = c.title.empty() ? c.heading : c.title;
	s.width = (int)(W + 2 * pad);
	s.height = (int)(cardH + 2 * pad);
	s.minWidth = s.width;
	s.minHeight = s.height;
	s.resizable = false;
	s.decorated = false;
	s.transparent = glass;
	s.animating = true;

	auto isEnabled = [](const CardButton& b) { return !b.enabled || b.enabled(); };
	auto press = [&](Sheet& sh, CardButton& b) {
		if (!isEnabled(b)) return;
		if (b.keepOpen) {
			if (b.action) b.action(b);
			return;
		}
		answer = b.answer;
		sh.close();
	};
	auto place = [&] {
		if (fieldEntry) gtk_widget_set_margin_top(fieldEntry, (int)(pad + fieldTop + 4 - 20 * drop()));
		if (codeEntry) gtk_widget_set_margin_top(codeEntry, (int)(pad + codeTop + 3 - 20 * drop()));
		const float o = appear();
		if (fieldEntry) gtk_widget_set_opacity(fieldEntry, o);
		if (codeEntry) gtk_widget_set_opacity(codeEntry, o);
	};
	s.onOpen = [&](Sheet& sh) {
		auto redraw = +[](GtkWidget*, GdkEvent*, gpointer area) -> gboolean { gtk_widget_queue_draw(GTK_WIDGET(area)); return FALSE; };
		if (c.field) {
			fieldEntry = gtk_entry_new();
			gtk_widget_set_name(fieldEntry, "alert-entry");
			gtk_entry_set_text(GTK_ENTRY(fieldEntry), c.value.c_str());
			gtk_entry_set_placeholder_text(GTK_ENTRY(fieldEntry), c.placeholder.c_str());
			gtk_entry_set_has_frame(GTK_ENTRY(fieldEntry), FALSE);
			gtk_entry_set_input_purpose(GTK_ENTRY(fieldEntry), GTK_INPUT_PURPOSE_FREE_FORM);
			gtk_widget_set_halign(fieldEntry, GTK_ALIGN_START);
			gtk_widget_set_valign(fieldEntry, GTK_ALIGN_START);
			gtk_widget_set_size_request(fieldEntry, (int)(textW - 20), (int)fieldH - 8);
			gtk_widget_set_margin_start(fieldEntry, (int)(pad + inset + 10));
			gtk_widget_set_opacity(fieldEntry, 0);
			gtk_overlay_add_overlay(GTK_OVERLAY(sh.overlay), fieldEntry);
			sh.initialFocus = fieldEntry;
			using Typed = std::function<void(const char*)>;
			g_signal_connect_data(fieldEntry, "changed", CL_CALLBACK(+[](GtkEditable* e, gpointer p) {
				(*static_cast<Typed*>(p))(gtk_entry_get_text(GTK_ENTRY(e)));
			}), new Typed([&c, &sh](const char* text) {
				c.value = text;
				c.checkGood = false;
				c.checkText = c.check ? c.check(c.value, c.checkGood) : std::string();
				sh.redraw();
			}), +[](gpointer p, GClosure*) { delete static_cast<Typed*>(p); }, (GConnectFlags)0);
			g_signal_connect(fieldEntry, "focus-in-event", G_CALLBACK(redraw), sh.area);
			g_signal_connect(fieldEntry, "focus-out-event", G_CALLBACK(redraw), sh.area);
			if (c.check) c.checkText = c.check(c.value, c.checkGood);
		}
		if (!c.code.empty()) {
			codeEntry = gtk_entry_new();
			gtk_widget_set_name(codeEntry, "sync-code");
			gtk_entry_set_text(GTK_ENTRY(codeEntry), c.code.c_str());
			gtk_editable_set_editable(GTK_EDITABLE(codeEntry), FALSE);
			gtk_entry_set_has_frame(GTK_ENTRY(codeEntry), FALSE);
			gtk_entry_set_alignment(GTK_ENTRY(codeEntry), 0.5);
			gtk_widget_set_halign(codeEntry, GTK_ALIGN_START);
			gtk_widget_set_valign(codeEntry, GTK_ALIGN_START);
			gtk_widget_set_size_request(codeEntry, (int)(textW - 20), (int)codeH - 6);
			gtk_widget_set_margin_start(codeEntry, (int)(pad + inset + 10));
			gtk_widget_set_opacity(codeEntry, 0);
			gtk_overlay_add_overlay(GTK_OVERLAY(sh.overlay), codeEntry);
		}
		place();
	};
	s.onClose = [&](Sheet& sh) {
		// (The field's text was kept as it was typed; its change handler's data goes with the card.)
		if (fieldEntry) g_signal_handlers_disconnect_by_data(fieldEntry, sh.area);
	};
	s.onTick = [&](Sheet& sh) {
		place();
		if (anim::now() - shown > 0.5) sh.animating = false;
	};
	s.paint = [&](Sheet& sh, cairo_t* cr, float w, float h) {
		const bool dark = prefs().dark;
		const Color ink = dark ? colorF(0.94f, 0.94f, 0.94f) : colorF(0.1f, 0.1f, 0.1f);
		const Color dim = dark ? colorF(0.66f, 0.66f, 0.66f) : colorF(0.38f, 0.38f, 0.38f);
		const float ox = pad, oy = pad - 20 * drop();
		const RectF card = glass ? rectF(ox, oy, ox + W, oy + cardH) : rectF(0, 0, w, h);
		const float radius = glass ? 18 : 0;
		const RGBA acc = accentColor(dark);
		const Color accent = colorF((float)acc.r, (float)acc.g, (float)acc.b);
		cairo_save(cr);
		cairo_push_group(cr);
		if (glass) brand::glow(cr, rectF(card.left, card.top + 10, card.right, card.bottom + 10), 18, colorF(0, 0, 0, 0.35f), 22);
		fillRound(cr, card, radius, dark ? colorF(0.075f, 0.085f, 0.1f, 0.97f) : colorF(0.97f, 0.975f, 0.98f, 0.98f));
		cairo_save(cr);
		roundedPath(cr, card, radius);
		cairo_clip(cr);
		cairo_pattern_t* tint = cairo_pattern_create_linear(0, card.top, 0, card.top + 90);
		cairo_pattern_add_color_stop_rgba(tint, 0, acc.r, acc.g, acc.b, dark ? 0.10 : 0.07);
		cairo_pattern_add_color_stop_rgba(tint, 1, acc.r, acc.g, acc.b, 0);
		cairo_set_source(cr, tint);
		cairo_paint(cr);
		cairo_pattern_destroy(tint);
		cairo_restore(cr);
		strokeRound(cr, card, radius, colorF(1, 1, 1, dark ? 0.1f : 0.5f));
		const float x = card.left + inset, top = card.top, right = card.right - inset;
		brand::icon(cr, x, top + inset, 40);
		brand::text(cr, c.heading, x, top + headTop, 16, brand::Bold, ink, textW);
		for (size_t i = 0; i < c.paragraphs.size(); i++) brand::text(cr, c.paragraphs[i], x, top + paraTop[i], 12.5f, brand::Normal, dim, textW);
		if (!c.code.empty()) {
			const RectF f = rectF(x, top + codeTop, right, top + codeTop + codeH);
			fillRound(cr, f, 9, withAlpha(ink, dark ? 0.07f : 0.05f));
			strokeRound(cr, f, 9, withAlpha(ink, 0.12f));
		}
		if (qrSize > 0) {
			// Always dark modules on white, in the dark theme too: a camera has to read it.
			const float qx = card.left + (W - qrBox) / 2, qy = top + qrTop;
			fillRound(cr, rectF(qx, qy, qx + qrBox, qy + qrBox), 6, colorF(1, 1, 1));
			setColor(cr, colorF(0, 0, 0));
			for (int r = 0; r < qrSize; r++)
				for (int q = 0; q < qrSize; q++)
					if (qrBits[(size_t)r * qrSize + q]) cairo_rectangle(cr, qx + (q + 4) * qrScale, qy + (r + 4) * qrScale, qrScale, qrScale);
			cairo_fill(cr);
		}
		if (c.field) {
			const RectF f = rectF(x, top + fieldTop, right, top + fieldTop + fieldH);
			const bool focused = fieldEntry && gtk_widget_has_focus(fieldEntry);
			fillRound(cr, f, 9, withAlpha(ink, dark ? 0.07f : 0.05f));
			strokeRound(cr, f, 9, focused ? withAlpha(accent, 0.8f) : withAlpha(ink, 0.12f), focused ? 2 : 1);
			if (!c.checkText.empty()) {
				const Color good = dark ? colorF(0.45f, 0.85f, 0.55f) : colorF(0.1f, 0.5f, 0.2f);
				const Color bad = dark ? colorF(1.0f, 0.55f, 0.5f) : colorF(0.75f, 0.15f, 0.12f);
				brand::text(cr, (c.checkGood ? "\xE2\x9C\x93 " : "") + c.checkText, x, top + msgTop, 12, brand::Normal, c.checkGood ? good : bad, textW);
			}
		}
		if (c.checkbox) {
			const RectF box = rectF(x, top + checkTop + 1, x + 16, top + checkTop + 17);
			fillRound(cr, box, 4, c.checked ? accent : withAlpha(ink, 0.08f));
			strokeRound(cr, box, 4, c.checked ? accent : withAlpha(ink, 0.35f));
			if (c.checked) {
				cairo_save(cr);
				cairo_set_line_width(cr, 2);
				setColor(cr, chrome().onAccent());
				cairo_move_to(cr, box.left + 3.5f, box.top + 8.5f);
				cairo_line_to(cr, box.left + 6.8f, box.top + 11.8f);
				cairo_line_to(cr, box.left + 12.5f, box.top + 4.8f);
				cairo_stroke(cr);
				cairo_restore(cr);
			}
			brand::text(cr, c.checkboxLabel, x + 28, top + checkTop, 12.5f, brand::Normal, ink, textW - 28);
			sh.hit(rectF(x, top + checkTop, right, top + checkTop + checkH), [&] { c.checked = !c.checked; });
		}
		auto pill = [&](CardButton& b, float left, float rightEdge) {
			const bool on = isEnabled(b);
			const char* key = b.answer == c.enter && !b.keepOpen ? "\xE2\x86\xA9" : b.answer == c.escape && !b.keepOpen ? "esc" : nullptr;
			const float tw = textWidth(b.label, 13, true) + (key ? textWidth(key, 10, true) + 17 : 0) + 28;
			if (left < 0) left = rightEdge - tw;
			const RectF r = rectF(left, card.bottom - inset - 34, left + tw, card.bottom - inset);
			const bool hot = on && sh.hotNext();
			const Color red = colorF(0.86f, 0.22f, 0.2f);
			const float dimmed = on ? 1.0f : 0.4f;
			fillRound(cr, r, 9, b.kind == 1 ? withAlpha(brand::kNeonDeep, (hot ? 1.0f : 0.92f) * dimmed)
			                    : b.kind == 2 ? withAlpha(red, (hot ? 1.0f : 0.9f) * dimmed)
			                                  : withAlpha(ink, hot ? 0.12f : 0.08f));
			const Color fg = b.kind ? colorF(1, 1, 1, dimmed) : withAlpha(ink, dimmed);
			drawTextMid(cr, b.label, rectF(r.left + 14, r.top, r.right, r.bottom), 13, fg, TextAlign::Leading, true);
			if (key) {
				const float kx = r.left + 14 + textWidth(b.label, 13, true) + 7;
				const RectF kr = rectF(kx, r.top + 9, kx + textWidth(key, 10, true) + 10, r.bottom - 9);
				fillRound(cr, kr, 4, withAlpha(fg, 0.16f));
				drawTextMid(cr, key, kr, 10, fg, TextAlign::Center, true);
			}
			CardButton* bp = &b;
			if (on) sh.hit(r, [&, bp] { press(sh, *bp); });
			return r.left;
		};
		float edge = card.right - inset;
		for (CardButton& b : c.buttons)
			if (!b.apart) edge = pill(b, -1, edge) - 10;
		for (CardButton& b : c.buttons)
			if (b.apart) pill(b, card.left + inset, 0);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, appear());
		cairo_restore(cr);
	};
	s.onKey = [&](Sheet& sh, guint k, guint) -> bool {
		if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) {
			for (CardButton& b : c.buttons)
				if (b.answer == c.enter && !b.keepOpen && isEnabled(b)) { press(sh, b); return true; }
			return true;
		}
		if (k == GDK_KEY_Escape) { answer = c.escape; sh.close(); return true; }
		return false;
	};
	s.run(parent);
	if (c.field) {
		gchar* t = g_strstrip(g_strdup(c.value.c_str()));
		c.value = t;
		g_free(t);
	}
	return answer;
}


// ---- Little helpers -------------------------------------------------------------------------------------

// Later, on the main loop: the engine's answers arrive inside its call and mustn't wait on a sheet.
void defer(std::function<void()> f) {
	auto* fn = new std::function<void()>(std::move(f));
	g_idle_add(+[](gpointer p) -> gboolean {
		std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()>*>(p));
		guarded("sync", [&] { (*f)(); });
		return G_SOURCE_REMOVE;
	}, fn);
}

void onClicked(GtkWidget* w, std::function<void()> f) {
	using Fn = std::function<void()>;
	g_signal_connect_data(w, "clicked", CL_CALLBACK(+[](GtkWidget*, gpointer p) {
		guarded("a button", [&] { (*static_cast<Fn*>(p))(); });
	}), new Fn(std::move(f)), +[](gpointer p, GClosure*) { delete static_cast<Fn*>(p); }, (GConnectFlags)0);
}

GtkWidget* makeButton(const std::string& label, std::function<void()> f, bool suggested = false) {
	GtkWidget* b = gtk_button_new_with_label(label.c_str());
	gtk_widget_set_halign(b, GTK_ALIGN_START);
	if (suggested) gtk_style_context_add_class(gtk_widget_get_style_context(b), "suggested-action");
	onClicked(b, std::move(f));
	return b;
}

GtkWidget* hintLabel(const std::string& text, int width = 360) {
	GtkWidget* h = gtk_label_new(text.c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(h), "hint");
	gtk_label_set_xalign(GTK_LABEL(h), 0);
	gtk_label_set_line_wrap(GTK_LABEL(h), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(h), width / 6);
	gtk_widget_set_size_request(h, width, -1);
	return h;
}

// Over the Settings window if it's open, else a circuit window.
GtkWindow* sheetParent() {
	if (GtkWidget* s = settings::window()) return GTK_WINDOW(s);
	for (CircuitWindow* w : circuitWindows())
		if (gtk_window_is_active(w->window())) return w->window();
	return circuitWindows().empty() ? nullptr : circuitWindows().front()->window();
}

CircuitWindow* frontCircuit() {
	for (CircuitWindow* w : circuitWindows())
		if (gtk_window_is_active(w->window())) return w;
	return circuitWindows().empty() ? nullptr : circuitWindows().front();
}

void copyText(const std::string& text) {
	gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
}

enum { SyncPage = 5 };

// What's being done for the person while they wait (Setting up…, Checking the code…).
struct Work {
	bool on = false;
	std::string text;
};
Work gWork;
void refreshPage();

void startWork(const std::string& text) {
	gWork.on = true;
	gWork.text = text;
	refreshPage();
}

void endWork() {
	gWork.on = false;
	gWork.text.clear();
	refreshPage();
}

bool needCurl() {
	if (syncplatform::curlAvailable()) return true;
	showMessage(sheetParent(), GTK_MESSAGE_WARNING, "Sync needs the curl program",
	            "CedarLogic talks to the website through curl, which isn't installed here. Install it (sudo apt install curl) and try again.");
	return false;
}

// ---- The words (SYNC.md 5.1, 8.1) -----------------------------------------------------------------------

const char* const kWhatItIs =
	"Keep Your Circuits the same on this computer, your other computers, your phone and CedarLogic Online. "
	"There's no account: a secret code links your devices, and circuits are encrypted on this computer before "
	"they're sent, so only your devices can read them.";
const char* const kFreeLine =
	"Free. Up to 1,000 circuits. If none of your devices syncs for a year, the synced copy is removed (your devices keep theirs).";

void showHowItWorks() {
	Card c;
	c.heading = "How sync keeps your circuits private";
	c.width = 520;
	c.paragraphs = {
		"There's no account. Turning on sync makes a secret code. Your devices use it to lock each circuit before it leaves the "
		"device, and only a device with the code can unlock it. CedarLogic's website stores the locked circuits so your devices can "
		"find them, but it can't read them: it never gets the code, so it doesn't know your circuits' names or what's in them.",
		"What the website (and its host) does see: that a set of synced circuits exists, how many there are and roughly how big, "
		"when they change, and the internet addresses your devices sync from \xE2\x80\x94 which can show, say, that they're used at the same "
		"school or home. CedarLogic uses addresses only to stop abuse and doesn't store them; the website's host keeps ordinary request logs.",
		"Keep the code private \xE2\x80\x94 anyone who has it can see and change your synced circuits. If someone else may have it, use Start Over "
		"with a New Code. Keep a copy somewhere safe: if you lose every device that has it, and the code, nobody can unlock the synced "
		"circuits. Not even us.",
		"Turn off sync any time; your circuits stay on your devices. \xE2\x80\x9C" "Delete Synced Copy\xE2\x80\x9D removes them from the website for good. "
		"If none of your devices syncs for a year, the synced copy is removed.",
	};
	c.buttons = { { "Done", 1, 1 } };
	c.escape = 1;
	runCard(sheetParent(), c);
}

// The code: big, selectable, with its QR code, and the warnings.
void showCode(bool fresh, bool startedOver = false) {
	clsync::Engine* e = syncapp::engine();
	if (!e) return;
	const std::string code = e->code();
	if (code.empty()) return;
	Card c;
	c.heading = "Your sync code";
	c.width = 520;
	c.code = clsync::groupCode(code);
	c.qrText = clsync::webLink(code);
	c.paragraphs = {
		"On your other computer: Settings \xE2\x96\xB8 Sync \xE2\x96\xB8 I Have a Code. On your phone: scan this with the camera, or open "
		"cedarlogic.netlify.app/app, then Your Circuits \xE2\x96\xB8 Sync \xE2\x96\xB8 Scan Code.",
		"Keep it private: anyone with this code can see and change your synced circuits. Keep a copy somewhere safe: if you lose every "
		"device that has it and this code, nobody can unlock the synced circuits \xE2\x80\x94 not even CedarLogic's website.",
	};
	if (startedOver) c.paragraphs.push_back("Every other device stops syncing until you link it again with this new code.");
	(void)fresh;
	CardButton copyCode{ "Copy Code", 10, 0, true, true, nullptr, nullptr };
	copyCode.action = [code](CardButton& b) {
		copyText(clsync::groupCode(code));
		b.label = "Copied";
	};
	CardButton copyLink{ "Copy Link", 11, 0, true, true, nullptr, nullptr };
	copyLink.action = [code](CardButton& b) {
		copyText(clsync::webLink(code));
		b.label = "Copied";
	};
	c.buttons = { { "Done", 1, 1 }, copyLink, copyCode };
	c.enter = 1;
	c.escape = 1;
	runCard(sheetParent(), c);
}

// ---- The flows ---------------------------------------------------------------------------------------------------

// Turn On Sync: a new code, then the sheet that shows it.
void turnOn() {
	clsync::Engine* e = syncapp::engine();
	if (!e || !needCurl()) return;
	startWork("Setting up\xE2\x80\xA6");
	e->turnOn([](bool ok, std::string message) {
		defer([ok, message] {
			endWork();
			if (!ok) showMessage(sheetParent(), GTK_MESSAGE_WARNING, "Sync couldn't be turned on", message);
			else showCode(true);
		});
	});
}

// What a code holds, then the question: only then does anything link.
void confirmLink(const std::string& code, const std::string& sentence) {
	clsync::Engine* e = syncapp::engine();
	if (!e) return;
	Card c;
	c.heading = "Link this computer?";
	c.width = 520;
	c.paragraphs = { sentence };
	c.buttons = { { "Link", 1, 1 }, { "Cancel", 0, 0 } };
	c.escape = 0;
	c.enter = 1;
	if (runCard(sheetParent(), c) != 1) return;
	startWork("Linking\xE2\x80\xA6");
	e->link(code, [](bool ok, std::string message) {
		defer([ok, message] {
			endWork();
			if (!ok) showMessage(sheetParent(), GTK_MESSAGE_WARNING, "Couldn't link this computer", message);
		});
	});
}

void previewThenLink(const std::string& code) {
	clsync::Engine* e = syncapp::engine();
	if (!e || !needCurl()) return;
	startWork("Checking the code\xE2\x80\xA6");
	e->preview(code, [code](bool ok, std::string text, clsync::Preview) {
		defer([code, ok, text] {
			endWork();
			if (!ok) showMessage(sheetParent(), GTK_MESSAGE_WARNING, "Can't link with that code", text);
			else confirmLink(code, text);
		});
	});
}

// I Have a Code: a field that checks as you type, then the preview.
void haveCode(const std::string& prefill) {
	Card c;
	c.heading = "Link this computer";
	c.paragraphs = { "Type or paste the code from your other device, or a sync link." };
	c.field = true;
	c.value = prefill;
	c.placeholder = "Sync code or link";
	c.check = [](const std::string& text, bool& good) -> std::string {
		good = false;
		if (text.empty()) return std::string();
		std::string code, why;
		if (clsync::parseCode(syncplatform::crypto(), text, code, why)) {
			good = true;
			return "That's a sync code.";
		}
		return clsync::whyText(why, text);
	};
	c.buttons = { { "Continue", 1, 1 }, { "Cancel", 0, 0 } };
	Card* cp = &c;
	c.buttons[0].enabled = [cp] { return cp->checkGood; };
	c.escape = 0;
	c.enter = 1;
	if (runCard(sheetParent(), c) != 1) return;
	std::string code, why;
	if (!clsync::parseCode(syncplatform::crypto(), c.value, code, why)) return;
	previewThenLink(code);
}

void turnOff() {
	clsync::Engine* e = syncapp::engine();
	if (!e) return;
	Card c;
	c.heading = "Turn off sync on this computer?";
	c.paragraphs = { "Your circuits stay on this computer. Your other devices keep syncing with each other." };
	c.checkbox = true;
	c.checkboxLabel = "Also remove the synced circuits from this computer (they go to the library's trash)";
	c.buttons = { { "Turn Off", 1, 2 }, { "Cancel", 0, 0 } };
	c.escape = 0;
	c.enter = 0;   // (not Enter: it isn't the one to press by habit)
	if (runCard(sheetParent(), c) != 1) return;
	e->turnOff(c.checked);
	// The circuits it removed are in the trash now (a moment later, on the engine's thread).
	defer([] { refreshPage(); });
}

void deleteCopy() {
	clsync::Engine* e = syncapp::engine();
	if (!e || !needCurl()) return;
	if (!askConfirm(sheetParent(), "Delete the synced copy?",
	                "Sync stops on every device. Each device keeps the circuits it has now. This can't be undone.", "Delete", "Cancel", true))
		return;
	startWork("Deleting the synced copy\xE2\x80\xA6");
	e->deleteSyncedCopy([](bool ok, std::string message) {
		defer([ok, message] {
			endWork();
			if (!ok) showMessage(sheetParent(), GTK_MESSAGE_WARNING, "The synced copy couldn't be deleted", message);
		});
	});
}

void startOver() {
	clsync::Engine* e = syncapp::engine();
	if (!e || !needCurl()) return;
	if (!askConfirm(sheetParent(), "Start over with a new code?",
	                "The synced copy is deleted and a new code is made. Your circuits here are sent with the new code. Every other device "
	                "stops syncing until you link it again with the new code. Use this if someone else may have your code.",
	                "Start Over", "Cancel", true))
		return;
	startWork("Starting over\xE2\x80\xA6");
	e->startOver([](bool ok, std::string message) {
		defer([ok, message] {
			endWork();
			if (!ok) showMessage(sheetParent(), GTK_MESSAGE_WARNING, "Couldn't start over", message);
			else showCode(true, true);
		});
	});
}

// ---- Settings > Sync -----------------------------------------------------------------------------------------------

struct Page {
	GtkWidget* box = nullptr;
	GtkWidget* statusLabel = nullptr;
	GtkWidget* devicesLabel = nullptr;
	std::string key;
	guint listener = 0, timer = 0;
};
Page* gPage = nullptr;

std::string devicesText(clsync::Engine* e) {
	const std::string own = e->deviceName();
	std::string out = own + " (this computer)";
	bool skipped = false;
	for (const auto& d : e->devices()) {
		if (!skipped && d.first == own) {   // this computer's own record
			skipped = true;
			continue;
		}
		out += "\n" + d.first;
		if (d.second > 0) out += ", synced " + library::agoText(d.second / 1000.0);
	}
	return out;
}

std::string pageKey() {
	clsync::Engine* e = syncapp::engine();
	if (gWork.on) return "work:" + gWork.text;
	if (!e) return "none";
	const clsync::Status st = e->status();
	if (e->enabled()) return "on";
	return st.kind == clsync::Status::Gone ? "gone" : "off";
}

// "Synced just now · 42 circuits" and the like, from the engine.
std::string statusLine(const clsync::Status& st) {
	std::string t = st.text;
	if (st.kind == clsync::Status::Synced && st.circuits > 0) t += " \xC2\xB7 " + std::to_string(st.circuits) + (st.circuits == 1 ? " circuit" : " circuits");
	return t;
}

void updateLabels(Page* p) {
	clsync::Engine* e = syncapp::engine();
	if (!e || !e->enabled()) return;
	if (p->statusLabel) gtk_label_set_text(GTK_LABEL(p->statusLabel), statusLine(e->status()).c_str());
	if (p->devicesLabel) gtk_label_set_text(GTK_LABEL(p->devicesLabel), devicesText(e).c_str());
}

// One setting: its name on the left, the control and a line of help on the right.
struct Form {
	GtkWidget* grid;
	int row = 0;
	Form() {
		grid = gtk_grid_new();
		gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
		gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
		gtk_widget_set_margin_start(grid, 28);
		gtk_widget_set_margin_end(grid, 28);
		gtk_widget_set_margin_top(grid, 22);
	}
	void add(const char* label, GtkWidget* control, const std::string& hint = "") {
		GtkWidget* l = gtk_label_new((std::string(label) + ":").c_str());
		gtk_label_set_xalign(GTK_LABEL(l), 1);
		gtk_label_set_yalign(GTK_LABEL(l), 0);
		gtk_widget_set_size_request(l, 150, -1);
		gtk_widget_set_margin_top(l, 5);
		gtk_widget_set_valign(l, GTK_ALIGN_START);
		gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
		GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_box_pack_start(GTK_BOX(v), control, FALSE, FALSE, 0);
		if (!hint.empty()) gtk_box_pack_start(GTK_BOX(v), hintLabel(hint), FALSE, FALSE, 0);
		gtk_grid_attach(GTK_GRID(grid), v, 1, row, 1, 1);
		row++;
	}
};

GtkWidget* pageBox(int top = 22) {
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start(box, 28);
	gtk_widget_set_margin_end(box, 28);
	gtk_widget_set_margin_top(box, top);
	gtk_widget_set_margin_bottom(box, 24);
	return box;
}

GtkWidget* headingLabel(const std::string& text) {
	GtkWidget* h = gtk_label_new(text.c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(h), "heading");
	gtk_label_set_xalign(GTK_LABEL(h), 0);
	return h;
}

void buildWorking(Page* p) {
	GtkWidget* box = pageBox(34);
	GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	GtkWidget* spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(spinner));
	gtk_box_pack_start(GTK_BOX(row), spinner, FALSE, FALSE, 0);
	GtkWidget* l = gtk_label_new(gWork.text.c_str());
	gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), hintLabel("This takes a moment. You can close this window; it carries on."), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(p->box), box, FALSE, FALSE, 0);
}

void buildOff(Page* p, const clsync::Status& st) {
	GtkWidget* box = pageBox();
	gtk_box_pack_start(GTK_BOX(box), headingLabel("Sync Your Circuits"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), hintLabel(kWhatItIs, 560), FALSE, FALSE, 0);
	if (st.kind == clsync::Status::Gone && !st.text.empty()) {
		GtkWidget* gone = gtk_label_new(st.text.c_str());
		gtk_label_set_xalign(GTK_LABEL(gone), 0);
		gtk_label_set_line_wrap(GTK_LABEL(gone), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(gone), 90);
		gtk_widget_set_size_request(gone, 560, -1);
		gtk_widget_set_margin_top(gone, 4);
		gtk_box_pack_start(GTK_BOX(box), gone, FALSE, FALSE, 0);
	}
	GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_top(buttons, 6);
	gtk_box_pack_start(GTK_BOX(buttons), makeButton("Turn On Sync", [] { turnOn(); }, true), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttons), makeButton("I Have a Code\xE2\x80\xA6", [] { haveCode(std::string()); }), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), hintLabel(kFreeLine, 560), FALSE, FALSE, 0);
	if (!syncplatform::curlAvailable())
		gtk_box_pack_start(GTK_BOX(box), hintLabel("Sync needs the curl program, which isn't installed here (sudo apt install curl).", 560), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), makeButton("How Sync Works\xE2\x80\xA6", [] { showHowItWorks(); }), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(p->box), box, FALSE, FALSE, 0);
}

void buildOn(Page* p, clsync::Engine* e) {
	GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	Form f;
	// Sync: the status, and Sync Now.
	GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	p->statusLabel = gtk_label_new(statusLine(e->status()).c_str());
	gtk_label_set_xalign(GTK_LABEL(p->statusLabel), 0);
	gtk_label_set_line_wrap(GTK_LABEL(p->statusLabel), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(p->statusLabel), 40);
	gtk_widget_set_size_request(p->statusLabel, 250, -1);
	gtk_box_pack_start(GTK_BOX(status), p->statusLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(status), makeButton("Sync Now", [] { if (clsync::Engine* e = syncapp::engine()) e->syncNow(); }), FALSE, FALSE, 0);
	f.add("Sync", status);
	// This computer's name.
	GtkWidget* name = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(name), e->deviceName().c_str());
	gtk_entry_set_max_length(GTK_ENTRY(name), 64);
	gtk_widget_set_size_request(name, 240, -1);
	gtk_widget_set_halign(name, GTK_ALIGN_START);
	auto commit = [](GtkWidget* entry) {
		clsync::Engine* e = syncapp::engine();
		const std::string text = gtk_entry_get_text(GTK_ENTRY(entry));
		if (e && !text.empty() && text != e->deviceName()) e->setDeviceName(text);
	};
	using Commit = std::function<void(GtkWidget*)>;
	g_signal_connect_data(name, "activate", CL_CALLBACK(+[](GtkWidget* w, gpointer p) { (*static_cast<Commit*>(p))(w); }), new Commit(commit),
	                      +[](gpointer p, GClosure*) { delete static_cast<Commit*>(p); }, (GConnectFlags)0);
	g_signal_connect_data(name, "focus-out-event", CL_CALLBACK(+[](GtkWidget* w, GdkEvent*, gpointer p) -> gboolean {
		(*static_cast<Commit*>(p))(w);
		return FALSE;
	}), new Commit(commit), +[](gpointer p, GClosure*) { delete static_cast<Commit*>(p); }, (GConnectFlags)0);
	f.add("This computer's name", name, "Shown on your other devices when a circuit comes from here.");
	// The devices.
	p->devicesLabel = gtk_label_new(devicesText(e).c_str());
	gtk_label_set_xalign(GTK_LABEL(p->devicesLabel), 0);
	gtk_label_set_line_wrap(GTK_LABEL(p->devicesLabel), TRUE);
	gtk_label_set_selectable(GTK_LABEL(p->devicesLabel), TRUE);
	f.add("Devices", p->devicesLabel, "Don't recognise one? Start over with a new code.");
	// The code, hidden.
	GtkWidget* code = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	const std::string grouped = clsync::groupCode(e->code());
	std::string masked;
	for (size_t i = 0; i + 4 < grouped.size(); i++) masked += grouped[i] == '-' ? std::string("-") : std::string("\xE2\x80\xA2");
	masked += grouped.substr(grouped.size() > 4 ? grouped.size() - 4 : 0);
	GtkWidget* codeLabel = gtk_label_new(masked.c_str());
	gtk_style_context_add_class(gtk_widget_get_style_context(codeLabel), "dim-label");
	gtk_box_pack_start(GTK_BOX(code), codeLabel, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(code), makeButton("Show Code", [] { showCode(false); }), FALSE, FALSE, 0);
	f.add("Sync code", code);
	gtk_box_pack_start(GTK_BOX(outer), f.grid, FALSE, FALSE, 0);
	// The three big ones, and how it works.
	GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start(buttons, 28);
	gtk_widget_set_margin_end(buttons, 28);
	gtk_widget_set_margin_top(buttons, 18);
	gtk_box_pack_start(GTK_BOX(buttons), makeButton("Turn Off Sync\xE2\x80\xA6", [] { turnOff(); }), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttons), makeButton("Start Over with a New Code\xE2\x80\xA6", [] { startOver(); }), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(buttons), makeButton("Delete Synced Copy\xE2\x80\xA6", [] { deleteCopy(); }), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(outer), buttons, FALSE, FALSE, 0);
	GtkWidget* how = makeButton("How Sync Works\xE2\x80\xA6", [] { showHowItWorks(); });
	gtk_widget_set_margin_start(how, 28);
	gtk_widget_set_margin_top(how, 12);
	gtk_widget_set_margin_bottom(how, 24);
	gtk_box_pack_start(GTK_BOX(outer), how, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(p->box), outer, FALSE, FALSE, 0);
}

void rebuild(Page* p) {
	GList* kids = gtk_container_get_children(GTK_CONTAINER(p->box));
	for (GList* k = kids; k; k = k->next) gtk_widget_destroy(GTK_WIDGET(k->data));
	g_list_free(kids);
	p->statusLabel = p->devicesLabel = nullptr;
	clsync::Engine* e = syncapp::engine();
	p->key = pageKey();
	const clsync::Status st = e ? e->status() : clsync::Status();
	if (gWork.on) buildWorking(p);
	else if (e && e->enabled()) buildOn(p, e);
	else buildOff(p, st);
	gtk_widget_show_all(p->box);
	// (Not while Settings is still building the page: it measures it when it's in.)
	if (gtk_widget_get_parent(p->box)) settings::pageChanged();
}

void refresh(Page* p) {
	if (pageKey() != p->key) rebuild(p);
	else updateLabels(p);
}

void refreshPage() {
	if (gPage) refresh(gPage);
}

}  // namespace

GtkWidget* settingsPage() {
	Page* p = new Page();
	p->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gPage = p;
	rebuild(p);
	p->listener = syncapp::addStatusListener([p] { refresh(p); });
	// "Synced 5 min ago" ages by itself.
	p->timer = g_timeout_add_seconds(20, +[](gpointer data) -> gboolean {
		updateLabels(static_cast<Page*>(data));
		return G_SOURCE_CONTINUE;
	}, p);
	g_signal_connect(p->box, "destroy", CL_CALLBACK(+[](GtkWidget*, gpointer data) {
		Page* p = static_cast<Page*>(data);
		syncapp::removeListener(p->listener);
		if (p->timer) g_source_remove(p->timer);
		if (gPage == p) gPage = nullptr;
		delete p;
	}), p);
	return p->box;
}

// ---- Your Circuits ---------------------------------------------------------------------------------------------------

Line yourCircuitsLine() {
	Line l;
	clsync::Engine* e = syncapp::engine();
	if (!e) return l;
	if (!e->enabled()) {
		const clsync::Status st = e->status();
		if (st.kind == clsync::Status::Gone && !st.text.empty()) l.text = st.text;
		l.button = "Sync\xE2\x80\xA6";
		return l;
	}
	l.on = true;
	const clsync::Status st = e->status();
	l.text = statusLine(st);
	const std::string note = syncapp::recentNotice();
	if (!note.empty() && st.kind == clsync::Status::Synced) l.text = note;
	l.button = "Sync Now";
	return l;
}

void yourCircuitsAction(CircuitWindow* from) {
	clsync::Engine* e = syncapp::engine();
	if (e && e->enabled()) e->syncNow();
	else settings::show(from, SyncPage);
}

std::string problemFor(const std::string& folderId) {
	clsync::Engine* e = syncapp::engine();
	if (!e || !e->enabled()) return std::string();
	for (const auto& pr : e->status().problems)
		if (pr.first == folderId) return pr.second;
	return std::string();
}

// ---- cedarlogic://sync links ----------------------------------------------------------------------------------------

void linkFromUrl(const std::string& link) {
	clsync::Engine* e = syncapp::engine();
	if (!e) return;
	std::string code, why;
	CircuitWindow* w = frontCircuit();
	if (!clsync::parseCode(syncplatform::crypto(), link, code, why)) {
		showMessage(w ? w->window() : nullptr, GTK_MESSAGE_WARNING, "That sync link has a problem",
		            clsync::whyText(why, link) + " Copy the code from your other device instead.");
		return;
	}
	settings::show(w, SyncPage);
	defer([code] {
		clsync::Engine* e = syncapp::engine();
		if (!e) return;
		if (e->enabled() && e->code() == code) {
			showMessage(sheetParent(), GTK_MESSAGE_INFO, "Already syncing", "This computer already syncs with this code.");
			return;
		}
		if (e->enabled()) {
			if (!askConfirm(sheetParent(), "This computer syncs with another code",
			                "Switch to this one? Circuits here stay; they'll be added to the other synced circuits.", "Switch", "Cancel"))
				return;
		}
		previewThenLink(code);
	});
}

// ---- Screenshots -----------------------------------------------------------------------------------------------------

void showForScreenshot(CircuitWindow* from, const std::string& what) {
	(void)from;
	if (what == "synccode") {
		// A fixed code, nothing made or sent.
		Card c;
		c.heading = "Your sync code";
		c.width = 520;
		c.code = "000G-40R4-0M30-E209-185G-R38E-1YZ4";
		c.qrText = clsync::webLink("000G40R40M30E209185GR38E1YZ4");
		c.paragraphs = {
			"On your other computer: Settings \xE2\x96\xB8 Sync \xE2\x96\xB8 I Have a Code. On your phone: scan this with the camera, or open "
			"cedarlogic.netlify.app/app, then Your Circuits \xE2\x96\xB8 Sync \xE2\x96\xB8 Scan Code.",
			"Keep it private: anyone with this code can see and change your synced circuits. Keep a copy somewhere safe: if you lose every "
			"device that has it and this code, nobody can unlock the synced circuits \xE2\x80\x94 not even CedarLogic's website.",
		};
		c.buttons = { { "Done", 1, 1 }, { "Copy Link", 11, 0, true, true, nullptr, nullptr }, { "Copy Code", 10, 0, true, true, nullptr, nullptr } };
		c.escape = c.enter = 1;
		runCard(from ? from->window() : nullptr, c);
	} else if (what == "synclink") {
		Card c;
		c.heading = "Link this computer";
		c.paragraphs = { "Type or paste the code from your other device, or a sync link." };
		c.field = true;
		c.value = "000G-40R4-0M30-E209-185G-R38E-1YZ4";
		c.placeholder = "Sync code or link";
		c.check = [](const std::string& text, bool& good) -> std::string {
			std::string code, why;
			good = clsync::parseCode(syncplatform::crypto(), text, code, why);
			return good ? std::string("That's a sync code.") : clsync::whyText(why, text);
		};
		Card* cp = &c;
		c.buttons = { { "Continue", 1, 1 }, { "Cancel", 0, 0 } };
		c.buttons[0].enabled = [cp] { return cp->checkGood; };
		runCard(from ? from->window() : nullptr, c);
	} else if (what == "syncconfirm") {
		Card c;
		c.heading = "Link this computer?";
		c.width = 520;
		c.paragraphs = {
			"This code has 14 circuits from Bob\xE2\x80\x99s laptop and Chrome on Android, last changed yesterday: \xE2\x80\x9C" "Lab 3 adder\xE2\x80\x9D, "
			"\xE2\x80\x9C" "Traffic light\xE2\x80\x9D, \xE2\x80\x9C" "ALU\xE2\x80\x9D, \xE2\x80\xA6 Linking adds your 23 circuits here to them. Circuits that are "
			"already the same aren't doubled. Anyone with this code can see and change all of them. Only link with a code you made yourself.",
		};
		c.buttons = { { "Link", 1, 1 }, { "Cancel", 0, 0 } };
		runCard(from ? from->window() : nullptr, c);
	} else {
		settings::show(from, SyncPage);
	}
}

}  // namespace syncui
