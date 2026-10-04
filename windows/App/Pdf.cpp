// A small PDF writer (see Pdf.h): Deflate for the pictures, then the file's
// few objects.

#include "Pdf.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <queue>

namespace pdf {

namespace {

// ---- Deflate (RFC 1951) and zlib (RFC 1950) ---------------------------------------
// LZ77 with hash chains over the whole input, then blocks of up to 64K tokens
// each with Huffman codes made for it (or stored as it is when that's
// smaller). Not as tight as zlib's own, but plenty for pictures that are
// mostly white.

const int kWindow = 32768, kMaxMatch = 258, kChain = 24, kGoodMatch = 96;
const size_t kBlockTokens = 1 << 16;

const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
// The order the code length codes' own lengths are sent in.
const uint8_t kClOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

// A literal byte (dist 0), or a match of `value` bytes `dist` back.
struct Token { uint16_t value, dist; };

// Bits go out low bit first.
class BitWriter {
public:
	explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
	void put(uint32_t bits, int count) {
		acc |= (uint64_t)bits << used;
		used += count;
		while (used >= 8) {
			out.push_back((uint8_t)acc);
			acc >>= 8;
			used -= 8;
		}
	}
	void align() {
		if (used > 0) out.push_back((uint8_t)acc);
		acc = 0;
		used = 0;
	}

private:
	std::vector<uint8_t>& out;
	uint64_t acc = 0;
	int used = 0;
};

// The index in kLenBase for each length 3..258.
const uint8_t* lengthIndex() {
	static uint8_t table[kMaxMatch + 1];
	static bool made = false;
	if (!made) {
		for (int i = 0; i < 28; i++)
			for (int len = kLenBase[i]; len < kLenBase[i] + (1 << kLenExtra[i]) && len < kMaxMatch; len++) table[len] = (uint8_t)i;
		table[kMaxMatch] = 28;
		made = true;
	}
	return table;
}

// The distance code for a distance 1..32768.
int distanceCode(int dist) {
	if (dist <= 4) return dist - 1;
	int top = 0;   // the highest bit of dist - 1
	for (int v = dist - 1; v > 1; v >>= 1) top++;
	return 2 * top + (((dist - 1) >> (top - 1)) & 1);
}

// Huffman code lengths for these frequencies (0 for a symbol that isn't
// used), none longer than `limit`: when the tree is too deep the weights are
// flattened a little and it's built again. At least two symbols are used (the
// callers see to it), so the code is complete.
std::vector<uint8_t> codeLengths(const std::vector<uint32_t>& freq, int limit) {
	const int n = (int)freq.size();
	std::vector<uint8_t> lens(n, 0);
	std::vector<uint64_t> weight(freq.begin(), freq.end());
	struct Node { uint64_t weight; int left, right; };   // a leaf has left -1 and its symbol in right
	for (;;) {
		std::vector<Node> nodes;
		typedef std::pair<uint64_t, int> Item;           // weight, node: lighter first, then older
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
		for (int i = 0; i < n; i++) {
			if (weight[i] == 0) continue;
			nodes.push_back({ weight[i], -1, i });
			heap.push(Item(weight[i], (int)nodes.size() - 1));
		}
		if (nodes.size() == 1) {
			lens[nodes[0].right] = 1;
			return lens;
		}
		while (heap.size() > 1) {
			const Item a = heap.top();
			heap.pop();
			const Item b = heap.top();
			heap.pop();
			nodes.push_back({ a.first + b.first, a.second, b.second });
			heap.push(Item(nodes.back().weight, (int)nodes.size() - 1));
		}
		// Children come before their parent: from the root down.
		std::vector<int> depth(nodes.size(), 0);
		int deepest = 0;
		for (int i = (int)nodes.size() - 1; i >= 0; i--) {
			if (nodes[i].left < 0) {
				lens[nodes[i].right] = (uint8_t)std::min(depth[i], 255);
				deepest = std::max(deepest, depth[i]);
			} else {
				depth[nodes[i].left] = depth[nodes[i].right] = depth[i] + 1;
			}
		}
		if (deepest <= limit) return lens;
		for (uint64_t& w : weight) if (w > 0) w = (w + 1) / 2;
	}
}

// The codes for those lengths, bit-reversed so they go out low bit first.
std::vector<uint16_t> canonicalCodes(const std::vector<uint8_t>& lens) {
	int count[16] = {};
	for (uint8_t l : lens) count[l]++;
	count[0] = 0;
	uint32_t next[16] = {};
	uint32_t code = 0;
	for (int bits = 1; bits <= 15; bits++) {
		code = (code + count[bits - 1]) << 1;
		next[bits] = code;
	}
	std::vector<uint16_t> codes(lens.size(), 0);
	for (size_t i = 0; i < lens.size(); i++) {
		if (lens[i] == 0) continue;
		uint32_t c = next[lens[i]]++, reversed = 0;
		for (int b = 0; b < lens[i]; b++, c >>= 1) reversed = (reversed << 1) | (c & 1);
		codes[i] = (uint16_t)reversed;
	}
	return codes;
}

// At least two symbols used, so a code of them is complete.
void ensureTwo(std::vector<uint32_t>& freq) {
	int used = 0;
	for (uint32_t f : freq) if (f) used++;
	for (size_t i = 0; i < freq.size() && used < 2; i++) {
		if (freq[i] == 0) {
			freq[i] = 1;
			used++;
		}
	}
}

// Code lengths as the header sends them: a length, or 16 (repeat the last 3 to
// 6 times), 17 (3 to 10 zeros), 18 (11 to 138 zeros), with the extra bits.
struct LenItem { uint8_t symbol, extra; };
void runLengths(const std::vector<uint8_t>& lens, int count, std::vector<LenItem>& items) {
	for (int i = 0; i < count;) {
		int run = 1;
		while (i + run < count && lens[i + run] == lens[i]) run++;
		const uint8_t value = lens[i];
		i += run;
		if (value == 0) {
			while (run >= 11) {
				const int n = std::min(run, 138);
				items.push_back({ 18, (uint8_t)(n - 11) });
				run -= n;
			}
			if (run >= 3) {
				items.push_back({ 17, (uint8_t)(run - 3) });
				run = 0;
			}
			for (; run > 0; run--) items.push_back({ 0, 0 });
		} else {
			items.push_back({ value, 0 });
			run--;
			while (run >= 3) {
				const int n = std::min(run, 6);
				items.push_back({ 16, (uint8_t)(n - 3) });
				run -= n;
			}
			for (; run > 0; run--) items.push_back({ value, 0 });
		}
	}
}

uint32_t adler32(const uint8_t* data, size_t size) {
	uint32_t a = 1, b = 0;
	while (size > 0) {
		const size_t n = std::min<size_t>(size, 5552);   // as many as can't overflow
		for (size_t i = 0; i < n; i++) {
			a += data[i];
			b += a;
		}
		a %= 65521;
		b %= 65521;
		data += n;
		size -= n;
	}
	return (b << 16) | a;
}

// One block of tokens covering data[from, to).
void writeBlock(BitWriter& bits, const std::vector<Token>& tokens, const uint8_t* data, size_t from, size_t to, bool last) {
	const uint8_t* lengthOf = lengthIndex();
	std::vector<uint32_t> litFreq(286, 0), distFreq(30, 0);
	for (const Token& t : tokens) {
		if (t.dist == 0) litFreq[t.value]++;
		else {
			litFreq[257 + lengthOf[t.value]]++;
			distFreq[distanceCode(t.dist)]++;
		}
	}
	litFreq[256] = 1;
	ensureTwo(litFreq);
	ensureTwo(distFreq);
	const std::vector<uint8_t> litLens = codeLengths(litFreq, 15), distLens = codeLengths(distFreq, 15);
	int litCount = 286, distCount = 30;
	while (litCount > 257 && litLens[litCount - 1] == 0) litCount--;
	while (distCount > 1 && distLens[distCount - 1] == 0) distCount--;
	std::vector<LenItem> items;
	runLengths(litLens, litCount, items);
	runLengths(distLens, distCount, items);
	std::vector<uint32_t> clFreq(19, 0);
	for (const LenItem& it : items) clFreq[it.symbol]++;
	ensureTwo(clFreq);
	const std::vector<uint8_t> clLens = codeLengths(clFreq, 7);
	int clCount = 19;
	while (clCount > 4 && clLens[kClOrder[clCount - 1]] == 0) clCount--;

	// What it costs in bits, against storing the bytes as they are.
	uint64_t cost = 3 + 5 + 5 + 4 + 3 * (uint64_t)clCount;
	for (const LenItem& it : items) cost += clLens[it.symbol] + (it.symbol == 16 ? 2 : it.symbol == 17 ? 3 : it.symbol == 18 ? 7 : 0);
	for (const Token& t : tokens) {
		if (t.dist == 0) {
			cost += litLens[t.value];
		} else {
			const int li = lengthOf[t.value], dc = distanceCode(t.dist);
			cost += litLens[257 + li] + kLenExtra[li] + distLens[dc] + kDistExtra[dc];
		}
	}
	cost += litLens[256];
	const size_t bytes = to - from;
	const uint64_t stored = (uint64_t)bytes * 8 + ((bytes + 65534) / 65535) * 5 * 8 + 7;
	if (stored < cost) {
		size_t at = from;
		do {
			const size_t n = std::min<size_t>(65535, to - at);
			const bool final = last && at + n == to;
			bits.put(final ? 1 : 0, 1);
			bits.put(0, 2);
			bits.align();
			bits.put((uint32_t)n, 16);
			bits.put((uint32_t)(~n & 0xFFFF), 16);
			for (size_t i = 0; i < n; i++) bits.put(data[at + i], 8);
			at += n;
		} while (at < to);
		return;
	}

	const std::vector<uint16_t> litCodes = canonicalCodes(litLens), distCodes = canonicalCodes(distLens), clCodes = canonicalCodes(clLens);
	bits.put(last ? 1 : 0, 1);
	bits.put(2, 2);   // dynamic Huffman codes
	bits.put((uint32_t)(litCount - 257), 5);
	bits.put((uint32_t)(distCount - 1), 5);
	bits.put((uint32_t)(clCount - 4), 4);
	for (int i = 0; i < clCount; i++) bits.put(clLens[kClOrder[i]], 3);
	for (const LenItem& it : items) {
		bits.put(clCodes[it.symbol], clLens[it.symbol]);
		if (it.symbol == 16) bits.put(it.extra, 2);
		else if (it.symbol == 17) bits.put(it.extra, 3);
		else if (it.symbol == 18) bits.put(it.extra, 7);
	}
	for (const Token& t : tokens) {
		if (t.dist == 0) {
			bits.put(litCodes[t.value], litLens[t.value]);
			continue;
		}
		const int li = lengthOf[t.value], dc = distanceCode(t.dist);
		bits.put(litCodes[257 + li], litLens[257 + li]);
		if (kLenExtra[li]) bits.put((uint32_t)(t.value - kLenBase[li]), kLenExtra[li]);
		bits.put(distCodes[dc], distLens[dc]);
		if (kDistExtra[dc]) bits.put((uint32_t)(t.dist - kDistBase[dc]), kDistExtra[dc]);
	}
	bits.put(litCodes[256], litLens[256]);
}

inline uint32_t hash3(const uint8_t* p) { return (((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u) >> 17; }

// ---- PNG's row filters (for the PDF's Predictor 15) --------------------------------

// Each row of the picture behind a byte saying which filter it went through
// (None, Sub, Up, Average or Paeth, whichever leaves the smallest numbers).
std::vector<uint8_t> filterRows(const uint8_t* pixels, int w, int h, int bpp) {
	const size_t stride = (size_t)w * bpp;
	std::vector<uint8_t> out((stride + 1) * h);
	std::vector<uint8_t> none(stride, 0), cand[5];
	for (std::vector<uint8_t>& c : cand) c.resize(stride);
	for (int y = 0; y < h; y++) {
		const uint8_t* row = pixels + (size_t)y * stride;
		const uint8_t* up = y > 0 ? row - stride : none.data();
		uint8_t* dest = out.data() + (size_t)y * (stride + 1);
		if (y > 0 && memcmp(row, up, stride) == 0) {   // the row above again: all zeros
			dest[0] = 2;
			memset(dest + 1, 0, stride);
			continue;
		}
		uint64_t sum[5] = {};
		for (size_t x = 0; x < stride; x++) {
			const int a = x >= (size_t)bpp ? row[x - bpp] : 0, b = up[x], c = x >= (size_t)bpp ? up[x - bpp] : 0, v = row[x];
			const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
			const int paeth = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
			cand[0][x] = (uint8_t)v;
			cand[1][x] = (uint8_t)(v - a);
			cand[2][x] = (uint8_t)(v - b);
			cand[3][x] = (uint8_t)(v - ((a + b) >> 1));
			cand[4][x] = (uint8_t)(v - paeth);
			for (int k = 0; k < 5; k++) sum[k] += (uint64_t)std::abs((int)(int8_t)cand[k][x]);
		}
		int best = 0;
		for (int k = 1; k < 5; k++) if (sum[k] < sum[best]) best = k;
		dest[0] = (uint8_t)best;
		memcpy(dest + 1, cand[best].data(), stride);
	}
	return out;
}

// ---- Text and numbers in the file ---------------------------------------------------

// A PDF string: plain when it's printable ASCII, else UTF-16 with a byte order mark.
std::string pdfString(const std::string& utf8) {
	bool plain = true;
	for (unsigned char c : utf8) if (c < 0x20 || c > 0x7E) plain = false;
	std::string s;
	if (plain) {
		s = "(";
		for (char c : utf8) {
			if (c == '(' || c == ')' || c == '\\') s += '\\';
			s += c;
		}
		return s + ")";
	}
	std::vector<uint32_t> points;
	for (size_t i = 0; i < utf8.size();) {
		const unsigned char c = (unsigned char)utf8[i];
		const int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
		uint32_t cp = more == 0 ? c : c & (0x3F >> more);
		bool ok = c < 0x80 || (c >= 0xC0 && c < 0xF8 && i + more < utf8.size());
		for (int k = 1; ok && k <= more; k++) {
			const unsigned char d = (unsigned char)utf8[i + k];
			if ((d & 0xC0) != 0x80) ok = false;
			else cp = (cp << 6) | (d & 0x3F);
		}
		if (!ok || cp > 0x10FFFF) { cp = 0xFFFD; i++; }
		else i += 1 + more;
		points.push_back(cp);
	}
	char hex[16];
	s = "<FEFF";
	for (uint32_t cp : points) {
		if (cp >= 0x10000) {
			cp -= 0x10000;
			snprintf(hex, sizeof hex, "%04X%04X", 0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
		} else {
			snprintf(hex, sizeof hex, "%04X", cp);
		}
		s += hex;
	}
	return s + ">";
}

std::string number(float v) {
	char buf[32];
	if (v == (float)(long)v) snprintf(buf, sizeof buf, "%ld", (long)v);
	else snprintf(buf, sizeof buf, "%.2f", v);
	for (char* c = buf; *c; c++) if (*c == ',') *c = '.';   // whatever the locale says
	return buf;
}

}  // namespace

std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t size) {
	std::vector<uint8_t> out;
	out.reserve(size / 8 + 64);
	out.push_back(0x78);
	out.push_back(0x9C);
	BitWriter bits(out);
	if (size == 0) {
		bits.put(1, 1);
		bits.put(1, 2);   // fixed codes, and the end of the block
		bits.put(0, 7);
	} else {
		std::vector<int32_t> head(1 << 15, -1), prev(kWindow, -1);
		std::vector<Token> tokens;
		tokens.reserve(kBlockTokens);
		auto insert = [&](size_t p) {
			if (p + 2 >= size) return;
			const uint32_t h = hash3(data + p);
			prev[p & (kWindow - 1)] = head[h];
			head[h] = (int32_t)p;
		};
		size_t pos = 0, blockStart = 0;
		while (pos < size) {
			size_t bestLen = 0, bestDist = 0;
			if (pos + 3 <= size) {
				const size_t limit = std::min<size_t>(kMaxMatch, size - pos);
				int32_t cand = head[hash3(data + pos)];
				int chain = kChain;
				while (cand >= 0 && pos - (size_t)cand < (size_t)kWindow && chain-- > 0) {
					const uint8_t* a = data + cand;
					const uint8_t* b = data + pos;
					if (bestLen >= limit) break;
					if (a[bestLen] == b[bestLen]) {
						size_t len = 0;
						while (len < limit && a[len] == b[len]) len++;
						if (len > bestLen) {
							bestLen = len;
							bestDist = pos - (size_t)cand;
							if (len >= (size_t)kGoodMatch || len == limit) break;
						}
					}
					const int32_t next = prev[(size_t)cand & (kWindow - 1)];
					if (next >= cand) break;   // chains only lead back
					cand = next;
				}
			}
			if (bestLen >= 3 && !(bestLen == 3 && bestDist > 4096)) {
				tokens.push_back({ (uint16_t)bestLen, (uint16_t)bestDist });
				for (size_t k = 0; k < bestLen; k++) insert(pos + k);
				pos += bestLen;
			} else {
				tokens.push_back({ data[pos], 0 });
				insert(pos);
				pos++;
			}
			if (tokens.size() >= kBlockTokens && pos < size) {
				writeBlock(bits, tokens, data, blockStart, pos, false);
				tokens.clear();
				blockStart = pos;
			}
		}
		writeBlock(bits, tokens, data, blockStart, size, true);
	}
	bits.align();
	const uint32_t sum = adler32(data, size);
	for (int shift = 24; shift >= 0; shift -= 8) out.push_back((uint8_t)(sum >> shift));
	return out;
}

// ---- The document -------------------------------------------------------------------

Document::Document(const std::string& t, const std::string& p, const std::string& d) : title(t), producer(p), date(d) {
	out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
	offsets.assign(3, 0);   // the catalog, the page list and the info, written last
}

int Document::begin() {
	offsets.push_back(out.size());
	const int n = (int)offsets.size();
	out += std::to_string(n) + " 0 obj\n";
	return n;
}

void Document::end() { out += "endobj\n"; }

void Document::addPage(float widthPt, float heightPt, int w, int h, int components, const uint8_t* pixels) {
	if (finished || w <= 0 || h <= 0 || (components != 1 && components != 3)) return;
	const std::vector<uint8_t> filtered = filterRows(pixels, w, h, components);
	const std::vector<uint8_t> data = zlibCompress(filtered.data(), filtered.size());
	const int image = begin();
	out += "<< /Type /XObject /Subtype /Image /Width " + std::to_string(w) + " /Height " + std::to_string(h) + " /ColorSpace " +
	       (components == 1 ? "/DeviceGray" : "/DeviceRGB") + " /BitsPerComponent 8 /Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors " +
	       std::to_string(components) + " /BitsPerComponent 8 /Columns " + std::to_string(w) + " >> /Length " + std::to_string(data.size()) +
	       " >>\nstream\n";
	out.append((const char*)data.data(), data.size());
	out += "\nendstream\n";
	end();
	const std::string content = "q " + number(widthPt) + " 0 0 " + number(heightPt) + " 0 0 cm /Im0 Do Q\n";
	const int contents = begin();
	out += "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream\n";
	end();
	const int page = begin();
	out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + number(widthPt) + " " + number(heightPt) + "] /Resources << /XObject << /Im0 " +
	       std::to_string(image) + " 0 R >> >> /Contents " + std::to_string(contents) + " 0 R >>\n";
	end();
	pages.push_back(page);
}

const std::string& Document::finish() {
	if (finished) return out;
	finished = true;
	offsets[0] = out.size();
	out += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
	offsets[1] = out.size();
	out += "2 0 obj\n<< /Type /Pages /Count " + std::to_string(pages.size()) + " /Kids [";
	for (int p : pages) out += " " + std::to_string(p) + " 0 R";
	out += " ] >>\nendobj\n";
	offsets[2] = out.size();
	out += "3 0 obj\n<< /Title " + pdfString(title) + " /Producer " + pdfString(producer);
	if (!date.empty()) out += " /CreationDate (D:" + date + ")";
	out += " >>\nendobj\n";
	const size_t xref = out.size();
	out += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
	for (size_t at : offsets) {
		char line[32];
		snprintf(line, sizeof line, "%010lu 00000 n \n", (unsigned long)at);
		out += line;
	}
	out += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R /Info 3 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
	return out;
}

}  // namespace pdf
