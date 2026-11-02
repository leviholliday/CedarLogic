// Find (Cmd-F): labels, named links (TO / FROM) and gate types on every page,
// best matches first -- a name that is exactly what was typed, then one that
// starts with it, then one that has it somewhere, then gates of a matching
// type ("and", "flip-flop", "LED").

#include "DocumentImpl.h"
#include "guiGate.h"
#include "GateLibrary.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <string>
#include <vector>

namespace {

std::deque<std::string> findText;   // what the last results point into

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

// 0 exact, 1 starts with, 2 has it, -1 no.
int matchScore(const std::string& text, const std::string& q) {
	if (text.empty()) return -1;
	const std::string t = lower(text);
	if (t == q) return 0;
	if (t.rfind(q, 0) == 0) return 1;
	return t.find(q) != std::string::npos ? 2 : -1;
}

}  // namespace

extern "C" {

int cl_find(CLDocument* doc, const char* query, CLFindResult* out, int max) {
	findText.clear();
	if (doc == nullptr || query == nullptr || out == nullptr || max <= 0) return 0;
	std::string q = lower(query);
	while (!q.empty() && q.front() == ' ') q.erase(q.begin());
	while (!q.empty() && q.back() == ' ') q.pop_back();
	if (q.empty()) return 0;

	struct Hit { int score; CLFindResult r; };
	std::vector<Hit> hits;
	for (int p = 0; p < cl_document_page_count(doc); p++) {
		GUICanvas* page = doc->page(p);
		if (page == nullptr) continue;
		for (auto& e : *page->getGateList()) {
			guiGate* g = e.second;
			if (g == nullptr) continue;
			float x, y;
			g->getGLcoords(x, y);
			const std::string type = g->getLibraryGateName();
			const bool link = type == "DE_TO" || type == "DA_FROM";
			std::string name, what;
			if (dynamic_cast<guiLabel*>(g)) { name = g->getGUIParam("LABEL_TEXT"); what = "Label"; }
			else if (link) { name = g->getLogicParam("JUNCTION_ID"); what = type == "DE_TO" ? "To link" : "From link"; }
			int score = matchScore(name, q);
			std::string caption;
			if (score < 0 && q.size() >= 2) {
				LibraryGate lg;
				if (gateLibrary().libParser.getGate(type, lg)) caption = lg.caption.empty() ? type : lg.caption;
				if (matchScore(caption, q) >= 0 || matchScore(type, q) >= 0) {
					score = 3;
					name = caption;
					what = "Part";
				}
			}
			if (score < 0) continue;
			findText.push_back(name);
			const char* text = findText.back().c_str();
			findText.push_back(what);
			hits.push_back({ score, CLFindResult{ p, (long)e.first, x, y, text, findText.back().c_str() } });
		}
	}
	std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
		if (a.score != b.score) return a.score < b.score;
		if (a.r.page != b.r.page) return a.r.page < b.r.page;
		if (a.r.y != b.r.y) return a.r.y > b.r.y;   // top to bottom
		return a.r.x < b.r.x;
	});
	const int n = std::min((int)hits.size(), max);
	for (int i = 0; i < n; i++) out[i] = hits[(size_t)i].r;
	return (int)hits.size();
}

bool cl_edit_select_gate(CLDocument* doc, int pageIndex, long gate) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return false;
	guiGate* g = doc->circuit.getGate((unsigned long)gate);
	if (g == nullptr) return false;
	page->unselectAllGates();
	page->unselectAllWires();
	g->select();
	return true;
}

const char* cl_gate_find_name(const CLDocument* doc, long gate) {
	findText.clear();
	if (doc == nullptr) return "";
	guiGate* g = const_cast<CLDocument*>(doc)->circuit.getGate((unsigned long)gate);
	if (g == nullptr) return "";
	if (dynamic_cast<guiLabel*>(g)) findText.push_back(g->getGUIParam("LABEL_TEXT"));
	else if (g->getLibraryGateName() == "DE_TO" || g->getLibraryGateName() == "DA_FROM") findText.push_back(g->getLogicParam("JUNCTION_ID"));
	else return "";
	return findText.back().c_str();
}

}  // extern "C"
