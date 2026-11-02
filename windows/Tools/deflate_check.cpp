// Runs App/Deflate.cpp for deflate-check.sh (which tests it against python's
// zlib). Reads requests from stdin, answers on stdout; each is
//   u32 mode (1 compress, 2 decompress), u32 limit (decompress), u32 length, bytes
// and its answer
//   u32 status (0 done, 1 refused, 2 refused as too big), u32 length, bytes
// (little-endian), until stdin ends.
#include "../App/Deflate.h"

#include <cstdint>
#include <cstdio>
#include <string>

static bool readAll(void* p, size_t n) { return n == 0 || fread(p, 1, n, stdin) == n; }

int main() {
	for (;;) {
		uint32_t head[3];
		if (fread(head, 4, 3, stdin) != 3) break;
		std::string in(head[2], '\0');
		if (!readAll(&in[0], in.size())) return 1;
		std::string out;
		uint32_t status = 0;
		if (head[0] == 1) {
			out = deflate::compress(in);
		} else {
			bool tooBig = false;
			status = deflate::decompress(in, out, head[1], &tooBig) ? 0 : tooBig ? 2 : 1;
		}
		const uint32_t reply[2] = { status, (uint32_t)out.size() };
		fwrite(reply, 4, 2, stdout);
		fwrite(out.data(), 1, out.size(), stdout);
		fflush(stdout);
	}
	return 0;
}
