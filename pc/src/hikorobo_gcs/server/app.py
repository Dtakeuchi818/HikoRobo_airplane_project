from fastapi import FastAPI
from fastapi.responses import HTMLResponse

app = FastAPI(title="HikoRobo GCS")


@app.get("/", response_class=HTMLResponse)
def index():
    return """
    <html>
      <head>
        <meta charset="utf-8">
        <title>HikoRobo GCS</title>
      </head>
      <body>
        <h1>Hello, HikoRobo!</h1>
        <p>FastAPI + Uvicorn + Docker is running.</p>
      </body>
    </html>
    """


@app.get("/api/hello")
def hello():
    return {"message": "Hello, HikoRobo! GCS v2!"}


@app.get("/api/health")
def health():
    return {"status": "ok"}