"""HikoRobo GCS: FastAPI hello + HTTP-to-UDP PING bridge."""
from __future__ import annotations

import socket
from ipaddress import IPv4Address

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field

# Reuse the existing Step 3/4 HikoRobo UDP client implementation.
from hikorobo_gcs.udp_cli import exchange

app = FastAPI(title="HikoRobo GCS", version="0.2.0")


class PingRequest(BaseModel):
    host: IPv4Address                  # XIAO IP, e.g. 10.42.0.162
    port: int = Field(default=5005, ge=1, le=65535)
    timeout_sec: float = Field(default=1.0, gt=0, le=10)


@app.get("/", response_class=HTMLResponse)
def index():
    return """<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>HikoRobo GCS</title>
  <style>
    body { font-family: system-ui, sans-serif; max-width: 640px; margin: 3rem auto; padding: 0 1rem; }
    label { display: inline-block; margin: .5rem 1rem .5rem 0; }
    input { padding: .5rem; }
    button { padding: .55rem 1.2rem; cursor: pointer; }
    pre { background: #f2f4f7; padding: 1rem; white-space: pre-wrap; overflow-wrap: anywhere; }
  </style>
</head>
<body>
  <h1>Hello, HikoRobo!</h1>
  <p>FastAPI + Uvicorn + Docker is running.</p>
  <h2>XIAO UDP PING</h2>
  <form id="ping-form">
    <label>XIAO IP<br><input id="host" required placeholder="10.42.0.162" value="10.42.0.162"></label>
    <label>UDP port<br><input id="port" type="number" min="1" max="65535" value="5005" required></label>
    <label>Timeout (s)<br><input id="timeout" type="number" min="0.1" max="10" step="0.1" value="1.0" required></label>
    <div><button id="ping-button" type="submit">PINGを送信</button></div>
  </form>
  <pre id="result" role="status">未実行</pre>
  <p><a href="/api/hello">Hello JSON</a> / <a href="/docs">API docs</a></p>
  <script>
    const form = document.getElementById("ping-form");
    const output = document.getElementById("result");
    const button = document.getElementById("ping-button");
    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      button.disabled = true;
      output.textContent = "通信中...";
      try {
        const response = await fetch("/api/ping", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            host: document.getElementById("host").value.trim(),
            port: Number(document.getElementById("port").value),
            timeout_sec: Number(document.getElementById("timeout").value)
          })
        });
        const data = await response.json();
        output.textContent = `HTTP ${response.status}\n` + JSON.stringify(data, null, 2);
      } catch (error) {
        output.textContent = "ブラウザからサーバーへの接続エラー: " + String(error);
      } finally {
        button.disabled = false;
      }
    });
  </script>
</body>
</html>"""


@app.get("/api/hello")
def hello():
    return {"message": "Hello, HikoRobo!"}


@app.get("/api/health")
def health():
    return {"status": "ok"}


@app.post("/api/ping")
def ping_xiao(request: PingRequest):
    """HTTP -> one UDP command -> HTTP; one socket per request."""
    peer = (str(request.host), request.port)
    try:
        # Fresh source port per request: replies won't be mixed with other requests.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            reply, rtt_ms = exchange(sock, peer, "PING", request.timeout_sec)
    except TimeoutError as exc:
        raise HTTPException(status_code=504, detail=f"XIAOのUDP応答がありません: {exc}") from exc
    except OSError as exc:
        raise HTTPException(status_code=502, detail=f"UDP通信エラー: {exc}") from exc

    if reply != "OK PONG":
        raise HTTPException(status_code=502, detail=f"予期しないXIAO応答: {reply}")
    return {"success": True, "reply": reply, "rtt_ms": round(rtt_ms, 2)}