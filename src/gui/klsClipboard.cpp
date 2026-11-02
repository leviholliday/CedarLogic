/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsClipboard: handles copy and paste of blocks
*****************************************************************************/

#include <algorithm>
#include <memory>
#include "klsClipboard.h"
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <unordered_map>   // removed .h  KAS

#ifndef CL_NO_WX
#include "MainApp.h"
#endif
#include "commands.h"
#include "cmdSerialize.h"
#include "cmdRegistry.h"
#include "GUICanvas.h"
#include "GUICircuit.h"
#include "guiGate.h"
#include "guiWire.h"
#ifndef CL_NO_WX
#include "wx/clipbrd.h"
#include "wx/dataobj.h"

class clipboardCtx {
public:
	bool valid;

	clipboardCtx() {
		valid = wxTheClipboard->Open();
	}

	virtual ~clipboardCtx() {
		if (valid) wxTheClipboard->Close();
	}
};
#else
// The native front end moves the text on and off the system clipboard itself
// and says whether Shift is down.
bool klsClipboard::shiftHeld = false;
string klsClipboard::rewrittenText;
#endif

// Paste-specific tweak (Colin Broberg, 10/6/16): when a single gate is pasted,
// auto-increment a trailing number on its JUNCTION_ID param so repeated pastes
// don't collide, and rewrite the clipboard so the next paste keeps counting.
// Skipped when the block holds more than one gate, or when Shift is held.
// Mutates `line` (the setparams command text) in place.
//
// The number is counted up as text, so a name ending in more digits than an int
// holds (stoi threw, and the app ended) still counts on: "link99" -> "link100".
static bool bumpTrailingNumber(string &name) {
	size_t i = name.size();
	while (i > 0 && isdigit((unsigned char)name[i - 1])) i--;
	if (i == name.size()) return false;
	size_t k = name.size();
	while (k > i && name[k - 1] == '9') name[--k] = '0';
	if (k > i) name[k - 1]++;
	else name.insert(i, "1");
	return true;
}

static void autoIncrementJunctionId(string &line, const string &pasteText, bool rewriteClipboard) {
	// More than one creategate in the block, or Shift held -> leave it alone.
#ifdef CL_NO_WX
	const bool shift = klsClipboard::shiftHeld;
#else
	const bool shift = wxGetKeyState(WXK_SHIFT);
#endif
	if (pasteText.find("creategate", pasteText.find("creategate") + 1) != string::npos || shift) {
		return;
	}

	cmdser::SetParams d;
	if (!cmdser::parse(line, d)) return;
	auto junction = d.logicParams.find("JUNCTION_ID");
	if (junction == d.logicParams.end() || !bumpTrailingNumber(junction->second)) return;
	const string bumped = cmdser::emit(d);

	// The next paste keeps counting: the same line in the clipboard's text, bumped.
	const size_t at = pasteText.find(line);
	string newPasteText = pasteText;
	if (at != string::npos) newPasteText.replace(at, line.size(), bumped);
	line = bumped;
#ifdef CL_NO_WX
	if (rewriteClipboard && at != string::npos) klsClipboard::rewrittenText = newPasteText;
#else
	if (rewriteClipboard && at != string::npos) wxTheClipboard->AddData(new wxTextDataObject(newPasteText));
#endif
}

#ifndef CL_NO_WX
cmdPasteBlock* klsClipboard::pasteBlock( GUICircuit* gCircuit, GUICanvas* gCanvas ) {
	clipboardCtx clipboard;
	if ( !clipboard.valid || !wxTheClipboard->IsSupported(wxDF_UNICODETEXT) ) {
		return NULL;
	}
	wxTextDataObject text;
	if ( !wxTheClipboard->GetData(text) ) return NULL;
	// Still open: the JUNCTION_ID bump in pasteText may rewrite it.
	return pasteText( gCircuit, gCanvas, text.GetText().ToStdString(), true );
}
#endif

// A pasted copy of a part locked in place comes unlocked: drop LOCKED from
// the block's setparams lines (before the JUNCTION_ID bump, which reads the
// end of the text).
static string withoutLocks(const string& text) {
	if (text.find("LOCKED") == string::npos) return text;
	istringstream iss(text);
	string out, line;
	while (getline(iss, line, '\n')) {
		cmdser::SetParams d;
		if (cmdser::keyword(line) == "setparams" && cmdser::parse(line, d) && d.guiParams.erase("LOCKED"))
			line = cmdser::emit(d);
		out += line + "\n";
	}
	return out;
}

cmdPasteBlock* klsClipboard::pasteText( GUICircuit* gCircuit, GUICanvas* gCanvas, const string& rawText, bool useClipboard ) {
    vector < klsCommand* > cmdList;
    const string pasteText = withoutLocks(rawText);
    {
    	if (pasteText.find('\n',0) == string::npos) return NULL;
    	istringstream iss(pasteText);
    	string temp;
		
		TranslationMap gateids;
		TranslationMap wireids;
		// The gates and wires there before, to tell the pasted ones (the id
		// maps keep only the newest for an id a damaged text names twice).
		std::set<unsigned long> gatesBefore, wiresBefore;
		for (const auto &g : *gCanvas->getGateList()) gatesBefore.insert(g.first);
		for (const auto &w : *gCanvas->getWireList()) wiresBefore.insert(w.first);
    	while (getline( iss, temp, '\n' )) {
    		// The setparams line for a lone pasted gate gets its JUNCTION_ID
    		// bumped before the command is rebuilt (this also rewrites the
    		// clipboard so the next paste keeps counting).
    		if (cmdser::keyword(temp) == "setparams")
    			autoIncrementJunctionId(temp, pasteText, useClipboard);

    		// The registry picks the concrete command from the line's keyword.
    		// An unrecognized keyword ends the block, as the old chain did.
    		klsCommand* cg = cmd::fromLine(temp);
    		if (cg == NULL) break;
    		cmdList.push_back( cg );
    		cg->setPointers( gCircuit, gCanvas, gateids, wireids );
    		cg->Do();
    	}
		// The text may come from anywhere and be damaged. As steps of the paste
		// (so Redo does them again): a pasted wire left holding fewer than two
		// pins joins nothing and goes; one whose shape doesn't match the pins it
		// holds is routed fresh from them.
		std::vector<unsigned long> pasted;
		for (const auto &w : *gCanvas->getWireList())
			if (w.second != nullptr && !wiresBefore.count(w.first)) pasted.push_back(w.first);
		for (unsigned long id : pasted) {
			guiWire *wire = gCircuit->getWire(id);
			if (wire == nullptr) continue;
			if (wire->numConnections() < 2) {
				klsCommand *gone = new cmdDeleteWire(gCircuit, gCanvas, id);
				gone->Do();
				cmdList.push_back(gone);
				continue;
			}
			if (wire->isSoundShape()) continue;
			const map < long, wireSegment > shape = wire->getSegmentMap();
			wire->straightenRoute();
			cmdList.push_back(new cmdMoveWire(gCircuit, id, shape, wire->getSegmentMap()));
		}
		// What was pasted is selected, so it moves as one.
		gCanvas->unselectAllGates();
		gCanvas->unselectAllWires();
		for (const auto &g : *gCanvas->getGateList())
			if (g.second != nullptr && !gatesBefore.count(g.first)) g.second->select();
		for (const auto &w : *gCanvas->getWireList())
			if (w.second != nullptr && !wiresBefore.count(w.first)) w.second->select();
		gCircuit->oscopeSignalsChanged();
    }

	if (cmdList.size() > 0) return new cmdPasteBlock ( cmdList );
	return NULL;
}

#ifndef CL_NO_WX
void klsClipboard::copyBlock( GUICircuit* gCircuit, GUICanvas* gCanvas, vector < unsigned long > gates, vector < unsigned long > wires ) {
	const string text = serializeBlock( gCircuit, gCanvas, gates, wires );
	if (text.empty()) return;
	if (!wxTheClipboard->Open()) return;
	wxTheClipboard->AddData(new wxTextDataObject(text));
	wxTheClipboard->Close();
}
#endif

string klsClipboard::serializeBlock( GUICircuit* gCircuit, GUICanvas* gCanvas, vector < unsigned long > gates, vector < unsigned long > wires ) {
	if (gates.size() == 0) return "";
	ostringstream oss;
	klsCommand* cmdTemp;
	map < unsigned long, unsigned long > connectWireList;
	// Write strings to copy gates
	for (unsigned int i = 0; i < gates.size(); i++) {
		// generate list of wire connections
		guiGate* gGate = gCircuit->getGate(gates[i]);
		if (gGate == nullptr) continue;
		map < string, GLPoint2f > hotspotmap = gGate->getHotspotList();
		map < string, GLPoint2f >::iterator hsmapWalk = hotspotmap.begin();
		while (hsmapWalk != hotspotmap.end()) {
			if ( gGate->isConnected(hsmapWalk->first) )connectWireList[gGate->getConnection(hsmapWalk->first)->getID()]++;
			hsmapWalk++;
		}
		// Creation of a gate takes care of type, position, id; all other items are in params
		float x, y;
		gGate->getGLcoords(x,y);
		cmdTemp = new cmdCreateGate( gCanvas, gCircuit, gates[i], gGate->getLibraryGateName(), x, y);
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
		cmdTemp = new cmdSetParams( gCircuit, gates[i], paramSet(gGate->getAllGUIParams(), gGate->getAllLogicParams()) );
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
	}
	// For wires, only copy if more than one active connection, and trim shape
	vector < std::unique_ptr<guiWire> > copyWires;
	map < unsigned long, unsigned long >::iterator wireWalk = connectWireList.begin();
	while (wireWalk != connectWireList.end()) {
		if ( wireWalk->second < 2 ) { wireWalk++; continue; }
		guiWire* source = gCircuit->getWire(wireWalk->first);
		if (source == nullptr) { wireWalk++; continue; }
		auto wire = std::make_unique<guiWire>();
		wire->setCircuit(gCircuit); // so removeConnection/setSegmentMap can resolve gids to gates
		// Set the IDs
		wire->setIDs( source->getIDs() );
		// Shove all the connections
		vector < wireConnection > wireConns = source->getConnections();
		for (unsigned int i = 0; i < wireConns.size(); i++) wire->addConnection( gCircuit->getGate(wireConns[i].gid), wireConns[i].connection, true );
		// Now get the segment map copy
		wire->setSegmentMap( source->getSegmentMap() );
		// Now that we have a good copy of the wire object, we can trim the connections that we don't want to carry over
		for (unsigned int i = 0; i < wireConns.size(); i++) {
			bool found = false;
			for (unsigned int j = 0; j < gates.size() && !found; j++) if (gates[j] == wireConns[i].gid) found = true;
			if (found) continue; // we found this connection; don't trim it
			// get rid of it
			wire->removeConnection( wireConns[i].gid, wireConns[i].connection );
		}
		// And no segment may still end on a pin outside the copy (removeConnection
		// can leave one behind), or the paste has an end with no gate.
		{
			map < long, wireSegment > shape = wire->getSegmentMap();
			bool trimmed = false;
			for (auto &seg : shape) {
				vector < wireConnection > &conns = seg.second.connections;
				for (auto c = conns.begin(); c != conns.end(); ) {
					if (std::find(gates.begin(), gates.end(), c->gid) == gates.end()) { c = conns.erase(c); trimmed = true; }
					else ++c;
				}
			}
			if (trimmed && !shape.empty()) wire->setSegmentMap(shape);
		}
		// Wire should now have a completely valid shape to copy, shove it on the vector
		copyWires.push_back(std::move(wire));
		wireWalk++;
	}
	// Now actually generate copy of wire
	for (unsigned int i = 0; i < copyWires.size(); i++) {
		vector < wireConnection > wconns;
		// Two pins in one spot (a RAM's DATA_IN_n and DATA_OUT_n) are joined
		// together: connecting either connects both. Writing a connectwire for
		// each made the paste connect each pin twice, and the doubled records
		// later turned up as repeated (connect ...) lines in the saved file.
		for (const wireConnection &c : copyWires[i]->getConnections()) {
			guiGate *g = gCircuit->getGate(c.gid);
			const string pal = g != nullptr ? g->getHotspotPal(c.connection) : string();
			bool palWritten = false;
			for (const wireConnection &k : wconns)
				if (k.gid == c.gid && (k.connection == c.connection || (!pal.empty() && k.connection == pal))) palWritten = true;
			if (!palWritten) wconns.push_back(c);
		}
		// A wire needs two ends to be worth copying, and the wire has to still
		// be in the circuit for its bus ids to be readable. Neither was checked:
		// a wire left with fewer than two connections read past the end of the
		// list, and a missing wire was dereferenced through an accessor whose
		// whole job is to return null.
		guiWire *sourceWire = gCircuit->getWire(copyWires[i]->getID());
		if (wconns.size() < 2 || sourceWire == nullptr) continue;
		// now generate the connections - connections 1 and 2 must be passed to create the wire
		//	after which all connections may be done in succession.
		cmdConnectWire *conn1 = new cmdConnectWire(gCircuit, copyWires[i]->getID(), wconns[0].gid, wconns[0].connection);
		cmdConnectWire *conn2 = new cmdConnectWire(gCircuit, copyWires[i]->getID(), wconns[1].gid, wconns[1].connection);
		cmdTemp = new cmdCreateWire(gCanvas, gCircuit, sourceWire->getIDs(), conn1, conn2);
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
		for (unsigned int j = 2; j < wconns.size(); j++) {
			cmdTemp = new cmdConnectWire(gCircuit, copyWires[i]->getID(), wconns[j].gid, wconns[j].connection);
			oss << cmdTemp->toString() << endl;
			delete cmdTemp;
		}
		// now track the wire's shape:
		cmdTemp = new cmdMoveWire(gCircuit, copyWires[i]->getID(), copyWires[i]->getSegmentMap(), copyWires[i]->getSegmentMap());
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
	}
	return oss.str();
}
