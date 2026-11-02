// Page link groups (docs/PAGE-LINKS.md) in the engine: TO/FROM signals cross
// only between pages in one group; groups save, reopen and sync; every change
// undoes; a circuit whose pages all connect saves exactly as before.
//   pagelinks_check <cl_gatedefs.xml> format/tests/fixtures/pagelinks [more .cdl to save unchanged...]
#include "DocumentImpl.h"
#include "SyncInternal.h"
#include "guiWire.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int fails = 0;
static void expect(bool ok, const std::string& what) {
	printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) fails++;
}

static std::string readAll(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static CLDocument* open(const std::string& text) {
	char err[256] = "";
	CLDocument* d = cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	if (!d) printf("  open failed: %s\n", err);
	return d;
}

// A wire's first state after the circuit settles: '1', '0' or '?' (anything else).
static char wire(CLDocument* d, unsigned long id) {
	d->sim->settle();
	guiWire* w = d->circuit.getWire(id);
	if (!w || w->getState().empty()) return '?';
	return w->getState()[0] == ONE ? '1' : w->getState()[0] == ZERO ? '0' : '?';
}

static std::string lights(CLDocument* d) { return std::string(1, wire(d, 11)) + wire(d, 12); }

static int count(const std::string& s, const std::string& what) {
	int n = 0;
	for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1)) n++;
	return n;
}

static std::string table(CLDocument* d, int page) {
	char err[256] = "";
	CLTruthTable* tt = cl_truth_table(d, page, err, sizeof err);
	if (!tt) return std::string("error: ") + err;
	std::string s;
	for (int r = 0; r < cl_tt_rows(tt); r++) {
		for (int c = 0; c < cl_tt_columns(tt); c++) s += cl_tt_cell(tt, r, c);
		s += ' ';
	}
	cl_tt_free(tt);
	return s;
}

int main(int argc, char** argv) {
	if (argc < 3 || !cl_library_load(argv[1])) { printf("usage: pagelinks_check <gatedefs> <fixture dir> [file.cdl...]\n"); return 2; }
	const std::string dir = argv[2];
	const std::string base = readAll(dir + "/three-pages.cdl");
	const std::string split = readAll(dir + "/three-pages-split.cdl");
	const std::string apart = readAll(dir + "/three-pages-apart.cdl");

	// 1. No groups: every page connects, as always.
	CLDocument* a = open(base);
	if (!a) return 1;
	expect(lights(a) == "11", "no link groups: both other pages hear page 1's TO (" + lights(a) + ")");
	expect(cl_pages_all_linked(a) && cl_page_link_mark(a, 0) == -1, "no link groups: all linked, no tab dots");
	const std::string saved = cl_document_save_text(a);
	expect(saved.find("linkgroup") == std::string::npos, "no link groups: the save writes none");

	// 2. Page 3 on its own: it hears nothing; page 2 still does.
	CLDocument* b = open(split);
	expect(lights(b) == "1?", "page 3 in its own group: only page 2 lights (" + lights(b) + ")");
	expect(cl_pages_linked(b, 0, 1) && !cl_pages_linked(b, 0, 2), "page 3 in its own group: linked as the file says");
	expect(cl_page_link_mark(b, 0) == 0 && cl_page_link_mark(b, 1) == 0 && cl_page_link_mark(b, 2) == -1,
	       "page 3 in its own group: pages 1 and 2 share a dot, page 3 has none");

	// 3. Round trip: saved, reopened and saved again, the same.
	const std::string s1 = cl_document_save_text(b);
	expect(count(s1, "(linkgroup 1)") == 1 && count(s1, "linkgroup") == 1, "round trip: one (linkgroup 1) written");
	CLDocument* b2 = open(s1);
	const std::string s2 = cl_document_save_text(b2);
	expect(s1 == s2 && lights(b2) == "1?", "round trip: reopened it behaves and saves the same");
	// The copy Check My Circuit makes (clSaveText -> clOpenText) keeps the groups.
	CLDocument* copy = clOpenText(s1.data(), (long)s1.size(), nullptr, 0, false);
	expect(copy && lights(copy) == "1?", "Check My Circuit's copy keeps the groups");
	cl_document_close(copy);

	// 4. Undo and redo.
	CLDocument* c = open(base);
	const int steps = cl_edit_undo_count(c);
	expect(cl_pages_disconnect_all(c) && cl_edit_undo_count(c) == steps + 1, "disconnect all: one undo step");
	expect(lights(c) == "??", "disconnect all: no page hears another (" + lights(c) + ")");
	expect(!cl_pages_disconnect_all(c), "disconnect all again: nothing to do, no step");
	const std::string sApart = cl_document_save_text(c);
	expect(count(sApart, "(linkgroup 1)") == 1 && count(sApart, "(linkgroup 2)") == 1, "disconnect all: saved as groups 0, 1, 2");
	expect(cl_edit_undo(c) && lights(c) == "11", "undo: every page connects again");
	expect(std::string(cl_document_save_text(c)) == saved, "undo: saves byte for byte as before");
	expect(cl_edit_redo(c) && lights(c) == "??", "redo: apart again");
	{
		bool with[3] = { true, false, false };   // page 3 joins page 1
		expect(cl_page_connect_to(c, 2, with, 3) && lights(c) == "?1", "connect page 3 to page 1: only page 3 lights");
		expect(cl_page_link_mark(c, 0) == 0 && cl_page_link_mark(c, 2) == 0 && cl_page_link_mark(c, 1) == -1, "connect to: the pair shares a dot");
		expect(cl_edit_undo(c) && lights(c) == "??", "undo connect to: apart again");
	}
	expect(cl_pages_connect_all(c) && lights(c) == "11", "connect all: everything lights");
	expect(std::string(cl_document_save_text(c)) == saved, "connect all: saves as the file with no groups");

	// 5. A page closed and reopened keeps its group; moving pages keeps who connects.
	CLDocument* d = open(split);
	expect(cl_document_close_page(d, 2) && cl_edit_undo(d) && lights(d) == "1?", "close and reopen page 3: still on its own");
	cl_document_move_page(d, 2, 0);   // page 3 first
	const std::string moved = cl_document_save_text(d);
	expect(count(moved, "(linkgroup 1)") == 2 && lights(d) == "1?", "page 3 moved first: it is group 0, the others group 1");

	// 6. New pages: share, or start on their own.
	CLDocument* e = open(split);
	cl_set_new_pages_share_links(true);
	int p = cl_document_add_page(e);
	expect(cl_pages_linked(e, 0, p) && !cl_pages_linked(e, 2, p), "new page (sharing): joins the biggest group");
	cl_set_new_pages_share_links(false);
	p = cl_document_add_page(e);
	bool alone = true;
	for (int i = 0; i < p; i++) alone = alone && !cl_pages_linked(e, i, p);
	expect(alone, "new page (on its own): connects to nothing");
	cl_set_new_pages_share_links(true);
	CLDocument* f = open(apart);
	p = cl_document_add_page(f);
	expect(!cl_pages_linked(f, 0, p) && !cl_pages_linked(f, 1, p), "new page (sharing) when no page shares: on its own");

	// 7. Truth tables see only connected pages.
	CLDocument* t1 = open(readAll(dir + "/truth-table-pages.cdl"));
	CLDocument* t2 = open(readAll(dir + "/truth-table-pages-apart.cdl"));
	expect(table(t1, 0) == "00 11 ", "truth table through a connected page: Y = A (" + table(t1, 0) + ")");
	expect(table(t2, 0) == "0Z 1Z ", "truth table, the page apart: Y floats (" + table(t2, 0) + ")");

	// 8. Sync's structure text: nothing new without groups, a line a page with them.
	auto noDefaults = [](const std::string&, bool, const std::string&) { return std::string("\x01"); };
	const std::string stBase = clsync::structureText(base, noDefaults);
	const std::string stSplit = clsync::structureText(split, noDefaults);
	expect(stBase.find("\nL ") == std::string::npos, "structure text: no L lines without groups");
	expect(stSplit.find("\nL 0 0\nL 1 0\nL 2 1\n") != std::string::npos, "structure text: L 0 0, L 1 0, L 2 1");
	expect(clsync::structureText(s1, noDefaults) == stSplit, "structure text: same after a save");

	// 9. Other circuits save as they did (no linkgroup, and save_check's
	//    second-save equality).
	for (int i = 3; i < argc; i++) {
		const std::string text = readAll(argv[i]);
		if (text.find("linkgroup") != std::string::npos) continue;   // the samples above
		CLDocument* g = open(text);
		if (!g) { printf("SKIP %s\n", argv[i]); continue; }
		const std::string t = cl_document_save_text(g);
		expect(t.find("linkgroup") == std::string::npos, std::string("unchanged: ") + argv[i]);
		cl_document_close(g);
	}

	for (CLDocument* x : { a, b, b2, c, d, e, f, t1, t2 }) cl_document_close(x);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
