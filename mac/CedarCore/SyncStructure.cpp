// The structure digest (SYNC.md 2.4): a circuit file -> its gates, the
// parameters a person sets and its connections, the same text for v3 and
// v1/v2 XML. Built on the apps' own readers (format/), without migration.

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS   // snprintf/sscanf/localtime/getenv are used with care
#endif

#include "SyncInternal.h"

#include "circuit_file_io.hpp"
#include "legacy_cdl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace clsync {

namespace {

std::string esc(const std::string& s) {
	std::string o;
	o.reserve(s.size());
	for (unsigned char b : s) {
		if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '_' || b == '.' || b == ':' ||
		    b == '-') {
			o += (char)b;
		} else {
			char buf[4];
			snprintf(buf, sizeof buf, "%%%02X", b);
			o += buf;
		}
	}
	return o;
}

// In thousandths, rounded half up, as a decimal integer. Like Python's
// math.floor, an infinite value is an error (the text is then "unparsed").
std::string milli(double v) {
	const double f = std::floor(v * 1000 + 0.5);
	if (!std::isfinite(f)) throw std::overflow_error("not a finite number");
	if (std::fabs(f) < 9e18) return std::to_string((long long)f);
	char buf[400];
	snprintf(buf, sizeof buf, "%.0f", f);   // exact: f is a whole number
	return buf;
}

bool digitAt(const std::string& v, size_t i) { return i < v.size() && v[i] >= '0' && v[i] <= '9'; }

// [+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?
bool isNum(const std::string& v) {
	size_t i = 0;
	const size_t n = v.size();
	if (i < n && (v[i] == '+' || v[i] == '-')) i++;
	size_t d = 0;
	while (digitAt(v, i)) { i++; d++; }
	if (i < n && v[i] == '.') {
		i++;
		while (digitAt(v, i)) { i++; d++; }
	}
	if (d == 0) return false;
	if (i < n && (v[i] == 'e' || v[i] == 'E')) {
		i++;
		if (i < n && (v[i] == '+' || v[i] == '-')) i++;
		size_t e = 0;
		while (digitAt(v, i)) { i++; e++; }
		if (!e) return false;
	}
	return i == n;
}

double toDouble(const std::string& v) {
	std::istringstream in(v[0] == '+' ? v.substr(1) : v);
	in.imbue(std::locale::classic());
	double d = 0;
	in >> d;
	if (in.fail()) {   // out of range
		const bool neg = v[0] == '-';
		// A tiny exponent underflows to 0; a huge one overflows.
		const size_t e = v.find_first_of("eE");
		const bool tiny = e != std::string::npos && v.size() > e + 1 && v[e + 1] == '-';
		return tiny ? 0.0 : (neg ? -HUGE_VAL : HUGE_VAL);
	}
	return d;
}

std::string canon(const std::string& v0) {
	const std::string v = trimAscii(v0);
	if (!isNum(v)) return v;
	return "#" + milli(toDouble(v));
}

}  // namespace

std::string structureText(const std::string& cdl, const GateDefaults& defaults) {
	const std::string t = normalizeCdl(cdl);
	cl::CircuitFile cf;
	try {
		const cl::SourceFormat f = cl::detectFormat(t);
		if (f == cl::SourceFormat::SexprV3) cf = cl::readCircuitFile(t);
		else if (f == cl::SourceFormat::XmlV1 || f == cl::SourceFormat::XmlV2) cf = cl::readLegacyCdl(t);
		else return "cedarlogic-structure/1 unparsed\n" + t;
	} catch (const std::exception&) {
		return "cedarlogic-structure/1 unparsed\n" + t;
	}
	std::vector<std::string> lines;
	try {
	for (const cl::Page& pg : cf.pages) {
		std::map<std::string, std::string> tok;
		for (const cl::GateInstance& g : pg.gates) {
			const std::string gt = esc(g.libName) + "@" + milli(g.at.x) + "," + milli(g.at.y);
			tok.emplace(g.uuid, gt);
			std::set<std::string> ps;
			for (const cl::Param& p : g.params) {
				if (!p.gui && (p.name == "OUTPUT_NUM" || p.name == "CURRENT_VALUE")) continue;
				if (p.gui && (p.name == "angle" || p.name.find("_BOX") != std::string::npos)) continue;
				const std::string cv = canon(p.value);
				if (defaults) {
					const std::string d = defaults(g.libName, p.gui, p.name);
					if (d != "\x01" && canon(d) == cv) continue;
				}
				ps.insert(std::string(p.gui ? "g:" : "l:") + esc(p.name) + "=" + esc(cv));
			}
			const double fa = std::floor(g.angle + 0.5);
			if (!std::isfinite(fa)) throw std::overflow_error("angle");
			long long a = (long long)std::fmod(fa, 360.0);
			if (a < 0) a += 360;
			std::string line = "G " + std::to_string(pg.index) + " " + gt + " " + std::to_string(a);
			for (const std::string& p : ps) line += " " + p;
			lines.push_back(line);
		}
		for (const cl::WireInstance& w : pg.wires) {
			std::set<std::string> es;
			for (const cl::WireSegment& s : w.segments)
				for (const cl::WireConn& cn : s.connects) {
					auto t2 = tok.find(cn.gateUuid);
					es.insert((t2 != tok.end() ? t2->second : "?" + esc(cn.gateUuid)) + "." + esc(cn.pin));
				}
			if (es.empty()) continue;
			std::string line = "W " + std::to_string(pg.index);
			for (const std::string& e : es) line += " " + e;
			lines.push_back(line);
		}
	}
	} catch (const std::exception&) {
		return "cedarlogic-structure/1 unparsed\n" + t;
	}
	std::sort(lines.begin(), lines.end());
	std::string out = "cedarlogic-structure/1\n";
	for (const std::string& l : lines) out += l + "\n";
	return out;
}

}  // namespace clsync
