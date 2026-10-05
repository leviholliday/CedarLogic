// A blocking HTTP request over WinHTTP (see Http.h).

#include "Http.h"
#include "App.h"

#include <winhttp.h>

#include <algorithm>
#include <mutex>

namespace http {

namespace {

// One session for the whole run: WinHTTP keeps its connections to the site
// open between requests, so a sync's many small ones don't each pay for a TLS
// handshake.
HINTERNET session() {
	static std::mutex lock;
	static HINTERNET s = nullptr;
	std::lock_guard<std::mutex> guard(lock);
	if (s == nullptr) {
		s = WinHttpOpen(L"CedarLogic", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (s) {
			WinHttpSetTimeouts(s, 20000, 20000, 60000, 60000);
			// Never follow a redirect: the sign-in header would go along.
			DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
			WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy);
		}
	}
	return s;
}

bool plainText(const std::string& s) {
	for (unsigned char c : s)
		if (c < 0x20 || c == 0x7f) return false;   // no line breaks, which would start another header
	return true;
}

bool localHost(const std::wstring& host) {
	std::wstring h = host;
	for (wchar_t& c : h) c = (wchar_t)towlower(c);
	return h == L"localhost" || h == L"127.0.0.1" || h == L"::1" || h == L"[::1]";
}

}  // namespace

bool urlAllowed(const std::string& url) {
	URL_COMPONENTS u = {};
	u.dwStructSize = sizeof u;
	wchar_t host[256] = L"";
	u.lpszHostName = host;
	u.dwHostNameLength = 256;
	u.dwSchemeLength = (DWORD)-1;
	u.dwUrlPathLength = (DWORD)-1;
	u.dwExtraInfoLength = (DWORD)-1;
	const std::wstring w = W(url);
	if (!WinHttpCrackUrl(w.c_str(), 0, 0, &u)) return false;
	if (u.nScheme == INTERNET_SCHEME_HTTPS) return true;
	return u.nScheme == INTERNET_SCHEME_HTTP && localHost(host);
}

std::map<std::string, std::string> parseHeaders(const std::string& raw) {
	std::map<std::string, std::string> out;
	size_t at = 0;
	while (at < raw.size()) {
		size_t end = raw.find("\r\n", at);
		if (end == std::string::npos) end = raw.size();
		const std::string line = raw.substr(at, end - at);
		const size_t colon = line.find(':');
		if (colon != std::string::npos && colon > 0) {
			std::string name = line.substr(0, colon), value = line.substr(colon + 1);
			for (char& c : name) c = (char)tolower((unsigned char)c);
			const size_t a = value.find_first_not_of(" \t"), b = value.find_last_not_of(" \t");
			out[name] = a == std::string::npos ? std::string() : value.substr(a, b - a + 1);
		}
		at = end + 2;
	}
	return out;
}

Response send(const Request& r, size_t maxBody) {
	Response out;
	if (!urlAllowed(r.url)) return out;
	for (const auto& h : r.headers)
		if (!plainText(h.first) || !plainText(h.second) || h.first.find(':') != std::string::npos) return out;
	HINTERNET ses = session();
	if (ses == nullptr) return out;

	URL_COMPONENTS u = {};
	u.dwStructSize = sizeof u;
	wchar_t host[256] = L"", path[4096] = L"", extra[4096] = L"";
	u.lpszHostName = host;
	u.dwHostNameLength = 256;
	u.lpszUrlPath = path;
	u.dwUrlPathLength = 4096;
	u.lpszExtraInfo = extra;
	u.dwExtraInfoLength = 4096;
	u.dwSchemeLength = (DWORD)-1;
	const std::wstring url = W(r.url);
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return out;
	const std::wstring target = std::wstring(path) + extra;

	HINTERNET connect = WinHttpConnect(ses, host, u.nPort, 0);
	HINTERNET req = connect ? WinHttpOpenRequest(connect, W(r.method).c_str(), target.empty() ? L"/" : target.c_str(), nullptr,
	                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                             u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
	                        : nullptr;
	if (req) {
		std::string head;
		for (const auto& h : r.headers) head += h.first + ": " + h.second + "\r\n";
		const std::wstring headers = W(head);
		const bool hasBody = !r.body.empty();
		// (Header text and body go in as given; the length is stated so a big
		// body is one request, not a chunked one.)
		out.sent = WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
		                              headers.empty() ? 0 : (DWORD)-1L, hasBody ? (LPVOID)r.body.data() : WINHTTP_NO_REQUEST_DATA,
		                              (DWORD)r.body.size(), (DWORD)r.body.size(), 0) != FALSE;
		if (out.sent && WinHttpReceiveResponse(req, nullptr)) {
			DWORD code = 0, size = sizeof code;
			if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size,
			                        WINHTTP_NO_HEADER_INDEX)) {
				// The headers, as one block.
				DWORD bytes = 0;
				WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &bytes,
				                    WINHTTP_NO_HEADER_INDEX);
				if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes > 0 && bytes < (1u << 20)) {
					std::wstring raw(bytes / sizeof(wchar_t) + 1, L'\0');
					if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &raw[0], &bytes,
					                        WINHTTP_NO_HEADER_INDEX)) {
						raw.resize(bytes / sizeof(wchar_t));
						out.headers = parseHeaders(U(raw));
					}
				}
				// The body, up to the limit; one that's longer is an answer that
				// can't be used, as no answer is.
				std::string body;
				bool whole = true;
				std::vector<char> chunk(16384);
				for (;;) {
					DWORD got = 0;
					if (!WinHttpReadData(req, chunk.data(), (DWORD)chunk.size(), &got)) { whole = false; break; }
					if (got == 0) break;
					body.append(chunk.data(), got);
					if (body.size() > maxBody) { whole = false; break; }
				}
				if (whole) {
					out.status = (int)code;
					out.body = std::move(body);
				} else {
					out.headers.clear();
				}
			}
		}
	}
	if (req) WinHttpCloseHandle(req);
	if (connect) WinHttpCloseHandle(connect);
	return out;
}

}  // namespace http
