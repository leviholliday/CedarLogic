// --bench-drag (see Bench.h and linux/SMOOTHNESS.md).
//
// Two parts:
//  1. Step by step: a pointer move goes through Canvas::onMotion (the engine
//     moves the parts), then Canvas::draw draws the whole canvas into an image
//     the size of the window's, at scale 1 and 2, both ways it can draw (through
//     its `frame`, as on X11; straight onto the window's image, as on
//     Wayland). Nothing else runs in between, so these are the canvas's own
//     costs per frame.
//  2. Live: one pointer move per tick of the window's frame clock, drawn by
//     GTK as a real drag is, at the screen's own scale and the way this
//     display draws; the canvas's draw times and the time between frames.
//
// Everything dragged is put back (cl_edit_cancel), nothing is saved.

#include "Bench.h"
#include "Canvas.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

double nowMs() { return g_get_monotonic_time() / 1000.0; }

// Lets the main loop run for a while: layout, the window's first frames.
void pump(double ms) {
	const double end = nowMs() + ms;
	while (nowMs() < end) {
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(2000);
	}
}

struct Summary {
	double mean = 0, median = 0, p95 = 0, worst = 0;
	size_t n = 0;
};

Summary summarize(std::vector<double> v) {
	Summary s;
	s.n = v.size();
	if (v.empty()) return s;
	std::sort(v.begin(), v.end());
	double total = 0;
	for (double x : v) total += x;
	s.mean = total / v.size();
	s.median = v[v.size() / 2];
	s.p95 = v[std::min(v.size() - 1, (size_t)std::ceil(v.size() * 0.95) - 1)];
	s.worst = v.back();
	return s;
}

void printRow(const char* label, const Summary& draw, const Summary& handler) {
	printf("  %-30s %7.2f %7.2f %7.2f %7.2f   %7.2f\n", label, draw.mean, draw.median, draw.p95, draw.worst, handler.mean);
}

}  // namespace

struct CanvasBench {
	CircuitWindow* w;
	Canvas* c;
	CLDocument* doc;
	int page;

	GdkEvent* event(GdkEventType type, double x, double y, guint button) {
		GdkEvent* e = gdk_event_new(type);
		GdkWindow* gw = gtk_widget_get_window(c->area);
		const guint32 time = (guint32)(g_get_monotonic_time() / 1000);
		if (type == GDK_MOTION_NOTIFY) {
			e->motion.window = gw ? GDK_WINDOW(g_object_ref(gw)) : nullptr;
			e->motion.time = time;
			e->motion.x = x;
			e->motion.y = y;
			e->motion.state = button == 2 ? GDK_BUTTON2_MASK : GDK_BUTTON1_MASK;
		} else {
			e->button.window = gw ? GDK_WINDOW(g_object_ref(gw)) : nullptr;
			e->button.time = time;
			e->button.x = x;
			e->button.y = y;
			e->button.button = button;
		}
		return e;
	}
	void press(double x, double y, guint button) {
		GdkEvent* e = event(GDK_BUTTON_PRESS, x, y, button);
		c->onPress(&e->button);
		gdk_event_free(e);
	}
	// One pointer move, as GTK hands it over; how long the handler took.
	double move(double x, double y, guint button) {
		GdkEvent* e = event(GDK_MOTION_NOTIFY, x, y, button);
		const double t0 = nowMs();
		c->onMotion(&e->motion);
		const double t = nowMs() - t0;
		gdk_event_free(e);
		return t;
	}
	// Ends a drag without keeping it: the parts go back where they were.
	void endDrag(guint button) {
		if (c->drag == Canvas::Drag::Edit) {
			cl_edit_cancel(doc);
			c->drag = Canvas::Drag::None;
		} else if (c->drag == Canvas::Drag::Pan) {
			GdkEvent* e = event(GDK_BUTTON_RELEASE, c->lastX, c->lastY, button);
			c->onRelease(&e->button);
			gdk_event_free(e);
		}
		c->drag = Canvas::Drag::None;
	}

	// A view point over a single gate, nearest the middle (false: none).
	bool findGate(double& gx, double& gy) {
		const double vw = c->width(), vh = c->height();
		std::vector<std::pair<double, std::pair<double, double>>> spots;
		for (int iy = 1; iy < 24; iy++)
			for (int ix = 1; ix < 36; ix++) {
				const double x = vw * ix / 36, y = vh * iy / 24;
				spots.push_back({ std::hypot(x - vw / 2, y - vh / 2), { x, y } });
			}
		std::sort(spots.begin(), spots.end());
		for (const auto& s : spots) {
			double wx, wy;
			c->worldPoint(s.second.first, s.second.second, wx, wy);
			const int r = cl_edit_press(doc, page, wx, wy, 0, c->upp);
			const bool gate = r == CL_PRESS_PART && cl_edit_selected_gate_count(doc, page) == 1 &&
			                  cl_edit_selected_wire_count(doc, page) == 0;
			cl_edit_cancel(doc);
			cl_edit_select_none(doc, page);
			if (gate) { gx = s.second.first; gy = s.second.second; return true; }
		}
		return false;
	}

	// Where the pointer is at step i of n: a figure of eight from (x0, y0).
	void pathPoint(int i, int n, double x0, double y0, double& x, double& y) {
		const double r = std::min(c->width(), c->height()) / 6;
		const double a = 4 * G_PI * (i + 1) / std::max(1, n);
		x = x0 + r * std::sin(a);
		y = y0 + r * std::sin(2 * a) / 2;
	}

	// Part 1: one kind of drag at one scale, one way of drawing.
	void stepped(const char* label, int steps, int scale, int path, bool fast, int kind, double gx, double gy, bool whole = false,
	             bool noImage = false) {
		const Canvas::View saved = c->view();
		c->scaleOverride = scale;
		c->pathOverride = path;
		c->fastDrag = fast;
		const bool keepAll = c->drawAll, keepImages = c->imagesDisabled;
		c->drawAll = whole || keepAll;
		c->imagesDisabled = noImage || keepImages;
		c->layerFrames = c->layerBuilds = 0;
		c->imageFrames = c->imageBuilds = 0;
		const int pw = (int)std::ceil(c->width() * scale), ph = (int)std::ceil(c->height() * scale);
		// The window's own image: ARGB, as Wayland's buffers are.
		cairo_surface_t* target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, pw), std::max(1, ph));
		cairo_surface_set_device_scale(target, scale, scale);
		auto drawOnce = [&]() -> double {
			cairo_t* cr = cairo_create(target);
			const double t0 = nowMs();
			c->draw(cr);
			cairo_surface_flush(target);
			const double t = nowMs() - t0;
			cairo_destroy(cr);
			return t;
		};
		const guint button = kind == 2 ? 2 : 1;
		if (kind == 1) cl_edit_select_all(doc, page);
		press(gx, gy, button);
		if (kind == 1 && cl_edit_selected_gate_count(doc, page) < 2)
			printf("  (pressing a gate kept only %d of the selection)\n", cl_edit_selected_gate_count(doc, page));
		// A new selection's halo fades in first (0.13 s), as before any
		// real drag; while it does, the moving parts are drawn each frame.
		if (kind != 2) settleFade();
		drawOnce();   // the first frame makes the images; not counted
		std::vector<double> draws, handlers;
		for (int i = 0; i < steps; i++) {
			double x, y;
			pathPoint(i, steps, gx, gy, x, y);
			handlers.push_back(move(x, y, button));
			draws.push_back(drawOnce());
		}
		endDrag(button);
		cl_edit_select_none(doc, page);
		c->setView(saved);
		c->scaleOverride = c->pathOverride = 0;
		c->fastDrag = false;
		c->drawAll = keepAll;
		c->imagesDisabled = keepImages;
		cairo_surface_destroy(target);
		printRow(label, summarize(draws), summarize(handlers));
		if (kind != 2 && !whole && !keepAll)
			printf("  %-30s (%d of %d frames from the kept layer, %d layer%s made; %d with the moving parts' image, %d made)\n", "",
			       c->layerFrames, steps, c->layerBuilds, c->layerBuilds == 1 ? "" : "s", c->imageFrames, c->imageBuilds);
	}

	// Until a new selection's halo has faded in (CircuitWindow::selectionFade).
	void settleFade() {
		const double giveUp = nowMs() + 1000;
		while (w->selectionFade() < 1 && nowMs() < giveUp) pump(10);
	}

	// Whether static + moving looks like the page drawn whole: one gate moved
	// along part of the path, then the canvas drawn both ways at scale 1.
	// They differ only where the moving gate overlaps something drawn after
	// it in id order (it's on top now).
	void compareLayers(double gx, double gy, bool all) {
		const Canvas::View saved = c->view();
		c->scaleOverride = 1;
		c->pathOverride = 2;
		const int pw = (int)std::ceil(c->width()), ph = (int)std::ceil(c->height());
		auto picture = [&](bool whole) {
			cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, pw), std::max(1, ph));
			c->drawAll = whole;
			if (whole) c->dropLayer();
			cairo_t* cr = cairo_create(s);
			c->draw(cr);
			cairo_destroy(cr);
			cairo_surface_flush(s);
			return s;
		};
		if (all) cl_edit_select_all(doc, page);
		press(gx, gy, 1);
		settleFade();
		for (int i = 0; i < 12; i++) {
			double x, y;
			pathPoint(i, 48, gx, gy, x, y);
			move(x, y, 1);
			cairo_surface_destroy(picture(false));
		}
		c->layerFrames = c->imageFrames = 0;
		cairo_surface_t* layered = picture(false);
		const int fromLayer = c->layerFrames, fromImage = c->imageFrames;
		cairo_surface_t* whole = picture(true);
		// CEDARLOGIC_BENCH_PICTURES=<folder>: both pictures, to look at.
		if (const char* dir = g_getenv("CEDARLOGIC_BENCH_PICTURES")) {
			const std::string base = std::string(dir) + (all ? "/compare-all-" : "/compare-one-");
			cairo_surface_write_to_png(layered, (base + "layered.png").c_str());
			cairo_surface_write_to_png(whole, (base + "whole.png").c_str());
		}
		long differ = 0, faint = 0;
		int most = 0;
		const int stride = cairo_image_surface_get_stride(layered);
		const unsigned char* a = cairo_image_surface_get_data(layered);
		const unsigned char* b = cairo_image_surface_get_data(whole);
		for (int y = 0; y < ph; y++)
			for (int x = 0; x < pw; x++) {
				const unsigned char* pa = a + y * stride + x * 4;
				const unsigned char* pb = b + y * stride + x * 4;
				int d = 0;
				for (int k = 0; k < 3; k++) d = std::max(d, std::abs((int)pa[k] - (int)pb[k]));
				if (d > 24) differ++;
				else if (d > 0) faint++;
				most = std::max(most, d);
			}
		printf("%sStatic + moving against the page drawn whole (%s, scale 1%s): %ld of %ld pixels differ (%.3f%%) by more than 24 "
		       "of 255, %ld by 24 or less, at most %d%s\n",
		       all ? "" : "\n", all ? "everything moved" : "one gate moved", fromImage ? ", the moving parts from their image" : "", differ,
		       (long)pw * ph, 100.0 * differ / std::max(1L, (long)pw * ph), faint, most, fromLayer ? "" : "  [the layer wasn't used!]");
		cairo_surface_destroy(layered);
		cairo_surface_destroy(whole);
		c->drawAll = false;
		endDrag(1);
		cl_edit_select_none(doc, page);
		c->setView(saved);
		c->scaleOverride = c->pathOverride = 0;
	}

	// Part 2: a drag on the frame clock, drawn by GTK.
	struct Live {
		CanvasBench* b;
		int i = 0, n = 0;
		double gx = 0, gy = 0, last = -1;
		std::vector<double> intervals, handlers;
		bool done = false;
	};
	static gboolean liveTick(GtkWidget*, GdkFrameClock* clock, gpointer data) {
		Live* l = static_cast<Live*>(data);
		const double t = gdk_frame_clock_get_frame_time(clock) / 1000.0;
		if (l->last >= 0) l->intervals.push_back(t - l->last);
		l->last = t;
		if (l->i >= l->n) { l->done = true; return G_SOURCE_REMOVE; }
		double x, y;
		l->b->pathPoint(l->i, l->n, l->gx, l->gy, x, y);
		l->handlers.push_back(l->b->move(x, y, 1));
		l->i++;
		return G_SOURCE_CONTINUE;
	}
	void live(const char* label, int steps, bool all, double gx, double gy) {
		std::vector<double> draws;
		Live l;
		l.b = this;
		l.n = steps;
		l.gx = gx;
		l.gy = gy;
		if (all) cl_edit_select_all(doc, page);
		press(gx, gy, 1);
		pump(100);
		settleFade();
		c->drawTimes = &draws;
		const guint tick = gtk_widget_add_tick_callback(c->area, liveTick, &l, nullptr);
		const double giveUp = nowMs() + 60000;
		while (!l.done && nowMs() < giveUp) g_main_context_iteration(nullptr, TRUE);
		if (!l.done) gtk_widget_remove_tick_callback(c->area, tick);   // it points at `l`
		pump(50);
		c->drawTimes = nullptr;
		endDrag(1);
		cl_edit_select_none(doc, page);
		c->redraw();
		if (!l.done) printf("  %-30s (the frame clock stopped; is the window on screen?)\n", label);
		const Summary d = summarize(draws), f = summarize(l.intervals);
		printRow(label, d, summarize(l.handlers));
		printf("  %-30s %7.2f %7.2f %7.2f %7.2f   (%.0f frames a second, %zu draws for %d moves)\n", "  time between frames",
		       f.mean, f.median, f.p95, f.worst, f.mean > 0 ? 1000 / f.mean : 0.0, d.n, steps);
	}
};

namespace bench {

int dragBenchmark(CircuitWindow* window, int steps) {
	Canvas* canvas = window ? window->currentCanvas() : nullptr;
	if (canvas == nullptr || canvas->page() < 0) { fprintf(stderr, "bench-drag: no circuit to drag\n"); return 1; }
	steps = std::max(5, steps);
	// The same size everywhere, so numbers from different machines compare.
	gtk_window_resize(window->window(), 1440, 900);
	pump(600);
	canvas->zoomToFit(false);
	pump(200);

	CanvasBench b{ window, canvas, window->document(), canvas->page() };
	double gx = 0, gy = 0;
	if (!b.findGate(gx, gy)) { fprintf(stderr, "bench-drag: no gate in view to drag\n"); return 1; }
	cl_edit_select_all(b.doc, b.page);
	const int gates = cl_edit_selected_gate_count(b.doc, b.page), wires = cl_edit_selected_wire_count(b.doc, b.page);
	cl_edit_select_none(b.doc, b.page);

	GdkDisplay* display = gtk_widget_get_display(canvas->widget());
	const int screenScale = gtk_widget_get_scale_factor(canvas->widget());
	printf("CedarLogic drag benchmark: %s, %d gates, %d wires\n", window->titleText().c_str(), gates, wires);
	printf("canvas %d x %d points; screen scale %d; %s; GTK %u.%u.%u; cairo %s; %d steps each\n",
	       (int)canvas->width(), (int)canvas->height(), screenScale, display ? G_OBJECT_TYPE_NAME(display) : "no display",
	       gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(), cairo_version_string(), steps);
	printf("16.7 ms a frame is 60 frames a second; 33 ms is 30.\n");

	const char* header = "  %-30s %7s %7s %7s %7s   %7s\n";
	struct Kind { int id; const char* name; };
	const Kind kinds[] = { { 0, "one gate" }, { 1, "select all" }, { 2, "pan" } };
	for (int scale : { 1, 2 }) {
		printf("\nStep by step at scale %d (%d x %d pixels), ms per frame:\n", scale,
		       (int)std::ceil(canvas->width() * scale), (int)std::ceil(canvas->height() * scale));
		printf(header, "", "mean", "median", "p95", "worst", "handler");
		for (const Kind& k : kinds) {
			const std::string frame = std::string(k.name) + ", through frame";
			const std::string direct = std::string(k.name) + ", straight";
			const std::string fast = std::string(k.name) + ", straight + fast AA";
			const std::string whole = std::string(k.name) + ", straight, whole page";
			b.stepped(frame.c_str(), steps, scale, 1, false, k.id, gx, gy);
			b.stepped(direct.c_str(), steps, scale, 2, false, k.id, gx, gy);
			if (k.id != 2) b.stepped(fast.c_str(), steps, scale, 2, true, k.id, gx, gy);
			// Every frame drawn whole, as before the static layer (CEDARLOGIC_DRAW_ALL=1).
			if (k.id != 2) b.stepped(whole.c_str(), steps, scale, 2, false, k.id, gx, gy, true);
			// The moving parts drawn every frame, as before their image (CEDARLOGIC_DRAG_IMAGE=0).
			if (k.id != 2) b.stepped((std::string(k.name) + ", straight, no image").c_str(), steps, scale, 2, false, k.id, gx, gy, false, true);
		}
	}
	b.compareLayers(gx, gy, false);
	b.compareLayers(gx, gy, true);

	printf("\nLive, on the frame clock, at the screen's scale (%d), the way this display draws:\n", screenScale);
	printf(header, "", "mean", "median", "p95", "worst", "handler");
	b.live("one gate: canvas draw", steps, false, gx, gy);
	b.live("select all: canvas draw", steps, true, gx, gy);
	printf("\n\"through frame\" is how X11 draws (and every display did before);\n"
	       "\"straight\" is how Wayland draws now. \"fast AA\" is CEDARLOGIC_FAST_DRAG=1 (off by default).\n"
	       "\"whole page\" draws every part every frame, as before the kept static layer (CEDARLOGIC_DRAW_ALL=1).\n"
	       "\"no image\" draws the moving parts every frame, as before their kept image (CEDARLOGIC_DRAG_IMAGE=0).\n");
	fflush(stdout);
	return 0;
}

}  // namespace bench
