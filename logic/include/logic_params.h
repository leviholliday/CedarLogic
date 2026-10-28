// logic_params.h: limits on the gate parameters a circuit file or a paste can set.

#ifndef LOGIC_PARAMS_H
#define LOGIC_PARAMS_H

#include <cerrno>
#include <cstdlib>
#include <string>

// The widest bus a gate may declare (INPUT_BITS, OUTPUT_BITS, DATA_BITS) and
// the widest memory address (ADDRESS_BITS). The library's own parts reach 16.
// A file or a paste can say anything, and a width of millions declared that
// many pins one by one (a hang, then the memory gone); -1 read as 2^64 - 1.
const long MAX_BUS_BITS = 32;

// The value to use for a gate parameter: a width held to 0..MAX_BUS_BITS,
// anything else as given. A width that isn't a number is left alone (the gate
// keeps the one it has, as it always did).
inline std::string boundedGateParameter( const std::string& name, const std::string& value ) {
	if( name != "INPUT_BITS" && name != "OUTPUT_BITS" && name != "DATA_BITS" && name != "ADDRESS_BITS" )
		return value;
	errno = 0;
	char* end = nullptr;
	const long long bits = std::strtoll( value.c_str(), &end, 10 );
	if( end == value.c_str() ) return value;
	if( bits < 0 ) return "0";
	if( bits > MAX_BUS_BITS || errno == ERANGE ) return std::to_string( MAX_BUS_BITS );
	return value;
}

#endif // LOGIC_PARAMS_H
