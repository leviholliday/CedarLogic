// A blocking HTTP request over WinHTTP, for the sync engine (SyncPlatform.cpp):
// the request's own headers and the response's headers, a body of up to a few
// megabytes, timeouts, and no redirects (a redirect would carry the sign-in
// header to wherever it pointed). Feedback.cpp has its own, simpler one.
//
// HTTPS only, except to this computer (localhost, 127.0.0.1, ::1), which a
// CL_SYNC_URL for testing against the mock server may name.

#ifndef CL_WINDOWS_HTTP_H
#define CL_WINDOWS_HTTP_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace http {

struct Request {
	std::string method;   // GET PUT POST DELETE
	std::string url;      // absolute
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
};

struct Response {
	bool sent = false;    // the whole request went out (the server may have acted on it)
	int status = 0;       // 0: no answer (offline, a timeout, TLS failed, the URL wasn't allowed)
	std::map<std::string, std::string> headers;   // names in lower case
	std::string body;
};

// https://..., or http:// to localhost, 127.0.0.1 or ::1.
bool urlAllowed(const std::string& url);

// Waits for the answer (20 s to connect, 60 s in all for each step) and
// reads at most `maxBody` bytes of it. Never throws.
Response send(const Request& r, size_t maxBody = 8u << 20);

// The raw header block ("HTTP/1.1 200 OK\r\nname: value\r\n...") as a map.
std::map<std::string, std::string> parseHeaders(const std::string& raw);

}  // namespace http

#endif  // CL_WINDOWS_HTTP_H
