#include "ShareLink.h"
#include "Window.h"

#include <shlobj.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace sharelink {

namespace {

std::string cacheRoot() {
	PWSTR p = nullptr;
	std::string dir;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) && p) dir = U(p);
	if (p) CoTaskMemFree(p);
	if (dir.empty()) {
		wchar_t tmp[MAX_PATH + 1];
		const DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
		dir = U(std::wstring(tmp, n > 0 && n <= MAX_PATH ? n : 0));
		while (!dir.empty() && dir.back() == '\\') dir.pop_back();
	}
	return dir + "\\CedarLogic\\links";
}

std::string readText(const std::string& path) {
	std::ifstream in(W(path).c_str(), std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

// Written to a temporary beside it and moved into place, so a circuit being
// opened never finds half a file.
bool writeText(const std::string& path, const std::string& text) {
	const std::wstring target = W(path), tmp = target + L".tmp";
	HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD wrote = 0;
	const bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &wrote, nullptr) && wrote == text.size();
	CloseHandle(h);
	if (ok && MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
	DeleteFileW(tmp.c_str());
	return false;
}

// A circuit's name as a file name: nothing Windows won't have in one, no dots
// or spaces at the ends, and not a device's name (CON, NUL, COM1...), which
// opens that device whatever follows it.
std::string fileName(const std::string& name) {
	std::string base;
	for (unsigned char c : name) base += (c < 32 || strchr("\\/:*?\"<>|", c)) ? '-' : (char)c;
	while (!base.empty() && (base.front() == '.' || base.front() == ' ')) base.erase(0, 1);
	while (!base.empty() && (base.back() == '.' || base.back() == ' ')) base.pop_back();
	if (base.empty()) base = "Shared circuit";
	std::string head = lowerCase(base.substr(0, base.find('.')));
	while (!head.empty() && head.back() == ' ') head.pop_back();
	const bool device = head == "con" || head == "prn" || head == "aux" || head == "nul" ||
	                    ((head.compare(0, 3, "com") == 0 || head.compare(0, 3, "lpt") == 0) && head.size() == 4 && head[3] >= '1' && head[3] <= '9');
	return device ? "_" + base : base;
}

// A folder's name for a link's data: a hash of it (FNV-1a).
std::string folderName(const std::string& data) {
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (unsigned char c : data) h = (h ^ c) * 0x100000001b3ULL;
	return strf("%016llx", h);
}

bool isHash(const std::wstring& s) {
	if (s.size() != 16) return false;
	for (wchar_t c : s)
		if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
	return true;
}

// Links opened more than a day ago go: Your Circuits has its own copy by then.
void tidyCache(const std::wstring& root) {
	FILETIME nowFt;
	GetSystemTimeAsFileTime(&nowFt);
	ULARGE_INTEGER now;
	now.LowPart = nowFt.dwLowDateTime;
	now.HighPart = nowFt.dwHighDateTime;
	const ULONGLONG day = 24ULL * 3600 * 10000000;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !isHash(fd.cFileName)) continue;
		ULARGE_INTEGER t;
		t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
		t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
		if (now.QuadPart < t.QuadPart || now.QuadPart - t.QuadPart < day) continue;
		const std::wstring dir = root + L"\\" + fd.cFileName;
		WIN32_FIND_DATAW f;
		HANDLE g = FindFirstFileW((dir + L"\\*").c_str(), &f);
		if (g != INVALID_HANDLE_VALUE) {
			do {
				if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + f.cFileName).c_str());
			} while (FindNextFileW(g, &f));
			FindClose(g);
		}
		RemoveDirectoryW(dir.c_str());
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

}  // namespace

bool isLink(const std::string& arg) { return arg.size() > 11 && lowerCase(arg.substr(0, 11)) == "cedarlogic:"; }

std::string fileForLink(const std::string& link, std::string& why) {
	why.clear();
	std::string data, name, text;
	if (!parse(link, data, name)) {
		why = "not a CedarLogic link";
		return std::string();
	}
	if (!decode(data, text, why)) return std::string();
	if (text.find("circuit") == std::string::npos && text.find("cedarlogic") == std::string::npos) {
		why = "not a circuit";
		return std::string();
	}
	const std::string root = cacheRoot();
	tidyCache(W(root));
	const std::string dir = root + "\\" + folderName(data);
	const int made = SHCreateDirectoryExW(nullptr, W(dir).c_str(), nullptr);
	if (made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS && made != ERROR_FILE_EXISTS) {
		why = "couldn't write the circuit";
		return std::string();
	}
	const std::string path = dir + "\\" + fileName(name) + ".cdl";
	if (readText(path) != text && !writeText(path, text)) {
		why = "couldn't write the circuit";
		return std::string();
	}
	return path;
}

bool isCacheFile(const std::string& path) {
	const std::string root = lowerCase(cacheRoot()) + "\\";
	return lowerCase(path).compare(0, root.size(), root) == 0;
}

void showProblem(HWND parent, const std::string& why) {
	std::string text = "It isn't a CedarLogic circuit link, or it was cut short when it was copied.";
	if (why == "too big to be a circuit") text = "That circuit is too big to open from a link. Ask for the .cdl file instead, and use File \u25B8 Import.";
	else if (why == "couldn't write the circuit") text = "CedarLogic couldn't save the circuit from the link on this computer. Is the disk full?";
	showMessage(parent, Tone::Error, "That link couldn't be opened", text);
}

void copyLink(CircuitWindow* window, const std::string& text, const std::string& name) {
	const std::string data = encode(text);
	if (data.empty()) {
		window->note("Couldn't make a link for this circuit.");
		return;
	}
	const std::string link = std::string(kWebBase) + "#" + fragment(data, name == "Untitled" ? std::string() : name);
	if (link.size() > kMaxWebLink) {
		showMessage(window->window(), Tone::Info, "This circuit is too big for a link",
		            strf("A link holds the whole circuit, and this one would be about %d KB. Use File \u25B8 Export\u2026 and send the file instead.",
		                 (int)(link.size() / 1024)));
		return;
	}
	if (!setClipboardText(window->window(), link)) {
		showMessage(window->window(), Tone::Warning, "The link couldn't be copied",
		            "Windows wouldn't let CedarLogic use the clipboard just now. Try again.");
		return;
	}
	window->note("Link copied. Anyone who opens it gets this circuit; it isn't stored anywhere.");
}

static bool selfTestFiles(std::string& report) {
	int failures = 0;
	auto check = [&](bool ok, const std::string& what) {
		report += strf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
		if (!ok) failures++;
	};
	auto linkTo = [](const std::string& text, const std::string& name) {
		return "cedarlogic://open#" + fragment(encode(text), name);
	};
	const std::string root = lowerCase(cacheRoot()) + "\\";
	auto endsWith = [](const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; };
	auto inCache = [&](const std::string& path) { return lowerCase(path).compare(0, root.size(), root) == 0 && path.find("..\\") == std::string::npos; };
	const std::string circuit = "(cedarlogic (version 3) (page 0 (name \"link test " + strf("%lu", (unsigned long)GetTickCount()) + "\")))";
	std::string why;
	const std::string path = fileForLink(linkTo(circuit, "Half adder"), why);
	check(!path.empty() && inCache(path), "a cedarlogic:// link becomes a file in the cache: " + path);
	check(endsWith(path, "\\Half adder.cdl"), "...named for the circuit");
	check(readText(path) == circuit, "...holding the circuit");
	check(fileForLink(linkTo(circuit, "Half adder"), why) == path, "...the same file every time");
	check(fileForLink("CEDARLOGIC://open#" + fragment(encode(circuit), "Half adder"), why) == path && isLink("CEDARLOGIC://open#c=x") && isLink("cedarlogic:open?c=x"),
	      "...whatever the case of the scheme");
	check(!isLink("C:\\Circuits\\a.cdl") && !isLink("--screenshot") && !isLink("cedarlogic"), "a path or an option isn't a link");
	check(fileForLink("cedarlogic://open#c=AAAA", why).empty() && !why.empty(), "a broken link says why: " + why);
	check(fileForLink("cedarlogic://open", why).empty() && why == "not a CedarLogic link", "a link with no circuit in it is refused");
	check(fileForLink(linkTo("hello there", "x"), why).empty() && why == "not a circuit", "text that isn't a circuit is refused");
	const std::string evil = fileForLink(linkTo(circuit + "1", "..\\..\\x:y*z?"), why);
	check(!evil.empty() && inCache(evil) && evil.find('?') == std::string::npos && evil.find('*') == std::string::npos,
	      "a name with slashes and colons stays in its folder: " + evil);
	const std::string device = fileForLink(linkTo(circuit + "2", "con"), why);
	check(endsWith(device, "\\_con.cdl"), "a name that's a device's isn't one: " + device);
	const std::string dots = fileForLink(linkTo(circuit + "3", "Ends with dots..."), why);
	check(endsWith(dots, "\\Ends with dots.cdl"), "a name ending in dots loses them: " + dots);
	const std::string plain = fileForLink("cedarlogic://open#" + fragment(encode(circuit + "4"), ""), why);
	check(endsWith(plain, "\\Shared circuit.cdl"), "a link without a name makes \"Shared circuit\"");
	check(isCacheFile(plain) && !isCacheFile("C:\\Circuits\\a.cdl") && !isCacheFile(root.substr(0, root.size() - 1) + "x\\a.cdl"), "a file in the cache is known for one");
	report += failures ? strf("%d share link file checks failed\n", failures) : std::string("share link file checks: all passed\n");
	return failures == 0;
}

bool runSelfTest(std::string& report, const std::vector<std::string>& paths) {
	bool ok = true;
	std::string circuit, given, made;
	if (!paths.empty()) {
		circuit = readText(paths[0]);
		if (circuit.empty()) {
			report += "FAIL  couldn't read " + paths[0] + "\n";
			ok = false;
		}
	}
	if (paths.size() > 2) {
		given = readText(paths[2]);
		const size_t end = given.find_last_not_of(" \t\r\n");
		given.resize(end == std::string::npos ? 0 : end + 1);
	}
	ok = selfTest(report, circuit, &made, given) && ok;
	ok = selfTestFiles(report) && ok;
	if (paths.size() > 1 && !made.empty() && !writeText(paths[1], made)) {
		report += "FAIL  couldn't write " + paths[1] + "\n";
		ok = false;
	}
	return ok;
}

}  // namespace sharelink
