// QR code encoder (see QrCodeGen.hpp).

#include "QrCodeGen.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace qrcodegen {

namespace {

// Indexed [ecc][version]; version 0 unused.
const int8_t kEccPerBlock[4][41] = {
	{ -1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28 },
	{ -1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ -1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
};
const int8_t kBlocks[4][41] = {
	{ -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25 },
	{ -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 },
	{ -1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 },
	{ -1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 },
};
const int kFormatBits[4] = { 1, 0, 3, 2 };   // L M Q H

int rawDataModules(int ver) {
	int result = (16 * ver + 128) * ver + 64;
	if (ver >= 2) {
		const int numAlign = ver / 7 + 2;
		result -= (25 * numAlign - 10) * numAlign - 55;
		if (ver >= 7) result -= 36;
	}
	return result;
}

int dataCodewords(int ver, int e) { return rawDataModules(ver) / 8 - kEccPerBlock[e][ver] * kBlocks[e][ver]; }

uint8_t gfMultiply(uint8_t x, uint8_t y) {
	int z = 0;
	for (int i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 7) * 0x11D);
		z ^= ((y >> i) & 1) * x;
	}
	return (uint8_t)z;
}

std::vector<uint8_t> rsDivisor(int degree) {
	std::vector<uint8_t> result((size_t)degree);
	result.back() = 1;
	uint8_t root = 1;
	for (int i = 0; i < degree; i++) {
		for (size_t j = 0; j < result.size(); j++) {
			result[j] = gfMultiply(result[j], root);
			if (j + 1 < result.size()) result[j] ^= result[j + 1];
		}
		root = gfMultiply(root, 0x02);
	}
	return result;
}

std::vector<uint8_t> rsRemainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& divisor) {
	std::vector<uint8_t> result(divisor.size());
	for (uint8_t b : data) {
		const uint8_t factor = b ^ result.at(0);
		result.erase(result.begin());
		result.push_back(0);
		for (size_t i = 0; i < result.size(); i++) result[i] ^= gfMultiply(divisor[i], factor);
	}
	return result;
}

struct Matrix {
	int size;
	std::vector<bool> m, fn;
	explicit Matrix(int s) : size(s), m((size_t)(s * s)), fn((size_t)(s * s)) {}
	bool get(int x, int y) const { return m[(size_t)(y * size + x)]; }
	void set(int x, int y, bool dark) { m[(size_t)(y * size + x)] = dark; }
	void setFn(int x, int y, bool dark) {
		set(x, y, dark);
		fn[(size_t)(y * size + x)] = true;
	}
	bool isFn(int x, int y) const { return fn[(size_t)(y * size + x)]; }
};

std::vector<int> alignmentPositions(int ver, int size) {
	if (ver == 1) return {};
	const int numAlign = ver / 7 + 2;
	const int step = (ver == 32) ? 26 : (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
	std::vector<int> result;
	for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step) result.insert(result.begin(), pos);
	result.insert(result.begin(), 6);
	return result;
}

void drawFormat(Matrix& q, int e, int mask) {
	const int data = kFormatBits[e] << 3 | mask;
	int rem = data;
	for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	const int bits = (data << 10 | rem) ^ 0x5412;
	auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
	const int size = q.size;
	for (int i = 0; i <= 5; i++) q.setFn(8, i, bit(i));
	q.setFn(8, 7, bit(6));
	q.setFn(8, 8, bit(7));
	q.setFn(7, 8, bit(8));
	for (int i = 9; i < 15; i++) q.setFn(14 - i, 8, bit(i));
	for (int i = 0; i < 8; i++) q.setFn(size - 1 - i, 8, bit(i));
	for (int i = 8; i < 15; i++) q.setFn(8, size - 15 + i, bit(i));
	q.setFn(8, size - 8, true);
}

void drawVersion(Matrix& q, int ver) {
	if (ver < 7) return;
	int rem = ver;
	for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	const long bits = (long)ver << 12 | rem;
	for (int i = 0; i < 18; i++) {
		const bool b = ((bits >> i) & 1) != 0;
		const int a = q.size - 11 + i % 3, c = i / 3;
		q.setFn(a, c, b);
		q.setFn(c, a, b);
	}
}

void drawFinder(Matrix& q, int x, int y) {
	for (int dy = -4; dy <= 4; dy++)
		for (int dx = -4; dx <= 4; dx++) {
			const int dist = std::max(std::abs(dx), std::abs(dy));
			const int xx = x + dx, yy = y + dy;
			if (0 <= xx && xx < q.size && 0 <= yy && yy < q.size) q.setFn(xx, yy, dist != 2 && dist != 4);
		}
}

void drawAlignment(Matrix& q, int x, int y) {
	for (int dy = -2; dy <= 2; dy++)
		for (int dx = -2; dx <= 2; dx++) q.setFn(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

void drawFunctionPatterns(Matrix& q, int ver, int e) {
	const int size = q.size;
	for (int i = 0; i < size; i++) {
		q.setFn(6, i, i % 2 == 0);
		q.setFn(i, 6, i % 2 == 0);
	}
	drawFinder(q, 3, 3);
	drawFinder(q, size - 4, 3);
	drawFinder(q, 3, size - 4);
	const std::vector<int> pos = alignmentPositions(ver, size);
	const size_t n = pos.size();
	for (size_t i = 0; i < n; i++)
		for (size_t j = 0; j < n; j++) {
			if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
			drawAlignment(q, pos[i], pos[j]);
		}
	drawFormat(q, e, 0);
	drawVersion(q, ver);
}

void drawCodewords(Matrix& q, const std::vector<uint8_t>& data) {
	const int size = q.size;
	size_t i = 0;
	for (int right = size - 1; right >= 1; right -= 2) {
		if (right == 6) right = 5;
		for (int vert = 0; vert < size; vert++) {
			for (int j = 0; j < 2; j++) {
				const int x = right - j;
				const bool upward = ((right + 1) & 2) == 0;
				const int y = upward ? size - 1 - vert : vert;
				if (!q.isFn(x, y) && i < data.size() * 8) {
					q.set(x, y, ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0);
					i++;
				}
			}
		}
	}
}

void applyMask(Matrix& q, int mask) {
	for (int y = 0; y < q.size; y++)
		for (int x = 0; x < q.size; x++) {
			bool invert;
			switch (mask) {
			case 0: invert = (x + y) % 2 == 0; break;
			case 1: invert = y % 2 == 0; break;
			case 2: invert = x % 3 == 0; break;
			case 3: invert = (x + y) % 3 == 0; break;
			case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
			case 5: invert = x * y % 2 + x * y % 3 == 0; break;
			case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
			default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
			}
			if (invert && !q.isFn(x, y)) q.set(x, y, !q.get(x, y));
		}
}

struct Penalty {
	int size;
	int countPatterns(const std::array<int, 7>& h) const {
		const int n = h[1];
		const bool core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
		return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) + (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
	}
	void addHistory(int run, std::array<int, 7>& h) const {
		if (h[0] == 0) run += size;   // the light border before the first run
		std::copy_backward(h.cbegin(), h.cend() - 1, h.end());
		h[0] = run;
	}
	int terminateAndCount(bool color, int run, std::array<int, 7>& h) const {
		if (color) {
			addHistory(run, h);
			run = 0;
		}
		run += size;
		addHistory(run, h);
		return countPatterns(h);
	}
};

long penaltyScore(const Matrix& q) {
	const int size = q.size;
	const Penalty p{ size };
	long result = 0;
	for (int pass = 0; pass < 2; pass++) {
		for (int a = 0; a < size; a++) {
			bool runColor = false;
			int run = 0;
			std::array<int, 7> h = {};
			for (int b = 0; b < size; b++) {
				const bool c = pass == 0 ? q.get(b, a) : q.get(a, b);
				if (c == runColor) {
					run++;
					if (run == 5) result += 3;
					else if (run > 5) result++;
				} else {
					p.addHistory(run, h);
					if (!runColor) result += p.countPatterns(h) * 40;
					runColor = c;
					run = 1;
				}
			}
			result += p.terminateAndCount(runColor, run, h) * 40;
		}
	}
	for (int y = 0; y < size - 1; y++)
		for (int x = 0; x < size - 1; x++) {
			const bool c = q.get(x, y);
			if (c == q.get(x + 1, y) && c == q.get(x, y + 1) && c == q.get(x + 1, y + 1)) result += 3;
		}
	long dark = 0;
	for (bool b : q.m) dark += b ? 1 : 0;
	const long total = (long)size * size;
	const long k = (std::labs(dark * 20 - total * 10) + total - 1) / total - 1;
	result += k * 10;
	return result;
}

}  // namespace

std::vector<bool> encodeBinary(const std::vector<uint8_t>& data, Ecc eccLevel, int& size) {
	size = 0;
	const int e = (int)eccLevel;
	int ver = 1;
	size_t usedBits = 0;
	for (;; ver++) {
		if (ver > 40) return {};
		const int countBits = ver <= 9 ? 8 : 16;
		if (data.size() >= ((size_t)1 << countBits)) continue;
		usedBits = 4 + (size_t)countBits + data.size() * 8;
		if (usedBits <= (size_t)dataCodewords(ver, e) * 8) break;
	}
	// The bit stream: byte mode, the count, the bytes, a terminator, padding.
	std::vector<bool> bits;
	auto append = [&](uint32_t v, int n) {
		for (int i = n - 1; i >= 0; i--) bits.push_back(((v >> i) & 1) != 0);
	};
	append(0x4, 4);
	append((uint32_t)data.size(), ver <= 9 ? 8 : 16);
	for (uint8_t b : data) append(b, 8);
	const size_t capacity = (size_t)dataCodewords(ver, e) * 8;
	append(0, (int)std::min<size_t>(4, capacity - bits.size()));
	append(0, (int)((8 - bits.size() % 8) % 8));
	for (uint8_t pad = 0xEC; bits.size() < capacity; pad ^= 0xEC ^ 0x11) append(pad, 8);
	std::vector<uint8_t> codewords(bits.size() / 8);
	for (size_t i = 0; i < bits.size(); i++)
		if (bits[i]) codewords[i >> 3] |= (uint8_t)(1 << (7 - (i & 7)));

	// Error correction, in blocks, interleaved.
	const int numBlocks = kBlocks[e][ver], blockEcc = kEccPerBlock[e][ver];
	const int rawCodewords = rawDataModules(ver) / 8;
	const int numShort = numBlocks - rawCodewords % numBlocks, shortLen = rawCodewords / numBlocks;
	std::vector<std::vector<uint8_t>> blocks;
	const std::vector<uint8_t> divisor = rsDivisor(blockEcc);
	for (int i = 0, k = 0; i < numBlocks; i++) {
		const int len = shortLen - blockEcc + (i < numShort ? 0 : 1);
		std::vector<uint8_t> dat(codewords.begin() + k, codewords.begin() + k + len);
		k += len;
		const std::vector<uint8_t> ecc = rsRemainder(dat, divisor);
		if (i < numShort) dat.push_back(0);
		dat.insert(dat.end(), ecc.begin(), ecc.end());
		blocks.push_back(std::move(dat));
	}
	std::vector<uint8_t> all;
	for (size_t i = 0; i < blocks.at(0).size(); i++)
		for (size_t j = 0; j < blocks.size(); j++)
			if (i != (size_t)(shortLen - blockEcc) || j >= (size_t)numShort) all.push_back(blocks[j][i]);

	// Modules, then the best mask.
	Matrix q(ver * 4 + 17);
	drawFunctionPatterns(q, ver, e);
	drawCodewords(q, all);
	int best = 0;
	long minPenalty = -1;
	for (int mask = 0; mask < 8; mask++) {
		applyMask(q, mask);
		drawFormat(q, e, mask);
		const long p = penaltyScore(q);
		if (minPenalty < 0 || p < minPenalty) {
			best = mask;
			minPenalty = p;
		}
		applyMask(q, mask);   // undo
	}
	applyMask(q, best);
	drawFormat(q, e, best);
	size = q.size;
	return q.m;
}

}  // namespace qrcodegen
