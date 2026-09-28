import express from "express";
import { createServer } from "node:http";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer } from "ws";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "..");
const runtimeDir = path.join(repoRoot, "bridge", "runtime");

const telemetryFile = path.join(runtimeDir, "latest_telemetry.json");
const aiFile = path.join(runtimeDir, "latest_ai_result.json");
const port = Number(process.env.DASHBOARD_PORT || 5173);

async function readJsonWithMeta(filePath) {
  try {
    const [raw, stat] = await Promise.all([
      fs.readFile(filePath, "utf8"),
      fs.stat(filePath),
    ]);

    return {
      data: JSON.parse(raw),
      updatedMs: stat.mtimeMs,
    };
  } catch {
    return {
      data: null,
      updatedMs: null,
    };
  }
}

async function snapshot() {
  const [telemetry, ai] = await Promise.all([
    readJsonWithMeta(telemetryFile),
    readJsonWithMeta(aiFile),
  ]);

  return {
    serverTimeMs: Date.now(),
    telemetry: telemetry.data,
    ai: ai.data,
    meta: {
      telemetryUpdatedMs: telemetry.updatedMs,
      aiUpdatedMs: ai.updatedMs,
      readOnly: true,
    },
  };
}

const app = express();
app.disable("x-powered-by");
app.use(express.json());

app.get("/api/health", async (_req, res) => {
  const state = await snapshot();

  res.json({
    ok: true,
    readOnly: true,
    telemetryAvailable: Boolean(state.telemetry),
    aiAvailable: Boolean(state.ai),
    telemetryUpdatedMs: state.meta.telemetryUpdatedMs,
    aiUpdatedMs: state.meta.aiUpdatedMs,
  });
});

app.get("/api/state", async (_req, res) => {
  res.json(await snapshot());
});

app.use(express.static(__dirname));

const httpServer = createServer(app);
const wss = new WebSocketServer({
  server: httpServer,
  path: "/ws",
});

function broadcast(state) {
  const payload = JSON.stringify(state);

  for (const client of wss.clients) {
    if (client.readyState === 1) {
      client.send(payload);
    }
  }
}

wss.on("connection", async (socket) => {
  socket.send(JSON.stringify(await snapshot()));
});

let lastSignature = "";

setInterval(async () => {
  const state = await snapshot();

  const signature = [
    state.telemetry?.timestamp_ms ?? "no-t",
    state.ai?.runtime?.source_timestamp_ms ?? "no-ai",
    state.meta.telemetryUpdatedMs ?? "no-t-file",
    state.meta.aiUpdatedMs ?? "no-ai-file",
  ].join(":");

  if (signature !== lastSignature) {
    lastSignature = signature;
    broadcast(state);
  }
}, 250);

httpServer.listen(port, "127.0.0.1", () => {
  console.log("");
  console.log("============================================================");
  console.log(" AUTONOMOUS BASE STATION - LIVE JS DASHBOARD");
  console.log("============================================================");
  console.log(`Dashboard : http://127.0.0.1:${port}`);
  console.log("Mode      : READ-ONLY MONITORING");
  console.log("Telemetry : bridge/runtime/latest_telemetry.json");
  console.log("AI        : bridge/runtime/latest_ai_result.json");
  console.log("");
});
