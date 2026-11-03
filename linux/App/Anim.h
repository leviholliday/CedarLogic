// Motion, as the Mac app has it: eased tweens (SwiftUI's .easeOut and
// .easeInOut), springs (SwiftUI's .spring(response:dampingFraction:)), and a
// fade per hovered item, all driven by the frame clock and asked for their
// value when drawing. Nothing here moves when the desktop has animations
// turned off.

#ifndef CL_LINUX_ANIM_H
#define CL_LINUX_ANIM_H

#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace anim {

inline double now() { return g_get_monotonic_time() / 1e6; }

// The desktop's "animations" switch (GNOME's, and GTK's gtk-enable-animations).
inline bool enabled() {
	gboolean on = TRUE;
	if (GtkSettings* s = gtk_settings_get_default()) g_object_get(s, "gtk-enable-animations", &on, nullptr);
	return on;
}

inline double clamp01(double t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
inline double easeOut(double t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
inline double easeInOut(double t) { t = clamp01(t); return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }

// A value easing from where it is to a target over `duration` seconds.
struct Tween {
	double from = 0, to = 0, start = -1, duration = 0.2;
	bool inOut = false;

	double value() const {
		if (start < 0 || duration <= 0) return to;
		const double t = (now() - start) / duration;
		if (t >= 1) return to;
		return from + (to - from) * (inOut ? easeInOut(t) : easeOut(t));
	}
	bool active() const { return start >= 0 && now() - start < duration; }
	void go(double target, double seconds, bool easeInOutCurve = false) {
		if (target == to && (active() || value() == target)) return;
		from = value();
		to = target;
		duration = enabled() ? seconds : 0;
		inOut = easeInOutCurve;
		start = now();
	}
	void set(double v) { from = to = v; start = -1; }
};

// A spring, as SwiftUI's: `response` seconds for one swing, `damping` 1 for
// no overshoot. Stepped once a frame.
struct Spring {
	double x = 0, v = 0, target = 0;
	double response = 0.3, damping = 0.86;
	double lastStep = -1;
	bool settled() const { return std::fabs(x - target) < 0.05 && std::fabs(v) < 0.5; }
	void snap(double to) { x = target = to; v = 0; lastStep = -1; }
	// Advances to now; true while it's still moving.
	bool step() {
		const double t = now();
		double dt = lastStep < 0 ? 1.0 / 60 : t - lastStep;
		lastStep = t;
		if (!enabled()) { x = target; v = 0; return false; }
		if (settled()) { x = target; v = 0; lastStep = -1; return false; }
		dt = dt > 0.05 ? 0.05 : dt;
		const double k = std::pow(2 * G_PI / response, 2), c = 4 * G_PI * damping / response;
		// A few small steps keep it steady at any frame rate.
		const int n = 4;
		for (int i = 0; i < n; i++) {
			const double h = dt / n;
			const double a = -k * (x - target) - c * v;
			v += a * h;
			x += v * h;
		}
		return true;
	}
};

// A fade in and out for each of a set of items (the one under the pointer
// fades up in `in` seconds, the one it left fades down).
struct HoverFade {
	std::map<int, Tween> items;
	int hot = -1;
	double in = 0.12, out = 0.18;
	void setHot(int id) {
		if (id == hot) return;
		if (hot >= 0) items[hot].go(0, out);
		hot = id;
		if (id >= 0) items[id].go(1, in);
	}
	double amount(int id) const {
		auto it = items.find(id);
		return it == items.end() ? 0 : it->second.value();
	}
	bool active() const {
		for (const auto& kv : items) if (kv.second.active()) return true;
		return false;
	}
};

// A frame that took this long says the computer (a Raspberry Pi, a desktop
// without a compositor) can't keep up: a fade then jumps to its end rather
// than stutter.
const double kSlowFrame = 0.1;

// A widget's opacity eased to `target` over `seconds` by the clock (not by
// counting frames, so a slow screen doesn't drag it out), then `done`. Any
// fade it already had stops. Without animations, or once a frame is slow, it
// jumps to the end. Cheap: it only sets the opacity each frame.
struct Fade {
	double from = 0, to = 1, start = 0, seconds = 0.16, last = 0;
	void (*done)(GtkWidget*, gpointer) = nullptr;
	gpointer data = nullptr;
};
inline void fadeTo(GtkWidget* w, double target, double seconds, void (*done)(GtkWidget*, gpointer) = nullptr, gpointer data = nullptr) {
	if (w == nullptr) return;
	if (guint old = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "cl-fade"))) gtk_widget_remove_tick_callback(w, old);
	g_object_set_data(G_OBJECT(w), "cl-fade", nullptr);
	if (!enabled() || seconds <= 0 || !gtk_widget_get_mapped(w)) {
		gtk_widget_set_opacity(w, target);
		if (done) done(w, data);
		return;
	}
	Fade* f = new Fade();
	f->from = gtk_widget_get_opacity(w);
	f->to = target;
	f->start = f->last = -1;
	f->seconds = seconds;
	f->done = done;
	f->data = data;
	const guint id = gtk_widget_add_tick_callback(w, [](GtkWidget* w, GdkFrameClock*, gpointer p) -> gboolean {
		Fade* f = static_cast<Fade*>(p);
		const double t = now();
		// Timed from its first frame (which can come late, as a window maps);
		// slow means a gap between two of its frames.
		if (f->start < 0) f->start = t;
		const bool slow = f->last >= 0 && t - f->last > kSlowFrame;
		f->last = t;
		const double k = slow ? 1 : clamp01((t - f->start) / f->seconds);
		gtk_widget_set_opacity(w, f->from + (f->to - f->from) * easeOut(k));
		if (k < 1) return G_SOURCE_CONTINUE;
		g_object_set_data(G_OBJECT(w), "cl-fade", nullptr);
		if (f->done) f->done(w, f->data);
		return G_SOURCE_REMOVE;
	}, f, [](gpointer p) { delete static_cast<Fade*>(p); });
	g_object_set_data(G_OBJECT(w), "cl-fade", GUINT_TO_POINTER(id));
}

// A window (or dialog) fading in as it arrives, as the Mac's sheets do.
// Its first frame is already transparent, so it never flashes in solid.
inline void fadeIn(GtkWidget* toplevel, double seconds = 0.16) {
	if (toplevel == nullptr || !enabled()) return;
	gtk_widget_set_opacity(toplevel, 0);
	if (gtk_widget_get_mapped(toplevel)) { fadeTo(toplevel, 1, seconds); return; }
	// Not on screen yet: the fade starts with its first frame.
	g_signal_connect(toplevel, "map", G_CALLBACK(+[](GtkWidget* w, gpointer data) {
		g_signal_handlers_disconnect_by_data(w, data);
		fadeTo(w, 1, *static_cast<double*>(data));
		delete static_cast<double*>(data);
	}), new double(seconds));
}

}  // namespace anim

#endif  // CL_LINUX_ANIM_H
