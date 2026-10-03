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

// A window (or dialog) fading in as it arrives, as the Mac's sheets do.
inline void fadeIn(GtkWidget* toplevel, double seconds = 0.16) {
	if (toplevel == nullptr || !enabled()) return;
	gtk_widget_set_opacity(toplevel, 0);
	const double step = 1.0 / (seconds * 60);
	gtk_widget_add_tick_callback(toplevel, [](GtkWidget* w, GdkFrameClock*, gpointer data) -> gboolean {
		const double s = *static_cast<double*>(data);
		const double o = std::min(1.0, gtk_widget_get_opacity(w) + s);
		gtk_widget_set_opacity(w, o);
		return o < 1 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
	}, new double(step), [](gpointer d) { delete static_cast<double*>(d); });
}

}  // namespace anim

#endif  // CL_LINUX_ANIM_H
