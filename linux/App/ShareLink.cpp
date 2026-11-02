#include "ShareLink.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <vector>

namespace sharelink {

const char* const kWebBase = "https://cedarlogic.netlify.app/online-logic-gate-simulator/";

namespace {

// Everything a converter makes from `in`; false if it refuses the data.
bool convertAll(GConverter* conv, const std::string& in, std::string& out, size_t limit) {
	std::vector<char> buf(1 << 16);
	size_t used = 0;
	out.clear();
	for (;;) {
		gsize read = 0, written = 0;
		GError* err = nullptr;
		const GConverterResult r = g_converter_convert(conv, in.data() + used, in.size() - used, buf.data(), buf.size(),
		                                               G_CONVERTER_INPUT_AT_END, &read, &written, &err);
		if (err) { g_error_free(err); return false; }
		used += read;
		out.append(buf.data(), written);
		if (out.size() > limit) return false;
		if (r == G_CONVERTER_FINISHED) return true;
		// (No progress at all: it wants more than is there.)
		if (read == 0 && written == 0) return false;
	}
}

std::string toBase64url(const std::string& bytes) {
	gchar* b = g_base64_encode((const guchar*)bytes.data(), bytes.size());
	std::string s = b;
	g_free(b);
	std::string out;
	for (char c : s) {
		if (c == '=') continue;
		out += c == '+' ? '-' : c == '/' ? '_' : c;
	}
	return out;
}

bool fromBase64url(const std::string& s, std::string& bytes) {
	std::string t;
	for (char c : s) {
		if (g_ascii_isspace(c) || c == '=') continue;
		if (c == '-') c = '+';
		else if (c == '_') c = '/';
		if (!g_ascii_isalnum(c) && c != '+' && c != '/') return false;
		t += c;
	}
	if (t.size() % 4 == 1) return false;
	while (t.size() % 4) t += '=';
	gsize n = 0;
	guchar* d = g_base64_decode(t.c_str(), &n);
	if (!d) return false;
	bytes.assign((const char*)d, n);
	g_free(d);
	return !bytes.empty();
}

}  // namespace

std::string encode(const std::string& text) {
	if (text.empty()) return "";
	GConverter* conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW, 9));
	std::string packed;
	const bool ok = convertAll(conv, text, packed, text.size() + text.size() / 8 + 4096);
	g_object_unref(conv);
	return ok ? toBase64url(packed) : "";
}

bool decode(const std::string& data, std::string& text, std::string& why) {
	std::string packed;
	if (!fromBase64url(data, packed)) { why = "not a CedarLogic link"; return false; }
	GConverter* conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW));
	const bool ok = convertAll(conv, packed, text, kMaxText);
	g_object_unref(conv);
	if (!ok) { why = text.size() > kMaxText ? "too big to be a circuit" : "the link was cut short"; text.clear(); return false; }
	if (!g_utf8_validate(text.data(), (gssize)text.size(), nullptr)) { why = "not text"; text.clear(); return false; }
	return true;
}

bool parse(const std::string& link, std::string& data, std::string& name) {
	const size_t cut = link.find_first_of("#?");
	if (cut == std::string::npos) return false;
	data.clear();
	name.clear();
	size_t i = cut + 1;
	while (i <= link.size()) {
		size_t e = link.find('&', i);
		if (e == std::string::npos) e = link.size();
		const std::string pair = link.substr(i, e - i);
		if (pair.compare(0, 2, "c=") == 0) data = pair.substr(2);
		else if (pair.compare(0, 2, "n=") == 0) {
			gchar* n = g_uri_unescape_string(pair.c_str() + 2, nullptr);
			name = n ? n : "";
			g_free(n);
		}
		i = e + 1;
	}
	if (data.empty()) return false;
	if (name.size() > 80) name.resize(80);
	return true;
}

std::string fragment(const std::string& data, const std::string& name) {
	std::string f = "c=" + data;
	if (!name.empty()) {
		gchar* n = g_uri_escape_string(name.c_str(), nullptr, FALSE);
		f += std::string("&n=") + n;
		g_free(n);
	}
	return f;
}

std::string fileForLink(const std::string& link, std::string& why) {
	std::string data, name, text;
	if (!parse(link, data, name) || !decode(data, text, why)) { if (why.empty()) why = "not a CedarLogic link"; return ""; }
	if (text.find("circuit") == std::string::npos && text.find("cedarlogic") == std::string::npos) { why = "not a circuit"; return ""; }
	// A file name: no slashes, no dots at the start.
	std::string base;
	for (char c : name) base += (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 32) ? '-' : c;
	while (!base.empty() && base[0] == '.') base.erase(0, 1);
	if (base.empty()) base = "Shared circuit";
	gchar* hash = g_compute_checksum_for_string(G_CHECKSUM_SHA1, data.c_str(), (gssize)data.size());
	const std::string root = std::string(g_get_user_cache_dir()) + "/cedarlogic/links";
	// (Links from earlier than a day ago go.)
	if (GDir* d = g_dir_open(root.c_str(), 0, nullptr)) {
		const gint64 now = g_get_real_time() / 1000000;
		while (const gchar* e = g_dir_read_name(d)) {
			const std::string p = root + "/" + e;
			GStatBuf st;
			if (g_stat(p.c_str(), &st) == 0 && now - (gint64)st.st_mtime > 86400) {
				g_remove((p + "/" + base + ".cdl").c_str());
				g_rmdir(p.c_str());
			}
		}
		g_dir_close(d);
	}
	const std::string dir = root + "/" + std::string(hash).substr(0, 16);
	g_free(hash);
	if (g_mkdir_with_parents(dir.c_str(), 0700) != 0) { why = "couldn't write the circuit"; return ""; }
	const std::string path = dir + "/" + base + ".cdl";
	if (!g_file_set_contents(path.c_str(), text.data(), (gssize)text.size(), nullptr)) { why = "couldn't write the circuit"; return ""; }
	return path;
}

}  // namespace sharelink
