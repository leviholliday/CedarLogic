// Straighten lab: one stress case for Straighten (and Tidy Up keep-shape),
// measured before and after. Driven overnight by mac/Tools/straighten-lab.sh.
//
//   straighten_lab gen    <lib.xml> <seed> <parts> <outprefix>
//       Build a random but plausible circuit (switches left, layered columns of
//       gates / flip-flops / muxes / registers, lights right), mess its wires up
//       by moving parts after routing, then Straighten every wire.
//   straighten_lab file   <lib.xml> <in.cdl> <page> <messSeed|-1> <outprefix>
//       The same on a saved circuit's page (messSeed >= 0 moves a few parts first).
//   straighten_lab render <lib.xml> <in.cdl> <page> <out.png> [w h]
//   straighten_lab pages  <lib.xml> <in.cdl>      pages with wires, one per line
//
// gen/file write <outprefix>.before.cdl (before anything runs, so a crash keeps
// it), <outprefix>.after.cdl and <outprefix>.tidy.cdl, and print one JSON line of
// metrics. Hard failures (connectivity changed, wire off its pin,
// too slow) are listed in "fail".
#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"
#include "klsBBox.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <CoreServices/CoreServices.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

struct LibGate { std::vector<std::string> ins, outs; float top = 1, bottom = -1; };
std::map<std::string, LibGate> lib;

// Pins of every gate in the library file (commented-out lines skipped).
void parseLibrary(const char* path) {
	std::ifstream f(path);
	std::string line, text;
	while (std::getline(f, line)) if (line.empty() || line[0] != '#') text += line + "\n";
	auto tag = [&](size_t from, size_t to, const std::string& t) -> std::string {
		size_t a = text.find("<" + t + ">", from);
		if (a == std::string::npos || a > to) return "";
		a += t.size() + 2;
		size_t b = text.find("</" + t + ">", a);
		std::string v = text.substr(a, b - a);
		v.erase(0, v.find_first_not_of(" \t\n"));
		v.erase(v.find_last_not_of(" \t\n") + 1);
		return v;
	};
	size_t at = 0;
	while ((at = text.find("<gate>", at)) != std::string::npos) {
		size_t end = text.find("</gate>", at);
		if (end == std::string::npos) break;
		std::string name = tag(at, end, "name");
		LibGate g;
		bool any = false;
		for (const char* kind : { "input", "output" }) {
			size_t p = at;
			std::string open = std::string("<") + kind + ">", close = std::string("</") + kind + ">";
			while ((p = text.find(open, p)) != std::string::npos && p < end) {
				size_t q = text.find(close, p);
				std::string pin = tag(p, q, "name"), pt = tag(p, q, "point");
				float x = 0, y = 0;
				sscanf(pt.c_str(), "%f,%f", &x, &y);
				if (!any) { g.top = g.bottom = y; any = true; }
				g.top = std::max(g.top, y); g.bottom = std::min(g.bottom, y);
				(kind[0] == 'i' ? g.ins : g.outs).push_back(pin);
				p = q;
			}
		}
		if (!name.empty()) lib[name] = g;
		at = end;
	}
}

// ---------- geometry ----------

struct Seg { unsigned long wire; bool vert; float c, a, b; float x1, y1, x2, y2; };   // c: fixed coord, a<b span

std::vector<Seg> segments(GUICanvas* page) {
	std::vector<Seg> out;
	for (auto& w : *page->getWireList()) {
		if (!w.second) continue;
		for (auto& s : w.second->getSegmentMap()) {
			const wireSegment& ws = s.second;
			Seg g;
			g.wire = w.first;
			g.x1 = ws.begin.x; g.y1 = ws.begin.y; g.x2 = ws.end.x; g.y2 = ws.end.y;
			g.vert = std::fabs(g.x1 - g.x2) < 1e-3f && std::fabs(g.y1 - g.y2) >= 1e-3f;
			if (g.vert) { g.c = g.x1; g.a = std::min(g.y1, g.y2); g.b = std::max(g.y1, g.y2); }
			else { g.c = g.y1; g.a = std::min(g.x1, g.x2); g.b = std::max(g.x1, g.x2); }
			out.push_back(g);
		}
	}
	return out;
}

bool onSeg(const Seg& s, float x, float y, float e = 0.02f) {
	if (s.vert) return std::fabs(x - s.c) < e && y > s.a - e && y < s.b + e;
	return std::fabs(y - s.c) < e && x > s.a - e && x < s.b + e;
}

struct Metrics {
	int parts = 0, wires = 0, segs = 0, crossings = 0, corners = 0, overlaps = 0, through = 0, wrongPin = 0,
	    detached = 0, split = 0, diagonal = 0;
	double overlapLen = 0, length = 0, hpwl = 0;
};

Metrics measure(GUICanvas* page) {
	Metrics m;
	std::vector<Seg> S = segments(page);
	m.segs = (int)S.size();
	std::map<unsigned long, std::vector<size_t>> byWire;
	for (size_t i = 0; i < S.size(); i++) {
		byWire[S[i].wire].push_back(i);
		m.length += S[i].b - S[i].a;
		if (std::fabs(S[i].x1 - S[i].x2) > 1e-3f && std::fabs(S[i].y1 - S[i].y2) > 1e-3f) m.diagonal++;
	}
	for (auto& g : *page->getGateList()) if (g.second) m.parts++;
	for (auto& w : *page->getWireList()) if (w.second) m.wires++;
	const float e = 0.02f;
	// Crossings: a horizontal of one wire through the inside of a vertical of another.
	std::vector<size_t> H, V;
	for (size_t i = 0; i < S.size(); i++) (S[i].vert ? V : H).push_back(i);
	std::sort(V.begin(), V.end(), [&](size_t a, size_t b) { return S[a].c < S[b].c; });
	for (size_t hi : H) {
		const Seg& h = S[hi];
		auto lo = std::lower_bound(V.begin(), V.end(), h.a + e, [&](size_t v, float x) { return S[v].c < x; });
		for (auto it = lo; it != V.end() && S[*it].c < h.b - e; ++it) {
			const Seg& v = S[*it];
			if (v.wire != h.wire && h.c > v.a + e && h.c < v.b - e) m.crossings++;
		}
	}
	// Overlaps: collinear runs shared by two different wires.
	std::map<std::pair<int, long>, std::vector<size_t>> lines;
	for (size_t i = 0; i < S.size(); i++) lines[{ S[i].vert, std::lround(S[i].c * 100) }].push_back(i);
	for (auto& l : lines) {
		auto& v = l.second;
		std::sort(v.begin(), v.end(), [&](size_t a, size_t b) { return S[a].a < S[b].a; });
		for (size_t i = 0; i < v.size(); i++)
			for (size_t j = i + 1; j < v.size() && S[v[j]].a < S[v[i]].b - e; j++) {
				if (S[v[i]].wire == S[v[j]].wire) continue;
				m.overlaps++;
				m.overlapLen += std::min(S[v[i]].b, S[v[j]].b) - S[v[j]].a;
			}
	}
	// Corners, and whether each wire is one connected piece.
	auto same = [](float ax, float ay, float bx, float by) { return std::fabs(ax - bx) < 0.02f && std::fabs(ay - by) < 0.02f; };
	for (auto& bw : byWire) {
		const auto& idx = bw.second;
		std::vector<int> parent(idx.size());
		for (size_t i = 0; i < idx.size(); i++) parent[i] = (int)i;
		std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
		for (size_t i = 0; i < idx.size(); i++)
			for (size_t j = i + 1; j < idx.size(); j++) {
				const Seg &p = S[idx[i]], &q = S[idx[j]];
				if (onSeg(p, q.x1, q.y1) || onSeg(p, q.x2, q.y2) || onSeg(q, p.x1, p.y1) || onSeg(q, p.x2, p.y2))
					parent[find((int)i)] = find((int)j);
				if (p.vert != q.vert &&
				    (same(p.x1, p.y1, q.x1, q.y1) || same(p.x1, p.y1, q.x2, q.y2) || same(p.x2, p.y2, q.x1, q.y1) || same(p.x2, p.y2, q.x2, q.y2)))
					m.corners++;
			}
		std::set<int> roots;
		for (size_t i = 0; i < idx.size(); i++) roots.insert(find((int)i));
		if (roots.size() > 1) {
			m.split++;
			if (getenv("SL_DEBUG")) {
				fprintf(stderr, "split wire %lu:", bw.first);
				for (size_t i : idx) fprintf(stderr, " (%g,%g)-(%g,%g)", S[i].x1, S[i].y1, S[i].x2, S[i].y2);
				fprintf(stderr, "\n");
			}
		}
	}
	// Pins: a wire reaches each pin it's on, and runs over no pin it isn't on.
	struct Pin { std::string name; float x, y; unsigned long wire; };
	std::vector<Pin> pins;
	for (auto& g : *page->getGateList()) {
		guiGate* gate = g.second;
		if (!gate) continue;
		auto it = lib.find(gate->getLibraryGateName());
		std::set<std::string> names;
		if (it != lib.end()) { names.insert(it->second.ins.begin(), it->second.ins.end()); names.insert(it->second.outs.begin(), it->second.outs.end()); }
		for (const auto& c : gate->getConnections()) names.insert(c.first);
		for (const std::string& n : names) {
			if (!gate->getHotspot(n)) continue;
			Pin p{ n, 0, 0, 0 };
			gate->getHotspotCoords(n, p.x, p.y);
			if (guiWire* w = gate->getConnection(n)) p.wire = w->getID();
			pins.push_back(p);
		}
	}
	std::map<std::pair<long, long>, std::vector<size_t>> cells;   // 4x4 buckets of segments
	auto cell = [](float v) { return (long)std::floor(v / 4.0f); };
	for (size_t i = 0; i < S.size(); i++) {
		float x1 = std::min(S[i].x1, S[i].x2), x2 = std::max(S[i].x1, S[i].x2), y1 = std::min(S[i].y1, S[i].y2), y2 = std::max(S[i].y1, S[i].y2);
		for (long cx = cell(x1 - 0.1f); cx <= cell(x2 + 0.1f); cx++)
			for (long cy = cell(y1 - 0.1f); cy <= cell(y2 + 0.1f); cy++) cells[{ cx, cy }].push_back(i);
	}
	for (const Pin& p : pins) {
		bool own = false;
		std::set<unsigned long> others;
		auto c = cells.find({ cell(p.x), cell(p.y) });
		if (c != cells.end())
			for (size_t i : c->second) {
				if (!onSeg(S[i], p.x, p.y)) continue;
				if (S[i].wire == p.wire) own = true; else others.insert(S[i].wire);
			}
		if (p.wire && !own && byWire.count(p.wire)) m.detached++;
		m.wrongPin += (int)others.size();
	}
	// Wires through part bodies.
	for (auto& g : *page->getGateList()) {
		guiGate* gate = g.second;
		if (!gate) continue;
		const std::string n = gate->getLibraryGateName();
		if (n == "AA_LABEL" || n.rfind("HA_JUNC", 0) == 0 || n.rfind("HE_JUNC", 0) == 0) continue;
		klsBBox b = gate->getSelectionBBox();
		if (b.empty()) continue;
		const float l = b.getLeft() + 0.1f, r = b.getRight() - 0.1f, bo = b.getBottom() + 0.1f, t = b.getTop() - 0.1f;
		if (l >= r || bo >= t) continue;
		std::set<unsigned long> hit;
		for (long cx = cell(l); cx <= cell(r); cx++)
			for (long cy = cell(bo); cy <= cell(t); cy++) {
				auto c = cells.find({ cx, cy });
				if (c == cells.end()) continue;
				for (size_t i : c->second) {
					const Seg& s = S[i];
					if (s.vert ? (s.c > l && s.c < r && s.b > bo && s.a < t) : (s.c > bo && s.c < t && s.b > l && s.a < r)) hit.insert(s.wire);
				}
			}
		m.through += (int)hit.size();
	}
	// Half-perimeter of each wire's pins: a lower bound on its length.
	std::map<unsigned long, std::vector<std::pair<float, float>>> ends;
	for (const Pin& p : pins) if (p.wire) ends[p.wire].push_back({ p.x, p.y });
	for (auto& e2 : ends) {
		float l = 1e9f, r = -1e9f, b = 1e9f, t = -1e9f;
		for (auto& p : e2.second) { l = std::min(l, p.first); r = std::max(r, p.first); b = std::min(b, p.second); t = std::max(t, p.second); }
		m.hpwl += (r - l) + (t - b);
	}
	return m;
}

// What's connected to what: each net as its sorted pins (part ids survive edits).
std::set<std::vector<std::string>> nets(GUICanvas* page) {
	std::set<std::vector<std::string>> out;
	for (auto& w : *page->getWireList()) {
		if (!w.second) continue;
		std::vector<std::string> pins;
		for (const wireConnection& c : w.second->getConnections()) pins.push_back(std::to_string(c.gid) + ":" + c.connection);
		std::sort(pins.begin(), pins.end());
		out.insert(pins);
	}
	return out;
}
std::set<std::string> gatePins(GUICanvas* page) {
	std::set<std::string> out;
	for (auto& g : *page->getGateList())
		if (g.second) for (const auto& c : g.second->getConnections()) if (c.second) out.insert(std::to_string(g.first) + ":" + c.first);
	return out;
}

void writeFile(const std::string& path, const char* text) {
	FILE* f = fopen(path.c_str(), "w");
	if (f) { fputs(text, f); fclose(f); }
}

std::string jsonMetrics(const char* p, const Metrics& m) {
	char b[1200];
	snprintf(b, sizeof b,
	         "\"%sparts\":%d,\"%swires\":%d,\"%ssegs\":%d,\"%scrossings\":%d,\"%scorners\":%d,\"%soverlaps\":%d,\"%soverlap_len\":%.2f,"
	         "\"%sthrough\":%d,\"%swrong_pin\":%d,\"%sdetached\":%d,\"%ssplit\":%d,\"%sdiagonal\":%d,\"%slength\":%.1f,\"%shpwl\":%.1f",
	         p, m.parts, p, m.wires, p, m.segs, p, m.crossings, p, m.corners, p, m.overlaps, p, m.overlapLen,
	         p, m.through, p, m.wrongPin, p, m.detached, p, m.split, p, m.diagonal, p, m.length, p, m.hpwl);
	return b;
}

// ---------- the random circuit ----------

struct Placed { std::string gate; double x, y; };
struct Link { int from; std::string fromPin; int to; std::string toPin; };

void generate(unsigned seed, int target, std::vector<Placed>& parts, std::vector<Link>& links) {
	std::mt19937 rng(seed);
	auto pick = [&](int n) { return n <= 0 ? 0 : (int)(rng() % (unsigned)n); };
	auto chance = [&](double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; };
	const std::vector<std::pair<const char*, int>> menu = {
		{ "AA_AND2", 10 }, { "AA_AND3", 4 }, { "AE_OR2", 9 }, { "AE_OR3", 3 }, { "AI_XOR2", 6 }, { "BA_NAND2", 6 },
		{ "BE_NOR2", 5 }, { "AA_INVERTER", 8 }, { "AE_DFF_LOW", 5 }, { "BE_JKFF_LOW", 3 }, { "AA_MUX_2x1", 4 },
		{ "AE_MUX_4x1", 2 }, { "AA_REGISTER4", 2 }, { "BA_DECODER_2x4", 2 }, { "AA_FULLADDER_1BIT", 3 }, { "AA_AND4", 1 },
	};
	int total = 0;
	for (auto& m : menu) if (lib.count(m.first)) total += m.second;
	auto randomGate = [&]() -> std::string {
		int r = pick(total);
		for (auto& m : menu) { if (!lib.count(m.first)) continue; if ((r -= m.second) < 0) return m.first; }
		return "AA_AND2";
	};
	const int inner = std::max(1, target * 6 / 10);
	const int cols = std::max(1, std::min(14, (int)std::lround(std::sqrt(inner / 2.5)) + pick(2)));
	const int switches = std::max(1, std::min(32, target / 5 + 1));
	std::vector<std::vector<int>> col(cols + 1);
	const double colGap = 18 + pick(8);
	double y = 0;
	for (int i = 0; i < switches; i++) {
		parts.push_back({ i == 0 && chance(0.15) ? "BB_CLOCK" : "AA_TOGGLE", -colGap, y });
		col[0].push_back((int)parts.size() - 1);
		y -= 3 + pick(3);
	}
	for (int c = 1; c <= cols; c++) {
		int n = inner / cols + (c <= inner % cols ? 1 : 0);
		double yy = -(double)pick(6);
		for (int i = 0; i < n; i++) {
			std::string g = randomGate();
			const LibGate& L = lib[g];
			yy -= std::max(0.f, L.top) + 1;
			parts.push_back({ g, (c - 1) * colGap + (pick(3) - 1) * 2, std::round(yy) });
			col[c].push_back((int)parts.size() - 1);
			yy -= std::max(0.f, -L.bottom) + 2 + pick(4);
		}
	}
	std::vector<int> fanout(parts.size(), 0);
	for (int c = 1; c <= cols; c++)
		for (int gi : col[c]) {
			for (const std::string& in : lib[parts[gi].gate].ins) {
				if (chance(0.04)) continue;   // left open
				int srcCol = std::max(0, c - 1 - (chance(0.3) ? pick(c) : 0));
				if (c > 1 && chance(0.05)) srcCol = std::min(cols, c + pick(cols - c + 1));   // feedback
				if (col[srcCol].empty() || srcCol == c) srcCol = 0;
				int src = col[srcCol][pick((int)col[srcCol].size())];
				for (int t = 0; t < 2 && fanout[src] > 0; t++) src = col[srcCol][pick((int)col[srcCol].size())];
				const auto& outs = lib[parts[src].gate].outs;
				if (outs.empty() || src == gi) continue;
				links.push_back({ src, outs[pick((int)outs.size())], gi, in });
				fanout[src]++;
			}
		}
	std::vector<std::pair<int, std::string>> loose;
	std::set<std::pair<int, std::string>> used;
	for (auto& l : links) used.insert({ l.from, l.fromPin });
	for (int c = 1; c <= cols; c++)
		for (int gi : col[c])
			for (auto& o : lib[parts[gi].gate].outs)
				if (!used.count({ gi, o }) && (c >= cols - 1 || chance(0.3))) loose.push_back({ gi, o });
	const double lightX = cols * colGap + 4;
	double ly = 0;
	for (auto& lo : loose) {
		if ((int)parts.size() >= target + 40) break;
		parts.push_back({ "GA_LED", lightX, ly });
		links.push_back({ lo.first, lo.second, (int)parts.size() - 1, "N_in0" });
		ly -= 3 + pick(2);
	}
}

// Move some parts by hand, as a user would, so the wires bend to follow.
void mess(CLDocument* doc, int page, unsigned seed, int moves) {
	std::mt19937 rng(seed * 7919u + 13);
	std::vector<unsigned long> ids;
	for (auto& g : *doc->page(page)->getGateList()) if (g.second && !g.second->isLocked()) ids.push_back(g.first);
	std::sort(ids.begin(), ids.end());
	if (ids.empty()) return;
	for (int i = 0; i < moves; i++) {
		unsigned long id = ids[rng() % ids.size()];
		if (!cl_edit_select_gate(doc, page, (long)id)) continue;
		cl_edit_nudge(doc, page, (int)(rng() % 13) - 6, (int)(rng() % 13) - 6);
	}
	cl_edit_select_none(doc, page);
}

int runCase(CLDocument* doc, int page, const std::string& out, const std::string& head) {
	GUICanvas* pg = doc->page(page);
	const std::string beforeText = cl_document_save_text(doc);
	writeFile(out + ".before.cdl", beforeText.c_str());
	const auto netsBefore = nets(pg);
	const auto pinsBefore = gatePins(pg);
	const Metrics mb = measure(pg);

	cl_edit_select_none(doc, page);
	cl_edit_select_all(doc, page);
	const auto t0 = std::chrono::steady_clock::now();
	cl_edit_straighten(doc, page);
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	cl_edit_select_none(doc, page);
	const Metrics ma = measure(pg);
	std::vector<std::string> fails;
	if (nets(pg) != netsBefore) fails.push_back("nets_changed");
	if (gatePins(pg) != pinsBefore) fails.push_back("pins_changed");
	const std::string afterText = cl_document_save_text(doc);
	writeFile(out + ".after.cdl", afterText.c_str());
	char err[512];
	if (CLDocument* re = cl_document_open_text(afterText.c_str(), (long)afterText.size(), err, sizeof err)) {
		if (page >= cl_document_page_count(re) || nets(re->page(page)) != netsBefore) fails.push_back("nets_changed_after_reload");
		cl_document_close(re);
	} else fails.push_back("reload_failed");
	if (ma.detached > mb.detached) fails.push_back("wire_off_pin");
	if (ma.diagonal > mb.diagonal) fails.push_back("diagonal_segment");
	if (secs > 20) fails.push_back("timeout");

	// Tidy Up keep-shape from the same starting point, when it's cheap.
	std::string tidy = "\"tidy_ran\":false";
	if (mb.parts <= 220) {
		if (CLDocument* t = cl_document_open_text(beforeText.c_str(), (long)beforeText.size(), err, sizeof err)) {
			const auto t1 = std::chrono::steady_clock::now();
			cl_edit_tidy_begin(t, page, 0);
			cl_edit_tidy_end(t, true);
			const double ts = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
			const Metrics mt = measure(t->page(page));
			if (nets(t->page(page)) != netsBefore) fails.push_back("tidy_nets_changed");
			writeFile(out + ".tidy.cdl", cl_document_save_text(t));
			char b[64];
			snprintf(b, sizeof b, "\"tidy_ran\":true,\"tidy_secs\":%.3f,", ts);
			tidy = std::string(b) + jsonMetrics("t_", mt);
			cl_document_close(t);
		}
	}
	std::string f = "[";
	for (size_t i = 0; i < fails.size(); i++) f += (i ? ",\"" : "\"") + fails[i] + "\"";
	f += "]";
	printf("{%s,\"page\":%d,\"secs\":%.3f,%s,%s,%s,\"fail\":%s}\n", head.c_str(), page, secs, jsonMetrics("b_", mb).c_str(),
	       jsonMetrics("a_", ma).c_str(), tidy.c_str(), f.c_str());
	fflush(stdout);
	return 0;
}

int render(const char* in, int page, const char* png, int W, int H) {
	char err[512];
	CLDocument* doc = cl_document_open(in, err, sizeof err);
	if (!doc) { fprintf(stderr, "open failed: %s\n", err); return 1; }
	const double sf = 2.0;
	CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
	CGContextRef ctx = CGBitmapContextCreate(nullptr, W, H, 8, 0, cs, kCGImageAlphaPremultipliedLast);
	CGContextTranslateCTM(ctx, 0, H);
	CGContextScaleCTM(ctx, sf, -sf);
	cl_document_draw_fitted(doc, page, ctx, W / sf, H / sf, 12, sf, CL_STYLE_DARK);
	CGImageRef img = CGBitmapContextCreateImage(ctx);
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)png, strlen(png), false);
	CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
	CGImageDestinationAddImage(dest, img, nullptr);
	bool ok = CGImageDestinationFinalize(dest);
	CFRelease(dest); CFRelease(url); CGImageRelease(img); CGContextRelease(ctx); CGColorSpaceRelease(cs);
	cl_document_close(doc);
	return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "usage: see straighten_lab.cpp\n"); return 2; }
	const std::string mode = argv[1];
	if (!cl_library_load(argv[2])) { fprintf(stderr, "library failed\n"); return 1; }
	parseLibrary(argv[2]);
	cl_set_settle_on_open(false);
	char err[512];
	if (mode == "render" && argc >= 6)
		return render(argv[3], atoi(argv[4]), argv[5], argc > 7 ? atoi(argv[6]) : 1400, argc > 7 ? atoi(argv[7]) : 900);
	if (mode == "pages") {
		CLDocument* doc = cl_document_open(argv[3], err, sizeof err);
		if (!doc) return 1;
		for (int p = 0; p < cl_document_page_count(doc); p++) if (!doc->page(p)->getWireList()->empty()) printf("%d\n", p);
		cl_document_close(doc);
		return 0;
	}
	if (mode == "gen" && argc >= 6) {
		const unsigned seed = (unsigned)strtoul(argv[3], nullptr, 10);
		const int target = std::max(3, atoi(argv[4]));
		std::vector<Placed> parts;
		std::vector<Link> links;
		generate(seed, target, parts, links);
		// Built scrambled, then each part is moved to its place: the wires keep
		// their first routes and bend to follow, as after a user rearranges.
		std::mt19937 rng(seed ^ 0x5bd1e995u);
		std::vector<CLBuildGate> gates;
		std::vector<std::pair<double, double>> off;
		for (auto& p : parts) {
			double dx = (int)(rng() % 17) - 8, dy = (int)(rng() % 17) - 8;
			if (p.gate == "AA_TOGGLE" || p.gate == "BB_CLOCK" || rng() % 4 == 0) dx = dy = 0;
			off.push_back({ dx, dy });
			gates.push_back({ p.gate.c_str(), p.x + dx, p.y + dy, nullptr, 0 });
		}
		std::vector<CLBuildWire> wires;
		for (auto& l : links) wires.push_back({ l.from, l.fromPin.c_str(), l.to, l.toPin.c_str() });
		CLDocument* doc = cl_document_new();
		cl_edit_build(doc, 0, gates.data(), (int)gates.size(), wires.data(), (int)wires.size(), "Lab");
		cl_edit_select_none(doc, 0);
		for (size_t i = 0; i < parts.size(); i++) {   // gate ids are 1.. in build order
			if (off[i].first == 0 && off[i].second == 0) continue;
			if (!cl_edit_select_gate(doc, 0, (long)(i + 1))) continue;
			cl_edit_nudge(doc, 0, -off[i].first, -off[i].second);
		}
		cl_edit_select_none(doc, 0);
		mess(doc, 0, seed, (int)parts.size() / 6);
		char head[256];
		snprintf(head, sizeof head, "\"kind\":\"gen\",\"seed\":%u,\"target\":%d", seed, target);
		int r = runCase(doc, 0, argv[5], head);
		cl_document_close(doc);
		return r;
	}
	if (mode == "file" && argc >= 7) {
		CLDocument* doc = cl_document_open(argv[3], err, sizeof err);
		if (!doc) { printf("{\"kind\":\"file\",\"fail\":[\"open_failed\"]}\n"); return 1; }
		const int page = atoi(argv[4]);
		const int messSeed = atoi(argv[5]);
		if (page < 0 || page >= cl_document_page_count(doc)) return 1;
		if (messSeed >= 0) mess(doc, page, (unsigned)messSeed, std::max(2, (int)doc->page(page)->getGateList()->size() / 6));
		std::string name = argv[3];
		name = name.substr(name.find_last_of('/') + 1);
		for (char& c : name) if (c == '"' || c == '\\') c = '_';
		char head[512];
		snprintf(head, sizeof head, "\"kind\":\"file\",\"file\":\"%s\",\"seed\":%d", name.c_str(), messSeed);
		int r = runCase(doc, page, argv[6], head);
		cl_document_close(doc);
		return r;
	}
	fprintf(stderr, "bad arguments\n");
	return 2;
}
