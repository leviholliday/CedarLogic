// Check my circuit for clocked circuits (docs/CHECK-SEQUENTIAL.md): a count,
// a state table or a timing table, checked clock pulse by clock pulse on a
// copy of the circuit, from power-on or a start state the check reaches by
// itself. The copies are made from the circuit saved as text, so a check can
// run off the main thread (cl_check_clocked_prepare, then _run). The key is read by Check.cpp; what the student reads is written
// here, word for word as the document has it (CedarLogic Online's
// sim-check.js says the same). mac/Tools/check_seq runs the shared cases.

#include "CheckImpl.h"
#include "DocumentImpl.h"
#include "TruthTableImpl.h"
#include "guiGate.h"
#include "guiWire.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clcheck;

namespace {

// ---- The circuit: its switches and lights, and its clocks (§4) ---------------------

struct Port { long gate; std::string name; bool input; bool labeled; std::string value; };

std::pair<float, float> where(guiGate* g) { float x, y; g->getGLcoords(x, y); return { x, y }; }

// The page's switches then lights, each top to bottom then left to right,
// named as cl_truth_table names them when nothing is selected.
std::vector<Port> portsOf(CLDocument* doc, int pageIndex) {
	auto* gates = doc->page(pageIndex)->getGateList();
	std::vector<guiGate*> ins, outs, labels;
	for (auto& g : *gates) {
		if (dynamic_cast<guiGateTOGGLE*>(g.second)) ins.push_back(g.second);
		else if (dynamic_cast<guiGateLED*>(g.second)) outs.push_back(g.second);
		else if (dynamic_cast<guiLabel*>(g.second) && !g.second->getGUIParam("LABEL_TEXT").empty()) labels.push_back(g.second);
	}
	auto byPlace = [](guiGate* a, guiGate* b) {
		const auto pa = where(a), pb = where(b);
		return pa.second != pb.second ? pa.second > pb.second : pa.first < pb.first;
	};
	std::sort(ins.begin(), ins.end(), byPlace);
	std::sort(outs.begin(), outs.end(), byPlace);
	std::sort(labels.begin(), labels.end(), [](guiGate* a, guiGate* b) { return a->getID() < b->getID(); });
	std::vector<bool> used(labels.size(), false);
	auto nameFor = [&](guiGate* port, const std::string& fallback, bool& labeled) {
		const auto p = where(port);
		int best = -1;
		float bestD = 8.0f;
		for (size_t i = 0; i < labels.size(); i++) {
			if (used[i]) continue;
			const std::string text = labels[i]->getGUIParam("LABEL_TEXT");
			if (text.size() > 16) continue;
			const auto q = where(labels[i]);
			const float d = std::hypot(q.first - p.first, q.second - p.second);
			if (d < bestD) { bestD = d; best = (int)i; }
		}
		labeled = best >= 0;
		if (best < 0) return fallback;
		used[best] = true;
		return labels[best]->getGUIParam("LABEL_TEXT");
	};
	std::vector<Port> ports;
	for (size_t i = 0; i < ins.size(); i++) {
		Port p{ (long)ins[i]->getID(), "", true, false, ins[i]->getLogicParam("OUTPUT_NUM") == "1" ? "1" : "0" };
		p.name = nameFor(ins[i], std::string(1, (char)('A' + i)), p.labeled);
		ports.push_back(p);
	}
	for (size_t i = 0; i < outs.size(); i++) {
		Port p{ (long)outs[i]->getID(), "", false, false, "" };
		p.name = nameFor(outs[i], outs.size() == 1 ? "Y" : "Y" + std::to_string(i + 1), p.labeled);
		ports.push_back(p);
	}
	return ports;
}

struct ClockPart { long gate; bool manual; };

std::vector<ClockPart> clocksOf(CLDocument* doc) {
	std::vector<ClockPart> out;
	for (size_t p = 0; p < doc->pages.size(); p++)
		for (auto& g : *doc->page((int)p)->getGateList())
			if (g.second->getLogicType() == "CLOCK") {
				// Looked up, not indexed: getLogicParam would add an empty MANUAL.
				auto* params = g.second->getAllLogicParams();
				auto it = params->find("MANUAL");
				out.push_back({ (long)g.first, it != params->end() && it->second == "true" });
			}
	std::sort(out.begin(), out.end(), [](const ClockPart& a, const ClockPart& b) { return a.gate < b.gate; });
	return out;
}

char readLight(guiGate* g) {
	for (auto& hs : g->getHotspotList()) {
		if (!g->isConnected(hs.first)) continue;
		const std::vector<StateType>& st = g->getConnection(hs.first)->getState();
		if (st.empty()) return 'X';
		switch (st[0]) {
			case ONE: return '1';
			case ZERO: return '0';
			case HI_Z: return 'Z';
			case CONFLICT: return '!';
			default: return 'X';
		}
	}
	return '-';
}


// The primitives of §5, on a copy of the circuit: the document saved and
// opened again before a single step (gate ids survive that, so the ports
// found on the real page address the copy), every clock part a manual clock
// (Step Clock's), started at 0.
struct Sim {
	std::string text;                      // the circuit, saved
	std::vector<long> clockParts;          // every clock part, every page
	long clockSwitch = -1;                 // the clock, when it's a switch (the clock parts are then held at 0)
	std::map<long, std::string> base;      // every switch's power-on value
	CLDocument* doc = nullptr;
	bool settled = true;
	int powerOns = 0;
	const std::atomic<bool>* cancel = nullptr;   // set when the app no longer wants the result
	bool cancelled() const { return cancel && cancel->load(std::memory_order_relaxed); }

	~Sim() { if (doc) cl_document_close(doc); }

	guiGate* gate(long id) {
		for (size_t p = 0; p < doc->pages.size(); p++) {
			auto* gl = doc->page((int)p)->getGateList();
			auto it = gl->find((unsigned long)id);
			if (it != gl->end()) return it->second;
		}
		return nullptr;
	}
	void send(long id, const std::string& name, const std::string& value) {
		doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
			new klsMessage::Message_SET_GATE_PARAM(id, name, value)));
	}
	void param(long id, const std::string& name, const std::string& value) {
		guiGate* g = gate(id);
		if (!g) return;
		g->setLogicParam(name, value);
		send(id, name, value);
	}
	// A cancelled check stops as though it never settled; nobody reads it.
	bool settle() { if (cancelled() || !doc->sim->settle(1000)) settled = false; return settled; }
	bool powerOn() {
		if (doc) cl_document_close(doc);
		doc = nullptr;
		if (cancelled()) { settled = false; return false; }
		powerOns++;
		doc = clOpenText(text.data(), (long)text.size(), nullptr, 0, false);
		if (!doc) { settled = false; return false; }   // made from a save of its own: doesn't happen
		settled = true;
		for (size_t p = 0; p < doc->pages.size(); p++)
			for (auto& g : *doc->page((int)p)->getGateList())
				if (g.second->getLogicType() == "REGISTER") param((long)g.first, "CURRENT_VALUE", "0");
		for (auto& b : base) param(b.first, "OUTPUT_NUM", b.second);
		// The level isn't a setting: it goes straight to the engine.
		for (long id : clockParts) { param(id, "MANUAL", "true"); send(id, "MANUAL_LEVEL", "0"); }
		return settle();
	}
	void set(long id, char v) { param(id, "OUTPUT_NUM", std::string(1, v)); }
	void clockTo(const char* level) {
		if (clockSwitch >= 0) param(clockSwitch, "OUTPUT_NUM", level);
		else for (long id : clockParts) send(id, "MANUAL_LEVEL", level);
	}
	bool pulse() {
		clockTo("1");
		if (!settle()) return false;
		clockTo("0");
		return settle();
	}
	char read(long id) { guiGate* g = gate(id); return g ? readLight(g) : '-'; }
};

// ---- The checker (§6 to §10) -----------------------------------------------------

using Step = CLCheck::Step;

struct Checker {
	CLDocument* real;
	Sim& sim;
	Key k;
	std::map<std::string, std::string> byHand;   // key(asked-for name) -> a switch's or light's name
	CLCheck& res;
	std::vector<Port> ports;             // the page's switches, then its lights (§4.1)

	std::vector<int> switches, lights;   // port indices
	int clockSwitch = -1;
	std::vector<int> inPorts;            // the key's inputs, in the key's order
	std::vector<std::pair<int, char>> setPorts, resetPorts;
	std::vector<int> control;            // switches the start search may turn on or off
	std::vector<std::string> paired;
	bool neverSettled = false;
	// What each try of the start search (§7) read, every port: a try always
	// reads the same, so a search for another state reads it from here
	// instead of powering on again. Keyed by the switches it set.
	std::map<std::string, std::string> tried;
	// Powering on is most of a check's time: past this many the search stops
	// trying new switches and a state table's walk stops.
	static constexpr int maxPowerOns = 300;
	bool outOfTries = false;

	Checker(CLDocument* real, Sim& sim, CLCheck& res) : real(real), sim(sim), res(res) {}

	const Port& port(int i) { return ports[i]; }
	// An error: verdict 2, and only the notes that say what to fix.
	CLCheck& fail(const std::string& error, const std::string& summary) {
		res.verdict = 2; res.error = error; res.summary = summary;
		res.notes.erase(std::remove_if(res.notes.begin(), res.notes.end(), [](auto& n) { return n.first != Problem; }), res.notes.end());
		return res;
	}

	int findPort(const std::string& want, const std::vector<int>& pool, bool& hand) {
		hand = false;
		auto h = byHand.find(key(want));
		if (h != byHand.end())
			for (int p : pool) if (key(port(p).name) == key(h->second)) { hand = true; return p; }
		for (int p : pool) if (key(port(p).name) == key(want)) return p;
		return -1;
	}

	// By hand, by name, then what's left in order when both sides have the same number.
	std::vector<int> match(const std::vector<std::string>& want, const std::vector<int>& pool, bool input, std::vector<std::string>& missing) {
		std::set<int> taken;
		std::vector<int> col(want.size(), -1);
		std::vector<bool> hand(want.size(), false);
		for (size_t i = 0; i < want.size(); i++) {
			auto h = byHand.find(key(want[i]));
			if (h == byHand.end()) continue;
			for (int p : pool) if (!taken.count(p) && key(port(p).name) == key(h->second)) { col[i] = p; hand[i] = true; taken.insert(p); break; }
		}
		for (size_t i = 0; i < want.size(); i++) {
			if (col[i] >= 0) continue;
			for (int p : pool) if (!taken.count(p) && key(port(p).name) == key(want[i])) { col[i] = p; taken.insert(p); break; }
		}
		std::vector<int> left, spare;
		for (size_t i = 0; i < want.size(); i++) if (col[i] < 0) left.push_back((int)i);
		for (int p : pool) if (!taken.count(p)) spare.push_back(p);
		if (!left.empty() && left.size() == spare.size()) {
			for (size_t j = 0; j < left.size(); j++) {
				col[left[j]] = spare[j];
				paired.push_back(want[left[j]] + " is " + port(spare[j]).name);
			}
		}
		for (size_t i = 0; i < want.size(); i++) {
			res.names.push_back({ want[i], input, col[i], hand[i] });
			if (col[i] < 0) missing.push_back(want[i]);
		}
		return col;
	}

	CLCheck& noSwitch(const std::vector<std::string>& names) {
		std::vector<std::string> all;
		for (int s : switches) all.push_back(port(s).name);
		const std::string what = names.size() == 1 ? "'s no switch called " + names[0] : " are no switches called " + list(names);
		res.notes = { { Problem, "There" + what + " (" + (all.empty() ? std::string("this page has no switches") : "the switches are " + list(all)) +
		                         "). Pick which switch " + (names.size() == 1 ? "it is" : "each one is") + " under Names, or change a switch's label." } };
		return fail("no_switch", "Can't check yet: there" + what + ".");
	}
	CLCheck& noLight(const std::vector<std::string>& names) {
		std::vector<std::string> all;
		for (int l : lights) all.push_back(port(l).name);
		res.notes.clear();
		for (const std::string& n : names)
			res.notes.push_back({ Problem, "There's no light called " + n + " (" + (all.empty() ? std::string("this page has no lights") : "the lights are " + list(all)) +
			                               "). Pick which light it is under Names, or change a light's label." });
		const std::string what = names.size() == 1 ? "'s no light called " : " are no lights called ";
		return fail("no_light", "Can't check yet: there" + what + list(names) + ".");
	}

	std::string readPorts(const std::vector<int>& which) { std::string s; for (int p : which) s += sim.read(port(p).gate); return s; }

	std::string setsText(const std::vector<std::pair<int, char>>& sets) {
		std::vector<std::string> parts;
		for (auto& s : sets) parts.push_back(port(s.first).name + " to " + s.second);
		return "To start, the check set " + list(parts) + ", gave one clock pulse and set " + (sets.size() == 1 ? "it" : "them") + " back.";
	}
	// Set some switches, settle, a pulse, set them back, settle.
	bool withPulse(const std::vector<std::pair<int, char>>& sets) {
		for (auto& s : sets) sim.set(port(s.first).gate, s.second);
		if (!sim.settle() || !sim.pulse()) return false;
		for (auto& s : sets) sim.set(port(s.first).gate, sim.base[port(s.first).gate][0]);
		return sim.settle();
	}

	// §7: power on; then the reset: line; then the other switches, a few at a
	// time, each try from power-on. `how` says what got there ("" at power-on).
	struct Reached { bool ok = false; std::string how; };
	Reached reach(const std::vector<int>& which, const std::function<bool(const std::string&)>& good) {
		Reached r;
		// A try: power on, the switches in `sets` (none: just power on). One
		// already made is read from `tried`, and made again only when it
		// works, to leave the copy there.
		auto attempt = [&](const std::vector<std::pair<int, char>>& sets) {
			std::string id;
			for (auto& s : sets) id += std::to_string(s.first) + "=" + s.second + ";";
			auto seen = tried.find(id);
			if (seen != tried.end()) {
				std::string got;
				for (int p : which) got += seen->second[p];
				if (!good(got)) return false;
			} else if (sim.powerOns >= maxPowerOns) {
				outOfTries = true;
				return false;
			}
			if (!sim.powerOn() || (!sets.empty() && !withPulse(sets))) { neverSettled = true; return false; }
			std::vector<int> all(ports.size());
			for (size_t p = 0; p < ports.size(); p++) all[p] = (int)p;
			tried[id] = readPorts(all);
			return good(readPorts(which));
		};
		if (attempt({})) { r.ok = true; return r; }
		if (neverSettled) return r;
		if (k.hasReset) {
			if (attempt(resetPorts)) { r.ok = true; r.how = setsText(resetPorts); return r; }
			if (neverSettled) return r;
		}
		const int c = std::min<int>(8, (int)control.size());
		for (int size = 1; size <= std::min(4, c); size++) {
			std::vector<int> idx(size);
			for (int i = 0; i < size; i++) idx[i] = i;
			for (;;) {
				std::vector<std::pair<int, char>> sets;
				for (int i : idx) sets.push_back({ control[i], sim.base[port(control[i]).gate] == "1" ? '0' : '1' });
				if (attempt(sets)) { r.ok = true; r.how = setsText(sets); return r; }
				if (neverSettled) return r;
				int i = size - 1;
				while (i >= 0 && idx[i] == c - size + i) i--;
				if (i < 0) break;
				idx[i]++;
				for (int j = i + 1; j < size; j++) idx[j] = idx[j - 1] + 1;
			}
		}
		return r;
	}

	CLCheck& unreachable(const std::string& summary, const std::string& got) {
		const bool any = k.hasReset || !control.empty();
		res.notes = { { Problem, "At the start the lights show " + got + (any
			? ". Turning the other switches on or off, with a clock pulse, didn't get them there."
			: ", and there's no other switch to reset them with.") +
			" Add a reset (a switch on the flip-flops' CLR' or PRE'), or name it with a line like reset: CLR' = 0." } };
		return fail("start_unreachable", summary);
	}

	CLCheck& unsettled(const std::string& where) {
		res.verdict = 1;
		res.error = "never_settles";
		res.summary = "Doesn't settle: " + where + " the circuit was still changing after 1000 steps, so a loop of gates may be flipping back and forth.";
		res.firstWrong = (int)res.steps.size() - 1;
		return res;
	}

	void oddNotes(const std::vector<std::string>& names, const std::vector<int>& at) {
		for (size_t s = 0; s < names.size(); s++) {
			std::map<char, int> odd;
			for (auto& st : res.steps)
				if (at[s] < (int)st.got.size()) { const char v = st.got[at[s]]; if (v != '0' && v != '1' && v != '~') odd[v]++; }
			for (auto& [v, count] : odd) {
				const char* what = v == 'X' ? "X (unknown): a gate feeding it may be missing an input"
				                 : v == 'Z' ? "Z (floating): nothing is driving it"
				                 : v == '!' ? "! (a conflict): two outputs are wired together"
				                            : "nothing: it isn't connected";
				res.notes.push_back({ Warning, "On " + plural(count, "step", "steps") + " the light " + names[s] + " shows " + what + "." });
			}
		}
	}

	// The notes every kind adds once its names are matched.
	void matchNotes(const std::vector<int>& usedLights) {
		if (!paired.empty()) res.notes.push_back({ Info, "Matched by position, as the names differ: " + list(paired) + "." });
		std::vector<std::string> names, values;
		for (int s : control) {
			if (std::any_of(resetPorts.begin(), resetPorts.end(), [&](auto& x) { return x.first == s; })) continue;
			names.push_back(port(s).name);
			values.push_back(sim.base[port(s).gate]);
		}
		if (!names.empty())
			res.notes.push_back({ Info, (names.size() == 1 ? "Switch " + names[0] + " isn't" : "Switches " + list(names) + " aren't") +
			                            " in what was asked for, so " + (names.size() == 1 ? "it stayed as it is" : "they stayed as they are") +
			                            " on the page (" + list(values) + ")." });
		names.clear();
		for (int l : lights) if (std::find(usedLights.begin(), usedLights.end(), l) == usedLights.end()) names.push_back(port(l).name);
		if (!names.empty())
			res.notes.push_back({ Info, (names.size() == 1 ? "Light " + names[0] + " isn't" : "Lights " + list(names) + " aren't") +
			                            " in what was asked for, so " + (names.size() == 1 ? "it wasn't" : "they weren't") + " checked." });
	}

	static std::string number(const std::string& bits) {
		if (!binary(bits)) return bits;
		long v = 0;
		for (char c : bits) v = v * 2 + (c == '1');
		return std::to_string(v);
	}
	static std::string bitsOf(long v, int n) { std::string s; for (int i = n - 1; i >= 0; i--) s += ((v >> i) & 1) ? '1' : '0'; return s; }

	CLCheck& run(const std::string& text, int pageIndex, const std::map<std::string, std::string>& hand);
	CLCheck& runCount();
	CLCheck& runTiming();
	CLCheck& runStates();
};

std::string dontCares(int n) { return " (" + plural(n, "don't-care wasn't", "don't-cares weren't") + " checked)."; }

CLCheck& Checker::run(const std::string& text, int pageIndex, const std::map<std::string, std::string>& hand) {
	for (auto& h : hand) byHand[key(h.first)] = h.second;
	k = readKey(text);
	res.kind = k.kind;
	if (k.kind == CL_KEY_EMPTY && !k.options)
		return fail("empty", "Type or paste what the assignment asks for: a formula, a truth table, a count, a state table or a timing table.");
	if (!k.error.empty()) return fail("bad_key", k.error);

	// The page's switches and lights, and the clock.
	if (!real || !real->page(pageIndex)) return fail("", "There's no circuit to check.");
	ports = portsOf(real, pageIndex);
	for (const Port& p : ports) res.ports.push_back({ p.name, p.input });
	for (size_t i = 0; i < ports.size(); i++) (ports[i].input ? switches : lights).push_back((int)i);
	const auto clocks = clocksOf(real);
	bool hand1;
	if (k.hasClock) {
		clockSwitch = findPort(k.clock, switches, hand1);
		res.names.push_back({ k.clock, true, clockSwitch, hand1 });
		if (clockSwitch < 0) return noSwitch({ k.clock });
	} else if (clocks.empty()) {
		for (int s : switches) if (key(port(s).name) == "clk" || key(port(s).name) == "clock") { clockSwitch = s; break; }
		if (clockSwitch < 0) {
			res.notes = { { Problem, "Add a clock (from Input and Output) to the flip-flops' clock inputs, or say which switch is the clock with a line like clock: CLK." } };
			return fail("no_clock", "Can't check yet: there's no clock.");
		}
	}
	for (auto& p : ports) if (p.input) sim.base[p.gate] = p.value;
	// Clock parts pulse, or with a clock switch are held at 0.
	for (auto& c : clocks) sim.clockParts.push_back(c.gate);
	if (clockSwitch >= 0) {
		sim.clockSwitch = port(clockSwitch).gate;
		sim.base[port(clockSwitch).gate] = "0";
	}

	// The key's inputs.
	std::vector<std::string> inNames;
	if (k.kind == CL_KEY_TIMING) {
		int ni = k.bar;
		if (ni < 0) {
			ni = 0;
			while (ni < (int)k.names.size() && findPort(k.names[ni], switches, hand1) >= 0) ni++;
			if (ni == (int)k.names.size()) return fail("bad_key", "Put a | between the switches and the lights in the top row, like Pulse | X | Z.");
		}
		inNames.assign(k.names.begin(), k.names.begin() + ni);
		k.outs.assign(k.names.begin() + ni, k.names.end());
		for (auto& r : k.rows) { r.in = r.out.substr(0, ni); r.out = r.out.substr(ni); }
	} else if (k.kind == CL_KEY_STATES) {
		inNames = k.ins;
	}
	for (const std::string& n : inNames) {
		bool isClock;
		if (clockSwitch >= 0) {
			auto h = byHand.find(key(n));
			isClock = key(n) == key(port(clockSwitch).name) || (h != byHand.end() && key(h->second) == key(port(clockSwitch).name));
		} else {
			isClock = key(n) == "clk" || key(n) == "clock";
		}
		if (isClock) {
			res.notes = { { Problem, "Each row is one clock pulse, and the check turns the clock on and off itself. Leave " + n + " out of the table." } };
			return fail("clock_in_key", "Can't check yet: " + n + " is the clock, so it can't be a column too.");
		}
	}
	std::vector<int> pool;
	for (int s : switches) if (s != clockSwitch) pool.push_back(s);
	std::vector<std::string> missing;
	inPorts = match(inNames, pool, true, missing);
	if (!missing.empty()) return noSwitch(missing);
	// set: and reset: name switches, by name only.
	auto named = [&](const Assign& a, std::vector<std::pair<int, char>>& out) {
		for (size_t i = 0; i < a.names.size(); i++) {
			bool h;
			const int p = findPort(a.names[i], pool, h);
			res.names.push_back({ a.names[i], true, p, h });
			if (p < 0) missing.push_back(a.names[i]); else out.push_back({ p, a.bits[i] });
		}
	};
	named(k.set, setPorts);
	named(k.reset, resetPorts);
	if (!missing.empty()) return noSwitch(missing);
	for (auto& s : setPorts)
		if (std::find(inPorts.begin(), inPorts.end(), s.first) != inPorts.end())
			return fail("bad_key", port(s.first).name + " is in the table and in set:; leave it out of one.");
	for (auto& s : setPorts) sim.base[port(s.first).gate] = std::string(1, s.second);
	for (int s : pool) {
		const bool used = std::find(inPorts.begin(), inPorts.end(), s) != inPorts.end() ||
		                  std::any_of(setPorts.begin(), setPorts.end(), [&](auto& x) { return x.first == s; });
		if (!used) control.push_back(s);
	}

	// Notes on the clock (each kind adds the names').
	if (clockSwitch < 0 && clocks.size() == 1 && !clocks[0].manual)
		res.notes.push_back({ Info, "The clock runs by itself on your page; for the check it was stepped one pulse at a time." });
	if (clockSwitch < 0 && clocks.size() > 1)
		res.notes.push_back({ Info, "This circuit has " + std::to_string(clocks.size()) + " clocks; for the check they all pulsed together, one pulse at a time." });
	if (clockSwitch >= 0 && !k.hasClock)
		res.notes.push_back({ Info, "There's no clock, so the switch " + port(clockSwitch).name + " was used as one: each pulse turned it on and off." });
	if (clockSwitch >= 0 && k.hasClock && !clocks.empty())
		res.notes.push_back({ Info, "The clock: line makes " + port(clockSwitch).name + " the clock, so the clock parts on the page were held at 0." });

	if (k.kind == CL_KEY_COUNT) return runCount();
	if (k.kind == CL_KEY_TIMING) return runTiming();
	return runStates();
}

CLCheck& Checker::runCount() {
	// Which lights make the number (§8.1).
	std::vector<int> watch;
	const bool byDefault = k.countNames.empty();
	if (byDefault) {
		for (int l : lights) {
			const std::string n = trim(port(l).name);
			std::string base;
			if (n.size() > 1 && n.back() == '\'') base = n.substr(0, n.size() - 1);
			else if (n.size() > 3 && n.compare(n.size() - 3, 3, "\xE2\x80\x99") == 0) base = n.substr(0, n.size() - 3);
			bool complement = false;
			if (!base.empty())
				for (int o : lights) if (o != l && lower(trim(port(o).name)) == lower(trim(base))) complement = true;
			if (!complement) watch.push_back(l);
		}
		if (watch.empty()) {
			res.notes = { { Problem, "Put a light on each flip-flop's output and label them, like Q2, Q1 and Q0." } };
			return fail("no_lights", "Can't check yet: there are no lights on this page to read the count from.");
		}
		// Lights labeled one name and different numbers (Q0, Q1, Q2) go by number, highest first.
		bool numbered = watch.size() >= 2;
		std::string prefix;
		std::set<long> seen;
		std::vector<std::pair<long, int>> order;
		for (int l : watch) {
			const std::string n = trim(port(l).name);
			size_t d = n.size();
			while (d > 0 && std::isdigit((unsigned char)n[d - 1])) d--;
			bool letters = true;
			for (size_t i = 0; i < d; i++) if (!std::isalpha((unsigned char)n[i]) && n[i] != '_') letters = false;
			if (!port(l).labeled || d == n.size() || n.size() - d > 6 || !letters) { numbered = false; break; }
			const std::string p = lower(n.substr(0, d));
			const long v = std::stol(n.substr(d));
			if (order.empty()) prefix = p;
			if (p != prefix || seen.count(v)) { numbered = false; break; }
			seen.insert(v);
			order.push_back({ v, l });
		}
		if (numbered) {
			std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
			watch.clear();
			for (auto& o : order) watch.push_back(o.second);
		}
		for (int l : watch) res.names.push_back({ port(l).name, false, l, false });
	} else {
		std::vector<std::string> missing;
		watch = match(k.countNames, lights, false, missing);
		if (!missing.empty()) return noLight(missing);
	}
	const int n = (int)watch.size();
	if (n > 16) return fail("bad_key", "That's " + std::to_string(n) + " lights; a count can use up to 16.");
	// The numbers: binary when every one is 0s and 1s of the same length, 2 or more; else decimal.
	bool inBinary = true;
	for (auto& t : k.countTokens) if (t.size() < 2 || !binary(t) || t.size() != k.countTokens[0].size()) inBinary = false;
	if (inBinary && (int)k.countTokens[0].size() != n)
		return fail("bad_key", "Those numbers have " + std::to_string(k.countTokens[0].size()) + " bits, but the count has " +
		                       plural(n, "light", "lights") + ".");
	std::vector<long> values;
	const long max = (1L << n) - 1;
	for (auto& t : k.countTokens) {
		long v = 0;
		if (inBinary) for (char c : t) v = v * 2 + (c == '1');
		else v = t.size() > 9 ? max + 1 : std::stol(t);
		if (v > max)
			return fail("bad_key", t + " doesn't fit in " + plural(n, "light", "lights") + ": " + (n == 1 ? "it counts" : "they count") +
			                       " up to " + std::to_string(max) + ".");
		values.push_back(v);
	}
	if (k.cyclic && values.size() >= 2 && values.back() == values.front()) values.pop_back();
	const int L = (int)values.size();
	for (int i = 0; i < n; i++) res.signals.push_back({ byDefault ? port(watch[i]).name : k.countNames[i], CL_SIGNAL_OUTPUT, watch[i] });

	matchNotes(watch);
	if (byDefault) {
		std::vector<std::string> ns;
		for (int l : watch) ns.push_back(port(l).name);
		res.notes.push_back({ Info, n == 1 ? "The count is read from the light " + ns[0] + "."
		                                   : "The count is read from the lights " + joined(ns, " ") + ", most significant first." });
	}

	// The start (§7).
	auto good = [&](const std::string& bits) {
		if (!binary(bits)) return false;
		const long x = std::stol(number(bits));
		return k.cyclic ? std::find(values.begin(), values.end(), x) != values.end() : x == values[0];
	};
	Reached r = reach(watch, good);
	if (neverSettled) {
		Step s; s.kind = CL_STEP_START; s.exp = bitsOf(values[0], n); s.got = std::string(n, '~'); s.wrong = true;
		res.steps.push_back(s);
		return unsettled("at the start");
	}
	if (!r.ok) {
		sim.powerOn();
		return unreachable(k.cyclic ? "Can't check yet: the lights can't be set to a number in the count to start."
		                            : "Can't check yet: the lights can't be set to " + std::to_string(values[0]) + " to start.",
		                   number(readPorts(watch)));
	}
	if (!r.how.empty()) res.notes.push_back({ Info, r.how });
	const std::string first = readPorts(watch);
	const int at = k.cyclic ? (int)(std::find(values.begin(), values.end(), std::stol(number(first))) - values.begin()) : 0;
	if (at != 0) res.notes.push_back({ Info, "The lights started at " + number(first) + ", so the check started there in the count." });
	Step s0; s0.kind = CL_STEP_START; s0.exp = bitsOf(values[at], n); s0.got = first;
	res.steps.push_back(s0);
	const int pulses = k.cyclic ? 2 * L : L - 1;
	for (int p = 1; p <= pulses; p++) {
		Step s;
		s.pulse = p;
		s.exp = bitsOf(values[k.cyclic ? (at + p) % L : p], n);
		if (!sim.pulse()) {
			s.got = std::string(n, '~'); s.wrong = true;
			res.steps.push_back(s);
			return unsettled("at clock pulse " + std::to_string(p));
		}
		s.got = readPorts(watch);
		s.wrong = s.got != s.exp;
		res.steps.push_back(s);
	}
	std::vector<std::string> ns;
	std::vector<int> idx;
	for (int i = 0; i < n; i++) { ns.push_back(port(watch[i]).name); idx.push_back(i); }
	oddNotes(ns, idx);
	for (size_t i = 0; i < res.steps.size(); i++) if (res.steps[i].wrong) { res.firstWrong = (int)i; break; }
	if (res.firstWrong >= 0) {
		const Step& w = res.steps[res.firstWrong];
		const std::string got = number(w.got);
		res.verdict = 1;
		res.summary = "Doesn't match: after clock pulse " + std::to_string(w.pulse) + (n == 1 ? " the light shows " : " the lights show ") + got + ", not " + number(w.exp) +
		              (!binary(w.got) && n > 1 ? " (" + w.exp + ")" : std::string("")) + ".";
		return res;
	}
	std::vector<std::string> seq;
	for (int i = 0; i < L; i++) seq.push_back(std::to_string(values[i]));
	if (L > 8) { seq.erase(seq.begin() + 6, seq.end() - 1); seq.insert(seq.end() - 1, "\xE2\x80\xA6"); }
	res.verdict = 0;
	res.summary = std::string(n == 1 ? "Matches: the light counts " : "Matches: the lights count ") + joined(seq, ", ") +
	              (k.cyclic ? " and repeat (checked twice round)." : ".");
	return res;
}

CLCheck& Checker::runTiming() {
	std::vector<std::string> missing;
	const std::vector<int> watch = match(k.outs, lights, false, missing);
	if (!missing.empty()) return noLight(missing);
	// start: names lights, by name only.
	std::vector<int> startPorts;
	for (auto& nm : k.start.names) {
		bool h;
		const int p = findPort(nm, lights, h);
		res.names.push_back({ nm, false, p, h });
		if (p < 0) missing.push_back(nm); else startPorts.push_back(p);
	}
	if (!missing.empty()) return noLight(missing);
	for (size_t i = 0; i < inPorts.size(); i++) res.signals.push_back({ k.names[i], CL_SIGNAL_INPUT, inPorts[i] });
	for (size_t i = 0; i < watch.size(); i++) res.signals.push_back({ k.outs[i], CL_SIGNAL_OUTPUT, watch[i] });
	std::vector<int> used = watch;
	used.insert(used.end(), startPorts.begin(), startPorts.end());
	matchNotes(used);

	const size_t no = watch.size();
	auto settleFailAtStart = [&]() -> CLCheck& {
		Step s; s.kind = CL_STEP_START; s.exp = std::string(no, '-'); s.got = std::string(no, '~'); s.wrong = true;
		res.steps.push_back(s);
		return unsettled("at the start");
	};
	if (k.hasStart) {
		Reached r = reach(startPorts, [&](const std::string& s) { return s == k.start.bits; });
		if (neverSettled) return settleFailAtStart();
		if (!r.ok) {
			sim.powerOn();
			std::vector<std::string> ns;
			for (int p : startPorts) ns.push_back(port(p).name);
			return unreachable("Can't check yet: the circuit can't be set to " + joined(ns, " ") + " = " + k.start.bits + " to start.",
			                   readPorts(startPorts));
		}
		if (!r.how.empty()) res.notes.push_back({ Info, r.how });
	} else {
		if (!sim.powerOn()) return settleFailAtStart();
		if (k.hasReset) {
			if (!withPulse(resetPorts)) return settleFailAtStart();
			res.notes.push_back({ Info, setsText(resetPorts) });
		}
	}
	std::string inputs;
	for (int p : inPorts) inputs += sim.base[port(p).gate];
	if (k.rows[0].pulse != 0) {
		Step s; s.kind = CL_STEP_START; s.exp = std::string(no, '-'); s.got = readPorts(watch);
		res.steps.push_back(s);
	}
	int dc = 0, checked = 0;
	for (auto& row : k.rows) {
		Step s;
		s.kind = row.pulse == 0 ? CL_STEP_START : CL_STEP_PULSE;
		s.pulse = row.pulse;
		s.row = row.number;
		for (size_t i = 0; i < inPorts.size(); i++) {
			if (row.in[i] != '-') inputs[i] = row.in[i];
			sim.set(port(inPorts[i]).gate, inputs[i]);
		}
		s.in = inputs;
		s.exp = row.out;
		if (!sim.settle() || (row.pulse != 0 && !sim.pulse())) {
			s.got = std::string(no, '~'); s.wrong = true;
			res.steps.push_back(s);
			return unsettled(row.pulse == 0 ? "at the start" : "at clock pulse " + std::to_string(row.pulse));
		}
		s.got = readPorts(watch);
		for (size_t i = 0; i < no; i++) {
			if (s.exp[i] == '-') { dc++; continue; }
			checked++;
			if (s.got[i] != s.exp[i]) s.wrong = true;
		}
		res.steps.push_back(s);
	}
	std::vector<std::string> lightNames;
	std::vector<int> idx;
	for (size_t i = 0; i < no; i++) { lightNames.push_back(port(watch[i]).name); idx.push_back((int)i); }
	oddNotes(lightNames, idx);
	for (size_t i = 0; i < res.steps.size(); i++) if (res.steps[i].wrong) { res.firstWrong = (int)i; break; }
	if (res.firstWrong >= 0) {
		const Step& w = res.steps[res.firstWrong];
		std::vector<std::string> parts;
		for (size_t i = 0; i < no; i++)
			if (w.exp[i] != '-' && w.got[i] != w.exp[i]) parts.push_back(k.outs[i] + " is " + w.got[i] + " instead of " + w.exp[i]);
		res.verdict = 1;
		res.summary = "Doesn't match: " + (w.pulse == 0 ? std::string("at the start") : "after clock pulse " + std::to_string(w.pulse)) + ", " +
		              list(parts) + ".";
		return res;
	}
	res.verdict = 0;
	res.summary = checked == 0 ? "Matches, but every value was a don't-care, so nothing was really checked."
	                           : "Matches: every clock pulse gives what was asked for" + (dc > 0 ? dontCares(dc) : std::string("."));
	return res;
}

CLCheck& Checker::runStates() {
	std::vector<std::string> want = k.state, missing;
	want.insert(want.end(), k.outs.begin(), k.outs.end());
	const std::vector<int> got = match(want, lights, false, missing);
	if (!missing.empty()) return noLight(missing);
	const int ns = (int)k.state.size(), no = (int)k.outs.size();
	const std::vector<int> statePorts(got.begin(), got.begin() + ns), outPorts(got.begin() + ns, got.end());
	// start: the state's bits, in the state's order.
	std::string startBits;
	if (k.hasStart) {
		if (k.start.names.empty()) {
			if ((int)k.start.bits.size() != ns)
				return fail("bad_key", "start: needs " + plural(ns, "value", "values") + ", one for each of " + list(k.state) + ".");
			startBits = k.start.bits;
		} else {
			startBits = std::string(ns, '?');
			for (size_t i = 0; i < k.start.names.size(); i++) {
				int at = -1;
				for (int j = 0; j < ns; j++) if (key(k.state[j]) == key(k.start.names[i])) at = j;
				if (at < 0 || startBits[at] != '?' || (int)k.start.names.size() != ns)
					return fail("bad_key", "start: gives the state, so it names " + list(k.state) + ".");
				startBits[at] = k.start.bits[i];
			}
		}
	}
	for (size_t i = 0; i < inPorts.size(); i++) res.signals.push_back({ k.ins[i], CL_SIGNAL_INPUT, inPorts[i] });
	for (int j = 0; j < ns; j++) res.signals.push_back({ k.state[j], CL_SIGNAL_STATE, statePorts[j] });
	for (int j = 0; j < ns; j++) res.signals.push_back({ k.next[j], CL_SIGNAL_NEXT, statePorts[j] });
	for (int j = 0; j < no; j++) res.signals.push_back({ k.outs[j], CL_SIGNAL_OUTPUT, outPorts[j] });
	matchNotes(got);

	auto stateText = [&](const std::string& bits) { return joined(k.state, " ") + " = " + bits; };
	auto inText = [&](const std::string& bits) { return k.ins.empty() ? std::string("") : " with " + joined(k.ins, " ") + " = " + bits; };
	auto unsettledStep = [&](int kind) { Step s; s.kind = kind; s.state = std::string(ns, '~'); s.wrong = true; res.steps.push_back(s); };

	// The start, and a restart: the same again. 0 there, 1 an error, 2 never settled.
	auto start = [&](bool first) -> int {
		if (k.hasStart) {
			Reached r = reach(statePorts, [&](const std::string& s) { return s == startBits; });
			if (neverSettled) return 2;
			if (!r.ok) {
				sim.powerOn();
				unreachable("Can't check yet: the circuit can't be set to " + stateText(startBits) + " to start.", readPorts(statePorts));
				return 1;
			}
			if (first && !r.how.empty()) res.notes.push_back({ Info, r.how });
		} else {
			if (!sim.powerOn()) return 2;
			if (k.hasReset) {
				if (!withPulse(resetPorts)) return 2;
				if (first) res.notes.push_back({ Info, setsText(resetPorts) });
			}
			const std::string st = readPorts(statePorts);
			if (!binary(st)) {
				res.notes = { { Problem, "Flip-flops made from gates start unknown. Add a start: line, like start: " +
				                         stateText(std::string(ns, '0')) + ", and a reset the check can use (a switch, or a reset: line)." } };
				fail("start_unknown", ns == 1 ? "Can't check yet: at the start " + k.state[0] + " is " + st + ", not 0 or 1."
				                              : "Can't check yet: at the start the state " + joined(k.state, " ") + " is " + st + ", not 0s and 1s.");
				return 1;
			}
		}
		Step s;
		s.kind = CL_STEP_START;
		s.state = readPorts(statePorts);
		res.steps.push_back(s);
		return 0;
	};
	int st = start(true);
	if (st == 1) return res;
	if (st == 2) { unsettledStep(CL_STEP_START); return unsettled("at the start"); }
	const std::string s0 = res.steps.back().state;
	std::string cur = s0;

	const int R = (int)k.rows.size();
	std::vector<bool> done(R, false);
	std::vector<std::string> seen(R);     // the next state each row gave
	std::set<std::string> cannotSet;
	int pulses = 0, dc = 0;
	bool stopped = false, settleFail = false;

	// A row: its inputs, settle, the outputs read; a pulse; the next state read.
	auto execute = [&](int q) -> bool {
		const Key::Row& row = k.rows[q];
		Step s;
		for (int i = (int)res.steps.size() - 1; i >= 0; i--) if (res.steps[i].kind != CL_STEP_PULSE) { s.pulse = (int)res.steps.size() - i; break; }
		s.row = row.number;
		s.state = cur;
		s.in = row.in;
		s.exp = row.nx + row.out;
		for (size_t i = 0; i < inPorts.size(); i++) sim.set(port(inPorts[i]).gate, row.in[i]);
		pulses++;
		if (!sim.settle()) settleFail = true;
		const std::string outs = settleFail ? std::string(no, '~') : readPorts(outPorts);
		if (!settleFail && !sim.pulse()) settleFail = true;
		const std::string next = settleFail ? std::string(ns, '~') : readPorts(statePorts);
		s.got = next + outs;
		if (settleFail) { s.wrong = true; res.steps.push_back(s); return false; }
		if (!done[q]) for (char c : s.exp) if (c == '-') dc++;
		for (size_t i = 0; i < s.exp.size(); i++) if (s.exp[i] != '-' && s.exp[i] != s.got[i]) s.wrong = true;
		res.steps.push_back(s);
		done[q] = true;
		seen[q] = next;
		cur = next;
		return !s.wrong;
	};
	auto rowsLeftIn = [&](const std::string& state) {
		for (int q = 0; q < R; q++) if (!done[q] && k.rows[q].st == state) return true;
		return false;
	};
	bool justRestarted = false;
	for (;;) {
		if (std::find(done.begin(), done.end(), false) == done.end()) break;
		if (pulses >= 1000) { stopped = true; break; }
		if (sim.powerOns >= maxPowerOns) { stopped = outOfTries = true; break; }
		// 1. A row for the state it's in.
		int here = -1;
		for (int q = 0; q < R; q++) if (!done[q] && k.rows[q].st == cur) { here = q; break; }
		if (here >= 0) {
			justRestarted = false;
			if (!execute(here)) break;
			continue;
		}
		// 2. The nearest state with rows left, along rows already run.
		std::map<std::string, std::pair<std::string, int>> prev;
		std::vector<std::string> queue = { cur };
		prev[cur] = { "", -1 };
		std::string goal;
		for (size_t qi = 0; qi < queue.size() && goal.empty(); qi++) {
			const std::string s = queue[qi];
			for (int q = 0; q < R && goal.empty(); q++) {
				if (!done[q] || k.rows[q].st != s || prev.count(seen[q])) continue;
				prev[seen[q]] = { s, q };
				if (rowsLeftIn(seen[q])) goal = seen[q];
				queue.push_back(seen[q]);
			}
		}
		if (!goal.empty()) {
			std::vector<int> path;
			for (std::string s = goal; prev[s].second >= 0; s = prev[s].first) path.push_back(prev[s].second);
			std::reverse(path.begin(), path.end());
			bool ok = true;
			for (int q : path) if (pulses >= 1000 || !(ok = execute(q))) break;
			if (!ok) break;
			justRestarted = false;
			continue;
		}
		// 3. Back to the start.
		if (cur != s0 && !justRestarted) {
			st = start(false);
			if (st == 2) { settleFail = true; unsettledStep(CL_STEP_START); break; }
			cur = res.steps.back().state;
			justRestarted = true;
			continue;
		}
		// 4. Set a state with switches: the first state with rows left that hasn't failed.
		bool set = false;
		for (int q = 0; q < R && !set; q++) {
			const std::string target = k.rows[q].st;
			if (done[q] || cannotSet.count(target)) continue;
			Reached r = reach(statePorts, [&](const std::string& s) { return s == target; });
			if (neverSettled) break;
			if (!r.ok) { cannotSet.insert(target); continue; }
			Step s;
			s.kind = CL_STEP_SET;
			s.state = readPorts(statePorts);
			res.steps.push_back(s);
			cur = s.state;
			justRestarted = false;
			set = true;
		}
		if (neverSettled) { settleFail = true; unsettledStep(CL_STEP_SET); break; }
		if (!set) { stopped = outOfTries; break; }
	}
	if (settleFail) {
		const Step& w = res.steps.back();
		return unsettled(w.kind == CL_STEP_PULSE ? "in row " + std::to_string(w.row) : "at the start");
	}
	std::vector<std::string> sigNames;
	std::vector<int> idx;
	for (int j = 0; j < ns; j++) { sigNames.push_back(port(statePorts[j]).name); idx.push_back(j); }
	for (int j = 0; j < no; j++) { sigNames.push_back(port(outPorts[j]).name); idx.push_back(ns + j); }
	oddNotes(sigNames, idx);
	for (size_t i = 0; i < res.steps.size(); i++) if (res.steps[i].wrong) { res.firstWrong = (int)i; break; }
	if (res.firstWrong >= 0) {
		const Step& w = res.steps[res.firstWrong];
		std::vector<std::string> parts;
		bool nextWrong = false;
		for (int j = 0; j < ns; j++) if (w.exp[j] != '-' && w.got[j] != w.exp[j]) nextWrong = true;
		if (nextWrong) parts.push_back("the next state is " + w.got.substr(0, ns) + " instead of " + w.exp.substr(0, ns));
		for (int j = 0; j < no; j++)
			if (w.exp[ns + j] != '-' && w.got[ns + j] != w.exp[ns + j]) parts.push_back(k.outs[j] + " is " + w.got[ns + j] + " instead of " + w.exp[ns + j]);
		res.verdict = 1;
		res.summary = "Doesn't match: in state " + stateText(w.state) + inText(w.in) + " (row " + std::to_string(w.row) + "), " + list(parts) + ".";
		return res;
	}
	// Rows never reached.
	std::vector<std::string> states;
	int left = 0;
	for (int q = 0; q < R; q++)
		if (!done[q]) { left++; if (std::find(states.begin(), states.end(), k.rows[q].st) == states.end()) states.push_back(k.rows[q].st); }
	if (left > 0 && !stopped)
		res.notes.push_back({ Warning, (states.size() == 1 ? "State " + states[0] + " was" : "States " + list(states) + " were") +
		                               " never reached: clocking from the start doesn't get there, and no switch sets " +
		                               (states.size() == 1 ? "it." : "them.") });
	if (stopped) res.notes.push_back({ Warning, outOfTries ? "The check stopped after starting the circuit again " + std::to_string(maxPowerOns) + " times."
	                                                       : std::string("The check stopped after 1000 clock pulses.") });
	res.verdict = 0;
	if (R == 0) res.summary = "Matches, but every value was a don't-care, so nothing was really checked.";
	else if (left > 0)
		res.summary = "Matches on every row it reached, but " + plural(left, "row", "rows") + " (" + (states.size() == 1 ? "state " : "states ") +
		              list(states) + ") " + (left == 1 ? "wasn't reached, so it wasn't checked." : "weren't reached, so they weren't checked.");
	else res.summary = "Matches: every row of the state table does what was asked for" + (dc > 0 ? dontCares(dc) : std::string("."));
	return res;
}

}  // namespace

struct CLCheckJob {
	bool hasCircuit = false;
	std::string circuit;   // saved; "" when the key can't be checked anyway
	int page = 0;
	std::string key, names;
	std::atomic<bool> cancelled{ false };
};

extern "C" {

CLCheckJob* cl_check_clocked_prepare(CLDocument* doc, int page, const char* key, const char* names) {
	auto* job = new CLCheckJob();
	job->hasCircuit = doc != nullptr;
	job->page = page;
	job->key = key ? key : "";
	job->names = names ? names : "";
	// Saved only when there's something to run (the document isn't touched).
	const Key k = readKey(job->key);
	if (doc && (k.kind != CL_KEY_EMPTY || k.options) && k.error.empty()) job->circuit = clSaveText(doc);
	return job;
}

void cl_check_job_cancel(CLCheckJob* job) { if (job) job->cancelled = true; }

CLCheck* cl_check_clocked_run(CLCheckJob* job) {
	auto* c = new CLCheck();
	if (!job) return c;
	// The page's switches and lights are read from a copy too: gate ids and
	// places survive the save, so its ports address every later copy.
	CLDocument* copy = nullptr;
	if (job->hasCircuit && !job->circuit.empty()) {
		copy = clOpenText(job->circuit.data(), (long)job->circuit.size(), nullptr, 0, false);
		if (!copy) copy = cl_document_new();   // made from a save of its own: doesn't happen
	}
	{
		Sim sim;
		sim.text = job->circuit;
		sim.cancel = &job->cancelled;
		Checker(copy, sim, *c).run(job->key, job->page, readNames(job->names.c_str()));
	}
	if (copy) cl_document_close(copy);
	return c;
}

void cl_check_job_free(CLCheckJob* job) { delete job; }

bool cl_check_clocked_page(CLDocument* doc, int page) {
	if (!doc || !doc->page(page)) return false;
	bool clocked = false;
	for (size_t p = 0; p < doc->pages.size(); p++)
		for (auto& g : *doc->page((int)p)->getGateList()) {
			if (g.second->getLogicType() == "CLOCK") clocked = true;
			if ((int)p != page) continue;
			if (clSequentialType(g.second->getLibraryGateName())) clocked = true;
			// A selection of switches and lights is for a truth table of them.
			if (g.second->isSelected() && (dynamic_cast<guiGateTOGGLE*>(g.second) || dynamic_cast<guiGateLED*>(g.second))) return false;
		}
	int switches = 0, lights = 0;
	for (const Port& p : portsOf(doc, page)) {
		(p.input ? switches : lights)++;
		if (p.input && (key(p.name) == "clk" || key(p.name) == "clock")) clocked = true;
	}
	return lights > 0 && (switches == 0 || switches > 8) && clocked;
}

CLCheck* cl_check_clocked(CLDocument* doc, int page, const char* key, const char* names) {
	CLCheckJob* job = cl_check_clocked_prepare(doc, page, key, names);
	CLCheck* c = cl_check_clocked_run(job);
	cl_check_job_free(job);
	return c;
}

}  // extern "C"
