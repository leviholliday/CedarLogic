// Your Circuits and Version History (see LibraryWindow.h).

#include "LibraryWindow.h"
#include "Alert.h"
#include "Chrome.h"
#include "Picker.h"
#include "Dialogs.h"
#include "Library.h"
#include "SyncApp.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>

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

}  // namespace

void showYourCircuits(CircuitWindow* from) {
	if (from == nullptr) return;
	std::vector<library::Item> all = library::items();
	Picker p;
	p.title = "Your Circuits";
	p.line = "Everything here saves itself. Use Import to bring in a .cdl file, and Export (in the title's menu) to get one out.";
	p.leftButtons = { "Import File…", "Rename…", "Delete…" };
	p.rightButtons = { "Cancel", "Open" };
	p.rows = [&](const std::string& query) {
		const std::string q = lowerCase(query);
		const std::vector<std::string> open = openIDs();
		// A circuit sync can't keep says so on its second line ("Too big to sync (over 512 KB)").
		std::map<std::string, std::string> problems;
		for (const auto& pr : syncapp::status().problems) problems[pr.first] = pr.second;
		std::vector<Row> out;
		for (const library::Item& it : all) {
			if (!q.empty() && lowerCase(it.name).find(q) == std::string::npos) continue;
			const int n = library::gateCount(it.circuit());
			auto problem = problems.find(it.id);
			out.push_back({ it.id, it.name,
			                problem != problems.end() ? problem->second
			                                          : strf("%d gate%s · ", n, n == 1 ? "" : "s") + library::friendlyTime(it.modified),
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
			if (askText(picker.hwnd, "Rename Circuit", "The name it has in Your Circuits:", name) && !name.empty()) {
				library::rename(it, name);
				all = library::items();
				picker.reload();
				for (CircuitWindow* w : circuitWindows()) w->libraryChanged();
			}
			return false;
		}
		case 2: {
			if (!selectedItem(it)) return false;
			// Its window can't close while something is open over it (gate
			// settings, a list, a card): that code is waiting on it, and would
			// carry on with the circuit gone. Asked again after the question,
			// which other windows go on working under.
			auto busyElsewhere = [&] {
				for (CircuitWindow* w : circuitWindows()) {
					library::Item wi;
					if (w != from && !IsWindowEnabled(w->window()) && library::itemFor(w->filePath(), wi) && wi.id == it.id) {
						showMessage(picker.hwnd, Tone::Warning, "“" + it.name + "” has something open over it",
						            "Close what's open in its window first, then delete it.");
						return true;
					}
				}
				return false;
			};
			if (busyElsewhere()) return false;
			const std::vector<std::string> open = openIDs();
			const bool isOpen = std::find(open.begin(), open.end(), it.id) != open.end();
			if (!askConfirm(picker.hwnd, "Delete “" + it.name + "”?",
			                std::string(isOpen ? "It's open, so its window will close. " : "") + "It and all its versions will be deleted.",
			                "Delete", "Cancel", true))
				return false;
			if (busyElsewhere()) return false;
			for (CircuitWindow* w : std::vector<CircuitWindow*>(circuitWindows())) {
				library::Item wi;
				if (library::itemFor(w->filePath(), wi) && wi.id == it.id && w != from) w->discard();
			}
			bool fromClosed = false;
			library::Item fi;
			if (windowAlive(from) && library::itemFor(from->filePath(), fi) && fi.id == it.id) {
				from->replaceDocument(cl_document_new(), "");
				fromClosed = true;
			}
			if (!library::moveToTrash(it)) showMessage(picker.hwnd, Tone::Warning, "Couldn't delete “" + it.name + "”", "");
			(void)fromClosed;
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
	p.onKey = [&](Picker& picker, UINT vk, bool ctrl) -> bool {
		if (ctrl && vk == 'R') { picker.onButton(picker, 1); picker.redraw(); return true; }
		if (ctrl && vk == 'I') { import = true; picker.close(); return true; }
		// Delete: the circuit, unless it's for the search box's text.
		if (vk == VK_DELETE && (GetFocus() != picker.edit || GetWindowTextLengthW(picker.edit) == 0)) {
			picker.onButton(picker, 2);
			picker.redraw();
			return true;
		}
		return false;
	};
	// The sync line under the list: how sync is doing and Sync Now, or a quiet
	// way into Settings > Sync while it's off.
	struct SyncLine { D2D1_RECT_F button = D2D1::RectF(0, 0, 0, 0); };
	auto line = std::make_shared<SyncLine>();
	p.footerHeight = 28;
	p.footer = [line](ID2D1RenderTarget* rt, const D2D1_RECT_F& r) {
		const Look look{ prefs().dark };
		const D2D1_COLOR_F accent = chrome().accent();
		const bool on = syncapp::enabled();
		const clsync::Status st = syncapp::status();
		drawIcon(rt, 0xE895, D2D1::RectF(r.left, r.top, r.left + 24, r.bottom), 13, on ? accent : look.ink(0.45f));
		if (!on) {
			line->button = D2D1::RectF(0, 0, 0, 0);
			drawText(rt, "Sync…", D2D1::RectF(r.left + 26, r.top, r.right - 8, r.bottom), 12.5f, look.ink(0.6f), TextAlign::Leading);
			return;
		}
		const std::string text = syncapp::statusLine(st);
		const std::string notice = syncapp::recentNotice();
		const float bw = std::max(84.0f, textWidth("Sync Now", 12) + 28), bh = 24;
		line->button = D2D1::RectF(r.right - bw, r.top + (r.bottom - r.top - bh) / 2, r.right, r.top + (r.bottom - r.top - bh) / 2 + bh);
		drawText(rt, notice.empty() ? text : notice, D2D1::RectF(r.left + 26, r.top, line->button.left - 10, r.bottom), 12,
		         st.kind == clsync::Status::Error || st.kind == clsync::Status::Full ? look.ink(0.85f) : look.ink(0.6f), TextAlign::Leading);
		fillRound(rt, line->button, 7, look.ink(0.07f));
		strokeRound(rt, line->button, 7, look.ink(0.10f));
		drawText(rt, "Sync Now", line->button, 12, look.ink(), TextAlign::Center);
	};
	p.onFooterClick = [line](Picker& picker, float x, float y) {
		if (inRect(line->button, x, y)) {
			syncapp::syncNow();
			return;
		}
		// The line itself: Settings, on its Sync page.
		setPreferencesPage(5);
		showPreferencesDialog(picker.hwnd);
		picker.redraw();
	};
	// Circuits that came or went, and how sync is doing, as it happens.
	const int listener = syncapp::listen([&](syncapp::Event e) {
		if (e == syncapp::Event::Library) {
			all = library::items();
			p.reload();
		} else {
			p.reload();   // (the problems under the names)
		}
	});
	p.run(from->window());
	syncapp::unlisten(listener);
	if (!windowAlive(from)) from = nullptr;
	if (!toOpen.empty()) openCircuit(toOpen, from);
	else if (import) chooseAndOpen(from);
}

void showVersionHistory(CircuitWindow* window) {
	if (window == nullptr) return;
	library::Item item;
	if (!library::itemFor(window->filePath(), item)) {
		showMessage(window->window(), Tone::Info, "No versions yet",
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
	p.doubleClickPresses = false;   // a double-click looks at a version; Restore restores it
	p.rows = [&](const std::string&) {
		std::vector<Row> out;
		for (size_t i = 0; i < versions.size(); i++) {
			const int n = library::gateCount(versions[i].path);
			// What sync kept it for ("From Levi's iPhone"), else how long ago.
			const std::string why = versions[i].note.substr(0, versions[i].note.find(" · "));
			out.push_back({ versions[i].path, library::friendlyTime(versions[i].time),
			                strf("%d gate%s · ", n, n == 1 ? "" : "s") + (why.empty() ? library::agoText(versions[i].time) : why),
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
	p.preview = [&](ID2D1RenderTarget* rt, const D2D1_RECT_F& box) {
		if (shownDoc == nullptr) {
			drawText(rt, shownPath.empty() ? "" : "No picture of this version.", box, 12, Look{ prefs().dark }.ink(0.55f), TextAlign::Center);
			return;
		}
		D2D1_MATRIX_3X2_F t;
		rt->GetTransform(&t);
		rt->SetTransform(D2D1::Matrix3x2F::Translation(box.left, box.top) * t);
		const float s = t._11;
		cl_document_draw_fitted(shownDoc, 0, rt, box.right - box.left, box.bottom - box.top, 16, s,
		                        prefs().dark ? CL_STYLE_DARK : CL_STYLE_LIGHT);
		rt->SetTransform(t);
	};
	bool restored = false;
	p.onButton = [&](Picker& picker, int b) -> bool {
		const int i = picker.selection;
		if (b == 100) return true;
		if (i < 0 || i >= (int)versions.size()) return false;
		if (b == 0) {
			const std::string file = chooseSaveFile(picker.hwnd, "Export a Copy", item.name + " (" + library::friendlyTime(versions[i].time) + ").cdl",
			                                        { { "CedarLogic circuits (*.cdl)", "*.cdl" } }, ".cdl");
			if (!file.empty() && !CopyFileW(W(versions[i].path).c_str(), W(file).c_str(), FALSE))
				showMessage(picker.hwnd, Tone::Warning, "Couldn't save a copy there", "Try another folder.");
			return false;
		}
		if (b == 101) {
			if (!library::restore(item, versions[i])) {
				showMessage(picker.hwnd, Tone::Warning, "Couldn't restore that version", "");
				return false;
			}
			restored = true;
			return true;
		}
		return false;
	};
	p.onKey = [&](Picker& picker, UINT vk, bool ctrl) -> bool {
		if (ctrl && vk == 'E') { picker.onButton(picker, 0); return true; }
		return false;
	};
	p.run(window->window());
	if (shownDoc) cl_document_close(shownDoc);
	if (restored && windowAlive(window)) window->reloadFromDisk("Restored that version. The one before it was kept too.");
}
