// Runs sharelink::selfTest (App/ShareCodec.cpp) for share-check.sh:
//   share_check [<a .cdl> [<a link someone else made of it> [<file to write this one's link to>]]]
#include "../App/ShareCodec.h"

#include <fstream>
#include <iostream>
#include <sstream>

static std::string slurp(const char* path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) { std::cerr << "can't read " << path << "\n"; exit(2); }
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

int main(int argc, char** argv) {
	std::string report, made;
	std::string given = argc > 2 ? slurp(argv[2]) : "";
	while (!given.empty() && (given.back() == '\n' || given.back() == '\r' || given.back() == ' ')) given.pop_back();
	const bool ok = sharelink::selfTest(report, argc > 1 ? slurp(argv[1]) : "", &made, given);
	std::cout << report;
	if (argc > 3) std::ofstream(argv[3], std::ios::binary) << made;
	return ok ? 0 : 1;
}
