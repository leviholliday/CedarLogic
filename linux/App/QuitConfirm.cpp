// Ctrl+Q's question (see QuitConfirm.h).

#include "QuitConfirm.h"
#include "Brand.h"
#include "Sheet.h"

#include <cmath>
#include <functional>

bool confirmQuitting(GtkWindow* parent) {
	if (!prefs().confirmQuit) return true;
	const float pad = 40, cw = 420, ch = 196;
	int answer = 0;   // 1 quit, 2 always
	const double shown = anim::now();
	Sheet s;
	s.title = "Quit CedarLogic";
	s.width = (int)(cw + 2 * pad);
	s.height = (int)(ch + 2 * pad);
	s.minWidth = s.width;
	s.minHeight = s.height;
	s.resizable = false;
	s.decorated = false;
	s.transparent = true;
	s.animating = true;
	s.onTick = [&](Sheet& sh) { if (anim::now() - shown > 0.5) sh.animating = false; };
	s.paint = [&](Sheet& sh, cairo_t* cr, float w, float h) {
		const bool dark = prefs().dark;
		const Color ink = dark ? colorF(0.94f, 0.94f, 0.94f) : colorF(0.1f, 0.1f, 0.1f);
		const Color dim = dark ? colorF(0.64f, 0.64f, 0.64f) : colorF(0.4f, 0.4f, 0.4f);
		// The Mac's arrival: solid in about 0.14 s, its drop easing into place over 0.35 s.
		const double t = anim::now() - shown;
		const float appear = (float)anim::easeOut(t / 0.14), drop = (float)(1 - anim::easeOut(t / 0.35));
		const float ox = sh.composited ? pad : 0, oy = (sh.composited ? pad : 0) - 20 * drop;
		const RectF card = sh.composited ? rectF(ox, oy, ox + cw, oy + ch) : rectF(0, 0, w, h);
		cairo_save(cr);
		cairo_push_group(cr);
		if (sh.composited) brand::glow(cr, rectF(card.left, card.top + 10, card.right, card.bottom + 10), 18, colorF(0, 0, 0, 0.35f), 22);
		fillRound(cr, card, sh.composited ? 18 : 0, dark ? colorF(0.075f, 0.085f, 0.1f, 0.97f) : colorF(0.97f, 0.975f, 0.98f, 0.98f));
		strokeRound(cr, card, sh.composited ? 18 : 0, colorF(1, 1, 1, dark ? 0.1f : 0.5f));
		const float x = card.left + 22, y = card.top + 22;
		brand::icon(cr, x, y, 38);
		drawText(cr, "Are you sure you want to quit CedarLogic?", rectF(x, y + 50, card.right - 22, y + 74), 17, ink, TextAlign::Leading, true);
		drawText(cr, "Your circuits are saved; they'll be here when you come back.", rectF(x, y + 76, card.right - 22, y + 94), 12.5f, dim);
		auto pill = [&](const std::string& label, const char* key, float right, bool primary, std::function<void()> act) {
			const float tw = textWidth(label, 13, true) + (key ? textWidth(key, 10, true) + 17 : 0) + 28;
			const RectF r = rectF(right - tw, card.bottom - 22 - 34, right, card.bottom - 22);
			const bool hot = sh.hotNext();
			fillRound(cr, r, 9, primary ? withAlpha(brand::kNeonDeep, hot ? 1.0f : 0.92f) : withAlpha(ink, hot ? 0.12f : 0.08f));
			const Color fg = primary ? colorF(1, 1, 1) : ink;
			drawTextMid(cr, label, rectF(r.left + 14, r.top, r.right, r.bottom), 13, fg, TextAlign::Leading, true);
			if (key) {
				const float kx = r.left + 14 + textWidth(label, 13, true) + 7;
				const RectF kr = rectF(kx, r.top + 9, kx + textWidth(key, 10, true) + 10, r.bottom - 9);
				fillRound(cr, kr, 4, withAlpha(fg, 0.14f));
				drawTextMid(cr, key, kr, 10, fg, TextAlign::Center, true);
			}
			sh.hit(r, act);
			return r.left;
		};
		float right = card.right - 22;
		right = pill("Quit", "↩", right, true, [&] { answer = 1; sh.close(); }) - 10;
		pill("Cancel", "esc", right, false, [&] { sh.close(); });
		const float aw = textWidth("Always Quit", 13, true) + 28;
		pill("Always Quit", nullptr, card.left + 22 + aw, false, [&] { answer = 2; sh.close(); });
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, appear);
		cairo_restore(cr);
	};
	s.onKey = [&](Sheet& sh, guint k, guint) -> bool {
		if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) { answer = 1; sh.close(); return true; }
		return false;
	};
	s.run(parent);
	if (answer == 2) {
		prefs().confirmQuit = false;
		prefs().save();
	}
	return answer != 0;
}
