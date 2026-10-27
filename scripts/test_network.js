// gjs scripts/test_network.js — real page script, fake DOM/IME/fetch/history and virtual clock.
const GLib = imports.gi.GLib;
const ByteArray = imports.byteArray;
const read = path => ByteArray.toString(GLib.file_get_contents(path)[1]);
function assert(ok, message) { if (!ok) throw new Error(message); }
async function settle() { for (let i = 0; i < 30; i++) await Promise.resolve(); }
const base = {
  state: "unconfigured", error: "none", trial_id: "", result: "idle", has_saved: false,
  ap_ssid: "BalanceBot-Setup", ap_ready: true, ap_ip: "192.168.4.1", ap_gateway: "192.168.4.1",
  ap_netmask: "255.255.255.0", ap_clients: 1, ap_channel: 1, sta_connected: false,
  sta_ssid: "", saved_ssid: "", sta_ip: "0.0.0.0", sta_gateway: "0.0.0.0", sta_channel: 0,
  sta_rssi: 0, retry_in_ms: 0, disconnect_reason: 0
};
const networks = { state: "complete", shown: 3, total_found: 3, networks: [
  { ssid: '<Router "24">', channel: 6, rssi: -40, auth: 3 },
  { ssid: "", channel: 11, rssi: -60, auth: 3 },
  { ssid: "Enterprise", channel: 1, rssi: -50, auth: 5 }
] };
const idleScan = { state: "idle", networks: [] };
function environment(hash = "") {
  const timers = new Map(), requests = [], documentEvents = {}, windowEvents = {}, replacements = [];
  let nextTimer = 0, inFlight = 0, maximumInFlight = 0;
  const document = { hidden: false, activeElement: null, keyboardOpen: false,
    addEventListener(name, fn) { documentEvents[name] = fn; } };
  function node(id = "") {
    const classes = new Set();
    let content = "", value = "", disabled = false;
    const item = { id, dataset: {}, events: {}, children: [], checked: false, hidden: false,
      valueWrites: 0, disabledWrites: 0, selectionStart: 0, selectionEnd: 0, selectionDirection: "none",
      classList: { add: x => classes.add(x), remove: x => classes.delete(x), contains: x => classes.has(x) },
      addEventListener(name, fn) { this.events[name] = fn; },
      append(child) { this.children.push(child); },
      replaceChildren() { assert(id === "networks", "Polling must never recreate form elements"); this.children = []; },
      contains(child) { return this === child || this.children.some(item => item.contains(child)); }
    };
    Object.defineProperties(item, {
      textContent: { get: () => content, set: next => { content = next; } },
      innerHTML: { set: () => { throw new Error("innerHTML must never replace the form"); } },
      value: { get: () => value, set: next => { value = next; item.valueWrites++; item.selectionStart = item.selectionEnd = next.length; } },
      disabled: { get: () => disabled, set: next => {
        disabled = next; item.disabledWrites++;
        if (next && document.activeElement === item) { document.activeElement = null; document.keyboardOpen = false; }
      } }
    });
    return item;
  }
  const nodes = new Map([...read("data/network.html").matchAll(/id="([^"]+)"/g)].map(match => [match[1], node(match[1])]));
  document.getElementById = id => { assert(nodes.has(id), `Missing HTML ID ${id}`); return nodes.get(id); };
  document.createElement = tag => { assert(tag !== "input" && tag !== "form", "No recreated input/form"); return node(); };
  const window = {
    location: { hash, pathname: "/network", search: "" },
    history: { state: null, replaceState(state, title, url) { replacements.push(url); window.location.hash = ""; } },
    addEventListener(name, fn) { windowEvents[name] = fn; }
  };
  class AbortController {
    constructor() { this.signal = {}; }
    abort() { if (this.signal.abort) this.signal.abort(); }
  }
  class Params {
    constructor(fields) {
      this.entries = typeof fields === "string" ? fields.split("&").map(pair => {
        const at = pair.indexOf("=");
        const decode = text => { try { return decodeURIComponent(text.replace(/\+/g, " ")); } catch { return text; } };
        return [decode(at < 0 ? pair : pair.slice(0, at)), decode(at < 0 ? "" : pair.slice(at + 1))];
      }) : Object.entries(fields);
    }
    toString() { return this.entries.map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`).join("&"); }
    getAll(name) { return this.entries.filter(([key]) => key === name).map(([, value]) => value); }
  }
  const fetch = (url, options) => new Promise((resolve, reject) => {
    assert(/^\/api\/network\/(status|scan|connect|cancel)$/.test(url), "Network operations use explicit routes without query credentials");
    assert(!options.headers["X-Network-Token"] && !options.credentials, "No token or session authentication");
    assert(options.cache === "no-store", "No cached network state");
    ++inFlight; maximumInFlight = Math.max(maximumInFlight, inFlight);
    assert(inFlight === 1, "Requests must never overlap, even when an abort is delayed");
    let finished = false;
    const finish = () => { if (!finished) { finished = true; --inFlight; } };
    const q = { url, options, ignoreAbort: false,
      resolve(status, data) { resolve({ ok: status < 400, status, json: async () => { finish(); return data; } }); },
      reject(error) { finish(); reject(error); }
    };
    options.signal.abort = () => { if (!q.ignoreAbort) { const error = new Error("Abort"); error.name = "AbortError"; q.reject(error); } };
    requests.push(q);
  });
  const timeout = (fn, delay) => { const id = ++nextTimer; timers.set(id, { fn, delay }); return id; };
  const text = id => nodes.get(id).textContent;
  const event = (id, name) => nodes.get(id).events[name]?.({ preventDefault() {} });
  function fire(delay) {
    const found = [...timers].find(([, value]) => value.delay === delay);
    assert(found, `Missing timer ${delay}`); timers.delete(found[0]); found[1].fn();
  }
  function focus(id) {
    const old = document.activeElement;
    document.activeElement = null;
    if (old) event(old.id, "blur");
    if (id) { document.activeElement = nodes.get(id); document.keyboardOpen = true; event(id, "focus"); }
    else document.keyboardOpen = false;
  }
  function type(id, value, start = value.length, end = start) {
    const item = nodes.get(id);
    item.value = value; item.selectionStart = start; item.selectionEnd = end; item.selectionDirection = "backward";
    event(id, "input");
  }
  function hashChange(fragment) { window.location.hash = fragment; windowEvents.hashchange(); }
  function unchanged(id, writes, start, end) {
    const item = nodes.get(id);
    assert(item.valueWrites === writes && item.disabledWrites === 0, `Polling changed input ${id}`);
    assert(document.activeElement === item && document.keyboardOpen, `Focus/mobile keyboard lost on ${id}`);
    assert(item.selectionStart === start && item.selectionEnd === end && item.selectionDirection === "backward", `Cursor/selection changed on ${id}`);
  }
  new Function("document", "window", "fetch", "setTimeout", "clearTimeout", "AbortController", "URLSearchParams", "TextEncoder", read("data/network.js"))(
    document, window, fetch, timeout, id => timers.delete(id), AbortController, Params,
    class { encode(value) { return ByteArray.fromString(value); } });
  return { nodes, document, window, timers, requests, replacements, documentEvents, windowEvents,
    text, event, fire, focus, type, hashChange, unchanged, maximum: () => maximumInFlight };
}
async function initialize(e, scan = idleScan) {
  assert(e.requests.length === 1 && e.requests[0].url.endsWith("/status"), "Status starts automatically without authorization");
  e.requests[0].resolve(200, base); await settle();
  e.requests[1].resolve(200, scan); await settle();
}
async function formAndPolling() {
  const e = environment();
  e.type("sta-ssid", "draft-before-status"); e.type("sta-password", "draft-password");
  e.focus("sta-password"); await initialize(e);
  assert(e.nodes.get("sta-password").value === "draft-password", "Initial response preserves draft");
  for (const id of ["sta-ssid", "sta-password"]) {
    e.focus(id); e.type(id, `${id}-partial`, 2, 5); e.event(id, "compositionstart");
    const writes = e.nodes.get(id).valueWrites;
    e.fire(3000);
    e.unchanged(id, writes, 2, 5); // Critical regression: old buttons() disabled inputs here.
    e.requests.at(-1).resolve(200, { ...base, saved_ssid: "SERVER-MUST-NOT-OVERWRITE", ap_clients: 2 }); await settle();
    e.unchanged(id, writes, 2, 5);
    e.event(id, "compositionend");
  }
  assert(e.maximum() === 1, "Serialized polling");
  e.windowEvents.pagehide(); await settle();
}
async function scanAndActions() {
  const e = environment(); await initialize(e);
  e.type("sta-password", "keep-password"); e.type("sta-ssid", "ManualHidden24"); e.focus("sta-ssid");
  e.event("scan", "click"); e.requests.at(-1).resolve(202, { state: "running", networks: [] }); await settle();
  e.fire(3000); e.requests.at(-1).resolve(200, base); await settle();
  e.requests.at(-1).resolve(200, networks); await settle();
  assert(e.nodes.get("networks").children.length === 0, "Defer scan list reflow while keyboard is editing");
  assert(e.nodes.get("sta-ssid").value === "ManualHidden24", "Scan completion must preserve manual SSID");
  e.focus(null); e.fire(0);
  const choices = e.nodes.get("networks").children.map(item => item.children[0]);
  assert(choices[1].disabled && choices[2].disabled, "Hidden and enterprise SSIDs require manual/unsupported handling");
  choices[0].events.click();
  assert(e.nodes.get("sta-ssid").value === '<Router "24">' && e.nodes.get("sta-password").value === "keep-password", "Explicit selection does not clear secrets");
  e.event("scan", "click"); e.requests.at(-1).resolve(202, { state: "running", networks: [] }); await settle();
  e.fire(3000); e.requests.at(-1).resolve(200, base); await settle();
  e.requests.at(-1).resolve(200, networks); await settle();
  assert(e.nodes.get("sta-ssid").value === '<Router "24">' && e.text("network-selection").includes('Router'), "Rescan preserves selection");
  const count = e.requests.length;
  e.type("sta-ssid", "á".repeat(17)); e.event("credentials-form", "submit");
  assert(e.requests.length === count, "SSID limit is UTF-8 bytes");
  e.type("sta-ssid", "Hidden24"); e.type("sta-password", "old-password");
  e.event("credentials-form", "submit");
  assert(e.requests.at(-1).options.body.includes("password=old-password"), "Submit snapshots explicit form values");
  e.focus("sta-password"); e.type("sta-password", "new-password-while-testing", 3, 8);
  const writes = e.nodes.get("sta-password").valueWrites;
  const trial = { ...base, state: "connecting_and_saving", trial_id: "nonce", sta_ssid: "Hidden24" };
  e.requests.at(-1).resolve(202, trial); await settle();
  e.unchanged("sta-password", writes, 3, 8);
  assert(e.text("network-message").includes("não foram enviadas"), "Feedback distinguishes sent snapshot from edited draft");
  e.fire(3000);
  const saved = { ...base, state: "online", has_saved: true, sta_connected: true, sta_ip: "192.168.100.26", result: "saved", result_ip: "192.168.100.26" };
  e.requests.at(-1).resolve(200, saved); await settle();
  e.unchanged("sta-password", writes, 3, 8);
  assert(e.text("net-result").includes("salva") && e.nodes.get("sta-ssid").value === "Hidden24", "Automatic save preserves entire form");
  assert(e.text("sta-access").includes("http://192.168.100.26/"), "STA browser access instructions");
  e.fire(3000); e.requests.at(-1).resolve(200, saved); await settle();
  assert(e.text("net-result").includes("salva"), "Final result persists across polls");
  e.event("credentials-form", "submit"); e.requests.at(-1).resolve(202, trial); await settle();
  e.event("cancel", "click"); e.requests.at(-1).resolve(200, { ...base, error: "cancelled" }); await settle();
  e.unchanged("sta-password", writes, 3, 8);
  assert([...e.nodes.values()].every(item => !item.textContent.includes("old-password")), "No secret in status/log text");
  e.windowEvents.pagehide(); await settle();
}
async function delayedResponsesAndBackoff() {
  const e = environment(); await initialize(e);
  e.type("sta-ssid", "Router24"); e.type("sta-password", "unsent-password"); e.focus("sta-password");
  e.fire(3000); const old = e.requests.at(-1); old.ignoreAbort = true;
  const count = e.requests.length;
  e.event("credentials-form", "submit");
  assert(e.requests.length === count, "Mutation waits for cancelled poll to settle");
  old.resolve(200, { ...base, ap_clients: 99 }); await settle();
  assert(e.text("net-ap-clients") !== "99", "Late poll cannot overwrite newer operation");
  assert(e.requests.at(-1).url.endsWith("/connect"), "Explicit connect needs no authorization");
  e.requests.at(-1).resolve(202, { ...base, state: "connecting_and_saving", trial_id: "nonce" }); await settle();
  const writes = e.nodes.get("sta-password").valueWrites;
  const start = e.nodes.get("sta-password").selectionStart;
  for (const delay of [3000, 6000, 12000, 24000, 30000]) {
    e.fire(delay); e.fire(5000); await settle();
    e.unchanged("sta-password", writes, start, start);
  }
  e.fire(30000); e.requests.at(-1).resolve(200, base); await settle();
  assert([...e.timers.values()].some(timer => timer.delay === 3000), "Recovery restores 3-second polling");
  e.document.hidden = true; e.documentEvents.visibilitychange();
  assert(e.timers.size === 0, "Hidden page pauses polling");
  e.document.hidden = false; e.documentEvents.visibilitychange();
  e.requests.at(-1).resolve(200, { ...base, state: "online", has_saved: true }); await settle();
  assert(!e.nodes.get("test").disabled, "Reboot/reconnect requires no token or session");
  assert(e.maximum() === 1, "Serialized operations across failures");
  e.windowEvents.pagehide(); await settle(); e.windowEvents.pageshow();
  e.requests.at(-1).resolve(200, base); await settle();
  assert(!e.nodes.get("test").disabled, "Back navigation resumes polling without login");
  e.windowEvents.pagehide(); await settle();
}
async function legacyFragment() {
  const e = environment(`#admin=${"f".repeat(64)}`);
  assert(e.window.location.hash === "" && e.replacements[0] === "/network", "Legacy fragment removed locally");
  assert(!e.nodes.has("admin-token") && !e.nodes.has("auth-form"), "No authentication UI");
  await initialize(e);
  assert(e.requests.every(q => !JSON.stringify(q.options).includes("f".repeat(64))), "Legacy fragment never transmitted");
  e.windowEvents.pagehide(); await settle();
}
async function operationFailuresAndConcurrency() {
  const e = environment(); await initialize(e);
  e.type("sta-ssid", "Router24"); e.type("sta-password", "password-snapshot");
  e.event("credentials-form", "submit");
  const pending = { ...base, state: "connecting_and_saving", trial_id: "epoch-new", result: "connecting" };
  e.requests.at(-1).resolve(202, pending); await settle();
  const count = e.requests.length;
  e.event("credentials-form", "submit");
  assert(e.requests.length === count && e.nodes.get("scan").disabled, "Pending save prevents concurrent submission/scan");
  e.focus("sta-password"); e.type("sta-password", "edited-password", 1, 4);
  const writes = e.nodes.get("sta-password").valueWrites;
  for (const error of ["connection_timeout", "dhcp_failed", "authentication_failed", "storage_failed"]) {
    e.fire(3000); e.requests.at(-1).resolve(200, { ...base, has_saved: true, result: "failed", result_error: error }); await settle();
    e.unchanged("sta-password", writes, 1, 4);
    assert(e.text("net-result").includes("anterior preservada"), "Specific failure and rollback persist in UI");
  }
  e.fire(3000); e.requests.at(-1).resolve(200, { ...base, state: "online", has_saved: true, result: "idle" }); await settle();
  assert(e.text("net-saved") === "Credenciais gravadas", "Reboot may restore saved configuration without a boot-local result");
  e.windowEvents.pagehide(); await settle();
}
async function idleNeverStartsScan() {
  const e = environment(); await initialize(e);
  for (let tick = 0; tick < 120; ++tick) {
    e.fire(3000); e.requests.at(-1).resolve(200, base); await settle();
  }
  assert(e.requests.every(q => !q.options.method || q.options.method === "GET"), "Polling/initialization must never start a radio scan");
  assert(e.requests.filter(q => q.url.endsWith("/scan")).length === 1, "Only initial cached scan-results read, no automatic scan loop");
  e.windowEvents.pagehide(); await settle();
}
async function main() {
  await formAndPolling(); print("PASS: stable DOM, dirty fields, focus/cursor/selection and simulated Android IME during status updates");
  await scanAndActions(); print("PASS: async scan selection, hidden SSID, UTF-8, late connect/autosave/cancel responses preserve draft");
  await delayedResponsesAndBackoff(); print("PASS: generations, stale responses, serialized mutations, timeout/backoff and reconnect");
  await legacyFragment(); print("PASS: no token/session UI or headers; legacy fragment never transmitted");
  await operationFailuresAndConcurrency(); print("PASS: autosave progress, concurrency guard, DHCP/auth/timeout/NVS failure, rollback and reboot display preserve focus");
  await idleNeverStartsScan(); print("PASS: six simulated minutes of portal polling send zero scan POSTs");
}
const loop = new GLib.MainLoop(null, false);
let failed = false;
main().catch(error => { failed = true; printerr(error.stack); }).finally(() => loop.quit());
loop.run();
if (failed) imports.system.exit(1);
