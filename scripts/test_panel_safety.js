// gjs scripts/test_panel_safety.js — exercise the real panel's reconnect behavior.
const GLib = imports.gi.GLib;
const ByteArray = imports.byteArray;
const read = path => ByteArray.toString(GLib.file_get_contents(path)[1]);
function assert(ok, message) { if (!ok) throw new Error(message); }
const nodes = new Map([...read("data/index.html").matchAll(/id="([^"]+)"/g)].map(match => [match[1], {
  value: "0", textContent: "", className: "", disabled: false, events: {},
  addEventListener(name, fn) { this.events[name] = fn; }
}]));
const document = { activeElement: null, getElementById: id => nodes.get(id), querySelector: selector => nodes.get(selector.slice(1)) };
const sockets = [], timers = [], frames = [];
class Socket {
  static get OPEN() { return 1; }
  constructor(url) { this.url = url; this.readyState = 1; sockets.push(this); }
  send(frame) { frames.push(JSON.parse(frame)); }
}
new Function("document", "location", "WebSocket", "setTimeout", "clearTimeout", "setInterval", read("data/app.js"))(
  document, { hostname: "192.168.1.20" }, Socket, fn => timers.push(fn), () => {}, () => {});
nodes.get("velocity").value = "0.9"; nodes.get("turn").value = "0.7";
sockets[0].onopen();
assert(sockets[0].url === "ws://192.168.1.20:81/", "WebSocket follows STA hostname");
assert(frames.at(-1).velocity === 0 && frames.at(-1).turn === 0, "Opening controller starts at zero");
nodes.get("velocity").value = "0.5"; nodes.get("velocity").events.input();
assert(frames.at(-1).velocity === 0.5, "Explicit user motion preserved");
sockets[0].readyState = 3; sockets[0].onclose();
assert(nodes.get("velocity").value === "0" && nodes.get("turn").value === "0", "Disconnect invalidates stale sliders");
timers[0](); sockets[1].onopen();
assert(frames.at(-1).velocity === 0 && frames.at(-1).turn === 0, "Reconnect cannot resume old demand");
print("PASS: existing panel WebSocket follows STA host, accepts explicit input and clears stale motion on reconnect");
