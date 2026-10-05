// Your Circuits and Version History (see LibraryWindow.h).

#include "LibraryWindow.h"
#include "Alert.h"
#include "Dialogs.h"
#include "Library.h"
#include "Picker.h"
#include "SyncApp.h"
#include "SyncUI.h"
#include "Window.h"

#include <algorithm>

using namespace picker;

namespace {

bool windowAlive(CircuitWindow* w) {
	return std::find(circuitWindows().begin(), circuitWindows().end(), w) != circuitWindows().end();
}

std::vector<std::string> openIDs() {
	std::vector<std::string> ids;
	for (CircuitWindow* w : circuitWindows()) {
		library::Item it;
		if (library::itemFor(w->filePath(), it)) ids.push_back(it.id);
	}
	return ids;
}

std::string lowered(const std::string& s) {
	gchar* l = g_utf8_strdown(s.c_str(), -1);
	std::string out = l ? l : "";
	g_free(l);
	return out;
}

}  // namespace

void showYourCircuits(CircuitWindow* from) {
	if (from == nullptr) return;
	GtkApplication* app = from->application();
	std::vector<library::Item> all = library::items();
	Picker p;
	p.title = "Your Circuits";
	p.line = "Everything here saves itself. Use Import to bring in a .cdl file, and Export (in the title's menu) to get one out.";
	p.leftButtons = { "Import File…", "Rename…", "Delete…" };
	p.rightButtons = { "Cancel", "Open" };
	p.rows = [&](const std::string& query) {
		const std::string q = lowered(query);
		const std::vector<std::string> open = openIDs();
		std::vector<Row> out;
		for (const library::Item& it : all) {
			if (!q.empty() && lowered(it.name).find(q) == std::string::npos) continue;
			const int n = library::gateCount(it.circuit());
			// A circuit sync can't carry says so on its second line.
			const std::string problem = syncui::problemFor(it.id);
			out.push_back({ it.id, it.name,
			                problem.empty() ? format("%d gate%s · ", n, n == 1 ? "" : "s") + library::friendlyTime(it.modified) : problem,
			                std::find(open.begin(), open.end(), it.id) != open.end() ? "OPEN" : "" });
		}
		p.emptyText = all.empty() ? "Nothing here yet." : "No circuits match.";
		return out;
	};
	auto selectedItem = [&](library::Item& out) {
		const Row* r = p.selected();
		if (r == nullptr) return false;
		for (const library::Item& it : all) if (it.id == r->id) { out = it; return true; }
		return false;
	};
	std::string toOpen;
	bool import = false;
	p.onButton = [&](Picker& picker, int b) -> bool {
		library::Item it;
		switch (b) {
		case 0: import = true; return true;
		case 1: {
			if (!selectedItem(it)) return false;
			std::string name = it.name;
			if (askText(GTK_WINDOW(picker.window), "Rename Circuit", "The name it has in Your Circuits:", name) && !name.empty()) {
				library::rename(it, name);
				all = library::items();
				picker.reload();
				for (CircuitWindow* w : circuitWindows()) w->libraryChanged();
			}
			return false;
		}
		case 2: {
			if (!selectedItem(it)) return false;
			const std::vector<std::string> open = openIDs();
			const bool isOpen = std::find(open.begin(), open.end(), it.id) != open.end();
			if (!askConfirm(GTK_WINDOW(picker.window), "Delete “" + it.name + "”?",
			                std::string(isOpen ? "It's open, so its window will close. " : "") + "It and all its versions will be deleted.",
			                "Delete", "Cancel", true))
				return false;
			for (CircuitWindow* w : std::vector<CircuitWindow*>(circuitWindows())) {
				library::Item wi;
				if (library::itemFor(w->filePath(), wi) && wi.id == it.id && w != from) w->discard();
			}
			library::Item fi;
			if (windowAlive(from) && library::itemFor(from->filePath(), fi) && fi.id == it.id) from->replaceDocument(cl_document_new(), "");
			if (!library::moveToTrash(it))
				showMessage(GTK_WINDOW(picker.window), GTK_MESSAGE_WARNING, "Couldn't delete “" + it.name + "”", "");
			const int at = picker.selection;
			all = library::items();
			picker.reload();
			picker.select(at);
			return false;
		}
		case 100: return true;
		case 101: {
			if (!selectedItem(it)) return false;
			toOpen = it.circuit();
			return true;
		}
		default: return false;
		}
	};
	p.onKey = [&](Picker& picker, guint k, bool ctrl) -> bool {
		if (ctrl && (k == GDK_KEY_r || k == GDK_KEY_R)) { picker.onButton(picker, 1); picker.redraw(); return true; }
		if (ctrl && (k == GDK_KEY_i || k == GDK_KEY_I)) { import = true; picker.close(); return true; }
		if (k == GDK_KEY_Delete && !(picker.entry && gtk_widget_has_focus(picker.entry))) {
			picker.onButton(picker, 2);
			picker.redraw();
			return true;
		}
		return false;
	};
	// The sync line under the list, and the changes sync makes while it's open.
	bool openSync = false;
	p.footer = [] { const syncui::Line l = syncui::yourCircuitsLine(); return std::make_pair(l.text, l.button); };
	p.footerAction = [&] {
		const syncui::Line l = syncui::yourCircuitsLine();
		if (l.on) { syncui::yourCircuitsAction(from); return; }
		openSync = true;   // Settings can't be used while this window holds the pointer: it closes first
		p.close();
	};
	const guint listener = syncapp::addLibraryListener([&] {
		all = library::items();
		p.reload();
	});
	p.run(from->window());
	syncapp::removeListener(listener);
	if (!windowAlive(from)) from = nullptr;
	if (!toOpen.empty()) openCircuit(app, toOpen, from);
	else if (import) chooseAndOpen(app, from ? from->window() : nullptr);
	else if (openSync) syncui::yourCircuitsAction(from);
}

void showVersionHistory(CircuitWindow* window) {
	if (window == nullptr) return;
	library::Item item;
	if (!library::itemFor(window->filePath(), item)) {
		showMessage(window->window(), GTK_MESSAGE_INFO, "No versions yet",
		            "A circuit keeps versions once it's in Your Circuits: it joins as soon as there's something on it.");
		return;
	}
	window->save();   // the newest work is the circuit itself
	std::vector<library::Version> versions = library::versions(item);
	Picker p;
	p.title = item.name;
	p.search = false;
	p.width = 940;
	p.height = 620;
	p.listWidth = 300;
	p.line = versions.empty()
	             ? "No earlier versions yet. Your work saves itself as you go; a version is kept each time you press Ctrl+S, "
	               "when you come back after a break, and every half hour while you work."
	             : "Pick a version to see it. Restoring keeps your current one too, so you can always come back.";
	p.emptyText = "No versions yet.";
	p.leftButtons = { "Export Copy…" };
	p.rightButtons = { "Close", "Restore" };
	p.rows = [&](const std::string&) {
		std::vector<Row> out;
		for (size_t i = 0; i < versions.size(); i++) {
			const int n = library::gateCount(versions[i].path);
			// One sync made says where it came from.
			const std::string note = library::versionNote(versions[i]);
			out.push_back({ versions[i].path, library::friendlyTime(versions[i].time),
			                format("%d gate%s · ", n, n == 1 ? "" : "s") + library::agoText(versions[i].time) + (note.empty() ? "" : " · " + note),
			                i == 0 ? "NEWEST" : "" });
		}
		return out;
	};
	// The chosen version, loaded as its own document just to be drawn.
	CLDocument* shownDoc = nullptr;
	std::string shownPath;
	p.onSelect = [&](Picker& picker) {
		const Row* r = picker.selected();
		const std::string path = r ? r->id : std::string();
		if (path == shownPath) return;
		if (shownDoc) cl_document_close(shownDoc);
		shownDoc = nullptr;
		shownPath = path;
		if (!path.empty()) {
			char err[256];
			shownDoc = cl_document_open(path.c_str(), err, sizeof err);
		}
	};
	p.preview = [&](cairo_t* cr, const RectF& box) {
		if (shownDoc == nullptr) {
			drawText(cr, shownPath.empty() ? "" : "No picture of this version.", box, 12, Look{ prefs().dark }.ink(0.55f), TextAlign::Center);
			return;
		}
		double sx = 1, sy = 1;
		cairo_user_to_device_distance(cr, &sx, &sy);
		cairo_translate(cr, box.left, box.top);
		cl_document_draw_fitted(shownDoc, 0, cr, box.right - box.left, box.bottom - box.top, 16, sx, prefs().dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
	};
	bool restored = false;
	p.onButton = [&](Picker& picker, int b) -> bool {
		const int i = picker.selection;
		if (b == 100) return true;
		if (i < 0 || i >= (int)versions.size()) return false;
		if (b == 0) {
			// Named for when it was, without a clock's ':' (which a USB stick won't take).
			std::string when;
			if (GDateTime* t = g_date_time_new_from_unix_local((gint64)versions[i].time)) {
				if (gchar* s = g_date_time_format(t, "%Y-%m-%d %H.%M")) { when = s; g_free(s); }
				g_date_time_unref(t);
			}
			const std::string file = chooseSaveFile(GTK_WINDOW(picker.window), "Export a Copy",
			                                        safeFileName(item.name + (when.empty() ? "" : " (" + when + ")")) + ".cdl");
			gchar* data = nullptr;
			gsize len = 0;
			if (!file.empty() && (!g_file_get_contents(versions[i].path.c_str(), &data, &len, nullptr) ||
			                      !g_file_set_contents(file.c_str(), data, (gssize)len, nullptr)))
				showMessage(GTK_WINDOW(picker.window), GTK_MESSAGE_WARNING, "Couldn't save a copy there", "Try another folder.");
			g_free(data);
			return false;
		}
		if (b == 101) {
			if (!library::restore(item, versions[i])) {
				showMessage(GTK_WINDOW(picker.window), GTK_MESSAGE_WARNING, "Couldn't restore that version", "");
				return false;
			}
			restored = true;
			return true;
		}
		return false;
	};
	p.onKey = [&](Picker& picker, guint k, bool ctrl) -> bool {
		if (ctrl && (k == GDK_KEY_e || k == GDK_KEY_E)) { picker.onButton(picker, 0); return true; }
		return false;
	};
	p.run(window->window());
	if (shownDoc) cl_document_close(shownDoc);
	if (restored && windowAlive(window)) window->reloadFromDisk("Restored that version. The one before it was kept too.");
}
