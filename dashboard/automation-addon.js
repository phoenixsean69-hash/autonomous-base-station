/* ABS equipment-automation observer: read-only, simulation-labelled, no firmware control. */
(() => {
  'use strict';
  if (window.__ABS_AUTOMATION_ADDON__) return;
  window.__ABS_AUTOMATION_ADDON__ = true;

  const HOST = document.querySelector('.status-left') || document.querySelector('.app-menu-bar');
  if (!HOST) return;

  const ICONS = {
    pa: 'settings_input_antenna', spare: 'cell_tower', cool: 'mode_fan_off',
    backhaul: 'hub', generator: 'electric_bolt', battery: 'battery_charging_full',
    rectifier: 'power',
  };
  const FACTOR = {FULL: 100, ECO: 82, REDUCED: 65, EMERGENCY: 50};
  const EVENT_KEY = 'abs-automation-events-v1';
  const readEvents = () => {
    try {
      const value = JSON.parse(localStorage.getItem(EVENT_KEY) || '[]');
      return Array.isArray(value) ? value.slice(0, 30) : [];
    } catch { return []; }
  };
  const events = readEvents();
  let audioCtx = null;
  let audioEnabled = false;
  let previous = null;
  let coolingRequested = false;
  let upstreamSince = null;
  let lastAlarmAt = 0;
  let previousAlarmKeys = [];
  let drawerOpen = false;

  const h = (v) => String(v ?? '—').replace(/[&<>"']/g, c => ({
    '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'
  }[c]));
  const msIcon = (name) => `<span class="material-symbols-rounded" aria-hidden="true">${name}</span>`;
  const field = (t, key, fallback = null) => t && t[key] !== undefined && t[key] !== null ? t[key] : fallback;
  const numeric = (n) => n == null || n === '' ? NaN : Number(n);

  const toggle = document.createElement('button');
  toggle.id = 'absAutoToggle';
  toggle.type = 'button';
  toggle.className = 'abs-auto-status-btn';
  toggle.setAttribute('aria-label', 'Show equipment automation');
  toggle.setAttribute('aria-expanded', 'false');
  toggle.innerHTML = `${msIcon('precision_manufacturing')} <span>Equipment</span> <b id="absAutoAlarmCount">0</b>`;

  const sound = document.createElement('button');
  sound.id = 'absAutoSound';
  sound.type = 'button';
  sound.className = 'abs-auto-status-btn';
  sound.title = 'Sound is off until manually enabled. Browser may require a click to allow audio.';
  sound.setAttribute('aria-pressed', 'false');
  sound.innerHTML = `${msIcon('notifications_off')} <span>Sound OFF</span>`;
  HOST.append(toggle, sound);

  const drawer = document.createElement('section');
  drawer.id = 'absAutoDrawer';
  drawer.className = 'abs-auto-drawer';
  drawer.setAttribute('aria-label', 'Equipment automation monitor');
  drawer.setAttribute('aria-hidden', 'true');
  drawer.innerHTML = `
    <header class="abs-auto-header">
      <div><strong>${msIcon('precision_manufacturing')} Equipment Automation</strong>
      <small>Telemetry-derived actions · dashboard-only simulations</small></div>
      <button type="button" id="absAutoClose" aria-label="Close equipment automation">${msIcon('close')}</button>
    </header>
    <div class="abs-auto-scroll">
      <div id="absAutoHealth" class="abs-auto-health">Waiting for live telemetry</div>
      <div id="absAutoAlerts" class="abs-auto-alerts"></div>
      <div class="abs-auto-heading">EQUIPMENT DECISIONS <span>SIM ≠ PHYSICAL ACTUATION</span></div>
      <div id="absAutoEquipment" class="abs-auto-equipment"></div>
      <div class="abs-auto-heading">CONTROL FEEDBACK</div>
      <div id="absAutoContext" class="abs-auto-context"></div>
      <div class="abs-auto-heading">RECENT TRANSITIONS <span>LOCAL BROWSER HISTORY</span></div>
      <div id="absAutoHistory" class="abs-auto-history"></div>
      <footer class="abs-auto-note">Equipment action labels are simulations inferred from live controller telemetry. The generator status is sourced from ESP32 actuator feedback. This monitor sends no control commands.</footer>
    </div>`;
  document.body.appendChild(drawer);

  const setDrawerOpen = (next) => {
    drawerOpen = next;
    drawer.classList.toggle('open', drawerOpen);
    drawer.setAttribute('aria-hidden', String(!drawerOpen));
    toggle.setAttribute('aria-expanded', String(drawerOpen));
  };
  toggle.addEventListener('click', () => setDrawerOpen(!drawerOpen));
  drawer.querySelector('#absAutoClose').addEventListener('click', () => setDrawerOpen(false));
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && drawerOpen) setDrawerOpen(false); });

  function addEvent(level, name, description) {
    const item = {
      when: Date.now(), level: String(level).slice(0, 16),
      name: String(name).slice(0, 65), description: String(description).slice(0, 160)
    };
    events.unshift(item);
    if (events.length > 30) events.length = 30;
    try { localStorage.setItem(EVENT_KEY, JSON.stringify(events)); } catch { /* private browsing */ }
  }

  function beep() {
    if (!audioEnabled) return;
    try {
      const A = window.AudioContext || window.webkitAudioContext;
      if (!A) return;
      audioCtx ||= new A();
      if (audioCtx.state !== 'running') { void audioCtx.resume().catch(() => {}); return; }
      const base = audioCtx.currentTime;
      for (let i = 0; i < 3; i++) {
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        const start = base + i * 0.24;
        osc.type = 'square';
        osc.frequency.value = i === 1 ? 660 : 825;
        gain.gain.setValueAtTime(0.0001, start);
        gain.gain.exponentialRampToValueAtTime(0.028, start + 0.014);
        gain.gain.exponentialRampToValueAtTime(0.0001, start + 0.145);
        osc.connect(gain); gain.connect(audioCtx.destination);
        osc.start(start); osc.stop(start + 0.15);
      }
    } catch { /* audio permission unsupported */ }
  }
  sound.addEventListener('click', async () => {
    if (audioEnabled) {
      audioEnabled = false;
      sound.setAttribute('aria-pressed', 'false');
      sound.innerHTML = `${msIcon('notifications_off')} <span>Sound OFF</span>`;
      return;
    }
    try {
      const A = window.AudioContext || window.webkitAudioContext;
      if (!A) throw Error('Audio API unavailable');
      audioCtx ||= new A();
      await audioCtx.resume();
      audioEnabled = audioCtx.state === 'running';
      sound.setAttribute('aria-pressed', String(audioEnabled));
      sound.innerHTML = `${msIcon(audioEnabled ? 'notifications_active' : 'notifications_off')} <span>${audioEnabled ? 'Sound ON' : 'Sound blocked'}</span>`;
    } catch {
      audioEnabled = false;
      sound.innerHTML = `${msIcon('notifications_off')} <span>Sound blocked</span>`;
    }
  });

  function decisions(s) {
    const t = s?.telemetry || null;
    const meta = s?.meta || {};
    const reportedAge = numeric(meta.telemetryUpdatedMs);
    const actualAgeMs = Number.isFinite(reportedAge) ? Date.now() - reportedAge : Infinity;
    const live = !!t && meta.telemetryStatus === 'LIVE' && actualAgeMs >= -5000 && actualAgeMs < 20000;
    const bts = s?.bts || null;
    const btsFresh = !!bts && meta.btsPresenceAvailable === true && bts.hardware_connected === true && numeric(bts.hardware_age_ms) < 15000;
    if (!live) return {live:false, t, meta, btsFresh, rows:[], alarms:[], health:'PAUSED · ESP32 TELEMETRY NOT FRESH'};

    const pa = numeric(field(t,'pa_temp_c'));
    const shelter = numeric(field(t,'shelter_temp_c'));
    const load = numeric(field(t,'traffic_load_pct'));
    const soc = numeric(field(t,'battery_soc_pct'));
    const mode = String(field(t,'operating_mode','UNKNOWN')).toUpperCase();
    const rf = String(field(t,'rf_health',field(t,'rfHealth','UNKNOWN'))).toUpperCase();
    const electrical = String(field(t,'electrical_health','UNKNOWN')).toUpperCase();
    const networkCondition = String(field(t,'backhaul_condition','UNKNOWN')).toUpperCase();
    const upstreamBad = field(t,'upstream_reachable') === false || field(t,'physical_link_up') === false;
    const knownBts = field(t,'network_input_source') === 'BTS_BACKHAUL' && btsFresh;
    const fanFail = field(t,'fan_operational') === false;
    const rectifierFail = field(t,'rectifier_normal') === false;
    const radioFail = field(t,'radio_operational') === false;
    const rfFail = rf === 'FAULT' || rf === 'SEVERE FAULT';
    const gridDown = field(t,'grid_available') === false;
    const genRunning = field(t,'generator_feedback_running',field(t,'generator_running')) === true;
    const genCmd = field(t,'generator_output_active') === true;
    const genVerify = String(field(t,'generator_verification_state','UNKNOWN')).toUpperCase();
    const criticalTemp = Number.isFinite(pa) && pa >= 80;
    const hotTemp = Number.isFinite(pa) && pa >= 65;
    const batteryCritical = Number.isFinite(soc) && soc <= 25 && gridDown && !genRunning;
    const generatorFault = /FAILED|MISMATCH|TIMEOUT/.test(genVerify);

    // Hysteresis is a dashboard-only requested cooling state, NOT proof fan started.
    if ((Number.isFinite(pa) && pa >= 55) || (Number.isFinite(shelter) && shelter >= 38)) coolingRequested = true;
    if ((Number.isFinite(pa) && pa <= 45) && (!Number.isFinite(shelter) || shelter <= 32)) coolingRequested = false;

    if (upstreamBad && knownBts) upstreamSince ||= Date.now();
    else upstreamSince = null;
    const extendedUpstreamFault = upstreamSince !== null && Date.now() - upstreamSince >= 10000;

    const alarms = [];
    if (criticalTemp) alarms.push({key:'thermal', label:'CRITICAL PA TEMPERATURE'});
    if (fanFail) alarms.push({key:'cooling', label:'COOLING FAN FAILURE'});
    if (radioFail) alarms.push({key:'radio', label:'LOCAL RADIO EQUIPMENT FAULT'});
    if (rectifierFail || electrical === 'FAULT') alarms.push({key:'rectifier', label:'ELECTRICAL / RECTIFIER FAULT'});
    if (rfFail) alarms.push({key:'rf', label:'RF SYSTEM FAULT'});
    if (batteryCritical) alarms.push({key:'battery', label:'CRITICAL BACKUP ENERGY'});
    if (generatorFault) alarms.push({key:'generator', label:'GENERATOR VERIFICATION FAILED'});
    if (extendedUpstreamFault) alarms.push({key:'upstream', label:'UPSTREAM PATH FAILURE'});

    const rows = [
      {key:'pa', label:'RF Power Amplifier', basis:'PA temperature and applied operating mode',
       state: criticalTemp || hotTemp || rfFail ? 'DERATE REQUESTED · SIM' : upstreamBad && knownBts && mode === 'ECO' ? 'ECO LOAD TARGET · SIM' : 'NORMAL TARGET · SIM',
       level: criticalTemp || rfFail ? 'danger' : hotTemp || (upstreamBad && knownBts) ? 'warn' : 'ok'},
      {key:'spare', label:'Spare Radio Capacity', basis:'Traffic, active calls, and available mode',
       state: radioFail ? 'LOCAL RADIO FAULT · HOLD' : Number.isFinite(load) && load < 20 && Number(field(t,'network_active_calls',0)) === 0 && mode !== 'FULL' ? 'SLEEP REQUESTED · SIM' : 'CAPACITY PRESERVED · SIM',
       level: radioFail ? 'danger' : 'ok'},
      {key:'cool', label:'Automatic Cooling', basis:'PA / shelter temperature; fan feedback separate',
       state: fanFail ? 'FAN FAILED · REQUEST UNSATISFIED' : coolingRequested ? 'FAN ON REQUEST · SIM' : 'FAN DEMAND OFF · SIM',
       level: fanFail ? 'danger' : coolingRequested ? 'warn' : 'ok'},
      {key:'backhaul', label:'Backhaul Recovery', basis:'Network source and upstream telemetry',
       state: !knownBts ? 'BTS FEED UNVERIFIED' : upstreamBad ? 'RECOVERY MONITORING · SIM' : 'UPSTREAM REACHABLE',
       level: !knownBts ? 'warn' : upstreamBad ? 'warn' : 'ok'},
      {key:'generator', label:'Generator / Power Switch', basis:'ESP32 output + independent feedback',
       state: generatorFault ? `VERIFY FAILED · ${genVerify}` : genRunning && genCmd && genVerify === 'RUNNING_CONFIRMED' ? 'RUNNING · FEEDBACK VERIFIED' : genRunning ? 'RUNNING · FEEDBACK ACTIVE' : genCmd ? 'START REQUEST · AWAITING FEEDBACK' : genVerify === 'STOPPED_CONFIRMED' ? 'STOPPED · VERIFIED' : 'STOP COMMAND · VERIFICATION PENDING',
       level: generatorFault ? 'danger' : genCmd !== genRunning ? 'warn' : 'ok'},
      {key:'battery', label:'Battery Protection', basis:'Grid status and state of charge',
       state: batteryCritical ? 'CRITICAL BACKUP PROTECTION' : gridDown ? `BACKUP ACTIVE · ${Number.isFinite(soc) ? soc.toFixed(1) : '—'}%` : 'GRID SUPPLY AVAILABLE',
       level: batteryCritical ? 'danger' : gridDown ? 'warn' : 'ok'},
      {key:'rectifier', label:'Rectifier Protection', basis:'Rectifier and electrical health inputs',
       state: rectifierFail || electrical === 'FAULT' ? 'FAULT · LOAD PROTECTION ACTIVE' : 'NORMAL · MONITORED',
       level: rectifierFail || electrical === 'FAULT' ? 'danger' : 'ok'}
    ];
    const saving = Object.hasOwn(FACTOR, mode) ? 100-FACTOR[mode] : null;
    return {live, t, meta, btsFresh, rows, alarms, health:`LIVE · ${mode} · ${saving == null ? 'NO ENERGY MODEL' : saving+'% MODELLED SAVING'}`, saving, upstreamBad, knownBts};
  }

  function updateHistory(d) {
    if (!d.live) {
      previous = null;
      previousAlarmKeys = [];
      lastAlarmAt = 0;
      return;
    }
    const current = Object.fromEntries(d.rows.map(row => [row.key, row.state]));
    const currentAlarmKeys = d.alarms.map(alarm => alarm.key);
    if (previous) {
      for (const row of d.rows) {
        if (previous[row.key] !== row.state) addEvent(row.level, row.label, `${previous[row.key]} → ${row.state}`);
      }
    }
    const freshKeys = currentAlarmKeys.filter(key => !previousAlarmKeys.includes(key));
    for (const alarm of d.alarms) if (freshKeys.includes(alarm.key)) addEvent('danger', 'CRITICAL ALARM', alarm.label);
    if (d.alarms.length && audioEnabled && (freshKeys.length || Date.now() - lastAlarmAt > 30000)) {
      beep(); lastAlarmAt = Date.now();
    }
    if (d.alarms.length === 0) lastAlarmAt = 0;
    previousAlarmKeys = currentAlarmKeys;
    previous = current;
  }

  function paint() {
    const getState = window.ABSDashboardGetState;
    const d = decisions(typeof getState === 'function' ? getState() : null);
    updateHistory(d);
    const alertCount = d.alarms.length;
    toggle.classList.toggle('abs-auto-critical', alertCount > 0);
    toggle.querySelector('b').textContent = String(alertCount);
    toggle.title = alertCount ? `${alertCount} serious fault(s) detected` : 'Open equipment automation monitor';
    drawer.querySelector('#absAutoHealth').textContent = d.health;
    drawer.querySelector('#absAutoHealth').className = `abs-auto-health ${d.live ? 'live' : 'stale'}`;
    drawer.querySelector('#absAutoAlerts').innerHTML = !d.live
      ? '<div class="abs-auto-warning">Alerts suspended while ESP32 telemetry is stale. Last-known data is not treated as live.</div>'
      : alertCount
        ? `<div class="abs-auto-warning critical">${msIcon('warning')} ${alertCount} serious fault${alertCount > 1 ? 's' : ''} detected · ${audioEnabled ? 'audible alerts armed' : 'audible alerts muted'}</div>`+
          d.alarms.map(a => `<div class="abs-auto-alarm">${h(a.label)}</div>`).join('')
        : `<div class="abs-auto-allclear">${msIcon('verified')} No serious faults detected · ${audioEnabled ? 'sound armed' : 'sound muted'}</div>`;
    drawer.querySelector('#absAutoEquipment').innerHTML = d.live
      ? d.rows.map(row => `<article class="abs-auto-row"><span class="abs-auto-row-icon">${msIcon(ICONS[row.key])}</span><div class="abs-auto-row-main"><strong>${h(row.label)}</strong><small>${h(row.basis)}</small><span class="abs-auto-decision ${h(row.level)}">${h(row.state)}</span></div></article>`).join('')
      : '<p class="abs-auto-empty">Awaiting fresh ESP32 telemetry. Simulated equipment commands are withheld.</p>';
    const t = d.t || {};
    drawer.querySelector('#absAutoContext').innerHTML = d.live
      ? [
          ['Operating mode', field(t,'operating_mode')],
          ['Guardrail', field(t,'guardrail_status')],
          ['Network source', field(t,'network_input_source')],
          ['Pico decision', field(t,'pico_decision_status')],
          ['Pico fresh', field(t,'pico_decision_fresh') === true ? 'YES' : 'NO'],
          ['Generator feedback', field(t,'generator_verification_state')],
          ['Simulated baseline saving', d.saving == null ? '—' : d.saving + '% (MODEL)']
        ].map(([name, value]) => `<div><span>${h(name)}</span><b>${h(value)}</b></div>`).join('')
      : '<p class="abs-auto-empty">Controller context unavailable.</p>';
    drawer.querySelector('#absAutoHistory').innerHTML = events.length
      ? events.slice(0, 12).map(e => `<div class="abs-auto-history-row"><time>${h(new Date(e.when).toLocaleTimeString())}</time><div><b>${h(e.name)}</b><small>${h(e.description)}</small></div></div>`).join('')
      : '<p class="abs-auto-empty">Watching for changes. No transitions recorded in this browser yet.</p>';
  }
  paint();
  setInterval(paint, 1000);
})();
