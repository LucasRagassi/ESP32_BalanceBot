"""Static route/asset validation. Does not claim to run the ESP32 HTTP server."""
from html.parser import HTMLParser
from pathlib import Path
import re

app = Path("src/esp32_app.cpp").read_text()
network = Path("src/esp32_network.cpp").read_text()
# Explicit patterns keep C++ pointer and object syntax separate.
routes = set(re.findall(r'httpServer\.on\("([^\"]+)",\s*HTTP_(GET|POST)', app)) | set(
    re.findall(r'http->on\("([^\"]+)",\s*HTTP_(GET|POST)', network)
)
for path in ("/", "/health", "/diagnostics", "/network", "/style.css", "/app.js",
             "/diagnostics.css", "/diagnostics.js", "/network.css", "/network.js"):
    assert (path, "GET") in routes, path
for path, method in (("status", "GET"), ("scan", "GET"), ("scan", "POST"),
                     ("connect", "POST"), ("cancel", "POST")):
    assert (f"/api/network/{path}", method) in routes


class Dependencies(HTMLParser):
    def handle_starttag(self, tag, attributes):
        for key, value in attributes:
            if key in ("src", "href") and value.startswith("/"):
                assert (value, "GET") in routes, f"Unregistered dependency {value}"


for page in Path("data").glob("*.html"):
    content = page.read_text()
    Dependencies().feed(content)
    assert 'name="viewport"' in content, page
for path, asset, mime in (("/network", "network.html", "text/html; charset=utf-8"),
                          ("/network.css", "network.css", "text/css"),
                          ("/network.js", "network.js", "application/javascript")):
    start = app.index(f'httpServer.on("{path}", HTTP_GET')
    block = app[start:app.index("    });", start)]
    assert f'handleAsset("/{asset}", "{mime}")' in block
    assert Path("data", asset).stat().st_size > 0
assert 'WiFi.mode(WIFI_AP_STA)' in app
assert 'LittleFS.begin(false)' in app
assert 'WiFi.persistent(false)' in network
assert '"bb_wifi"' in network
assert 'new WebSocket' not in Path("data/network.js").read_text()
print(f"PASS: {len(routes)} GET/POST route registrations, HTML dependencies, MIME types and assets; hardware HTTP pending")
