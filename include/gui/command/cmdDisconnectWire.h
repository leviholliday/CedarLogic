
#pragma once
#include "klsCommand.h"
#include "../wireSegment.h"
#include <map>

// cmdDisconnectWire - disconnects a wire from a gate hotspot
class cmdDisconnectWire : public klsCommand {
public:
	cmdDisconnectWire(GUICircuit* gCircuit, IDType wid, IDType gid,
		const std::string &hotspot, bool noCalcShape = false);

	bool Do();

	bool Undo();

	virtual std::string toString() const override;

private:
	IDType gateId;
	IDType wireId;
	std::string hotspot;
	bool noCalcShape;
	// The wire's shape before the pin came off, so Undo puts it back as it was
	// rather than routing it afresh.
	std::map<long, wireSegment> shapeBefore;
};