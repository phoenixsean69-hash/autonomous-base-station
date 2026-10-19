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
const AI_FRESH_MS = 30000;

// ABS DASHBOARD MOBILE PRESENCE V1
const BTS_SERVICE_URL =
  process.env.DASHBOARD_BTS_SERVICE_URL ||
  "http://[::1]:8100";

let btsPresence = null;
let btsPresenceUpdatedMs = null;

async function pollBtsPresence() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 1000);
  try {
    const response = await fetch(`${BTS_SERVICE_URL}/telemetry`, { signal: controller.signal });
    if (!response.ok) return;
    btsPresence = await response.json();
    btsPresenceUpdatedMs = Date.now();
  } catch {
    // Keep the last known snapshot.
  } finally {
    clearTimeout(timeout);
  }
}
const TELEMETRY_LIVE_MS = Number(
  process.env.DASHBOARD_TELEMETRY_LIVE_MS || 30000,
);
const TELEMETRY_OFFLINE_MS = Number(
  process.env.DASHBOARD_TELEMETRY_OFFLINE_MS || 120000,
);

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

  const nowMs = Date.now();

  const telemetryAgeMs =
    Number.isFinite(Number(telemetry.updatedMs))
      ? nowMs - Number(telemetry.updatedMs)
      : Infinity;

  const telemetryStatus =
    !telemetry.data
      ? "WAITING"
      : telemetryAgeMs < TELEMETRY_LIVE_MS
        ? "LIVE"
        : telemetryAgeMs < TELEMETRY_OFFLINE_MS
          ? "STALE"
          : "OFFLINE";

  const aiAgeMs =
    Number.isFinite(Number(ai.updatedMs))
      ? nowMs - Number(ai.updatedMs)
      : Infinity;

  // Preserve the latest genuine AI result. Freshness is tracked
  // separately so a real inference is never converted into fake WAITING.
  const liveAi = ai.data;

  const aiFresh =
    Boolean(ai.data) &&
    aiAgeMs < AI_FRESH_MS;

  return {
    serverTimeMs: nowMs,
    telemetry: telemetry.data,
    ai: liveAi,
    bts: btsPresence,
    meta: {
      telemetryUpdatedMs: telemetry.updatedMs,
      telemetryStatus,
      telemetryAgeMs:
        Number.isFinite(telemetryAgeMs)
          ? telemetryAgeMs
          : null,
      telemetryLiveWindowMs: TELEMETRY_LIVE_MS,
      telemetryOfflineWindowMs: TELEMETRY_OFFLINE_MS,
      aiUpdatedMs: ai.updatedMs,
      aiFresh,
      aiAgeMs:
        Number.isFinite(aiAgeMs)
          ? aiAgeMs
          : null,
      aiFreshWindowMs: AI_FRESH_MS,
      btsPresenceUpdatedMs,
      btsPresenceAvailable: Boolean(btsPresence),
      readOnly: true,
    },
  };
}

const app = express();
app.disable("x-powered-by");
app.use(express.json());

app.use(
  "/vendor/three",
  express.static(
    path.join(
      __dirname,
      "node_modules",
      "three",
      "build",
    ),
  ),
);

app.use(
  "/vendor/three-addons",
  express.static(
    path.join(
      __dirname,
      "node_modules",
      "three",
      "examples",
      "jsm",
    ),
  ),
);

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

void pollBtsPresence();
setInterval(pollBtsPresence, 750);

let lastSignature = "";

setInterval(async () => {
  const state = await snapshot();

  const signature = [
    state.telemetry?.timestamp_ms ?? "no-t",
    state.ai?.runtime?.source_timestamp_ms ?? "no-ai",
    state.meta.telemetryUpdatedMs ?? "no-t-file",
    state.meta.aiUpdatedMs ?? "no-ai-file",
    JSON.stringify(state.bts?.subscriber_numbers ?? []),
    JSON.stringify((state.bts?.active_call_pairs ?? []).map((call) => [
      call?.caller ?? "",
      call?.callee ?? "",
      call?.state ?? "",
    ])),
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
