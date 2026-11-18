"use strict";

(() => {
  const button = document.getElementById("imu-graph-toggle");
  const state = document.getElementById("imu-graph-state");
  const hostInput = document.getElementById("host");
  const plots = [
    [document.getElementById("imu-accel-graph"), ["ax", "ay", "az"]],
    [document.getElementById("imu-gyro-graph"), ["gx", "gy", "gz"]],
  ];
  const colors = ["#b32929", "#18724b", "#245bb8"];
  const windowUs = 10_000_000;
  let socket = null;
  let samples = [];
  let missing = 0;
  let idleTimer;

  function draw(canvas, fields) {
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
    const ctx = canvas.getContext("2d");
    ctx.scale(ratio, ratio);
    const left = 60, right = width - 12, top = 10, bottom = height - 26;
    if (right <= left) return;

    let low = 0, high = 0;
    for (const sample of samples) {
      for (const field of fields) {
        low = Math.min(low, sample[field]);
        high = Math.max(high, sample[field]);
      }
    }
    const step = Math.max(1, Math.ceil((high - low) / 4));
    const latest = samples.at(-1)?.t_us ?? 0;
    ctx.font = "11px system-ui";
    ctx.fillStyle = "#526579";
    ctx.strokeStyle = "#d9e1e9";
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i = 0; i <= 4; i++) {
      const y = bottom - (bottom - top) * i / 4;
      ctx.moveTo(left, y);
      ctx.lineTo(right, y);
      ctx.textAlign = "right";
      ctx.fillText(String(low + step * i), left - 6, y + 4);
    }
    for (let i = 0; i <= 5; i++) {
      const x = left + (right - left) * i / 5;
      ctx.moveTo(x, top);
      ctx.lineTo(x, bottom);
      ctx.textAlign = i === 5 ? "right" : "center";
      ctx.fillText(String(i * 2 - 10), x, bottom + 16);
    }
    ctx.stroke();

    fields.forEach((field, index) => {
      ctx.strokeStyle = colors[index];
      ctx.fillStyle = colors[index];
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      let previous;
      for (const sample of samples) {
        const x = right - (latest - sample.t_us) * (right - left) / windowUs;
        const y = bottom - (sample[field] - low) * (bottom - top) / (step * 4);
        if (previous && sample.seq === previous.seq + 1 && sample.t_us - previous.t_us <= 500_000) {
          ctx.lineTo(x, y);
        } else {
          ctx.moveTo(x, y);
          ctx.fillRect(x - 1, y - 1, 2, 2);
        }
        previous = sample;
      }
      ctx.stroke();
    });
  }

  function redraw() {
    for (const [canvas, fields] of plots) draw(canvas, fields);
  }

  function stop(label) {
    const current = socket;
    socket = null;
    if (current) current.close();
    clearTimeout(idleTimer);
    state.textContent = label;
    button.textContent = "ライブ表示開始";
  }

  function start() {
    const host = hostInput.value.trim();
    const octets = host.split(".");
    if (octets.length !== 4 || octets.some(octet => !/^(0|[1-9][0-9]{0,2})$/.test(octet) || Number(octet) > 255)) {
      state.textContent = "接続設定にXIAOのIPv4アドレスを入力してください";
      return;
    }
    samples = [];
    missing = 0;
    redraw();
    const url = new URL("/ws/imu", window.location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    url.searchParams.set("host", host);
    const current = new WebSocket(url);
    socket = current;
    button.textContent = "ライブ表示停止";
    state.textContent = "接続中";
    current.addEventListener("open", () => {
      if (socket === current) state.textContent = "IMUデータ待ち";
    });
    current.addEventListener("message", event => {
      if (socket !== current) return;
      for (const sample of JSON.parse(event.data).samples) {
        const previous = samples.at(-1);
        if (previous && sample.boot_id !== previous.boot_id) {
          samples = [];
          missing = 0;
        } else if (previous) {
          if (sample.seq <= previous.seq || sample.t_us <= previous.t_us) continue;
          missing += sample.seq - previous.seq - 1;
        }
        samples.push(sample);
      }
      const cutoff = samples.at(-1).t_us - windowUs;
      samples = samples.filter(sample => sample.t_us >= cutoff).slice(-2048);
      redraw();
      state.textContent = `受信中 · 連番の欠落 ${missing}`;
      clearTimeout(idleTimer);
      idleTimer = setTimeout(() => { state.textContent = "IMUデータの受信が停止"; }, 2000);
    });
    current.addEventListener("close", () => {
      if (socket === current) stop("WebSocket切断");
    });
  }

  button.addEventListener("click", () => socket ? stop("停止中") : start());
  hostInput.addEventListener("change", () => {
    if (socket) stop("接続先が変更されました");
  });
  window.addEventListener("resize", redraw);
  redraw();
})();
