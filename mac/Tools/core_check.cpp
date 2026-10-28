// Regression checks for the engine's hardening (docs/CORE-HARDENING.md): each
// bug the fuzzers found gets a case here, small enough to read. Headless: it
// builds with the rest of mac/Tools/headless and runs in CI under the sanitizers.
//   core_check <cl_gatedefs.xml> [fixtures-dir]
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int fails = 0;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); fails++; } } while (0)

static std::string fixtures;

static long newest(CLDocument* doc, int page) {
	long best = -1;
	for (auto& g : *doc->page(page)->getGateList())
		if (g.second && (long)g.first > best) best = (long)g.first;
	return best;
}

static std::string setting(CLDocument* doc, long gate, const char* name) {
	for (int i = 0; i < cl_gate_setting_count(doc, gate); i++) {
		CLGateSetting s;
		if (cl_gate_setting(doc, gate, i, &s) && strcmp(s.name, name) == 0) return s.value;
	}
	return "(no such setting)";
}

static CLDocument* reopen(CLDocument* doc) {
	const std::string text = cl_document_save_text(doc);
	char err[256] = "";
	return cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
}

static std::string readFile(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

// ---- Text typed into a part comes back exactly ----------------------------------

static void textRoundTrip() {
	printf("text typed into labels, links and pages saves and reopens exactly\n");
	const char* const texts[] = {
		"back\\slash", "\\", "\\\\", "ends with \\", "\\\"", "\"quoted\"", "<version>", "a < b > c",
		"&amp; & &lt;", "two\nlines", "tab\there", "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80", "(paren)", ")(",
		"C:\\Users\\student\\lab.cdl", "trailing space ", " leading space", "",
	};
	std::string longText(20000, 'x');
	longText += "\\end";
	std::vector<std::string> all(std::begin(texts), std::end(texts));
	all.push_back(longText);
	int bad = 0;
	for (const std::string& t : all) {
		CLDocument* doc = cl_document_new();
		cl_edit_add_gate(doc, 0, "AA_LABEL", 0, 0);
		const long label = newest(doc, 0);
		cl_edit_add_gate(doc, 0, "DE_TO", 10, 0);
		const long to = newest(doc, 0);
		cl_gate_set_setting(doc, label, "LABEL_TEXT", t.c_str());
		cl_gate_set_setting(doc, to, "JUNCTION_ID", t.c_str());
		cl_document_rename_page(doc, 0, t.c_str());
		const std::string first = cl_document_save_text(doc);
		CLDocument* back = reopen(doc);
		bool ok = back != nullptr;
		if (ok) {
			ok = setting(back, label, "LABEL_TEXT") == t && setting(back, to, "JUNCTION_ID") == t &&
			     std::string(cl_document_save_text(back)) == first;
			// Copy and paste carries it too (the clipboard has a text format of its own).
			cl_edit_select_all(back, 0);
			const std::string copied = cl_edit_copy(back, 0);
			const char* why = nullptr;
			cl_edit_paste(back, 0, copied.c_str(), 30, 30, false, &why);
			const long pasted = newest(back, 0);
			ok = ok && pasted > to && (setting(back, pasted, "LABEL_TEXT") == t || setting(back, pasted, "JUNCTION_ID") == t);
			cl_document_close(back);
		}
		if (!ok) { printf("    lost: \"%.40s\"\n", t.c_str()); bad++; }
		cl_document_close(doc);
	}
	CHECK(bad == 0, "every text came back exactly, through save and through copy/paste");
}

int main(int argc, char** argv) {
	if (argc < 2 || !cl_library_load(argv[1])) { fprintf(stderr, "usage: core_check gatedefs [fixtures-dir]\n"); return 2; }
	if (argc > 2) fixtures = argv[2];
	cl_set_settle_on_open(true);

	textRoundTrip();

	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
