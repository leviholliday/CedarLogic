// For mac/Tools/sync-selftest.sh: prints the QR modules of each argument
// (QrCodeGen, error correction M) as "size" then size rows of 0/1, for
// sync-qr-decode.swift to read back with Core Image.
#include "QrCodeGen.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
	for (int a = 1; a < argc; a++) {
		const std::string t = argv[a];
		int size = 0;
		const std::vector<bool> m = qrcodegen::encodeBinary(std::vector<uint8_t>(t.begin(), t.end()), qrcodegen::Ecc::Medium, size);
		printf("%d\n", size);
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) putchar(m[(size_t)(y * size + x)] ? '1' : '0');
			putchar('\n');
		}
	}
	return 0;
}
