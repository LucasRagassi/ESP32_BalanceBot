(() => {
  const labels = ["DESARMADO", "CALIBRANDO", "PRONTO", "EQUILIBRANDO", "FALHA", "DEMO (IMU ausente)"];
  const faultLabels = ["Nenhuma", "Sensor indisponível", "Leitura inválida", "Atraso de controle", "Inclinação excessiva", "Intertravamento aberto", "Passos perdidos (simulação)"];
  const connection = document.querySelector("#connection");
  const socketUrl = `ws://${location.hostname}:81/`;
  let socket;
  let reconnectTimer;

  function connect() {
    socket = new WebSocket(socketUrl);
    socket.onopen = () => {
      resetMotionInputs();
      connection.textContent = "Conectado";
      connection.className = "badge online";
      document.querySelector("#arm").disabled = false;
      document.querySelector("#disarm").disabled = false;
      document.querySelector("#save-motors").disabled = false;
      sendMotion();
    };
    socket.onclose = () => {
      resetMotionInputs();
      connection.textContent = "Desconectado";
      connection.className = "badge offline";
      document.querySelector("#arm").disabled = true;
      document.querySelector("#disarm").disabled = true;
      document.querySelector("#save-motors").disabled = true;
      clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(connect, 1500);
    };
    socket.onmessage = ({ data }) => {
      let item;
      try { item = JSON.parse(data); } catch { return; }
      if (item.type !== "telemetry") return;
      document.querySelector("#state").textContent = labels[item.state] || "Desconhecido";
      document.querySelector("#arm").disabled = item.state === 5 || !item.imu_available;
      document.querySelector("#save-motors").disabled = item.state === 3;
      const remote = item.remote_expired ? " · Comando remoto expirado; referência zerada" : "";
      const demo = !item.imu_available ? " · Sem MPU6050: modo DEMO, motores desabilitados" : "";
      document.querySelector("#fault").textContent = `Falha: ${faultLabels[item.fault] || "Desconhecida"}${demo}${remote} · Motores físicos desabilitados`;
      document.querySelector("#angle").textContent = Number(item.angle).toFixed(1);
      document.querySelector("#gyro").textContent = Number(item.gyro).toFixed(1);
      document.querySelector("#execution").textContent = Number(item.execution_us).toFixed(0);
      document.querySelector("#jitter").textContent = Number(item.jitter_us).toFixed(0);
      document.querySelector("#deadlines").textContent = item.missed_deadlines;
      document.querySelector("#left-speed").textContent = Number(item.left_speed).toFixed(0);
      document.querySelector("#right-speed").textContent = Number(item.right_speed).toFixed(0);
      document.querySelector("#left-error").textContent = item.left_step_error;
      document.querySelector("#right-error").textContent = item.right_step_error;
      setIfInactive("left-current", item.left_current_a);
      setIfInactive("right-current", item.right_current_a);
      setIfInactive("left-max", item.left_max_steps_s);
      setIfInactive("right-max", item.right_max_steps_s);
      setIfInactive("left-accel", item.left_accel_steps_s2);
      setIfInactive("right-accel", item.right_accel_steps_s2);
      setIfInactive("left-direction", item.left_direction);
      setIfInactive("right-direction", item.right_direction);
      document.querySelector("#spec-warning").textContent = item.motor_specs_confirmed
        ? "Especificações elétricas marcadas como confirmadas."
        : "Corrente nominal, Rsense, microstepping, tensão e limites térmicos ainda precisam ser confirmados. Corrente 0 = não configurada; nenhuma configuração é aplicada a hardware.";
    };
  }

  function send(value) {
    if (socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify(value));
  }
  function resetMotionInputs() {
    for (const id of ["velocity", "turn"]) {
      document.getElementById(id).value = "0";
      document.getElementById(`${id}-value`).value = "0.00";
    }
  }
  function sendMotion() {
    send({ type: "motion", velocity: Number(document.querySelector("#velocity").value), turn: Number(document.querySelector("#turn").value) });
  }
  function setIfInactive(id, value) {
    const element = document.getElementById(id);
    if (document.activeElement !== element && Number.isFinite(Number(value))) {
      element.value = value;
    }
  }
  for (const id of ["velocity", "turn"]) {
    const input = document.getElementById(id);
    const output = document.getElementById(`${id}-value`);
    input.addEventListener("input", () => {
      output.value = Number(input.value).toFixed(2);
      sendMotion();
    });
  }
  document.querySelector("#arm").addEventListener("click", () => send({ type: "arm", armed: true }));
  document.querySelector("#disarm").addEventListener("click", () => send({ type: "arm", armed: false }));
  document.querySelector("#save-motors").addEventListener("click", () => {
    const value = id => Number(document.getElementById(id).value);
    const config = {
      type: "motor_config",
      left_current_a: value("left-current"),
      left_max_steps_s: value("left-max"),
      left_accel_steps_s2: value("left-accel"),
      left_direction: value("left-direction"),
      right_current_a: value("right-current"),
      right_max_steps_s: value("right-max"),
      right_accel_steps_s2: value("right-accel"),
      right_direction: value("right-direction")
    };
    if (Object.values(config).slice(1).every(Number.isFinite)) {
      send(config);
      document.querySelector("#config-status").textContent = "Configuração enviada; aguarde a telemetria confirmar os valores. Hardware permanece desabilitado.";
    } else {
      document.querySelector("#config-status").textContent = "Valores inválidos.";
    }
  });
  setInterval(sendMotion, 200);
  connect();
})();
