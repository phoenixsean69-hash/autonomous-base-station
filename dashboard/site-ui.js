
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

const COLORS = {
  ground: 0x24292c,
  steel: 0x535b61,
  dark: 0x282d30,
  darker: 0x1f2325,
  teal: 0x3f7f8d,
  tealGlow: 0x58a7b5,
  amber: 0xa86d2f,
  amberGlow: 0xd18b37,
  green: 0x5f9f80,
  red: 0xaa554b,
  cream: 0xb8b1a3,
};

let view = null;

// ABS SITE 3D MOBILE PRESENCE V1
const DEMO_MOBILE_SUBSCRIBERS = [
  "0712000001",
  "0712000002",
  "0712000003",
  "0712000004",
  "0712000005",
  "0712000006",
];

function normalizeMobileNumber(value) {
  return String(value ?? "").replace(/\D/g, "");
}

function formatMobileNumber(value) {
  const n = normalizeMobileNumber(value);
  if (n.length !== 10) return n || "—";
  return `${n.slice(0,4)} ${n.slice(4,7)} ${n.slice(7)}`;
}

function shortMobileNumber(value) {
  const n = normalizeMobileNumber(value);
  return n.length >= 3 ? n.slice(-3) : n || "—";
}

function dashboardState() {
  return window.ABSDashboardGetState?.() ?? {};
}

function activeOverview() {
  return window.ABSDashboardGetActiveTab?.() === "overview";
}

function clamp(v, min, max) {
  return Math.max(min, Math.min(max, Number(v) || 0));
}

function mat(color, opts = {}) {
  return new THREE.MeshStandardMaterial({
    color,
    roughness: opts.roughness ?? 0.52,
    metalness: opts.metalness ?? 0.48,
    emissive: opts.emissive ?? 0x000000,
    emissiveIntensity: opts.emissiveIntensity ?? 0,
    transparent: opts.transparent ?? false,
    opacity: opts.opacity ?? 1,
  });
}

function box(group, size, pos, color, opts = {}) {
  const mesh = new THREE.Mesh(
    new THREE.BoxGeometry(...size),
    mat(color, opts),
  );
  mesh.position.set(...pos);
  group.add(mesh);
  return mesh;
}

function cyl(group, radius, height, pos, color, rotation = [0, 0, 0], opts = {}) {
  const mesh = new THREE.Mesh(
    new THREE.CylinderGeometry(radius, radius, height, 20),
    mat(color, opts),
  );
  mesh.position.set(...pos);
  mesh.rotation.set(...rotation);
  group.add(mesh);
  return mesh;
}

function tube(group, points, color, radius = 0.055, emissive = false) {
  const curve = new THREE.CatmullRomCurve3(
    points.map((p) => new THREE.Vector3(...p)),
  );

  const mesh = new THREE.Mesh(
    new THREE.TubeGeometry(curve, 48, radius, 8, false),
    mat(color, {
      metalness: 0.25,
      roughness: 0.32,
      emissive: emissive ? color : 0x000000,
      emissiveIntensity: emissive ? 0.72 : 0,
    }),
  );

  group.add(mesh);
  return { mesh, curve };
}

function platform(root) {
  box(root, [13.6, 0.3, 9.4], [0, -0.18, 0], 0x202427, {
    roughness: 0.78,
    metalness: 0.12,
  });

  box(root, [12.9, 0.12, 8.7], [0, 0.02, 0], 0x30373b, {
    roughness: 0.72,
    metalness: 0.16,
  });

  const grid = new THREE.GridHelper(13, 26, 0x374047, 0x2b3135);
  grid.position.y = 0.1;
  grid.material.transparent = true;
  grid.material.opacity = 0.35;
  root.add(grid);

  const fence = new THREE.Group();
  root.add(fence);

  const postMat = mat(0x4c545a, { roughness: 0.7, metalness: 0.5 });
  const railMat = mat(0x3c4449, { roughness: 0.72, metalness: 0.45 });

  function post(x, z) {
    const m = new THREE.Mesh(
      new THREE.BoxGeometry(0.07, 1.05, 0.07),
      postMat,
    );
    m.position.set(x, 0.58, z);
    fence.add(m);
  }

  function rail(x1, z1, x2, z2) {
    const dx = x2 - x1;
    const dz = z2 - z1;
    const len = Math.hypot(dx, dz);

    const g = new THREE.Mesh(
      new THREE.BoxGeometry(len, 0.045, 0.045),
      railMat,
    );
    g.position.set((x1 + x2) / 2, 0.75, (z1 + z2) / 2);
    g.rotation.y = -Math.atan2(dz, dx);
    fence.add(g);

    const g2 = g.clone();
    g2.position.y = 0.42;
    fence.add(g2);
  }

  for (let x = -6.2; x <= 6.2; x += 1.55) {
    post(x, -4.1);
    post(x, 4.1);
  }
  for (let z = -4.1; z <= 4.1; z += 1.37) {
    post(-6.2, z);
    post(6.2, z);
  }

  rail(-6.2, -4.1, 6.2, -4.1);
  rail(-6.2, 4.1, 6.2, 4.1);
  rail(-6.2, -4.1, -6.2, 4.1);
  rail(6.2, -4.1, 6.2, 4.1);
}

function buildBattery(root, pos, state) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "battery";
  root.add(g);

  box(g, [2.5, 0.16, 1.65], [0, 0.05, 0], 0x252a2d);
  box(g, [2.05, 1.2, 1.2], [0, 0.72, 0], 0x30363a);
  box(g, [2.12, 0.12, 1.26], [0, 1.35, 0], 0x4a5258);

  cyl(g, 0.12, 0.22, [-0.62, 1.55, 0], 0xb64238);
  cyl(g, 0.12, 0.22, [0.62, 1.55, 0], 0x767d82);

  const soc = clamp(state?.telemetry?.battery_soc_pct ?? 0, 0, 100);
  const strip = box(g, [0.05, 0.8 * (soc / 100), 0.05], [-0.98, 0.68, 0.62], COLORS.green, {
    emissive: COLORS.green,
    emissiveIntensity: 0.6,
  });
  strip.position.y = 0.34 + 0.4 * (soc / 100);

  return g;
}

function buildGenerator(root, pos, state) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "generator";
  root.add(g);

  box(g, [2.8, 0.16, 1.65], [0, 0.05, 0], 0x252a2d);
  box(g, [2.2, 1.25, 1.25], [0, 0.76, 0], 0x343a3e);

  for (let i = -5; i <= 5; i += 1) {
    box(g, [0.045, 0.68, 1.28], [-0.65 + i * 0.12, 0.72, 0], 0x202427);
  }

  box(g, [0.52, 0.38, 0.06], [0.67, 0.7, 0.66], 0x59656d);

  cyl(g, 0.13, 0.24, [-0.55, 1.52, 0], 0x1f2325);
  cyl(g, 0.13, 0.24, [0.55, 1.52, 0], 0x1f2325);

  const running = Boolean(
    state?.telemetry?.generator_feedback_running ??
    state?.telemetry?.generator_running,
  );

  box(g, [0.05, 0.9, 0.05], [-1.05, 0.82, 0.66], running ? COLORS.green : COLORS.amber, {
    emissive: running ? COLORS.green : COLORS.amber,
    emissiveIntensity: 0.55,
  });

  return g;
}

function buildTransformer(root, pos, state) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "grid";
  root.add(g);

  box(g, [2.8, 0.16, 1.8], [0, 0.05, 0], 0x252a2d);
  box(g, [1.8, 1.45, 1.1], [0, 0.86, 0], 0x41484d);

  for (let i = -5; i <= 5; i += 1) {
    box(g, [0.04, 1.05, 1.15], [-0.72 + i * 0.14, 0.86, 0], 0x24292c);
  }

  const up = Boolean(state?.telemetry?.grid_available);
  for (const x of [-0.65, 0, 0.65]) {
    cyl(g, 0.09, 0.9, [x, 2.0, 0], 0x2f3437);
    cyl(g, 0.14, 0.13, [x, 2.42, 0], up ? COLORS.green : COLORS.red, [0, 0, 0], {
      emissive: up ? COLORS.green : COLORS.red,
      emissiveIntensity: 0.48,
    });
  }

  return g;
}

function buildPowerController(root, pos, state) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "power";
  root.add(g);

  box(g, [2.2, 0.16, 1.7], [0, 0.05, 0], 0x252a2d);
  box(g, [1.7, 1.55, 1.15], [0, 0.88, 0], 0x343a3e);

  box(g, [0.78, 0.38, 0.05], [-0.18, 0.92, 0.59], 0x253139);
  box(g, [0.65, 0.18, 0.055], [-0.18, 0.93, 0.63], COLORS.teal, {
    emissive: COLORS.teal,
    emissiveIntensity: 0.45,
  });

  for (const x of [-0.45, 0, 0.45]) {
    cyl(g, 0.08, 0.5, [x, 1.92, 0], 0x4d555a);
  }

  const kw = clamp(state?.telemetry?.managed_power_kw ?? 0, 0, 2);
  box(g, [0.08, 0.7 * clamp(kw / 1.0, 0.05, 1), 0.05], [0.77, 0.64, 0.59], COLORS.amber, {
    emissive: COLORS.amber,
    emissiveIntensity: 0.55,
  });

  return g;
}

function buildServerCabinet(root, pos) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "server";
  root.add(g);

  box(g, [2.25, 0.16, 1.7], [0, 0.05, 0], 0x252a2d);
  box(g, [1.8, 1.8, 1.18], [0, 1.0, 0], 0x2f3539);

  for (let y = 0.45; y <= 1.55; y += 0.23) {
    box(g, [1.25, 0.12, 0.05], [0, y, 0.61], 0x1d2326);
    box(g, [0.55, 0.035, 0.058], [0.1, y, 0.64], COLORS.teal, {
      emissive: COLORS.teal,
      emissiveIntensity: 0.45,
    });
  }

  return g;
}

function buildMast(root, pos, state) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "mast";
  root.add(g);

  box(g, [2.0, 0.16, 1.65], [0, 0.05, 0], 0x252a2d);
  cyl(g, 0.1, 4.7, [0, 2.42, 0], 0x4c545a);

  for (let y = 0.6; y <= 4.2; y += 0.48) {
    box(g, [0.95, 0.045, 0.045], [0, y, 0], 0x5b6369);
  }

  for (const [x, z] of [[-0.35,0.05],[0.35,0.05],[-0.35,-0.05],[0.35,-0.05]]) {
    box(g, [0.18, 1.25, 0.12], [x, 3.42, z], 0xb9bdba);
  }

  const traffic = clamp(state?.telemetry?.traffic_load_pct ?? 0, 0, 100);
  const ringCount = Math.max(1, Math.round(traffic / 35));

  for (let i = 0; i < ringCount; i += 1) {
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(0.58 + i * 0.18, 0.018, 8, 40, Math.PI * 1.2),
      mat(COLORS.amber, {
        emissive: COLORS.amber,
        emissiveIntensity: 0.45,
        metalness: 0.1,
      }),
    );
    ring.position.set(0, 3.35, 0);
    ring.rotation.x = Math.PI / 2;
    ring.rotation.z = -Math.PI * 0.1;
    g.add(ring);
  }

  return g;
}

function buildBackhaul(root, pos, state) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "backhaul";
  root.add(g);

  box(g, [2.8, 0.16, 1.65], [0, 0.05, 0], 0x252a2d);

  function dish(x, facing) {
    cyl(g, 0.055, 1.5, [x, 0.82, 0], 0x5a6267);

    const d = new THREE.Mesh(
      new THREE.CylinderGeometry(0.65, 0.14, 0.16, 28),
      mat(0x817462, { metalness: 0.3, roughness: 0.62 }),
    );
    d.position.set(x, 1.42, 0);
    d.rotation.z = Math.PI / 2;
    d.rotation.y = facing;
    g.add(d);
  }

  dish(-0.8, 0);
  dish(0.8, Math.PI);

  const healthy =
    String(state?.telemetry?.backhaul_status ?? "").toUpperCase() !== "CRITICAL";

  tube(
    g,
    [[-0.25,1.42,0],[0,1.5,0],[0.25,1.42,0]],
    healthy ? COLORS.tealGlow : COLORS.red,
    0.024,
    true,
  );

  return g;
}

function addRoutes(root) {
  const flows = [];

  function power(points) {
    const x = tube(root, points, COLORS.amberGlow, 0.055, true);
    flows.push({ curve: x.curve, color: COLORS.amberGlow, speed: 0.075 });
  }

  function data(points) {
    const x = tube(root, points, COLORS.tealGlow, 0.045, true);
    flows.push({ curve: x.curve, color: COLORS.tealGlow, speed: 0.09 });
  }

  // Power: grid -> controller -> generator/battery/load/mast.
  power([[-1.9,0.35,-2.8],[-1.9,0.35,-1.8],[-0.5,0.35,-1.0],[0,0.35,0]]);
  power([[-4.0,0.35,2.4],[-3.2,0.35,1.4],[-1.8,0.35,0.7],[0,0.35,0]]);
  power([[-4.2,0.35,-1.8],[-3.0,0.35,-1.0],[-1.6,0.35,-0.4],[0,0.35,0]]);
  power([[0,0.35,0],[1.7,0.35,0.5],[3.1,0.35,1.7],[4.2,0.35,2.3]]);
  power([[0,0.35,0],[1.8,0.35,-0.8],[3.0,0.35,-1.7]]);

  // Data: backhaul -> controller -> server -> mast.
  data([[-4.3,0.55,-3.0],[-2.8,0.55,-2.2],[-1.2,0.55,-1.0],[0,0.55,0]]);
  data([[0,0.55,0],[1.2,0.55,-0.2],[2.7,0.55,-1.2],[3.5,0.55,-1.7]]);
  data([[0,0.55,0],[1.5,0.55,0.8],[3.2,0.55,1.9],[4.4,0.55,2.3]]);

  const pulses = [];

  for (const flow of flows) {
    for (let i = 0; i < 3; i += 1) {
      const p = new THREE.Mesh(
        new THREE.SphereGeometry(0.075, 12, 12),
        mat(flow.color, {
          emissive: flow.color,
          emissiveIntensity: 1.1,
          metalness: 0,
          roughness: 0.25,
        }),
      );
      root.add(p);
      pulses.push({
        mesh: p,
        curve: flow.curve,
        offset: i / 3,
        speed: flow.speed,
      });
    }
  }

  return pulses;
}


/* ==========================================================================
   ABS SITE 3D — TWO-STATION LIVE DIGITAL TWIN V7
   BTS-001 + Main Autonomous Base Station
   ========================================================================== */

const TWIN_V7_INFO = {
  btsRadio: {
    title: "BTS Radio + RF Plant",
    type: "BTS radio equipment",
    icon: "cell_tower",
    role: "Radio access, RF power/amplification and BTS environmental state",
  },
  btsService: {
    title: "BTS Service Cabinet",
    type: "BTS network/service node",
    icon: "router",
    role: "Routes calls/SMS and BTS telemetry toward the autonomous base station",
  },
  btsBackhaul: {
    title: "BTS Backhaul Terminal",
    type: "Microwave/backhaul terminal",
    icon: "settings_input_antenna",
    role: "BTS-side transport link toward the main autonomous base station",
  },
  mainBackhaul: {
    title: "Main Backhaul Terminal",
    type: "Backhaul receiver",
    icon: "settings_input_antenna",
    role: "Receives BTS traffic and telemetry at the main autonomous base station",
  },
  mainControl: {
    title: "Main ESP32 Control Cabinet",
    type: "Main station controller",
    icon: "developer_board",
    role: "Receives telemetry, applies deterministic guardrails and controls final actuation",
  },
  aiPico: {
    title: "AI + Pico Decision Cabinet",
    type: "AI and embedded decision equipment",
    icon: "neurology",
    role: "Laptop temporal AI inference plus Raspberry Pi Pico independent decision validation",
  },
  rectifier: {
    title: "Rectifier + DC Power Plant",
    type: "Managed DC power equipment",
    icon: "electric_bolt",
    role: "Combines grid, generator and battery sources into managed site power",
  },
  siteLoad: {
    title: "Managed Site Load",
    type: "Telecommunications site load",
    icon: "dns",
    role: "Represents the controlled load served by the autonomous station",
  },
};

// V7.2 TDZ FIX: do not access SITE_COMPONENT_INFO before its const declaration.

function twinV7Rows(component) {
  const s = dashboardState();
  const t = s.telemetry ?? {};
  const ai = s.ai ?? {};
  const cmd = ai?.runtime?.ai_command ?? {};
  const pico = ai?.runtime?.pico_decision ?? {};

  const rows = {
    btsRadio: [
      ["Radio", t.radio_operational === true ? "OPERATIONAL" : "FAULT / UNKNOWN"],
      ["RSSI", `${Number(t.rssi_dbm ?? 0).toFixed(1)} dBm`],
      ["Traffic", `${Number(t.traffic_load_pct ?? 0).toFixed(1)}%`],
      ["Active calls", `${Number(t.network_active_calls ?? 0)}`],
      ["RF forward", `${Number(t.rf_forward_w ?? 0).toFixed(1)} W`],
      ["RF reflected", `${Number(t.rf_reflected_w ?? 0).toFixed(1)} W`],
    ],
    btsService: [
      ["Service", ":8100"],
      ["Input source", t.network_input_source ?? "—"],
      ["Physical link", t.physical_link_up === true ? "UP" : "DOWN / UNKNOWN"],
      ["Upstream", t.upstream_reachable === true ? "REACHABLE" : "DOWN / UNKNOWN"],
      ["Traffic", `${Number(t.traffic_load_pct ?? 0).toFixed(1)}%`],
      ["Calls", `${Number(t.network_active_calls ?? 0)}`],
    ],
    btsBackhaul: [
      ["Backhaul", t.backhaul_status ?? "—"],
      ["Latency", `${Number(t.latency_ms ?? 0).toFixed(1)} ms`],
      ["Packet loss", `${Number(t.packet_loss_pct ?? 0).toFixed(2)}%`],
      ["Upstream", t.upstream_reachable === true ? "REACHABLE" : "DOWN / UNKNOWN"],
      ["Source", t.network_input_source ?? "—"],
    ],
    mainBackhaul: [
      ["Backhaul", t.backhaul_status ?? "—"],
      ["Main input", t.network_input_source ?? "—"],
      ["Latency", `${Number(t.latency_ms ?? 0).toFixed(1)} ms`],
      ["Packet loss", `${Number(t.packet_loss_pct ?? 0).toFixed(2)}%`],
      ["Physical link", t.physical_link_up === true ? "UP" : "DOWN / UNKNOWN"],
    ],
    mainControl: [
      ["ESP32 endpoint", ":4001"],
      ["Telemetry", s.meta?.telemetryStatus ?? "—"],
      ["Final mode", t.operating_mode ?? "—"],
      ["Guardrails", t.guardrail_status ?? "—"],
      ["Power source", t.active_power_source ?? "—"],
    ],
    aiPico: [
      ["Laptop AI", ai?.fault_domain?.label ?? "WAITING"],
      ["Confidence", ai?.fault_domain?.confidence == null ? "—" : `${(Number(ai.fault_domain.confidence) * 100).toFixed(1)}%`],
      ["AI trust", ai?.trust?.decision ?? "—"],
      ["Pico endpoint", ":4000"],
      ["Pico domain", cmd.pico_ai_fault_domain ?? "WAITING"],
      ["Dual AI", cmd.dual_ai_agreement ?? "WAITING"],
      ["Pico mode", pico.mode_decision ?? "WAITING"],
    ],
    rectifier: [
      ["Active source", t.active_power_source ?? "—"],
      ["Grid", t.grid_available === true ? "AVAILABLE" : "FAILED / UNKNOWN"],
      ["Generator", (t.generator_feedback_running ?? t.generator_running) ? "RUNNING" : "STOPPED"],
      ["DC voltage", `${Number(t.dc_voltage_v ?? 0).toFixed(2)} V`],
      ["Managed power", `${Number(t.managed_power_kw ?? 0).toFixed(3)} kW`],
      ["Mode", t.operating_mode ?? "—"],
    ],
    siteLoad: [
      ["Traffic", `${Number(t.traffic_load_pct ?? 0).toFixed(1)}%`],
      ["Managed power", `${Number(t.managed_power_kw ?? 0).toFixed(3)} kW`],
      ["Operating mode", t.operating_mode ?? "—"],
      ["Active calls", `${Number(t.network_active_calls ?? 0)}`],
    ],
  };

  return rows[component] ?? componentRows(component);
}

function twinStatus(name, state) {
  const t = state?.telemetry ?? {};
  const ai = state?.ai ?? {};
  const cmd = ai?.runtime?.ai_command ?? {};
  const pico = ai?.runtime?.pico_decision ?? {};
  const backhaul = String(t.backhaul_status ?? "").toUpperCase();
  const source = String(t.network_input_source ?? "").toUpperCase();
  const telemetry = String(state?.meta?.telemetryStatus ?? "OFFLINE").toUpperCase();
  const agreement = String(cmd.dual_ai_agreement ?? "").toUpperCase();

  if (name === "radio") {
    return t.radio_operational === true ? "good" : t.radio_operational === false ? "down" : "warn";
  }

  if (name === "backhaul") {
    if (
      t.physical_link_up === false ||
      t.upstream_reachable === false ||
      backhaul === "CRITICAL"
    ) return "down";

    if (
      backhaul === "DEGRADED" ||
      (source && source !== "BTS_BACKHAUL")
    ) return "warn";

    return source === "BTS_BACKHAUL" ? "good" : "warn";
  }

  if (name === "telemetry") {
    if (telemetry === "LIVE") return "good";
    if (telemetry === "STALE") return "warn";
    return "down";
  }

  if (name === "ai") {
    if (!state?.ai) return "down";
    return state?.meta?.aiFresh === true ? "good" : "warn";
  }

  if (name === "dual") {
    if (!cmd.pico_ai_fault_domain && !pico.mode_decision) return "down";
    if (agreement === "AGREE") return "good";
    if (agreement === "DISAGREE") return "warn";
    return "warn";
  }

  if (name === "grid") {
    return t.grid_available === true ? "good" : "down";
  }

  if (name === "generator") {
    return (t.generator_feedback_running ?? t.generator_running) ? "good" : "idle";
  }

  if (name === "battery") {
    const soc = Number(t.battery_soc_pct ?? 0);
    if (soc <= 25) return "down";
    if (soc <= 40) return "warn";
    return "good";
  }

  if (name === "power") {
    return String(t.active_power_source ?? "").toUpperCase() === "NO POWER" ? "down" : "good";
  }

  return "idle";
}

function twinColor(status, kind = "data") {
  if (status === "down") return COLORS.red;
  if (status === "warn") return 0xb58c4d;
  if (status === "idle") return 0x62696d;
  return kind === "power" ? COLORS.amberGlow : COLORS.tealGlow;
}

function setMaterialColor(material, color, emissive = true) {
  if (!material) return;
  material.color?.setHex(color);
  if (material.emissive) {
    material.emissive.setHex(emissive ? color : 0x000000);
  }
}

function addTwinBeacon(group, position = [0, 1.7, 0]) {
  const beacon = new THREE.Mesh(
    new THREE.SphereGeometry(0.085, 14, 14),
    mat(COLORS.green, {
      emissive: COLORS.green,
      emissiveIntensity: 0.9,
      metalness: 0.05,
      roughness: 0.28,
    }),
  );
  beacon.position.set(...position);
  group.add(beacon);
  group.userData.beacon = beacon;
  return beacon;
}

function setTwinBeacon(group, status) {
  const beacon = group?.userData?.beacon;
  if (!beacon) return;
  const color = twinColor(status, "data");
  setMaterialColor(beacon.material, color, true);
  beacon.material.emissiveIntensity = status === "idle" ? 0.25 : 0.9;
}

function makeTwinLabel(text, width = 420) {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = 74;

  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "rgba(31,34,36,.88)";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = "rgba(255,255,255,.16)";
  ctx.strokeRect(1, 1, canvas.width - 2, canvas.height - 2);
  ctx.fillStyle = "#d7d7d7";
  ctx.font = "500 25px Arial, sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, canvas.width / 2, canvas.height / 2);

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;

  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({
      map: texture,
      transparent: true,
      depthTest: true,
    }),
  );
  sprite.scale.set(3.5, 0.62, 1);
  return sprite;
}

function buildTwinPad(root, centerX, width, depth, label) {
  const pad = new THREE.Group();
  pad.position.set(centerX, 0, 0);
  root.add(pad);

  box(pad, [width, 0.30, depth], [0, -0.18, 0], 0x202427, {
    roughness: 0.8,
    metalness: 0.12,
  });

  box(pad, [width - 0.32, 0.11, depth - 0.32], [0, 0.02, 0], 0x30373b, {
    roughness: 0.72,
    metalness: 0.16,
  });

  const grid = new THREE.GridHelper(Math.max(width, depth), 14, 0x56646c, 0x3a444a);
  grid.scale.x = width / Math.max(width, depth);
  grid.scale.z = depth / Math.max(width, depth);
  grid.position.y = 0.085;
  grid.material.transparent = true;
  grid.material.opacity = 0.32;
  pad.add(grid);

  const fence = new THREE.Group();
  pad.add(fence);

  function post(x, z) {
    box(fence, [0.065, 0.92, 0.065], [x, 0.50, z], 0x515a60);
  }

  function rail(x1, z1, x2, z2, y) {
    const dx = x2 - x1;
    const dz = z2 - z1;
    const len = Math.hypot(dx, dz);
    const r = box(fence, [len, 0.035, 0.035], [(x1+x2)/2, y, (z1+z2)/2], 0x465056);
    r.rotation.y = -Math.atan2(dz, dx);
  }

  const x0 = -width / 2 + 0.2;
  const x1 = width / 2 - 0.2;
  const z0 = -depth / 2 + 0.2;
  const z1 = depth / 2 - 0.2;

  for (let x = x0; x <= x1 + 0.01; x += 1.35) {
    post(x, z0);
    post(x, z1);
  }

  for (let z = z0; z <= z1 + 0.01; z += 1.35) {
    post(x0, z);
    post(x1, z);
  }

  rail(x0, z0, x1, z0, 0.42);
  rail(x0, z0, x1, z0, 0.72);
  rail(x0, z1, x1, z1, 0.42);
  rail(x0, z1, x1, z1, 0.72);
  rail(x0, z0, x0, z1, 0.42);
  rail(x0, z0, x0, z1, 0.72);
  rail(x1, z0, x1, z1, 0.42);
  rail(x1, z0, x1, z1, 0.72);

  const sign = makeTwinLabel(label);
  sign.position.set(0, 2.55, -depth / 2 + 0.55);
  pad.add(sign);

  return pad;
}

function buildBtsRadioCabinet(root, pos) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "btsRadio";
  root.add(g);

  box(g, [2.0, 0.16, 1.45], [0, 0.05, 0], 0x252a2d);
  box(g, [1.55, 1.35, 1.05], [0, 0.78, 0], 0x343a3e);

  for (let i = -5; i <= 5; i += 1) {
    box(g, [0.035, 0.86, 1.08], [-0.55 + i * 0.11, 0.78, 0], 0x22272a);
  }

  box(g, [0.46, 0.28, 0.055], [0.46, 0.84, 0.56], COLORS.teal, {
    emissive: COLORS.teal,
    emissiveIntensity: 0.35,
  });

  const fan = new THREE.Mesh(
    new THREE.TorusGeometry(0.23, 0.035, 8, 28),
    mat(0x576168, { metalness: 0.55, roughness: 0.35 }),
  );
  fan.position.set(-0.47, 0.76, 0.57);
  g.add(fan);

  addTwinBeacon(g, [0.64, 1.58, 0.42]);
  return g;
}

function buildBtsServiceCabinet(root, pos) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "btsService";
  root.add(g);

  box(g, [1.9, 0.16, 1.4], [0, 0.05, 0], 0x252a2d);
  box(g, [1.45, 1.5, 1.0], [0, 0.84, 0], 0x30363a);

  for (let y = 0.35; y <= 1.32; y += 0.22) {
    box(g, [0.92, 0.10, 0.045], [0, y, 0.52], 0x1f2427);
    box(g, [0.42, 0.025, 0.055], [0.14, y, 0.56], COLORS.teal, {
      emissive: COLORS.teal,
      emissiveIntensity: 0.35,
    });
  }

  addTwinBeacon(g, [0.56, 1.72, 0.38]);
  return g;
}

function buildMicrowaveTerminal(root, pos, component, facing = 1) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = component;
  root.add(g);

  box(g, [1.6, 0.14, 1.25], [0, 0.05, 0], 0x252a2d);
  cyl(g, 0.055, 1.55, [0, 0.84, 0], 0x5c646a);

  const dish = new THREE.Mesh(
    new THREE.CylinderGeometry(0.62, 0.12, 0.17, 30),
    mat(0x91836e, { metalness: 0.28, roughness: 0.58 }),
  );
  dish.position.set(0, 1.48, 0);
  dish.rotation.z = Math.PI / 2;
  dish.rotation.y = facing > 0 ? 0 : Math.PI;
  g.add(dish);

  box(g, [0.34, 0.22, 0.26], [0, 1.47, 0], 0x4c555a);
  addTwinBeacon(g, [0.48, 1.78, 0.34]);
  return g;
}

function buildMainControlCabinet(root, pos) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "mainControl";
  root.add(g);

  box(g, [2.0, 0.16, 1.5], [0, 0.05, 0], 0x252a2d);
  box(g, [1.55, 1.55, 1.05], [0, 0.88, 0], 0x343a3e);

  box(g, [0.68, 0.34, 0.055], [0, 0.97, 0.56], 0x22343c);
  box(g, [0.56, 0.19, 0.06], [0, 0.97, 0.59], COLORS.teal, {
    emissive: COLORS.teal,
    emissiveIntensity: 0.42,
  });

  for (const x of [-0.55, 0.55]) {
    box(g, [0.05, 0.82, 1.08], [x, 0.80, 0], 0x23282b);
  }

  addTwinBeacon(g, [0.60, 1.80, 0.38]);
  return g;
}

function buildAiPicoCabinet(root, pos) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "aiPico";
  root.add(g);

  box(g, [2.0, 0.16, 1.5], [0, 0.05, 0], 0x252a2d);
  box(g, [1.55, 1.7, 1.08], [0, 0.95, 0], 0x30363a);

  const laptop = box(g, [1.05, 0.18, 0.62], [0, 1.18, 0.58], 0x232a2d);
  laptop.rotation.x = -0.12;

  box(g, [0.82, 0.06, 0.52], [0, 1.22, 0.68], COLORS.teal, {
    emissive: COLORS.teal,
    emissiveIntensity: 0.40,
  });

  box(g, [0.62, 0.16, 0.48], [0, 0.52, 0.58], 0x45525a);
  box(g, [0.46, 0.035, 0.36], [0, 0.54, 0.68], 0x68a889, {
    emissive: 0x68a889,
    emissiveIntensity: 0.35,
  });

  addTwinBeacon(g, [0.58, 1.98, 0.38]);
  return g;
}

function buildRectifierCabinet(root, pos) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "rectifier";
  root.add(g);

  box(g, [2.2, 0.16, 1.55], [0, 0.05, 0], 0x252a2d);
  box(g, [1.72, 1.55, 1.1], [0, 0.88, 0], 0x363c40);

  for (let y = 0.38; y <= 1.32; y += 0.24) {
    box(g, [1.15, 0.11, 0.055], [0, y, 0.58], 0x202629);
    box(g, [0.52, 0.025, 0.06], [0.18, y, 0.62], COLORS.amber, {
      emissive: COLORS.amber,
      emissiveIntensity: 0.28,
    });
  }

  addTwinBeacon(g, [0.65, 1.82, 0.38]);
  return g;
}

function buildSiteLoadCabinet(root, pos) {
  const g = new THREE.Group();
  g.position.set(...pos);
  g.userData.component = "siteLoad";
  root.add(g);

  box(g, [1.9, 0.16, 1.35], [0, 0.05, 0], 0x252a2d);
  box(g, [1.45, 1.3, 0.98], [0, 0.75, 0], 0x30363a);

  for (const y of [0.40, 0.66, 0.92, 1.18]) {
    box(g, [0.92, 0.11, 0.045], [0, y, 0.52], 0x1f2427);
  }

  addTwinBeacon(g, [0.56, 1.52, 0.34]);
  return g;
}

function curveSlice(curve, a, b, steps = 18) {
  const pts = [];
  for (let i = 0; i <= steps; i += 1) {
    pts.push(curve.getPoint(a + (b - a) * (i / steps)));
  }
  return new THREE.CatmullRomCurve3(pts);
}

function buildLiveTwinLink(root, points, kind, statusKey, radius = 0.045) {
  const curve = new THREE.CatmullRomCurve3(points.map((p) => new THREE.Vector3(...p)));
  const base = kind === "power" ? COLORS.amberGlow : COLORS.tealGlow;

  const intervals = [[0, 0.42], [0.42, 0.58], [0.58, 1]];
  const segments = intervals.map(([a, b]) => {
    const mesh = new THREE.Mesh(
      new THREE.TubeGeometry(curveSlice(curve, a, b), 22, radius, 8, false),
      mat(base, {
        emissive: base,
        emissiveIntensity: 0.55,
        metalness: 0.22,
        roughness: 0.32,
      }),
    );
    root.add(mesh);
    return mesh;
  });

  const pulses = [];
  for (let i = 0; i < 3; i += 1) {
    const p = new THREE.Mesh(
      new THREE.SphereGeometry(radius * 1.55, 10, 10),
      mat(base, {
        emissive: base,
        emissiveIntensity: 1.0,
        metalness: 0,
        roughness: 0.2,
      }),
    );
    root.add(p);
    pulses.push({
      mesh: p,
      curve,
      offset: i / 3,
      speed: kind === "power" ? 0.075 : 0.10,
    });
  }

  return { kind, statusKey, curve, segments, pulses, status: "idle" };
}

function setLiveTwinLink(link, status) {
  if (!link || link.status === status) return;
  link.status = status;

  const color = twinColor(status, link.kind);
  const emissive = status !== "idle";

  link.segments.forEach((mesh, index) => {
    setMaterialColor(mesh.material, color, emissive);
    mesh.material.emissiveIntensity = status === "good" ? 0.60 : status === "warn" ? 0.34 : 0.20;
    mesh.visible = !(status === "down" && index === 1);
  });

  link.pulses.forEach((pulse) => {
    setMaterialColor(pulse.mesh.material, color, status === "good");
    pulse.mesh.visible = status === "good";
  });
}

function updateTwinV7(live, state) {
  const t = state?.telemetry ?? {};
  const activeSource = String(t.active_power_source ?? "").toUpperCase();

  for (const link of live.links) {
    let status = "idle";

    if (link.statusKey === "radio") status = twinStatus("radio", state);
    if (link.statusKey === "backhaul") status = twinStatus("backhaul", state);
    if (link.statusKey === "telemetry") status = twinStatus("telemetry", state);
    if (link.statusKey === "ai") status = twinStatus("ai", state);
    if (link.statusKey === "dual") status = twinStatus("dual", state);
    if (link.statusKey === "load") status = twinStatus("power", state);

    if (link.statusKey === "gridPower") {
      status = t.grid_available === false
        ? "down"
        : activeSource === "GRID"
          ? "good"
          : "idle";
    }

    if (link.statusKey === "generatorPower") {
      const running = Boolean(t.generator_feedback_running ?? t.generator_running);
      status = running ? "good" : "idle";
    }

    if (link.statusKey === "batteryPower") {
      const soc = Number(t.battery_soc_pct ?? 0);
      status = soc <= 25
        ? "down"
        : activeSource === "BATTERY"
          ? "good"
          : soc <= 40
            ? "warn"
            : "idle";
    }

    setLiveTwinLink(link, status);
  }

  setTwinBeacon(live.groups.btsRadio, twinStatus("radio", state));
  setTwinBeacon(live.groups.btsService, twinStatus("backhaul", state));
  setTwinBeacon(live.groups.btsBackhaul, twinStatus("backhaul", state));
  setTwinBeacon(live.groups.mainBackhaul, twinStatus("backhaul", state));
  setTwinBeacon(live.groups.mainControl, twinStatus("telemetry", state));
  setTwinBeacon(live.groups.aiPico, twinStatus("dual", state));
  setTwinBeacon(live.groups.rectifier, twinStatus("power", state));
  setTwinBeacon(live.groups.grid, twinStatus("grid", state));
  setTwinBeacon(live.groups.generator, twinStatus("generator", state));
  setTwinBeacon(live.groups.battery, twinStatus("battery", state));
  setTwinBeacon(live.groups.siteLoad, twinStatus("power", state));
}


/* ==========================================================================
   ABS SITE 3D — TWO-STATION SPACING V9
   Increases physical separation between the mountain BTS and main site.
   ========================================================================== */

const ABS_BTS_SITE_SHIFT_X = -3.2;

/* ==========================================================================
   ABS SITE 3D — BTS MOUNTAIN SITE V8
   Elevates BTS-001 onto a realistic telecom hilltop / mountain platform.
   ========================================================================== */

function buildBtsMountain(root, centerX, summitY) {
  const mountain = new THREE.Group();
  mountain.position.set(centerX, 0, 0);
  root.add(mountain);

  const rockMaterial = new THREE.MeshStandardMaterial({
    color: 0x667078,
    roughness: 0.96,
    metalness: 0.02,
    flatShading: true,
  });

  const lowerRockMaterial = new THREE.MeshStandardMaterial({
    color: 0x566168,
    roughness: 0.98,
    metalness: 0.01,
    flatShading: true,
  });

  const base = new THREE.Mesh(
    new THREE.CylinderGeometry(4.05, 7.5, summitY + 0.25, 14, 4, false),
    rockMaterial,
  );
  base.position.y = (summitY + 0.25) / 2 - 0.12;
  base.rotation.y = 0.18;
  mountain.add(base);

  const shoulder = new THREE.Mesh(
    new THREE.CylinderGeometry(3.72, 5.15, 1.05, 13, 2, false),
    lowerRockMaterial,
  );
  shoulder.position.set(-0.15, summitY - 0.34, 0.08);
  shoulder.rotation.y = -0.11;
  mountain.add(shoulder);

  const outcrops = [
    [-4.55, 0.68, -1.15, 1.55, 0.82, 1.28],
    [4.35, 0.58, 0.95, 1.42, 0.74, 1.16],
    [-2.65, 0.42, 4.05, 1.18, 0.62, 1.04],
    [2.85, 0.48, -4.15, 1.28, 0.68, 1.12],
  ];

  for (const [x, y, z, sx, sy, sz] of outcrops) {
    const rock = new THREE.Mesh(
      new THREE.DodecahedronGeometry(1, 0),
      lowerRockMaterial.clone(),
    );
    rock.position.set(x, y, z);
    rock.scale.set(sx, sy, sz);
    rock.rotation.set(0.12 * z, 0.18 * x, 0.08 * x);
    mountain.add(rock);
  }

  box(
    mountain,
    [7.55, 0.24, 7.35],
    [0, summitY - 0.11, 0],
    0x394247,
    { roughness: 0.88, metalness: 0.08 },
  );

  const track = new THREE.Mesh(
    new THREE.BoxGeometry(1.0, 0.05, 5.3),
    new THREE.MeshStandardMaterial({
      color: 0x8b806e,
      roughness: 1,
      metalness: 0,
    }),
  );
  track.position.set(-3.25, summitY * 0.42, 3.65);
  track.rotation.x = -0.43;
  track.rotation.y = 0.14;
  mountain.add(track);

  return mountain;
}

// ABS SITE 3D — DARK BACKGROUND V10: dark world, bright readable equipment.
function buildSite(state) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x16191b);
  scene.fog = new THREE.FogExp2(0x16191b, 0.014);

  scene.add(new THREE.HemisphereLight(0xffffff, 0x58646a, 2.55));
  scene.add(new THREE.AmbientLight(0xffffff, 0.70));

  const sun = new THREE.DirectionalLight(0xfff1d7, 3.45);
  sun.position.set(9, 13, 8);
  scene.add(sun);

  const fill = new THREE.DirectionalLight(0xe4f4ff, 1.55);
  fill.position.set(-10, 7, 3);
  scene.add(fill);

  const root = new THREE.Group();
  scene.add(root);

  // Two actual physical zones.
  // BTS-001 is a remote elevated telecom site on a mountain summit.
  const BTS_Y = 3.25;
  buildBtsMountain(root, -4.6 + ABS_BTS_SITE_SHIFT_X, BTS_Y);

  const btsPad = buildTwinPad(root, -4.6 + ABS_BTS_SITE_SHIFT_X, 7.1, 7.0, "BTS-001 STATION");
  btsPad.position.y = BTS_Y;

  buildTwinPad(root, 4.6, 7.1, 7.0, "AUTONOMOUS BASE STATION");

  // ------------------------------------------------------------------
  // BTS-001
  // Actual simulation roles represented as site equipment:
  // BTS ESP32/RF plant + service node + radio mast + backhaul terminal.
  // ------------------------------------------------------------------
  const btsRadio = buildBtsRadioCabinet(root, [-5.1 + ABS_BTS_SITE_SHIFT_X, BTS_Y + 0.1, 0.3]);

  const mast = buildMast(root, [-6.25 + ABS_BTS_SITE_SHIFT_X, BTS_Y + 0.1, 2.0], state);
  mast.userData.component = "btsRadio";
  addTwinBeacon(mast, [0.55, 4.7, 0.25]);

  const btsService = buildBtsServiceCabinet(root, [-4.1 + ABS_BTS_SITE_SHIFT_X, BTS_Y + 0.1, -1.5]);
  const btsBackhaul = buildMicrowaveTerminal(root, [-1.9 + ABS_BTS_SITE_SHIFT_X, BTS_Y + 0.1, -1.55], "btsBackhaul", 1);

  // ------------------------------------------------------------------
  // Main autonomous base station
  // Main ESP32 + Laptop AI + Pico + power plant + managed load.
  // ------------------------------------------------------------------
  const mainBackhaul = buildMicrowaveTerminal(root, [1.9, 0.1, -1.55], "mainBackhaul", -1);
  const mainControl = buildMainControlCabinet(root, [2.7, 0.1, 0.15]);
  const aiPico = buildAiPicoCabinet(root, [4.65, 0.1, 0.15]);
  const rectifier = buildRectifierCabinet(root, [4.05, 0.1, 2.1]);

  const grid = buildTransformer(root, [6.45, 0.1, 2.0], state);
  grid.userData.component = "grid";
  addTwinBeacon(grid, [0.72, 2.62, 0.32]);

  const generator = buildGenerator(root, [6.25, 0.1, -2.05], state);
  generator.userData.component = "generator";
  addTwinBeacon(generator, [0.90, 1.74, 0.32]);

  const battery = buildBattery(root, [3.95, 0.1, -2.15], state);
  battery.userData.component = "battery";
  addTwinBeacon(battery, [0.90, 1.72, 0.32]);

  const siteLoad = buildSiteLoadCabinet(root, [6.3, 0.1, 0.0]);

  // ------------------------------------------------------------------
  // Real operational connections.
  // Each link is physically broken in the middle when DOWN.
  // ------------------------------------------------------------------
  const links = [
    // BTS internal path.
    buildLiveTwinLink(
      root,
      [[-6.0 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,1.55],[-5.8 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,0.9],[-5.1 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,0.3]],
      "data",
      "radio",
    ),
    buildLiveTwinLink(
      root,
      [[-5.1 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,0.3],[-4.7 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,-0.6],[-4.1 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,-1.5]],
      "data",
      "radio",
    ),
    buildLiveTwinLink(
      root,
      [[-4.1 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,-1.5],[-3.2 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,-1.55],[-1.9 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 0.55,-1.55]],
      "data",
      "backhaul",
    ),

    // Inter-station backhaul.
    buildLiveTwinLink(
      root,
      [[-1.9 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 1.48,-1.55],[-0.85 + ABS_BTS_SITE_SHIFT_X,BTS_Y + 1.05,-1.55],[-2.2,BTS_Y * 0.55 + 1.55,-1.55],[0.2,2.0,-1.55],[1.9,1.48,-1.55]],
      "data",
      "backhaul",
      0.032,
    ),

    // Main telemetry path.
    buildLiveTwinLink(
      root,
      [[1.9,0.55,-1.55],[2.25,0.55,-0.65],[2.7,0.55,0.15]],
      "data",
      "telemetry",
    ),
    buildLiveTwinLink(
      root,
      [[2.7,0.72,0.15],[3.6,0.72,0.15],[4.65,0.72,0.15]],
      "data",
      "ai",
    ),
    buildLiveTwinLink(
      root,
      [[4.65,0.48,0.15],[4.0,0.40,-0.35],[3.15,0.40,-0.05],[2.7,0.40,0.15]],
      "data",
      "dual",
      0.036,
    ),

    // Power plant.
    buildLiveTwinLink(
      root,
      [[6.45,0.36,2.0],[5.25,0.36,2.0],[4.05,0.36,2.1]],
      "power",
      "gridPower",
      0.055,
    ),
    buildLiveTwinLink(
      root,
      [[6.25,0.36,-2.05],[5.35,0.36,-1.2],[4.65,0.36,0.3],[4.05,0.36,2.1]],
      "power",
      "generatorPower",
      0.055,
    ),
    buildLiveTwinLink(
      root,
      [[3.95,0.36,-2.15],[3.95,0.36,-0.6],[4.05,0.36,2.1]],
      "power",
      "batteryPower",
      0.055,
    ),
    buildLiveTwinLink(
      root,
      [[4.05,0.36,2.1],[3.5,0.36,1.1],[2.7,0.36,0.15]],
      "power",
      "load",
      0.055,
    ),
    buildLiveTwinLink(
      root,
      [[4.05,0.36,2.1],[5.1,0.36,1.1],[6.3,0.36,0.0]],
      "power",
      "load",
      0.055,
    ),
  ];

  const pulses = links.flatMap((link) => link.pulses);

  const live = {
    links,
    groups: {
      btsRadio,
      btsService,
      btsBackhaul,
      mainBackhaul,
      mainControl,
      aiPico,
      rectifier,
      grid,
      generator,
      battery,
      siteLoad,
    },
  };

  updateTwinV7(live, state);

  return { scene, root, pulses, live };
}

function disposeObject(object) {
  object.traverse((node) => {
    node.geometry?.dispose?.();
    const materials = Array.isArray(node.material) ? node.material : [node.material];
    for (const m of materials) m?.dispose?.();
  });
}

function destroy() {
  if (!view) return;

  cancelAnimationFrame(view.raf);
  view.resizeObserver?.disconnect();
  view.controls?.dispose();
  disposeObject(view.scene);
  view.renderer?.dispose();
  view.renderer?.forceContextLoss?.();

  view.shell?.remove();
  document.body.classList.remove("site3d-overview-active");
  view = null;
}


const SITE_COMPONENT_INFO = {
  battery: { title: "Battery Bank", type: "Energy storage", icon: "battery_charging_full", role: "DC backup and energy buffer" },
  generator: { title: "Generator", type: "Backup power plant", icon: "offline_bolt", role: "Backup generation for the site" },
  grid: { title: "Grid Transformer", type: "Primary power input", icon: "electrical_services", role: "Utility/grid feed into the managed power system" },
  power: { title: "Managed Power Controller", type: "Site power control", icon: "speed", role: "Managed site power and final operating mode" },
  server: { title: "Control + AI Cabinet", type: "Control and inference equipment", icon: "dns", role: "ESP32 control, laptop AI and site electronics" },
  mast: { title: "Radio Mast", type: "Radio access plant", icon: "cell_tower", role: "Radio traffic and live BTS radio path" },
  backhaul: { title: "Backhaul Dishes", type: "Transport link", icon: "router", role: "BTS backhaul connection into the autonomous site" },
};

function componentRows(component) {
  const s = dashboardState();
  const t = s.telemetry ?? {};
  const ai = s.ai ?? {};
  const cmd = ai?.runtime?.ai_command ?? {};
  const pico = ai?.runtime?.pico_decision ?? {};

  const rows = {
    battery: [
      ["State of charge", `${Number(t.battery_soc_pct ?? 0).toFixed(1)}%`],
      ["Battery voltage", `${Number(t.battery_voltage_v ?? 0).toFixed(2)} V`],
      ["SOC trend", `${Number(t.battery_soc_trend_pct_per_min ?? 0).toFixed(2)} %/min`],
    ],
    generator: [
      ["State", (t.generator_feedback_running ?? t.generator_running) ? "RUNNING" : "STOPPED"],
      ["Verification", t.generator_verification_state ?? "—"],
      ["Pico action", pico.generator_action ?? "—"],
    ],
    grid: [
      ["Grid", t.grid_available === true ? "AVAILABLE" : "FAILED / UNKNOWN"],
      ["Active source", t.active_power_source ?? "—"],
      ["DC bus", `${Number(t.dc_voltage_v ?? 0).toFixed(2)} V`],
    ],
    power: [
      ["Managed power", `${Number(t.managed_power_kw ?? 0).toFixed(3)} kW`],
      ["Final mode", t.operating_mode ?? "—"],
      ["Guardrails", t.guardrail_status ?? "—"],
      ["Power source", t.active_power_source ?? "—"],
    ],
    server: [
      ["ESP32", ":4001"],
      ["Laptop AI", ai?.fault_domain?.label ?? "WAITING"],
      ["AI trust", ai?.trust?.decision ?? "—"],
      ["Dual AI", cmd.dual_ai_agreement ?? "—"],
    ],
    mast: [
      ["Traffic", `${Number(t.traffic_load_pct ?? 0).toFixed(1)}%`],
      ["Radio", t.radio_operational === true ? "OPERATIONAL" : "FAULT / UNKNOWN"],
      ["Active calls", `${Number(t.network_active_calls ?? 0)}`],
      ["RSSI", `${Number(t.rssi_dbm ?? 0).toFixed(1)} dBm`],
    ],
    backhaul: [
      ["Status", t.backhaul_status ?? "—"],
      ["Latency", `${Number(t.latency_ms ?? 0).toFixed(1)} ms`],
      ["Packet loss", `${Number(t.packet_loss_pct ?? 0).toFixed(2)}%`],
      ["Input source", t.network_input_source ?? "—"],
    ],
  };

  return rows[component] ?? [];
}

function renderComponentInfo(shell, component) {
  const panel = shell.querySelector("[data-site3d-info]");
  const def = TWIN_V7_INFO[component] ?? SITE_COMPONENT_INFO[component];
  if (!panel || !def) return;

  panel.hidden = false;
  shell.dataset.selectedComponent = component;

  panel.querySelector("[data-site3d-info-title]").textContent = def.title;
  panel.querySelector("[data-site3d-info-type]").textContent = def.type;
  panel.querySelector("[data-site3d-info-icon]").textContent = def.icon;
  panel.querySelector("[data-site3d-info-role]").textContent = def.role;

  panel.querySelector("[data-site3d-info-body]").innerHTML = twinV7Rows(component)
    .map(([label, value]) => `
      <div class="site3d-info-row">
        <span>${label}</span>
        <strong>${value}</strong>
      </div>
    `)
    .join("");
}

function hideComponentInfo(shell) {
  const panel = shell.querySelector("[data-site3d-info]");
  if (panel) panel.hidden = true;
  delete shell.dataset.selectedComponent;
}

function updateHud(shell) {
  const state = dashboardState();
  const t = state.telemetry ?? {};
  const ai = state.ai ?? {};
  const cmd = ai?.runtime?.ai_command ?? {};

  const values = {
    battery: `${Number(t.battery_soc_pct ?? 0).toFixed(1)}%`,
    source: t.active_power_source ?? "—",
    generator: (t.generator_feedback_running ?? t.generator_running) ? "RUNNING" : "STOPPED",
    traffic: `${Number(t.traffic_load_pct ?? 0).toFixed(1)}%`,
    backhaul: t.backhaul_status ?? "—",
    power: `${Number(t.managed_power_kw ?? 0).toFixed(3)} kW`,
    ai: `${ai.fault_domain?.label ?? "—"} / ${cmd.dual_ai_agreement ?? "—"}`,
  };

  for (const [key, value] of Object.entries(values)) {
    const el = shell.querySelector(`[data-site3d-value="${key}"]`);
    if (el) el.textContent = value;
  }

  const bts = state.bts ?? {};
  const online = new Set(
    Array.isArray(bts.subscriber_numbers)
      ? bts.subscriber_numbers.map(normalizeMobileNumber).filter(Boolean)
      : [],
  );
  const calls = Array.isArray(bts.active_call_pairs)
    ? bts.active_call_pairs.filter((call) => {
        const x = String(call?.state ?? "").toUpperCase();
        return x === "RINGING" || x === "CONNECTED";
      })
    : [];
  const inCall = new Set();
  for (const call of calls) {
    const a = normalizeMobileNumber(call?.caller);
    const b = normalizeMobileNumber(call?.callee);
    if (a) inCall.add(a);
    if (b) inCall.add(b);
  }

  const mobileRows = shell.querySelector("[data-site3d-mobile-subscribers]");
  if (mobileRows) {
    mobileRows.innerHTML = DEMO_MOBILE_SUBSCRIBERS.map((number) => {
      const status = inCall.has(number) ? "call" : online.has(number) ? "online" : "free";
      const label = status === "call" ? "IN CALL" : status === "online" ? "ONLINE" : "FREE";
      return `<div class="site3d-mobile-row ${status}"><span class="site3d-mobile-dot"></span><strong>${formatMobileNumber(number)}</strong><small>${label}</small></div>`;
    }).join("");
  }

  const onlineNode = shell.querySelector("[data-site3d-mobile-online]");
  if (onlineNode) onlineNode.textContent = `${online.size} / 6 online`;

  const callNode = shell.querySelector("[data-site3d-mobile-calls]");
  if (callNode) {
    callNode.innerHTML = calls.length
      ? calls.map((call) => `<div class="site3d-mobile-call"><span>${shortMobileNumber(call?.caller)}</span><i>↔</i><span>${shortMobileNumber(call?.callee)}</span><small>${String(call?.state ?? "ACTIVE").toUpperCase()}</small></div>`).join("")
      : `<div class="site3d-mobile-call empty"><span>No active calls</span></div>`;
  }

  if (shell.dataset.selectedComponent) {
    renderComponentInfo(shell, shell.dataset.selectedComponent);
  }
}

function mount() {
  if (!activeOverview()) {
    destroy();
    return;
  }

  if (view?.shell?.isConnected) {
    updateHud(view.shell);
    return;
  }

  const stats = document.querySelector(".stats-grid");
  const pageStack = stats?.closest(".page-stack");
  if (!stats || !pageStack) return;

  document.body.classList.add("site3d-overview-active");

  const shell = document.createElement("section");
  shell.className = "site3d-shell";
  shell.innerHTML = `
    <div class="site3d-toolbar">
      <div>
        <strong>Complete Base Station · 3D Site</strong>
        <span>interconnected power + telecom plant</span>
      </div>
      <div class="site3d-toolbar-actions">
        <button type="button" data-site3d-action="reset">Reset View</button>
      </div>
    </div>

    <div class="site3d-stage">
      <canvas aria-label="Interactive 3D interconnected base station"></canvas>
      <div class="site3d-fallback"></div>

      <div class="site3d-overlay">
        <div class="site3d-status">
          <div class="site3d-status-row"><span>Battery</span><strong data-site3d-value="battery">—</strong></div>
          <div class="site3d-status-row"><span>Power source</span><strong data-site3d-value="source">—</strong></div>
          <div class="site3d-status-row"><span>Generator</span><strong data-site3d-value="generator">—</strong></div>
          <div class="site3d-status-row"><span>Traffic</span><strong data-site3d-value="traffic">—</strong></div>
          <div class="site3d-status-row"><span>Backhaul</span><strong data-site3d-value="backhaul">—</strong></div>
          <div class="site3d-status-row"><span>Managed power</span><strong data-site3d-value="power">—</strong></div>
          <div class="site3d-status-row"><span>AI / Dual AI</span><strong data-site3d-value="ai">—</strong></div>
        </div>

        <div class="site3d-mobile-panel">
          <div class="site3d-mobile-head">
            <span><i class="material-symbols-rounded">smartphone</i>Mobile subscribers</span>
            <strong data-site3d-mobile-online>0 / 6 online</strong>
          </div>
          <div class="site3d-mobile-subscribers" data-site3d-mobile-subscribers></div>
          <div class="site3d-mobile-calls" data-site3d-mobile-calls>
            <div class="site3d-mobile-call empty"><span>No active calls</span></div>
          </div>
        </div>

        <div class="site3d-info" data-site3d-info hidden>
          <div class="site3d-info-head">
            <div class="site3d-info-icon material-symbols-rounded" data-site3d-info-icon>info</div>
            <div>
              <span data-site3d-info-type>Equipment</span>
              <strong data-site3d-info-title>Selected component</strong>
            </div>
          </div>
          <div class="site3d-info-role" data-site3d-info-role></div>
          <div class="site3d-info-body" data-site3d-info-body></div>
          <small>Tap empty space to close</small>
        </div>

        <div class="site3d-legend">
          <span><i class="power"></i>Power path</span>
          <span><i class="data"></i>Telecom / backhaul</span>
          <span>Tap equipment for live info</span>
        </div>
      </div>
    </div>
  `;

  stats.before(shell);

  const canvas = shell.querySelector("canvas");
  const stage = shell.querySelector(".site3d-stage");

  let renderer;

  try {
    renderer = new THREE.WebGLRenderer({
      canvas,
      antialias: true,
      alpha: false,
      powerPreference: "high-performance",
    });
  } catch (error) {
    shell.classList.add("webgl-failed");
    updateHud(shell);
    return;
  }

  const state = dashboardState();
  const { scene, pulses, live } = buildSite(state);

  const camera = new THREE.PerspectiveCamera(38, 1, 0.1, 100);
  const start = new THREE.Vector3(20.6, 14.4, 22.6);
  camera.position.copy(start);

  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.34;

  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.dampingFactor = 0.06;
  controls.enablePan = true;
  controls.minDistance = 10;
  controls.maxDistance = 34;
  controls.target.set(-0.8, 2.2, 0);

  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();
  let pointerDown = null;
  let selectionHelper = null;
  let selectedGroup = null;

  function componentGroupFromObject(object) {
    let current = object;
    while (current && current !== scene) {
      if (current.userData?.component) return current;
      current = current.parent;
    }
    return null;
  }

  function clearSelection() {
    selectedGroup = null;

    if (selectionHelper) {
      scene.remove(selectionHelper);
      selectionHelper.geometry?.dispose?.();
      selectionHelper.material?.dispose?.();
      selectionHelper = null;
    }

    hideComponentInfo(shell);
  }

  function selectGroup(group) {
    if (!group?.userData?.component) {
      clearSelection();
      return;
    }

    selectedGroup = group;

    if (selectionHelper) {
      scene.remove(selectionHelper);
      selectionHelper.geometry?.dispose?.();
      selectionHelper.material?.dispose?.();
    }

    selectionHelper = new THREE.BoxHelper(group, 0x6f9eaa);
    selectionHelper.material.transparent = true;
    selectionHelper.material.opacity = 0.9;
    scene.add(selectionHelper);

    renderComponentInfo(shell, group.userData.component);
  }

  canvas.addEventListener("pointerdown", (event) => {
    pointerDown = {
      x: event.clientX,
      y: event.clientY,
      time: performance.now(),
    };
  });

  canvas.addEventListener("pointerup", (event) => {
    if (!pointerDown) return;

    const move = Math.hypot(
      event.clientX - pointerDown.x,
      event.clientY - pointerDown.y,
    );
    const elapsed = performance.now() - pointerDown.time;
    pointerDown = null;

    if (move > 7 || elapsed > 700) return;

    const rect = canvas.getBoundingClientRect();
    pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;

    raycaster.setFromCamera(pointer, camera);

    const hits = raycaster.intersectObjects(scene.children, true);
    const group = hits
      .map((hit) => componentGroupFromObject(hit.object))
      .find(Boolean);

    if (group) selectGroup(group);
    else clearSelection();
  });

  const clock = new THREE.Clock();
  let nextTwinLiveRefresh = 0;

  function resize() {
    const rect = stage.getBoundingClientRect();
    const w = Math.max(1, Math.floor(rect.width));
    const h = Math.max(1, Math.floor(rect.height));

    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }

  function animate() {
    const t = clock.getElapsedTime();

    for (let i = 0; i < pulses.length; i += 1) {
      const pulse = pulses[i];
      const p = pulse.curve.getPointAt((t * pulse.speed + pulse.offset) % 1);
      pulse.mesh.position.copy(p);
    }

    if (t >= nextTwinLiveRefresh) {
      updateTwinV7(live, dashboardState());
      nextTwinLiveRefresh = t + 0.35;
    }

    if (selectionHelper && selectedGroup) {
      selectionHelper.update();
    }

    controls.update();
    renderer.render(scene, camera);
    view.raf = requestAnimationFrame(animate);
  }

  const ro = new ResizeObserver(resize);
  ro.observe(stage);

  shell.querySelector('[data-site3d-action="reset"]')?.addEventListener("click", () => {
    camera.position.copy(start);
    controls.target.set(0, 1.0, 0);
    controls.update();
  });

  view = {
    shell,
    scene,
    renderer,
    controls,
    resizeObserver: ro,
    raf: 0,
  };

  resize();
  updateHud(shell);
  animate();
}

window.addEventListener("abs-dashboard-before-render", destroy);
window.addEventListener("abs-dashboard-rendered", () => {
  requestAnimationFrame(mount);
});

window.setInterval(() => {
  if (activeOverview()) {
    if (!view?.shell?.isConnected) {
      mount();
    } else {
      updateHud(view.shell);
    }
  }
}, 1500);

requestAnimationFrame(mount);



// ABS EXACT IMAGE3 RESTORE + COLLAPSIBLE HUD V7
(() => {
  function addCollapseButton(panel, header, label) {
    if (!panel || !header) return;
    if (header.querySelector('[data-abs-collapse-button]')) return;

    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'abs-hud-collapse-button material-symbols-rounded';
    button.dataset.absCollapseButton = '1';
    button.setAttribute('aria-expanded', 'true');
    button.setAttribute('aria-label', `Collapse ${label}`);
    button.title = `Collapse ${label}`;
    button.textContent = 'expand_less';

    button.addEventListener('pointerdown', (event) => {
      event.stopPropagation();
    });

    button.addEventListener('click', (event) => {
      event.preventDefault();
      event.stopPropagation();

      const collapsed = panel.classList.toggle('abs-hud-collapsed');
      button.textContent = collapsed ? 'expand_more' : 'expand_less';
      button.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
      button.setAttribute(
        'aria-label',
        `${collapsed ? 'Expand' : 'Collapse'} ${label}`,
      );
      button.title = `${collapsed ? 'Expand' : 'Collapse'} ${label}`;
    });

    header.appendChild(button);
  }

  function ensureSiteStatusCollapse() {
    const panel = document.querySelector('.site3d-status');
    if (!panel) return;

    let header = panel.querySelector('.abs-site-status-head');
    if (!header) {
      header = document.createElement('div');
      header.className = 'abs-site-status-head';

      const title = document.createElement('span');
      title.textContent = 'Site status';
      header.appendChild(title);
      panel.prepend(header);
    }

    addCollapseButton(panel, header, 'Site status');
  }

  function ensureMobileCollapse() {
    const panel = document.querySelector('.site3d-mobile-panel');
    const header = panel?.querySelector('.site3d-mobile-head');
    addCollapseButton(panel, header, 'Mobile subscribers');
  }

  function ensureEquipmentCollapse() {
    const panel = document.querySelector('.site3d-info');
    const header = panel?.querySelector('.site3d-info-head');
    addCollapseButton(panel, header, 'Equipment details');
  }

  function install() {
    ensureSiteStatusCollapse();
    ensureMobileCollapse();
    ensureEquipmentCollapse();
  }

  window.addEventListener('abs-dashboard-rendered', () => {
    requestAnimationFrame(install);
  });

  window.setInterval(install, 800);
  requestAnimationFrame(install);
})();
