
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

function buildSite(state) {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x1b1e20);
  scene.fog = new THREE.FogExp2(0x1b1e20, 0.032);

  scene.add(new THREE.HemisphereLight(0xc8d0d4, 0x16191b, 1.55));

  const key = new THREE.DirectionalLight(0xffd4a3, 2.1);
  key.position.set(6, 10, 7);
  scene.add(key);

  const rim = new THREE.DirectionalLight(0x6da6b2, 1.55);
  rim.position.set(-7, 5, -6);
  scene.add(rim);

  const root = new THREE.Group();
  scene.add(root);

  platform(root);

  buildGenerator(root, [-4.2, 0.1, 2.4], state);
  buildBattery(root, [-4.1, 0.1, -1.8], state);
  buildBackhaul(root, [-4.3, 0.1, -3.0], state);
  buildTransformer(root, [-1.9, 0.1, -2.8], state);
  buildPowerController(root, [0, 0.1, 0], state);
  buildServerCabinet(root, [3.0, 0.1, -1.8]);
  buildMast(root, [4.4, 0.1, 2.3], state);

  const pulses = addRoutes(root);

  return { scene, root, pulses };
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

        <div class="site3d-legend">
          <span><i class="power"></i>Power path</span>
          <span><i class="data"></i>Telecom / backhaul</span>
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
  const { scene, pulses } = buildSite(state);

  const camera = new THREE.PerspectiveCamera(38, 1, 0.1, 100);
  const start = new THREE.Vector3(11.6, 8.7, 12.8);
  camera.position.copy(start);

  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.08;

  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.dampingFactor = 0.06;
  controls.enablePan = true;
  controls.minDistance = 8;
  controls.maxDistance = 24;
  controls.target.set(0, 1.0, 0);

  const clock = new THREE.Clock();

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
