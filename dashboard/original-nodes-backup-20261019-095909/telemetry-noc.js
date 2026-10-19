"use strict";

(() => {
  if (typeof renderTelemetry !== "function") {
    console.error("[ABS NOC] renderTelemetry() is unavailable.");
    return;
  }

  const originalRenderTelemetry = renderTelemetry;

  const statusDot = (value) => {
    const cls = tone(value);
    return `<i class="noc-dot ${cls}"></i>`;
  };

  const nodeClass = (value) => `noc-node ${tone(value)}`;
  const linkClass = (value) => `noc-link ${tone(value)}`;

  renderTelemetry = function renderTelemetryNoc() {
    const t = state.telemetry ?? {};
    const ai = state.ai ?? {};
    const runtime = ai.runtime ?? {};
    const cmd = runtime.ai_command ?? {};
    const pico = runtime.pico_decision ?? {};
    const fault = ai.fault_domain ?? {};
    const trust = ai.trust ?? {};

    const telemetryState =
      typeof telemetryStatus === "function"
        ? telemetryStatus()
        : telemetryLive()
          ? "LIVE"
          : "OFFLINE";

    const source = String(t.network_input_source ?? "UNKNOWN").toUpperCase();
    const btsLinked =
      source === "BTS_BACKHAUL" &&
      telemetryState !== "OFFLINE";

    const activeCalls =
      Math.max(0, Number(t.network_active_calls ?? 0) || 0);

    const callActive = activeCalls > 0;
    const radioOkay = t.radio_operational === true;
    const physicalLink = t.physical_link_up === true;
    const upstreamOkay = t.upstream_reachable === true;
    const gridOkay = t.grid_available === true;

    const aiState =
      !state.ai
        ? "WAITING"
        : aiFresh()
          ? "LIVE"
          : "STALE";

    const picoReady = Boolean(
      cmd.pico_ai_fault_domain ||
      pico.mode_decision,
    );

    const picoState = picoReady ? "LIVE" : "WAITING";
    const agreement = cmd.dual_ai_agreement ?? "WAITING";
    const mode = t.operating_mode ?? "—";
    const guard = t.guardrail_status ?? "WAITING";
    const powerSource =
      t.active_power_source ??
      pico.power_source_decision ??
      "—";

    const generatorRunning = Boolean(
      t.generator_feedback_running ??
      t.generator_running,
    );

    const generatorAction =
      pico.generator_action ??
      (generatorRunning ? "RUNNING" : "STOPPED");

    const callQuality =
      !callActive
        ? "IDLE"
        : (
            Number(t.packet_loss_pct ?? 0) >= 5 ||
            Number(t.latency_ms ?? 0) >= 180 ||
            !radioOkay ||
            !upstreamOkay
          )
          ? "DEGRADED"
          : "GOOD";

    const phoneState = callActive ? "LIVE" : "WAITING";
    const btsState = btsLinked ? "LIVE" : "OFFLINE";

    return `
      <div class="noc-page">
        <header class="noc-page-header">
          <div>
            <span class="eyebrow">LIVE DIGITAL TWIN</span>
            <h1>Live Telemetry</h1>
            <p>Real-time telecommunications, AI, control and power topology.</p>
          </div>

          <div class="noc-header-status">
            ${pill(telemetryState, "Telemetry")}
            ${pill(aiState, "AI")}
            ${pill(agreement, "Dual AI")}
          </div>
        </header>

        <div class="noc-workspace">
          <section class="noc-canvas">
            <div class="noc-toolbar">
              <div>
                <strong>System topology</strong>
                <span>Live operational path</span>
              </div>

              <div class="noc-legend">
                <span>${statusDot("LIVE")} Live</span>
                <span>${statusDot("WAITING")} Idle / waiting</span>
                <span>${statusDot("OFFLINE")} Unavailable</span>
              </div>
            </div>

            <div class="noc-main-row">
              <div class="noc-phone-stack">
                <article class="${nodeClass(phoneState)}">
                  <div class="noc-icon material-symbols-rounded">smartphone</div>
                  <div>
                    <span>Phone A</span>
                    <strong>0712 000 001</strong>
                    <small>${callActive ? "CALL ACTIVE" : "CALL IDLE"}</small>
                  </div>
                </article>

                <article class="${nodeClass(phoneState)}">
                  <div class="noc-icon material-symbols-rounded">smartphone</div>
                  <div>
                    <span>Phone B</span>
                    <strong>0712 000 002</strong>
                    <small>${callActive ? "CALL ACTIVE" : "CALL IDLE"}</small>
                  </div>
                </article>
              </div>

              <div class="${linkClass(callActive ? "LIVE" : "WAITING")}">
                <span>VOICE / SMS</span>
                <small>${callActive ? `${activeCalls} active call${activeCalls === 1 ? "" : "s"}` : "idle"}</small>
                <b></b>
              </div>

              <article class="${nodeClass(btsState)} noc-equipment">
                <div class="noc-icon material-symbols-rounded">cell_tower</div>
                <div class="noc-equipment-copy">
                  <span>BTS-001</span>
                  <strong>${btsState}</strong>
                  <small>Service :8100</small>
                  <small>Hardware :4010</small>
                </div>
                <div class="noc-node-foot">
                  <span>${statusDot(radioOkay ? "LIVE" : "OFFLINE")} Radio ${radioOkay ? "operational" : "fault"}</span>
                  <span>${statusDot(upstreamOkay ? "LIVE" : "OFFLINE")} Backhaul ${upstreamOkay ? "up" : "down"}</span>
                </div>
              </article>

              <div class="${linkClass(btsLinked ? "LIVE" : "OFFLINE")}">
                <span>BTS BACKHAUL</span>
                <small>${num(t.latency_ms, 0)} ms · ${num(t.packet_loss_pct, 1)}% loss</small>
                <b></b>
              </div>

              <article class="${nodeClass(telemetryState)} noc-equipment">
                <div class="noc-icon material-symbols-rounded">developer_board</div>
                <div class="noc-equipment-copy">
                  <span>Main ESP32</span>
                  <strong>:4001</strong>
                  <small>${esc(source)}</small>
                  <small>Mode ${esc(mode)}</small>
                </div>
                <div class="noc-node-foot">
                  <span>${statusDot(physicalLink ? "LIVE" : "OFFLINE")} Physical link</span>
                  <span>${statusDot(String(guard).toUpperCase() === "PASSED" ? "LIVE" : "WAITING")} Guard ${esc(guard)}</span>
                </div>
              </article>

              <div class="${linkClass(aiState)}">
                <span>TELEMETRY</span>
                <small>${telemetryState} · ${ageLabel(ageOf(state.meta?.telemetryUpdatedMs))}</small>
                <b></b>
              </div>

              <article class="${nodeClass(aiState)} noc-equipment">
                <div class="noc-icon material-symbols-rounded">neurology</div>
                <div class="noc-equipment-copy">
                  <span>Laptop AI</span>
                  <strong>${esc(fault.label ?? "WAITING")}</strong>
                  <small>${fault.confidence == null ? "No live inference" : `${prob(fault.confidence, 1)} confidence`}</small>
                  <small>${esc(trust.decision ?? "WAITING")}</small>
                </div>
                <div class="noc-node-foot">
                  <span>${statusDot(aiState)} Temporal AI</span>
                  <span>${statusDot(ai?.anomaly?.flagged ? "OFFLINE" : "LIVE")} Anomaly ${ai?.anomaly?.flagged ? "YES" : "NO"}</span>
                </div>
              </article>

              <div class="${linkClass(picoReady ? agreement : "WAITING")}">
                <span>DUAL AI</span>
                <small>${esc(agreement)}</small>
                <b></b>
              </div>

              <article class="${nodeClass(picoState)} noc-equipment">
                <div class="noc-icon material-symbols-rounded">memory</div>
                <div class="noc-equipment-copy">
                  <span>Raspberry Pi Pico</span>
                  <strong>:4000</strong>
                  <small>${esc(cmd.pico_ai_fault_domain ?? "WAITING")}</small>
                  <small>${esc(agreement)}</small>
                </div>
                <div class="noc-node-foot">
                  <span>${statusDot(picoState)} Embedded AI</span>
                  <span>${statusDot(pico.mode_decision ? "LIVE" : "WAITING")} Decision ${esc(pico.mode_decision ?? "WAITING")}</span>
                </div>
              </article>
            </div>

            <section class="noc-control-loop">
              <div class="noc-loop-label">
                <span>CONTROL LOOP</span>
                <small>Pico decision → ESP32 deterministic guardrails</small>
              </div>

              <div class="noc-control-decision">
                <span>Pico decision</span>
                <strong>${esc(pico.mode_decision ?? "WAITING")}</strong>
                <small>${esc(pico.power_source_decision ?? "—")} · ${esc(generatorAction)}</small>
              </div>

              <div class="${linkClass(pico.mode_decision ? "LIVE" : "WAITING")} noc-control-arrow">
                <b></b>
              </div>

              <div class="noc-control-decision">
                <span>ESP32 guardrails</span>
                <strong>${esc(mode)}</strong>
                <small>${esc(guard)}</small>
              </div>
            </section>

            <section class="noc-power-layer">
              <div class="noc-power-title">
                <span>POWER + SITE LAYER</span>
                <small>Supply and load state feeding the final control authority</small>
              </div>

              <div class="noc-power-grid">
                <article class="${nodeClass(gridOkay ? "LIVE" : "OFFLINE")} noc-power-card">
                  <div class="noc-icon material-symbols-rounded">electrical_services</div>
                  <div>
                    <span>Grid Power</span>
                    <strong>${gridOkay ? "AVAILABLE" : "FAILED"}</strong>
                    <small>${esc(powerSource)}</small>
                  </div>
                </article>

                <article class="${nodeClass(generatorRunning ? "LIVE" : "WAITING")} noc-power-card">
                  <div class="noc-icon material-symbols-rounded">offline_bolt</div>
                  <div>
                    <span>Generator</span>
                    <strong>${generatorRunning ? "RUNNING" : "STOPPED"}</strong>
                    <small>${esc(t.generator_verification_state ?? generatorAction)}</small>
                  </div>
                </article>

                <article class="${nodeClass(Number(t.battery_soc_pct ?? 0) > 25 ? "LIVE" : "WAITING")} noc-power-card">
                  <div class="noc-icon material-symbols-rounded">battery_charging_full</div>
                  <div>
                    <span>Battery</span>
                    <strong>${num(t.battery_soc_pct, 1)}%</strong>
                    <small>${num(t.battery_voltage_v, 2)} V</small>
                  </div>
                </article>

                <article class="noc-node neutral noc-power-card">
                  <div class="noc-icon material-symbols-rounded">dns</div>
                  <div>
                    <span>Site Load</span>
                    <strong>${num(t.traffic_load_pct, 1)}%</strong>
                    <small>${num(t.managed_power_kw, 3)} kW · ${esc(mode)}</small>
                  </div>
                </article>
              </div>
            </section>
          </section>

          <aside class="noc-side-rail">
            <section class="noc-side-panel">
              <div class="noc-side-head">
                <strong>System Status</strong>
                <span>LIVE</span>
              </div>

              <div class="noc-status-list">
                <div><span>${statusDot(phoneState)} Phones</span><strong>${callActive ? `${activeCalls} active call${activeCalls === 1 ? "" : "s"}` : "Idle"}</strong></div>
                <div><span>${statusDot(btsState)} BTS-001</span><strong>${btsState} · :8100 / :4010</strong></div>
                <div><span>${statusDot(telemetryState)} Main ESP32</span><strong>${telemetryState} · :4001</strong></div>
                <div><span>${statusDot(aiState)} Laptop AI</span><strong>${aiState}</strong></div>
                <div><span>${statusDot(picoState)} Raspberry Pi Pico</span><strong>${picoState} · :4000</strong></div>
              </div>
            </section>

            <section class="noc-side-panel">
              <div class="noc-side-head">
                <strong>Link Metrics</strong>
                <span>${esc(source)}</span>
              </div>

              <div class="noc-status-list">
                <div><span>Phone ↔ BTS</span><strong>${callActive ? callQuality : "Idle"}</strong></div>
                <div><span>BTS → ESP32</span><strong>${num(t.latency_ms, 1)} ms · ${num(t.packet_loss_pct, 2)}%</strong></div>
                <div><span>RSSI</span><strong>${num(t.rssi_dbm, 1)} dBm</strong></div>
                <div><span>Traffic</span><strong>${num(t.traffic_load_pct, 1)}%</strong></div>
                <div><span>AI ↔ Pico</span><strong>${esc(agreement)}</strong></div>
              </div>
            </section>

            <section class="noc-side-panel">
              <div class="noc-side-head">
                <strong>Current Call</strong>
                <span>${callActive ? "ACTIVE" : "IDLE"}</span>
              </div>

              ${
                callActive
                  ? `
                    <div class="noc-call-card">
                      <div class="noc-call-route">
                        <strong>0712 000 001</strong>
                        <span class="material-symbols-rounded">arrow_forward</span>
                        <strong>0712 000 002</strong>
                      </div>
                      <div class="noc-call-meta">
                        <span>${statusDot(callQuality)} ${callQuality}</span>
                        <span>${num(t.latency_ms, 0)} ms</span>
                        <span>${num(t.packet_loss_pct, 1)}% loss</span>
                      </div>
                    </div>
                  `
                  : `
                    <div class="noc-empty-state">
                      <span class="material-symbols-rounded">phone_disabled</span>
                      <strong>No active call</strong>
                      <small>BTS-001 is ready for the next call session.</small>
                    </div>
                  `
              }
            </section>

            <section class="noc-side-panel">
              <div class="noc-side-head">
                <strong>AI Decision</strong>
                <span>${esc(trust.decision ?? "WAITING")}</span>
              </div>

              <div class="noc-decision-grid">
                <div><span>Fault domain</span><strong>${esc(fault.label ?? "—")}</strong></div>
                <div><span>Final mode</span><strong>${esc(mode)}</strong></div>
                <div><span>Power source</span><strong>${esc(powerSource)}</strong></div>
                <div><span>Generator</span><strong>${esc(generatorAction)}</strong></div>
                <div><span>Guardrails</span><strong>${esc(guard)}</strong></div>
              </div>
            </section>
          </aside>
        </div>
      </div>
    `;
  };

  function syncTelemetryMode() {
    const active =
      document.querySelector(".workspace-tabs .tab.active")
        ?.dataset?.tab === "telemetry";

    document.body.classList.toggle("telemetry-noc", active);
  }

  for (const tab of document.querySelectorAll(".workspace-tabs .tab")) {
    tab.addEventListener("click", () => {
      window.setTimeout(syncTelemetryMode, 0);
    });
  }

  syncTelemetryMode();

  if (typeof render === "function") {
    render();
  }
})();


/* ==========================================================================
   ABS TELEMETRY — NODE CONFIG INSPECTOR
   ========================================================================== */

(() => {
  "use strict";

  const CONFIGS = {
    "Phone A": {
      type: "Mobile subscriber",
      endpoint: "0712 000 001",
      transport: "BTS WebSocket",
      role: "Subscriber A",
      icon: "smartphone",
      links: ["BTS-001"],
    },
    "Phone B": {
      type: "Mobile subscriber",
      endpoint: "0712 000 002",
      transport: "BTS WebSocket",
      role: "Subscriber B",
      icon: "smartphone",
      links: ["BTS-001"],
    },
    "BTS-001": {
      type: "Telecommunications BTS",
      endpoint: "Service :8100 / Hardware :4010",
      transport: "WebSocket + RFC2217",
      role: "Radio access + backhaul source",
      icon: "cell_tower",
      links: ["Phone A", "Phone B", "Main ESP32"],
    },
    "Main ESP32": {
      type: "Main site controller",
      endpoint: "RFC2217 :4001",
      transport: "Serial / JSON",
      role: "Sensors + deterministic guardrails",
      icon: "developer_board",
      links: ["BTS-001", "Laptop AI", "ESP32 guardrails"],
    },
    "Laptop AI": {
      type: "Temporal AI inference",
      endpoint: "Local laptop process",
      transport: "Bridge pipeline",
      role: "Fault diagnosis + recommendation",
      icon: "neurology",
      links: ["Main ESP32", "Raspberry Pi Pico"],
    },
    "Raspberry Pi Pico": {
      type: "Embedded AI controller",
      endpoint: "RFC2217 :4000",
      transport: "Serial / JSON",
      role: "Independent validation + decision",
      icon: "memory",
      links: ["Laptop AI", "Pico decision"],
    },
    "Pico decision": {
      type: "Decision output",
      endpoint: "Pico control policy",
      transport: "Control message",
      role: "Mode + source + generator decision",
      icon: "account_tree",
      links: ["Raspberry Pi Pico", "ESP32 guardrails"],
    },
    "ESP32 guardrails": {
      type: "Final actuation authority",
      endpoint: "Main ESP32",
      transport: "Deterministic control",
      role: "Physical safety + final mode",
      icon: "shield",
      links: ["Pico decision", "Grid Power", "Generator", "Battery", "Site Load"],
    },
    "Grid Power": {
      type: "Power source",
      endpoint: "Grid input",
      transport: "Site power",
      role: "Primary supply",
      icon: "electrical_services",
      links: ["ESP32 guardrails", "Site Load"],
    },
    "Generator": {
      type: "Backup power source",
      endpoint: "Generator plant",
      transport: "Site power",
      role: "Backup generation",
      icon: "offline_bolt",
      links: ["ESP32 guardrails", "Site Load"],
    },
    "Battery": {
      type: "DC energy storage",
      endpoint: "Battery bank",
      transport: "DC bus",
      role: "Backup / buffer energy",
      icon: "battery_charging_full",
      links: ["ESP32 guardrails", "Site Load"],
    },
    "Site Load": {
      type: "Managed site demand",
      endpoint: "Base-station load",
      transport: "Power + traffic",
      role: "Final managed load",
      icon: "dns",
      links: ["Grid Power", "Generator", "Battery", "ESP32 guardrails"],
    },
  };

  const state = {
    selectedTitle: null,
    tab: "properties",
    focus: false,
    inspector: null,
    canvas: null,
  };

  function getDashboardState() {
    try {
      return window.ABSDashboardGetState?.() ?? {};
    } catch {
      return {};
    }
  }

  function nodeElements() {
    return [
      ...document.querySelectorAll(
        ".noc-node, .noc-control-decision, .noc-power-card",
      ),
    ];
  }

  function titleOf(node) {
    if (!node) return "";

    const first =
      node.querySelector(
        ".noc-equipment-copy > span:first-child, " +
        ".noc-node > div:not(.noc-icon) > span:first-child, " +
        ".noc-control-decision > span:first-child",
      );

    if (first?.textContent?.trim()) {
      return first.textContent.trim();
    }

    const text = node.textContent || "";

    for (const title of Object.keys(CONFIGS)) {
      if (text.includes(title)) return title;
    }

    return "";
  }

  function statusOf(node) {
    if (!node) return "WAITING";
    if (node.classList.contains("good")) return "LIVE";
    if (node.classList.contains("bad")) return "OFFLINE";
    if (node.classList.contains("warn")) return "WAITING";
    return "READY";
  }

  function toneOf(status) {
    const s = String(status || "").toUpperCase();
    if (
      ["LIVE", "AVAILABLE", "RUNNING", "PASSED", "AGREE", "ACCEPT", "GOOD", "READY"].some(
        (x) => s.includes(x),
      )
    ) return "good";

    if (
      ["OFFLINE", "FAULT", "FAILED", "REJECT", "DOWN", "BAD"].some(
        (x) => s.includes(x),
      )
    ) return "bad";

    return "warn";
  }

  function esc(value) {
    return String(value ?? "—")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;");
  }

  function path(obj, keys, fallback = "—") {
    let current = obj;

    for (const key of keys) {
      if (
        current == null ||
        !Object.prototype.hasOwnProperty.call(current, key)
      ) {
        return fallback;
      }
      current = current[key];
    }

    return current ?? fallback;
  }

  function liveRows(title, dashboard) {
    const t = dashboard.telemetry ?? {};
    const ai = dashboard.ai ?? {};
    const runtime = ai.runtime ?? {};
    const cmd = runtime.ai_command ?? {};
    const pico = runtime.pico_decision ?? {};

    const rows = {
      "Phone A": [
        ["Subscriber", "0712 000 001"],
        ["Active calls", t.network_active_calls ?? 0],
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
        ["Latency", `${t.latency_ms ?? "—"} ms`],
      ],
      "Phone B": [
        ["Subscriber", "0712 000 002"],
        ["Active calls", t.network_active_calls ?? 0],
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
        ["Packet loss", `${t.packet_loss_pct ?? "—"} %`],
      ],
      "BTS-001": [
        ["Input source", t.network_input_source ?? "—"],
        ["Latency", `${t.latency_ms ?? "—"} ms`],
        ["Packet loss", `${t.packet_loss_pct ?? "—"} %`],
        ["RSSI", `${t.rssi_dbm ?? "—"} dBm`],
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
        ["Radio", t.radio_operational === true ? "OPERATIONAL" : "FAULT / UNKNOWN"],
        ["Upstream", t.upstream_reachable === true ? "REACHABLE" : "DOWN / UNKNOWN"],
      ],
      "Main ESP32": [
        ["Telemetry", dashboard.meta?.telemetryStatus ?? "—"],
        ["Input source", t.network_input_source ?? "—"],
        ["Operating mode", t.operating_mode ?? "—"],
        ["Guardrails", t.guardrail_status ?? "—"],
        ["Physical link", t.physical_link_up === true ? "UP" : "DOWN / UNKNOWN"],
      ],
      "Laptop AI": [
        ["Fault domain", ai.fault_domain?.label ?? "—"],
        ["Confidence", ai.fault_domain?.confidence == null ? "—" : `${(Number(ai.fault_domain.confidence) * 100).toFixed(1)} %`],
        ["Trust", ai.trust?.decision ?? "—"],
        ["Anomaly", ai.anomaly?.flagged === true ? "YES" : "NO"],
        ["Energy recommendation", ai.energy_recommendation?.mode ?? ai.energy?.recommendation ?? "—"],
      ],
      "Raspberry Pi Pico": [
        ["Embedded domain", cmd.pico_ai_fault_domain ?? "—"],
        ["Dual AI", cmd.dual_ai_agreement ?? "—"],
        ["Decision", pico.mode_decision ?? "—"],
        ["Power source", pico.power_source_decision ?? "—"],
        ["Generator", pico.generator_action ?? "—"],
      ],
      "Pico decision": [
        ["Mode", pico.mode_decision ?? "—"],
        ["Power source", pico.power_source_decision ?? "—"],
        ["Generator", pico.generator_action ?? "—"],
        ["Reason", pico.reason ?? "—"],
      ],
      "ESP32 guardrails": [
        ["Final mode", t.operating_mode ?? "—"],
        ["Guardrail", t.guardrail_status ?? "—"],
        ["Power source", t.active_power_source ?? "—"],
        ["Generator", t.generator_feedback_running === true ? "RUNNING" : "STOPPED"],
      ],
      "Grid Power": [
        ["Available", t.grid_available === true ? "YES" : "NO"],
        ["Active source", t.active_power_source ?? "—"],
        ["DC voltage", `${t.dc_voltage_v ?? "—"} V`],
        ["DC power", `${t.dc_power_w ?? "—"} W`],
      ],
      "Generator": [
        ["Running", (t.generator_feedback_running ?? t.generator_running) === true ? "YES" : "NO"],
        ["Verification", t.generator_verification_state ?? "—"],
        ["Action", pico.generator_action ?? "—"],
      ],
      "Battery": [
        ["State of charge", `${t.battery_soc_pct ?? "—"} %`],
        ["Voltage", `${t.battery_voltage_v ?? "—"} V`],
        ["Trend", `${t.battery_soc_trend_pct_per_min ?? "—"} %/min`],
      ],
      "Site Load": [
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
        ["Managed power", `${t.managed_power_kw ?? "—"} kW`],
        ["Operating mode", t.operating_mode ?? "—"],
        ["Active calls", t.network_active_calls ?? 0],
      ],
    };

    return rows[title] ?? [];
  }

  function propRow(label, value, readonly = false) {
    return `
      <div class="abs-prop-row">
        <span>${esc(label)}</span>
        ${
          readonly
            ? `<input class="abs-prop-input" readonly value="${esc(value)}" />`
            : `<strong class="abs-prop-value">${esc(value)}</strong>`
        }
      </div>
    `;
  }

  function section(title, icon, body) {
    return `
      <section class="abs-prop-section">
        <header>
          <span class="material-symbols-rounded">${esc(icon)}</span>
          ${esc(title)}
        </header>
        ${body}
      </section>
    `;
  }

  function renderProperties(node, title) {
    const config = CONFIGS[title] ?? {
      type: "System node",
      endpoint: "—",
      transport: "—",
      role: "—",
      links: [],
    };

    return [
      section(
        "Node configuration",
        "tune",
        `<div class="abs-prop-grid">
          ${propRow("Type", config.type, true)}
          ${propRow("Endpoint", config.endpoint, true)}
          ${propRow("Transport", config.transport, true)}
          ${propRow("Role", config.role, true)}
        </div>`,
      ),
      section(
        "Rendered state",
        "data_object",
        `<div class="abs-prop-grid">
          ${propRow("Status", statusOf(node))}
          ${propRow("Node title", title)}
          ${propRow("UI class", node?.className ?? "—", true)}
        </div>`,
      ),
    ].join("");
  }

  function renderLive(title) {
    const dashboard = getDashboardState();
    const rows = liveRows(title, dashboard);

    return section(
      "Live properties",
      "monitoring",
      `<div class="abs-prop-grid">
        ${
          rows.length
            ? rows.map(([k, v]) => propRow(k, v)).join("")
            : propRow("State", "No mapped live properties")
        }
      </div>`,
    );
  }

  function renderLinks(title) {
    const config = CONFIGS[title] ?? { links: [] };

    return section(
      "Connections",
      "account_tree",
      `<div class="abs-link-list">
        ${
          config.links?.length
            ? config.links
                .map(
                  (name) => `
                    <div class="abs-link-item">
                      <i class="abs-link-socket"></i>
                      <span>${esc(name)}</span>
                      <strong>CONNECTED PATH</strong>
                    </div>
                  `,
                )
                .join("")
            : `
              <div class="abs-link-item">
                <i class="abs-link-socket"></i>
                <span>No mapped links</span>
                <strong>—</strong>
              </div>
            `
        }
      </div>`,
    );
  }

  function selectedNode() {
    if (!state.selectedTitle) return null;

    return nodeElements().find(
      (node) => titleOf(node) === state.selectedTitle,
    ) ?? null;
  }

  function renderInspector() {
    if (!state.inspector?.isConnected) return;

    const node = selectedNode();
    const title = node ? titleOf(node) : state.selectedTitle;

    const config =
      CONFIGS[title] ?? {
        type: "System node",
        endpoint: "—",
        transport: "—",
        role: "—",
        icon: "device_hub",
        links: [],
      };

    const status = node ? statusOf(node) : "WAITING";
    const tone = toneOf(status);

    state.inspector.innerHTML = `
      <div class="abs-inspector-titlebar">
        <strong>Node Properties</strong>
        <span>Blender-style inspector</span>
      </div>

      ${
        title
          ? `
            <div class="abs-inspector-node-head">
              <div class="abs-inspector-node-icon material-symbols-rounded">
                ${esc(config.icon ?? "device_hub")}
              </div>

              <div class="abs-inspector-node-copy">
                <span>${esc(config.type)}</span>
                <strong>${esc(title)}</strong>
                <small>${esc(config.endpoint)}</small>
              </div>

              <span class="abs-inspector-status ${tone}">
                <i></i>${esc(status)}
              </span>
            </div>

            <div class="abs-inspector-tabs">
              <button type="button" class="abs-inspector-tab ${state.tab === "properties" ? "active" : ""}" data-abs-tab="properties">Properties</button>
              <button type="button" class="abs-inspector-tab ${state.tab === "live" ? "active" : ""}" data-abs-tab="live">Live</button>
              <button type="button" class="abs-inspector-tab ${state.tab === "links" ? "active" : ""}" data-abs-tab="links">Links</button>
            </div>

            <div class="abs-inspector-body">
              ${
                state.tab === "properties"
                  ? renderProperties(node, title)
                  : state.tab === "live"
                    ? renderLive(title)
                    : renderLinks(title)
              }
            </div>

            <div class="abs-inspector-actions">
              <button type="button" data-abs-action="focus">Focus</button>
              <button type="button" data-abs-action="refresh">Refresh</button>
              <button type="button" data-abs-action="reset">Reset View</button>
            </div>
          `
          : `
            <div></div>
            <div></div>
            <div class="abs-inspector-body">
              <div class="abs-inspector-empty">
                <span class="material-symbols-rounded">touch_app</span>
                <strong>Select a node</strong>
                <small>Click any node on the left to inspect its configuration, current live values and mapped connections.</small>
              </div>
            </div>
            <div class="abs-inspector-actions">
              <button type="button" disabled>Focus</button>
              <button type="button" data-abs-action="refresh">Refresh</button>
              <button type="button" data-abs-action="reset">Reset View</button>
            </div>
          `
      }
    `;
  }

  function applySelection() {
    for (const node of nodeElements()) {
      node.classList.toggle(
        "abs-node-selected",
        titleOf(node) === state.selectedTitle,
      );
    }
  }

  function ensureInspector() {
    const page = document.querySelector(".noc-page");
    const workspace = page?.querySelector(".noc-workspace");
    const canvas = page?.querySelector(".noc-canvas");

    if (!page || !workspace || !canvas) return;

    state.canvas = canvas;

    let inspector = workspace.querySelector(".abs-node-inspector");

    if (!inspector) {
      inspector = document.createElement("aside");
      inspector.className = "abs-node-inspector";
      workspace.append(inspector);
    }

    state.inspector = inspector;

    if (!state.selectedTitle) {
      const preferred = nodeElements().find(
        (node) => titleOf(node) === "BTS-001",
      );

      if (preferred) {
        state.selectedTitle = "BTS-001";
      }
    }

    applySelection();
    renderInspector();
  }

  document.addEventListener("click", (event) => {
    const node = event.target.closest(
      ".noc-node, .noc-control-decision, .noc-power-card",
    );

    if (
      node &&
      node.closest(".noc-page")
    ) {
      const title = titleOf(node);

      if (title) {
        state.selectedTitle = title;
        applySelection();
        renderInspector();
      }
      return;
    }

    const tab = event.target.closest("[data-abs-tab]");
    if (tab) {
      state.tab = tab.dataset.absTab;
      renderInspector();
      return;
    }

    const action = event.target.closest("[data-abs-action]");
    if (!action) return;

    if (action.dataset.absAction === "focus") {
      const current = selectedNode();

      state.focus = !state.focus;
      state.canvas?.classList.toggle("abs-focus-mode", state.focus);

      current?.scrollIntoView({
        behavior: "smooth",
        block: "center",
        inline: "center",
      });
    }

    if (action.dataset.absAction === "refresh") {
      renderInspector();
    }

    if (action.dataset.absAction === "reset") {
      state.focus = false;
      state.canvas?.classList.remove("abs-focus-mode");
      state.canvas?.scrollTo({
        left: 0,
        top: 0,
        behavior: "smooth",
      });
    }
  });

  const observer = new MutationObserver(() => {
    window.requestAnimationFrame(ensureInspector);
  });

  observer.observe(document.body, {
    childList: true,
    subtree: true,
  });

  window.addEventListener(
    "abs-dashboard-rendered",
    () => window.requestAnimationFrame(ensureInspector),
  );

  window.setInterval(() => {
    if (
      state.inspector?.isConnected &&
      state.tab === "live"
    ) {
      renderInspector();
    }
  }, 2000);

  window.requestAnimationFrame(ensureInspector);
})();


/* ==========================================================================
   ABS TELEMETRY — BLENDER NOODLE OVERLAY V4
   ========================================================================== */

(() => {
  "use strict";

  const EDGES = [
    ["Phone A", "BTS-001", "idle"],
    ["Phone B", "BTS-001", "idle"],
    ["BTS-001", "Main ESP32", "data"],
    ["Main ESP32", "Laptop AI", "data"],
    ["Laptop AI", "Raspberry Pi Pico", "data"],
    ["Raspberry Pi Pico", "Pico decision", "control"],
    ["Pico decision", "ESP32 guardrails", "control"],
    ["Grid Power", "ESP32 guardrails", "control"],
    ["Generator", "ESP32 guardrails", "control"],
    ["Battery", "ESP32 guardrails", "control"],
    ["ESP32 guardrails", "Site Load", "control"],
  ];

  let raf = 0;

  function titleOf(node) {
    if (!node) return "";

    const selectors = [
      ".noc-equipment-copy > span:first-child",
      ".noc-phone-stack .noc-node > div:not(.noc-icon) > span:first-child",
      ".noc-power-card > div:not(.noc-icon) > span:first-child",
      ".noc-control-decision > span:first-child",
    ];

    for (const selector of selectors) {
      const el = node.matches?.(selector)
        ? node
        : node.querySelector?.(selector);

      if (el?.textContent?.trim()) {
        return el.textContent.trim();
      }
    }

    const text = node.textContent || "";

    const known = [
      "Phone A",
      "Phone B",
      "BTS-001",
      "Main ESP32",
      "Laptop AI",
      "Raspberry Pi Pico",
      "Pico decision",
      "ESP32 guardrails",
      "Grid Power",
      "Generator",
      "Battery",
      "Site Load",
    ];

    return known.find((name) => text.includes(name)) || "";
  }

  function nodesByTitle(canvas) {
    const map = new Map();

    for (
      const node of canvas.querySelectorAll(
        ".noc-node, .noc-control-decision, .noc-power-card",
      )
    ) {
      const title = titleOf(node);
      if (title) map.set(title, node);
    }

    return map;
  }

  function nodeStatus(node) {
    if (!node) return "bad";
    if (node.classList.contains("bad")) return "bad";
    if (node.classList.contains("warn")) return "idle";
    return "good";
  }

  function point(node, side, canvasRect, canvas) {
    const rect = node.getBoundingClientRect();

    return {
      x:
        rect.left -
        canvasRect.left +
        canvas.scrollLeft +
        (side === "out" ? rect.width : 0),
      y:
        rect.top -
        canvasRect.top +
        canvas.scrollTop +
        rect.height / 2,
    };
  }

  function pathD(a, b) {
    const dx = Math.max(34, Math.abs(b.x - a.x) * 0.48);

    return [
      `M ${a.x.toFixed(1)} ${a.y.toFixed(1)}`,
      `C ${(a.x + dx).toFixed(1)} ${a.y.toFixed(1)},`,
      `${(b.x - dx).toFixed(1)} ${b.y.toFixed(1)},`,
      `${b.x.toFixed(1)} ${b.y.toFixed(1)}`,
    ].join(" ");
  }

  function ensureSvg(canvas) {
    let svg = canvas.querySelector(":scope > .abs-blender-noodles");

    if (!svg) {
      svg = document.createElementNS(
        "http://www.w3.org/2000/svg",
        "svg",
      );

      svg.classList.add("abs-blender-noodles");
      canvas.prepend(svg);
    }

    return svg;
  }

  function draw() {
    raf = 0;

    const canvas = document.querySelector(".noc-page .noc-canvas");
    if (!canvas) return;

    const svg = ensureSvg(canvas);
    const map = nodesByTitle(canvas);
    const canvasRect = canvas.getBoundingClientRect();

    const width = Math.max(canvas.clientWidth, canvas.scrollWidth);
    const height = Math.max(canvas.clientHeight, canvas.scrollHeight);

    svg.setAttribute("width", String(width));
    svg.setAttribute("height", String(height));
    svg.setAttribute("viewBox", `0 0 ${width} ${height}`);

    const parts = [];

    for (const [from, to, kind] of EDGES) {
      const aNode = map.get(from);
      const bNode = map.get(to);

      if (!aNode || !bNode) continue;

      const a = point(aNode, "out", canvasRect, canvas);
      const b = point(bNode, "in", canvasRect, canvas);

      const bad =
        nodeStatus(aNode) === "bad" ||
        nodeStatus(bNode) === "bad";

      const idle =
        kind === "idle" ||
        nodeStatus(aNode) === "idle" ||
        nodeStatus(bNode) === "idle";

      const cls = bad
        ? "abs-noodle-bad"
        : kind === "control"
          ? "abs-noodle-control"
          : idle
            ? "abs-noodle-idle"
            : "";

      parts.push(
        `<path class="${cls}" d="${pathD(a, b)}"></path>`,
      );
    }

    svg.innerHTML = parts.join("");
  }

  function schedule() {
    if (raf) return;
    raf = window.requestAnimationFrame(draw);
  }

  const observer = new MutationObserver(schedule);

  observer.observe(document.body, {
    childList: true,
    subtree: true,
    attributes: true,
    attributeFilter: ["class"],
  });

  window.addEventListener("resize", schedule);
  window.addEventListener("abs-dashboard-rendered", schedule);

  document.addEventListener(
    "scroll",
    (event) => {
      if (event.target?.closest?.(".noc-canvas")) {
        schedule();
      }
    },
    true,
  );

  window.setInterval(schedule, 1500);
  schedule();
})();


/* ==========================================================================
   ABS TELEMETRY — TRUE BLENDER NODE EDITOR V5
   ========================================================================== */

(() => {
  "use strict";

  const WORLD_W = 1000;
  const WORLD_H = 650;
  const STORAGE_KEY = "abs.telemetry.blender.nodes.v5";

  const DEFAULT_POS = {
    phoneA: [26, 115],
    phoneB: [26, 225],
    bts: [215, 165],
    esp32: [415, 165],
    ai: [610, 105],
    pico: [610, 265],
    guard: [815, 265],
    grid: [205, 455],
    generator: [375, 455],
    battery: [545, 455],
    load: [760, 455],
  };

  const DEFINITIONS = {
    phoneA: {
      title: "Phone A",
      icon: "smartphone",
      type: "Mobile subscriber",
      endpoint: "0712 000 001",
      role: "Subscriber A",
      transport: "BTS WebSocket",
      cls: "phone compact",
    },
    phoneB: {
      title: "Phone B",
      icon: "smartphone",
      type: "Mobile subscriber",
      endpoint: "0712 000 002",
      role: "Subscriber B",
      transport: "BTS WebSocket",
      cls: "phone compact",
    },
    bts: {
      title: "BTS-001",
      icon: "cell_tower",
      type: "Telecommunications BTS",
      endpoint: "Service :8100 · Hardware :4010",
      role: "Radio access + backhaul",
      transport: "WebSocket + RFC2217",
    },
    esp32: {
      title: "Main ESP32",
      icon: "developer_board",
      type: "Main site controller",
      endpoint: "RFC2217 :4001",
      role: "Sensors + final guardrails",
      transport: "Serial JSON",
    },
    ai: {
      title: "Laptop AI",
      icon: "neurology",
      type: "Temporal AI inference",
      endpoint: "Laptop process",
      role: "Diagnosis + recommendation",
      transport: "Local bridge",
    },
    pico: {
      title: "Raspberry Pi Pico",
      icon: "memory",
      type: "Embedded AI controller",
      endpoint: "RFC2217 :4000",
      role: "Independent validation",
      transport: "Serial JSON",
    },
    guard: {
      title: "ESP32 Guardrails",
      icon: "shield",
      type: "Final control authority",
      endpoint: "Main ESP32",
      role: "Deterministic safety layer",
      transport: "Local control",
    },
    grid: {
      title: "Grid Power",
      icon: "electrical_services",
      type: "Power source",
      endpoint: "Grid input",
      role: "Primary site supply",
      transport: "Site power",
      cls: "compact",
    },
    generator: {
      title: "Generator",
      icon: "offline_bolt",
      type: "Backup power",
      endpoint: "Generator plant",
      role: "Backup generation",
      transport: "Site power",
      cls: "compact",
    },
    battery: {
      title: "Battery",
      icon: "battery_charging_full",
      type: "Energy storage",
      endpoint: "Battery bank",
      role: "DC backup / buffer",
      transport: "DC bus",
      cls: "compact",
    },
    load: {
      title: "Site Load",
      icon: "dns",
      type: "Managed demand",
      endpoint: "Base-station load",
      role: "Managed telecom load",
      transport: "Power + traffic",
      cls: "compact",
    },
  };

  const EDGES = [
    ["phoneA", "bts", "idle"],
    ["phoneB", "bts", "idle"],
    ["bts", "esp32", "data"],
    ["esp32", "ai", "data"],
    ["ai", "pico", "data"],
    ["pico", "guard", "control"],
    ["grid", "guard", "power"],
    ["generator", "guard", "power"],
    ["battery", "guard", "power"],
    ["guard", "load", "power"],
  ];

  const ui = {
    root: null,
    viewport: null,
    world: null,
    wires: null,
    inspector: null,
    zoomLabel: null,
    selected: "bts",
    tab: "properties",
    scale: 0.78,
    tx: 14,
    ty: 12,
    positions: structuredClone(DEFAULT_POS),
    drag: null,
    pan: null,
  };

  function dashboardState() {
    try {
      return window.ABSDashboardGetState?.() ?? {};
    } catch {
      return {};
    }
  }

  function loadPositions() {
    try {
      const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || "null");
      if (!saved || typeof saved !== "object") return;
      for (const key of Object.keys(DEFAULT_POS)) {
        if (
          Array.isArray(saved[key]) &&
          saved[key].length === 2 &&
          saved[key].every(Number.isFinite)
        ) {
          ui.positions[key] = saved[key];
        }
      }
    } catch {}
  }

  function savePositions() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(ui.positions));
    } catch {}
  }

  function esc(value) {
    return String(value ?? "—")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;");
  }

  function data() {
    const s = dashboardState();
    const t = s.telemetry ?? {};
    const ai = s.ai ?? {};
    const runtime = ai.runtime ?? {};
    const cmd = runtime.ai_command ?? {};
    const pico = runtime.pico_decision ?? {};

    return { s, t, ai, runtime, cmd, pico };
  }

  function statusFor(id) {
    const { s, t, ai, cmd, pico } = data();
    const telem = String(s.meta?.telemetryStatus ?? "OFFLINE").toUpperCase();

    if (id === "phoneA" || id === "phoneB") {
      return Number(t.network_active_calls ?? 0) > 0 ? "good" : "warn";
    }
    if (id === "bts") {
      return String(t.network_input_source ?? "").toUpperCase() === "BTS_BACKHAUL"
        ? "good"
        : "bad";
    }
    if (id === "esp32") return telem === "LIVE" ? "good" : telem === "STALE" ? "warn" : "bad";
    if (id === "ai") return s.meta?.aiFresh === true ? "good" : ai ? "warn" : "bad";
    if (id === "pico") return cmd.pico_ai_fault_domain || pico.mode_decision ? "good" : "warn";
    if (id === "guard") return String(t.guardrail_status ?? "").toUpperCase() === "PASSED" ? "good" : "warn";
    if (id === "grid") return t.grid_available === true ? "good" : "bad";
    if (id === "generator") return (t.generator_feedback_running ?? t.generator_running) === true ? "good" : "warn";
    if (id === "battery") return Number(t.battery_soc_pct ?? 0) > 25 ? "good" : "warn";
    return "good";
  }

  function nodeDisplay(id) {
    const { s, t, ai, cmd, pico } = data();
    const def = DEFINITIONS[id];

    const common = {
      title: def.title,
      icon: def.icon,
      status: statusFor(id),
      statusText: "READY",
      value: "—",
      meta: def.endpoint,
    };

    if (id === "phoneA" || id === "phoneB") {
      const calls = Number(t.network_active_calls ?? 0);
      return {
        ...common,
        value: id === "phoneA" ? "0712 000 001" : "0712 000 002",
        meta: calls > 0 ? `${calls} active call${calls === 1 ? "" : "s"}` : "Call idle",
        statusText: calls > 0 ? "ACTIVE" : "IDLE",
      };
    }

    if (id === "bts") {
      const linked = String(t.network_input_source ?? "").toUpperCase() === "BTS_BACKHAUL";
      return {
        ...common,
        value: linked ? "ONLINE" : "OFFLINE",
        meta: `:8100 / :4010 · ${Number(t.latency_ms ?? 0).toFixed(0)} ms`,
        statusText: t.radio_operational === true ? "RADIO OK" : "RADIO FAULT",
      };
    }

    if (id === "esp32") {
      return {
        ...common,
        value: ":4001",
        meta: `${t.network_input_source ?? "—"} · ${t.operating_mode ?? "—"}`,
        statusText: t.guardrail_status ?? "WAITING",
      };
    }

    if (id === "ai") {
      return {
        ...common,
        value: ai.fault_domain?.label ?? "WAITING",
        meta:
          ai.fault_domain?.confidence == null
            ? "No live confidence"
            : `${(Number(ai.fault_domain.confidence) * 100).toFixed(1)}% confidence`,
        statusText: ai.trust?.decision ?? "WAITING",
      };
    }

    if (id === "pico") {
      return {
        ...common,
        value: ":4000",
        meta: `${cmd.pico_ai_fault_domain ?? "WAITING"} · ${cmd.dual_ai_agreement ?? "WAITING"}`,
        statusText: pico.mode_decision ?? "WAITING",
      };
    }

    if (id === "guard") {
      return {
        ...common,
        value: t.operating_mode ?? "—",
        meta: `${t.active_power_source ?? "—"} · ${pico.generator_action ?? "—"}`,
        statusText: t.guardrail_status ?? "WAITING",
      };
    }

    if (id === "grid") {
      return {
        ...common,
        value: t.grid_available === true ? "AVAILABLE" : "FAILED",
        meta: t.active_power_source ?? "—",
        statusText: t.grid_available === true ? "UP" : "DOWN",
      };
    }

    if (id === "generator") {
      const running = (t.generator_feedback_running ?? t.generator_running) === true;
      return {
        ...common,
        value: running ? "RUNNING" : "STOPPED",
        meta: t.generator_verification_state ?? pico.generator_action ?? "—",
        statusText: running ? "ACTIVE" : "IDLE",
      };
    }

    if (id === "battery") {
      return {
        ...common,
        value: `${Number(t.battery_soc_pct ?? 0).toFixed(1)}%`,
        meta: `${Number(t.battery_voltage_v ?? 0).toFixed(2)} V`,
        statusText: Number(t.battery_soc_pct ?? 0) > 25 ? "HEALTHY" : "LOW",
      };
    }

    if (id === "load") {
      return {
        ...common,
        value: `${Number(t.traffic_load_pct ?? 0).toFixed(1)}%`,
        meta: `${Number(t.managed_power_kw ?? 0).toFixed(3)} kW · ${t.operating_mode ?? "—"}`,
        statusText: "MANAGED",
      };
    }

    return common;
  }

  function liveRows(id) {
    const { s, t, ai, cmd, pico } = data();

    const rows = {
      phoneA: [
        ["Subscriber", "0712 000 001"],
        ["Active calls", t.network_active_calls ?? 0],
        ["Latency", `${t.latency_ms ?? "—"} ms`],
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
      ],
      phoneB: [
        ["Subscriber", "0712 000 002"],
        ["Active calls", t.network_active_calls ?? 0],
        ["Packet loss", `${t.packet_loss_pct ?? "—"} %`],
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
      ],
      bts: [
        ["Input source", t.network_input_source ?? "—"],
        ["Latency", `${t.latency_ms ?? "—"} ms`],
        ["Packet loss", `${t.packet_loss_pct ?? "—"} %`],
        ["RSSI", `${t.rssi_dbm ?? "—"} dBm`],
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
        ["Radio", t.radio_operational === true ? "OPERATIONAL" : "FAULT / UNKNOWN"],
        ["Upstream", t.upstream_reachable === true ? "REACHABLE" : "DOWN / UNKNOWN"],
      ],
      esp32: [
        ["Telemetry", s.meta?.telemetryStatus ?? "—"],
        ["Input source", t.network_input_source ?? "—"],
        ["Mode", t.operating_mode ?? "—"],
        ["Guardrail", t.guardrail_status ?? "—"],
        ["Physical link", t.physical_link_up === true ? "UP" : "DOWN / UNKNOWN"],
      ],
      ai: [
        ["Fault domain", ai.fault_domain?.label ?? "—"],
        ["Confidence", ai.fault_domain?.confidence == null ? "—" : `${(Number(ai.fault_domain.confidence) * 100).toFixed(1)} %`],
        ["Trust", ai.trust?.decision ?? "—"],
        ["Anomaly", ai.anomaly?.flagged === true ? "YES" : "NO"],
      ],
      pico: [
        ["Embedded domain", cmd.pico_ai_fault_domain ?? "—"],
        ["Dual AI", cmd.dual_ai_agreement ?? "—"],
        ["Decision", pico.mode_decision ?? "—"],
        ["Power source", pico.power_source_decision ?? "—"],
        ["Generator", pico.generator_action ?? "—"],
      ],
      guard: [
        ["Final mode", t.operating_mode ?? "—"],
        ["Guardrail", t.guardrail_status ?? "—"],
        ["Power source", t.active_power_source ?? "—"],
        ["Generator", (t.generator_feedback_running ?? t.generator_running) === true ? "RUNNING" : "STOPPED"],
      ],
      grid: [
        ["Available", t.grid_available === true ? "YES" : "NO"],
        ["Active source", t.active_power_source ?? "—"],
        ["DC voltage", `${t.dc_voltage_v ?? "—"} V`],
        ["DC power", `${t.dc_power_w ?? "—"} W`],
      ],
      generator: [
        ["Running", (t.generator_feedback_running ?? t.generator_running) === true ? "YES" : "NO"],
        ["Verification", t.generator_verification_state ?? "—"],
        ["Action", pico.generator_action ?? "—"],
      ],
      battery: [
        ["State of charge", `${t.battery_soc_pct ?? "—"} %`],
        ["Voltage", `${t.battery_voltage_v ?? "—"} V`],
        ["Trend", `${t.battery_soc_trend_pct_per_min ?? "—"} %/min`],
      ],
      load: [
        ["Traffic", `${t.traffic_load_pct ?? "—"} %`],
        ["Managed power", `${t.managed_power_kw ?? "—"} kW`],
        ["Mode", t.operating_mode ?? "—"],
        ["Active calls", t.network_active_calls ?? 0],
      ],
    };

    return rows[id] ?? [];
  }

  function linksFor(id) {
    const out = [];

    for (const [a, b, kind] of EDGES) {
      if (a === id) out.push([b, "OUT", kind]);
      if (b === id) out.push([a, "IN", kind]);
    }

    return out;
  }

  function nodeHtml(id) {
    const d = nodeDisplay(id);
    const def = DEFINITIONS[id];

    return `
      <article
        class="be-node ${def.cls ?? ""} ${d.status}"
        data-be-node="${id}"
        style="left:${ui.positions[id][0]}px;top:${ui.positions[id][1]}px"
      >
        <span class="be-port in"></span>
        <span class="be-port out"></span>

        <header class="be-node-head">
          <strong>${esc(def.title)}</strong>
          <span class="material-symbols-rounded">${esc(def.icon)}</span>
        </header>

        <div class="be-node-body">
          <div class="be-node-value" data-be-bind="${id}:value">${esc(d.value)}</div>
          <div class="be-node-meta" data-be-bind="${id}:meta">${esc(d.meta)}</div>
          <div class="be-node-status">
            <i class="be-dot ${d.status}" data-be-bind-class="${id}:dot"></i>
            <span data-be-bind="${id}:status">${esc(d.statusText)}</span>
          </div>
        </div>
      </article>
    `;
  }

  function build() {
    const page = document.querySelector(".noc-page");
    if (!page) return false;
    if (page.querySelector(":scope > .be-workspace")) return true;

    loadPositions();

    const root = document.createElement("section");
    root.className = "be-workspace";
    root.innerHTML = `
      <section class="be-editor">
        <div class="be-editor-bar">
          <div class="be-editor-bar-left">
            <strong>Node Editor</strong>
            <span>MMB/Shift+drag pan · wheel zoom · drag headers</span>
          </div>
          <div class="be-editor-bar-right">
            <button type="button" class="be-tool" data-be-action="fit">View All</button>
            <button type="button" class="be-tool" data-be-action="reset">Reset Layout</button>
            <span class="be-zoom-label">100%</span>
          </div>
        </div>

        <div class="be-viewport">
          <div class="be-world">
            <svg class="be-wires" viewBox="0 0 ${WORLD_W} ${WORLD_H}" aria-hidden="true"></svg>
            ${Object.keys(DEFINITIONS).map(nodeHtml).join("")}
          </div>
        </div>
      </section>

      <aside class="be-inspector">
        <div class="be-inspector-title">
          <strong>Properties</strong>
          <span>Selected node</span>
        </div>

        <nav class="be-tabs" aria-label="Node inspector">
          <button type="button" class="be-tab active" data-be-tab="properties" title="Properties">
            <span class="material-symbols-rounded">tune</span>
          </button>
          <button type="button" class="be-tab" data-be-tab="live" title="Live values">
            <span class="material-symbols-rounded">monitoring</span>
          </button>
          <button type="button" class="be-tab" data-be-tab="links" title="Connections">
            <span class="material-symbols-rounded">account_tree</span>
          </button>
        </nav>

        <div class="be-selection"></div>
        <div class="be-inspector-body"></div>
      </aside>
    `;

    page.append(root);

    ui.root = root;
    ui.viewport = root.querySelector(".be-viewport");
    ui.world = root.querySelector(".be-world");
    ui.wires = root.querySelector(".be-wires");
    ui.inspector = root.querySelector(".be-inspector");
    ui.zoomLabel = root.querySelector(".be-zoom-label");

    bindEvents();
    fitView(false);
    selectNode(ui.selected);
    updateAll();
    return true;
  }

  function transform() {
    if (!ui.world) return;

    ui.world.style.transform =
      `translate(${ui.tx}px, ${ui.ty}px) scale(${ui.scale})`;

    if (ui.zoomLabel) {
      ui.zoomLabel.textContent = `${Math.round(ui.scale * 100)}%`;
    }
  }

  function fitView(animate = false) {
    if (!ui.viewport) return;

    const rect = ui.viewport.getBoundingClientRect();
    const sx = Math.max(.48, Math.min(.95, (rect.width - 26) / WORLD_W));
    const sy = Math.max(.48, Math.min(.95, (rect.height - 26) / WORLD_H));

    ui.scale = Math.min(sx, sy);
    ui.tx = Math.max(10, (rect.width - WORLD_W * ui.scale) / 2);
    ui.ty = Math.max(10, (rect.height - WORLD_H * ui.scale) / 2);

    if (animate) {
      ui.world.style.transition = "transform 140ms ease";
      window.setTimeout(() => {
        if (ui.world) ui.world.style.transition = "";
      }, 160);
    }

    transform();
  }

  function resetLayout() {
    ui.positions = structuredClone(DEFAULT_POS);
    savePositions();

    for (const [id, pos] of Object.entries(ui.positions)) {
      const node = ui.world?.querySelector(`[data-be-node="${id}"]`);
      if (!node) continue;
      node.style.left = `${pos[0]}px`;
      node.style.top = `${pos[1]}px`;
    }

    fitView(true);
    drawWires();
  }

  function portPoint(id, side) {
    const node = ui.world?.querySelector(`[data-be-node="${id}"]`);
    if (!node) return null;

    const [x, y] = ui.positions[id];
    const width = node.offsetWidth;
    const height = node.offsetHeight;

    return {
      x: side === "out" ? x + width : x,
      y: y + height / 2,
    };
  }

  function edgeClass(a, b, kind) {
    const sa = statusFor(a);
    const sb = statusFor(b);

    if (sa === "bad" || sb === "bad") return "down";
    if (sa === "warn" || sb === "warn") return "idle";
    if (kind === "control") return "control";
    if (kind === "power") return "power";
    return "";
  }

  function drawWires() {
    if (!ui.wires) return;

    const paths = [];

    for (const [a, b, kind] of EDGES) {
      const p1 = portPoint(a, "out");
      const p2 = portPoint(b, "in");
      if (!p1 || !p2) continue;

      const dx = Math.max(45, Math.abs(p2.x - p1.x) * .48);
      const d =
        `M ${p1.x.toFixed(1)} ${p1.y.toFixed(1)} ` +
        `C ${(p1.x + dx).toFixed(1)} ${p1.y.toFixed(1)}, ` +
        `${(p2.x - dx).toFixed(1)} ${p2.y.toFixed(1)}, ` +
        `${p2.x.toFixed(1)} ${p2.y.toFixed(1)}`;

      paths.push(
        `<path class="be-wire ${edgeClass(a,b,kind)}" d="${d}"></path>`,
      );
    }

    ui.wires.innerHTML = paths.join("");
  }

  function selectNode(id) {
    if (!DEFINITIONS[id]) return;
    ui.selected = id;

    for (const node of ui.root?.querySelectorAll(".be-node") ?? []) {
      node.classList.toggle(
        "selected",
        node.dataset.beNode === id,
      );
    }

    renderInspector();
  }

  function row(label, value) {
    return `
      <div class="be-row">
        <span>${esc(label)}</span>
        <strong>${esc(value)}</strong>
      </div>
    `;
  }

  function section(title, icon, body) {
    return `
      <section class="be-section">
        <div class="be-section-head">
          <span class="material-symbols-rounded">${icon}</span>
          ${esc(title)}
        </div>
        <div class="be-rows">${body}</div>
      </section>
    `;
  }

  function renderInspector() {
    if (!ui.inspector) return;

    const id = ui.selected;
    const def = DEFINITIONS[id];
    const d = nodeDisplay(id);

    const selection = ui.inspector.querySelector(".be-selection");
    const body = ui.inspector.querySelector(".be-inspector-body");

    selection.innerHTML = `
      <div class="be-selection-icon material-symbols-rounded">${esc(def.icon)}</div>
      <div class="be-selection-copy">
        <span>${esc(def.type)}</span>
        <strong>${esc(def.title)}</strong>
        <small>${esc(def.endpoint)}</small>
      </div>
      <span class="be-selection-status">
        <i class="be-dot ${d.status}"></i>${esc(d.statusText)}
      </span>
    `;

    if (ui.tab === "properties") {
      body.innerHTML =
        section(
          "Node",
          "tune",
          [
            row("Type", def.type),
            row("Endpoint", def.endpoint),
            row("Transport", def.transport),
            row("Role", def.role),
          ].join(""),
        ) +
        section(
          "Editor",
          "deployed_code",
          [
            row("Position X", `${Math.round(ui.positions[id][0])}`),
            row("Position Y", `${Math.round(ui.positions[id][1])}`),
            row("Status", d.statusText),
          ].join(""),
        );
    } else if (ui.tab === "live") {
      body.innerHTML = section(
        "Live values",
        "monitoring",
        liveRows(id).map(([k,v]) => row(k,v)).join(""),
      );
    } else {
      const links = linksFor(id);
      body.innerHTML = `
        <section class="be-section">
          <div class="be-section-head">
            <span class="material-symbols-rounded">account_tree</span>
            Connections
          </div>
          <div class="be-rows">
            ${
              links.length
                ? links.map(([other, direction, kind]) => `
                    <div class="be-link-row">
                      <i class="be-link-socket"></i>
                      <span>${esc(DEFINITIONS[other].title)}</span>
                      <small>${esc(direction)} · ${esc(kind.toUpperCase())}</small>
                    </div>
                  `).join("")
                : row("Connections", "None")
            }
          </div>
        </section>
      `;
    }
  }

  function updateAll() {
    if (!ui.root) return;

    for (const id of Object.keys(DEFINITIONS)) {
      const d = nodeDisplay(id);
      const node = ui.root.querySelector(`[data-be-node="${id}"]`);
      if (!node) continue;

      node.classList.remove("good", "warn", "bad");
      node.classList.add(d.status);

      const value = node.querySelector(`[data-be-bind="${id}:value"]`);
      const meta = node.querySelector(`[data-be-bind="${id}:meta"]`);
      const status = node.querySelector(`[data-be-bind="${id}:status"]`);
      const dot = node.querySelector(`[data-be-bind-class="${id}:dot"]`);

      if (value) value.textContent = d.value;
      if (meta) meta.textContent = d.meta;
      if (status) status.textContent = d.statusText;
      if (dot) dot.className = `be-dot ${d.status}`;
    }

    drawWires();
    renderInspector();
  }

  function bindEvents() {
    ui.root.addEventListener("click", (event) => {
      const tab = event.target.closest("[data-be-tab]");
      if (tab) {
        ui.tab = tab.dataset.beTab;

        for (const item of ui.root.querySelectorAll("[data-be-tab]")) {
          item.classList.toggle("active", item === tab);
        }

        renderInspector();
        return;
      }

      const action = event.target.closest("[data-be-action]");
      if (action) {
        if (action.dataset.beAction === "fit") fitView(true);
        if (action.dataset.beAction === "reset") resetLayout();
        return;
      }

      const node = event.target.closest("[data-be-node]");
      if (node) selectNode(node.dataset.beNode);
    });

    ui.root.addEventListener("pointerdown", (event) => {
      const head = event.target.closest(".be-node-head");

      if (head) {
        const node = head.closest("[data-be-node]");
        const id = node.dataset.beNode;

        selectNode(id);

        ui.drag = {
          id,
          startX: event.clientX,
          startY: event.clientY,
          x: ui.positions[id][0],
          y: ui.positions[id][1],
          pointerId: event.pointerId,
        };

        head.setPointerCapture?.(event.pointerId);
        event.preventDefault();
        return;
      }

      const onBackground = event.target === ui.viewport;
      const wantsPan =
        event.button === 1 ||
        (event.button === 0 && event.shiftKey);

      if (onBackground && wantsPan) {
        ui.pan = {
          startX: event.clientX,
          startY: event.clientY,
          tx: ui.tx,
          ty: ui.ty,
          pointerId: event.pointerId,
        };

        ui.viewport.classList.add("is-panning");
        ui.viewport.setPointerCapture?.(event.pointerId);
        event.preventDefault();
      }
    });

    ui.root.addEventListener("pointermove", (event) => {
      if (ui.drag) {
        const dx = (event.clientX - ui.drag.startX) / ui.scale;
        const dy = (event.clientY - ui.drag.startY) / ui.scale;

        const x = Math.max(0, Math.min(WORLD_W - 120, ui.drag.x + dx));
        const y = Math.max(0, Math.min(WORLD_H - 60, ui.drag.y + dy));

        ui.positions[ui.drag.id] = [x, y];

        const node = ui.root.querySelector(`[data-be-node="${ui.drag.id}"]`);
        node.style.left = `${x}px`;
        node.style.top = `${y}px`;

        drawWires();
        if (ui.tab === "properties") renderInspector();
      }

      if (ui.pan) {
        ui.tx = ui.pan.tx + (event.clientX - ui.pan.startX);
        ui.ty = ui.pan.ty + (event.clientY - ui.pan.startY);
        transform();
      }
    });

    const finishPointer = () => {
      if (ui.drag) savePositions();
      ui.drag = null;
      ui.pan = null;
      ui.viewport?.classList.remove("is-panning");
    };

    ui.root.addEventListener("pointerup", finishPointer);
    ui.root.addEventListener("pointercancel", finishPointer);

    ui.viewport.addEventListener(
      "wheel",
      (event) => {
        event.preventDefault();

        const old = ui.scale;
        const next = Math.max(.48, Math.min(1.35, old * (event.deltaY < 0 ? 1.08 : .92)));

        const rect = ui.viewport.getBoundingClientRect();
        const px = event.clientX - rect.left;
        const py = event.clientY - rect.top;

        const worldX = (px - ui.tx) / old;
        const worldY = (py - ui.ty) / old;

        ui.scale = next;
        ui.tx = px - worldX * next;
        ui.ty = py - worldY * next;

        transform();
      },
      { passive: false },
    );
  }

  const observer = new MutationObserver(() => {
    if (!document.querySelector(".noc-page")) return;
    if (document.querySelector(".noc-page > .be-workspace")) return;
    window.requestAnimationFrame(build);
  });

  observer.observe(document.body, {
    childList: true,
    subtree: true,
  });

  window.addEventListener("resize", () => {
    if (ui.root?.isConnected) fitView(false);
  });

  window.addEventListener("abs-dashboard-rendered", () => {
    window.requestAnimationFrame(() => {
      if (!build()) return;
      updateAll();
    });
  });

  window.setInterval(() => {
    if (ui.root?.isConnected) updateAll();
  }, 1500);

  window.requestAnimationFrame(build);
})();
