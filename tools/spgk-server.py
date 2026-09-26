#!/usr/bin/env python3
"""spgk server: serves OpenOS packages to the guest over QEMU user-net.

Run this on the host before `getspgk` inside OpenOS:

    python3 tools/spgk-server.py

The guest reaches it at 10.0.2.2:8080 (the user-net gateway).
Special paths: /index lists every file in packages/, /news serves
packages/news.html, /updates is the feature-update index page,
/updates/<name> downloads an update from packages/updates/, and
/version is the current feature-pack version.
"""
import os
from http.server import HTTPServer, BaseHTTPRequestHandler

PKG_DIR = os.path.join(os.path.dirname(__file__), "..", "packages")
UPD_DIR = os.path.join(PKG_DIR, "updates")
VERSION = "1.0.3"


def send(self, body, kind):
    self.send_response(200)
    self.send_header("Content-Type", kind)
    self.send_header("Content-Length", str(len(body)))
    self.end_headers()
    self.wfile.write(body)


def not_found(self, msg):
    self.send_response(404)
    body = msg.encode() + b"\n"
    self.send_header("Content-Length", str(len(body)))
    self.end_headers()
    self.wfile.write(body)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        name = self.path.lstrip("/")
        if name == "version":
            send(self, VERSION.encode(), "text/plain")
            return
        if name == "updates":
            try:
                files = sorted(f for f in os.listdir(UPD_DIR)
                               if not f.startswith(".") and f != "index")
            except OSError:
                files = []
            rows = "\n".join(
                f"<li><a href='/updates/{f}'>{f}</a> -- click to install, "
                f"or run <b>update</b> in the shell</li>" for f in files)
            page = (f"<html><head><title>OpenOS Updates</title></head><body>"
                    f"<h1>Feature updates</h1>"
                    f"<p>Server feature-pack version: {VERSION}. Click a "
                    f"package to install it -- it lands on your desktop "
                    f"straight away, no reboot needed.</p>"
                    f"<ul>{rows}</ul>"
                    f"<p><a href='/news'>Back to the news</a></p>"
                    f"</body></html>").encode()
            send(self, page, "text/html")
            return
        if name == "updates/index":
            try:
                files = sorted(f for f in os.listdir(UPD_DIR)
                               if not f.startswith("."))
            except OSError:
                files = []
            send(self, ("\n".join(files) + "\n").encode(), "text/plain")
            return
        if name.startswith("updates/"):
            path = os.path.normpath(os.path.join(UPD_DIR, name[len("updates/"):]))
            if not path.startswith(os.path.abspath(UPD_DIR)) or not os.path.isfile(path):
                not_found(self, "no such update")
                return
            with open(path, "rb") as f:
                send(self, f.read(), "application/octet-stream")
            return
        if name == "index":
            names = sorted(os.listdir(PKG_DIR))
            body = ("\n".join(n for n in names if not n.startswith(".")) + "\n").encode()
            send(self, body, "text/plain")
            return
        if name == "news":
            path = os.path.join(PKG_DIR, "news.html")
            if not os.path.isfile(path):
                not_found(self, "no news yet")
                return
            with open(path, "rb") as f:
                send(self, f.read(), "text/html")
            return
        path = os.path.normpath(os.path.join(PKG_DIR, name))
        if not path.startswith(os.path.abspath(PKG_DIR)) or not os.path.isfile(path):
            not_found(self, "no such package")
            return
        with open(path, "rb") as f:
            send(self, f.read(), "application/octet-stream")

    def log_message(self, fmt, *args):
        print("spgk:", fmt % args)


if __name__ == "__main__":
    os.makedirs(UPD_DIR, exist_ok=True)
    print("spgk server on 0.0.0.0:8080, packages from", os.path.abspath(PKG_DIR))
    print("updates from", os.path.abspath(UPD_DIR), "- feature pack", VERSION)
    HTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
