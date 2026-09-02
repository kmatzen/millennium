#!/usr/bin/env python3
"""Small local-only captive portal for Millennium Wi-Fi onboarding."""

import html
import json
import secrets
import socket
import time
import urllib.parse
from http import cookies
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SOCKET_PATH = "/run/millennium-wifi/helper.sock"
MAX_BODY = 4096
SESSIONS = set()
ATTEMPTS = {}

PAGE = """<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Millennium Wi-Fi Setup</title><style>
body{font:17px system-ui;max-width:34rem;margin:2rem auto;padding:0 1rem;background:#111;color:#eee}
h1{font-size:1.6rem}label{display:block;margin:1rem 0}.card{background:#222;padding:1rem;border-radius:.6rem}
input,select,button{box-sizing:border-box;width:100%;padding:.8rem;font:inherit;margin-top:.35rem}
button{background:#f2c94c;border:0;font-weight:700}.note{color:#bbb;font-size:.9rem}.error{color:#ff8b8b}</style></head>
<body><h1>Connect your Millennium Phone</h1><div class=card>{message}
<form method=post action=/connect><input type=hidden name=csrf value="{csrf}">
<label>Home Wi-Fi network<input name=ssid list=networks maxlength=32 required autocomplete=off></label>
<datalist id=networks>{network_options}</datalist>
<label>Security<select name=security><option value=wpa-psk>WPA2 / WPA3 Personal</option>
<option value=open>Open network (not recommended)</option></select></label>
<label>Wi-Fi password<input name=passphrase type=password maxlength=64 autocomplete=new-password></label>
<label><input style="width:auto" type=checkbox name=hidden value=1> Hidden network</label>
<button type=submit>Connect phone</button></form>
<p class=note>Your password stays on this phone and is never sent to kmatzen.com.</p></div></body></html>"""


def helper_request(message):
    payload = json.dumps(message, separators=(",", ":")).encode() + b"\n"
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(15)
        connection.connect(SOCKET_PATH)
        connection.sendall(payload)
        stream = connection.makefile("rb")
        line = stream.readline(65537)
    if not line or len(line) > 65536:
        raise RuntimeError("setup service did not respond")
    return json.loads(line)


class Portal(BaseHTTPRequestHandler):
    server_version = "MillenniumSetup/1"

    def log_message(self, fmt, *args):
        # Deliberately omit request bodies, queries, cookies, and credentials.
        print("portal: %s %s" % (self.command, self.path.split("?", 1)[0]), flush=True)

    def session(self):
        parsed = cookies.SimpleCookie(self.headers.get("Cookie", ""))
        token = parsed.get("millennium_setup")
        if token and token.value in SESSIONS:
            return token.value, False
        token = secrets.token_urlsafe(24)
        SESSIONS.add(token)
        return token, True

    def trusted_host(self):
        host = self.headers.get("Host", "").split(":", 1)[0].lower()
        return host in ("setup.millennium", "10.42.0.1")

    def send_page(self, status=200, message="<p>Select your home network.</p>"):
        token, fresh = self.session()
        try:
            result = helper_request({"action": "scan"})
            options = "".join('<option value="%s">' % html.escape(item["ssid"], quote=True)
                              for item in result.get("networks", []))
        except (OSError, RuntimeError, ValueError, json.JSONDecodeError):
            options = ""
        body = PAGE.format(csrf=html.escape(token), message=message,
                           network_options=options).encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'")
        self.send_header("X-Content-Type-Options", "nosniff")
        if fresh:
            self.send_header("Set-Cookie", "millennium_setup=%s; HttpOnly; SameSite=Strict; Path=/" % token)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if not self.trusted_host():
            self.send_response(302)
            self.send_header("Location", "http://setup.millennium/")
            self.end_headers()
            return
        path = urllib.parse.urlsplit(self.path).path
        if path in ("/generate_204", "/gen_204"):
            self.send_response(302); self.send_header("Location", "/"); self.end_headers(); return
        if path == "/hotspot-detect.html":
            self.send_page(); return
        if path == "/ncsi.txt":
            self.send_response(302); self.send_header("Location", "/"); self.end_headers(); return
        if path != "/":
            self.send_response(302); self.send_header("Location", "/"); self.end_headers(); return
        self.send_page()

    def do_POST(self):
        if not self.trusted_host():
            self.send_error(421); return
        if urllib.parse.urlsplit(self.path).path != "/connect":
            self.send_error(404); return
        try:
            now = time.monotonic()
            recent = [stamp for stamp in ATTEMPTS.get(self.client_address[0], []) if now - stamp < 60]
            if len(recent) >= 5:
                self.send_error(429); return
            recent.append(now)
            ATTEMPTS[self.client_address[0]] = recent
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if length < 0 or length > MAX_BODY:
            self.send_error(413); return
        try:
            values = urllib.parse.parse_qs(self.rfile.read(length).decode("utf-8"),
                                           keep_blank_values=True, strict_parsing=True)
            token, unused = self.session()
            if values.get("csrf") != [token]:
                self.send_error(403); return
            one = lambda name: values.get(name, [""])[0]
            response = helper_request({"action": "connect", "network": {
                "ssid": one("ssid"), "security": one("security"),
                "passphrase": one("passphrase"), "hidden": one("hidden") == "1"}})
            if not response.get("ok"):
                raise ValueError(response.get("error", "connection rejected"))
            self.send_page(message="<p><strong>Connecting…</strong></p><p>Stay nearby. The setup network will disappear when the phone connects.</p>")
        except (UnicodeDecodeError, ValueError, RuntimeError, OSError) as exc:
            self.send_page(400, '<p class=error>%s</p>' % html.escape(str(exc)))


def main():
    server = ThreadingHTTPServer(("10.42.0.1", 80), Portal)
    server.serve_forever()


if __name__ == "__main__":
    main()
