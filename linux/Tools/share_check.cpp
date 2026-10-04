// Checks of App/ShareLink.cpp (GLib only): round trips, a link made by
// python's zlib (the website's fixtures), a link turned into a file.
//   share_check [<link file> <the .cdl it holds>] [<file to write a link to>]
#include "../App/ShareLink.h"

#include <glib.h>
#include <stdio.h>
#include <string>

static int fails = 0;
static void ok(bool c, const std::string& m) { printf("%s%s\n", c ? "ok   " : "FAIL ", m.c_str()); if (!c) fails++; }
static std::string slurp(const char* path) {
	gchar* b = nullptr; gsize n = 0;
	if (!g_file_get_contents(path, &b, &n, nullptr)) { printf("FAIL can't read %s\n", path); exit(1); }
	std::string s(b, n); g_free(b);
	return s;
}
static std::string trim(std::string s) { while (!s.empty() && g_ascii_isspace(s.back())) s.pop_back(); return s; }

int main(int argc, char** argv) {
	std::string big;
	for (int i = 0; i < 20000; i++) big += "gate AND 12 34\n";
	const std::string texts[] = { "(cedarlogic (version 3) (page 0 (name \"é ✓ 日本\")))", big, "x" };
	for (const std::string& t : texts) {
		std::string back, why;
		const std::string d = sharelink::encode(t);
		ok(!d.empty() && sharelink::decode(d, back, why) && back == t, "round trip of " + std::to_string(t.size()) + " bytes");
		ok(d.find_first_of("+/=") == std::string::npos, "...base64url, no padding");
	}
	ok(sharelink::encode("").empty(), "nothing to encode makes no link");
	std::string data, name;
	ok(sharelink::parse("https://x/online/#c=abc&n=A%20B", data, name) && data == "abc" && name == "A B", "parse reads the data and the name");
	ok(sharelink::parse("cedarlogic://open?c=xyz", data, name) && data == "xyz", "...and the ? form");
	ok(!sharelink::parse("cedarlogic://open", data, name) && !sharelink::parse("https://x/#compare", data, name), "...and refuses what has no data");
	ok(sharelink::fragment("abc", "Half adder & more") == "c=abc&n=Half%20adder%20%26%20more", "fragment encodes the name");
	for (const char* bad : { "not*base64", "AAAA", "", "QUJD" }) {
		std::string t, why;
		ok(!sharelink::decode(bad, t, why), std::string("garbage '") + bad + "' is refused");
	}
	{
		std::string t, why;
		const std::string cut = sharelink::encode(big).substr(0, 40);
		ok(!sharelink::decode(cut, t, why), "a link cut short is refused");
	}
	{
		g_setenv("XDG_CACHE_HOME", g_get_tmp_dir(), TRUE);
		std::string why;
		const std::string link = std::string("cedarlogic://open#") + sharelink::fragment(sharelink::encode("(cedarlogic (version 3))"), "Half adder");
		const std::string path = sharelink::fileForLink(link, why);
		ok(!path.empty() && g_str_has_suffix(path.c_str(), "/Half adder.cdl") && slurp(path.c_str()) == "(cedarlogic (version 3))", "a cedarlogic:// link becomes Half adder.cdl");
		ok(sharelink::fileForLink(link, why) == path, "...the same file every time");
		ok(sharelink::fileForLink("cedarlogic://open#c=AAAA", why).empty() && !why.empty(), "a broken link says why");
		ok(sharelink::fileForLink(std::string("cedarlogic://open#") + sharelink::fragment(sharelink::encode("hello"), "x"), why).empty(), "text that isn't a circuit is refused");
		const std::string evil = std::string("cedarlogic://open#") + sharelink::fragment(sharelink::encode("(cedarlogic)"), "../../x");
		const std::string p2 = sharelink::fileForLink(evil, why);
		ok(!p2.empty() && p2.find("/../") == std::string::npos && g_str_has_suffix(p2.c_str(), "/..-..-x.cdl") || g_str_has_suffix(p2.c_str(), "-..-x.cdl") , "a name with slashes stays in its folder");
	}
	if (argc >= 3) {
		const std::string want = slurp(argv[2]);
		std::string got, why, d, n;
		ok(sharelink::parse("#" + trim(slurp(argv[1])), d, n) && sharelink::decode(d, got, why) && got == want, "a link made by python's zlib decodes to the file (" + std::to_string(want.size()) + " bytes)");
		if (argc >= 4) g_file_set_contents(argv[3], sharelink::encode(want).c_str(), -1, nullptr);
	}
	printf(fails ? "%d failed\n" : "all good\n", fails);
	return fails ? 1 : 0;
}
