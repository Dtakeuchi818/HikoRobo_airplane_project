"use strict";
const $ = (id) => document.getElementById(id);
const endpoints = {PING:"/api/ping", GET_STATUS:"/api/get-status", GET_WIFI:"/api/get-wifi", GET_RATE:"/api/get-rate", SET_RATE:"/api/set-rate"};
let working = false, probing = false, connected = false, failureStreak = 0, timer = null, lastQuality = null;
const logLines = [];
const maxLines = 300;

function mode() { return document.querySelector('input[name="transport"]:checked').value; }
function setConnection(label, css) {
  const box = $("link-status");
  box.classList.remove("good","bad","warn");
  if (css) box.classList.add(css);
  $("status-label").textContent = label;
}
function setWorking(value) {
  working = value;
  document.querySelectorAll("button:not(#clear-log):not(#save-log):not(#export-csv):not([id^=\"imu-bias-\"])").forEach(b => b.disabled = value);
  // Measuring RTT is a Wi-Fi/UDP diagnostic only.
  if (!value && mode() === "usb") $("run-quality").disabled = true;
  if (!value && mode() === "udp") $("run-quality").disabled = false;
  $("imu-start").disabled = value || imuRunning;
  $("imu-stop").disabled = value || !imuRunning;
  $("imu-csv").disabled = value || imuRows.length === 0;
  updateBiasControls();
}
function logLine(kind, message) {
  const line = `[${new Date().toLocaleTimeString("ja-JP",{hour12:false})}] ${message}`;
  logLines.push(line);
  if (logLines.length > maxLines) logLines.shift();
  const element = document.createElement("div");
  element.className = `log-line ${kind}`;
  element.textContent = line;
  $("log").appendChild(element);
  while ($("log").children.length > maxLines) $("log").firstElementChild.remove();
  $("log").scrollTop = $("log").scrollHeight;
}
function saveSettings() {
  const state = {};
  for (const id of ["host","port","timeout","broadcast","serial-port","baudrate","usb-timeout","rate","quality-count","quality-interval","monitor-interval","imu-interval"]) state[id] = $(id).value;
  state.transport = mode();
  state.monitorEnabled = $("monitor-enabled").checked;
  try { localStorage.setItem("hikorobo-gcs-v4", JSON.stringify(state)); } catch (_) {}
}
function loadSettings() {
  try {
    const old = JSON.parse(localStorage.getItem("hikorobo-gcs-udp-target") || "null");
    const newer = JSON.parse(localStorage.getItem("hikorobo-gcs-v4") || "null");
    if (old && !newer) {
      if (old.host) $("host").value = old.host;
      if (old.port) $("port").value = old.port;
      if (old.timeout) $("timeout").value = old.timeout;
    }
    if (!newer) return;
    for (const [key, value] of Object.entries(newer)) {
      if (key === "transport") {
        const radio = document.querySelector(`input[name="transport"][value="${value}"]`);
        if (radio) radio.checked = true;
      } else if (key === "monitorEnabled") $("monitor-enabled").checked = Boolean(value);
      else if ($(key)) $(key).value = value;
    }
  } catch (_) {}
}
function requestParameters() {
  const isUsb = mode() === "usb";
  const port = Number($("port").value);
  const timeout_sec = Number($(isUsb ? "usb-timeout" : "timeout").value);
  if (!Number.isFinite(timeout_sec) || timeout_sec < 0.1 || timeout_sec > 3) throw Error("タイムアウトは0.1〜3秒にしてください");
  if (isUsb) {
    const serial_port = $("serial-port").value.trim();
    const baudrate = Number($("baudrate").value);
    if (!serial_port) throw Error("USBポートを指定してください");
    return {transport:"usb", serial_port, baudrate, timeout_sec};
  }
  if (!$("host").value.trim()) throw Error("XIAOのIPを入力してください");
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw Error("UDPポートは1〜65535にしてください");
  return {transport:"udp", host:$("host").value.trim(), port, timeout_sec};
}
async function post(url, body = {}) {
  const response = await fetch(url, {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(body)});
  let data;
  try { data = await response.json(); } catch (_) { throw Error(`HTTP ${response.status}: JSONが返りませんでした`); }
  if (!response.ok) {
    const details = typeof data.detail === "string" ? data.detail : JSON.stringify(data.detail || data);
    const err = Error(`HTTP ${response.status}: ${details}`); err.status = response.status; throw err;
  }
  return data;
}
function updateLatest(data) {
  $("latest-result").classList.remove("error");
  $("latest-result").textContent = data.reply || "OK";
  $("rtt").textContent = typeof data.rtt_ms === "number" ? `RTT: ${data.rtt_ms.toFixed(2)} ms` : "RTT: —";
  if (typeof data.rtt_ms === "number") $("live-rtt").textContent = `${data.rtt_ms.toFixed(2)} ms`;
}
function showError(error) {
  const message = String(error.message || error);
  $("latest-result").classList.add("error");
  $("latest-result").textContent = message;
  logLine("err", `ERROR  ${message}`);
}
function registerSuccess(data) {
  connected = true; failureStreak = 0;
  $("fail-streak").textContent = "0";
  $("last-ok").textContent = new Date().toLocaleTimeString("ja-JP",{hour12:false});
  if (typeof data.rtt_ms === "number") $("live-rtt").textContent = `${data.rtt_ms.toFixed(2)} ms`;
  setConnection("応答あり", "good");
}
function registerFailure() {
  failureStreak++;
  $("fail-streak").textContent = String(failureStreak);
  if (failureStreak >= 3) { connected = false; setConnection("応答なし", "bad"); }
  else setConnection("通信不安定", "warn");
}
function stopMonitoring() { if (timer !== null) clearInterval(timer); timer = null; }
function startMonitoring() {
  stopMonitoring();
  if (!$("monitor-enabled").checked || !connected) return;
  const interval = Number($("monitor-interval").value) * 1000;
  timer = setInterval(probe, interval);
}
async function probe(force = false) {
  if (working || probing || (!connected && !force)) return;
  const wasConnected = connected;
  probing = true;
  try { const result = await post("/api/probe", requestParameters()); registerSuccess(result); }
  catch (error) {
    if (error.status !== 409) {
      if (!wasConnected) {
        connected = false; failureStreak = 1;
        $("fail-streak").textContent = "1";
        setConnection("応答なし", "bad");
      } else registerFailure();
    }
  }
  finally { probing = false; }
}
async function connect() {
  if (working) return;
  setWorking(true); stopMonitoring();
  setConnection("確認中", "warn");
  try {
    saveSettings();
    const data = await post("/api/connect", requestParameters());
    registerSuccess(data); updateLatest(data);
    logLine("rx", `CONNECT ${mode().toUpperCase()} → ${data.reply} [${data.rtt_ms.toFixed(2)} ms]`);
    $("connection-detail").textContent = mode() === "usb" ? "USBポートを保持しています。" : "UDPはPING応答をもって接続を確認します。";
    startMonitoring();
  } catch (error) { connected = false; setConnection("接続失敗","bad"); showError(error); }
  finally { setWorking(false); }
}
async function disconnect() {
  if (working) return;
  stopMonitoring(); connected = false;
  setWorking(true);
  try { await post("/api/disconnect"); logLine("info", "DISCONNECT"); }
  catch (error) { showError(error); }
  finally { setWorking(false); setConnection("未接続",null); }
}
async function switchMode() {
  if (working) return;
  const isUsb = mode() === "usb";
  $("udp-fields").hidden = isUsb; $("usb-fields").hidden = !isUsb;
  $("quality-card").hidden = isUsb;
  $("run-quality").disabled = isUsb;
  stopMonitoring(); connected = false; failureStreak = 0;
  $("fail-streak").textContent = "0";
  setConnection("未接続",null);
  saveSettings();
  // Releasing previous USB port before subsequent connects prevents race conditions.
  setWorking(true);
  try { await post("/api/disconnect"); }
  catch(e) { logLine("err", `切替時のポート解放: ${e.message}`); }
  finally { setWorking(false); }
}
async function sendCommand(command) {
  if (working || probing) return;
  let data;
  try {
    data = requestParameters();
    if (command === "SET_RATE") {
      const rate = Number($("rate").value);
      if (!Number.isInteger(rate) || rate < 1 || rate > 500) throw Error("RATEは1〜500の整数で指定してください");
      data.rate = rate;
    }
  } catch (error) { showError(error); return; }
  saveSettings();
  setWorking(true);
  const commandText = command === "SET_RATE" ? `SET_RATE ${data.rate}` : command;
  logLine("tx", `TX ${commandText} (${mode().toUpperCase()})`);
  try {
    const result = await post(endpoints[command], data);
    registerSuccess(result); updateLatest(result);
    logLine("rx", `RX ${result.reply} [${result.rtt_ms.toFixed(2)} ms]`);
    if (["GET_RATE","SET_RATE","GET_STATUS"].includes(command)) {
      const match = result.reply.match(/(?:RATE=|OK RATE )(\d+)/);
      if (match) $("rate").value = match[1];
    }
    startMonitoring();
  } catch (error) { showError(error); if (error.status !== 409) registerFailure(); }
  finally { setWorking(false); }
}
async function discover() {
  if (working) return;
  setWorking(true);
  try {
    const data = await post("/api/discover", {
      broadcast: $("broadcast").value.trim(),
      port: Number($("port").value), timeout_sec: Number($("timeout").value),
    });
    $("host").value = data.host;
    $("port").value = data.port;
    saveSettings();
    logLine("rx", `DISCOVER → ${data.host}:${data.port}`);
    $("latest-result").textContent = `発見: ${data.host}:${data.port}`;
  } catch (error) { showError(error); }
  finally { setWorking(false); }
}
async function refreshSerialPorts() {
  if (working) return;
  try {
    const response = await fetch("/api/serial-ports");
    const data = await response.json();
    if (!response.ok) throw Error(data.detail || `HTTP ${response.status}`);
    const list = $("serial-device-list"); list.textContent = "";
    for (const p of data.ports) {
      const option = document.createElement("option"); option.value = p.device; option.label = p.description; list.append(option);
    }
    $("serial-hint").textContent = data.ports.length ? data.ports.map(p=>`${p.device} (${p.description})`).join(" / ") : "ポートが見つかりません。DockerのUSB割当を確認してください。";
  } catch (e) { $("serial-hint").textContent = e.message; }
}
function csvEscape(v) { const s = String(v ?? ""); return `"${s.replaceAll('"','""')}"`; }
function downloadText(filename, text, mime) {
  const url = URL.createObjectURL(new Blob([text], {type:mime}));
  const a = document.createElement("a"); a.href=url; a.download=filename; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url),1000);
}
function exportQuality() {
  if (!lastQuality) return;
  const header=["index","timestamp_utc","success","rtt_ms","reply"];
  const rows=lastQuality.samples.map(s=>header.map(key=>csvEscape(s[key])).join(","));
  downloadText(`hikorobo_quality_${new Date().toISOString().replaceAll(":","-")}.csv`, [header.join(","), ...rows].join("\r\n"), "text/csv;charset=utf-8");
}
function drawChart(result) {
  const canvas=$("quality-chart");
  const rect=canvas.getBoundingClientRect();
  const w=Math.max(320,Math.floor(rect.width)), h=210, dpr=window.devicePixelRatio||1;
  canvas.width=Math.round(w*dpr); canvas.height=Math.round(h*dpr);
  const ctx=canvas.getContext("2d"); ctx.scale(dpr,dpr); ctx.clearRect(0,0,w,h);
  ctx.fillStyle="#f7fafc"; ctx.fillRect(0,0,w,h);
  const L=47,R=14,T=16,B=30, W=w-L-R, H=h-T-B;
  const values=result?.samples||[];
  const valid=values.filter(x=>x.success).map(x=>x.rtt_ms);
  const maxY=Math.max(10, Math.ceil((Math.max(0,...valid)*1.2)/5)*5);
  ctx.lineWidth=1; ctx.strokeStyle="#dce7f0"; ctx.fillStyle="#6b7e91"; ctx.font="11px sans-serif";
  for(let j=0;j<=4;j++) {
    const y=T+H*j/4;
    ctx.beginPath(); ctx.moveTo(L,y); ctx.lineTo(L+W,y); ctx.stroke();
    ctx.fillText(`${(maxY*(4-j)/4).toFixed(0)}`,5,y+4);
  }
  ctx.fillText("RTT (ms)",5,10); ctx.fillText("試行番号",Math.max(L,w-60),h-5);
  if (!values.length) { ctx.fillStyle="#8091a4";ctx.fillText("まだ測定データがありません",L+18,T+H/2);return; }
  let started=false; ctx.strokeStyle="#247ab6";ctx.lineWidth=1.8;ctx.beginPath();
  for (const s of values) {
    const x=L+(s.index-1)/Math.max(1,values.length-1)*W;
    if(s.success) {
      const y=T+H*(1-s.rtt_ms/maxY);
      if(!started) ctx.moveTo(x,y); else ctx.lineTo(x,y);
      started=true;
    } else {
      ctx.stroke();ctx.beginPath();started=false;
      ctx.strokeStyle="#d16856";ctx.lineWidth=2;
      ctx.moveTo(x-4,T+H-6);ctx.lineTo(x+4,T+H+2);
      ctx.moveTo(x-4,T+H+2);ctx.lineTo(x+4,T+H-6);ctx.stroke();
      ctx.beginPath();ctx.strokeStyle="#247ab6";ctx.lineWidth=1.8;
    }
  }
  ctx.stroke();
  ctx.fillStyle="#6b7e91";ctx.fillText("1",L,h-13);ctx.fillText(String(values.length),w-25,h-13);
}
async function runQuality() {
  if (mode() !== "udp" || working || probing) return;
  let req;
  try {
    req=requestParameters();
    req.count=Number($("quality-count").value); req.interval_ms=Number($("quality-interval").value);
    if (!Number.isInteger(req.count)||req.count<5||req.count>100) throw Error("測定回数は5〜100回にしてください");
    if (!Number.isInteger(req.interval_ms)||req.interval_ms<50||req.interval_ms>1000) throw Error("測定間隔は50〜1000msにしてください");
  } catch(e) { showError(e);return; }
  setWorking(true); $("quality-result").textContent=`測定中… ${req.count}回のPINGを送信しています。`;
  logLine("info", `QUALITY START ${req.count}回 / ${req.interval_ms}ms / UDP ${req.host}:${req.port}`);
  try {
    const data=await post("/api/quality",req);
    lastQuality=data;
    $("quality-loss").textContent=`${data.unanswered_percent.toFixed(1)}%`;
    $("quality-median").textContent=data.median_rtt_ms===null?"—":`${data.median_rtt_ms.toFixed(2)}ms`;
    $("quality-p95").textContent=data.p95_rtt_ms===null?"—":`${data.p95_rtt_ms.toFixed(2)}ms`;
    $("quality-jitter").textContent=data.rtt_variation_ms===null?"—":`${data.rtt_variation_ms.toFixed(2)}ms`;
    $("quality-result").textContent=`${data.success_count}/${data.count}応答 | min ${data.min_rtt_ms ?? "—"}ms / max ${data.max_rtt_ms ?? "—"}ms`;
    $("export-csv").disabled=false; drawChart(data);
    logLine("rx", `QUALITY DONE ${data.success_count}/${data.count} 成功 / 未応答 ${data.unanswered_percent}%`);
    if(data.success_count) registerSuccess({rtt_ms:data.samples.filter(s=>s.success).at(-1).rtt_ms});
    else registerFailure();
  } catch(error) {$("quality-result").textContent=`測定失敗: ${error.message}`;showError(error);}
  finally {setWorking(false);startMonitoring();}
}

loadSettings();
window.addEventListener("resize",()=>drawChart(lastQuality));
drawChart(null);
for (const radio of document.querySelectorAll('input[name="transport"]')) radio.addEventListener("change", switchMode);
// Render stored mode without calling API on load.
$("udp-fields").hidden=mode()==="usb"; $("usb-fields").hidden=mode()!=="usb";
$("quality-card").hidden=mode()==="usb"; $("run-quality").disabled=mode()==="usb";
for (const button of document.querySelectorAll("button[data-command]")) button.addEventListener("click",()=>sendCommand(button.dataset.command));
$("set-rate").addEventListener("click",()=>sendCommand("SET_RATE"));
$("connect").addEventListener("click",connect);
$("disconnect").addEventListener("click",disconnect);
$("probe-now").addEventListener("click",async()=>{
  if(working||probing)return;
  // Manual probe works without a prior 'Connect' on UDP.
  const prior=connected;
  await probe(true);
  if(!prior&&connected)startMonitoring();
});
$("discover").addEventListener("click",discover);
$("refresh-ports").addEventListener("click",refreshSerialPorts);
$("run-quality").addEventListener("click",runQuality);
$("export-csv").addEventListener("click",exportQuality);
$("save-log").addEventListener("click",()=>downloadText(`hikorobo_commands_${new Date().toISOString().replaceAll(":","-")}.txt`,logLines.join("\n"),"text/plain;charset=utf-8"));
$("clear-log").addEventListener("click",()=>{logLines.length=0;$("log").textContent="";});
$("monitor-enabled").addEventListener("change",()=>{saveSettings();startMonitoring();});
$("monitor-interval").addEventListener("change",()=>{saveSettings();startMonitoring();});
for(const id of ["host","port","timeout","broadcast","serial-port","baudrate","usb-timeout","rate","quality-count","quality-interval"]) {
  $(id).addEventListener("change",()=>{
    saveSettings();
    if(["host","port","timeout","serial-port","baudrate","usb-timeout"].includes(id)) {
      stopMonitoring();connected=false;setConnection("設定変更・再確認待ち","warn");
    }
  });
}
fetch("/api/health").then(r=>r.json()).then(d=>{$("server-state").textContent=d.status==="ok"?"FastAPI正常稼働":"サーバー状態不明";}).catch(()=>{$("server-state").textContent="FastAPIに接続できません";});


// --- IMU (GET_IMU_RAW over the existing UDP request/reply channel) ---
let imuTimer = null, imuRunning = false, imuBusy = false, imuFailures = 0;
let imuLastSeq = null, imuLastUpdateMs = 0, imuRows = [];
const imuCsvFields = ["pc_utc","seq","t_us","ax","ay","az","gx","gy","gz","temp","status","rtt_ms"];

function stopImuPolling() {
  if (imuTimer !== null) clearInterval(imuTimer);
  imuTimer = null;
  imuRunning = false;
  $("imu-start").disabled = false;
  $("imu-stop").disabled = true;
}
function imuSetState(value) { $("imu-state").textContent = value; }
function renderImu(result) {
  const s = result.sample;
  for (const key of ["ax","ay","az","gx","gy","gz","temp"]) {
    $("imu-" + key).textContent = String(s[key]);
  }
  $("imu-status").textContent = "0x" + s.status.toString(16).toUpperCase().padStart(2,"0");
  $("imu-seq").textContent = String(s.seq);
  $("imu-t-us").textContent = String(s.t_us);
  $("imu-rtt").textContent = result.rtt_ms.toFixed(2) + " ms";
  $("imu-last").textContent = new Date().toLocaleTimeString("ja-JP", {hour12:false});
  const repeated = imuLastSeq === s.seq;
  imuLastSeq = s.seq;
  if (!repeated) imuLastUpdateMs = Date.now();
  imuSetState(repeated && Date.now() - imuLastUpdateMs > 2000
    ? "注意: XIAO側のサンプル更新が2秒以上止まっています"
    : repeated ? "取得成功（同一サンプル）" : "取得成功（更新あり）");
  imuRows.push({pc_utc:new Date().toISOString(),...s,rtt_ms:result.rtt_ms});
  if (imuRows.length > 1000) imuRows.shift();
  $("imu-csv").disabled = false;
}
async function fetchImuRaw() {
  if (imuBusy || working || probing) return;
  if (mode() !== "udp") { stopImuPolling(); imuSetState("UDP通信を選択してください"); return; }
  if (!connected) { stopImuPolling(); imuSetState("先に接続確認してください"); return; }
  imuBusy = true;
  try {
    const result = await post("/api/get-imu-raw", requestParameters());
    renderImu(result);
    imuFailures = 0;
  } catch (e) {
    // A short PING/command may hold the backend's single-command lock; retry later.
    if (e.status !== 409) {
      imuFailures++;
      imuSetState("取得失敗: " + e.message);
      if (imuFailures >= 3 && imuRunning) {
        stopImuPolling();
        logLine("err", "IMU連続表示を停止しました: " + e.message);
      }
    }
  } finally { imuBusy = false; }
}
function startImuPolling() {
  if (imuRunning) return;
  if (mode() !== "udp") { imuSetState("UDP通信を選択してください"); return; }
  if (!connected) { imuSetState("先に接続確認してください"); return; }
  const ms = Number($("imu-interval").value);
  if (![100,200,500].includes(ms)) { imuSetState("更新頻度が不正です"); return; }
  imuFailures = 0;
  imuRunning = true;
  $("imu-start").disabled = true;
  $("imu-stop").disabled = false;
  imuTimer = setInterval(fetchImuRaw, ms);
  fetchImuRaw();
}
function exportImuCsv() {
  if (!imuRows.length) return;
  const lines = [imuCsvFields.join(","), ...imuRows.map(s => imuCsvFields.map(k=>csvEscape(s[k])).join(","))];
  downloadText("hikorobo_imu_raw_" + new Date().toISOString().replaceAll(":","-") + ".csv",
    lines.join("\r\n"), "text/csv;charset=utf-8");
}
$("imu-once").addEventListener("click", fetchImuRaw);
$("imu-start").addEventListener("click", startImuPolling);
$("imu-stop").addEventListener("click", stopImuPolling);
$("imu-csv").addEventListener("click", exportImuCsv);
$("imu-interval").addEventListener("change", () => {
  saveSettings();
  if (imuRunning) { stopImuPolling(); startImuPolling(); }
});
// Keep existing UI behavior while stopping IMU polling on disconnect/mode changes.
$("disconnect").addEventListener("click", () => { stopImuPolling(); imuSetState("停止中"); });
for (const radio of document.querySelectorAll('input[name="transport"]')) {
  radio.addEventListener("change", () => { stopImuPolling(); $("imu-card").hidden=mode()!=="udp"; });
}
for (const id of ["host","port","timeout","serial-port","baudrate","usb-timeout"]) {
  $(id).addEventListener("change", () => { stopImuPolling(); imuSetState("通信設定変更・再接続待ち"); });
}
$("imu-card").hidden=mode()!=="udp";


// Calibration is owned by the XIAO. Poll the same target that received START.
let biasRequestBusy = false;
let biasPoll = null;
let biasTargetRevision = 0;
const biasAxis = $("imu-bias-axis");
const biasStatus = $("imu-bias-state");

function updateBiasControls() {
  biasAxis.disabled = working || biasRequestBusy || biasPoll !== null;
  $("imu-bias-start").disabled = biasAxis.disabled || !biasAxis.value;
  $("imu-bias-reset").disabled = working || biasRequestBusy;
  $("imu-bias-query").disabled = working || biasRequestBusy;
}

function renderBiasStatus(result) {
  const labels = {
    IDLE: "未補正",
    RUNNING: `XIAOで測定中 · ${result.count}/30`,
    DONE: "測定完了",
    UNSTABLE: "動きまたは軸のずれを検出しました。姿勢と軸を確認してください",
    TIMEOUT: "有効なサンプルが揃わず測定を中止しました",
  };
  biasStatus.textContent = labels[result.state] + (result.applied ? " · 補正適用中" : "");
}

function scheduleBiasPoll(target, deadline) {
  biasPoll = setTimeout(() => {
    biasPoll = null;
    if (Date.now() >= deadline) {
      biasStatus.textContent = "測定結果を確認できませんでした。「状態を確認」を押してください";
      updateBiasControls();
    } else {
      void runBiasCommand("status", target, deadline);
    }
  }, 400);
}

async function runBiasCommand(action, pollTarget = null, deadline = Date.now() + 12_000) {
  if (biasRequestBusy) return;
  clearTimeout(biasPoll);
  biasPoll = null;
  biasRequestBusy = true;
  const revision = biasTargetRevision;
  updateBiasControls();
  try {
    const target = pollTarget ?? requestParameters();
    const body = action === "start" ? {...target, axis: biasAxis.value} : target;
    const result = await post(`/api/imu-bias/${action}`, body);
    if (revision !== biasTargetRevision) return;
    renderBiasStatus(result);
    if (result.state === "RUNNING") scheduleBiasPoll(target, deadline);
  } catch (error) {
    if (revision !== biasTargetRevision) return;
    if (pollTarget && error.status === 409) {
      scheduleBiasPoll(pollTarget, deadline);
    } else {
      biasStatus.textContent = `${error.message} · 必要に応じて「状態を確認」を押してください`;
    }
  } finally {
    biasRequestBusy = false;
    updateBiasControls();
  }
}

biasAxis.addEventListener("change", updateBiasControls);
for (const action of ["start", "reset", "query"]) {
  $(`imu-bias-${action}`).addEventListener("click", () => {
    void runBiasCommand(action === "query" ? "status" : action);
  });
}
for (const id of ["host", "port", "serial-port", "baudrate"]) {
  $(id).addEventListener("change", clearBiasStatus);
}
document.querySelectorAll('input[name="transport"]').forEach(input => {
  input.addEventListener("change", clearBiasStatus);
});
function clearBiasStatus() {
  ++biasTargetRevision;
  clearTimeout(biasPoll);
  biasPoll = null;
  biasStatus.textContent = "状態未確認";
  $("imu-attitude-state").textContent = "設定未確認";
  updateBiasControls();
}
updateBiasControls();


const attitudeForm = $("imu-attitude-form");
const attitudeControls = $("imu-attitude-controls");
const attitudeStatus = $("imu-attitude-state");

async function configureAttitude(apply) {
  if (working || attitudeControls.disabled) return;
  attitudeControls.disabled = true;
  try {
    const target = requestParameters();
    const body = apply ? {
      ...target,
      filter: $("imu-attitude-filter").value,
      tau_ms: Math.round($("imu-attitude-tau").valueAsNumber * 1000),
      kp_milli: Math.round($("imu-attitude-kp").valueAsNumber * 1000),
      ki_milli: Math.round($("imu-attitude-ki").valueAsNumber * 1000),
    } : target;
    const result = await post(`/api/imu-attitude/${apply ? "set" : "get"}`, body);
    if (JSON.stringify(requestParameters()) !== JSON.stringify(target)) {
      attitudeStatus.textContent = "接続先が変わりました。XIAOの設定を取得してください";
      return;
    }
    $("imu-attitude-filter").value = result.filter;
    $("imu-attitude-tau").value = result.tau_ms / 1000;
    $("imu-attitude-kp").value = result.kp_milli / 1000;
    $("imu-attitude-ki").value = result.ki_milli / 1000;
    const selected = result.filter === "MAHONY" ? "Mahonyフィルタ" : "相補フィルタ";
    attitudeStatus.textContent = `XIAOで適用中 · ${selected}`;
  } catch (error) {
    attitudeStatus.textContent = error.message;
  } finally {
    attitudeControls.disabled = false;
  }
}

attitudeForm.addEventListener("submit", event => {
  event.preventDefault();
  void configureAttitude(true);
});
$("imu-attitude-get").addEventListener("click", () => void configureAttitude(false));
