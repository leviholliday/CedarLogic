#include "ShareCodec.h"
#include "Deflate.h"

#include <cstdio>
#include <vector>

namespace sharelink {

const char* const kWebBase = "https://cedarlogic.netlify.app/online-logic-gate-simulator/";

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string toBase64url(const std::string& bytes) {
	std::string out;
	out.reserve((bytes.size() * 4 + 2) / 3);
	size_t i = 0;
	for (; i + 2 < bytes.size(); i += 3) {
		const unsigned v = (unsigned char)bytes[i] << 16 | (unsigned char)bytes[i + 1] << 8 | (unsigned char)bytes[i + 2];
		out += kAlphabet[v >> 18];
		out += kAlphabet[v >> 12 & 63];
		out += kAlphabet[v >> 6 & 63];
		out += kAlphabet[v & 63];
	}
	if (i + 1 == bytes.size()) {
		const unsigned v = (unsigned char)bytes[i] << 16;
		out += kAlphabet[v >> 18];
		out += kAlphabet[v >> 12 & 63];
	} else if (i + 2 == bytes.size()) {
		const unsigned v = (unsigned char)bytes[i] << 16 | (unsigned char)bytes[i + 1] << 8;
		out += kAlphabet[v >> 18];
		out += kAlphabet[v >> 12 & 63];
		out += kAlphabet[v >> 6 & 63];
	}
	return out;
}

// Either alphabet (a link that went through something that changed - and _
// back to + and /), padding and white space ignored, as the website does.
bool fromBase64url(const std::string& s, std::string& bytes) {
	bytes.clear();
	unsigned acc = 0;
	int bits = 0;
	size_t count = 0;
	for (char c : s) {
		int v;
		if (c >= 'A' && c <= 'Z') v = c - 'A';
		else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
		else if (c >= '0' && c <= '9') v = c - '0' + 52;
		else if (c == '-' || c == '+') v = 62;
		else if (c == '_' || c == '/') v = 63;
		else if (c == '=' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
		else return false;
		count++;
		acc = acc << 6 | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			bytes += (char)(acc >> bits & 0xFF);
		}
	}
	if (count % 4 == 1) return false;   // (no whole number of bytes ends there)
	return !bytes.empty();
}

// UTF-8 as the standard has it: no overlong forms, no surrogates, nothing past U+10FFFF.
bool validUtf8(const std::string& s) {
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = (unsigned char)s[i];
		size_t more;
		unsigned lowest, cp;
		if (c < 0x80) { i++; continue; }
		else if (c >= 0xC2 && c <= 0xDF) { more = 1; lowest = 0x80; cp = c & 0x1F; }
		else if (c >= 0xE0 && c <= 0xEF) { more = 2; lowest = 0x800; cp = c & 0x0F; }
		else if (c >= 0xF0 && c <= 0xF4) { more = 3; lowest = 0x10000; cp = c & 0x07; }
		else return false;
		if (i + more >= s.size()) return false;
		for (size_t k = 1; k <= more; k++) {
			const unsigned char d = (unsigned char)s[i + k];
			if ((d & 0xC0) != 0x80) return false;
			cp = cp << 6 | (d & 0x3F);
		}
		if (cp < lowest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
		i += more + 1;
	}
	return true;
}

int hexValue(char c) {
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

std::string percentDecoded(const std::string& s) {
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '%' && i + 2 < s.size() && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
			out += (char)(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2]));
			i += 2;
		} else {
			out += s[i];
		}
	}
	return out;
}

// What a URL may hold unescaped (RFC 3986's unreserved characters); the rest as %XX.
std::string percentEncoded(const std::string& s) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

// The first `count` characters (not bytes) of UTF-8 text.
std::string firstCharacters(const std::string& s, size_t count) {
	size_t i = 0;
	while (i < s.size() && count > 0) {
		i++;
		while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
		count--;
	}
	return s.substr(0, i);
}

}  // namespace

std::string encode(const std::string& text) {
	if (text.empty()) return std::string();
	return toBase64url(deflate::compress(text));
}

bool decode(const std::string& data, std::string& text, std::string& why) {
	text.clear();
	std::string packed;
	if (!fromBase64url(data, packed)) {
		why = "not a CedarLogic link";
		return false;
	}
	bool tooBig = false;
	if (!deflate::decompress(packed, text, kMaxText, &tooBig)) {
		why = tooBig ? "too big to be a circuit" : "the link was cut short";
		return false;
	}
	if (!validUtf8(text)) {
		text.clear();
		why = "not text";
		return false;
	}
	return true;
}

bool parse(const std::string& link, std::string& data, std::string& name) {
	// After the "#" (where the website keeps it), else after a "?".
	size_t cut = link.find('#');
	if (cut == std::string::npos) cut = link.find('?');
	if (cut == std::string::npos) return false;
	data.clear();
	name.clear();
	bool haveData = false, haveName = false;
	size_t i = cut + 1;
	while (i <= link.size()) {
		size_t end = link.find('&', i);
		if (end == std::string::npos) end = link.size();
		const std::string pair = link.substr(i, end - i);
		if (!haveData && pair.compare(0, 2, "c=") == 0) {
			data = pair.substr(2);
			haveData = true;
		} else if (!haveName && pair.compare(0, 2, "n=") == 0) {
			name = percentDecoded(pair.substr(2));
			haveName = true;
		}
		i = end + 1;
	}
	if (data.empty()) return false;
	if (!validUtf8(name)) name.clear();
	name = firstCharacters(name, 80);
	return true;
}

std::string fragment(const std::string& data, const std::string& name) {
	std::string f = "c=" + data;
	if (!name.empty()) f += "&n=" + percentEncoded(firstCharacters(name, 80));
	return f;
}

// ---- The checks ----------------------------------------------------------------------

namespace {

// Links made by python's zlib (raw deflate, base64url), one of each kind of
// block, and the text each holds -- made again here rather than kept:
//   zlib.compressobj(0 or 9, zlib.DEFLATED, -15)
const char* const kStoredLink = "AS8A0P8oY2VkYXJsb2dpYyAodmVyc2lvbiAzKSAobmFtZSAiw6kg4pyTIOaXpeacrCIpKQ";
const char* const kFixedLink = "00hOTUksyslPz0xW0ChLLSrOzM9TMNZU0MhLzE1VUDq8UuHRnMkKz6YvfTZnjZKmJgA";
const char* const kRunLink = "7cGxCQAgDACwVxz1rFJFBEHw_8Er3JLUHD3uPnNlCQAAAADgu_YA";
const char* const kDynamicLink =
    "bZc7bmRHEAR9naLNXe9l9t8UIFt3kCHsBXR_qDA1U5TAcEgiQcx09C-jf_z665-_29N-__OP-Pn8_O3HK9Ar2E39k_iVaDSvT9RfkdX6_UQjo9OmP9F8RX22NT_RekXDbZ9PtDO67eoTnVc0Vxuf5L"
    "6S1Zt2DTSHvp_Wvwb_Hv1uo4avHP8Z8YGVJcBVWwWgJLinnSJQIsx2i0CJEJ9aBEoE3ZiRypLBq_WCUFL03kZROCnG01ZROCnGbvtrEZJijnaKwkmx1G5ROCnWiRFWlhR7xl-VJcZx68Xh5Di3zeJw"
    "ctzVVnH4zRHzXBsiMWJNbmH0xIjZKYqeFDFIfe2lpIj_dlH0pOgntkplSTFmm0XRk2I6gCtLinnbKYqeFGu1WxQ9KXZg1B5OivPEslSWFCcoCmMkxh1tFMZIDMUXV5QUp-2CGAkR--oUxEiIWOFbEC"
    "MhYqZVECMhgtkFMRIivrwXxUyK-DWLYibF3HGGKkuKNdouipkUW7ELKkuMfdrXyU6MM4OkssS4jj1TWWLcG9NY2Xst4nMrSoo4lasoVlLE-ThFsZIi9uktipUUsWMKYiVErJwKYiVEzGEvivU-3TOW"
    "pbKkiE-dRbGSYt2Yl8qSYq-4WCpLjNPbLYydGPeJs1FZYtwdJ7iyxBixvBW9d5RilStLjLhdVmHsxIhTvgtjJ0act_N10SZG7Pui2EkRO1BFsZMidoKL4iRFrMkoipMUMTmzKE5SxChXYZzEiH_fhX"
    "HeJ-PEdqksMW6c74reJyN4K3rvqLgvKkqIuJhHQZyEiBtyFsRNiLiqdkHchIg74xTEfS_FiFuvsoSIY1QMNxliO7sYbjLExuoFcRMiFngUxU2KmOpVGDcxgnl_1V5ixLef__Te985-vre2HuhtPdDc"
    "eqC79UB764H-1gMNrud7h-uhFucapx7HIscmxyqnLqcyxzbHOqc-FxW6qNFFlS7qdFGpi1pdVOuiXhcVu6DZRdUu6HZRuYvaXVTvon4XFbyo4UUVL-h4UcmLWl5U84KeFxS9qOlFVS_qelHZi9peVP"
    "eivhcVvqjxBZUv6nxR6YtaX1D7ot4XFb-o-QXVL-p-UfmL2l9U_6L-FwmAyABECiByAIEEiCxApAEiDxCJgMAERCogcgGRDIhsQKQDIh8QCYHACARKIHACkRSIrECkBSIvEImBwAxEaiByA5EciOxA"
    "pAciP_B3PzD4gckPTH5g8gOTH5j8wOQHBj8w-YHJD0x-YPIDkx-Y_MDgBwY_MPmByQ-M73188POLn578-ObHRz---vHZj-9-fPiDH5j8wOAHJj8w-YHJD0x-YPIDkx-Y_MDgByY_MPmByQ8MfmDwA5"
    "MfmPzA5AcmPzD5gckPTH5g8gOTHxj8wOQHJj8w-YHBD0x-YPIDkx8Y_MDkByY_MPmByQ9MfmDyA5MfmPzA5AcGPzD5gckPTH5g8gODH5j8wOQHJj8w-YHJD0x-YPIDgx8Y_MDgByY_MPmByQ9MfmDy"
    "A4MfmPzA5AcmPzD5gckP_H8_-Bc";

std::string smallText() { return "(cedarlogic (version 3) (name \"\xC3\xA9 \xE2\x9C\x93 \xE6\x97\xA5\xE6\x9C\xAC\"))"; }
std::string runText() { return "(cedarlogic " + std::string(5000, 'a') + ")"; }
std::string gatesText() {
	std::string s;
	for (int i = 0; i < 300; i++) {
		char line[64];
		snprintf(line, sizeof line, "(gate %d AND %d %d)\n", i, i * 7 % 100, i * 13 % 100);
		s += line;
	}
	return s;
}

}  // namespace

bool selfTest(std::string& report, const std::string& circuit, std::string* linkMade, const std::string& linkGiven) {
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		report += std::string(ok ? "PASS  " : "FAIL  ") + what + "\n";
		if (!ok) failures++;
	};
	auto roundTrip = [&](const std::string& text, const std::string& what) {
		std::string back, why;
		const std::string data = encode(text);
		check(!data.empty() && decode(data, back, why) && back == text, "round trip of " + what);
		check(data.find_first_of("+/= \n") == std::string::npos, "...written as base64url, with no padding");
		return data;
	};

	// Round trips.
	std::string big;
	for (int i = 0; i < 20000; i++) big += "gate AND 12 34\n";
	std::string noise;
	unsigned seed = 12345;
	for (int i = 0; i < 70000; i++) {
		seed = seed * 1103515245u + 12345u;
		noise += (char)(32 + (seed >> 16) % 95);
	}
	roundTrip(smallText(), "a circuit's text with accents, a check mark and kanji");
	roundTrip(gatesText(), "300 lines of gates");
	roundTrip(big, "20000 repeated lines (" + std::to_string(big.size()) + " bytes)");
	roundTrip(noise, "70000 bytes of random text, which barely compresses");
	roundTrip("x", "one byte");
	check(encode("").empty(), "nothing to encode makes no link");
	check(encode(gatesText()).size() < gatesText().size() / 3, "it compresses 300 lines of gates to under a third");

	// What python's zlib made.
	auto fixture = [&](const char* link, const std::string& want, const std::string& what) {
		std::string got, why;
		check(decode(link, got, why) && got == want, "a link made by python's zlib decodes: " + what);
	};
	fixture(kStoredLink, smallText(), "a stored block");
	fixture(kFixedLink, smallText(), "a fixed-code block");
	fixture(kRunLink, runText(), "a match that overlaps itself");
	fixture(kDynamicLink, gatesText(), "a dynamic-code block (" + std::to_string(std::string(kDynamicLink).size()) + " characters)");
	{
		// The same link as another program might have copied it: the standard
		// alphabet, padded, in lines.
		std::string mangled, got, why;
		for (const char* p = kDynamicLink; *p; p++) {
			mangled += *p == '-' ? '+' : *p == '_' ? '/' : *p;
			if ((p - kDynamicLink) % 76 == 75) mangled += "\r\n";
		}
		while (mangled.size() % 4) mangled += '=';
		check(decode(mangled, got, why) && got == gatesText(), "...and the same link in the standard alphabet, padded and in lines");
	}

	// What isn't a link.
	for (const char* bad : { "not*base64", "AAAA", "", "QUJD", "A", "////" }) {
		std::string t, why;
		check(!decode(bad, t, why) && !why.empty() && t.empty(), std::string("'") + bad + "' is refused");
	}
	{
		std::string t, why;
		check(!decode(encode(big).substr(0, 40), t, why), "a link cut short is refused");
		const std::string bomb = encode(std::string(30000000, 'a'));
		check(!bomb.empty() && bomb.size() < 100000, "30 MB of one letter makes a link of " + std::to_string(bomb.size()) + " characters");
		check(!decode(bomb, t, why) && why == "too big to be a circuit", "...which is refused as too big, and not inflated");
		check(!decode(encode("\xFF\xFE not utf-8"), t, why) && why == "not text", "text that isn't UTF-8 is refused");
	}

	// Reading and making the address.
	std::string data, name;
	check(parse("https://x/online/#c=abc&n=A%20B", data, name) && data == "abc" && name == "A B", "parse reads the data and the name");
	check(parse("cedarlogic://open?c=xyz", data, name) && data == "xyz" && name.empty(), "...and the ? form");
	check(parse("https://x/?utm=1#c=abc", data, name) && data == "abc", "...and the # when a ? comes first");
	check(parse("#c=q&c=r&n=a%2Bb+c", data, name) && data == "q" && name == "a+b+c", "...the first of each, + staying +");
	check(!parse("cedarlogic://open", data, name) && !parse("https://x/#compare", data, name) && !parse("#n=a", data, name), "...and refuses what has no data");
	check(parse("#c=a&n=%E6%97%A5%E6%9C%AC", data, name) && name == "\xE6\x97\xA5\xE6\x9C\xAC", "...names in UTF-8");
	check(parse("#c=a&n=%FF%FE", data, name) && name.empty(), "...and drops a name that isn't");
	check(parse("#c=a&n=" + std::string(200, 'x'), data, name) && name.size() == 80, "...and keeps 80 characters of it");
	check(fragment("abc", "Half adder & more") == "c=abc&n=Half%20adder%20%26%20more", "fragment escapes the name");
	check(fragment("abc", "") == "c=abc", "...and leaves out an empty one");
	check(fragment("abc", "\xE6\x97\xA5") == "c=abc&n=%E6%97%A5", "...in UTF-8");
	check(fragment("abc", std::string(200, 'x')).size() == std::string("c=abc&n=").size() + 80, "...and cuts a long one to 80 characters");
	{
		const std::string link = std::string(kWebBase) + "#" + fragment(encode(smallText()), "Half adder");
		std::string text, why;
		check(parse(link, data, name) && name == "Half adder" && decode(data, text, why) && text == smallText(), "a whole link made here reads back");
	}

	// A circuit of the caller's, and a link someone else made of it.
	if (!circuit.empty()) {
		std::string back, why;
		const std::string data2 = encode(circuit);
		check(!data2.empty() && decode(data2, back, why) && back == circuit, "the circuit (" + std::to_string(circuit.size()) + " bytes) round trips: a link of " +
		                                                                     std::to_string(data2.size()) + " characters");
		if (linkMade) *linkMade = data2;
		if (!linkGiven.empty()) {
			std::string d, n, got;
			check(parse("#" + linkGiven, d, n) && decode(d, got, why) && got == circuit, "a link of it made by another program decodes to the circuit");
		}
	}
	report += failures ? std::to_string(failures) + " share link checks failed\n" : std::string("share link checks: all passed\n");
	return failures == 0;
}

}  // namespace sharelink
