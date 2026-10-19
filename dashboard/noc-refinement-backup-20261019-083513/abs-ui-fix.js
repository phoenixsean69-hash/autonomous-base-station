/* ABS Dashboard UI Repair: fault operations display, local-only overlay. */
(() => {
  "use strict";
  const subscriberNumbers = ["0712000001", "0712000002", "0712000003", "0712000004", "0712000005", "0712000006"];
  let selected = "upstream";
  const escHtml = value => String(value ?? "—").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]));
  const fmt = v => typeof v === "number" && Number.isFinite(v) ? v.toFixed(1) : (v ?? "—");
  const availability = (v, yes, no) => v === true ? yes : v === false ? no : "UNKNOWN";
  const norm = value => String(value ?? "").replace(/\D/g, "");
  const niceNum = value => norm(value).replace(/^(\d{4})(\d{3})(\d{3})$/, "$1 $2 $3");
  function current() { return window.ABSDashboardGetState?.() ?? {}; }
  function synthesize(s) {
    const btsLive = s.meta?.btsPresenceAvailable === true && !!s.bts;
    const b = btsLive ? s.bts : {};
    const tLive = s.meta?.telemetryStatus === "LIVE" && !!s.telemetry;
    const t = tLive ? s.telemetry : {};
    const aiLive = s.meta?.aiFresh === true && !!s.ai;
    const a = aiLive ? s.ai : {};
    const cmd = a.runtime?.ai_command ?? {};
    const pico = a.runtime?.pico_decision ?? {};
    const online = new Set(Array.isArray(b.subscriber_numbers) ? b.subscriber_numbers.map(norm) : []);
    const callPairs = Array.isArray(b.active_call_pairs) ? b.active_call_pairs.filter(x => ["RINGING", "CONNECTED"].includes(String(x.state).toUpperCase())) : [];
    const inCall = new Set(callPairs.flatMap(x => [norm(x.caller), norm(x.callee)]));
    const calls = btsLive ? Math.max(0, Number(b.active_calls ?? callPairs.length) || 0) : null;
    const phoneStatus = n => !btsLive ? "unknown" : !online.has(n) ? "idle" : inCall.has(n) ? "call" : "good";
    const upstreamReason = !btsLive ? "BTS telemetry unavailable" : b.link_up === false ? "PHYSICAL BACKHAUL LINK DOWN" : b.upstream_reachable === false ? "LINK UP · UPSTREAM UNREACHABLE" : "Backhaul path healthy";
    const radioState = !btsLive ? "unknown" : b.radio_operational === false ? "bad" : b.radio_operational === true ? "good" : "unknown";
    const localState = !tLive ? "unknown" : ["FAULT", "DEGRADED"].includes(String(t.local_site_status).toUpperCase()) ? "bad" : String(t.local_site_status).toUpperCase() === "NORMAL" ? "good" : "unknown";
    const upstreamState = !btsLive ? "unknown" : b.link_up === false || b.upstream_reachable === false ? "bad" : "good";
    const linkState = !btsLive ? "unknown" : b.link_up === false ? "bad" : b.link_up === true ? "good" : "unknown";
    const stateOf = (ok, value) => ok ? value : "unknown";
    const rows = (items) => items.map(([k,v]) => `<div class="fo-pair-row"><span>${escHtml(k)}</span><strong>${escHtml(v)}</strong></div>`).join("");
    const info = {
      phones: {
        title: "Subscriber Service", state: stateOf(btsLive, online.size > 0 ? "good" : "idle"),
        label: !btsLive ? "BTS OFFLINE" : `${online.size}/6 REGISTERED · ${calls} CALLS`,
        hint: "Actual BTS registrations; not derived from simulated traffic load.",
        rows: [["BTS data", btsLive ? "FRESH" : "UNAVAILABLE"],["Registered", btsLive ? `${online.size}/6` : "—"],["Active calls", calls ?? "—"],["Voice media", "Peer-to-peer WebRTC over Wi-Fi"]]
      },
      bts: {
        title: "BTS-001", state: radioState,
        label: !btsLive ? "SERVICE UNAVAILABLE" : b.radio_operational === false ? "RADIO FAULT" : "RADIO OPERATIONAL",
        hint: "Live BTS backend + hardware telemetry on service port 8100.",
        rows: [["Telemetry", btsLive ? "CONNECTED" : "UNAVAILABLE"],["Hardware fresh", availability(b.hardware_connected,"YES","NO")],["Radio", availability(b.radio_operational,"OPERATIONAL","FAULT")],["Traffic model", btsLive ? `${fmt(b.traffic_load_pct)}%` : "—"],["Active calls", calls ?? "—"]]
      },
      link: {
        title: "Physical Backhaul", state: linkState,
        label: !btsLive ? "NO LIVE READING" : availability(b.link_up,"LINK UP","LINK DOWN"),
        hint: "Physical link failure and upstream reachability are independent signals.",
        rows: [["Physical link", btsLive ? availability(b.link_up,"UP","DOWN") : "—"],["Upstream reachable", btsLive ? availability(b.upstream_reachable,"YES","NO") : "—"],["Interpretation", upstreamReason],["ESP32 rule cause", tLive ? t.backhaul_rule_cause : "—"]]
      },
      upstream: {
        title: "Upstream Network", state: upstreamState,
        label: !btsLive ? "NO LIVE READING" : b.link_up === false ? "BACKHAUL LINK FAILURE" : b.upstream_reachable === false ? "UPSTREAM OUTAGE" : "UPSTREAM HEALTHY",
        hint: upstreamReason,
        rows: [["Link up", btsLive ? availability(b.link_up,"YES","NO") : "—"],["Upstream reachable", btsLive ? availability(b.upstream_reachable,"YES","NO") : "—"],["Backhaul available", btsLive ? availability(b.backhaul_available,"YES","NO") : "—"],["Packet loss", btsLive ? `${fmt(b.packet_loss_pct)}%` : "—"],["Latency", btsLive ? `${fmt(b.latency_ms)} ms` : "—"],["ESP32 classification", tLive ? t.fault_label : "—"]]
      },
      local: {
        title: "Local Equipment", state: localState,
        label: !tLive ? "ESP32 TELEMETRY OFFLINE" : `LOCAL ${t.local_site_status ?? "UNKNOWN"}`,
        hint: "Cooling, radio, RF, rectifier, grid, battery and mechanical conditions.",
        rows: [["Local status", tLive ? t.local_site_status : "—"],["Radio", tLive ? availability(t.radio_operational,"OK","FAULT") : "—"],["Fan", tLive ? availability(t.fan_operational,"OK","FAULT") : "—"],["Rectifier", tLive ? availability(t.rectifier_normal,"OK","FAULT") : "—"],["RF health", tLive ? t.rf_health : "—"],["Power supply", tLive ? t.active_power_source : "—"]]
      },
      ai: {
        title: "Laptop AI + Pico", state: stateOf(aiLive, cmd.dual_ai_agreement === "DISAGREE" ? "bad" : cmd.dual_ai_agreement === "AGREE" ? "good" : "unknown"),
        label: !aiLive ? (s.ai ? "AI RESULTS STALE" : "AI NOT RUNNING") : `${a.fault_domain?.label ?? "UNKNOWN"} · ${cmd.dual_ai_agreement ?? "WAITING"}`,
        hint: "Independent fault-domain predictions must agree before AI automation is trusted.",
        rows: [["Laptop domain", aiLive ? a.fault_domain?.label : "—"],["Pico domain", aiLive ? cmd.pico_ai_fault_domain : "—"],["Agreement", aiLive ? cmd.dual_ai_agreement : "—"],["Trust", aiLive ? a.trust?.decision : "—"],["Upstream root cause", aiLive ? (a.root_cause?.upstream?.label ?? "NOT APPLICABLE") : "—"],["Local root cause", aiLive ? (a.root_cause?.local?.label ?? "NOT APPLICABLE") : "—"]]
      },
      control: {
        title: "ESP32 Safety + Power", state: stateOf(tLive, String(t.guardrail_status).includes("FAULT") ? "bad" : t.guardrail_status === "PASSED" ? "good" : "idle"),
        label: !tLive ? "ESP32 TELEMETRY OFFLINE" : `${t.operating_mode ?? "—"} · ${t.guardrail_status ?? "—"}`,
        hint: "ESP32 guardrails have final authority over the AI recommendation.",
        rows: [["Requested mode", tLive ? t.requested_mode : "—"],["Final mode", tLive ? t.operating_mode : "—"],["Guardrail", tLive ? t.guardrail_status : "—"],["Power source", tLive ? t.active_power_source : "—"],["Battery SoC", tLive ? `${fmt(t.battery_soc_pct)}%` : "—"],["Recovery state", tLive ? t.recovery_state : "—"]]
      }
    };
    return { info, btsLive, tLive, aiLive, online, inCall, calls, rows, phoneStatus, subscriberNumbers };
  }
  function card(id, info, suffix = "") {
    const n = info[id];
    return `<button type="button" class="fo-node fo-${n.state} ${selected === id ? "fo-selected" : ""}" data-fo-node="${id}" aria-pressed="${selected === id}"><span class="fo-node-heading">${escHtml(n.title)}<i class="fo-light"></i></span><strong>${escHtml(n.label)}</strong><small>${escHtml(suffix || n.hint)}</small></button>`;
  }
  function newTelemetry() {
    const p = synthesize(current());
    const {info, btsLive, tLive, aiLive, online, inCall, calls, rows} = p;
    const n = info[selected] ?? info.upstream;
    const phoneItems = subscriberNumbers.map(number => {
      const status = p.phoneStatus(number);
      const label = !btsLive ? "UNKNOWN" : inCall.has(number) ? "IN CALL" : online.has(number) ? "ONLINE" : "OFFLINE";
      return `<div class="fo-subscriber fo-${status}"><i class="fo-light"></i><span>${niceNum(number)}</span><strong>${label}</strong></div>`;
    }).join("");
    const summary = !btsLive && !tLive && !aiLive ? "Systems are stopped. This map shows the architecture; live status resumes when services restart." :
      !btsLive ? "BTS-001 telemetry unavailable: subscriber and upstream status are UNKNOWN." :
      info.upstream.state === "bad" ? "UPSTREAM ALERT: BTS-001 reports a broken link or unreachable upstream network." :
      info.local.state === "bad" ? "LOCAL ALERT: the Main ESP32 reports equipment/site degradation." :
      "Live feed connected. Select any node for measurements and fault evidence.";
    return `<section class="fo-page">
      <header class="fo-title"><div><span>READ-ONLY MONITORING</span><h1>Fault & Control Map</h1><p>Two independent fault dimensions: <b>upstream network</b> and <b>local equipment</b>.</p></div><div class="fo-liveness"><span class="${btsLive ? "fo-good" : "fo-unknown"}">BTS ${btsLive ? "LIVE" : "OFFLINE"}</span><span class="${tLive ? "fo-good" : "fo-unknown"}">ESP32 ${tLive ? "LIVE" : "OFFLINE"}</span><span class="${aiLive ? "fo-good" : "fo-unknown"}">AI ${aiLive ? "LIVE" : "OFFLINE"}</span></div></header>
      <p class="fo-summary">${escHtml(summary)}</p>
      <div class="fo-workspace"><div class="fo-map">
        <div class="fo-upper"><div class="fo-subscribers"><div class="fo-group-title">MOBILE SUBSCRIBERS · ${btsLive ? `${online.size}/6 REGISTERED · ${calls} CALLS` : "FEED UNAVAILABLE"}</div>${phoneItems}</div><div class="fo-bridge"><div class="fo-arrowline">SIGNALLING →</div>${card("bts",info)}</div></div>
        <div class="fo-flow-label">BTS status splits into independently diagnosed paths</div>
        <div class="fo-dimensions"><div class="fo-lane"><div class="fo-group-title">DIMENSION 1 · UPSTREAM</div>${card("link",info)}<div class="fo-arrowdown">↓</div>${card("upstream",info)}</div><div class="fo-lane"><div class="fo-group-title">DIMENSION 2 · LOCAL</div>${card("local",info)}<div class="fo-local-notes">RF · cooling · radio · power · vibration</div></div></div>
        <div class="fo-arrowdown fo-arrowwide">↓ Fault evidence + temporal telemetry</div>
        ${card("ai",info)}
        <div class="fo-arrowdown">↓ Recommendation + validation</div>
        ${card("control",info)}
        <div class="fo-disclaimer">WebRTC voice media is peer-to-peer over Wi-Fi; Wokwi faults do not physically disconnect that media path.</div>
      </div><aside class="fo-detail"><div class="fo-detail-header">FAULT INSPECTOR <span>LIVE WHEN AVAILABLE</span></div><div class="fo-inspector-state fo-${n.state}"><span>${escHtml(n.title)}</span><strong>${escHtml(n.label)}</strong><small>${escHtml(n.hint)}</small></div><div class="fo-rows">${rows(n.rows)}</div><p class="fo-inspector-help">Select a node on the map to inspect its measurements. Unknown/offline is not the same as a confirmed fault.</p></aside></div>
    </section>`;
  }
  // Avoid the legacy Blender node editor and its competing mutation observers.
  // The telemetry renderer is global because app.js is loaded as a classic script.
  window.renderTelemetry = newTelemetry;

  function addOfflineNotice(original, type) {
    return function() {
      const s = current();
      const available = type === "AI" ? s.meta?.aiFresh === true : s.meta?.telemetryStatus === "LIVE";
      const label = type === "AI" ? "Laptop AI and Pico results are not live." : "Main ESP32 control telemetry is offline.";
      return (available ? "" : `<div class="fo-offline-note">${label} The screen remains available for inspection; values will populate when services restart.</div>`) + original();
    };
  }
  if (typeof window.renderAI === "function") window.renderAI = addOfflineNotice(window.renderAI,"AI");
  if (typeof window.renderControl === "function") window.renderControl = addOfflineNotice(window.renderControl,"CONTROL");

  // Fast, immediate inspector selection. Only this light page is redrawn.
  document.addEventListener("click", ev => {
    const el = ev.target.closest?.("[data-fo-node]");
    if (!el) return;
    selected = el.dataset.foNode;
    if (window.ABSDashboardGetActiveTab?.() === "telemetry" && typeof window.render === "function") window.render();
  });

  if (window.ABSDashboardGetActiveTab?.() === "telemetry" && typeof window.render === "function") window.render();
})();
