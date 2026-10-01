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
    ["ESP32", "Sensors + guardrails", telemetryLive() ? "LIVE" : "OFFLINE"],
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

function telemetryLive() {
  // ESP32 machine telemetry is emitted about every 2 seconds.
  // Allow several simulation cycles before declaring the source offline so
  // one delayed RFC2217/Wokwi packet cannot make the UI flap LIVE/OFFLINE.
  return ageOf(state.meta?.telemetryUpdatedMs) < 8000;
}

function aiFresh() {
  return ageOf(state.meta?.aiUpdatedMs) < 15000;
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
            ${pill(telemetryLive() ? "LIVE" : "OFFLINE", "Telemetry")}
            ${pill(aiFresh() ? "LIVE" : "WAITING", "AI")}
            ${pill(cmd.dual_ai_agreement ?? "WAITING", "Dual AI")}
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

function renderTelemetry() {
  const t = state.telemetry ?? {};

  const groups = [
    metricGroup("Thermal", "ENVIRONMENT", [
      ["Shelter temperature", num(t.shelter_temp_c, 2), " °C"],
      ["Humidity", num(t.humidity_pct, 2), " %"],
      ["PA temperature", num(t.pa_temp_c, 2), " °C"],
      ["PA temperature trend", num(t.pa_temp_trend_c_per_min, 3), " °C/min"],
      ["PA - shelter delta", num(t.pa_shelter_delta_c, 2), " °C"],
    ]),
    metricGroup("Vibration", "MECHANICAL", [
      ["RMS", num(t.vibration_rms_mps2, 4), " m/s²"],
      ["Standard deviation", num(t.vibration_std_mps2, 4), " m/s²"],
      ["Peak-to-peak", num(t.vibration_peak_to_peak_mps2, 4), " m/s²"],
      ["Dominant frequency", num(t.vibration_dominant_hz, 2), " Hz"],
    ]),
    metricGroup("DC + Battery", "POWER", [
      ["DC voltage", num(t.dc_voltage_v, 2), " V"],
      ["DC current", num(t.dc_current_a, 2), " A"],
      ["DC power", num(t.dc_power_w, 2), " W"],
      ["Battery voltage", num(t.battery_voltage_v, 3), " V"],
      ["Battery State of Charge", num(t.battery_soc_pct, 2), " %"],
      ["Battery SoC trend", num(t.battery_soc_trend_pct_per_min, 3), " %/min"],
    ]),
    metricGroup("RF", "RADIO FREQUENCY", [
      ["Forward power", num(t.rf_forward_w, 2), " W"],
      ["Reflection ratio", num(t.rf_reflection_ratio_pct, 3), " %"],
      ["Reflected power", num(t.rf_reflected_w, 2), " W"],
      ["VSWR", num(t.vswr, 3)],
      ["Return loss", num(t.return_loss_db, 3), " dB"],
    ]),
    metricGroup("Network", "BACKHAUL", [
      ["Latency", num(t.latency_ms, 2), " ms"],
      ["Latency jitter", num(t.latency_jitter_ms, 3), " ms"],
      ["Packet loss", num(t.packet_loss_pct, 3), " %"],
      ["RSSI", num(t.rssi_dbm, 2), " dBm"],
      ["RSSI drop", num(t.rssi_drop_db, 2), " dB"],
      ["Traffic load", num(t.traffic_load_pct, 2), " %"],
    ]),
    metricGroup("Equipment + Connectivity", "BOOLEAN INPUTS", [
      ["Physical link", boolText(t.physical_link_up, "UP", "DOWN")],
      ["Upstream reachable", boolText(t.upstream_reachable, "YES", "NO")],
      ["Grid available", boolText(t.grid_available, "YES", "NO")],
      ["Generator running", boolText(t.generator_running, "YES", "NO")],
      ["Cooling fan", boolText(t.fan_operational, "OPERATIONAL", "FAILED")],
      ["Rectifier", boolText(t.rectifier_normal, "NORMAL", "FAULT")],
      ["Radio subsystem", boolText(t.radio_operational, "OPERATIONAL", "FAULT")],
    ]),
  ];

  return `
    <div class="page-stack">
      <section class="page-title">
        <span class="eyebrow">33-FEATURE INPUT WINDOW</span>
        <h1>Live Telemetry</h1>
        <p>The same engineering signals feeding the temporal AI models.</p>
      </section>
      <div class="metric-groups">${groups.join("")}</div>
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
  if (activeTab === "overview") content.innerHTML = renderOverview();
  if (activeTab === "telemetry") content.innerHTML = renderTelemetry();
  if (activeTab === "ai") content.innerHTML = renderAI();
  if (activeTab === "control") content.innerHTML = renderControl();
  if (activeTab === "models") content.innerHTML = renderModels();

  dashboardStatus.className = `pill ${tone(socketState)}`;
  dashboardStatus.innerHTML = `<i></i>Dashboard: ${esc(socketState)}`;

  const esp = telemetryLive() ? "LIVE" : "OFFLINE";
  espStatus.className = `pill ${tone(esp)}`;
  espStatus.innerHTML = `<i></i>ESP32: ${esc(esp)}`;
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

setInterval(render, 1000);
render();
connect();
