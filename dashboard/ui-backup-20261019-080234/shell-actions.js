"use strict";

(() => {
  const root = document.getElementById("app");
  const menu = document.getElementById("shellMenuDropdown");

  if (!root || !menu) return;

  const menuButtons = [...document.querySelectorAll(".shell-menu-btn")];
  const workspaceTabs = [...document.querySelectorAll(".workspace-tabs .tab")];
  const navButtons = [...document.querySelectorAll(".shell-nav")];

  let openMenu = null;
  let modal = null;
  let toastTimer = null;

  const definitions = {
    file: [
      ["Export live snapshot", "export-snapshot", "download"],
      ["Export telemetry only", "export-telemetry", "monitoring"],
      ["Export AI result only", "export-ai", "neurology"],
      ["Refresh dashboard", "refresh", "refresh"],
    ],
    view: [
      ["Overview", "tab:overview", "dashboard"],
      ["Telemetry", "tab:telemetry", "monitoring"],
      ["AI Intelligence", "tab:ai", "memory"],
      ["Control Chain", "tab:control", "shield"],
      ["Models", "tab:models", "query_stats"],
    ],
    system: [
      ["Live system status", "system-status", "fact_check"],
      ["BTS / Network telemetry", "tab:telemetry", "cell_tower"],
      ["Power + Guardrails", "tab:control", "bolt"],
      ["System overview", "tab:overview", "dashboard"],
    ],
    ai: [
      ["AI Intelligence", "tab:ai", "neurology"],
      ["Model performance", "tab:models", "query_stats"],
      ["Decision + Actuation", "tab:control", "account_tree"],
    ],
    help: [
      ["Controls", "help-controls", "help"],
      ["Architecture", "help-architecture", "schema"],
      ["About this dashboard", "about", "info"],
    ],
  };

  function activeTabName() {
    return document.querySelector(".workspace-tabs .tab.active")?.dataset?.tab || "overview";
  }

  function navigate(tabName) {
    const button = workspaceTabs.find((item) => item.dataset.tab === tabName);

    if (!button) {
      showToast(`Screen "${tabName}" is unavailable`);
      return;
    }

    button.click();
    closeMenu();
    updateNavigationState();
  }

  function updateNavigationState() {
    const active = activeTabName();

    for (const button of navButtons) {
      const selected = button.dataset.tabTarget === active;
      button.classList.toggle("active", selected);
      button.setAttribute("aria-pressed", selected ? "true" : "false");
    }
  }

  function closeMenu() {
    openMenu = null;
    menu.hidden = true;
    menu.replaceChildren();

    for (const button of menuButtons) {
      button.classList.remove("active");
      button.setAttribute("aria-expanded", "false");
    }
  }

  function openDropdown(name, owner) {
    if (openMenu === name) {
      closeMenu();
      return;
    }

    openMenu = name;
    menu.replaceChildren();

    const items = definitions[name] || [];

    for (const [label, action, icon] of items) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "shell-dropdown-item";

      const glyph = document.createElement("span");
      glyph.className = "material-symbols-rounded";
      glyph.textContent = icon;

      const text = document.createElement("span");
      text.textContent = label;

      button.append(glyph, text);
      button.addEventListener("click", () => execute(action));
      menu.append(button);
    }

    const menuRect = owner.getBoundingClientRect();
    const rootRect = root.getBoundingClientRect();

    menu.style.left = `${Math.max(4, menuRect.left - rootRect.left)}px`;
    menu.style.top = `${menuRect.bottom - rootRect.top}px`;
    menu.hidden = false;

    for (const button of menuButtons) {
      const selected = button === owner;
      button.classList.toggle("active", selected);
      button.setAttribute("aria-expanded", selected ? "true" : "false");
    }
  }

  function safeFileName(base) {
    const stamp = new Date().toISOString().replaceAll(":", "-").replaceAll(".", "-");
    return `${base}-${stamp}.json`;
  }

  async function getState() {
    const response = await fetch("/api/state", { cache: "no-store" });

    if (!response.ok) {
      throw new Error(`Dashboard state request failed: HTTP ${response.status}`);
    }

    return response.json();
  }

  function downloadJson(fileName, value) {
    const blob = new Blob([JSON.stringify(value, null, 2)], {
      type: "application/json",
    });

    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = fileName;
    document.body.append(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
  }

  async function exportPart(kind) {
    closeMenu();

    try {
      const state = await getState();

      if (kind === "snapshot") {
        downloadJson(safeFileName("abs-live-snapshot"), state);
        showToast("Live snapshot exported");
      }

      if (kind === "telemetry") {
        downloadJson(
          safeFileName("abs-live-telemetry"),
          {
            serverTimeMs: state.serverTimeMs,
            telemetry: state.telemetry,
            meta: state.meta,
          },
        );
        showToast("Telemetry exported");
      }

      if (kind === "ai") {
        downloadJson(
          safeFileName("abs-ai-result"),
          {
            serverTimeMs: state.serverTimeMs,
            ai: state.ai,
            meta: state.meta,
          },
        );
        showToast("AI result exported");
      }
    } catch (error) {
      showToast(error?.message || "Export failed", true);
    }
  }

  function showToast(message, isError = false) {
    let toast = document.getElementById("shellToast");

    if (!toast) {
      toast = document.createElement("div");
      toast.id = "shellToast";
      toast.className = "shell-toast";
      root.append(toast);
    }

    toast.textContent = message;
    toast.classList.toggle("error", Boolean(isError));
    toast.classList.add("visible");

    window.clearTimeout(toastTimer);
    toastTimer = window.setTimeout(() => {
      toast.classList.remove("visible");
    }, 2200);
  }

  function closeModal() {
    modal?.remove();
    modal = null;
  }

  function modalShell(title, body) {
    closeModal();

    modal = document.createElement("div");
    modal.className = "shell-modal-backdrop";

    const panel = document.createElement("section");
    panel.className = "shell-modal";
    panel.setAttribute("role", "dialog");
    panel.setAttribute("aria-modal", "true");
    panel.setAttribute("aria-label", title);

    const header = document.createElement("header");
    const heading = document.createElement("strong");
    heading.textContent = title;

    const close = document.createElement("button");
    close.type = "button";
    close.className = "shell-modal-close material-symbols-rounded";
    close.setAttribute("aria-label", "Close");
    close.textContent = "close";
    close.addEventListener("click", closeModal);

    header.append(heading, close);

    const content = document.createElement("div");
    content.className = "shell-modal-content";

    if (typeof body === "string") {
      const p = document.createElement("p");
      p.textContent = body;
      content.append(p);
    } else {
      content.append(body);
    }

    panel.append(header, content);
    modal.append(panel);
    root.append(modal);

    modal.addEventListener("click", (event) => {
      if (event.target === modal) closeModal();
    });

    close.focus();
  }

  function makeRows(rows) {
    const list = document.createElement("div");
    list.className = "shell-status-list";

    for (const [label, value, tone] of rows) {
      const row = document.createElement("div");

      const key = document.createElement("span");
      key.textContent = label;

      const val = document.createElement("strong");
      val.textContent = value ?? "—";

      if (tone) val.dataset.tone = tone;

      row.append(key, val);
      list.append(row);
    }

    return list;
  }

  async function showSystemStatus() {
    closeMenu();

    try {
      const state = await getState();
      const t = state.telemetry || {};
      const ai = state.ai || {};
      const meta = state.meta || {};
      const runtime = ai.runtime || {};
      const cmd = runtime.ai_command || {};

      const rows = [
        ["Dashboard", "LIVE", "good"],
        ["ESP32 telemetry", meta.telemetryStatus || "UNKNOWN", String(meta.telemetryStatus || "").toLowerCase()],
        ["BTS source", t.network_input_source || t.network_source || "—", ""],
        ["Fault domain", ai.fault_domain?.label || t.fault_label || "—", ""],
        ["Operating mode", t.operating_mode || "—", ""],
        ["Laptop / Pico", cmd.dual_ai_agreement || "—", ""],
        ["Guardrail", t.guardrail_status || "—", ""],
        ["Generator", t.generator_feedback_running ? "RUNNING" : "STOPPED", ""],
      ];

      modalShell("Live System Status", makeRows(rows));
    } catch (error) {
      showToast(error?.message || "Could not load system status", true);
    }
  }

  function showHelp(kind) {
    closeMenu();

    if (kind === "controls") {
      const wrap = document.createElement("div");
      wrap.className = "shell-help-grid";
      wrap.innerHTML = `
        <div><span class="material-symbols-rounded">mouse</span><strong>3D models</strong><p>Drag a model to orbit it, use the wheel to zoom, and use its reset-view button to recenter it.</p></div>
        <div><span class="material-symbols-rounded">tab</span><strong>Workspaces</strong><p>Overview, Telemetry, AI Intelligence, Control Chain, and Models are live dashboard screens.</p></div>
        <div><span class="material-symbols-rounded">download</span><strong>Exports</strong><p>Use File to export a complete live snapshot, telemetry only, or the latest AI result.</p></div>
        <div><span class="material-symbols-rounded">shield</span><strong>Authority</strong><p>Laptop AI recommends, Pico validates, and ESP32 deterministic guardrails remain final authority.</p></div>
      `;
      modalShell("Dashboard Controls", wrap);
    }

    if (kind === "architecture") {
      const wrap = document.createElement("div");
      wrap.className = "shell-architecture";
      wrap.innerHTML = `
        <div><b>Phones</b><small>Mobile clients</small></div><i>→</i>
        <div><b>BTS-001</b><small>:8100 / :4010</small></div><i>→</i>
        <div><b>Main ESP32</b><small>:4001</small></div><i>→</i>
        <div><b>Laptop AI</b><small>Temporal models</small></div><i>→</i>
        <div><b>Pico</b><small>:4000</small></div><i>→</i>
        <div><b>ESP32</b><small>Final guardrails</small></div>
      `;
      modalShell("System Architecture", wrap);
    }

    if (kind === "about") {
      modalShell(
        "About ABS Control Center",
        "Read-only live monitoring UI for BTS telemetry, temporal AI, Pico validation, power decisions, and ESP32 deterministic guardrails.",
      );
    }
  }

  function execute(action) {
    if (action.startsWith("tab:")) {
      navigate(action.slice(4));
      return;
    }

    if (action === "export-snapshot") {
      void exportPart("snapshot");
      return;
    }

    if (action === "export-telemetry") {
      void exportPart("telemetry");
      return;
    }

    if (action === "export-ai") {
      void exportPart("ai");
      return;
    }

    if (action === "refresh") {
      closeMenu();
      window.location.reload();
      return;
    }

    if (action === "system-status") {
      void showSystemStatus();
      return;
    }

    if (action === "help-controls") {
      showHelp("controls");
      return;
    }

    if (action === "help-architecture") {
      showHelp("architecture");
      return;
    }

    if (action === "about") {
      showHelp("about");
    }
  }

  for (const button of menuButtons) {
    button.setAttribute("aria-haspopup", "menu");
    button.setAttribute("aria-expanded", "false");

    button.addEventListener("click", (event) => {
      event.stopPropagation();
      openDropdown(button.dataset.menu, button);
    });
  }

  for (const button of navButtons) {
    button.addEventListener("click", () => {
      navigate(button.dataset.tabTarget);
    });
  }

  for (const tab of workspaceTabs) {
    tab.addEventListener("click", () => {
      window.setTimeout(updateNavigationState, 0);
    });
  }

  document.addEventListener("click", (event) => {
    if (!menu.hidden && !menu.contains(event.target)) {
      closeMenu();
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      closeMenu();
      closeModal();
    }
  });

  updateNavigationState();
})();
