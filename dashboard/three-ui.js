import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

const MODEL_BY_LABEL = new Map([
  ["Battery State of Charge", "battery"],
  ["Active Power Source", "grid"],
  ["Generator", "generator"],
  ["Traffic Load", "traffic"],
  ["Backhaul", "backhaul"],
  ["Managed Site Power", "power"],
]);

const views = new Map();

const COLORS = {
  cyan: 0x6fc8cf,
  orange: 0xe69537,
  green: 0x63b58b,
  red: 0xd8614a,
};

function dashboardState() {
  return window.ABSDashboardGetState?.() ?? {};
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, Number(value) || 0));
}

function material(color, options = {}) {
  return new THREE.MeshStandardMaterial({
    color,
    metalness: options.metalness ?? 0.45,
    roughness: options.roughness ?? 0.48,
    emissive: options.emissive ?? 0x000000,
    emissiveIntensity: options.emissiveIntensity ?? 0,
    transparent: options.transparent ?? false,
    opacity: options.opacity ?? 1,
  });
}

function addBox(group, size, position, color, options = {}) {
  const mesh = new THREE.Mesh(
    new THREE.BoxGeometry(size[0], size[1], size[2]),
    material(color, options),
  );
  mesh.position.set(position[0], position[1], position[2]);
  group.add(mesh);
  return mesh;
}

function addCylinder(group, radius, height, position, color, rotation = [0, 0, 0], options = {}) {
  const mesh = new THREE.Mesh(
    new THREE.CylinderGeometry(radius, radius, height, 24),
    material(color, options),
  );
  mesh.position.set(position[0], position[1], position[2]);
  mesh.rotation.set(rotation[0], rotation[1], rotation[2]);
  group.add(mesh);
  return mesh;
}

function createBaseScene() {
  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0x1f2123, 0.045);

  const root = new THREE.Group();
  scene.add(root);

  scene.add(new THREE.HemisphereLight(0xcbd8df, 0x1a1a1a, 1.5));

  const key = new THREE.DirectionalLight(0xffc77b, 2.2);
  key.position.set(4, 7, 5);
  scene.add(key);

  const rim = new THREE.DirectionalLight(0x69c8da, 1.7);
  rim.position.set(-5, 3, -5);
  scene.add(rim);

  const grid = new THREE.GridHelper(10, 20, 0x3f474d, 0x2a2e31);
  grid.position.y = -1.18;
  grid.material.transparent = true;
  grid.material.opacity = 0.48;
  scene.add(grid);

  const ground = new THREE.Mesh(
    new THREE.PlaneGeometry(10, 10),
    new THREE.MeshStandardMaterial({
      color: 0x202325,
      roughness: 0.9,
      metalness: 0.05,
      transparent: true,
      opacity: 0.58,
    }),
  );
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -1.2;
  scene.add(ground);

  return { scene, root };
}

function buildBattery(root, state) {
  const soc = clamp(state?.telemetry?.battery_soc_pct ?? 0, 0, 100);

  addBox(root, [3.3, 0.18, 1.9], [0, -1.05, 0], 0x262b2f);
  const shell = addBox(
    root,
    [2.7, 1.65, 1.25],
    [0, -0.05, 0],
    0x30363b,
    { transparent: true, opacity: 0.72, metalness: 0.55 },
  );
  shell.renderOrder = 2;

  for (let i = 0; i < 4; i += 1) {
    const x = -0.82 + i * 0.55;
    addBox(root, [0.47, 1.26, 0.88], [x, -0.13, 0], 0x1e272a, {
      metalness: 0.2,
      roughness: 0.55,
    });

    const fillHeight = 1.16 * (soc / 100);
    if (fillHeight > 0.02) {
      addBox(
        root,
        [0.42, fillHeight, 0.82],
        [x, -0.71 + fillHeight / 2, 0],
        COLORS.cyan,
        {
          emissive: COLORS.cyan,
          emissiveIntensity: 0.8,
          metalness: 0.1,
          roughness: 0.35,
          transparent: true,
          opacity: 0.78,
        },
      );
    }
  }

  addCylinder(root, 0.14, 0.28, [-0.72, 0.93, 0], 0xb83c34);
  addCylinder(root, 0.14, 0.28, [0.72, 0.93, 0], 0x747c84);
  addBox(root, [2.95, 0.14, 1.4], [0, 0.78, 0], 0x454c52);
}

function buildGrid(root, state) {
  const available = Boolean(state?.telemetry?.grid_available);
  const accent = available ? COLORS.green : COLORS.red;

  addBox(root, [3.5, 0.15, 2.1], [0, -1.05, 0], 0x2a2e31);
  addBox(root, [1.65, 1.15, 1.05], [-0.45, -0.32, 0], 0x4d555b);

  for (let i = -4; i <= 4; i += 1) {
    addBox(root, [0.045, 0.92, 1.12], [-0.45 + i * 0.15, -0.33, 0], 0x2a3035);
  }

  for (const x of [-1.0, 0.1]) {
    addCylinder(root, 0.08, 0.7, [x, 0.63, 0], 0x30363a);
    addCylinder(root, 0.13, 0.12, [x, 0.9, 0], accent, [0, 0, 0], {
      emissive: accent,
      emissiveIntensity: available ? 0.8 : 0.45,
    });
  }

  const tower = new THREE.Group();
  tower.position.set(1.1, -0.1, -0.15);
  root.add(tower);

  for (const x of [-0.45, 0.45]) {
    addBox(tower, [0.08, 2.65, 0.08], [x, 0, 0], 0x697178);
  }
  for (let y = -1; y <= 1; y += 0.4) {
    addBox(tower, [1.05, 0.045, 0.045], [0, y, 0], 0x697178);
  }
  tower.rotation.z = 0.08;
}

function buildGenerator(root, state) {
  const running = Boolean(
    state?.telemetry?.generator_feedback_running ??
    state?.telemetry?.generator_running,
  );

  addBox(root, [3.35, 0.16, 1.75], [0, -1.05, 0], 0x24282c);
  addBox(root, [2.85, 1.45, 1.25], [0, -0.2, 0], 0x3c4247);
  addBox(root, [0.7, 0.65, 1.3], [0.75, -0.18, 0], 0x202428);

  for (let i = -4; i <= 4; i += 1) {
    addBox(root, [0.045, 0.62, 1.31], [-0.72 + i * 0.1, -0.18, 0], 0x1e2225);
  }

  addBox(root, [0.48, 0.35, 0.05], [0.83, -0.13, 0.66], running ? COLORS.cyan : 0x607078, {
    emissive: running ? COLORS.cyan : 0x000000,
    emissiveIntensity: running ? 1.0 : 0,
  });

  addBox(root, [0.07, 0.86, 0.05], [-1.32, -0.1, 0.67], running ? COLORS.green : COLORS.orange, {
    emissive: running ? COLORS.green : COLORS.orange,
    emissiveIntensity: 0.55,
  });

  addCylinder(root, 0.16, 0.22, [-0.6, 0.64, 0], 0x24282b);
  addCylinder(root, 0.16, 0.22, [0.7, 0.64, 0], 0x24282b);
}

function buildTraffic(root, state) {
  const traffic = clamp(state?.telemetry?.traffic_load_pct ?? 0, 0, 100);

  addBox(root, [2.9, 0.14, 1.8], [0, -1.05, 0], 0x25292c);
  addCylinder(root, 0.1, 2.4, [0, 0, 0], 0x657079);

  for (const y of [-0.65, -0.2, 0.25, 0.7]) {
    addBox(root, [0.9, 0.06, 0.06], [0, y, 0], 0x657079);
  }
  for (const x of [-0.38, 0.38]) {
    addBox(root, [0.28, 0.8, 0.1], [x, 0.42, 0], 0xc0c5c8);
  }

  const ringCount = Math.max(1, Math.round(traffic / 22));
  for (let i = 0; i < ringCount; i += 1) {
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(0.75 + i * 0.25, 0.025, 8, 42, Math.PI * 1.15),
      material(COLORS.orange, {
        emissive: COLORS.orange,
        emissiveIntensity: 0.75,
        metalness: 0.1,
      }),
    );
    ring.rotation.y = Math.PI / 2;
    ring.rotation.z = Math.PI * 0.42;
    ring.position.y = 0.35;
    root.add(ring);

    const mirror = ring.clone();
    mirror.rotation.z = -Math.PI * 0.42;
    root.add(mirror);
  }
}

function buildBackhaul(root, state) {
  const status = String(state?.telemetry?.backhaul_status ?? "UNKNOWN").toUpperCase();
  const linkColor = status === "NORMAL" || status === "GOOD" ? COLORS.cyan : COLORS.red;

  addBox(root, [3.8, 0.12, 1.8], [0, -1.05, 0], 0x25292c);

  for (const x of [-1.2, 1.2]) {
    addCylinder(root, 0.06, 1.6, [x, -0.2, 0], 0x707981);

    const dish = new THREE.Mesh(
      new THREE.CylinderGeometry(0.78, 0.18, 0.22, 32),
      material(0xb9c1c7, { metalness: 0.55, roughness: 0.28 }),
    );
    dish.position.set(x, 0.38, 0);
    dish.rotation.z = Math.PI / 2;
    dish.rotation.y = x < 0 ? 0 : Math.PI;
    root.add(dish);
  }

  const points = [
    new THREE.Vector3(-0.75, 0.38, 0),
    new THREE.Vector3(0.75, 0.38, 0),
  ];
  root.add(
    new THREE.Line(
      new THREE.BufferGeometry().setFromPoints(points),
      new THREE.LineBasicMaterial({ color: linkColor, transparent: true, opacity: 0.95 }),
    ),
  );

  for (let x = -0.65; x <= 0.65; x += 0.22) {
    const dot = new THREE.Mesh(
      new THREE.SphereGeometry(0.035, 12, 12),
      material(linkColor, {
        emissive: linkColor,
        emissiveIntensity: 1.3,
        metalness: 0,
      }),
    );
    dot.position.set(x, 0.38, 0);
    root.add(dot);
  }
}

function buildPower(root, state) {
  const kw = clamp(state?.telemetry?.managed_power_kw ?? 0, 0, 3);

  addBox(root, [3.25, 0.14, 1.7], [0, -1.05, 0], 0x25292c);
  addBox(root, [2.3, 1.55, 0.9], [0, -0.1, 0], 0x32383d);
  addCylinder(root, 0.72, 0.16, [0, 0, 0.5], 0x202629, [Math.PI / 2, 0, 0]);

  const fraction = clamp(kw / 1.5, 0.03, 1);
  const ring = new THREE.Mesh(
    new THREE.TorusGeometry(0.58, 0.065, 10, 64, Math.PI * 2 * fraction),
    material(COLORS.orange, {
      emissive: COLORS.orange,
      emissiveIntensity: 0.75,
      metalness: 0.2,
    }),
  );
  ring.position.z = 0.62;
  ring.rotation.z = Math.PI / 2;
  root.add(ring);

  addBox(root, [0.92, 0.32, 0.06], [0, -0.03, 0.66], COLORS.cyan, {
    emissive: COLORS.cyan,
    emissiveIntensity: 0.75,
    metalness: 0.05,
  });

  for (const x of [-0.5, 0, 0.5]) {
    addCylinder(root, 0.08, 0.5, [x, 0.95, 0], 0x5b6369);
  }
}

function buildModel(kind, root, state) {
  if (kind === "battery") buildBattery(root, state);
  if (kind === "grid") buildGrid(root, state);
  if (kind === "generator") buildGenerator(root, state);
  if (kind === "traffic") buildTraffic(root, state);
  if (kind === "backhaul") buildBackhaul(root, state);
  if (kind === "power") buildPower(root, state);
}

function disposeObject(object) {
  object.traverse((node) => {
    node.geometry?.dispose?.();
    const mats = Array.isArray(node.material) ? node.material : [node.material];
    for (const mat of mats) mat?.dispose?.();
  });
}

function createView(card, kind) {
  const viewport = document.createElement("div");
  viewport.className = "three-card-viewport";
  viewport.dataset.model = kind;

  const hint = document.createElement("div");
  hint.className = "three-view-hint";
  hint.textContent = "drag to orbit · wheel to zoom";

  const reset = document.createElement("button");
  reset.type = "button";
  reset.className = "three-view-reset material-symbols-rounded";
  reset.setAttribute("aria-label", "Reset 3D view");
  reset.title = "Reset 3D view";
  reset.textContent = "center_focus_strong";

  const canvas = document.createElement("canvas");
  canvas.setAttribute("aria-label", kind + " interactive 3D model");

  viewport.append(canvas, hint, reset);
  card.append(viewport);
  card.classList.add("three-enabled");

  const { scene, root } = createBaseScene();
  buildModel(kind, root, dashboardState());
  root.rotation.y = -0.3;

  const camera = new THREE.PerspectiveCamera(38, 1, 0.1, 100);
  const startPosition = new THREE.Vector3(4.5, 2.8, 5.2);
  camera.position.copy(startPosition);

  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: true,
    alpha: true,
    powerPreference: "high-performance",
  });

  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.1;

  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = false;
  controls.enablePan = false;
  controls.minDistance = 3.2;
  controls.maxDistance = 9.5;
  controls.rotateSpeed = 0.65;
  controls.zoomSpeed = 0.75;
  controls.target.set(0, -0.05, 0);

  function renderView() {
    const rect = viewport.getBoundingClientRect();
    const width = Math.max(1, Math.floor(rect.width));
    const height = Math.max(1, Math.floor(rect.height));

    const neededWidth = Math.floor(width * renderer.getPixelRatio());
    const neededHeight = Math.floor(height * renderer.getPixelRatio());

    if (canvas.width !== neededWidth || canvas.height !== neededHeight) {
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
    }

    renderer.render(scene, camera);
  }

  controls.addEventListener("change", renderView);

  reset.addEventListener("click", () => {
    camera.position.copy(startPosition);
    controls.target.set(0, -0.05, 0);
    controls.update();
    renderView();
  });

  const resizeObserver = new ResizeObserver(renderView);
  resizeObserver.observe(viewport);

  renderView();

  views.set(card, {
    renderer,
    controls,
    resizeObserver,
    scene,
  });
}

function destroyAll() {
  for (const [card, view] of views) {
    view.resizeObserver.disconnect();
    view.controls.dispose();
    disposeObject(view.scene);
    view.renderer.dispose();
    view.renderer.forceContextLoss?.();
    card.classList.remove("three-enabled");
  }
  views.clear();
}

function decorateOverview() {
  if (window.ABSDashboardGetActiveTab?.() !== "overview") return;

  for (const card of document.querySelectorAll(".stat-card")) {
    const label = card.querySelector(".stat-label")?.textContent?.trim();
    const kind = MODEL_BY_LABEL.get(label);
    if (!kind || views.has(card)) continue;
    createView(card, kind);
  }
}

window.addEventListener("abs-dashboard-before-render", destroyAll);
window.addEventListener("abs-dashboard-rendered", () => {
  requestAnimationFrame(decorateOverview);
});

requestAnimationFrame(decorateOverview);
