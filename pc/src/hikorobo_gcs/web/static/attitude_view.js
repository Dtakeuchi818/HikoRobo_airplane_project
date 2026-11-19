"use strict";

window.createAttitudeView = (canvas, label) => {
  const colors = ["#b32929", "#18724b", "#245bb8"];
  let angles = null;
  let azimuth = Math.PI / 5;
  let elevation = Math.PI / 6;
  let drag = null;

  function axesFromEuler(angles) {
    const [roll, pitch, yaw] = angles.map(angle => angle * Math.PI / 180);
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

  function draw() {
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (!width || !height) return;
    const pixelRatio = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * pixelRatio);
    canvas.height = Math.round(height * pixelRatio);
    const ctx = canvas.getContext("2d");
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
    if (angles) drawAxes(axesFromEuler(angles), label, 1.06, []);
  }

  canvas.addEventListener("pointerdown", event => {
    if (event.button !== 0 || drag) return;
    canvas.setPointerCapture(event.pointerId);
    drag = {id: event.pointerId, x: event.clientX, y: event.clientY};
  });
  canvas.addEventListener("pointermove", event => {
    if (drag?.id !== event.pointerId) return;
    const radiansPerPixel = 0.01;
    azimuth = (azimuth + (event.clientX - drag.x) * radiansPerPixel) % (2 * Math.PI);
    elevation = Math.max(-Math.PI / 2, Math.min(Math.PI / 2,
      elevation + (event.clientY - drag.y) * radiansPerPixel));
    drag.x = event.clientX;
    drag.y = event.clientY;
    draw();
  });
  canvas.addEventListener("lostpointercapture", event => {
    if (drag?.id === event.pointerId) drag = null;
  });

  return value => {
    angles = value;
    draw();
  };
};
