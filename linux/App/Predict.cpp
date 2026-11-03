// Predict, then reveal (the Mac's Predict.swift): in Simulation View every
// light and display is covered; students guess what each will show (a
// click cycles ?, 0, 1, or a display's digit; 0-9 and A-F type one; Tab
// moves along), then Reveal rings each right, wrong or not guessed and
// says the score. The covers and the rings are drawn by the core
// (cl_simview_draw_page with predict, cl_simview_draw_predict). While
// covered the circuit can move on (a switch, Step, Step Clock) and the
// guesses stay; once revealed, the next such move starts a new round, as
// does another page.

#include "Window.h"
#include "Canvas.h"

#include <algorithm>

std::vector<CLSimLight> CircuitWindow::predictLights() const {
	std::vector<CLSimLight> buf(256);
	const int n = cl_simview_lights(doc, currentPage(), buf.data(), (int)buf.size());
	buf.resize((size_t)std::max(0, std::min(n, (int)buf.size())));
	return buf;
}

bool CircuitWindow::predictCovers() const { return simViewOn && predict.on && !predict.revealed; }

void CircuitWindow::predictNewRound() {
	predict.revealed = false;
	predict.guesses.clear();
	predict.answers.clear();
	predict.page = currentPage();
}

std::vector<CLPredictMark> CircuitWindow::predictMarks() {
	std::vector<CLPredictMark> out;
	if (!simViewOn || !predict.on) return out;
	if (predict.page != currentPage()) { predictNewRound(); predict.focus = -1; }
	for (const CLSimLight& l : predictLights()) {
		CLPredictMark m{};
		m.gate = l.gate;
		auto g = predict.guesses.find(l.gate);
		m.guess = g == predict.guesses.end() ? -1 : g->second;
		m.focused = predict.focus == l.gate;
		if (!predict.revealed) m.mark = CL_PREDICT_COVERED;
		else {
			auto a = predict.answers.find(l.gate);
			if (a == predict.answers.end() || a->second < 0) m.mark = CL_PREDICT_UNCLEAR;
			else if (g == predict.guesses.end()) m.mark = CL_PREDICT_UNGUESSED;
			else m.mark = g->second == a->second ? CL_PREDICT_RIGHT : CL_PREDICT_WRONG;
		}
		out.push_back(m);
	}
	return out;
}

void CircuitWindow::togglePredict() {
	if (!simViewOn && !predict.on) {
		note("Predict works in Simulation View: open it first (Simulation ▸ Simulation View).");
		return;
	}
	predict.on = !predict.on;
	predict.focus = -1;
	predictNewRound();
	note(predict.on ? "Predict is on. The lights are covered: click each to guess, then Simulation ▸ Reveal (or Return)."
	                : "Predict is off.");
	updateActions();
	redraw();
}

void CircuitWindow::revealOrCoverAgain() {
	if (!predict.on || !simViewOn) return;
	if (predict.revealed) {
		predictNewRound();
		note("Covered again. Make your guesses.");
	} else {
		predict.answers.clear();
		for (const CLSimLight& l : predictLights()) predict.answers[l.gate] = l.value;
		predict.revealed = true;
		int right = 0, total = 0;
		for (const auto& a : predict.answers) {
			if (a.second < 0) continue;
			total++;
			auto g = predict.guesses.find(a.first);
			if (g != predict.guesses.end() && g->second == a.second) right++;
		}
		note(total == 0 ? std::string("No clear values to score.") : format("%d of %d right.", right, total));
	}
	updateActions();
	redraw();
}

void CircuitWindow::circuitAdvancedByUser() {
	if (!predict.on || !predict.revealed) return;
	predictNewRound();
	updateActions();
	redraw();
}

bool CircuitWindow::predictTap(double wx, double wy) {
	if (!predictCovers()) return false;
	int digit = 0;
	const long gate = cl_simview_light_at(doc, currentPage(), wx, wy, &digit);
	if (gate < 0) return false;
	predict.focus = gate;
	for (const CLSimLight& l : predictLights()) {
		if (l.gate != gate) continue;
		auto g = predict.guesses.find(gate);
		if (l.digits == 0) {
			if (g == predict.guesses.end()) predict.guesses[gate] = 0;
			else if (g->second == 0) g->second = 1;
			else predict.guesses.erase(g);
		} else if (g == predict.guesses.end()) {
			predict.guesses[gate] = 0;
		} else {
			const int n = l.digits, shift = 4 * (n - 1 - std::min(std::max(digit, 0), n - 1));
			const int d = (g->second >> shift) & 0xF;
			if (n == 1 && d == 15) predict.guesses.erase(g);
			else g->second = (g->second & ~(0xF << shift)) | (((d + 1) & 0xF) << shift);
		}
	}
	redraw();
	return true;
}

bool CircuitWindow::predictKey(const GdkEventKey* e) {
	if (!predictCovers()) {
		// Revealed: Return covers the lights again for the next round.
		if (simViewOn && predict.on && (e->keyval == GDK_KEY_Return || e->keyval == GDK_KEY_KP_Enter)) { revealOrCoverAgain(); return true; }
		return false;
	}
	const std::vector<CLSimLight> lights = predictLights();
	if (lights.empty()) return false;
	auto at = std::find_if(lights.begin(), lights.end(), [&](const CLSimLight& l) { return l.gate == predict.focus; });
	const guint k = e->keyval;
	if (k == GDK_KEY_Return || k == GDK_KEY_KP_Enter) { revealOrCoverAgain(); return true; }
	if (k == GDK_KEY_Tab || k == GDK_KEY_ISO_Left_Tab) {
		const int n = (int)lights.size();
		const int delta = k == GDK_KEY_ISO_Left_Tab || (e->state & GDK_SHIFT_MASK) ? -1 : 1;
		const int i = at == lights.end() ? (delta > 0 ? 0 : n - 1) : (((int)(at - lights.begin()) + delta) % n + n) % n;
		predict.focus = lights[(size_t)i].gate;
		redraw();
		return true;
	}
	if (k == GDK_KEY_BackSpace || k == GDK_KEY_Delete) {
		if (at != lights.end()) predict.guesses.erase(at->gate);
		redraw();
		return true;
	}
	const gunichar c = gdk_keyval_to_unicode(k);
	const int digit = g_unichar_xdigit_value(c);
	if (digit < 0) return false;
	if (at == lights.end()) { predict.focus = lights[0].gate; at = lights.begin(); }
	if (at->digits == 0) {
		if (digit > 1) return false;
		predict.guesses[at->gate] = digit;
	} else {
		const int mask = (1 << (4 * at->digits)) - 1;
		auto g = predict.guesses.find(at->gate);
		predict.guesses[at->gate] = (((g == predict.guesses.end() ? 0 : g->second) << 4) | digit) & mask;
	}
	redraw();
	return true;
}
