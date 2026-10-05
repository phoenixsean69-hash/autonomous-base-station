const content = document.getElementById("content");
const tabs = [...document.querySelectorAll(".tab")];
const dashboardStatus = document.getElementById("dashboardStatus");
const espStatus = document.getElementById("espStatus");

let activeTab = "overview";
let state = {
  serverTimeMs: Date.now(),
  telemetry: null,
  ai: null,
  meta: {
    telemetryUpdatedMs: null,
    aiUpdatedMs: null,
    readOnly: true,
  },
};
let socketState = "CONNECTING";
let history = [];
let lastTelemetryTimestamp = null;

const MAX_HISTORY = 180;

window.ABSDashboardGetState =
  () => state;

window.ABSDashboardGetActiveTab =
  () => activeTab;

function esc(value) {
  return String(value ?? "—")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function num(value, digits = 1, fallback = "—") {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed.toFixed(digits) : fallback;
}

function pct(value, digits = 1) {
  return `${num(value, digits)}%`;
}

function prob(value, digits = 1) {
  const parsed = Number(value);
  return Number.isFinite(parsed)
    ? `${(parsed * 100).toFixed(digits)}%`
    : "—";
}

function boolText(value, yes = "YES", no = "NO") {
  if (value === true) return yes;
  if (value === false) return no;
  return "—";
}

function ageLabel(ms) {
  if (!Number.isFinite(ms)) return "never";
  if (ms < 1000) return `${Math.max(0, Math.round(ms))} ms`;
  return `${(ms / 1000).toFixed(1)} s`;
}

function ageOf(updatedMs) {
  if (!Number.isFinite(Number(updatedMs))) return Infinity;
  return Date.now() - Number(updatedMs);
}

function tone(value) {
  const v = String(value ?? "").toUpperCase();

  if (
    [
      "LIVE",
      "NORMAL",
      "OK",
      "PASSED",
      "ACCEPT",
      "ACCEPTED",
      "AGREE",
      "AVAILABLE",
      "RUNNING",
      "CONFIRMED",
      "REACHABLE",
      "UP",
      "OPERATIONAL",
      "FRESH",
    ].includes(v)
  ) {
    return "good";
  }

  if (
    [
      "ECO",
      "BATTERY",
      "UNCERTAIN",
      "DEGRADED",
      "WAITING",
      "STARTING",
      "RECONNECTING",
      "STALE",
      "HOLD",
    ].includes(v)
  ) {
    return "warn";
  }

  if (
    v.includes("FAIL") ||
    v.includes("FAULT") ||
    v.includes("CRITICAL") ||
    v.includes("EMERGENCY") ||
    v.includes("OUTAGE") ||
    v.includes("UNKNOWN") ||
    v.includes("DOWN") ||
    v.includes("OFFLINE") ||
    v.includes("DISAGREE")
  ) {
    return "bad";
  }

  return "neutral";
}

function pill(value, label = "") {
  const cls = tone(value);
  return `<span class="pill ${cls}"><i></i>${label ? `${esc(label)}: ` : ""}${esc(value ?? "—")}</span>`;
}

function statCard(label, value, unit = "", note = "", cardTone = "neutral") {
  return `
    <section class="stat-card ${cardTone}">
      <div class="stat-label">${esc(label)}</div>
      <div class="stat-value-row">
        <div class="stat-value">${esc(value)}</div>
        ${unit ? `<div class="stat-unit">${esc(unit)}</div>` : ""}
      </div>
      ${note ? `<div class="stat-note">${esc(note)}</div>` : ""}
    </section>
  `;
}

function probabilityBars(probabilities = {}) {
  const order = ["NORMAL", "LOCAL", "UPSTREAM", "MIXED"];

  return `
    <div class="prob-list">
      ${order
        .map((key) => {
          const value = Math.max(
            0,
            Math.min(100, Number(probabilities?.[key] ?? 0) * 100),
          );

          return `
            <div class="prob-row">
              <div class="prob-head">
                <span>${key}</span>
                <strong>${value.toFixed(2)}%</strong>
              </div>
              <div class="prob-track">
                <div class="prob-fill" style="width:${value}%"></div>
              </div>
            </div>
          `;
        })
        .join("")}
    </div>
  `;
}

function sparkline(values, label, suffix = "", minValue, maxValue) {
  const clean = values.filter(Number.isFinite);

  if (clean.length < 2) {
    return `
      <div class="spark-card">
        <div class="spark-head"><span>${esc(label)}</span><strong>—</strong></div>
        <div class="spark-empty">Collecting history…</div>
      </div>
    `;
  }

  const width = 260;
  const height = 72;
  const localMin = Number.isFinite(minValue) ? minValue : Math.min(...clean);
  const localMax = Number.isFinite(maxValue) ? maxValue : Math.max(...clean);
  const span = Math.max(0.0001, localMax - localMin);

  const points = clean
    .map((value, index) => {
      const x = (index / Math.max(1, clean.length - 1)) * width;
      const y = height - ((value - localMin) / span) * height;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");

  const latest = clean.at(-1);

  return `
    <div class="spark-card">
      <div class="spark-head">
        <span>${esc(label)}</span>
        <strong>${latest.toFixed(1)}${esc(suffix)}</strong>
      </div>
      <svg viewBox="0 0 ${width} ${height}" aria-label="${esc(label)} history">
        <polyline points="${points}" fill="none" stroke="currentColor" stroke-width="3" vector-effect="non-scaling-stroke"></polyline>
      </svg>
    </div>
  `;
}

function gauge(value, threshold) {
  const numeric = Number(value) || 0;
  const th = Number(threshold) || 1;
  const max = Math.max(th * 1.5, numeric, 1);
  const pctValue = Math.max(0, Math.min(100, (numeric / max) * 100));
  const danger = numeric >= th;

  return `
    <div class="gauge ${danger ? "danger" : ""}">
      <div class="gauge-ring" style="--gauge:${pctValue * 3.6}deg">
        <div class="gauge-center">
          <strong>${num(numeric, 2)}</strong>
          <span>anomaly score</span>
        </div>
      </div>
    </div>
  `;
}

function decisionRows(rows) {
  return `
    <div class="decision-list">
      ${rows
        .map(
          ([label, value]) => `
            <div>
              <span>${esc(label)}</span>
              <strong>${esc(value ?? "—")}</strong>
            </div>
          `,
        )
        .join("")}
    </div>
  `;
}

function pipeline() {
  const t = state.telemetry ?? {};
  const ai = state.ai ?? {};
  const cmd = ai?.runtime?.ai_command ?? {};

  const stages = [
    ["ESP32", "Sensors + guardrails", telemetryStatus()],
    ["Laptop AI", ai?.fault_domain?.label ?? "Temporal AI", aiFresh() ? "LIVE" : "WAITING"],
    ["Pico", cmd.pico_ai_fault_domain ?? "Embedded AI", cmd.pico_decision_status ?? "WAITING"],
    ["ESP32", "Final authority", t.guardrail_status ?? "WAITING"],
  ];

  return `
    <div class="pipeline">
      ${stages
        .map(
          ([name, sub, status], index) => `
            <div class="pipeline-item">
              <div class="pipeline-node">
                <div class="pipeline-index">${index + 1}</div>
                <div class="pipeline-copy">
                  <strong>${esc(name)}</strong>
                  <span>${esc(sub)}</span>
                </div>
                ${pill(status)}
              </div>
              ${index < stages.length - 1 ? '<div class="pipeline-arrow">→</div>' : ""}
            </div>
          `,
        )
        .join("")}
    </div>
  `;
}

function telemetryStatus() {
  const explicit =
    String(
      state.meta?.telemetryStatus ?? "",
    ).toUpperCase();

  if (
    ["LIVE", "STALE", "OFFLINE", "WAITING"].includes(explicit)
  ) {
    return explicit;
  }

  // Fallback for an older dashboard server.
  const age = ageOf(
    state.meta?.telemetryUpdatedMs,
  );

  if (!state.telemetry) return "WAITING";
  if (age < 30000) return "LIVE";
  if (age < 120000) return "STALE";
  return "OFFLINE";
}

function telemetryLive() {
  return telemetryStatus() === "LIVE";
}

function aiFresh() {
  if (typeof state.meta?.aiFresh === "boolean") {
    return state.meta.aiFresh;
  }

  return ageOf(state.meta?.aiUpdatedMs) < 30000;
}

function renderOverview() {
  const t = state.telemetry ?? {};
  const ai = state.ai ?? {};
  const cmd = ai?.runtime?.ai_command ?? {};
  const pico = ai?.runtime?.pico_decision ?? {};
  const fault = ai?.fault_domain ?? {};
  const anomaly = ai?.anomaly ?? {};
  const trust = ai?.trust ?? {};

  const battery = Number(t.battery_soc_pct ?? 0);
  const traffic = Number(t.traffic_load_pct ?? 0);

  const batteryTone = battery <= 25 ? "bad" : battery <= 40 ? "warn" : "good";
  const sourceTone =
    t.active_power_source === "NO POWER"
      ? "bad"
      : t.active_power_source === "GENERATOR"
        ? "warn"
        : "good";
  const backhaulTone =
    t.backhaul_status === "CRITICAL"
      ? "bad"
      : t.backhaul_status === "DEGRADED"
        ? "warn"
        : "good";

  return `
    <div class="page-stack">
      <section class="hero-grid">
        <div class="panel hero">
          <div class="eyebrow">LIVE DIGITAL TWIN</div>
          <h1>Autonomous Base Station <span>Control Center</span></h1>
          <p>
            Live visibility across ESP32 telemetry, temporal AI,
            Raspberry Pi Pico validation, power automation, and final
            deterministic guardrails.
          </p>
          <div class="pill-row">
            ${pill(telemetryStatus(), "Telemetry")}
            ${pill(
              state.ai
                ? (aiFresh() ? "LIVE" : "STALE")
                : "WAITING",
              "AI",
            )}
            ${pill(
              state.ai
                ? (cmd.dual_ai_agreement ?? "WAITING")
                : "WAITING",
              "Dual AI",
            )}
          </div>
        </div>

        <div class="panel current-state">
          <div class="eyebrow">CURRENT OPERATING STATE</div>
          <div class="state-big">
            <div><span>Fault domain</span><strong>${esc(fault.label ?? t.fault_label ?? "—")}</strong></div>
            <div><span>Final mode</span><strong>${esc(t.operating_mode ?? "—")}</strong></div>
          </div>
          <div class="state-age">
            <span>Telemetry ${ageLabel(ageOf(state.meta?.telemetryUpdatedMs))}</span>
            <span>AI ${ageLabel(ageOf(state.meta?.aiUpdatedMs))}</span>
          </div>
        </div>
      </section>

      <section class="stats-grid">
        ${statCard(
          "Battery State of Charge",
          num(battery, 1),
          "%",
          `${num(t.battery_voltage_v, 2)} V`,
          batteryTone,
        )}
        ${statCard(
          "Active Power Source",
          t.active_power_source ?? "—",
          "",
          `Grid ${boolText(t.grid_available, "available", "failed")}`,
          sourceTone,
        )}
        ${statCard(
          "Generator",
          boolText(
            t.generator_feedback_running ?? t.generator_running,
            "RUNNING",
            "STOPPED",
          ),
          "",
          t.generator_verification_state ?? "No verification",
          String(t.generator_verification_state ?? "").includes("FAILED") ? "bad" : "neutral",
        )}
        ${statCard(
          "Traffic Load",
          num(traffic, 1),
          "%",
          `Mode ${t.operating_mode ?? "—"}`,
          traffic >= 65 ? "warn" : "neutral",
        )}
        ${statCard(
          "Backhaul",
          t.backhaul_status ?? "—",
          "",
          `${num(t.latency_ms, 0)} ms · ${num(t.packet_loss_pct, 1)}% loss`,
          backhaulTone,
        )}
        ${statCard(
          "Managed Site Power",
          num(t.managed_power_kw, 3),
          "kW",
          `${num(t.energy_saving_pct, 1)}% estimated saving`,
          "neutral",
        )}
      </section>

      <section class="panel">
        <div class="panel-head">
          <div><span class="eyebrow">CONTROL PATH</span><h2>Closed-loop architecture</h2></div>
          <span class="read-only">READ ONLY</span>
        </div>
        ${pipeline()}
      </section>

      <section class="two-col">
        <section class="panel">
          <div class="panel-head">
            <div><span class="eyebrow">TEMPORAL AI</span><h2>Fault-domain inference</h2></div>
            ${pill(trust.decision ?? "WAITING")}
          </div>
          <div class="ai-grid">
            <div>
              <div class="domain-big">
                <span>Predicted domain</span>
                <strong>${esc(fault.label ?? "—")}</strong>
                <small>${fault.confidence == null ? "—" : prob(fault.confidence, 2)}</small>
              </div>
              ${probabilityBars(fault.probabilities)}
            </div>
            ${gauge(anomaly.score, anomaly.threshold)}
          </div>
          <div class="micro-grid">
            <div><span>Threshold</span><strong>${num(anomaly.threshold, 3)}</strong></div>
            <div><span>Margin</span><strong>${num(trust.prediction_margin, 3)}</strong></div>
            <div><span>Entropy</span><strong>${num(trust.normalized_entropy, 3)}</strong></div>
            <div><span>Inference</span><strong>${num(ai?.runtime?.inference_ms, 1)} ms</strong></div>
          </div>
        </section>

        <section class="panel">
          <div class="panel-head">
            <div><span class="eyebrow">DUAL AI + PICO</span><h2>Decision validation</h2></div>
            ${pill(cmd.dual_ai_agreement ?? "WAITING")}
          </div>
          <div class="compare-grid">
            <div class="compare-card">
              <span>Laptop AI</span>
              <strong>${esc(cmd.laptop_ai_fault_domain ?? fault.label ?? "—")}</strong>
              <small>${prob(cmd.laptop_ai_raw_probability, 2)} raw</small>
            </div>
            <div class="compare-card">
              <span>Pico AI</span>
              <strong>${esc(cmd.pico_ai_fault_domain ?? "—")}</strong>
              <small>${prob(cmd.pico_ai_raw_probability, 2)} raw</small>
            </div>
          </div>
          ${decisionRows([
            ["Pico mode", pico.mode_decision],
            ["Power source", pico.power_source_decision],
            ["Generator", pico.generator_action],
            ["Reason", pico.decision_reason],
          ])}
        </section>
      </section>

      <section class="panel">
        <div class="panel-head">
          <div><span class="eyebrow">LIVE HISTORY</span><h2>Recent movement</h2></div>
          <span class="muted">Browser memory · ${history.length} samples</span>
        </div>
        <div class="spark-grid">
          ${sparkline(history.map((x) => x.battery), "Battery", "%", 0, 100)}
          ${sparkline(history.map((x) => x.traffic), "Traffic", "%", 0, 100)}
          ${sparkline(history.map((x) => x.voltage), "DC Bus", "V", 40, 60)}
          ${sparkline(
            history.map((x) => x.latency),
            "Latency",
            "ms",
            0,
            Math.max(250, ...history.map((x) => x.latency), 250),
          )}
        </div>
      </section>
    </div>
  `;
}

function metricGroup(title, eyebrow, items) {
  return `
    <section class="panel metric-group">
      <div class="panel-head">
        <div><span class="eyebrow">${esc(eyebrow)}</span><h3>${esc(title)}</h3></div>
      </div>
      <div class="metric-table">
        ${items
          .map(
            ([label, value, unit = ""]) => `
              <div class="metric-row">
                <span>${esc(label)}</span>
                <strong>${esc(value ?? "—")}${esc(unit)}</strong>
              </div>
            `,
          )
          .join("")}
      </div>
    </section>
  `;
}

function renderConnectionMap2D() {
  const t = state.telemetry ?? {};
  const ai = state.ai ?? {};
  const runtime = ai.runtime ?? {};
  const cmd = runtime.ai_command ?? {};
  const pico = runtime.pico_decision ?? {};

  const source = String(t.network_input_source ?? "UNKNOWN").toUpperCase();
  const telemetryState =
    typeof telemetryStatus === "function"
      ? telemetryStatus()
      : telemetryLive()
        ? "LIVE"
        : "OFFLINE";

  const btsLinked = source === "BTS_BACKHAUL" && telemetryState !== "OFFLINE";
  const linkUp = t.physical_link_up === true;
  const upstream = t.upstream_reachable === true;
  const radio = t.radio_operational === true;
  const activeCalls = Math.max(0, Number(t.network_active_calls ?? 0) || 0);
  const laptopReady = Boolean(state.ai) && aiFresh();
  const picoReady = Boolean(cmd.pico_ai_fault_domain || pico.mode_decision);
  const decisionReady = Boolean(pico.mode_decision);
  const guardPassed = String(t.guardrail_status ?? "").toUpperCase() === "PASSED";
  const networkGood = linkUp && upstream && radio;

  const edge = (ok) => ok ? "map-link live" : "map-link down";
  const node = (ok) => ok ? "connection-node live" : "connection-node down";

  const aiLabel = ai?.fault_domain?.label ?? "WAITING";
  const picoDomain = cmd.pico_ai_fault_domain ?? "WAITING";
  const agreement = cmd.dual_ai_agreement ?? "WAITING";
  const finalMode = t.operating_mode ?? "—";
  const generator =
    (t.generator_feedback_running ?? t.generator_running)
      ? "RUNNING"
      : "STOPPED";

  return `
    <section class="panel connection-map-panel">
      <div class="panel-head">
        <div>
          <span class="eyebrow">LIVE 2D TOPOLOGY</span>
          <h2>Current system connections</h2>
        </div>
        <div class="connection-map-legend">
          <span><i class="legend-dot live"></i>Live</span>
          <span><i class="legend-dot idle"></i>Idle / waiting</span>
          <span><i class="legend-dot down"></i>Unavailable</span>
        </div>
      </div>

      <div class="connection-map-wrap">
        <svg class="connection-map-lines" viewBox="0 0 1200 460" preserveAspectRatio="none" aria-hidden="true">
          <path class="${activeCalls > 0 ? "map-link active" : "map-link idle"}" d="M115 225 C175 225 205 225 270 225"></path>
          <path class="${edge(btsLinked)}" d="M390 225 C455 225 485 225 550 225"></path>
          <path class="${edge(laptopReady)}" d="M670 225 C735 225 765 225 830 225"></path>
          <path class="${edge(picoReady)}" d="M950 225 C1010 225 1035 225 1090 225"></path>
          <path class="${edge(btsLinked)}" d="M330 120 C330 160 330 175 330 205"></path>
          <path class="${edge(decisionReady)}" d="M890 245 C890 300 890 320 890 355"></path>
          <path class="${edge(guardPassed)}" d="M1090 245 C1090 300 1090 320 1090 355"></path>

          ${btsLinked ? `<circle class="flow-dot live" r="5"><animateMotion dur="1.8s" repeatCount="indefinite" path="M390 225 C455 225 485 225 550 225"></animateMotion></circle>` : ""}
          ${laptopReady ? `<circle class="flow-dot live" r="5"><animateMotion dur="2.2s" repeatCount="indefinite" path="M670 225 C735 225 765 225 830 225"></animateMotion></circle>` : ""}
          ${picoReady ? `<circle class="flow-dot live" r="5"><animateMotion dur="2s" repeatCount="indefinite" path="M950 225 C1010 225 1035 225 1090 225"></animateMotion></circle>` : ""}
        </svg>

        <div class="connection-node-grid">
          <article class="${activeCalls > 0 ? "connection-node active" : "connection-node idle"}" style="--x:2%;--y:39%;">
            <div class="node-icon material-symbols-rounded">smartphone</div>
            <div class="node-copy"><span>Mobile users</span><strong>${activeCalls} active call${activeCalls === 1 ? "" : "s"}</strong><small>${activeCalls > 0 ? "CALL IN PROGRESS" : "IDLE"}</small></div>
          </article>

          <article class="${node(btsLinked)}" style="--x:24%;--y:39%;">
            <div class="node-icon material-symbols-rounded">cell_tower</div>
            <div class="node-copy"><span>BTS-001 service</span><strong>:8100</strong><small>${source === "BTS_BACKHAUL" ? "ROUTING TO ABS" : "NOT ACTIVE SOURCE"}</small></div>
          </article>

          <article class="${node(btsLinked)} compact" style="--x:24%;--y:4%;">
            <div class="node-icon material-symbols-rounded">router</div>
            <div class="node-copy"><span>BTS hardware</span><strong>:4010</strong><small>RF / backhaul plant</small></div>
          </article>

          <article class="${node(telemetryState !== "OFFLINE")}" style="--x:47%;--y:39%;">
            <div class="node-icon material-symbols-rounded">developer_board</div>
            <div class="node-copy"><span>Main ESP32</span><strong>:4001</strong><small>${telemetryState} · ${esc(source)}</small></div>
          </article>

          <article class="${node(laptopReady)}" style="--x:70%;--y:39%;">
            <div class="node-icon material-symbols-rounded">neurology</div>
            <div class="node-copy"><span>Laptop AI</span><strong>${esc(aiLabel)}</strong><small>${laptopReady ? "TEMPORAL AI LIVE" : "WAITING / STALE"}</small></div>
          </article>

          <article class="${node(picoReady)}" style="--x:89%;--y:39%;">
            <div class="node-icon material-symbols-rounded">memory</div>
            <div class="node-copy"><span>Raspberry Pi Pico</span><strong>:4000</strong><small>${esc(picoDomain)} · ${esc(agreement)}</small></div>
          </article>

          <article class="${node(decisionReady)} compact" style="--x:70%;--y:75%;">
            <div class="node-icon material-symbols-rounded">account_tree</div>
            <div class="node-copy"><span>Pico decision</span><strong>${esc(pico.mode_decision ?? "WAITING")}</strong><small>${esc(pico.power_source_decision ?? "—")} · ${esc(pico.generator_action ?? "—")}</small></div>
          </article>

          <article class="${node(guardPassed)} compact" style="--x:89%;--y:75%;">
            <div class="node-icon material-symbols-rounded">shield</div>
            <div class="node-copy"><span>ESP32 guardrails</span><strong>${esc(finalMode)}</strong><small>${esc(t.guardrail_status ?? "WAITING")} · GEN ${generator}</small></div>
          </article>
        </div>

        <div class="connection-live-strip">
          <div><span>Backhaul</span><strong>${networkGood ? "AVAILABLE" : "DEGRADED / DOWN"}</strong></div>
          <div><span>Latency</span><strong>${num(t.latency_ms, 1)} ms</strong></div>
          <div><span>Packet loss</span><strong>${num(t.packet_loss_pct, 2)}%</strong></div>
          <div><span>RSSI</span><strong>${num(t.rssi_dbm, 1)} dBm</strong></div>
          <div><span>Traffic</span><strong>${num(t.traffic_load_pct, 1)}%</strong></div>
          <div><span>Radio</span><strong>${radio ? "OPERATIONAL" : "FAULT"}</strong></div>
        </div>
      </div>
    </section>
  `;
}

function renderTelemetry() {
  const status =
    typeof telemetryStatus === "function"
      ? telemetryStatus()
      : telemetryLive()
        ? "LIVE"
        : "OFFLINE";

  return `
    <div class="telemetry-topology-page">
      <section class="telemetry-topology-title">
        <div>
          <span class="eyebrow">LIVE DIGITAL TWIN</span>
          <h1>Live System Topology</h1>
          <p>Real-time service, telemetry, AI, control and power connections across the complete autonomous base-station system.</p>
        </div>

        <div class="topology-title-actions">
          ${pill(status, "Telemetry")}
          ${pill(aiFresh() ? "LIVE" : "STALE", "AI")}
        </div>
      </section>

      ${renderConnectionMap2D()}
    </div>
  `;
}

function renderAI() {
  const ai = state.ai ?? {};
  const trust = ai.trust ?? {};
  const fault = ai.fault_domain ?? {};
  const anomaly = ai.anomaly ?? {};
  const cmd = ai?.runtime?.ai_command ?? {};
  const local = ai?.root_cause?.local ?? null;
  const upstream = ai?.root_cause?.upstream ?? null;

  return `
    <div class="page-stack">
      <section class="page-title">
        <span class="eyebrow">TEMPORAL MODEL STACK</span>
        <h1>AI Intelligence</h1>
        <p>Fault-domain TCN, root-cause heads, anomaly autoencoder, trust gating, and dual-AI agreement.</p>
      </section>

      <section class="two-col">
        <section class="panel">
          <div class="panel-head">
            <div><span class="eyebrow">DOMAIN TCN</span><h2>${esc(fault.label ?? "Waiting for inference")}</h2></div>
            ${pill(trust.decision ?? "WAITING")}
          </div>
          ${probabilityBars(fault.probabilities)}
          <div class="micro-grid">
            <div><span>Top probability</span><strong>${prob(trust.confidence, 2)}</strong></div>
            <div><span>Runner-up</span><strong>${prob(trust.runner_up_probability, 2)}</strong></div>
            <div><span>Margin</span><strong>${num(trust.prediction_margin, 4)}</strong></div>
            <div><span>Entropy</span><strong>${num(trust.normalized_entropy, 4)}</strong></div>
          </div>
          <div class="callout">
            <strong>Probability status</strong>
            <span>${esc(trust.probability_status ?? "—")}</span>
          </div>
        </section>

        <section class="panel">
          <div class="panel-head">
            <div><span class="eyebrow">AUTOENCODER</span><h2>Anomaly detector</h2></div>
            ${pill(anomaly.flagged ? "ANOMALY" : "NORMAL")}
          </div>
          <div class="center-gauge">${gauge(anomaly.score, anomaly.threshold)}</div>
          ${decisionRows([
            ["Threshold", num(anomaly.threshold, 6)],
            ["Score / threshold", `${num(anomaly.score_to_threshold_ratio, 3)}×`],
            ["Model", anomaly.model],
          ])}
        </section>
      </section>

      <section class="two-col">
        <section class="panel">
          <div class="panel-head">
            <div><span class="eyebrow">HIERARCHICAL DIAGNOSIS</span><h2>Root causes</h2></div>
          </div>
          <div class="compare-grid">
            <div class="compare-card">
              <span>Local head</span>
              <strong>${esc(local?.label ?? "NOT APPLICABLE")}</strong>
              <small>${local?.confidence == null ? "No local routing" : prob(local.confidence, 2)}</small>
            </div>
            <div class="compare-card">
              <span>Upstream head</span>
              <strong>${esc(upstream?.label ?? "NOT APPLICABLE")}</strong>
              <small>${upstream?.confidence == null ? "No upstream routing" : prob(upstream.confidence, 2)}</small>
            </div>
          </div>
        </section>

        <section class="panel">
          <div class="panel-head">
            <div><span class="eyebrow">DUAL AI</span><h2>Laptop vs Pico</h2></div>
            ${pill(cmd.dual_ai_agreement ?? "WAITING")}
          </div>
          <div class="compare-grid">
            <div class="compare-card">
              <span>Laptop domain</span>
              <strong>${esc(cmd.laptop_ai_fault_domain ?? fault.label ?? "—")}</strong>
              <small>${prob(cmd.laptop_ai_raw_probability, 2)} raw</small>
            </div>
            <div class="compare-card">
              <span>Pico domain</span>
              <strong>${esc(cmd.pico_ai_fault_domain ?? "—")}</strong>
              <small>${prob(cmd.pico_ai_raw_probability, 2)} raw</small>
            </div>
          </div>
          ${decisionRows([
            ["Agreement", cmd.dual_ai_agreement],
            ["Confidence gap", num(cmd.dual_ai_confidence_gap, 4)],
            ["Trust reason", trust.reason],
            ["Inference time", `${num(ai?.runtime?.inference_ms, 2)} ms`],
          ])}
        </section>
      </section>
    </div>
  `;
}

function renderControl() {
  const t = state.telemetry ?? {};
  const ai = state.ai ?? {};
  const runtime = ai.runtime ?? {};
  const recommendation = runtime.pico_recommendation ?? {};
  const decision = runtime.pico_decision ?? {};
  const cmd = runtime.ai_command ?? {};

  return `
    <div class="page-stack">
      <section class="page-title">
        <span class="eyebrow">DECISION + ACTUATION</span>
        <h1>Control Chain</h1>
        <p>Read-only view of recommendation, Pico validation, ESP32 guardrails, and generator feedback.</p>
      </section>

      <section class="panel">
        <div class="panel-head">
          <div><span class="eyebrow">END-TO-END FLOW</span><h2>Current closed loop</h2></div>
          ${pill(t.guardrail_status ?? "WAITING")}
        </div>
        ${pipeline()}
      </section>

      <section class="three-col">
        <section class="panel">
          <div class="panel-head"><div><span class="eyebrow">AI OUTPUT</span><h3>Recommendation</h3></div></div>
          ${decisionRows([
            ["Mode", recommendation.recommended_mode],
            ["Power", recommendation.recommended_power_source],
            ["Generator", recommendation.generator_recommendation],
            ["Trust", recommendation.trust_decision],
          ])}
        </section>

        <section class="panel">
          <div class="panel-head"><div><span class="eyebrow">PICO OUTPUT</span><h3>Validated decision</h3></div></div>
          ${decisionRows([
            ["Mode", decision.mode_decision],
            ["Power", decision.power_source_decision],
            ["Generator", decision.generator_action],
            ["Reason", decision.decision_reason],
          ])}
        </section>

        <section class="panel">
          <div class="panel-head"><div><span class="eyebrow">ESP32 OUTPUT</span><h3>Applied state</h3></div></div>
          ${decisionRows([
            ["Final mode", t.operating_mode],
            ["Power source", t.active_power_source],
            ["Guardrail", t.guardrail_status],
            ["Actuation", t.pico_actuation_status],
          ])}
        </section>
      </section>

      <section class="two-col">
        <section class="panel">
          <div class="panel-head">
            <div><span class="eyebrow">POWER SYSTEM</span><h2>Generator verification</h2></div>
            ${pill(t.generator_verification_state ?? "WAITING")}
          </div>
          <div class="power-flow">
            <div class="flow-node ${t.grid_available ? "active" : "failed"}">
              <span>GRID</span><strong>${boolText(t.grid_available, "AVAILABLE", "FAILED")}</strong>
            </div>
            <b>→</b>
            <div class="flow-node active">
              <span>BATTERY</span><strong>${num(t.battery_soc_pct, 1)}%</strong>
            </div>
            <b>→</b>
            <div class="flow-node ${t.generator_running ? "active" : ""}">
              <span>GENERATOR</span><strong>${boolText(t.generator_running, "RUNNING", "STOPPED")}</strong>
            </div>
          </div>
          ${decisionRows([
            ["Command GPIO", boolText(t.generator_output_active, "HIGH", "LOW")],
            ["Independent feedback", boolText(t.generator_feedback_running, "RUNNING", "STOPPED")],
            ["Verification reason", t.generator_verification_reason],
            ["Fast power state", t.fast_power_state],
          ])}
        </section>

        <section class="panel">
          <div class="panel-head"><div><span class="eyebrow">SAFETY AUTHORITY</span><h2>Deterministic guardrails</h2></div></div>
          ${decisionRows([
            ["Requested mode", t.requested_mode],
            ["Final mode", t.operating_mode],
            ["Recovery state", t.recovery_state],
            ["Control source", t.pico_control_source ?? cmd.control_source],
          ])}
          <div class="callout">
            <strong>Final mode authority</strong>
            <span>ESP32 deterministic guardrails</span>
          </div>
        </section>
      </section>
    </div>
  `;
}

function renderModels() {
  const c = state.ai?.runtime?.ai_command ?? {};
  const metrics = [
    ["Domain accuracy", c.model_domain_accuracy],
    ["Balanced accuracy", c.model_domain_balanced_accuracy],
    ["Domain macro F1", c.model_domain_macro_f1],
    ["Local head accuracy", c.model_local_head_accuracy],
    ["Upstream head accuracy", c.model_upstream_head_accuracy],
    ["Local end-to-end", c.model_local_e2e_accuracy],
    ["Upstream end-to-end", c.model_upstream_e2e_accuracy],
    ["Mixed exact", c.model_mixed_exact_accuracy],
    ["Hierarchy exact", c.model_hierarchy_accuracy],
    ["Anomaly ROC-AUC", c.model_anomaly_roc_auc],
    ["Anomaly average precision", c.model_anomaly_average_precision],
    ["Anomaly false-positive rate", c.model_anomaly_fpr],
    ["Known-fault detection", c.model_anomaly_detection_rate],
  ];

  return `
    <div class="page-stack">
      <section class="page-title">
        <span class="eyebrow">HELD-OUT VALIDATION</span>
        <h1>Model Performance</h1>
        <p>Fixed validation metrics. These are not live confidence values.</p>
      </section>

      <section class="model-grid">
        ${metrics
          .map(
            ([label, value]) => `
              <div class="model-card">
                <span>${esc(label)}</span>
                <strong>${value == null ? "—" : prob(value, 2)}</strong>
              </div>
            `,
          )
          .join("")}
        <div class="model-card">
          <span>Domain log loss</span>
          <strong>${num(c.model_domain_log_loss, 4)}</strong>
        </div>
      </section>

      <section class="panel">
        <div class="panel-head">
          <div><span class="eyebrow">MODEL CONTEXT</span><h2>Live vs fixed</h2></div>
        </div>
        <div class="explain-grid">
          <div>
            <strong>Live</strong>
            <p>Fault domain, raw probabilities, anomaly score, root causes, trust, Pico AI, Pico decision, control state, and inference timing.</p>
          </div>
          <div>
            <strong>Fixed validation</strong>
            <p>Accuracy, balanced accuracy, F1, log loss, hierarchy performance, ROC-AUC, average precision, false-positive rate, and known-fault detection.</p>
          </div>
        </div>
      </section>
    </div>
  `;
}

function render() {
  window.dispatchEvent(
    new Event(
      "abs-dashboard-before-render",
    ),
  );
  if (activeTab === "overview") content.innerHTML = renderOverview();
  if (activeTab === "telemetry") content.innerHTML = renderTelemetry();
  if (activeTab === "ai") content.innerHTML = renderAI();
  if (activeTab === "control") content.innerHTML = renderControl();
  if (activeTab === "models") content.innerHTML = renderModels();

  dashboardStatus.className = `pill ${tone(socketState)}`;
  dashboardStatus.innerHTML = `<i></i>Dashboard: ${esc(socketState)}`;

  const esp = telemetryStatus();
  espStatus.className = `pill ${tone(esp)}`;
  espStatus.innerHTML = `<i></i>ESP32: ${esc(esp)}`;

  window.dispatchEvent(
    new Event(
      "abs-dashboard-rendered",
    ),
  );
}

function applyState(next) {
  state = next;

  const timestamp = next?.telemetry?.timestamp_ms;

  if (
    timestamp !== undefined &&
    timestamp !== null &&
    timestamp !== lastTelemetryTimestamp
  ) {
    lastTelemetryTimestamp = timestamp;

    history.push({
      timestamp,
      battery: Number(next.telemetry?.battery_soc_pct ?? 0),
      traffic: Number(next.telemetry?.traffic_load_pct ?? 0),
      voltage: Number(next.telemetry?.dc_voltage_v ?? 0),
      latency: Number(next.telemetry?.latency_ms ?? 0),
      rssi: Number(next.telemetry?.rssi_dbm ?? 0),
      anomaly: Number(next.ai?.anomaly?.score ?? 0),
    });

    history = history.slice(-MAX_HISTORY);
  }

  render();
}

tabs.forEach((button) => {
  button.addEventListener("click", () => {
    activeTab = button.dataset.tab;

    tabs.forEach((item) => {
      item.classList.toggle("active", item === button);
    });

    render();
  });
});

async function connect() {
  try {
    const initial = await fetch("/api/state").then((response) => response.json());
    applyState(initial);
  } catch {
    // WebSocket reconnect handles recovery.
  }

  const protocol = location.protocol === "https:" ? "wss" : "ws";
  const socket = new WebSocket(`${protocol}://${location.host}/ws`);

  socket.addEventListener("open", () => {
    socketState = "LIVE";
    render();
  });

  socket.addEventListener("message", (event) => {
    try {
      applyState(JSON.parse(event.data));
    } catch {
      // Ignore malformed dashboard transport payload.
    }
  });

  socket.addEventListener("error", () => {
    socketState = "ERROR";
    render();
  });

  socket.addEventListener("close", () => {
    socketState = "RECONNECTING";
    render();
    window.setTimeout(connect, 1500);
  });
}

function refreshRuntimeStatus() {
  dashboardStatus.className =
    `pill ${tone(socketState)}`;

  dashboardStatus.innerHTML =
    `<i></i>Dashboard: ${esc(socketState)}`;

  const esp =
    typeof telemetryStatus === "function"
      ? telemetryStatus()
      : telemetryLive()
        ? "LIVE"
        : "OFFLINE";

  espStatus.className =
    `pill ${tone(esp)}`;

  espStatus.innerHTML =
    `<i></i>ESP32: ${esc(esp)}`;
}

setInterval(
  refreshRuntimeStatus,
  1000,
);
render();
connect();
