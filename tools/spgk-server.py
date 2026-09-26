#!/usr/bin/env python3
"""spgk server: serves OpenOS packages to the guest over QEMU user-net.

Run this on the host before `getspgk` inside OpenOS:

    python3 tools/spgk-server.py

The guest reaches it at 10.0.2.2:8080 (the user-net gateway).
Special paths: /index lists every file in packages/.
"""
import os
from http.server import HTTPServer, BaseHTTPRequestHandler

PKG_DIR = os.path.join(os.path.dirname(__file__), "..", "packages")


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        name = self.path.lstrip("/")
        if name == "index":
            names = sorted(os.listdir(PKG_DIR))
            body = ("\n".join(n for n in names if not n.startswith(".")) + "\n").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if name == "news":
            path = os.path.join(PKG_DIR, "news.html")
            if not os.path.isfile(path):
                self.send_response(404)
                body = b"no news yet\n"
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            with open(path, "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        path = os.path.normpath(os.path.join(PKG_DIR, name))
        if not path.startswith(os.path.abspath(PKG_DIR)) or not os.path.isfile(path):
            self.send_response(404)
            body = b"no such package\n"
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        with open(path, "rb") as f:
            body = f.read()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        print("spgk:", fmt % args)


if __name__ == "__main__":
    if not os.path.isdir(PKG_DIR):
        os.makedirs(PKG_DIR)
    print("spgk server on 0.0.0.0:8080, packages from", os.path.abspath(PKG_DIR))
    HTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
