// The oscilloscope's recording and the C interface to read it. Samples the
// same thing the wx oscope does (OscopeCanvas::UpdateData): for each TO label,
// the first bit of the wire on its pin, or UNKNOWN when nothing is connected.

#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"

#include <algorithm>
#include <map>

namespace {
const size_t kMaxSamples = 60000;   // about 25 minutes at the default 25 ms a step
const size_t kTrim = 10000;         // dropped from the front when full
const unsigned char kNoData = 255;  // before a signal existed
}

void CLDocument::recordScope() {
	// The set of TO labels changes rarely; check it only when the gates do.
	if (scope.gateVersion != circuit.getGateListVersion()) {
		scope.gateVersion = circuit.getGateListVersion();
		std::vector<std::string> names;
		for (auto& g : circuit.gates())
			if (g.second && g.second->getGUIType() == "TO") {
				const std::string n = g.second->getLogicParam("JUNCTION_ID");
				if (!n.empty()) names.push_back(n);
			}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
		if (names != scope.names) {
			std::vector<std::vector<unsigned char>> samples;
			for (const std::string& n : names) {
				auto it = std::find(scope.names.begin(), scope.names.end(), n);
				samples.push_back(it != scope.names.end()
					? scope.samples[it - scope.names.begin()]
					: std::vector<unsigned char>(scope.length, kNoData));
			}
			scope.names = std::move(names);
			scope.samples = std::move(samples);
		}
	}
	// Which label feeds each name (the first one, when several share it).
	std::map<std::string, guiGate*> toGate;
	for (auto& g : circuit.gates())
		if (g.second && g.second->getGUIType() == "TO") {
			const std::string n = g.second->getLogicParam("JUNCTION_ID");
			if (!toGate.count(n)) toGate[n] = g.second.get();
		}
	for (size_t i = 0; i < scope.names.size(); i++) {
		unsigned char v = UNKNOWN;
		auto it = toGate.find(scope.names[i]);
		if (it != toGate.end()) {
			auto hs = it->second->getHotspotList();
			if (!hs.empty() && it->second->isConnected(hs.begin()->first)) {
				const std::vector<StateType>& st = it->second->getConnection(hs.begin()->first)->getState();
				if (!st.empty()) v = st[0];
			}
		}
		scope.samples[i].push_back(v);
	}
	scope.length++;
	if (scope.length > kMaxSamples) {
		for (auto& s : scope.samples) s.erase(s.begin(), s.begin() + kTrim);
		scope.length -= kTrim;
		scope.firstStep += kTrim;
	}
}

extern "C" {

int cl_scope_signal_count(const CLDocument* doc) { return doc ? (int)doc->scope.names.size() : 0; }

const char* cl_scope_signal(const CLDocument* doc, int index) {
	if (!doc || index < 0 || index >= (int)doc->scope.names.size()) return "";
	return doc->scope.names[index].c_str();
}

long long cl_scope_length(const CLDocument* doc) { return doc ? (long long)doc->scope.length : 0; }

long long cl_scope_first_step(const CLDocument* doc) { return doc ? (long long)doc->scope.firstStep : 0; }

int cl_scope_samples(const CLDocument* doc, int signal, long long from, int count, unsigned char* out) {
	if (!doc || !out || signal < 0 || signal >= (int)doc->scope.names.size() || count <= 0) return 0;
	const std::vector<unsigned char>& s = doc->scope.samples[signal];
	int n = 0;
	for (long long i = from; i < from + count; i++, n++)
		out[n] = (i >= 0 && i < (long long)s.size()) ? s[(size_t)i] : kNoData;
	return n;
}

void cl_scope_clear(CLDocument* doc) {
	if (!doc) return;
	for (auto& s : doc->scope.samples) s.clear();
	doc->scope.firstStep += doc->scope.length;
	doc->scope.length = 0;
}

}  // extern "C"
