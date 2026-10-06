// A truth table behind the C interface (TruthTable.cpp makes them, Check.cpp
// compares them). Not part of the interface the apps see.

#ifndef CL_MAC_TRUTHTABLEIMPL_H
#define CL_MAC_TRUTHTABLEIMPL_H

#include "CedarCore.h"

#include <string>
#include <vector>

struct CLTruthTable {
	std::vector<std::string> names;          // inputs, then outputs
	int inputs = 0;
	std::vector<std::vector<char>> rows;
	bool sequential = false;
	int unsettled = 0;
};

// A library gate name for a part that remembers (a clock, a flip-flop, a
// latch, a register, a counter, memory).
bool clSequentialType(const std::string& libraryGateName);

#endif  // CL_MAC_TRUTHTABLEIMPL_H
