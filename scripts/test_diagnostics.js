// Run from the project root: gjs scripts/test_diagnostics.js
// Executes the real browser script with a fake DOM, HTTP transport and virtual timers.
const GLib = imports.gi.GLib;
const ByteArray = imports.byteArray;
const read = path => ByteArray.toString(GLib.file_get_contents(path)[1]);
function assert(condition, message) { if (!condition) throw new Error(message); }
async function settle() { for (let i = 0; i < 12; i++) await Promise.resolve(); }

async function main() {
  const html = read("data/diagnostics.html");
  const nodes = new Map();
  for (const match of html.matchAll(/id="([^"]+)"/g)) {
    const classes = new Set();
    nodes.set(match[1], {
      textContent: "", dataset: {}, disabled: false, events: {},
      classList: { add: name => classes.add(name), remove: name => classes.delete(name), contains: name => classes.has(name) },
      addEventListener(name, callback) { this.events[name] = callback; }
    });
  }
  const documentEvents = {};
  const windowEvents = {};
  const document = {
    hidden: false,
    getElementById(id) { assert(nodes.has(id), `Missing HTML element: ${id}`); return nodes.get(id); },
    addEventListener(name, callback) { documentEvents[name] = callback; }
  };
  const window = { addEventListener(name, callback) { windowEvents[name] = callback; } };
  const timers = new Map();
  let nextTimer = 0;
  const requests = [];
  class AbortController {
    constructor() { this.signal = { aborted: false }; }
    abort() {
      this.signal.aborted = true;
      if (this.signal.reject) {
        const error = new Error("Aborted"); error.name = "AbortError";
        this.signal.reject(error);
      }
    }
  }
  const fetch = (url, options) => new Promise((resolve, reject) => {
    assert(url === "/health", "Diagnostics must only query /health");
    assert(options.cache === "no-store", "Do not cache health responses");
    options.signal.reject = reject;
    requests.push({ resolve: (status, data) => resolve({ status, json: async () => data }), reject });
  });
  const timeout = (callback, delay) => { const id = ++nextTimer; timers.set(id, { callback, delay }); return id; };
  function fire(delay) {
    const found = [...timers].find(([, timer]) => timer.delay === delay);
    assert(found, `Missing timer: ${delay}`);
    timers.delete(found[0]); found[1].callback();
  }
  const value = id => nodes.get(id).textContent;
  const click = () => nodes.get("refresh").events.click();
  new Function("document", "window", "fetch", "setTimeout", "clearTimeout", "AbortController", read("data/diagnostics.js"))(
    document, window, fetch, timeout, id => timers.delete(id), AbortController);
  assert(requests.length === 1 && nodes.get("refresh").disabled, "Initial asynchronous request");
  click();
  assert(requests.length === 1, "No overlapping requests on manual refresh");
  const demo = {
    ok: true, mode: "DEMO", state: 5, fault: 1, imu_available: false, motors_enabled: false,
    ap_ready: true, ap_config_ready: true, ap_ssid: '<AP & "test">', ap_ip: "192.168.4.1",
    ap_gateway: "192.168.4.1", ap_netmask: "255.255.255.0", ap_clients: 1,
    ap_last_event: "AP_START", ap_start_events: 1, ap_stop_events: 0, ap_event_ms: 100,
    wifi_mode: "AP_STA", scan_state: "never", scan_starts: 0,
    http_ready: true, websocket_ready: true, littlefs_ready: true,
    index_ready: true, script_ready: true, style_ready: true,
    control_task_ready: true, control_healthy: true, control_heartbeat_count: 2000,
    control_heartbeat_age_ms: 5, uptime_ms: 90061000, free_heap: 123456, min_free_heap: 100000,
    version: "1.2.0-diagnostics", commit: "b72e760", tree_state: "dirty",
    build_utc: "2026-10-27T17:00:00Z", source_sha256: "abc", reset_reason: "power-on"
  };
  requests[0].resolve(200, demo); await settle();
  assert(value("mode") === "DEMO" && value("motors") === "Desabilitados", "Safe DEMO display");
  assert(value("imu").includes("Ausente") && value("heartbeat") === "Recente", "Sensor and heartbeat display");
  assert(value("uptime") === "1 d 1 h 1 min 1 s", "Uptime formatting");
  assert(value("ssid") === demo.ap_ssid && value("build") === demo.build_utc, "Text-only SSID and build display");
  assert(timers.size === 1 && !nodes.get("refresh").disabled, "One completion-based poll timer");

  fire(3000); requests[1].resolve(503, { ...demo, ok: false, littlefs_ready: false, ap_ready: false, ap_last_event: "AP_STOP", ap_stop_events: 1 }); await settle();
  assert(value("update-status").includes("degradada") && value("filesystem") === "Não montado", "503 payload is rendered");
  assert(value("wifi").includes("indisponível") && value("ap-last-event").includes("AP_STOP"), "An assigned IP must not mask AP_STOP");
  click(); requests[2].reject(new Error("offline")); await settle();
  assert(nodes.get("diagnostic-values").classList.contains("is-stale"), "Network error marks stale values");
  assert(value("last-update").includes("desatualizados") && value("mode") === "DEMO", "Retain and label last successful reading");
  fire(3000); fire(4000); await settle();
  assert(value("update-status").includes("4 s"), "Timeout aborts request and permits retry");

  fire(3000); const unknownMotor = { ...demo }; delete unknownMotor.motors_enabled;
  requests[4].resolve(200, unknownMotor); await settle();
  assert(value("motors") === "Não informado", "Missing motor status must not be inferred safe");
  assert(!nodes.get("diagnostic-values").classList.contains("is-stale"), "Recovery clears stale state");
  document.hidden = true; documentEvents.visibilitychange();
  assert(timers.size === 0, "Hidden page pauses polling");
  document.hidden = false; documentEvents.visibilitychange();
  assert(requests.length === 6, "Visible page refreshes immediately");
  document.hidden = true; documentEvents.visibilitychange(); await settle();
  assert(timers.size === 0, "Hidden page aborts in-flight request without rescheduling");
  document.hidden = false; documentEvents.visibilitychange();
  requests[6].resolve(200, { error: "malformed" }); await settle();
  assert(value("update-status").includes("inválida"), "Malformed JSON shape is rejected");
  click(); requests[7].resolve(500, { ok: false }); await settle();
  assert(value("update-status").includes("HTTP 500"), "Server error is distinguished from 503 health");
  windowEvents.pagehide();
  assert(timers.size === 0, "Navigation stops polling");
  windowEvents.pageshow();
  assert(requests.length === 9, "Back-forward cache restore restarts polling");
  requests[8].resolve(200, { ...demo, mode: "FAULT", state: 4, fault: 5 }); await settle();
  assert(value("robot-state") === "FALHA" && value("mode") === "FAULT", "Fault remains visible even without IMU");
  assert(nodes.get("mode").dataset.status === "bad", "Fault must never be styled as quiet/DEMO");
  windowEvents.pagehide();
  print("PASS: DEMO, FAULT visibility, fields, 503, errors, timeout, no overlap, stale/recovery, visibility and navigation");
}

const loop = new GLib.MainLoop(null, false);
let failed = false;
main().catch(error => { failed = true; printerr(error.stack); }).finally(() => loop.quit());
loop.run();
if (failed) imports.system.exit(1);
