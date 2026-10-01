/*
 * Presentation-only decorator for the ABS dashboard.
 *
 * app.js owns all data, rendering and WebSocket behaviour.
 * This file only adds RoadSafe-style semantic icons to dynamic DOM nodes
 * after app.js renders them.
 */

(() => {
  "use strict";

  const content = document.getElementById("content");
  if (!content) return;

  const statIcons = new Map([
    ["Battery State of Charge", "battery_5_bar"],
    ["Active Power Source", "electrical_services"],
    ["Generator", "electric_bolt"],
    ["Traffic Load", "network_cell"],
    ["Backhaul", "router"],
    ["Managed Site Power", "speed"],
  ]);

  const pageIcons = new Map([
    ["Live Telemetry", "sensors"],
    ["AI Intelligence", "neurology"],
    ["Control Chain", "account_tree"],
    ["Model Performance", "analytics"],
  ]);

  const panelIconRules = [
    ["CONTROL PATH", "account_tree"],
    ["END-TO-END FLOW", "account_tree"],
    ["TEMPORAL AI", "neurology"],
    ["DOMAIN TCN", "neurology"],
    ["DUAL AI + PICO", "developer_board"],
    ["DUAL AI", "developer_board"],
    ["LIVE HISTORY", "monitoring"],
    ["AUTOENCODER", "query_stats"],
    ["HIERARCHICAL DIAGNOSIS", "schema"],
    ["AI OUTPUT", "neurology"],
    ["PICO OUTPUT", "developer_board"],
    ["ESP32 OUTPUT", "memory"],
    ["POWER SYSTEM", "bolt"],
    ["SAFETY AUTHORITY", "verified_user"],
    ["MODEL CONTEXT", "info"],
    ["ENVIRONMENT", "thermostat"],
    ["MECHANICAL", "vibration"],
    ["POWER", "battery_charging_full"],
    ["RADIO FREQUENCY", "cell_tower"],
    ["BACKHAUL", "router"],
    ["BOOLEAN INPUTS", "memory"],
  ];

  function makeIcon(name, className = "") {
    const holder = document.createElement("span");
    holder.className = [
      "material-symbols-outlined",
      "rs-icon",
      className,
    ]
      .filter(Boolean)
      .join(" ");
    holder.setAttribute("aria-hidden", "true");
    holder.textContent = name;
    return holder;
  }

  function decorateHero() {
    const hero = content.querySelector(".hero");
    if (hero && !hero.querySelector(":scope > .rs-hero-icon")) {
      const holder = document.createElement("div");
      holder.className = "rs-hero-icon";
      holder.append(makeIcon("cell_tower"));
      hero.prepend(holder);
    }

    const current = content.querySelector(".current-state");
    if (
      current &&
      !current.querySelector(":scope > .rs-current-state-icon")
    ) {
      const holder = document.createElement("div");
      holder.className = "rs-current-state-icon";
      holder.append(makeIcon("monitor_heart"));
      current.prepend(holder);
    }
  }

  function decorateStats() {
    for (const card of content.querySelectorAll(".stat-card")) {
      if (card.querySelector(":scope > .rs-stat-head")) continue;

      const label = card.querySelector(":scope > .stat-label");
      if (!label) continue;

      const head = document.createElement("div");
      head.className = "rs-stat-head";

      const iconHolder = document.createElement("div");
      iconHolder.className = "rs-stat-icon";
      iconHolder.append(
        makeIcon(statIcons.get(label.textContent.trim()) || "monitoring"),
      );

      label.before(head);
      head.append(iconHolder, label);
    }
  }

  function panelIconName(panelHead) {
    const eyebrow =
      panelHead.querySelector(".eyebrow")?.textContent?.trim().toUpperCase() ||
      "";

    const heading =
      panelHead.querySelector("h2, h3")?.textContent?.trim().toUpperCase() ||
      "";

    const combined = `${eyebrow} ${heading}`;

    for (const [needle, icon] of panelIconRules) {
      if (combined.includes(needle)) return icon;
    }

    return "dashboard_customize";
  }

  function decoratePanelHeads() {
    for (const head of content.querySelectorAll(".panel-head")) {
      if (head.querySelector(":scope > .rs-panel-icon")) continue;

      const holder = document.createElement("div");
      holder.className = "rs-panel-icon";
      holder.append(makeIcon(panelIconName(head)));
      head.prepend(holder);

      const readOnly = head.querySelector(".read-only");
      if (readOnly && !readOnly.querySelector(".material-symbols-outlined")) {
        readOnly.prepend(makeIcon("lock"));
      }
    }
  }

  function decoratePageTitles() {
    for (const title of content.querySelectorAll(".page-title")) {
      if (title.querySelector(":scope > .rs-page-title-icon")) continue;

      const heading = title.querySelector("h1")?.textContent?.trim() || "";
      const holder = document.createElement("div");
      holder.className = "rs-page-title-icon";
      holder.append(makeIcon(pageIcons.get(heading) || "monitoring"));
      title.prepend(holder);
    }
  }

  function decorateTopStatus() {
    const pairs = [
      ["dashboardStatus", "dashboard"],
      ["espStatus", "memory"],
    ];

    for (const [id, iconName] of pairs) {
      const pill = document.getElementById(id);
      if (!pill || pill.querySelector(".rs-status-icon")) continue;

      const dot = pill.querySelector("i");
      const icon = makeIcon(iconName, "rs-status-icon");
      icon.style.fontSize = "12px";

      if (dot) {
        dot.after(icon);
      } else {
        pill.prepend(icon);
      }
    }
  }

  let scheduled = false;

  function decorate() {
    scheduled = false;
    decorateHero();
    decorateStats();
    decoratePanelHeads();
    decoratePageTitles();
    decorateTopStatus();
  }

  function scheduleDecorate() {
    if (scheduled) return;
    scheduled = true;
    requestAnimationFrame(decorate);
  }

  new MutationObserver(scheduleDecorate).observe(content, {
    childList: true,
    subtree: true,
  });

  const dashboardStatus = document.getElementById("dashboardStatus");
  const espStatus = document.getElementById("espStatus");

  if (dashboardStatus) {
    new MutationObserver(scheduleDecorate).observe(dashboardStatus, {
      childList: true,
      subtree: true,
    });
  }

  if (espStatus) {
    new MutationObserver(scheduleDecorate).observe(espStatus, {
      childList: true,
      subtree: true,
    });
  }

  scheduleDecorate();
})();
