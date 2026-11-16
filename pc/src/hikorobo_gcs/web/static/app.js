"use strict";

const $ = (id) => document.getElementById(id);
const commandEndpoints = {
  PING: "/api/ping",
  GET_STATUS: "/api/get-status",
  GET_WIFI: "/api/get-wifi",
  GET_RATE: "/api/get-rate",
  SET_RATE: "/api/set-rate",
};
const MAX_LOG_LINES = 200;
let busy = false;

function saveSettings() {
  try {
    localStorage.setItem("hikorobo-gcs-udp-target", JSON.stringify({
      host: $("host").value.trim(),
      port: $("port").value,
      timeout: $("timeout").value,
    }));
  } catch (_) { /* private mode: storage may be disabled */ }
}
function loadSettings() {
  try {
    const saved = JSON.parse(localStorage.getItem("hikorobo-gcs-udp-target") || "null");
    if (saved && typeof saved.host === "string") {
      $("host").value = saved.host;
      $("port").value = saved.port || "5005";
      $("timeout").value = saved.timeout || "1.0";
    }
  } catch (_) { /* use defaults */ }
}
function logLine(kind, message) {
  const line = document.createElement("div");
  line.className = `log-line ${kind}`;
  line.textContent = `[${new Date().toLocaleTimeString("ja-JP", {hour12:false})}] ${message}`;
  const log = $("log");
  log.appendChild(line);
  while (log.childElementCount > MAX_LOG_LINES) log.firstElementChild.remove();
  log.scrollTop = log.scrollHeight;
}
function setLinkStatus(ok, text) {
  const box = $("link-status");
  box.classList.remove("good", "bad");
  box.classList.add(ok ? "good" : "bad");
  $("status-label").textContent = text;
}
function setBusy(value) {
  busy = value;
  document.querySelectorAll("button[data-command], #set-rate").forEach((button) => {
    button.disabled = value;
  });
}
function getTarget() {
  const host = $("host").value.trim();
  const port = Number($("port").value);
  const timeout_sec = Number($("timeout").value);
  if (!host) throw Error("XIAOのIPアドレスを入力してください");
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw Error("UDPポートは1〜65535の整数にしてください");
  if (!Number.isFinite(timeout_sec) || timeout_sec <= 0 || timeout_sec > 10) throw Error("タイムアウトは0より大きく10秒以下にしてください");
  return {host, port, timeout_sec};
}
async function sendCommand(command) {
  if (busy) return;
  let request;
  try {
    request = getTarget();
    if (command === "SET_RATE") {
      const rate = Number($("rate").value);
      if (!Number.isInteger(rate) || rate < 1 || rate > 500) throw Error("RATEは1〜500の整数にしてください");
      request.rate = rate;
    }
  } catch (error) {
    $("latest-result").textContent = String(error.message || error);
    $("latest-result").classList.add("error");
    return;
  }
  saveSettings();
  setBusy(true);
  const commandText = command === "SET_RATE" ? `SET_RATE ${request.rate}` : command;
  logLine("tx", `TX  ${commandText}  →  ${request.host}:${request.port}`);
  $("latest-result").textContent = `${commandText} 実行中...`;
  $("latest-result").classList.remove("error");
  try {
    const resp = await fetch(commandEndpoints[command], {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(request),
    });
    const data = await resp.json();
    if (!resp.ok) {
      const detail = typeof data.detail === "string" ? data.detail : JSON.stringify(data.detail || data);
      throw Error(`HTTP ${resp.status}: ${detail}`);
    }
    if (typeof data.reply !== "string") throw Error("APIの応答形式が不正です");
    const rtt = typeof data.rtt_ms === "number" ? `${data.rtt_ms.toFixed(2)} ms` : "—";
    $("latest-result").textContent = data.reply;
    $("rtt").textContent = `RTT: ${rtt}`;
    logLine("rx", `RX  ${data.reply}   [${rtt}]`);
    setLinkStatus(true, "通信成功");
    if ((command === "GET_RATE" || command === "SET_RATE" || command === "GET_STATUS") && /RATE=(\d+)/.test(data.reply)) {
      $("rate").value = data.reply.match(/RATE=(\d+)/)[1];
    } else if (command === "GET_RATE" || command === "SET_RATE") {
      const match = data.reply.match(/^OK RATE (\d+)$/);
      if (match) $("rate").value = match[1];
    }
  } catch (error) {
    const message = String(error.message || error);
    $("latest-result").textContent = message;
    $("latest-result").classList.add("error");
    $("rtt").textContent = "RTT: —";
    logLine("err", `ERROR  ${message}`);
    setLinkStatus(false, "通信エラー");
  } finally {
    setBusy(false);
  }
}

loadSettings();
for (const button of document.querySelectorAll("button[data-command]")) {
  button.addEventListener("click", () => sendCommand(button.dataset.command));
}
$("set-rate").addEventListener("click", () => sendCommand("SET_RATE"));
$("clear-log").addEventListener("click", () => { $("log").textContent = ""; });
for (const id of ["host", "port", "timeout"]) $(id).addEventListener("change", saveSettings);
