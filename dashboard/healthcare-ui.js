/*
 * ABS Healthcare Dashboard UI decorator
 *
 * Presentation only.
 * dashboard/app.js remains the owner of telemetry, WebSocket state,
 * AI results, control values and rendering cadence.
 */

(() => {
  "use strict";

  const content = document.getElementById("content");
  if (!content) return;

  const title = document.getElementById("healthPageTitle");
  const subtitle = document.getElementById("healthPageSubtitle");

  const pageCopy = {
    overview: [
      "Base Station Overview",
      "Live telecom control and system health",
    ],
    telemetry: [
      "Live Telemetry",
      "Engineering signals feeding the temporal AI",
    ],
    ai: [
      "AI Intelligence",
      "Temporal diagnosis, anomaly detection and trust",
    ],
    control: [
      "Control Chain",
      "Pico validation, ESP32 guardrails and actuation",
    ],
    models: [
      "Model Performance",
      "Held-out validation and model context",
    ],
  };

  const statStyles = new Map([
    ["Battery State of Charge", ["accent-orange", "battery"]],
    ["Active Power Source", ["accent-coral", "power"]],
    ["Generator", ["accent-teal", "bolt"]],
    ["Traffic Load", ["accent-orange", "signal"]],
    ["Backhaul", ["accent-coral", "network"]],
    ["Managed Site Power", ["accent-teal", "gauge"]],
  ]);

  const icons = {
    battery:
      '<rect x="5" y="7" width="12" height="10" rx="2"></rect><path d="M17 10h2v4h-2M8 10v4M11 10v4M14 10v4"></path>',
    power:
      '<path d="M8 7l4 4 4-4M12 11v8M5 4v6a3 3 0 0 0 3 3h1M19 4v6a3 3 0 0 1-3 3h-1"></path>',
    bolt:
      '<path d="M13 2 6 13h6l-1 9 7-12h-6z"></path>',
    signal:
      '<path d="M5 18v-3M9 18v-6M13 18V9M17 18V6"></path>',
    network:
      '<rect x="4" y="12" width="16" height="7" rx="2"></rect><path d="M8 15h.01M12 15h.01M15 12V9M9 9a6 6 0 0 1 6 0M7 6a9 9 0 0 1 10 0"></path>',
    gauge:
      '<path d="M5 17a8 8 0 1 1 14 0"></path><path d="M12 13l4-4"></path><circle cx="12" cy="13" r="1.4"></circle>',
  };

  function makeSvg(name) {
    const svg = document.createElementNS(
      "http://www.w3.org/2000/svg",
      "svg",
    );

    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("aria-hidden", "true");
    svg.innerHTML = icons[name] || icons.gauge;
    return svg;
  }

  function waveSvg() {
    const holder = document.createElement("div");
    holder.className = "health-card-wave";
    holder.innerHTML = `
      <svg viewBox="0 0 120 34" preserveAspectRatio="none" aria-hidden="true">
        <path
          class="wave-fill"
          fill="currentColor"
          d="M0 26 C14 17 20 18 28 21 S46 27 53 14 S66 5 75 13 S92 29 120 18 L120 34 L0 34 Z"
        ></path>
        <path
          stroke="currentColor"
          d="M0 26 C14 17 20 18 28 21 S46 27 53 14 S66 5 75 13 S92 29 120 18"
        ></path>
      </svg>
    `;
    return holder;
  }

  function decorateStats() {
    for (const card of content.querySelectorAll(".stat-card")) {
      if (card.querySelector(":scope > .health-stat-head")) continue;

      const label = card.querySelector(":scope > .stat-label");
      if (!label) continue;

      const [accent, iconName] =
        statStyles.get(label.textContent.trim()) ||
        ["accent-orange", "gauge"];

      card.classList.add(accent);

      const head = document.createElement("div");
      head.className = "health-stat-head";

      const icon = document.createElement("div");
      icon.className = "health-stat-icon";
      icon.append(makeSvg(iconName));

      label.before(head);
      head.append(icon, label);
      card.append(waveSvg());
    }
  }

  function updateHeaderFromActiveTab() {
    const active = document.querySelector(".tab.active");
    const key = active?.dataset?.tab || "overview";
    const copy = pageCopy[key] || pageCopy.overview;

    if (title) title.textContent = copy[0];
    if (subtitle) subtitle.textContent = copy[1];
  }

  function textOf(selector, fallback = "—") {
    const node = content.querySelector(selector);
    const value = node?.textContent?.trim();
    return value || fallback;
  }

  function statValue(label) {
    for (const card of content.querySelectorAll(".stat-card")) {
      const cardLabel =
        card.querySelector(".stat-label")?.textContent?.trim();

      if (cardLabel === label) {
        const value =
          card.querySelector(".stat-value")?.textContent?.trim() || "—";
        const unit =
          card.querySelector(".stat-unit")?.textContent?.trim() || "";

        return [value, unit].filter(Boolean).join(" ");
      }
    }

    return "—";
  }

  function statNote(label) {
    for (const card of content.querySelectorAll(".stat-card")) {
      const cardLabel =
        card.querySelector(".stat-label")?.textContent?.trim();

      if (cardLabel === label) {
        return (
          card.querySelector(".stat-note")?.textContent?.trim() || "—"
        );
      }
    }

    return "—";
  }

  function setText(id, value) {
    const node = document.getElementById(id);
    if (node && value && value !== "—") node.textContent = value;
  }

  function updateInsightRail() {
    const stateValues =
      content.querySelectorAll(
        ".current-state .state-big > div strong",
      );

    if (stateValues.length >= 2) {
      setText("railFault", stateValues[0].textContent.trim());
      setText("railMode", stateValues[1].textContent.trim());
    }

    const source = statValue("Active Power Source");
    const battery = statValue("Battery State of Charge");
    const traffic = statValue("Traffic Load");
    const generator = statValue("Generator");

    setText("railSource", source);
    setText("railBattery", battery);
    setText("railTraffic", traffic);
    setText("railGenerator", generator);

    const fault =
      document.getElementById("railFault")?.textContent?.trim() || "—";

    const tag = document.getElementById("railHealthTag");
    const marker = document.getElementById("railSpectrumMarker");

    if (!tag || !marker) return;

    const upper = fault.toUpperCase();

    if (upper === "NORMAL" || upper === "—") {
      tag.textContent =
        upper === "NORMAL" ? "System Healthy" : "Waiting for telemetry";
      tag.style.color = "#3e6750";
      tag.style.background = "#dff7e7";
      marker.style.left = "28%";
    } else if (upper.includes("LOCAL")) {
      tag.textContent = "Local Site Attention";
      tag.style.color = "#76551e";
      tag.style.background = "#f8debd";
      marker.style.left = "68%";
    } else {
      tag.textContent = "System Attention";
      tag.style.color = "#7a3f43";
      tag.style.background = "#fbf0f3";
      marker.style.left = "84%";
    }
  }

  let scheduled = false;

  function decorate() {
    scheduled = false;
    decorateStats();
    updateHeaderFromActiveTab();
    updateInsightRail();
  }

  function schedule() {
    if (scheduled) return;
    scheduled = true;
    requestAnimationFrame(decorate);
  }

  new MutationObserver(schedule).observe(content, {
    childList: true,
    subtree: true,
  });

  for (const button of document.querySelectorAll(".tab")) {
    button.addEventListener("click", () => {
      window.setTimeout(updateHeaderFromActiveTab, 0);
    });
  }

  schedule();
})();
