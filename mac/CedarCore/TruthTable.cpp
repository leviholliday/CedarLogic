// Truth tables: a port of MainFrame::OnTruthTable (src/gui/MainFrame.cpp) --
// the same inputs (switches), outputs (lights), column order, names from
// nearby text labels, and every combination tried with the circuit settled,
// then the switches put back.

#include "DocumentImpl.h"
#include "guiGate.h"
#include "guiWire.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

struct CLTruthTable {
	std::vector<std::string> names;          // inputs, then outputs
	int inputs = 0;
	std::vector<std::vector<char>> rows;
	bool sequential = false;
	int unsettled = 0;
};

namespace {

void setError(char* error, int len, const std::string& msg) {
	if (error == nullptr || len <= 0) return;
	std::strncpy(error, msg.c_str(), (size_t)len - 1);
	error[len - 1] = 0;
}

std::pair<float, float> pos(guiGate* g) { float x, y; g->getGLcoords(x, y); return { x, y }; }

}  // namespace

extern "C" {

CLTruthTable* cl_truth_table(CLDocument* doc, int pageIndex, char* error, int errorLen) {
	GUICanvas* page = doc ? doc->page(pageIndex) : nullptr;
	if (page == nullptr) return nullptr;
	auto* gates = page->getGateList();

	// The selected switches and lights if any are selected, else the page's.
	bool useSelection = false;
	for (auto& g : *gates)
		if (g.second->isSelected() && (dynamic_cast<guiGateTOGGLE*>(g.second) || dynamic_cast<guiGateLED*>(g.second)))
			useSelection = true;
	std::vector<guiGate*> ins, outs;
	bool sequential = false;
	for (auto& g : *gates) {
		const std::string type = g.second->getLibraryGateName();
		for (const char* seq : { "CLOCK", "FF", "LATCH", "REGISTER", "COUNTER", "RAM", "ROM" })
			if (type.find(seq) != std::string::npos) sequential = true;
		if (useSelection && !g.second->isSelected()) continue;
		if (dynamic_cast<guiGateTOGGLE*>(g.second)) ins.push_back(g.second);
		else if (dynamic_cast<guiGateLED*>(g.second)) outs.push_back(g.second);
	}
	if (ins.empty() || outs.empty()) {
		setError(error, errorLen, std::string("A truth table needs at least one switch (an input) and one light (an output)") +
		         (useSelection ? " in the selection." : " on this page."));
		return nullptr;
	}
	if (ins.size() > 8) {
		setError(error, errorLen, "That's " + std::to_string(ins.size()) + " switches. Select up to 8 switches "
		         "(and the lights you care about) and try again.");
		return nullptr;
	}

	// Columns in drawing order: top to bottom, then left to right. The first
	// input is the most significant bit.
	auto byPlace = [](guiGate* a, guiGate* b) {
		const auto pa = pos(a), pb = pos(b);
		return pa.second != pb.second ? pa.second > pb.second : pa.first < pb.first;
	};
	std::sort(ins.begin(), ins.end(), byPlace);
	std::sort(outs.begin(), outs.end(), byPlace);

	// Names: the nearest unused short text label, if one is close; otherwise
	// A, B, C... for inputs and Y (or Y1, Y2...) for outputs.
	std::vector<guiGate*> labels;
	for (auto& g : *gates)
		if (dynamic_cast<guiLabel*>(g.second) && !g.second->getGUIParam("LABEL_TEXT").empty()) labels.push_back(g.second);
	std::vector<bool> used(labels.size(), false);
	auto nameFor = [&](guiGate* port, const std::string& fallback) {
		const auto p = pos(port);
		int best = -1;
		float bestD = 8.0f;
		for (size_t i = 0; i < labels.size(); i++) {
			if (used[i]) continue;
			const std::string text = labels[i]->getGUIParam("LABEL_TEXT");
			if (text.size() > 16) continue;
			const auto q = pos(labels[i]);
			const float d = std::hypot(q.first - p.first, q.second - p.second);
			if (d < bestD) { bestD = d; best = (int)i; }
		}
		if (best < 0) return fallback;
		used[best] = true;
		return labels[best]->getGUIParam("LABEL_TEXT");
	};
	auto* tt = new CLTruthTable();
	tt->sequential = sequential;
	tt->inputs = (int)ins.size();
	for (size_t i = 0; i < ins.size(); i++) tt->names.push_back(nameFor(ins[i], std::string(1, (char)('A' + i))));
	for (size_t i = 0; i < outs.size(); i++)
		tt->names.push_back(nameFor(outs[i], outs.size() == 1 ? "Y" : "Y" + std::to_string(i + 1)));

	auto setSwitch = [&](guiGate* g, const std::string& v) {
		g->setLogicParam("OUTPUT_NUM", v);
		doc->circuit.sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM,
			new klsMessage::Message_SET_GATE_PARAM(g->getID(), "OUTPUT_NUM", v)));
	};
	auto readLight = [](guiGate* g) {
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
	};

	std::vector<std::string> original;
	for (guiGate* g : ins) original.push_back(g->getLogicParam("OUTPUT_NUM"));
	const int n = (int)ins.size();
	for (int r = 0; r < (1 << n); r++) {
		std::vector<char> row;
		for (int i = 0; i < n; i++) {
			const bool bit = (r >> (n - 1 - i)) & 1;
			setSwitch(ins[i], bit ? "1" : "0");
			row.push_back(bit ? '1' : '0');
		}
		if (!doc->sim->settle()) tt->unsettled++;
		for (guiGate* g : outs) row.push_back(readLight(g));
		tt->rows.push_back(row);
	}
	for (size_t i = 0; i < ins.size(); i++) setSwitch(ins[i], original[i].empty() ? "0" : original[i]);
	doc->sim->settle();
	return tt;
}

void cl_tt_free(CLTruthTable* tt) { delete tt; }
int cl_tt_inputs(const CLTruthTable* tt) { return tt ? tt->inputs : 0; }
int cl_tt_columns(const CLTruthTable* tt) { return tt ? (int)tt->names.size() : 0; }
int cl_tt_rows(const CLTruthTable* tt) { return tt ? (int)tt->rows.size() : 0; }
const char* cl_tt_name(const CLTruthTable* tt, int col) {
	return (tt && col >= 0 && col < (int)tt->names.size()) ? tt->names[col].c_str() : "";
}
char cl_tt_cell(const CLTruthTable* tt, int row, int col) {
	if (!tt || row < 0 || row >= (int)tt->rows.size() || col < 0 || col >= (int)tt->rows[row].size()) return ' ';
	return tt->rows[row][col];
}
bool cl_tt_sequential(const CLTruthTable* tt) { return tt && tt->sequential; }
int cl_tt_unsettled(const CLTruthTable* tt) { return tt ? tt->unsettled : 0; }

}  // extern "C"
