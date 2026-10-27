"""Static boundaries complement native idle-duration tests; not a radio/RF test."""
from pathlib import Path

network = Path("src/esp32_network.cpp").read_text()
app = Path("src/esp32_app.cpp").read_text()

assert app.count("WiFi.mode(") == 3  # two log labels and one actual startup call
assert network.count("WiFi.scanNetworks(") == 1
start = network.index('http->on("/api/network/scan", HTTP_POST')
stop = network.index('http->on("/api/network/connect", HTTP_POST', start)
post_scan = network[start:stop]
assert "scanSession.allowed(" in post_scan
assert "WiFi.scanNetworks(true, true, false, 120)" in post_scan
assert post_scan.index("scanSession.allowed(") < post_scan.index("WiFi.scanNetworks(")
idle_loop = network[network.index("void balanceNetworkLoop()"):network.index("void balanceNetworkPrintDriverStatus()")]
for call in ("WiFi.mode(", "WiFi.begin(", "WiFi.scanNetworks(", "WiFi.softAP(",
             "WiFi.softAPdisconnect(", "esp_wifi_start(", "esp_wifi_stop("):
    assert call not in idle_loop, call
    assert call not in network[network.index("DriverView readDriver()"):network.index('String scanJson')], call
assert "ARDUINO_EVENT_WIFI_AP_START" in network
assert "ARDUINO_EVENT_WIFI_AP_STOP" in network
assert "esp_wifi_get_mode(&d.mode)" in network
assert "esp_wifi_get_config(WIFI_IF_AP, &config)" in network
assert "esp_wifi_get_config(WIFI_IF_STA, &config)" in network
assert 'data.apReady = accessPointReady' not in app
assert "health.apReady = ap.ready" in network
assert "config.ap.password" not in network and "config.sta.password" not in network
print("PASS: startup-only mode, explicit scan, no periodic AP/radio restart, read-only driver queries and no config password dump")
