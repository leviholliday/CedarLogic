// Raw deflate, compressing and decompressing (Deflate.h). RFC 1951 is the
// whole of the format; the layout here follows its sections.

#include "Deflate.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace deflate {

namespace {

const int kMinMatch = 3, kMaxMatch = 258;
const int kWindow = 32768;                // matches reach back at most 32767 bytes
const int kEndOfBlock = 256;

// Length codes 257..285 and distance codes 0..29: the first value each
// stands for and the extra bits that follow it.
const int kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const int kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const int kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                            8193, 12289, 16385, 24577 };
const int kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
// The order a dynamic block's code-length code lengths are sent in.
const int kClOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

// ---- Compressing ---------------------------------------------------------------

class BitWriter {
public:
	explicit BitWriter(std::string& s) : out(s) {}
	// `bits` bits of `value`, the low one first.
	void put(uint32_t value, int bits) {
		acc |= (uint64_t)value << n;
		n += bits;
		while (n >= 8) {
			out.push_back((char)(acc & 0xFF));
			acc >>= 8;
			n -= 8;
		}
	}
	void alignToByte() {
		if (n > 0) out.push_back((char)(acc & 0xFF));
		acc = 0;
		n = 0;
	}

private:
	std::string& out;
	uint64_t acc = 0;
	int n = 0;
};

// What a block is made of: a literal byte (dist 0), or a match.
struct Sym {
	uint16_t value;   // the byte, or the match's length
	uint16_t dist;    // 0 for a literal, else how far back the match is
};

int lengthIndex(int length) {
	if (length >= kMaxMatch) return 28;
	int i = 27;
	while (kLenBase[i] > length) i--;
	return i;
}

int distIndex(int dist) {
	int i = 29;
	while (kDistBase[i] > dist) i--;
	return i;
}

uint32_t reversed(uint32_t code, int bits) {
	uint32_t r = 0;
	for (int i = 0; i < bits; i++) {
		r = (r << 1) | (code & 1);
		code >>= 1;
	}
	return r;
}

// Code lengths, none over `limit`, for a Huffman code of these frequencies.
// There are always at least two codes (a block with one is still decodable),
// and a code that comes out too deep has its counts halved and is made
// again, which shortens it.
void codeLengths(std::vector<uint32_t> freq, int limit, uint8_t* lens) {
	const int n = (int)freq.size();
	int used = 0;
	for (int i = 0; i < n; i++) used += freq[i] > 0;
	for (int i = 0; i < n && used < 2; i++)
		if (freq[i] == 0) { freq[i] = 1; used++; }
	struct Node {
		uint64_t weight;
		int left, right;   // children, or -1 for a leaf
		int symbol;
	};
	for (;;) {
		std::vector<Node> nodes;
		typedef std::pair<uint64_t, int> Item;   // weight, then the order made (so ties fall the same way every time)
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
		for (int i = 0; i < n; i++) {
			if (freq[i] == 0) continue;
			nodes.push_back({ freq[i], -1, -1, i });
			heap.push(Item(freq[i], (int)nodes.size() - 1));
		}
		while (heap.size() > 1) {
			const Item a = heap.top();
			heap.pop();
			const Item b = heap.top();
			heap.pop();
			nodes.push_back({ a.first + b.first, a.second, b.second, -1 });
			heap.push(Item(a.first + b.first, (int)nodes.size() - 1));
		}
		// Parents were made after their children: walk down from the root.
		std::vector<int> depth(nodes.size(), 0);
		int deepest = 0;
		for (int i = (int)nodes.size() - 1; i >= 0; i--) {
			if (nodes[i].left >= 0) {
				depth[nodes[i].left] = depth[i] + 1;
				depth[nodes[i].right] = depth[i] + 1;
			} else {
				deepest = std::max(deepest, depth[i]);
			}
		}
		if (deepest > limit) {
			for (int i = 0; i < n; i++)
				if (freq[i]) freq[i] = (freq[i] + 1) >> 1;
			continue;
		}
		for (int i = 0; i < n; i++) lens[i] = 0;
		for (size_t i = 0; i < nodes.size(); i++)
			if (nodes[i].left < 0) lens[nodes[i].symbol] = (uint8_t)depth[i];
		return;
	}
}

// The canonical codes for these lengths, bit-reversed ready for the writer.
void canonicalCodes(const uint8_t* lens, int n, uint16_t* codes) {
	int count[16] = {}, next[16] = {};
	for (int i = 0; i < n; i++) count[lens[i]]++;
	count[0] = 0;
	int code = 0;
	for (int bits = 1; bits <= 15; bits++) {
		code = (code + count[bits - 1]) << 1;
		next[bits] = code;
	}
	for (int i = 0; i < n; i++)
		if (lens[i]) codes[i] = (uint16_t)reversed((uint32_t)next[lens[i]]++, lens[i]);
}

// A dynamic block's code lengths, run-length coded: symbols 0..15 are a
// length, 16 repeats the last one 3-6 times, 17 and 18 are runs of zeros.
struct ClToken {
	uint8_t symbol;
	uint8_t extra;
	uint8_t extraBits;
};

std::vector<ClToken> runLengthCoded(const std::vector<uint8_t>& seq) {
	std::vector<ClToken> out;
	size_t i = 0;
	while (i < seq.size()) {
		const uint8_t v = seq[i];
		size_t run = 1;
		while (i + run < seq.size() && seq[i + run] == v) run++;
		if (v == 0) {
			size_t left = run;
			while (left >= 11) {
				const size_t r = std::min<size_t>(left, 138);
				out.push_back({ 18, (uint8_t)(r - 11), 7 });
				left -= r;
			}
			if (left >= 3) {
				out.push_back({ 17, (uint8_t)(left - 3), 3 });
				left = 0;
			}
			for (; left > 0; left--) out.push_back({ 0, 0, 0 });
		} else {
			out.push_back({ v, 0, 0 });
			size_t left = run - 1;
			while (left >= 3) {
				const size_t r = std::min<size_t>(left, 6);
				out.push_back({ 16, (uint8_t)(r - 3), 2 });
				left -= r;
			}
			for (; left > 0; left--) out.push_back({ v, 0, 0 });
		}
		i += run;
	}
	return out;
}

void putSymbols(BitWriter& bw, const std::vector<Sym>& syms, const uint16_t* litCode, const uint8_t* litLen, const uint16_t* distCode,
                const uint8_t* distLen) {
	for (const Sym& s : syms) {
		if (s.dist == 0) {
			bw.put(litCode[s.value], litLen[s.value]);
			continue;
		}
		const int li = lengthIndex(s.value), di = distIndex(s.dist);
		bw.put(litCode[257 + li], litLen[257 + li]);
		if (kLenExtra[li]) bw.put((uint32_t)(s.value - kLenBase[li]), kLenExtra[li]);
		bw.put(distCode[di], distLen[di]);
		if (kDistExtra[di]) bw.put((uint32_t)(s.dist - kDistBase[di]), kDistExtra[di]);
	}
	bw.put(litCode[kEndOfBlock], litLen[kEndOfBlock]);
}

// One block of the input's bytes [start, end), made of `syms`: stored,
// fixed-code or dynamic-code, whichever is the smallest.
void writeBlock(BitWriter& bw, const std::string& in, size_t start, size_t end, const std::vector<Sym>& syms, bool last) {
	std::vector<uint32_t> litFreq(286, 0), distFreq(30, 0);
	uint64_t extraBits = 0;
	for (const Sym& s : syms) {
		if (s.dist == 0) {
			litFreq[s.value]++;
			continue;
		}
		const int li = lengthIndex(s.value), di = distIndex(s.dist);
		litFreq[257 + li]++;
		distFreq[di]++;
		extraBits += (uint64_t)kLenExtra[li] + kDistExtra[di];
	}
	litFreq[kEndOfBlock] = 1;

	// The fixed code.
	uint8_t fixedLit[288], fixedDist[30];
	for (int i = 0; i < 288; i++) fixedLit[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
	for (int i = 0; i < 30; i++) fixedDist[i] = 5;
	uint64_t fixedBits = 3 + extraBits;
	for (int i = 0; i < 286; i++) fixedBits += (uint64_t)litFreq[i] * fixedLit[i];
	for (int i = 0; i < 30; i++) fixedBits += (uint64_t)distFreq[i] * 5;

	// The dynamic code, and what it costs to describe.
	uint8_t litLen[286], distLen[30], clLen[19];
	codeLengths(litFreq, 15, litLen);
	codeLengths(distFreq, 15, distLen);
	int hlit = 286, hdist = 30;
	while (hlit > 257 && litLen[hlit - 1] == 0) hlit--;
	while (hdist > 1 && distLen[hdist - 1] == 0) hdist--;
	std::vector<uint8_t> seq(litLen, litLen + hlit);
	seq.insert(seq.end(), distLen, distLen + hdist);
	const std::vector<ClToken> tokens = runLengthCoded(seq);
	std::vector<uint32_t> clFreq(19, 0);
	for (const ClToken& t : tokens) clFreq[t.symbol]++;
	codeLengths(clFreq, 7, clLen);
	int hclen = 19;
	while (hclen > 4 && clLen[kClOrder[hclen - 1]] == 0) hclen--;
	uint64_t dynBits = 3 + 14 + 3 * (uint64_t)hclen + extraBits;
	for (const ClToken& t : tokens) dynBits += (uint64_t)clLen[t.symbol] + t.extraBits;
	for (int i = 0; i < hlit; i++) dynBits += (uint64_t)litFreq[i] * litLen[i];
	for (int i = 0; i < hdist; i++) dynBits += (uint64_t)distFreq[i] * distLen[i];

	// Stored: 3 bits, up to 7 to the next byte, 32 of lengths, then the bytes
	// themselves, for every 65535 of them.
	const size_t span = end - start;
	const uint64_t storedBits = span == 0 ? ~0ULL : (uint64_t)((span + 65534) / 65535) * (3 + 7 + 32) + 8 * (uint64_t)span;

	if (storedBits < fixedBits && storedBits < dynBits) {
		size_t at = start;
		do {
			const size_t chunk = std::min<size_t>(end - at, 65535);
			const bool finalChunk = at + chunk == end;
			bw.put(last && finalChunk ? 1 : 0, 1);
			bw.put(0, 2);
			bw.alignToByte();
			bw.put((uint32_t)chunk, 16);
			bw.put((uint32_t)(~chunk & 0xFFFF), 16);
			for (size_t i = 0; i < chunk; i++) bw.put((uint8_t)in[at + i], 8);
			at += chunk;
		} while (at < end);
		return;
	}
	uint16_t litCode[288] = {}, distCode[30] = {};
	if (fixedBits <= dynBits) {
		bw.put(last ? 1 : 0, 1);
		bw.put(1, 2);
		canonicalCodes(fixedLit, 288, litCode);
		canonicalCodes(fixedDist, 30, distCode);
		putSymbols(bw, syms, litCode, fixedLit, distCode, fixedDist);
		return;
	}
	bw.put(last ? 1 : 0, 1);
	bw.put(2, 2);
	bw.put((uint32_t)(hlit - 257), 5);
	bw.put((uint32_t)(hdist - 1), 5);
	bw.put((uint32_t)(hclen - 4), 4);
	for (int i = 0; i < hclen; i++) bw.put(clLen[kClOrder[i]], 3);
	uint16_t clCode[19] = {};
	canonicalCodes(clLen, 19, clCode);
	for (const ClToken& t : tokens) {
		bw.put(clCode[t.symbol], clLen[t.symbol]);
		if (t.extraBits) bw.put(t.extra, t.extraBits);
	}
	canonicalCodes(litLen, 286, litCode);
	canonicalCodes(distLen, 30, distCode);
	putSymbols(bw, syms, litCode, litLen, distCode, distLen);
}

// Finds matches: the positions seen so far are chained by a hash of the three
// bytes at each.
class Matcher {
public:
	Matcher(const uint8_t* data, size_t size) : d(data), n(size), head(1 << 15, -1), prev(kWindow, -1) {
		maxChain = n <= (1u << 20) ? 4096 : 96;   // a long input gets a quicker search
	}
	// Puts position p (needing three bytes from it) in its chain; the chain's
	// previous newest, or -1.
	int64_t insert(size_t p) {
		const uint32_t h = (((uint32_t)d[p] | (uint32_t)d[p + 1] << 8 | (uint32_t)d[p + 2] << 16) * 2654435761u) >> 17;
		const int64_t old = head[h];
		prev[p & (kWindow - 1)] = old;
		head[h] = (int64_t)p;
		return old;
	}
	// The longest match at p longer than `have` bytes, along the chain from
	// `cand`; its length (0 if none is longer) and distance.
	int longest(size_t p, int64_t cand, int have, int& dist) const {
		const size_t maxLen = std::min<size_t>(kMaxMatch, n - p);
		if (maxLen <= (size_t)have) return 0;
		int best = have, chain = have >= 32 ? maxChain >> 2 : maxChain;
		const int64_t limit = (int64_t)p - (kWindow - 1);
		const uint8_t* a = d + p;
		while (cand >= 0 && cand >= limit && chain-- > 0) {
			const uint8_t* b = d + cand;
			if (b[best] == a[best] && b[0] == a[0]) {
				size_t len = 0;
				while (len < maxLen && a[len] == b[len]) len++;
				if ((int)len > best) {
					best = (int)len;
					dist = (int)((int64_t)p - cand);
					if (len == maxLen) break;
				}
			}
			const int64_t next = prev[cand & (kWindow - 1)];
			if (next >= cand) break;   // (an entry another position has reused)
			cand = next;
		}
		return best > have ? best : 0;
	}

private:
	const uint8_t* d;
	size_t n;
	int maxChain;
	std::vector<int64_t> head, prev;
};

// ---- Decompressing --------------------------------------------------------------

class BitReader {
public:
	BitReader(const uint8_t* data, size_t size) : d(data), n(size) {}
	// The next `need` bits (up to 16), low first; 0, and `bad`, past the end.
	uint32_t get(int need) {
		while (have < need) {
			if (pos >= n) {
				bad = true;
				return 0;
			}
			acc |= (uint32_t)d[pos++] << have;
			have += 8;
		}
		const uint32_t v = acc & ((1u << need) - 1);
		acc >>= need;
		have -= need;
		return v;
	}
	void dropToByte() {
		acc = 0;
		have = 0;
	}
	const uint8_t* d;
	size_t n, pos = 0;
	bool bad = false;

private:
	uint32_t acc = 0;
	int have = 0;
};

// A canonical Huffman code, as the decoder walks it: how many codes there are
// of each length, and the symbols in code order.
struct Huffman {
	uint16_t count[16];
	uint16_t symbol[320];
};

// 0 for a complete code, 1 incomplete, -1 more codes than the lengths allow.
int buildHuffman(Huffman& h, const uint8_t* lens, int n) {
	for (int i = 0; i < 16; i++) h.count[i] = 0;
	for (int i = 0; i < n; i++) h.count[lens[i]]++;
	int left = 1;
	for (int len = 1; len <= 15; len++) {
		left <<= 1;
		left -= h.count[len];
		if (left < 0) return -1;
	}
	uint16_t offs[16];
	offs[1] = 0;
	for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h.count[len]);
	for (int i = 0; i < n; i++)
		if (lens[i]) h.symbol[offs[lens[i]]++] = (uint16_t)i;
	return left > 0 ? 1 : 0;
}

// What a code that isn't complete may be, as zlib allows: no codes at all (a
// block with no matches has no distances), or a single code one bit long.
bool usable(const Huffman& h, int status) {
	if (status < 0) return false;
	if (status == 0) return true;
	int used = 0;
	for (int len = 1; len <= 15; len++) used += h.count[len];
	return used == 0 || (used == 1 && h.count[1] == 1);
}

int decodeSymbol(BitReader& r, const Huffman& h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= 15; len++) {
		code |= (int)r.get(1);
		if (r.bad) return -1;
		const int count = h.count[len];
		if (code - count < first) return h.symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

// The codes of a block, to its end-of-block. False if damaged; `tooBig` if it
// would pass `limit`.
bool inflateCodes(BitReader& r, std::string& out, size_t limit, bool& tooBig, const Huffman& lit, const Huffman& dist) {
	for (;;) {
		int sym = decodeSymbol(r, lit);
		if (sym < 0) return false;
		if (sym < 256) {
			if (out.size() >= limit) {
				tooBig = true;
				return false;
			}
			out.push_back((char)sym);
			continue;
		}
		if (sym == kEndOfBlock) return true;
		sym -= 257;
		if (sym >= 29) return false;
		const int length = kLenBase[sym] + (int)r.get(kLenExtra[sym]);
		const int ds = decodeSymbol(r, dist);
		if (ds < 0 || ds >= 30) return false;
		const size_t distance = (size_t)kDistBase[ds] + r.get(kDistExtra[ds]);
		if (r.bad || distance > out.size()) return false;
		if (out.size() + (size_t)length > limit) {
			tooBig = true;
			return false;
		}
		const size_t at = out.size();
		out.resize(at + (size_t)length);
		char* o = &out[0];
		for (int i = 0; i < length; i++) o[at + i] = o[at - distance + i];
	}
}

bool inflateStored(BitReader& r, std::string& out, size_t limit, bool& tooBig) {
	r.dropToByte();
	if (r.pos + 4 > r.n) return false;
	const size_t len = (size_t)r.d[r.pos] | (size_t)r.d[r.pos + 1] << 8;
	const size_t inverse = (size_t)r.d[r.pos + 2] | (size_t)r.d[r.pos + 3] << 8;
	r.pos += 4;
	if (len != (~inverse & 0xFFFF) || r.pos + len > r.n) return false;
	if (out.size() + len > limit) {
		tooBig = true;
		return false;
	}
	out.append((const char*)r.d + r.pos, len);
	r.pos += len;
	return true;
}

bool inflateDynamic(BitReader& r, std::string& out, size_t limit, bool& tooBig) {
	const int nlen = (int)r.get(5) + 257, ndist = (int)r.get(5) + 1, ncode = (int)r.get(4) + 4;
	if (r.bad || nlen > 286 || ndist > 30) return false;
	uint8_t lens[286 + 30] = {};
	for (int i = 0; i < ncode; i++) lens[kClOrder[i]] = (uint8_t)r.get(3);
	if (r.bad) return false;
	Huffman cl;
	if (buildHuffman(cl, lens, 19) != 0) return false;   // (this code must be complete)
	uint8_t lengths[286 + 30] = {};
	int at = 0;
	while (at < nlen + ndist) {
		const int sym = decodeSymbol(r, cl);
		if (sym < 0) return false;
		if (sym < 16) {
			lengths[at++] = (uint8_t)sym;
			continue;
		}
		uint8_t value = 0;
		int repeat;
		if (sym == 16) {
			if (at == 0) return false;
			value = lengths[at - 1];
			repeat = 3 + (int)r.get(2);
		} else if (sym == 17) {
			repeat = 3 + (int)r.get(3);
		} else {
			repeat = 11 + (int)r.get(7);
		}
		if (r.bad || at + repeat > nlen + ndist) return false;
		while (repeat-- > 0) lengths[at++] = value;
	}
	if (lengths[kEndOfBlock] == 0) return false;   // (a block must be able to end)
	Huffman lit, dist;
	if (!usable(lit, buildHuffman(lit, lengths, nlen)) || !usable(dist, buildHuffman(dist, lengths + nlen, ndist))) return false;
	return inflateCodes(r, out, limit, tooBig, lit, dist);
}

}  // namespace

std::string compress(const std::string& in) {
	std::string out;
	BitWriter bw(out);
	const uint8_t* d = (const uint8_t*)in.data();
	const size_t n = in.size();
	const size_t kBlockSymbols = 1 << 15;
	Matcher m(d, n);
	std::vector<Sym> syms;
	size_t blockStart = 0, covered = 0;   // the block's first byte, and how many bytes the symbols so far cover
	// zlib's lazy matching: a match isn't taken until the next position has
	// had a chance to do better.
	size_t p = 0;
	int prevLen = kMinMatch - 1, prevDist = 0;
	bool pending = false;   // the byte before p, which no symbol has covered yet
	while (p < n) {
		int64_t cand = -1;
		if (p + kMinMatch <= n) cand = m.insert(p);
		int curLen = kMinMatch - 1, curDist = 0;
		if (cand >= 0 && prevLen < kMaxMatch) {
			const int found = m.longest(p, cand, prevLen, curDist);
			if (found > 0) curLen = found;
		}
		// A far match of three bytes costs more than the three bytes.
		if (curLen == kMinMatch && curDist > 4096) curLen = kMinMatch - 1;
		if (prevLen >= kMinMatch && curLen <= prevLen) {
			syms.push_back({ (uint16_t)prevLen, (uint16_t)prevDist });
			covered += (size_t)prevLen;
			const size_t next = p - 1 + (size_t)prevLen;
			for (size_t k = p + 1; k < next; k++)
				if (k + kMinMatch <= n) m.insert(k);
			p = next;
			prevLen = kMinMatch - 1;
			pending = false;
		} else {
			if (pending) {
				syms.push_back({ (uint16_t)d[p - 1], 0 });
				covered++;
			}
			pending = true;
			prevLen = curLen;
			prevDist = curDist;
			p++;
		}
		if (syms.size() >= kBlockSymbols && blockStart + covered < n) {
			writeBlock(bw, in, blockStart, blockStart + covered, syms, false);
			blockStart += covered;
			covered = 0;
			syms.clear();
		}
	}
	if (pending) {
		syms.push_back({ (uint16_t)d[n - 1], 0 });
		covered++;
	}
	writeBlock(bw, in, blockStart, blockStart + covered, syms, true);
	bw.alignToByte();
	return out;
}

bool decompress(const std::string& in, std::string& out, size_t limit, bool* tooBigOut) {
	out.clear();
	bool tooBig = false;
	BitReader r((const uint8_t*)in.data(), in.size());
	bool ok = true, last = false;
	// The fixed codes (blocks of type 1).
	static Huffman fixedLit, fixedDist;
	static const bool made = [] {
		uint8_t lens[288], dists[32];
		for (int i = 0; i < 288; i++) lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
		for (int i = 0; i < 32; i++) dists[i] = 5;
		buildHuffman(fixedLit, lens, 288);
		buildHuffman(fixedDist, dists, 32);
		return true;
	}();
	(void)made;
	while (ok && !last) {
		last = r.get(1) != 0;
		const uint32_t type = r.get(2);
		if (r.bad) ok = false;
		else if (type == 0) ok = inflateStored(r, out, limit, tooBig);
		else if (type == 1) ok = inflateCodes(r, out, limit, tooBig, fixedLit, fixedDist);
		else if (type == 2) ok = inflateDynamic(r, out, limit, tooBig);
		else ok = false;
	}
	if (!ok) out.clear();
	if (tooBigOut) *tooBigOut = tooBig;
	return ok;
}

}  // namespace deflate
