// A RAM's or ROM's contents -- the Mac's RamEditorView: sixteen words a row
// with the address down the side, in hex or decimal. The word last read glows
// green and the one last written amber, as it runs. Click a word to change it;
// jump to an address; load or save a .cdm memory file. Only the rows on
// screen are drawn, so a 64K memory opens as fast as a 16 word one.

#include "Alert.h"
#include "Anim.h"
#include "Dialogs.h"
#include "Sheet.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

const char* const kMono = "Monospace";
const float kAddrW = 74, kHeadH = 26, kRowH = 25, kGridTop = 128, kFootH = 64;

Color readColor(bool dark) { return dark ? colorF(0.22f, 0.96f, 0.44f) : colorF(0.05f, 0.68f, 0.27f); }
Color writtenColor(bool dark) { return dark ? colorF(1, 0.72f, 0.25f) : colorF(0.93f, 0.55f, 0.05f); }

// Mono text centred in a box.
void monoMid(cairo_t* cr, const std::string& s, const RectF& r, float size, const Color& c, bool center = true, bool bold = false) {
	const float tw = faceWidth(s, kMono, size, bold);
	const float x = center ? (r.left + r.right - tw) / 2 : r.left;
	drawFace(cr, s, x, (r.top + r.bottom) / 2 - size * 0.68f, kMono, size, c, bold);
}

GtkWidget* fieldEntry(const char* name, float width, float height) {
	GtkWidget* e = gtk_entry_new();
	gtk_widget_set_name(e, name);
	gtk_entry_set_has_frame(GTK_ENTRY(e), FALSE);
	gtk_widget_set_halign(e, GTK_ALIGN_START);
	gtk_widget_set_valign(e, GTK_ALIGN_START);
	gtk_widget_set_size_request(e, (int)width, (int)height);
	return e;
}

void place(GtkWidget* e, float x, float y) {
	gtk_widget_set_margin_start(e, (int)std::lround(x));
	gtk_widget_set_margin_top(e, (int)std::lround(y));
}

}  // namespace

void showRamEditor(CircuitWindow* w, long gate) {
	int addressBits = 0, dataBits = 0;
	CLDocument* doc = w->document();
	if (!cl_ram_info(doc, gate, &addressBits, &dataBits)) return;
	const unsigned long words = 1UL << std::min(std::max(addressBits, 0), 20);
	const int cols = (int)std::min<unsigned long>(16, words);
	const long rowsTotal = (long)std::max(1UL, words / 16);
	const int aDigits = std::max(1, (std::max(addressBits, 4) + 3) / 4), dDigits = std::max(1, (dataBits + 3) / 4);
	const int decDigits = (int)std::ceil(dataBits * 0.30103) + 1;
	const float cellW = std::max(3, std::max(dDigits, decDigits) + 1) * 7.4f + 10;
	const float gridW = 12 + kAddrW + cols * (cellW + 3) + 14;
	const float width = std::max(600.0f, gridW + 40);
	const float gridH = std::min(400.0f, kHeadH + 8 + rowsTotal * kRowH + 8);
	const float height = kGridTop + gridH + kFootH + 10;
	const std::string libName = cl_gate_library_name(doc, gate) ? cl_gate_library_name(doc, gate) : "";
	const std::string caption = cl_gate_caption(doc, gate) ? cl_gate_caption(doc, gate) : "Memory";

	bool decimal = false;
	float scrollY = 0;                 // in points, from the first row
	anim::Tween scrollTween;
	bool scrollEasing = false;
	long editing = -1;
	GtkWidget* cellEntry = nullptr;
	GtkWidget* jumpEntry = nullptr;
	std::map<unsigned long, unsigned long> originals;   // what words the user changed held before
	long seenRead = -2, seenWritten = -2;
	double flashedAt = -1;             // the glow moving to a new word
	bool openSettings = false;
	const float gridLeft = 20, gridTopY = kGridTop;
	auto viewRows = [&] { return (gridH - kHeadH - 8) / kRowH; };
	auto maxScroll = [&] { return std::max(0.0f, (rowsTotal - viewRows()) * kRowH); };
	auto shown = [&](unsigned long v) { return decimal ? format("%lu", v) : format("%0*lX", dDigits, v); };
	auto cellRect = [&](unsigned long addr) {
		const float x = gridLeft + 12 + kAddrW + (addr % 16) * (cellW + 3);
		const float y = gridTopY + kHeadH + 4 + (addr / 16) * kRowH - scrollY;
		return rectF(x, y, x + cellW, y + kRowH - 3);
	};

	Sheet s;
	s.title = caption;
	s.width = (int)width;
	s.height = (int)height;
	s.minWidth = s.width;
	s.minHeight = s.height;
	s.resizable = false;

	auto remember = [&](unsigned long addr, unsigned long becoming) {
		auto it = originals.find(addr);
		const unsigned long before = it != originals.end() ? it->second : cl_ram_value(doc, gate, addr);
		if (before == becoming) originals.erase(addr);
		else originals[addr] = before;
	};
	auto finishEdit = [&](bool keep) {
		if (editing < 0) return;
		const unsigned long addr = (unsigned long)editing;
		editing = -1;
		if (keep) {
			gchar* t = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(cellEntry))));
			char* end = nullptr;
			const unsigned long v = strtoul(t, &end, decimal ? 10 : 16);
			if (*t && end && *end == 0) {
				if (v != cl_ram_value(doc, gate, addr)) {
					remember(addr, v);
					cl_ram_set(doc, gate, addr, v);
					w->edited();
				}
			} else if (*t) {
				gtk_widget_error_bell(s.area);
			}
			g_free(t);
		}
		gtk_widget_hide(cellEntry);
		s.redraw();
	};
	auto beginEdit = [&](unsigned long addr) {
		finishEdit(true);
		// Whole on screen first.
		const float y = (addr / 16) * kRowH;
		if (y < scrollY) scrollY = y;
		else if (y + kRowH > scrollY + viewRows() * kRowH) scrollY = std::min(maxScroll(), y + kRowH - viewRows() * kRowH);
		editing = (long)addr;
		const RectF c = cellRect(addr);
		place(cellEntry, c.left + 1, c.top + 1);
		gtk_widget_set_size_request(cellEntry, (int)(c.right - c.left - 2), (int)(c.bottom - c.top - 2));
		gtk_entry_set_text(GTK_ENTRY(cellEntry), shown(cl_ram_value(doc, gate, addr)).c_str());
		gtk_widget_show(cellEntry);
		gtk_widget_grab_focus(cellEntry);
		gtk_editable_select_region(GTK_EDITABLE(cellEntry), 0, -1);
		s.redraw();
	};
	auto scrollTo = [&](float y, bool ease) {
		finishEdit(true);
		y = std::max(0.0f, std::min(maxScroll(), y));
		if (ease && anim::enabled()) {
			scrollTween.set(scrollY);
			scrollTween.go(y, 0.25);
			scrollEasing = true;
			s.animating = true;
		} else {
			scrollY = y;
			s.redraw();
		}
	};
	// Closing with changes asks, as the Mac's does on clicking away.
	auto closeAsking = [&](Sheet& sh) {
		finishEdit(true);
		if (!originals.empty()) {
			Alert a;
			a.heading = "Keep the changes to this memory?";
			a.text = originals.size() == 1 ? std::string("You changed 1 value.") : format("You changed %zu values.", originals.size());
			a.buttons = { { "Keep", 1, 1 }, { "Cancel", 0, 0 }, { "Put Them Back", 2, 0, true } };
			a.enter = 1;
			a.escape = 0;
			const int r = runAlert(GTK_WINDOW(sh.window), a);
			if (r == 0) return;
			if (r == 2) {
				for (const auto& o : originals) cl_ram_set(doc, gate, o.first, o.second);
				originals.clear();
				w->edited();
			}
		}
		sh.close();
	};

	std::function<void(bool)> finishFn = [&](bool keep) { finishEdit(keep); };
	std::function<void(const char*)> jumpFn = [&](const char* text) {
		gchar* t = g_strstrip(g_strdup(text));
		char* end = nullptr;
		const unsigned long a = strtoul(t, &end, 16);
		if (*t && end && *end == 0 && a < words) scrollTo((a / 16) * kRowH, true);
		g_free(t);
		s.redraw();
	};
	s.onOpen = [&](Sheet& sh) {
		gtk_widget_set_name(sh.window, "cl-sheet-ram");
		cellEntry = fieldEntry("ram-cell", cellW, kRowH - 3);
		gtk_entry_set_alignment(GTK_ENTRY(cellEntry), 0.5f);
		gtk_widget_set_no_show_all(cellEntry, TRUE);
		gtk_overlay_add_overlay(GTK_OVERLAY(sh.overlay), cellEntry);
		g_signal_connect(cellEntry, "focus-out-event", CL_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer f) -> gboolean {
			(*static_cast<std::function<void(bool)>*>(f))(true);
			return FALSE;
		}), &finishFn);
		jumpEntry = fieldEntry("ram-jump", 150, 24);
		gtk_entry_set_placeholder_text(GTK_ENTRY(jumpEntry), "Go to address (hex)");
		place(jumpEntry, width - 20 - 186 + 26, 90);
		gtk_overlay_add_overlay(GTK_OVERLAY(sh.overlay), jumpEntry);
		g_signal_connect(jumpEntry, "changed", CL_CALLBACK(+[](GtkEditable* e, gpointer f) {
			(*static_cast<std::function<void(const char*)>*>(f))(gtk_entry_get_text(GTK_ENTRY(e)));
		}), &jumpFn);
		auto redraw = +[](GtkWidget*, GdkEvent*, gpointer area) -> gboolean { gtk_widget_queue_draw(GTK_WIDGET(area)); return FALSE; };
		g_signal_connect(jumpEntry, "focus-in-event", G_CALLBACK(redraw), sh.area);
		g_signal_connect(jumpEntry, "focus-out-event", G_CALLBACK(redraw), sh.area);
	};

	s.onClose = [&](Sheet&) {
		finishEdit(true);
		g_signal_handlers_disconnect_by_data(cellEntry, &finishFn);
		g_signal_handlers_disconnect_by_data(jumpEntry, &jumpFn);
		g_signal_handlers_disconnect_by_data(jumpEntry, s.area);
	};
	s.onTick = [&](Sheet& sh) {
		bool more = false;
		if (scrollEasing) {
			scrollY = (float)scrollTween.value();
			if (scrollTween.active()) more = true;
			else scrollEasing = false;
		}
		if (flashedAt >= 0 && anim::now() - flashedAt < 0.3) more = true;
		sh.animating = more;
	};

	s.paint = [&](Sheet& sh, cairo_t* cr, float W, float H) {
		const Chrome c = chrome();
		const bool dark = c.dark;
		const Color paper = dark ? colorF(28 / 255.f, 31 / 255.f, 37 / 255.f) : colorF(250 / 255.f, 250 / 255.f, 252 / 255.f);
		const Color card = dark ? colorF(36 / 255.f, 40 / 255.f, 47 / 255.f) : colorF(1, 1, 1);
		const Color ink = dark ? colorF(226 / 255.f, 230 / 255.f, 238 / 255.f) : colorF(30 / 255.f, 33 / 255.f, 40 / 255.f);
		const Color line = withAlpha(ink, dark ? 0.08f : 0.09f);
		const Color accent = c.accent();
		fillRect(cr, rectF(0, 0, W, H), paper);

		// The part's picture and name, as in its settings.
		const RectF tile = rectF(20, 20, 76, 72);
		fillRound(cr, tile, 10, card);
		strokeRound(cr, tile, 10, line);
		if (!libName.empty()) {
			cairo_save(cr);
			cairo_translate(cr, tile.left + 6, tile.top + 8);
			cl_library_draw_gate(libName.c_str(), cr, 44, 36, std::max(1, gtk_widget_get_scale_factor(sh.area)), dark);
			cairo_restore(cr);
		}
		drawText(cr, caption, rectF(90, 26, W - 190, 48), 16, ink, TextAlign::Leading, true);
		drawText(cr, format("%lu addresses × %d bits · click a value to change it", words, dataBits), rectF(90, 50, W - 190, 66),
		         11, withAlpha(ink, 0.5f));
		// Hex | Decimal.
		{
			const char* names[] = { "Hex", "Decimal" };
			float sw[2];
			for (int i = 0; i < 2; i++) sw[i] = textWidth(names[i], 12, true) + 24;
			float x = W - 20 - sw[0] - sw[1] - 4;
			fillRound(cr, rectF(x, 32, W - 20, 60), 14, withAlpha(ink, 0.07f));
			x += 2;
			for (int i = 0; i < 2; i++) {
				const RectF r = rectF(x, 34, x + sw[i], 58);
				const bool on = (i == 1) == decimal;
				const bool hot = sh.hotNext();
				if (on) fillRound(cr, r, 12, accent);
				else if (hot) fillRound(cr, r, 12, withAlpha(ink, 0.06f));
				drawTextMid(cr, names[i], r, 12, on ? c.onAccent() : withAlpha(ink, 0.75f), TextAlign::Center, on);
				sh.hit(r, [&, i] { finishEdit(true); decimal = i == 1; });
				x += sw[i];
			}
		}

		// The legend, and the jump to an address.
		{
			float x = 20;
			auto legend = [&](const Color& col, const char* label) {
				const RectF sw = rectF(x, 97, x + 14, 107);
				fillRound(cr, sw, 3, withAlpha(col, 0.4f));
				strokeRound(cr, sw, 3, withAlpha(col, 0.8f));
				drawTextMid(cr, label, rectF(x + 20, 92, x + 200, 112), 11.5f, withAlpha(ink, 0.6f));
				x += 20 + textWidth(label, 11.5f) + 16;
			};
			legend(readColor(dark), "Last read");
			legend(writtenColor(dark), "Last written");
			const RectF f = rectF(W - 20 - 186, 88, W - 20, 116);
			const bool focused = jumpEntry && gtk_widget_has_focus(jumpEntry);
			fillRound(cr, f, 7, withAlpha(ink, dark ? 0.07f : 0.05f));
			strokeRound(cr, f, 7, focused ? withAlpha(accent, 0.85f) : withAlpha(ink, 0.12f), focused ? 1.5f : 1);
			// An arrow down to a line: "go to".
			const float ax = f.left + 14, ay = f.top + 8;
			cairo_save(cr);
			setColor(cr, withAlpha(ink, 0.45f));
			cairo_set_line_width(cr, 1.4);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_move_to(cr, ax, ay);
			cairo_line_to(cr, ax, ay + 8);
			cairo_move_to(cr, ax - 3.5, ay + 4.5);
			cairo_line_to(cr, ax, ay + 8);
			cairo_line_to(cr, ax + 3.5, ay + 4.5);
			cairo_move_to(cr, ax - 4.5, ay + 11.5);
			cairo_line_to(cr, ax + 4.5, ay + 11.5);
			cairo_stroke(cr);
			cairo_restore(cr);
		}

		// The words.
		const RectF g = rectF(gridLeft, gridTopY, W - 20, gridTopY + gridH);
		fillRound(cr, g, 12, card);
		const long read = cl_ram_last_read(doc, gate), written = cl_ram_last_written(doc, gate);
		const float flash = flashedAt < 0 ? 1 : (float)anim::easeOut((anim::now() - flashedAt) / 0.25);
		cairo_save(cr);
		roundedPath(cr, g, 12);
		cairo_clip(cr);
		cairo_rectangle(cr, g.left, g.top + kHeadH, g.right - g.left, g.bottom - g.top - kHeadH - 4);
		cairo_clip(cr);
		const long first = (long)(scrollY / kRowH);
		for (long r = first; r < rowsTotal && r <= first + (long)viewRows() + 1; r++) {
			const float y = gridTopY + kHeadH + 4 + r * kRowH - scrollY;
			monoMid(cr, format("0x%0*lX", aDigits, (unsigned long)r * 16), rectF(g.left + 12, y, g.left + 12 + kAddrW, y + kRowH - 3), 11,
			        withAlpha(ink, 0.45f), false);
			for (int col = 0; col < cols; col++) {
				const unsigned long addr = (unsigned long)r * 16 + col;
				if (addr >= words) break;
				const RectF cell = cellRect(addr);
				const unsigned long v = cl_ram_value(doc, gate, addr);
				const bool isRead = (long)addr == read, isWritten = (long)addr == written;
				const bool hot = sh.hotNext() && editing != (long)addr;
				Color back = withAlpha(ink, hot ? 0.09f : 0.045f);
				if (isRead) back = withAlpha(readColor(dark), 0.35f * flash);
				else if (isWritten) back = withAlpha(writtenColor(dark), 0.35f * flash);
				fillRound(cr, cell, 5, back);
				if (editing == (long)addr) {
					fillRound(cr, cell, 5, dark ? colorF(0.1f, 0.11f, 0.13f) : colorF(1, 1, 1));
					strokeRound(cr, cell, 5, accent, 1.5f);
				} else {
					if (originals.count(addr)) fillCircle(cr, PointF{ cell.right - 4.5f, cell.top + 4.5f }, 2, withAlpha(accent, 0.9f));
					monoMid(cr, shown(v), cell, 11.5f, v == 0 ? withAlpha(ink, 0.35f) : ink);
				}
				if (cell.bottom > g.top + kHeadH && cell.top < g.bottom - 4) sh.hit(cell, [&, addr] { beginEdit(addr); });
			}
		}
		cairo_restore(cr);
		// The column heads, pinned.
		for (int col = 0; col < cols; col++) {
			const float x = g.left + 12 + kAddrW + col * (cellW + 3);
			monoMid(cr, format("%X", col), rectF(x, g.top + 4, x + cellW, g.top + kHeadH), 10.5f, withAlpha(ink, 0.45f), true, true);
		}
		drawLine(cr, PointF{ g.left + 10, g.top + kHeadH }, PointF{ g.right - 10, g.top + kHeadH }, withAlpha(ink, 0.06f));
		strokeRound(cr, g, 12, line);
		// Where the list is, when there's more than fits.
		if (maxScroll() > 0) {
			const float track = gridH - kHeadH - 12, thumb = std::max(24.0f, track * viewRows() / rowsTotal);
			const float ty = g.top + kHeadH + 4 + (track - thumb) * scrollY / maxScroll();
			fillRound(cr, rectF(g.right - 8, ty, g.right - 4, ty + thumb), 2, withAlpha(ink, 0.25f));
		}

		// Load, Save, Settings; Done.
		const float fy = H - kFootH + 14;
		float x = 20;
		auto foot = [&](const char* label, std::function<void()> act) {
			const RectF r = rectF(x, fy, x + textWidth(label, 12.5f) + 26, fy + 32);
			fillRound(cr, r, 8, withAlpha(ink, sh.hotNext() ? 0.13f : 0.08f));
			drawTextMid(cr, label, r, 12.5f, ink, TextAlign::Center);
			sh.hit(r, std::move(act));
			x = r.right + 8;
		};
		foot("Load File…", [&] {
			finishEdit(true);
			GtkFileChooserNative* fc = gtk_file_chooser_native_new("Load Memory", GTK_WINDOW(sh.window), GTK_FILE_CHOOSER_ACTION_OPEN, "_Load",
			                                                       "_Cancel");
			GtkFileFilter* ff = gtk_file_filter_new();
			gtk_file_filter_set_name(ff, "Memory files (.cdm, .hex)");
			gtk_file_filter_add_pattern(ff, "*.cdm");
			gtk_file_filter_add_pattern(ff, "*.hex");
			gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), ff);
			if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
				if (gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc))) {
					std::vector<unsigned long> before;
					const unsigned long n = std::min(words, 1UL << 16);
					for (unsigned long a = 0; a < n; a++) before.push_back(cl_ram_value(doc, gate, a));
					cl_ram_load_file(doc, gate, f);
					for (unsigned long a = 0; a < n; a++) {
						auto it = originals.find(a);
						const unsigned long old = it != originals.end() ? it->second : before[a];
						if (old == cl_ram_value(doc, gate, a)) originals.erase(a);
						else originals[a] = old;
					}
					w->edited();
					g_free(f);
				}
			}
			g_object_unref(fc);
		});
		foot("Save File…", [&] {
			finishEdit(true);
			GtkFileChooserNative* fc = gtk_file_chooser_native_new("Save Memory", GTK_WINDOW(sh.window), GTK_FILE_CHOOSER_ACTION_SAVE, "_Save",
			                                                       "_Cancel");
			gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
			gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), "Memory.cdm");
			if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
				if (gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc))) {
					cl_ram_save_file(doc, gate, f);
					g_free(f);
				}
			}
			g_object_unref(fc);
		});
		foot("Settings…", [&] { finishEdit(true); openSettings = true; sh.close(); });
		{
			const float tw = textWidth("Done", 13, true), kw = textWidth("↩", 10, true) + 10;
			const RectF r = rectF(W - 20 - (tw + kw + 7 + 32), fy, W - 20, fy + 32);
			fillRound(cr, r, 9, sh.hotNext() ? accent : withAlpha(accent, 0.92f));
			drawTextMid(cr, "Done", rectF(r.left + 16, r.top, r.right, r.bottom), 13, c.onAccent(), TextAlign::Leading, true);
			const RectF kr = rectF(r.left + 16 + tw + 7, r.top + 9, r.left + 16 + tw + 7 + kw, r.bottom - 9);
			fillRound(cr, kr, 4, withAlpha(c.onAccent(), 0.16f));
			drawTextMid(cr, "↩", kr, 10, c.onAccent(), TextAlign::Center, true);
			sh.hit(r, [&] { finishEdit(true); sh.close(); });
		}
	};

	s.onScroll = [&](Sheet&, float dy) {
		if (editing >= 0) finishEdit(true);
		scrollEasing = false;
		scrollY = std::max(0.0f, std::min(maxScroll(), scrollY + dy));
	};
	s.onKey = [&](Sheet& sh, guint k, guint) -> bool {
		const bool enter = k == GDK_KEY_Return || k == GDK_KEY_KP_Enter;
		if (editing >= 0) {
			if (enter) {
				// Enter keeps the value and moves on to the next word, as a spreadsheet does.
				const unsigned long next = (unsigned long)editing + 1;
				finishEdit(true);
				if (next < words) beginEdit(next);
				return true;
			}
			if (k == GDK_KEY_Escape) { finishEdit(false); gtk_widget_grab_focus(sh.area); return true; }
			if (k == GDK_KEY_Tab) { const unsigned long next = (unsigned long)editing + 1; finishEdit(true); if (next < words) beginEdit(next); return true; }
			return false;
		}
		if (jumpEntry && gtk_widget_has_focus(jumpEntry)) {
			if (enter || k == GDK_KEY_Escape) { gtk_widget_grab_focus(sh.area); return true; }
			return false;
		}
		if (enter) { sh.close(); return true; }
		if (k == GDK_KEY_Escape) { closeAsking(sh); return true; }
		if (k == GDK_KEY_Page_Down) { scrollTo(scrollY + viewRows() * kRowH, true); return true; }
		if (k == GDK_KEY_Page_Up) { scrollTo(scrollY - viewRows() * kRowH, true); return true; }
		if (k == GDK_KEY_Home) { scrollTo(0, true); return true; }
		if (k == GDK_KEY_End) { scrollTo(maxScroll(), true); return true; }
		return false;
	};

	// The words last read and written change as the circuit runs.
	struct Watch { std::function<void()> f; } watch{ [&] {
		const long r = cl_ram_last_read(doc, gate), wr = cl_ram_last_written(doc, gate);
		if (r == seenRead && wr == seenWritten) return;
		if (seenRead != -2) { flashedAt = anim::now(); s.animating = true; }
		seenRead = r;
		seenWritten = wr;
		s.redraw();
	} };
	const guint timer = g_timeout_add(200, +[](gpointer p) -> gboolean { static_cast<Watch*>(p)->f(); return G_SOURCE_CONTINUE; }, &watch);
	s.run(w->window());
	g_source_remove(timer);
	if (openSettings) showGateSettings(w, gate);
}
