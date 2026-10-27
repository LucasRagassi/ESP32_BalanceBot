(() => {
  const intervalMs = 3000;
  const timeoutMs = 4000;
  const states = ["DESARMADO", "CALIBRANDO", "PRONTO", "EQUILIBRANDO", "FALHA", "DEMO"];
  const faults = ["Nenhuma", "Sensor indisponível", "Leitura inválida", "Atraso de controle", "Inclinação excessiva", "Intertravamento aberto", "Passos perdidos (simulação)"];
  const element = id => document.getElementById(id);
  let timer;
  let controller;
  let busy = false;
  let stopped = false;
  let lastSuccess;

  function text(id, value) {
    element(id).textContent = value === undefined || value === null ? "—" : String(value);
  }
  function indicator(id, value, yes, no, noStatus = "bad") {
    text(id, value === true ? yes : value === false ? no : "Não informado");
    element(id).dataset.status = value === true ? "good" : value === false ? noStatus : "unknown";
  }
  function quantity(value, suffix) {
    return Number.isFinite(value) ? `${value.toLocaleString("pt-BR")} ${suffix}` : "—";
  }
  function uptime(value) {
    if (!Number.isFinite(value)) return "—";
    const seconds = Math.floor(value / 1000);
    return `${Math.floor(seconds / 86400)} d ${Math.floor(seconds / 3600) % 24} h ${Math.floor(seconds / 60) % 60} min ${seconds % 60} s`;
  }
  function render(data, status) {
    text("mode", data.mode);
    element("mode").dataset.status = data.state === 4 || data.mode === "FAULT" ? "bad" : data.mode === "DEMO" ? "warning" : "unknown";
    text("robot-state", states[data.state] || "Desconhecido");
    text("fault", faults[data.fault] || "Desconhecida");
    indicator("imu", data.imu_available, "Disponível", "Ausente / indisponível", "warning");
    // Do not infer safe outputs when an older/malformed response omits this field.
    indicator("motors", typeof data.motors_enabled === "boolean" ? !data.motors_enabled : undefined,
      "Desabilitados", "Habilitados — verificar firmware");
    indicator("wifi", data.ap_ready, "Iniciado · driver consistente", "Não confirmado / indisponível");
    indicator("ap-config", data.ap_config_ready, "Aplicada", "Falhou");
    text("ssid", data.ap_ssid);
    text("address", `${data.ap_ip || "—"} / ${data.ap_gateway || "—"}`);
    text("netmask", data.ap_netmask);
    text("clients", data.ap_clients);
    text("ap-channel", data.ap_channel);
    text("ap-driver-mode", data.wifi_mode);
    text("ap-last-event", `${data.ap_last_event || "—"} · ${data.ap_event_ms ?? "—"} ms`);
    text("ap-events", `${data.ap_start_events ?? "—"} / ${data.ap_stop_events ?? "—"}`);
    text("ap-bssid", data.ap_bssid);
    text("ap-driver-rc", `${data.ap_get_mode_rc ?? "—"} / ${data.ap_get_config_rc ?? "—"}`);
    text("ap-scans", `${data.scan_state || "—"} / ${data.scan_starts ?? "—"}`);
    indicator("sta-connected", data.sta_connected, "Conectada", "Desconectada · AP disponível", "warning");
    text("sta-state", data.sta_state);
    text("sta-ssid", "Omitido por privacidade");
    text("sta-address", `${data.sta_ip || "—"} / ${data.sta_gateway || "—"}`);
    text("sta-signal", `${data.sta_channel ?? "—"} / ${data.sta_rssi ?? "—"} dBm`);
    text("sta-error", `${data.sta_error || "—"} · ${data.sta_disconnect_reason ?? "—"}`);
    text("ws-clients", data.ws_clients);
    indicator("http", data.http_ready, `Escutando · porta ${data.http_port || 80}`, "Sem listener");
    text("http-response", `HTTP ${status}`);
    indicator("websocket", data.websocket_ready, `Escutando · porta ${data.ws_port || 81}`, "Sem listener");
    indicator("filesystem", data.littlefs_ready, "Montado", "Não montado");
    indicator("index-file", data.index_ready, "Disponível", "Ausente / vazio");
    indicator("script-file", data.script_ready, "Disponível", "Ausente / vazio");
    indicator("style-file", data.style_ready, "Disponível", "Ausente / vazio");
    text("uptime", uptime(data.uptime_ms));
    text("heap", quantity(data.free_heap, "bytes"));
    text("min-heap", quantity(data.min_free_heap, "bytes"));
    indicator("task", data.control_task_ready, "Criada", "Não criada");
    indicator("heartbeat", data.control_healthy, "Recente", "Ausente / atrasado");
    text("heartbeat-count", data.control_heartbeat_count);
    text("heartbeat-age", quantity(data.control_heartbeat_age_ms, "ms"));
    text("reset", data.reset_reason);
    text("version", data.version);
    text("commit", data.commit);
    text("tree", data.tree_state);
    text("build", data.build_utc);
    text("source-hash", data.source_sha256);
  }

  async function refresh() {
    clearTimeout(timer);
    if (busy || stopped || document.hidden) return;
    busy = true;
    element("refresh").disabled = true;
    controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const response = await fetch("/health", { cache: "no-store", signal: controller.signal });
      // HTTP 503 carries the useful degraded-health payload, not a transport failure.
      if (response.status !== 200 && response.status !== 503) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      if (!data || typeof data !== "object" || Array.isArray(data) || typeof data.ok !== "boolean") {
        throw new Error("Resposta /health inválida");
      }
      if (stopped || document.hidden) return;
      render(data, response.status);
      lastSuccess = new Date().toLocaleTimeString("pt-BR");
      text("last-update", `Última leitura: ${lastSuccess}`);
      text("update-status", data.ok && response.status === 200 ? "Diagnóstico atualizado" : "Diagnóstico atualizado · infraestrutura degradada");
      element("update-status").dataset.status = data.ok && response.status === 200 ? "good" : "warning";
      element("diagnostic-values").classList.remove("is-stale");
    } catch (error) {
      if (stopped || document.hidden) return;
      text("update-status", error.name === "AbortError" ? "Sem resposta em 4 s · tentando novamente" : `Falha na consulta: ${error.message} · tentando novamente`);
      element("update-status").dataset.status = "bad";
      element("diagnostic-values").classList.add("is-stale");
      text("last-update", lastSuccess ? `Última leitura: ${lastSuccess} · dados desatualizados` : "Nenhuma leitura válida recebida.");
    } finally {
      clearTimeout(timeout);
      controller = undefined;
      busy = false;
      element("refresh").disabled = false;
      // One request at a time; delay starts after completion, never in the control task.
      if (!stopped && !document.hidden) timer = setTimeout(refresh, intervalMs);
    }
  }

  element("refresh").addEventListener("click", refresh);
  document.addEventListener("visibilitychange", () => {
    clearTimeout(timer);
    if (document.hidden) {
      if (controller) controller.abort();
      text("update-status", "Atualização pausada · página oculta");
    } else {
      refresh();
    }
  });
  window.addEventListener("pagehide", () => {
    stopped = true;
    clearTimeout(timer);
    if (controller) controller.abort();
  });
  window.addEventListener("pageshow", () => {
    if (stopped) { stopped = false; refresh(); }
  });
  refresh();
})();
