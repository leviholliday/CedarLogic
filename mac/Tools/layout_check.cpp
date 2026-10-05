// Regression check for Tidy Up's Full rearrange on saved circuits:
//   layout_check <cl_gatedefs.xml> <circuit.cdl>...
// For each file, every page with parts on it is rearranged and checked: it
// finishes in under 8 seconds (Tidy Up runs on the UI thread), no two parts overlap
// (unless they did before), named links drawn in one column keep their
// top-to-bottom order, parts drawn as separate groups (a clear gap between
// them) stay separate and in order, and running it again gives the same
// file. Exit code is the number of failed checks.
#include "DocumentImpl.h"
#include "guiGate.h"
#include "klsBBox.h"
#include "guiWire.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Part { unsigned long id; std::string type; float l, b, r, t; std::vector<unsigned long> wires; int comp = 0; };

std::vector<Part> parts(CLDocument* doc, int page) {
	std::vector<Part> out;
	for (auto& e : *doc->page(page)->getGateList()) {
		guiGate* g = e.second;
		if (!g) continue;
		klsBBox bb = g->getSelectionBBox();
		if (bb.empty()) bb = g->getBBox();
		Part p = { e.first, g->getLibraryGateName(), bb.getLeft(), bb.getBottom(), bb.getRight(), bb.getTop() };
		for (const auto& c : g->getConnections()) if (c.second) p.wires.push_back(c.second->getID());
		std::sort(p.wires.begin(), p.wires.end());
		p.wires.erase(std::unique(p.wires.begin(), p.wires.end()), p.wires.end());
		out.push_back(p);
	}
	std::sort(out.begin(), out.end(), [](const Part& a, const Part& b) { return a.id < b.id; });
	// Connected circuits: parts joined by wires.
	std::vector<int> parent(out.size());
	for (size_t i = 0; i < out.size(); i++) parent[i] = (int)i;
	std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
	std::map<unsigned long, int> firstOnWire;
	for (size_t i = 0; i < out.size(); i++)
		for (unsigned long w : out[i].wires) {
			auto it = firstOnWire.find(w);
			if (it == firstOnWire.end()) firstOnWire[w] = (int)i; else parent[find((int)i)] = find(it->second);
		}
	for (size_t i = 0; i < out.size(); i++) out[i].comp = find((int)i);
	return out;
}

// Wires with a part in both sets.
int crossing(const std::vector<Part>& P, const std::vector<size_t>& A, const std::vector<size_t>& B) {
	std::set<unsigned long> inA;
	for (size_t i : A) inA.insert(P[i].wires.begin(), P[i].wires.end());
	std::set<unsigned long> both;
	for (size_t i : B) for (unsigned long w : P[i].wires) if (inA.count(w)) both.insert(w);
	return (int)both.size();
}

// Wires joining two or more of these parts.
int inner(const std::vector<Part>& P, const std::vector<size_t>& S) {
	std::map<unsigned long, int> count;
	for (size_t i : S) for (unsigned long w : P[i].wires) count[w]++;
	int c = 0;
	for (const auto& kv : count) if (kv.second >= 2) c++;
	return c;
}

bool overlaps(const Part& a, const Part& b) {
	const float e = 0.01f;
	return a.l < b.r - e && b.l < a.r - e && a.b < b.t - e && b.b < a.t - e;
}

// The groups the layout keeps apart: within one circuit, parts separated by a
// gap of 3 or more along x that every part clears, at least two on each side,
// with few wires across it next to the wiring within each side.
std::vector<std::vector<size_t>> xGroups(const std::vector<Part>& P, int comp) {
	std::vector<size_t> order;
	for (size_t i = 0; i < P.size(); i++) if (P[i].comp == comp && !P[i].wires.empty()) order.push_back(i);
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return P[a].l < P[b].l; });
	std::vector<std::vector<size_t>> groups;
	std::vector<size_t> cur;
	float reach = -1e9f;
	for (size_t k = 0; k < order.size(); k++) {
		const size_t i = order[k];
		if (k > 0 && P[i].l >= reach + 3.0f) {
			std::vector<size_t> before(order.begin(), order.begin() + k), after(order.begin() + k, order.end());
			if (before.size() >= 2 && after.size() >= 2 &&
			    crossing(P, before, after) <= std::max(1, std::min(inner(P, before), inner(P, after)) / 4)) {
				groups.push_back(cur);
				cur.clear();
			}
		}
		cur.push_back(i);
		reach = std::max(reach, P[i].r);
	}
	if (!cur.empty()) groups.push_back(cur);
	return groups;
}

const double TIME_LIMIT = 8.0;   // seconds for one page

int check(const char* path, int page) {
	int fails = 0;
	auto report = [&](bool ok, const std::string& what) {
		printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
		if (!ok) fails++;
	};
	char err[512];
	CLDocument* doc = cl_document_open(path, err, sizeof err);
	if (!doc) { printf("  FAIL open: %s\n", err); return 1; }
	const std::vector<Part> before = parts(doc, page);
	const auto t0 = std::chrono::steady_clock::now();
	cl_edit_tidy_begin(doc, page, 1);
	cl_edit_tidy_end(doc, true);
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	char took[96];
	snprintf(took, sizeof took, "finishes in time (%.2f s, %zu parts; limit %.0f s)", secs, before.size(), TIME_LIMIT);
	report(secs <= TIME_LIMIT, took);
	const std::vector<Part> after = parts(doc, page);
	const std::string text = cl_document_save_text(doc);
	cl_document_close(doc);
	if (after.size() != before.size()) { printf("  FAIL part count changed\n"); return 1; }

	// No new overlaps.
	int overlapsNow = 0;
	for (size_t i = 0; i < after.size(); i++)
		for (size_t j = i + 1; j < after.size(); j++)
			if (overlaps(after[i], after[j]) && !overlaps(before[i], before[j])) {
				if (overlapsNow < 5) printf("       %s %lu overlaps %s %lu\n", after[i].type.c_str(), after[i].id, after[j].type.c_str(), after[j].id);
				overlapsNow++;
			}
	report(overlapsNow == 0, "no part overlaps another (" + std::to_string(overlapsNow) + " new overlaps)");

	// Named links drawn in one column keep their order.
	std::map<std::pair<int, int>, std::vector<size_t>> columns;   // (circuit, x) -> links
	for (size_t i = 0; i < before.size(); i++)
		if (before[i].type == "DE_TO" || before[i].type == "DA_FROM") columns[{ before[i].comp, (int)std::lround(before[i].l) }].push_back(i);
	int reordered = 0, columnsChecked = 0;
	for (auto& c : columns) {
		if (c.second.size() < 2) continue;
		columnsChecked++;
		std::vector<size_t> was = c.second, now = c.second;
		std::sort(was.begin(), was.end(), [&](size_t a, size_t b) { return before[a].t > before[b].t; });
		std::sort(now.begin(), now.end(), [&](size_t a, size_t b) { return after[a].t > after[b].t; });
		if (was == now) continue;
		reordered++;
		printf("       column at x %d:", c.first.second);
		for (size_t i : now) printf(" %lu(%s%.1f)", after[i].id, was[std::find(now.begin(), now.end(), i) - now.begin()] == i ? "" : "was ", after[i].t);
		printf("\n");
	}
	report(reordered == 0, "named links keep their drawn order (" + std::to_string(columnsChecked) + " columns)");

	// Groups drawn apart stay apart, in order.
	int merged = 0, big = 0;
	std::set<int> comps;
	for (const Part& p : before) comps.insert(p.comp);
	for (int comp : comps) {
		const std::vector<std::vector<size_t>> groups = xGroups(before, comp);
		if (groups.size() < 2) continue;
		big += (int)groups.size();
		float prevR = -1e9f;
		for (const auto& g : groups) {
			float l = 1e9f, r = -1e9f;
			for (size_t i : g) { l = std::min(l, after[i].l); r = std::max(r, after[i].r); }
			if (l < prevR) merged++;
			prevR = std::max(prevR, r);
		}
	}
	report(merged == 0, "groups drawn apart stay apart (" + std::to_string(big) + " groups)");

	// Deterministic.
	CLDocument* again = cl_document_open(path, err, sizeof err);
	cl_edit_tidy_begin(again, page, 1);
	cl_edit_tidy_end(again, true);
	report(text == cl_document_save_text(again), "same result twice");
	cl_document_close(again);
	return fails;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: layout_check lib.xml circuit.cdl...\n"); return 2; }
	if (!cl_library_load(argv[1])) { fprintf(stderr, "library failed\n"); return 1; }
	cl_set_settle_on_open(false);
	int fails = 0;
	for (int i = 2; i < argc; i++) {
		char err[512];
		CLDocument* doc = cl_document_open(argv[i], err, sizeof err);
		if (!doc) { printf("%s\n  FAIL open: %s\n", argv[i], err); fails++; continue; }
		const int pages = cl_document_page_count(doc);
		std::vector<int> withParts;
		for (int p = 0; p < pages; p++) if (!doc->page(p)->getGateList()->empty()) withParts.push_back(p);
		cl_document_close(doc);
		for (int p : withParts) {
			printf("%s page %d\n", argv[i], p + 1);
			fails += check(argv[i], p);
		}
	}
	printf("%d failed\n", fails);
	return fails;
}
