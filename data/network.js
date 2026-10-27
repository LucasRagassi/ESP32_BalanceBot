(() => {
  const el = id => document.getElementById(id);
  const phases = { unconfigured: "Não configurada", connecting: "Conectando", online: "Conectada", backoff: "Aguardando reconexão", connecting_and_saving: "Conectando e salvando", subnet_conflict: "Conflito de sub-rede" };
  const errors = { none: "Nenhum", invalid_credentials: "SSID/senha inválidos", connection_timeout: "Sem conexão em 25 s: confira senha, sinal e banda 2,4 GHz", link_lost: "Conexão perdida", subnet_conflict: "Roteador sobrepõe 192.168.4.0/24: altere a sub-rede do roteador", dhcp_failed: "DHCP inválido ou sem IPv4 válido em 25 s; rede anterior restaurada", authentication_failed: "Autenticação Wi-Fi falhou; confira a senha", storage_failed: "Falha na NVS; rede anterior restaurada", cancelled: "Operação cancelada; rede anterior restaurada", network_busy: "Rede ocupada/AP não confirmado: aguarde conexão/varredura e ao menos 15 s entre buscas, ou cancele a operação", trial_mismatch: "Operação mudou ou terminou; atualize o estado", scan_failed: "Falha ao iniciar varredura", scan_failed_or_timeout: "Varredura falhou ou expirou" };
  // Form draft never receives telemetry. Only input events or an explicit user choice write it.
  const draft = { ssid: "", password: "", open: false, selected: null, revision: 0, dirty: new Set(), editing: new Set() };
  const inputs = { "sta-ssid": "ssid", "sta-password": "password", "open-network": "open" };
  let generation = 0;
  let sequence = 0;
  let telemetry;
  let telemetrySequence = 0;
  let active;
  let queued;
  let timer;
  let failures = 0;
  let stopped = false;
  let scanning = false;
  let deferredScan;
  let scanSignature = "";
  const text = (id, value) => {
    const next = value === null || value === undefined ? "—" : String(value);
    if (el(id).textContent !== next) el(id).textContent = next;
  };
  function message(value, bad = false) { text("network-message", value); el("network-message").dataset.status = bad ? "bad" : "good"; }
  function buttons() {
    const trial = telemetry && telemetry.state === "connecting_and_saving";
    const mutation = active && active.kind !== "poll";
    // Never change disabled/readOnly/value on inputs here: Android drops the keyboard on disable.
    el("test").disabled = !!trial || scanning || !!mutation;
    el("scan").disabled = !!trial || scanning || !!mutation;
    el("cancel").disabled = !trial || !telemetry?.trial_id || !!mutation;
  }
  function readDraft(id, dirty = true) {
    const field = inputs[id];
    draft[field] = field === "open" ? el(id).checked : el(id).value;
    if (dirty) { draft.dirty.add(field); ++draft.revision; }
    if (field === "ssid" && dirty) {
      draft.selected = null;
      text("network-selection", "SSID manual em edição; a varredura não altera este campo.");
    }
  }
  function current(op) { return active === op && op.generation === generation && !op.cancelled && !stopped; }
  function validState(data) { return data && typeof data.state === "string" && Object.prototype.hasOwnProperty.call(phases, data.state); }
  function render(data, op) {
    if (!current(op) || op.sequence < telemetrySequence) return;
    if (!validState(data)) throw new Error("invalid_response");
    telemetry = data; telemetrySequence = op.sequence;
    text("net-ap-ready", data.ap_ready ? "Iniciado · driver consistente" : "Não confirmado / indisponível");
    text("net-ap-ssid", data.ap_ssid); text("net-ap-ip", data.ap_ip);
    text("net-ap-subnet", `${data.ap_gateway} / ${data.ap_netmask}`);
    text("net-ap-channel", data.ap_channel); text("net-ap-clients", data.ap_clients);
    text("net-state", phases[data.state] || data.state); text("net-connected", data.sta_connected ? "Sim" : "Não");
    text("net-ssid", "Omitido por privacidade"); text("net-saved", data.has_saved ? "Credenciais gravadas" : "Nenhuma configuração");
    text("net-ip", data.sta_ip); text("net-gateway", data.sta_gateway);
    text("net-signal", data.sta_connected ? `${data.sta_channel} / ${data.sta_rssi} dBm` : "Sem conexão STA");
    text("net-error", `${errors[data.error] || data.error} · motivo ${data.disconnect_reason}`);
    text("net-retry", `${Math.ceil(data.retry_in_ms / 1000)} s`);
    text("sta-access", data.sta_connected ? `No Fedora conectado ao roteador por Ethernet, abra http://${data.sta_ip}/ (mesma LAN, sem isolamento de clientes).` : "O IP STA aparecerá após obter endereço do roteador.");
    const result = data.result || "idle";
    text("net-result", result === "saved" ? `Configuração salva e verificada na NVS. IP atribuído: ${data.result_ip}. A reconexão será automática após reboot.` :
      result === "connecting" ? "Conectando… aguardando DHCP válido para salvar automaticamente (até 25 s)." :
      result === "failed" ? `Não foi possível salvar: ${errors[data.result_error] || data.result_error}. Configuração anterior preservada.` :
      result === "cancelled" ? "Operação cancelada; configuração anterior preservada." : "Nenhuma operação neste boot.");
    el("net-result").dataset.status = result === "failed" ? "bad" : "good";
    el("network-values").classList.remove("is-stale");
  }
  async function request(op, path, fields) {
    if (!current(op)) throw new Error("obsolete");
    const controller = new AbortController(); op.controller = controller;
    const timeout = setTimeout(() => controller.abort(), 5000);
    try {
      // No credentials in URL, storage or logs; form POST only for network operations.
      const options = { cache: "no-store", signal: controller.signal, headers: {} };
      if (fields !== undefined) {
        options.method = "POST";
        options.headers["Content-Type"] = "application/x-www-form-urlencoded";
        options.body = new URLSearchParams(fields).toString();
      }
      const response = await fetch(`/api/network/${path}`, options);
      const data = await response.json();
      if (!current(op)) throw new Error("obsolete");
      if (!response.ok) {
        const error = new Error(errors[data?.error] ? data.error : "http_error");
        error.status = response.status; error.data = data;
        throw error;
      }
      return data;
    } finally {
      clearTimeout(timeout);
      if (op.controller === controller) op.controller = undefined;
    }
  }
  function applyScan() {
    // Defer list reflow while typing/composing; never replace form elements or selection.
    if (!deferredScan || draft.editing.size || el("networks").contains(document.activeElement)) return;
    const data = deferredScan;
    deferredScan = undefined;
    const networks = data.networks || [];
    const signature = JSON.stringify(networks);
    if (signature === scanSignature) return;
    scanSignature = signature;
    el("networks").replaceChildren();
    for (const network of networks) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = `${network.ssid || "SSID oculto — digite manualmente"} · canal ${network.channel} · ${network.rssi} dBm · ${network.auth === 0 ? "aberta" : "protegida"}`;
      button.disabled = !network.ssid || ![0, 2, 3, 4].includes(network.auth);
      button.addEventListener("click", () => {
        // Explicit selection can replace the SSID. It never clears an existing password.
        el("sta-ssid").value = network.ssid;
        el("open-network").checked = network.auth === 0;
        readDraft("sta-ssid"); readDraft("open-network");
        draft.selected = { ssid: network.ssid, channel: network.channel };
        text("network-selection", `Selecionada: ${network.ssid} · canal ${network.channel}`);
      });
      item.append(button); el("networks").append(item);
    }
  }
  function renderScan(data, op) {
    if (!current(op)) return;
    if (!data || !Array.isArray(data.networks)) throw new Error("invalid_response");
    scanning = data.state === "running";
    text("scan-status", scanning ? "Buscando redes…" : data.state === "failed" ? (errors[data.error] || "Falha na varredura") : `${data.shown || 0} redes exibidas · ${data.total_found || 0} encontradas`);
    deferredScan = data;
    applyScan();
  }
  function schedule() {
    clearTimeout(timer);
    if (!stopped && !document.hidden) {
      const delay = Math.min(30000, 3000 * (2 ** Math.min(failures, 4)));
      timer = setTimeout(poll, delay);
    }
  }
  async function run(job) {
    const op = { ...job, sequence: ++sequence, cancelled: false };
    active = op; buttons();
    try {
      await op.action(op);
      if (current(op)) {
        failures = 0;
        text("network-poll-status", `Última leitura: ${new Date().toLocaleTimeString("pt-BR")} · atualização em 3 s.`);
      }
    } catch (error) {
      if (current(op)) {
        ++failures;
        el("network-values").classList.add("is-stale");
        if (validState(error.data)) render(error.data, op);
        const detail = errors[error.message] || (error.message === "invalid_response" ? "Resposta de estado inválida." : "Sem resposta válida. Reconecte ao AP se o canal mudou.");
        text("network-poll-status", `${detail} Dados podem estar desatualizados; nova consulta em ${Math.min(30, 3 * (2 ** Math.min(failures, 4)))} s.`);
        if (op.kind !== "poll") message(`${detail} O formulário foi preservado.`, true);
      }
    } finally {
      if (active === op) active = undefined;
      buttons();
      const next = queued; queued = undefined;
      if (next && next.generation === generation && !stopped && !document.hidden) run(next);
      else schedule();
    }
  }
  function enqueue(kind, action) {
    clearTimeout(timer);
    if (stopped || document.hidden) return;
    const job = { kind, action, generation };
    if (active) {
      if (kind === "poll" && !active.cancelled) { schedule(); return; }
      queued = job;
      // Abort a status read for an explicit action, but wait for it to settle before sending more HTTP.
      if (active.kind === "poll" || active.cancelled) {
        active.cancelled = true;
        if (active.controller) active.controller.abort();
      }
    } else run(job);
  }
  async function readStatus(op) {
    render(await request(op, "status"), op);
    if (scanning && current(op)) renderScan(await request(op, "scan"), op);
  }
  function poll() { enqueue("poll", readStatus); }
  function cancelPending() {
    ++generation; queued = undefined; clearTimeout(timer);
    if (active) { active.cancelled = true; if (active.controller) active.controller.abort(); }
  }
  for (const id of Object.keys(inputs)) {
    readDraft(id, false);
    el(id).addEventListener("input", () => readDraft(id));
    el(id).addEventListener("change", () => readDraft(id));
    el(id).addEventListener("focus", () => draft.editing.add(id));
    el(id).addEventListener("blur", () => { draft.editing.delete(id); setTimeout(applyScan, 0); });
    el(id).addEventListener("compositionstart", () => draft.editing.add(id));
    el(id).addEventListener("compositionend", () => { readDraft(id); });
  }
  el("networks").addEventListener("focusout", () => setTimeout(applyScan, 0));
  el("scan").addEventListener("click", () => {
    enqueue("scan", async op => { renderScan(await request(op, "scan", {}), op); if (current(op)) message("Varredura iniciada. Rede selecionada e campos preservados."); });
  });
  el("credentials-form").addEventListener("submit", event => {
    event.preventDefault();
    if (telemetry?.state === "connecting_and_saving" || scanning || (active && active.kind !== "poll")) return;
    for (const id of ["sta-ssid", "sta-password", "open-network"]) readDraft(id, false);
    const { ssid, password, open, revision } = draft;
    if (new TextEncoder().encode(ssid).length > 32 || !ssid ||
        (!open && !/^(?:[\x20-\x7e]{8,63}|[0-9a-fA-F]{64})$/.test(password)) || (open && password)) {
      message("Informe SSID até 32 bytes e senha WPA de 8–63 caracteres ou 64 hexadecimais. Para rede aberta deixe a senha vazia e autorize a opção.", true); return;
    }
    message("Conectando com os valores enviados; o salvamento será automático após DHCP válido. Formulário preservado.");
    enqueue("connect", async op => {
      render(await request(op, "connect", { ssid, password, open: open ? "1" : "0" }), op);
      if (current(op)) message(draft.revision === revision ? "Conexão iniciada. Ao receber DHCP válido, as credenciais serão salvas automaticamente; formulário preservado." : "Conexão dos valores enviados iniciada. As alterações feitas durante a resposta foram preservadas e não foram enviadas.");
    });
  });
  el("cancel").addEventListener("click", () => {
    if (!telemetry?.trial_id) return;
    const trial_id = telemetry.trial_id;
    message("Solicitando cancelamento; formulário preservado…");
    enqueue("cancel", async op => {
      render(await request(op, "cancel", { trial_id }), op);
      if (current(op)) message("Operação cancelada; configuração anterior preservada. Formulário preservado.");
    });
  });
  // Remove legacy fragments locally without consuming or transmitting them.
  if (window.location.hash) window.history.replaceState(window.history.state, "", window.location.pathname + window.location.search);
  document.addEventListener("visibilitychange", () => {
    clearTimeout(timer);
    if (document.hidden) { queued = undefined; if (active) { active.cancelled = true; if (active.controller) active.controller.abort(); } }
    else poll();
  });
  window.addEventListener("pagehide", () => {
    stopped = true; cancelPending(); draft.password = ""; el("sta-password").value = "";
  });
  window.addEventListener("pageshow", () => { if (stopped) { stopped = false; poll(); buttons(); } });
  buttons();
  enqueue("initial", async op => {
    render(await request(op, "status"), op);
    renderScan(await request(op, "scan"), op);
    if (current(op)) message("Pronto para configurar. Use somente rede confiável.");
  });
})();
