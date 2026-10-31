#include "cmdSerialize.h"

#include <sstream>

namespace cmdser {

std::string emit(const CreateGate &c) {
	std::ostringstream oss;
	oss << "creategate " << c.gid << " " << c.gateType << " " << c.x << " " << c.y;
	return oss.str();
}

std::string emit(const ConnectWire &c) {
	std::ostringstream oss;
	oss << "connectwire " << c.wireId << ' ' << c.gateId << ' ' << c.hotspot;
	return oss.str();
}

std::string emit(const MoveGate &c) {
	std::ostringstream oss;
	oss << "movegate " << c.gid << " " << c.startX << " " << c.startY << " "
	    << c.endX << " " << c.endY;
	return oss.str();
}

std::string emit(const DisconnectWire &c) {
	std::ostringstream oss;
	oss << "disconnectwire " << c.wireId << " " << c.gateId << " " << c.hotspot;
	return oss.str();
}

std::string emit(const CreateWire &c) {
	std::ostringstream oss;
	oss << "createwire ";
	// Each id is followed by a space; the two connectwire clauses follow.
	for (IDType id : c.wireIds) oss << id << ' ';
	oss << emit(c.conn1) << ' ' << emit(c.conn2);
	return oss.str();
}

std::string keyword(const std::string &line) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	return kw;
}

bool parse(const std::string &line, CreateGate &out) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	if (kw != "creategate") return false;
	iss >> out.gid;
	// A library name may contain spaces ("3x8 Decoder Chip"), and emit() writes
	// it as is, so the last two words are x and y and everything between them
	// is the name. Reading the name as one word made such a paste create
	// nothing, and the next pasted part was given the same id.
	std::vector<std::string> words;
	std::string w;
	while (iss >> w) words.push_back(w);
	if (words.size() < 3) {
		std::istringstream rest(line);
		rest >> kw >> out.gid >> out.gateType >> out.x >> out.y;
		return true;
	}
	out.gateType = words[0];
	for (size_t i = 1; i + 2 < words.size(); i++) out.gateType += " " + words[i];
	std::istringstream(words[words.size() - 2]) >> out.x;
	std::istringstream(words[words.size() - 1]) >> out.y;
	return true;
}

bool parse(const std::string &line, ConnectWire &out) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	if (kw != "connectwire") return false;
	iss >> out.wireId >> out.gateId >> out.hotspot;
	return true;
}

bool parse(const std::string &line, CreateWire &out) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	if (kw != "createwire") return false;
	// Read wire ids until the extraction fails on the first "connectwire" token,
	// then clear the failbit and read the two connectwire clauses. This mirrors
	// the original cmdCreateWire string constructor exactly.
	IDType id;
	while (iss >> id) out.wireIds.push_back(id);
	iss.clear();
	std::string cwkw;
	iss >> cwkw >> out.conn1.wireId >> out.conn1.gateId >> out.conn1.hotspot;
	iss >> cwkw >> out.conn2.wireId >> out.conn2.gateId >> out.conn2.hotspot;
	return true;
}

// A value is written up to its tab and the line up to its newline, so those
// can't appear in it as they are: a label with two lines cut the paste in half
// and the half-line left threw out_of_range, which ended the app. A backslash
// starts an escape (\\ \t \n \r); any other backslash is taken as it is, as
// it was in blocks copied before escapes existed.
static std::string escapeValue(const std::string &v) {
	std::string out;
	out.reserve(v.size());
	for (char c : v) {
		switch (c) {
		case '\\': out += "\\\\"; break;
		case '\t': out += "\\t"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		default: out += c;
		}
	}
	return out;
}

static std::string unescapeValue(const std::string &v) {
	std::string out;
	out.reserve(v.size());
	for (size_t i = 0; i < v.size(); i++) {
		if (v[i] == '\\' && i + 1 < v.size()) {
			const char n = v[i + 1];
			if (n == '\\' || n == 't' || n == 'n' || n == 'r') {
				out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : '\\';
				i++;
				continue;
			}
		}
		out += v[i];
	}
	return out;
}

std::string emit(const SetParams &c) {
	std::ostringstream oss;
	oss << "setparams " << c.gid << " " << c.guiParams.size() << ","
	    << c.logicParams.size() << " ";
	for (const auto &p : c.guiParams) oss << p.first << " " << escapeValue(p.second) << "\t";
	for (const auto &p : c.logicParams) oss << p.first << " " << escapeValue(p.second) << "\t";
	return oss.str();
}

bool parse(const std::string &line, SetParams &out) {
	std::istringstream iss(line);
	std::string kw;
	char comma;
	unsigned long numGui = 0, numLogic = 0;
	iss >> kw;
	if (kw != "setparams") return false;
	iss >> out.gid >> numGui >> comma >> numLogic;
	// A value can contain spaces, so read the name with >> but the value up to
	// its tab; the value carries a leading space from the "name value" join,
	// which is dropped. The counts come from the text, which may be damaged:
	// stop at the end of the line, not after however many it claims.
	auto readParam = [&iss](std::map<std::string, std::string> &into) {
		std::string name, val;
		if (!(iss >> name)) return false;
		std::getline(iss, val, '\t');
		if (!val.empty() && val[0] == ' ') val.erase(0, 1);
		into[name] = unescapeValue(val);
		return true;
	};
	for (unsigned long i = 0; i < numGui && readParam(out.guiParams); i++) {}
	for (unsigned long i = 0; i < numLogic && readParam(out.logicParams); i++) {}
	return true;
}

std::string emit(const MoveWire &c) {
	std::ostringstream oss;
	oss << "movewire " << c.wid << " ";
	for (const MoveWireSeg &s : c.segments) {
		oss << (s.vertical ? "vsegment " : "hsegment ");
		oss << s.id << " ";
		oss << s.bx << "," << s.by << "," << s.ex << "," << s.ey << " ";
		for (const auto &conn : s.connections)
			oss << "connection " << conn.first << " " << conn.second << " ";
		for (const auto &isect : s.intersects)
			oss << "isect " << isect.first << " " << isect.second << " ";
	}
	oss << " done ";
	return oss.str();
}

bool parse(const std::string &line, MoveWire &out) {
	std::istringstream iss(line);
	std::string temp;
	char dump;
	iss >> temp;
	if (temp != "movewire") return false;
	iss >> out.wid >> temp;
	// Each record is read only while the line lasts: at its end >> leaves
	// temp as it was, and a line cut after "connection" looped for ever.
	while (iss && (temp == "vsegment" || temp == "hsegment")) {
		MoveWireSeg s;
		s.vertical = (temp == "vsegment");
		iss >> s.id >> s.bx >> dump >> s.by >> dump >> s.ex >> dump >> s.ey;
		if (!(iss >> temp)) temp.clear();
		while (temp == "connection") {
			unsigned long gid = 0;
			std::string name;
			if (!(iss >> gid >> name)) { temp.clear(); break; }
			s.connections.push_back({ gid, name });
			if (!(iss >> temp)) temp.clear();
		}
		while (temp == "isect") {
			float key = 0;
			long sid = 0;
			if (!(iss >> key >> sid)) { temp.clear(); break; }
			s.intersects.push_back({ key, sid });
			if (!(iss >> temp)) temp.clear();
		}
		out.segments.push_back(s);
	}
	return true;
}

} // namespace cmdser
