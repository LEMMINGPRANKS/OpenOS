#!/usr/bin/env python3
"""spgk server: serves OpenOS packages to the guest over QEMU user-net.

Run this on the host before `getspgk` inside OpenOS:

    python3 tools/spgk-server.py

The guest reaches it at 10.0.2.2:8080 (the user-net gateway).
Special paths: /index lists every file in packages/, /news serves
packages/news.html, /updates is the feature-update index page,
/updates/<name> downloads an update from packages/updates/, and
/version is the current feature-pack version.
/comments and /roadmap power the Internet app's Comments + Ideas tabs:
GET renders them as HTML, POST appends a line (form field "text") and
answers with the refreshed page.
"""
import os
import time
import json
import urllib.parse
import urllib.request
from http.server import HTTPServer, BaseHTTPRequestHandler

PKG_DIR = os.path.join(os.path.dirname(__file__), "..", "packages")
UPD_DIR = os.path.join(PKG_DIR, "updates")
KDIR = os.path.join(PKG_DIR, "kernel")
COMMENTS = os.path.join(PKG_DIR, "comments.txt")
ROADMAP = os.path.join(PKG_DIR, "roadmap.txt")
VERSION = "unknown"
try:
    with open(os.path.join(os.path.dirname(__file__), "..", "kernel", "version.h"),
              encoding="utf-8") as _f:
        for _line in _f:
            if _line.startswith("#define OS_VERSION"):
                VERSION = _line.split('"')[1]
                break
except OSError:
    pass


def stable_version():
    """The version the stable channel serves (packages/kernel/stable)."""
    try:
        with open(os.path.join(KDIR, "stable"), encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return ""


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


def render_list(path, title, post_to, subtitle):
    lines = []
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            lines = [l.rstrip("\n") for l in f if l.strip()]
    rows = "\n".join(f"<li>{l}</li>" for l in lines) or "<li>(nothing yet)</li>"
    return (f"<html><head><title>{title}</title></head><body>"
            f"<h1>{title}</h1>"
            f"<p>{subtitle}. POST text=... here to add yours.</p>"
            f"<ul>{rows}</ul>"
            f"<p><a href='/news'>Back to the news</a></p>"
            f"</body></html>").encode()


def append_line(path, text):
    stamp = time.strftime("%Y-%m-%d %H:%M")
    with open(path, "a", encoding="utf-8") as f:
        f.write(f"[{stamp}] {text}\n")


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;").replace("'", "&#39;"))


def _fetch_json(url, timeout):
    # wikipedia 403s requests without a User-Agent, so we always send one
    req = urllib.request.Request(url, headers={
        "User-Agent": "OpenOS/1.6 (Freddie's from-scratch OS; contact via the "
                      "OpenOS Internet app comments tab)"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def _ddg_part(q):
    try:
        url = "https://api.duckduckgo.com/?" + urllib.parse.urlencode(
            {"q": q, "format": "json", "no_html": "1", "skip_disambig": "1"})
        data = _fetch_json(url, 4)
    except Exception as e:
        return f"<p>(duckduckgo did not answer: {esc(e)})</p>", 0
    out, hits = [], 0
    if data.get("AbstractText"):
        out.append(f"<p><b>{esc(data.get('AbstractSource') or 'Answer')}:</b> "
                   f"{esc(data['AbstractText'])}</p>")
        if data.get("AbstractURL"):
            out.append(f"<p><a href='{esc(data['AbstractURL'])}'>read more about "
                       f"{esc(q)}</a></p>")
            hits += 1
    rel = []
    for t in data.get("RelatedTopics", []):
        if isinstance(t, dict) and t.get("FirstURL") and t.get("Text"):
            rel.append((t["FirstURL"], t["Text"]))
        elif isinstance(t, dict):
            for s in t.get("Topics", []):
                if s.get("FirstURL") and s.get("Text"):
                    rel.append((s["FirstURL"], s["Text"]))
    for u, txt in rel[:8]:
        out.append(f"<p><a href='{esc(u)}'>{esc(txt)}</a></p>")
        hits += 1
    return "".join(out), hits


def _wiki_part(q):
    try:
        url = ("https://en.wikipedia.org/w/api.php?" + urllib.parse.urlencode(
            {"action": "opensearch", "search": q, "limit": "5", "format": "json"}))
        j = _fetch_json(url, 4)
    except Exception as e:
        return f"<p>(wikipedia did not answer: {esc(e)})</p>", 0
    if not (len(j) == 4 and j[1]):
        return "", 0
    out = ["<h2>Wikipedia</h2>"]
    for title, dsc, u in zip(j[1], j[2], j[3]):
        label = title + (" -- " + dsc if dsc else "")
        out.append(f"<p><a href='{esc(u)}'>{esc(label)}</a></p>")
    return "".join(out), len(j[1])


_search_cache = {}          # q -> html bytes; repeated searches answer instantly
_SEARCH_CACHE_MAX = 32


def web_search(q):
    """The OpenOS search engine: DuckDuckGo instant answers + Wikipedia,
    fetched in parallel so OpenOS never waits twice."""
    cached = _search_cache.get(q)
    if cached is not None:
        return cached
    import concurrent.futures
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as ex:
        f1 = ex.submit(_ddg_part, q)
        f2 = ex.submit(_wiki_part, q)
        dhtml, dhits = f1.result()
        whtml, whits = f2.result()
    parts = [f"<html><head><title>Search: {esc(q)}</title></head><body>",
             f"<h1>Results for: {esc(q)}</h1>", dhtml, whtml]
    if not (dhits + whits):
        parts.append("<p>no results. try different words?</p>")
    parts.append("<p><a href='/news'>back to OpenOS news</a></p></body></html>")
    out = "".join(parts).encode()
    if len(_search_cache) >= _SEARCH_CACHE_MAX:
        _search_cache.pop(next(iter(_search_cache)))
    _search_cache[q] = out
    return out


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        name = parsed.path.lstrip("/")
        if name == "search":
            qs = urllib.parse.parse_qs(parsed.query)
            q = (qs.get("q") or [""])[0].strip()
            if q:
                print(f"spgk: search: {q!r}")
                send(self, web_search(q), "text/html")
            else:
                not_found(self, "search for what?")
            return
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
        if name == "kernel/versions":
            st = stable_version()
            vers = []
            try:
                for f in sorted(os.listdir(KDIR)):
                    if f.startswith("v") and os.path.isdir(os.path.join(KDIR, f)):
                        vers.append(f[1:])
            except OSError:
                pass
            body = "".join(
                f"{v} {'stable' if v == st else 'unstable'}\n" for v in vers)
            send(self, body.encode(), "text/plain")
            return
        # the classic single-kernel paths serve whatever the stable channel
        # points at, so older OpenOS updaters keep working unchanged
        if name in ("kernel/manifest.txt", "kernel/kernel.flat"):
            st = stable_version()
            if not st or not os.path.isfile(os.path.join(KDIR, "v" + st,
                                                        name.split("/")[-1])):
                not_found(self, "no stable kernel is set")
                return
            with open(os.path.join(KDIR, "v" + st, name.split("/")[-1]), "rb") as f:
                send(self, f.read(), "application/octet-stream")
            return
        if name == "comments":
            send(self, render_list(COMMENTS, "Comments", "/comments",
                                   "what people said about new updates"),
                 "text/html")
            return
        if name == "roadmap":
            send(self, render_list(ROADMAP, "Ideas to improve OpenOS",
                                   "/roadmap", "the plan + fresh ideas"),
                 "text/html")
            return
        path = os.path.normpath(os.path.join(PKG_DIR, name))
        if not path.startswith(os.path.abspath(PKG_DIR)) or not os.path.isfile(path):
            not_found(self, "no such package")
            return
        with open(path, "rb") as f:
            send(self, f.read(), "application/octet-stream")

    def do_POST(self):
        name = self.path.lstrip("/")
        if name not in ("comments", "roadmap"):
            not_found(self, "no such page")
            return
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode("utf-8", "replace")
        form = urllib.parse.parse_qs(body)
        text = (form.get("text") or [""])[0].strip()[:200]
        if text:
            append_line(COMMENTS if name == "comments" else ROADMAP, text)
            print(f"spgk: new {name} entry: {text!r}")
            page = render_list(COMMENTS if name == "comments" else ROADMAP,
                               "Comments" if name == "comments"
                               else "Ideas to improve OpenOS",
                               "/" + name, "thanks!")
            send(self, page, "text/html")
        else:
            not_found(self, "empty text")

    def log_message(self, fmt, *args):
        print("spgk:", fmt % args)


if __name__ == "__main__":
    os.makedirs(UPD_DIR, exist_ok=True)
    print("spgk server on 0.0.0.0:8080, packages from", os.path.abspath(PKG_DIR))
    print("updates from", os.path.abspath(UPD_DIR), "- feature pack", VERSION)
    HTTPServer(("0.0.0.0", 8080), Handler).serve_forever()
