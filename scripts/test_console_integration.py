"""Static integration boundaries; runtime UART/Wire behavior still needs the ESP32."""
from pathlib import Path

app = Path("src/esp32_app.cpp").read_text()
network = Path("src/esp32_network.cpp").read_text()
logger = Path("src/esp32_console.cpp").read_text()

control = app[app.index("void controlTask(void*)"):app.index("void broadcastTelemetry()")]
assert "Serial." not in control and "consoleDrain" not in control
producer = logger[logger.index("void consoleLog("):logger.index("void consoleDrain()")]
assert "Serial." not in producer
assert "xQueueSend(queue, &record, 0)" in producer
assert "xQueueOverwrite(lastCritical, &record)" in producer
assert '#define BALANCEBOT_LOG_LEVEL 1' in logger

for obsolete in ("AdminToken", "balanceNetworkPrintAdmin", "X-Network-Token", "ConsoleCommand::Admin"):
    assert obsolete not in network + app
assert 'Serial.' not in network[network.index("void balanceNetworkSetup("):network.index("void balanceNetworkLoop()")]
for path in ("data/network.html", "data/network.js"):
    content = Path(path).read_text()
    for obsolete in ("admin-token", "auth-form", "sessionStorage", "localStorage", "document.cookie"):
        assert obsolete not in content
assert 'esp_log_level_set("i2c", target)' in app
assert 'esp_log_level_set("*"' not in app
assert "Wire.endTransmission(true) != 0" in app
print("PASS: open network portal without token/session, nonblocking control log producer, default NORMAL and targeted I2C policy")
