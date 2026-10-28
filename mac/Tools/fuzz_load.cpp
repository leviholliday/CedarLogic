// File fuzzer for the engine's loader: damaged, hand-edited and hostile .cdl
// files, in the old XML formats and the current one.
//   fuzz_load <cl_gatedefs.xml> <first-seed> <seed-count> <tries-per-seed> [start.cdl...]
//   fuzz_load <cl_gatedefs.xml> 0 0 0 <file.cdl...>     (each file as it is, undamaged:
//                                                        to rerun one a sweep kept)
// Each seed takes one start file (seed s: file s % count; with none, a small
// built-in circuit), and makes <tries> damaged copies of it: bytes flipped,
// cut, repeated and spliced; numbers made huge, negative, NaN or infinite; ids
// duplicated or pointed at gates that aren't there; text with backslashes,
// quotes, '<', '&', newlines, non-ASCII and very long runs; deep nesting.
//
// Every copy is opened. A file may be refused (with a message) or opened with
// notices; it must never crash, hang or leak. One that opens is also
// simulated, drawn (headless: CL_HEADLESS), copied and pasted, and saved,
// reopened and saved again: the two saves must match.
//
// Built with -DCL_LIBFUZZER and clang's -fsanitize=fuzzer, the same check is
// libFuzzer's target instead (mac/Tools/headless/CMakeLists.txt, CL_LIBFUZZER).
//
// Environment: CL_FUZZ_DUMP=<dir> keeps the file that failed (or crashed, or
// hung) as <dir>/load-seed<s>.cdl, to rerun as above; CL_FUZZ_TRACE=1
// prints each try; CL_FUZZ_HANG=<seconds> is how long one file may take (20).
#include "DocumentImpl.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

// The sanitizers call this just before they end the process, so the file that
// set them off is kept (declared weak: absent in a plain build).
extern "C" void __sanitizer_set_death_callback(void (*)(void)) __attribute__((weak));

namespace {

std::string current;          // the file being tried, for the crash and hang handlers
std::string dumpPath;         // where to keep it ("" for nowhere)

void keepCurrent() {
	if (dumpPath.empty()) return;
	// Plain write(): this runs in a signal handler or a sanitizer's death path.
	FILE* f = fopen(dumpPath.c_str(), "wb");
	if (f == nullptr) return;
	fwrite(current.data(), 1, current.size(), f);
	fclose(f);
}

void onFatal(int sig) {
	keepCurrent();
	if (sig == SIGALRM) {
		static const char msg[] = "fuzz_load: one file took too long (a hang)\n";
		(void)!write(2, msg, sizeof msg - 1);
	}
	signal(sig, SIG_DFL);
	raise(sig == SIGALRM ? SIGABRT : sig);
}

void damage(std::string& t);
extern unsigned long long rs;

// Leave out what a reload recomputes (the simulation's values, written by the
// settle that runs on open), so two saves compare as circuits.
std::string withoutSimState(const std::string& text) {
	std::string out;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (line.find("(lparam \"CURRENT_VALUE\"") != std::string::npos) continue;
		out += line;
		out += '\n';
	}
	return out;
}

// One file, start to end. False when something is wrong that isn't a crash.
bool tryFile(const std::string& text, std::string& why) {
	current = text;
	char err[512] = "";
	CLDocument* doc = cl_document_open_text(text.data(), (long)text.size(), err, sizeof err);
	if (doc == nullptr) return true;   // refused, with a message: fine
	char bad[512] = "";
	if (!cl_document_check(doc, bad, sizeof bad)) {
		why = std::string("opened inconsistent: ") + bad;
		cl_document_close(doc);
		return false;
	}

	const int pages = cl_document_page_count(doc);
	cl_document_set_running(doc, true);
	for (int i = 0; i < 4; i++) cl_document_tick(doc, 50);
	cl_document_step(doc);
#if defined(CL_HEADLESS)
	for (int p = 0; p < pages; p++) cl_document_draw(doc, p, nullptr, 2, -50, 50, 0.1, p % 2);
#endif
	for (int p = 0; p < pages && p < 3; p++) {
		cl_edit_select_all(doc, p);
		std::string copied = cl_edit_copy(doc, p);
		if (!copied.empty()) {
			const char* back = nullptr;
			cl_edit_paste(doc, p, copied.c_str(), 40, 40, false, &back);
			if (!cl_document_check(doc, bad, sizeof bad)) {
				why = std::string("inconsistent after a paste: ") + bad;
				cl_document_close(doc);
				return false;
			}
			if (cl_edit_can_undo(doc)) cl_edit_undo(doc);
			// The clipboard is text from anywhere: a damaged copy must paste safely too.
			// Damaged as the file's own text decides, so rerunning a kept file repeats it.
			const unsigned long long saved = rs;
			rs = std::hash<std::string>()(text) | 1;
			std::string odd = copied;
			for (int k = 0; k < 3; k++) damage(odd);
			rs = saved;
			cl_edit_paste(doc, p, odd.c_str(), -40, 40, false, &back);
			if (!cl_document_check(doc, bad, sizeof bad)) {
				why = std::string("inconsistent after a damaged paste: ") + bad;
				cl_document_close(doc);
				return false;
			}
		}
	}
	cl_document_set_running(doc, false);

	const std::string a = cl_document_save_text(doc);
	CLDocument* back = cl_document_open_text(a.data(), (long)a.size(), err, sizeof err);
	bool ok = true;
	if (back == nullptr) {
		why = std::string("its save did not reopen: ") + err;
		ok = false;
	} else {
		const std::string b = cl_document_save_text(back);
		const std::string ca = withoutSimState(a), cb = withoutSimState(b);
		if (ca != cb) {
			// Where they part, a line of each.
			size_t i = 0;
			while (i < ca.size() && i < cb.size() && ca[i] == cb[i]) i++;
			const size_t line = ca.rfind('\n', i == 0 ? 0 : i - 1);
			const size_t from = line == std::string::npos ? 0 : line + 1;
			why = "save, reopen, save is not stable:\n  first: " + ca.substr(from, ca.find('\n', i) - from) +
			      "\n  again: " + cb.substr(from, cb.find('\n', i) - from);
			ok = false;
			if (const char* dir = getenv("CL_FUZZ_DUMP")) {   // both saves, to diff
				for (int w = 0; w < 2; w++) {
					std::ofstream out(std::string(dir) + (w ? "/again.cdl" : "/first.cdl"), std::ios::binary);
					out << (w ? b : a);
				}
			}
		}
		cl_document_close(back);
	}
	cl_document_close(doc);
	return ok;
}

// ---- Damage -------------------------------------------------------------------

unsigned long long rs;   // the damage's random state
unsigned rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }
size_t ri(size_t n) { return n == 0 ? 0 : rnd() % n; }

const char* const kNumbers[] = {
	"0", "-1", "-0", "1e308", "-1e308", "1e309", "nan", "-nan", "inf", "-inf", "1e-320",
	"4294967295", "4294967296", "18446744073709551615", "18446744073709551616",
	"99999999999999999999999999", "2147483647", "-2147483648", "9223372036854775807",
	"0x10", "1,2", "", " ", "3.5e", "+5", "00000000000000000001", "1.0000001",
};
const char* const kTexts[] = {
	"\\", "\\\\", "a\\b", "\\\"", "\"", "\"\"", "<", "<version>", "</circuit>", "&", "&amp;", "&lt;",
	"\n", "\r\n", "line one\nline two", "\t", "\x07", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80",
	"\xFF\xFE", "\xC3", "(", ")", "((", "))", "(lparam", "\\)", "\\(", " ", "",
	"\xEF\xBB\xBF", "%s%n", "\x01\x02", "]]>", "<!--", "-->",
};
const char* const kTags[] = {
	"(gate", "(wire", "(page", "(seg", "(connect", "(cross", "(ids", "(uuid", "(at", "(angle",
	"(lparam", "(gparam", "(notes", "(ink", "(name", "(links",
	"<gate>", "<wire>", "<ID>", "<type>", "<position>", "<lparam>", "<gparam>", "<shape>",
	"<hsegment>", "<vsegment>", "<points>", "<connection>", "<GID>", "<intersection>", "<page 0>",
	"<page 300>", "<page -1>", "<circuit>", "</gate>", "</wire>",
};

// The spans of the text that are numbers or quoted strings, to aim damage at.
void tokens(const std::string& t, std::vector<std::pair<size_t, size_t>>& nums,
            std::vector<std::pair<size_t, size_t>>& strs) {
	for (size_t i = 0; i < t.size();) {
		if (t[i] == '"') {
			size_t j = i + 1;
			while (j < t.size() && t[j] != '"') j += (t[j] == '\\') ? 2 : 1;
			if (j >= t.size()) break;
			strs.emplace_back(i + 1, j - i - 1);
			i = j + 1;
		} else if ((t[i] >= '0' && t[i] <= '9') || (t[i] == '-' && i + 1 < t.size() && t[i + 1] >= '0' && t[i + 1] <= '9')) {
			size_t j = i + 1;
			while (j < t.size() && ((t[j] >= '0' && t[j] <= '9') || t[j] == '.' || t[j] == 'e' || t[j] == '-')) j++;
			nums.emplace_back(i, j - i);
			i = j;
		} else {
			i++;
		}
	}
}

void damage(std::string& t) {
	std::vector<std::pair<size_t, size_t>> nums, strs;
	switch (ri(16)) {
	case 0: case 1: case 2: {   // a number made odd
		tokens(t, nums, strs);
		if (nums.empty()) break;
		auto n = nums[ri(nums.size())];
		t.replace(n.first, n.second, kNumbers[ri(sizeof kNumbers / sizeof *kNumbers)]);
		break; }
	case 3: {   // an id copied over another (duplicate ids, wires to gates that aren't there)
		tokens(t, nums, strs);
		if (nums.size() < 2) break;
		auto a = nums[ri(nums.size())], b = nums[ri(nums.size())];
		const std::string with = t.substr(a.first, a.second);
		t.replace(b.first, b.second, with);
		break; }
	case 4: case 5: {   // text put into a string or between XML tags
		tokens(t, nums, strs);
		std::string ins;
		for (size_t k = 1 + ri(3); k > 0; k--) ins += kTexts[ri(sizeof kTexts / sizeof *kTexts)];
		if (!strs.empty() && ri(2)) {
			auto s = strs[ri(strs.size())];
			t.insert(s.first + ri(s.second + 1), ins);
		} else {
			t.insert(ri(t.size() + 1), ins);
		}
		break; }
	case 6: {   // a long run
		const size_t n = ri(3) == 0 ? 200000 : 1 + ri(5000);
		t.insert(ri(t.size() + 1), std::string(n, "a\\\"(<9 "[ri(7)]));
		break; }
	case 7: {   // deep nesting
		const bool xml = t.find('<') != std::string::npos && ri(2);
		const size_t n = 1 + ri(ri(2) ? 100 : 50000);
		std::string open, close;
		for (size_t k = 0; k < n; k++) { open += xml ? "<gate>" : "("; close += xml ? "</gate>" : ")"; }
		const size_t at = ri(t.size() + 1);
		t.insert(at, open + (ri(2) ? close : std::string()));
		break; }
	case 8: {   // a span repeated (duplicate gates, wires, pages)
		if (t.empty()) break;
		size_t a = ri(t.size()), n = 1 + ri(std::min<size_t>(t.size() - a, 2000));
		t.insert(ri(t.size() + 1), t.substr(a, n));
		break; }
	case 9: {   // a span cut out (truncated, missing ids)
		if (t.empty()) break;
		size_t a = ri(t.size()), n = 1 + ri(std::min<size_t>(t.size() - a, 500));
		t.erase(a, n);
		break; }
	case 10: {   // cut short
		t.resize(ri(t.size() + 1));
		break; }
	case 11: case 12: {   // a byte changed
		if (t.empty()) break;
		t[ri(t.size())] = (char)ri(256);
		break; }
	case 13: {   // a tag dropped in
		t.insert(ri(t.size() + 1), kTags[ri(sizeof kTags / sizeof *kTags)]);
		break; }
	case 14: {   // a whole span repeated many times (an enormous file)
		if (t.empty()) break;
		size_t a = ri(t.size()), n = 1 + ri(std::min<size_t>(t.size() - a, 400));
		const std::string piece = t.substr(a, n);
		std::string many;
		for (size_t k = 50 + ri(500); k > 0; k--) many += piece;
		t.insert(a, many);
		break; }
	case 15: {   // a string's text replaced outright
		tokens(t, nums, strs);
		if (strs.empty()) break;
		auto s = strs[ri(strs.size())];
		t.replace(s.first, s.second, kTexts[ri(sizeof kTexts / sizeof *kTexts)]);
		break; }
	}
}

// With no start file: a few parts, two of them holding text, saved by the engine.
std::string builtIn() {
	CLDocument* doc = cl_document_new();
	cl_edit_add_gate(doc, 0, "AA_TOGGLE", 0, 0);
	cl_edit_add_gate(doc, 0, "AA_AND2", 10, 0);
	cl_edit_add_gate(doc, 0, "GA_LED", 20, 0);
	cl_edit_add_gate(doc, 0, "AA_LABEL", 0, 10);
	cl_edit_add_gate(doc, 0, "DE_TO", 10, 10);
	for (auto& g : *doc->page(0)->getGateList()) {
		cl_gate_set_setting(doc, (long)g.first, "LABEL_TEXT", "a\\b \"c\" <d> & e");
		cl_gate_set_setting(doc, (long)g.first, "JUNCTION_ID", "x\\y");
	}
	std::string text = cl_document_save_text(doc);
	cl_document_close(doc);
	return text;
}

std::string readFile(const char* path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

}  // namespace

#if defined(CL_LIBFUZZER)

extern "C" int LLVMFuzzerInitialize(int*, char***) {
	const char* defs = getenv("CL_GATEDEFS");
	if (!cl_library_load(defs ? defs : "res/cl_gatedefs.xml")) {
		fprintf(stderr, "fuzz_load: set CL_GATEDEFS to res/cl_gatedefs.xml\n");
		abort();
	}
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size) {
	std::string why;
	if (!tryFile(std::string((const char*)data, size), why)) {
		fprintf(stderr, "fuzz_load: %s\n", why.c_str());
		abort();
	}
	return 0;
}

#else

int main(int argc, char** argv) {
	if (argc < 5 || !cl_library_load(argv[1])) {
		fprintf(stderr, "usage: fuzz_load gatedefs first-seed count tries [start.cdl...]\n");
		return 2;
	}
	const unsigned long long first = strtoull(argv[2], nullptr, 10);
	const int count = atoi(argv[3]), tries = atoi(argv[4]);
	std::vector<std::string> starts;
	for (int i = 5; i < argc; i++) starts.push_back(readFile(argv[i]));
	if (starts.empty()) starts.push_back(builtIn());
	const bool trace = getenv("CL_FUZZ_TRACE") != nullptr;
	const char* hang = getenv("CL_FUZZ_HANG");
	const unsigned hangSeconds = hang ? (unsigned)atoi(hang) : 20;
	cl_set_settle_on_open(true);

	signal(SIGALRM, onFatal);
	signal(SIGABRT, onFatal);
	signal(SIGSEGV, onFatal);
	if (__sanitizer_set_death_callback) __sanitizer_set_death_callback(keepCurrent);

	int bad = 0;
	if (tries == 0) {
		for (int i = 5; i < argc; i++) {
			std::string why;
			if (!tryFile(starts[(size_t)(i - 5)], why)) { printf("%s: %s\n", argv[i], why.c_str()); bad++; }
		}
		printf("%d files, %d failed\n", argc - 5, bad);
		return bad ? 1 : 0;
	}
	for (int i = 0; i < count; i++) {
		const unsigned long long seed = first + i;
		rs = seed * 0x9E3779B97F4A7C15ull + 1;
		if (const char* dir = getenv("CL_FUZZ_DUMP")) dumpPath = std::string(dir) + "/load-seed" + std::to_string(seed) + ".cdl";
		const std::string& start = starts[seed % starts.size()];
		std::string t = start;
		for (int k = 0; k < tries; k++) {
			// Damage builds up over a few tries, then starts again from the clean file.
			if (ri(4) == 0) t = start;
			for (size_t m = 1 + ri(3); m > 0; m--) damage(t);
			if (trace) printf("seed %llu try %d: %zu bytes\n", seed, k, t.size());
			std::string why;
			alarm(hangSeconds);
			const bool ok = tryFile(t, why);
			alarm(0);
			if (!ok) {
				printf("seed %llu: try %d: %s\n", seed, k, why.c_str());
				keepCurrent();
				bad++;
				break;
			}
		}
		fflush(stdout);
	}
	printf("%d seeds, %d failed\n", count, bad);
	return bad ? 1 : 0;
}

#endif
