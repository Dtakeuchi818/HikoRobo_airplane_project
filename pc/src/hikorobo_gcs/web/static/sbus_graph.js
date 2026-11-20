(() => {
  const button = document.getElementById("sbus-toggle");
  const state = document.getElementById("sbus-state");
  const hostInput = document.getElementById("host");
  const chart = document.getElementById("sbus-channels");
  let socket = null;
  let idleTimer;

  function makeBars(container, angles = false) {
    return Array.from({length: 8}, (_, i) => {
      const item = document.createElement("div");
      const label = document.createElement("span");
      label.textContent = `CH${i + 1}`;
      const track = document.createElement("div");
      track.className = angles ? "sbus-track sbus-angle-track" : "sbus-track";
      track.setAttribute("aria-hidden", "true");
      const bar = document.createElement("div");
      bar.className = "sbus-bar";
      const value = document.createElement("strong");
      track.append(bar);
      item.append(label, track, value);
      const normalized = document.createElement("small");
      if (!angles) item.append(normalized);
      container.append(item);
      return {bar, value, normalized};
    });
  }

  const channels = makeBars(chart);
  const angles = makeBars(document.getElementById("sbus-angles"), true);
  const modeState = document.getElementById("sbus-mode-state");
  const servoPulses = document.querySelectorAll("[data-servo-pwm]");
  const attitudeState = document.getElementById("sbus-attitude-state");
  const attitudeReadings = document.getElementById("sbus-attitude-readings");
  const drawAttitude = window.createAttitudeView(document.getElementById("sbus-attitude-view"), "目標");
  const axisLabels = ["ロール", "ピッチ", "ヨー"];
  const captureButtons = [...document.querySelectorAll("[data-capture]")];
  let latest = null;

  function renderAttitude() {
    const mapping = latest?.attitude_channels;
    const targets = mapping?.map(ch => latest.mode === 2 ? latest.angles[ch - 1] : null);
    const valid = targets?.every(Number.isFinite);
    const message = !latest ? "目標姿勢データ待ち" : !mapping ? "CH 割り当てデータなし"
      : latest.mode !== 2 ? "目標姿勢指令モード待ち" : !valid ? "割り当て CH の角度指令が無効" : "受信中";
    if (attitudeState.textContent !== message) attitudeState.textContent = message;
    attitudeReadings.textContent = axisLabels.map((axis, i) => {
      const channel = mapping ? ` (CH${mapping[i]})` : "";
      return `${axis}${channel} ${Number.isFinite(targets?.[i]) ? targets[i].toFixed(2) + "°" : "—"}`;
    }).join(" / ");
    drawAttitude(valid ? targets : null);
  }

  function render(sample, message) {
    latest = sample;
    captureButtons.forEach(button => { button.disabled = !sample || sample.has_frame === false; });
    servoPulses.forEach((output, i) => {
      const width = sample?.servo_pwm_us?.[i];
      const text = Number.isInteger(width) ? `${width} µs` : "—";
      if (output.value !== text) output.value = text;
    });
    channels.forEach(({bar, value, normalized}, i) => {
      const raw = sample?.channels[i];
      bar.style.height = `${(raw ?? 0) / 2047 * 100}%`;
      const fraction = sample?.normalized?.[i];
      value.textContent = raw ?? "—";
      normalized.textContent = fraction == null ? "—" : fraction.toFixed(3);
    });
    angles.forEach(({bar, value}, i) => {
      const angle = sample?.angles?.[i];
      const height = Math.abs(angle ?? 0) / 180 * 50;
      bar.style.height = `${height}%`;
      bar.style.bottom = `${angle < 0 ? 50 - height : 50}%`;
      value.textContent = angle == null ? "—" : `${angle.toFixed(2)}°`;
    });
    const modeLabel = ["モード未選択・角度指令無効", "舵面直接操作モード", "目標姿勢指令モード"][sample?.mode] ?? "モード未受信";
    if (modeState.textContent !== modeLabel) modeState.textContent = modeLabel;
    if (state.textContent !== message) state.textContent = message;
    chart.classList.toggle("sbus-warning", Boolean(sample?.failsafe || sample?.lost));
    renderAttitude();
  }

  function stop(message) {
    const current = socket;
    socket = null;
    current?.close();
    clearTimeout(idleTimer);
    render(null, message);
    button.textContent = "ライブ表示開始";
  }

  button.addEventListener("click", () => {
    if (socket) {
      stop("停止中");
      return;
    }
    const host = hostInput.value.trim();
    const octets = host.split(".");
    if (octets.length !== 4 || octets.some(part => !/^(0|[1-9][0-9]{0,2})$/.test(part) || Number(part) > 255)) {
      render(null, "接続設定にXIAOのIPv4アドレスを入力してください");
      return;
    }
    const url = new URL("/ws/sbus", window.location.href);
    url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
    url.searchParams.set("host", host);
    const current = new WebSocket(url);
    socket = current;
    render(null, "接続中");
    button.textContent = "ライブ表示停止";
    current.addEventListener("open", () => {
      if (socket === current) render(null, "S.BUSデータ待ち");
    });
    current.addEventListener("message", event => {
      if (socket !== current) return;
      const sample = JSON.parse(event.data).samples.at(-1);
      const message = sample.has_frame === false ? "S.BUSフレーム待ち"
        : sample.failsafe ? "フェイルセーフ" : sample.lost ? "フレーム欠落" : "受信中";
      render(sample, message);
      clearTimeout(idleTimer);
      idleTimer = setTimeout(() => render(null, "S.BUSデータの受信が停止"), 1000);
    });
    current.addEventListener("close", () => {
      if (socket === current) stop("WebSocket切断");
    });
  });

  hostInput.addEventListener("change", () => {
    if (socket) stop("接続先が変更されました");
  });
  const channelSelect = document.getElementById("sbus-channel-select");
  const modeSelect = document.getElementById("sbus-mode-select");
  for (let ch = 1; ch <= 8; ch++) {
    channelSelect.add(new Option(`CH${ch}`, ch));
    modeSelect.add(new Option(`CH${ch}`, ch));
  }

  function settingsTarget() {
    return {
      transport: "udp",
      host: hostInput.value.trim(),
      port: Number(document.getElementById("port").value),
      timeout_sec: Number(document.getElementById("timeout").value),
    };
  }

  async function settingsRequest(section, values, target = settingsTarget()) {
    const response = await fetch(`/api/sbus/${section}`, {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({...target, values}),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(typeof result.detail === "string" ? result.detail : "通信設定と入力値を確認してください");
    return result;
  }

  function settingsIssue(section, values) {
    if (section === "mode") {
      const [, surfaceMin, surfaceMax, attitudeMin, attitudeMax] = values;
      if (surfaceMin > surfaceMax || attitudeMin > attitudeMax ||
          !(surfaceMax < attitudeMin || attitudeMax < surfaceMin)) {
        return "範囲の順序と重複を確認してください";
      }
    } else if (!(values[0] < values[1] && values[1] < values[2])) {
      return "最小値 < 中央値 < 最大値 にしてください";
    }
    return "";
  }

  function bindSettings(form, section, names, degreeStart) {
    const fields = names.map(name => form.elements.namedItem(name));
    const status = form.querySelector('[role="status"]');
    const load = form.querySelector("[data-read]");
    const submit = form.querySelector('[type="submit"]');
    let busy = false;
    let baseline;
    const fieldValue = (field, i) => Math.round(Number(field.value) * (i >= degreeStart ? 100 : 1));
    function clear() {
      fields.forEach(field => { field.value = ""; });
      baseline = fields.map(field => field.value);
      status.textContent = "取得を押して XIAO の設定を読み込んでください";
    }

    async function update(write) {
      if (busy) return;
      const selected = section();
      const host = hostInput.value;
      const values = write ? fields.map(fieldValue) : undefined;
      const issue = write ? settingsIssue(selected, values) : "";
      if (issue) {
        status.textContent = issue;
        return;
      }
      busy = true;
      load.disabled = submit.disabled = true;
      status.textContent = write ? "適用中…" : "取得中…";
      try {
        const result = await settingsRequest(selected, values);
        if (selected !== section() || host !== hostInput.value) return;
        fields.forEach((field, i) => { field.value = result.values[i] / (i >= degreeStart ? 100 : 1); });
        baseline = fields.map(field => field.value);
        status.textContent = write ? "XIAO に適用済み" : "XIAO から取得済み";
      } catch (error) {
        if (selected === section() && host === hostInput.value) status.textContent = error.message;
      } finally {
        busy = false;
        load.disabled = submit.disabled = false;
      }
    }

    load.addEventListener("click", () => update(false));
    form.addEventListener("submit", event => { event.preventDefault(); update(true); });
    hostInput.addEventListener("change", clear);
    clear();
    return {
      clear,
      draft() {
        const selected = section();
        const values = fields.map((field, i) => {
          if (field.value === baseline[i] && !field.validity.badInput) return undefined;
          if (field.value === "" || !field.checkValidity()) {
            throw new Error(`${selected === "mode" ? "モード設定" : `CH${selected}`} の入力値を確認してください`);
          }
          return fieldValue(field, i);
        });
        return [selected, values];
      },
    };
  }

  const channelSettings = bindSettings(document.getElementById("sbus-channel-form"), () => channelSelect.value,
    ["raw_min", "raw_center", "raw_max", "surface_min", "surface_max", "attitude_min", "attitude_max"], 3);
  channelSelect.addEventListener("change", channelSettings.clear);
  const modeSettings = bindSettings(document.getElementById("sbus-mode-form"), () => "mode",
    ["channel", "surface_min", "surface_max", "attitude_min", "attitude_max"], Infinity);
  captureButtons.forEach(button => {
    button.addEventListener("click", () => {
      if (latest) button.form.elements.namedItem(button.dataset.capture).value = latest.channels[Number(channelSelect.value) - 1];
    });
  });
  const save = document.getElementById("sbus-save");
  const applyDefaults = document.getElementById("sbus-apply-defaults");
  const saveState = document.getElementById("sbus-save-state");
  save.addEventListener("click", async () => {
    save.disabled = true;
    saveState.textContent = "保存中…";
    try {
      await settingsRequest("save");
      saveState.textContent = "XIAO に保存済み";
    } catch (error) {
      saveState.textContent = error.message;
    } finally {
      save.disabled = false;
    }
  });

  applyDefaults.addEventListener("click", async () => {
    if (!window.confirm("全 CH とモードの現在値を、ファームウェアに組み込まれた初期値に切り替えますか？")) return;
    applyDefaults.disabled = true;
    save.disabled = true;
    saveState.textContent = "初期値を適用中…";
    try {
      await settingsRequest("apply-defaults");
      channelSettings.clear();
      modeSettings.clear();
      saveState.textContent = "初期値を適用しました。再起動後も使う場合は保存してください";
    } catch (error) {
      saveState.textContent = error.message;
    } finally {
      applyDefaults.disabled = false;
      save.disabled = false;
    }
  });

  const exportButton = document.getElementById("sbus-export");
  const exportState = document.getElementById("sbus-export-state");
  exportButton.addEventListener("click", async () => {
    exportButton.disabled = true;
    exportState.textContent = "XIAO から設定を取得中…";
    try {
      const target = settingsTarget();
      const drafts = new Map([channelSettings.draft(), modeSettings.draft()]);
      async function readSection(section, label) {
        const {values} = await settingsRequest(section, undefined, target);
        const draft = drafts.get(section);
        const merged = values.map((value, i) => draft?.[i] ?? value);
        const issue = settingsIssue(section, merged);
        if (issue) throw new Error(`${label} — ${issue}`);
        return merged;
      }
      const [switch_channel, surfaceMin, surfaceMax, attitudeMin, attitudeMax] =
        await readSection("mode", "モード設定");
      const config = {
        attitude_axes: {},
        mode: {
          switch_channel,
          surface_raw: [surfaceMin, surfaceMax],
          attitude_raw: [attitudeMin, attitudeMax],
        },
        channels: {},
      };
      const {values: [roll, pitch, yaw]} = await settingsRequest("axes", undefined, target);
      config.attitude_axes = {roll, pitch, yaw};
      for (let ch = 1; ch <= 8; ch++) {
        const values = await readSection(String(ch), `CH${ch}`);
        config.channels[`CH${ch}`] = {
          raw: values.slice(0, 3),
          surface_deg: values.slice(3, 5).map(value => value / 100),
          attitude_deg: values.slice(5, 7).map(value => value / 100),
        };
      }
      const file = new Blob([JSON.stringify(config, null, 2) + "\n"], {type: "application/json"});
      const link = document.createElement("a");
      link.href = URL.createObjectURL(file);
      link.download = "sbus_defaults.json";
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 1000);
      exportState.textContent = "sbus_defaults.json を作成しました";
    } catch (error) {
      exportState.textContent = error.message;
    } finally {
      exportButton.disabled = false;
    }
  });

  document.getElementById("graphs-panel").addEventListener("tabshown", renderAttitude);
  window.addEventListener("resize", renderAttitude);
  render(null, "停止中");
})();
