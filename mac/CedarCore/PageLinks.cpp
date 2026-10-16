// Page link groups (docs/PAGE-LINKS.md): which pages' TO/FROM links connect.
// Each page has a group number (GUICanvas::linkGroup); pages with the same
// number connect by name, others never do. Only the partition counts: the
// numbers themselves are renumbered when written (CircuitParse.cpp). The
// engine hears of a group through the name LogicHost hands it (linkKey).

#include "DocumentImpl.h"
#include "guiGate.h"

#include <algorithm>
#include <atomic>
#include <vector>

namespace {

std::atomic<bool> newPagesShare{ true };

// A number no page has now.
int freshGroup(const CLDocument* doc) {
	int g = 0;
	for (auto& p : doc->pages) g = std::max(g, p->linkGroup);
	return g + 1;
}

// One undoable change of every page's group. Pages are held by pointer: a
// page closed later is kept by its own command, which is undone first.
class LinkGroupsCommand : public klsCommand {
public:
	LinkGroupsCommand(CLDocument* doc, const char* name, std::vector<std::pair<GUICanvas*, int>> to)
		: klsCommand(true, name), doc(doc), after(std::move(to)) {
		for (auto& e : after) before.push_back({ e.first, e.first->linkGroup });
	}
	bool Do() override { apply(after); return true; }
	bool Undo() override { apply(before); return true; }

private:
	void apply(const std::vector<std::pair<GUICanvas*, int>>& groups) {
		for (auto& e : groups) e.first->linkGroup = e.second;
		doc->sim->relinkJunctions();
		doc->edited = true;
	}
	CLDocument* doc;
	std::vector<std::pair<GUICanvas*, int>> before, after;
};

bool submit(CLDocument* doc, const char* name, const std::vector<int>& groups) {
	bool changed = false;
	std::vector<std::pair<GUICanvas*, int>> to;
	for (size_t i = 0; i < doc->pages.size(); i++) {
		to.push_back({ doc->pages[i].get(), groups[i] });
		for (size_t j = 0; j < doc->pages.size(); j++)
			if ((doc->pages[i]->linkGroup == doc->pages[j]->linkGroup) != (groups[i] == groups[j])) changed = true;
	}
	if (!changed) return false;
	return doc->circuit.GetCommandProcessor()->Submit(new LinkGroupsCommand(doc, name, std::move(to)));
}

int groupSize(const CLDocument* doc, int group) {
	int n = 0;
	for (auto& p : doc->pages) n += p->linkGroup == group;
	return n;
}

}  // namespace

int clLinkGroupOfGate(const CLDocument* doc, unsigned long gateId) {
	for (auto& p : doc->pages)
		if (p->getGateList()->count(gateId)) return p->linkGroup;
	return 0;
}

// The group a page added now joins (cl_set_new_pages_share_links).
int clNewPageLinkGroup(const CLDocument* doc) {
	if (doc->pages.empty()) return 0;
	if (!newPagesShare) return freshGroup(doc);
	// Sharing: the group most pages are in (the first page's on a tie). When
	// no page shares with another, there is no shared group to join.
	int best = doc->pages[0]->linkGroup, bestN = 0;
	for (auto& p : doc->pages) {
		const int n = groupSize(doc, p->linkGroup);
		if (n > bestN) { best = p->linkGroup; bestN = n; }
	}
	if (bestN < 2 && doc->pages.size() >= 2) return freshGroup(doc);
	return best;
}

extern "C" {

void cl_set_new_pages_share_links(bool share) { newPagesShare = share; }
bool cl_new_pages_share_links(void) { return newPagesShare; }

bool cl_pages_linked(const CLDocument* doc, int a, int b) {
	GUICanvas* pa = doc ? doc->page(a) : nullptr;
	GUICanvas* pb = doc ? doc->page(b) : nullptr;
	return pa && pb && pa->linkGroup == pb->linkGroup;
}

int cl_page_link_mark(const CLDocument* doc, int page) {
	GUICanvas* p = doc ? doc->page(page) : nullptr;
	if (p == nullptr) return -1;
	const int n = groupSize(doc, p->linkGroup);
	if (n < 2 || n == (int)doc->pages.size()) return -1;   // alone, or everything connects
	// Multi-page groups numbered in the order their first page comes.
	std::vector<int> seen;
	for (auto& q : doc->pages) {
		if (groupSize(doc, q->linkGroup) < 2) continue;
		if (std::find(seen.begin(), seen.end(), q->linkGroup) == seen.end()) seen.push_back(q->linkGroup);
	}
	return (int)(std::find(seen.begin(), seen.end(), p->linkGroup) - seen.begin());
}

bool cl_pages_all_linked(const CLDocument* doc) {
	return doc && !doc->pages.empty() && groupSize(doc, doc->pages[0]->linkGroup) == (int)doc->pages.size();
}

bool cl_pages_none_linked(const CLDocument* doc) {
	if (doc == nullptr || doc->pages.size() < 2) return false;
	for (auto& p : doc->pages)
		if (groupSize(doc, p->linkGroup) > 1) return false;
	return true;
}

bool cl_pages_connect_all(CLDocument* doc) {
	if (doc == nullptr) return false;
	return submit(doc, "Connect All Pages", std::vector<int>(doc->pages.size(), 0));
}

bool cl_pages_disconnect_all(CLDocument* doc) {
	if (doc == nullptr) return false;
	std::vector<int> g;
	for (size_t i = 0; i < doc->pages.size(); i++) g.push_back((int)i);
	return submit(doc, "Disconnect All Pages", g);
}

bool cl_page_connect_to(CLDocument* doc, int page, const bool* with, int count) {
	if (doc == nullptr || doc->page(page) == nullptr || with == nullptr || count != (int)doc->pages.size()) return false;
	const int fresh = freshGroup(doc);
	std::vector<int> g;
	for (size_t i = 0; i < doc->pages.size(); i++) g.push_back(doc->pages[i]->linkGroup);
	for (int i = 0; i < count; i++)
		if (i == page || with[i]) g[i] = fresh;
	return submit(doc, "Connect Pages", g);
}

unsigned long long cl_pages_link_signature(const CLDocument* doc) {
	if (doc == nullptr) return 0;
	// The partition, as each page's first partner: changes when any link does.
	unsigned long long h = 1469598103934665603ull;
	for (size_t i = 0; i < doc->pages.size(); i++) {
		size_t first = i;
		for (size_t j = 0; j < i; j++)
			if (doc->pages[j]->linkGroup == doc->pages[i]->linkGroup) { first = j; break; }
		h = (h ^ (first + 1)) * 1099511628211ull;
	}
	return h;
}

}  // extern "C"
