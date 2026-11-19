"use strict";

(() => {
  const button = document.getElementById("imu-graph-toggle");
  const state = document.getElementById("imu-graph-state");
  const hostInput = document.getElementById("host");
  const attitudeView = document.getElementById("imu-attitude-view");
  const attitudeViewState = document.getElementById("imu-attitude-view-state");
  const referenceMode = document.getElementById("imu-reference-view");
  const worldAxisLabel = document.getElementById("imu-world-axis-label");
  const attitudeFields = ["roll_deg", "pitch_deg", "yaw_deg"];
  const relativeFields = attitudeFields.map(field => `relative_${field}`);
  const plots = [
    ["accel", ["ax", "ay", "az"]],
    ["gyro", ["gx", "gy", "gz"]],
    ["accel-mps2", ["ax_mps2", "ay_mps2", "az_mps2"], true],
    ["gyro-dps", ["gx_dps", "gy_dps", "gz_dps"], true],
    ["attitude", attitudeFields, true],
  ].map(([kind, fields, physical = false]) => ({
    canvas: document.getElementById(`imu-${kind}-graph`),
    fixed: document.getElementById(`imu-${kind}-fixed`),
    min: document.getElementById(`imu-${kind}-min`),
    max: document.getElementById(`imu-${kind}-max`),
    error: document.getElementById(`imu-${kind}-range-error`),
    fields,
    physical,
    range: null,
  }));
  const attitudePlot = plots.at(-1);
  const readings = plots.flatMap(plot =>
    plot.fields.map((field, index) => [plot, index, document.getElementById(`imu-live-${field}`)])
  );
  const colors = ["#b32929", "#18724b", "#245bb8"];
  const windowUs = 10_000_000;
  let socket = null;
  let samples = [];
  let missing = 0;
  let idleTimer;
  let attitudeLive = false;
  let azimuth = Math.PI / 5;
  let elevation = Math.PI / 6;
  let drag = null;

  function axesFromEuler(sample, fields) {
    const [roll, pitch, yaw] = fields.map(field => sample[field] * Math.PI / 180);
    const cr = Math.cos(roll), sr = Math.sin(roll);
    const cp = Math.cos(pitch), sp = Math.sin(pitch);
    const cy = Math.cos(yaw), sy = Math.sin(yaw);
    // Columns of Rz(yaw) Ry(pitch) Rx(roll).
    return [
      [cy * cp, sy * cp, -sp],
      [cy * sp * sr - sy * cr, sy * sp * sr + cy * cr, cp * sr],
      [cy * sp * cr + sy * sr, sy * sp * cr - cy * sr, cp * cr],
    ];
  }

  function drawAttitude(sample, fields) {
    const width = attitudeView.clientWidth;
    const height = attitudeView.clientHeight;
    if (!width || !height) return;
    const pixelRatio = window.devicePixelRatio || 1;
    attitudeView.width = Math.round(width * pixelRatio);
    attitudeView.height = Math.round(height * pixelRatio);
    const ctx = attitudeView.getContext("2d");
    ctx.scale(pixelRatio, pixelRatio);
    const scale = Math.min(width, height) * 0.21;
    const ca = Math.cos(azimuth), sa = Math.sin(azimuth);
    const ce = Math.cos(elevation), se = Math.sin(elevation);

    function project([x, y, z]) {
      const depth = x * sa + y * ca;
      // Screen right and screen up cross toward the camera.
      return [width / 2 + scale * (y * sa - x * ca),
              height / 2 + scale * (depth * se - z * ce)];
    }

    function line(from, to) {
      ctx.moveTo(...project(from));
      ctx.lineTo(...project(to));
    }

    ctx.strokeStyle = "#d9e1e9";
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let i = -4; i <= 4; i++) {
      const offset = i * 0.4;
      line([offset, -1.6, 0], [offset, 1.6, 0]);
      line([-1.6, offset, 0], [1.6, offset, 0]);
    }
    ctx.stroke();

    const origin = project([0, 0, 0]);
    const basis = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];

    function drawAxes(vectors, labelPrefix, length, dash) {
      const sensor = dash.length === 0;
      ctx.font = sensor ? "600 12px system-ui" : "12px system-ui";
      ctx.textBaseline = "middle";
      ctx.textAlign = "center";
      vectors.forEach((axis, i) => {
        const end = project(axis.map(value => value * length));
        const dx = end[0] - origin[0], dy = end[1] - origin[1];
        const screenLength = Math.hypot(dx, dy);
        const label = `${labelPrefix} ${"XYZ"[i]}`;
        ctx.strokeStyle = ctx.fillStyle = colors[i];
        ctx.lineWidth = sensor ? 3 : 1.5;
        ctx.setLineDash(dash);
        ctx.beginPath();
        ctx.moveTo(...origin);
        ctx.lineTo(...end);
        ctx.stroke();
        if (screenLength >= 5) {
          const angle = Math.atan2(dy, dx);
          ctx.beginPath();
          ctx.moveTo(...end);
          for (const offset of [-0.45, 0.45]) {
            ctx.lineTo(end[0] - 9 * Math.cos(angle + offset),
                       end[1] - 9 * Math.sin(angle + offset));
          }
          ctx.fill();
        }
        // An axis aimed at the camera has a short projection; label it off the origin.
        const labelX = screenLength >= 5 ? end[0] + 21 * dx / screenLength : end[0] + 29;
        const labelY = screenLength >= 5 ? end[1] + 13 * dy / screenLength : end[1] - 13 - i * 13;
        ctx.lineWidth = 3;
        ctx.strokeStyle = "#f7f9fc";
        ctx.strokeText(label, labelX, labelY);
        ctx.fillText(label, labelX, labelY);
      });
    }

    drawAxes(basis, "W", 1.65, [5, 4]);
    if (sample) drawAxes(axesFromEuler(sample, fields), "IMU", 1.06, []);
  }

  function updateAttitudeView() {
    const latest = samples.at(-1);
    const relative = referenceMode.value === "relative";
    const fields = relative ? relativeFields : attitudeFields;
    const valid = attitudeLive && latest && fields.every(field => Number.isFinite(latest[field]));
    drawAttitude(valid ? latest : null, fields);

    const axisLabel = relative ? "設定した基準の軸" : "地面基準の軸";
    if (worldAxisLabel.textContent !== axisLabel) worldAxisLabel.textContent = axisLabel;
    const message = !attitudeLive ? "姿勢データ待ち" : valid ? "受信中"
      : relative ? "設定した基準のデータなし" : "姿勢データなし";
    if (attitudeViewState.textContent !== message) attitudeViewState.textContent = message;
  }

  function draw({canvas, fields, physical, range}) {
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (!width || !height) return;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
    const ctx = canvas.getContext("2d");
    ctx.scale(ratio, ratio);
    const left = 60, right = width - 12, top = 10, bottom = height - 26;
    if (right <= left) return;

    let low = 0, high = 0;
    if (range) {
      [low, high] = range;
    } else {
      for (const sample of samples) {
        for (const field of fields) {
          if (!Number.isFinite(sample[field])) continue;
          low = Math.min(low, sample[field]);
          high = Math.max(high, sample[field]);
        }
      }
      const quarterSpan = (high - low) / 4;
      const resolution = physical && quarterSpan > 0
        ? 10 ** Math.floor(Math.log10(quarterSpan))
        : 1;
      high = low + 4 * resolution * Math.max(1, Math.ceil(quarterSpan / resolution));
    }
    const step = (high - low) / 4;
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
      ctx.fillText(String(Number((low + step * i).toPrecision(6))), left - 6, y + 4);
    }
    for (let i = 0; i <= 5; i++) {
      const x = left + (right - left) * i / 5;
      ctx.moveTo(x, top);
      ctx.lineTo(x, bottom);
      ctx.textAlign = i === 5 ? "right" : "center";
      ctx.fillText(String(i * 2 - 10), x, bottom + 16);
    }
    ctx.stroke();

    ctx.save();
    ctx.beginPath();
    ctx.rect(left, top, right - left, bottom - top);
    ctx.clip();
    fields.forEach((field, index) => {
      ctx.strokeStyle = colors[index];
      ctx.fillStyle = colors[index];
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      let previous;
      for (const sample of samples) {
        if (!Number.isFinite(sample[field])) {
          previous = undefined;
          continue;
        }
        const x = right - (latest - sample.t_us) * (right - left) / windowUs;
        const y = bottom - (sample[field] - low) * (bottom - top) / (step * 4);
        const wrapped = (field.endsWith("roll_deg") || field.endsWith("yaw_deg"))
          && previous && Math.abs(sample[field] - previous[field]) > 180;
        const referenceChanged = field.startsWith("relative_") && previous
          && sample.reference_seq !== previous.reference_seq;
        if (previous && !wrapped && !referenceChanged && sample.seq === previous.seq + 1 && sample.t_us - previous.t_us <= 500_000) {
          ctx.lineTo(x, y);
        } else {
          ctx.moveTo(x, y);
          ctx.fillRect(x - 1, y - 1, 2, 2);
        }
        previous = sample;
      }
      ctx.stroke();
    });
    ctx.restore();
  }

  function redraw() {
    const latest = samples.at(-1);
    for (const [plot, index, element] of readings) {
      element.textContent = latest?.[plot.fields[index]] ?? "—";
    }
    for (const plot of plots) draw(plot);
    updateAttitudeView();
  }

  function stop(label) {
    const current = socket;
    socket = null;
    if (current) current.close();
    clearTimeout(idleTimer);
    attitudeLive = false;
    updateAttitudeView();
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
      if (!samples.length) return;
      const cutoff = samples.at(-1).t_us - windowUs;
      samples = samples.filter(sample => sample.t_us >= cutoff).slice(-2048);
      attitudeLive = true;
      redraw();
      state.textContent = `受信中 · 連番の欠落 ${missing}`;
      clearTimeout(idleTimer);
      idleTimer = setTimeout(() => {
        state.textContent = "IMUデータの受信が停止";
        attitudeLive = false;
        updateAttitudeView();
      }, 2000);
    });
    current.addEventListener("close", () => {
      if (socket === current) stop("WebSocket切断");
    });
  }

  attitudeView.addEventListener("pointerdown", event => {
    if (event.button !== 0 || drag) return;
    attitudeView.setPointerCapture(event.pointerId);
    drag = {id: event.pointerId, x: event.clientX, y: event.clientY};
  });
  attitudeView.addEventListener("pointermove", event => {
    if (drag?.id !== event.pointerId) return;
    const radiansPerPixel = 0.01;
    azimuth = (azimuth + (event.clientX - drag.x) * radiansPerPixel) % (2 * Math.PI);
    elevation = Math.max(-Math.PI / 2, Math.min(Math.PI / 2,
      elevation + (event.clientY - drag.y) * radiansPerPixel));
    drag.x = event.clientX;
    drag.y = event.clientY;
    updateAttitudeView();
  });
  attitudeView.addEventListener("lostpointercapture", event => {
    if (drag?.id === event.pointerId) drag = null;
  });

  button.addEventListener("click", () => socket ? stop("停止中") : start());
  hostInput.addEventListener("change", () => {
    if (socket) stop("接続先が変更されました");
  });
  for (const plot of plots) {
    const {fixed, min, max, error} = plot;
    function updateRange() {
      min.disabled = max.disabled = !fixed.checked;
      error.textContent = "";
      if (!fixed.checked) {
        plot.range = null;
      } else {
        const low = min.valueAsNumber;
        const high = max.valueAsNumber;
        if (!min.validity.valid || !max.validity.valid || !(low < high) || !Number.isFinite(high - low)) {
          error.textContent = plot.physical
            ? "最小値と最大値を数値で入力し、最小値を最大値より小さくしてください"
            : "−32768〜32767の整数で、最小値を最大値より小さくしてください";
          return;
        }
        plot.range = [low, high];
      }
      draw(plot);
    }
    fixed.addEventListener("change", updateRange);
    min.addEventListener("input", updateRange);
    max.addEventListener("input", updateRange);
    updateRange();
  }
  referenceMode.addEventListener("change", () => {
    attitudePlot.fields = referenceMode.value === "relative" ? relativeFields : attitudeFields;
    redraw();
  });
  updateAttitudeView();
  document.getElementById("graphs-panel").addEventListener("tabshown", redraw);
  window.addEventListener("resize", redraw);
})();
