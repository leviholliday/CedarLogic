#!/usr/bin/env python3
"""A stand-in for GitHub for the Windows app's --channel-check (Updater.h).

    python update-channel-server.py <folder> <port file>

Serves <folder> over HTTP on 127.0.0.1, at GitHub's paths: a GET for
/repos/O/R/releases?per_page=100 answers with the file
<folder>/repos/O/R/releases, /O/R/releases/download/T/N with
<folder>/O/R/releases/download/T/N, and so on (the query is ignored).
Anything missing is a 404. The app writes the fake releases into the
folder itself, case by case; this only serves what's there when asked.
The port it listens on goes into <port file> once it's ready.
"""

import http.server
import os
import sys
import urllib.parse


def main():
    root = os.path.abspath(sys.argv[1])
    port_file = sys.argv[2]
    os.makedirs(root, exist_ok=True)

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
            parts = [p for p in path.split("/") if p]
            body = None
            if parts and not any(p in (".", "..") or "\\" in p or ":" in p for p in parts):
                f = os.path.join(root, *parts)
                if os.path.isfile(f):
                    with open(f, "rb") as h:
                        body = h.read()
            if body is None:
                body = b'{"message":"Not Found"}'
                self.send_response(404)
            else:
                self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
            self.close_connection = True

        def log_message(self, fmt, *args):
            sys.stderr.write("channel server: " + (fmt % args) + "\n")

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tmp = port_file + ".tmp"
    with open(tmp, "w") as h:
        h.write(str(server.server_address[1]))
    os.replace(tmp, port_file)
    server.serve_forever()


if __name__ == "__main__":
    main()
