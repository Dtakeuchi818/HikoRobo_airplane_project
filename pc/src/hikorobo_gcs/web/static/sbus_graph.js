(() => {
  const button = document.getElementById("sbus-toggle");
  const state = document.getElementById("sbus-state");
  const hostInput = document.getElementById("host");
  const chart = document.getElementById("sbus-channels");
  let socket = null;
  let idleTimer;

  const channels = Array.from({length: 8}, (_, i) => {
    const item = document.createElement("div");
    const label = document.createElement("span");
    label.textContent = `CH${i + 1}`;
    const track = document.createElement("div");
    track.className = "sbus-track";
    track.setAttribute("aria-hidden", "true");
    const bar = document.createElement("div");
    bar.className = "sbus-bar";
    const value = document.createElement("strong");
    track.append(bar);
    item.append(label, track, value);
    chart.append(item);
    return {bar, value};
  });

  function render(sample, message) {
    channels.forEach(({bar, value}, i) => {
      const raw = sample?.channels[i];
      bar.style.height = `${(raw ?? 0) / 2047 * 100}%`;
      value.textContent = raw ?? "—";
    });
    if (state.textContent !== message) state.textContent = message;
    chart.classList.toggle("sbus-warning", Boolean(sample?.failsafe || sample?.lost));
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
      const message = sample.failsafe ? "フェイルセーフ" : sample.lost ? "フレーム欠落" : "受信中";
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
  render(null, "停止中");
})();
