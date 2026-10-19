/* ABS Control Center — compact Fault Operations View, local-only v2.
 * Replaces prior oversized Fault Map skin. Works while simulators are stopped.
 */
(() => {
  'use strict';

  const numbers = ['0712000001','0712000002','0712000003','0712000004','0712000005','0712000006'];
  const safe = x => String(x ?? '—').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const norm = x => String(x ?? '').replace(/\D/g,'');
  const phoneText = x => norm(x).replace(/^(\d{4})(\d{3})(\d{3})$/, '$1 $2 $3');
  const bool = (v, yes='UP', no='DOWN') => v === true ? yes : v === false ? no : 'UNKNOWN';
  const val = (x,d=1) => x == null || !Number.isFinite(Number(x)) ? '—' : Number(x).toFixed(d);
  const state = () => window.ABSDashboardGetState?.() ?? {};
  const ATTR = [
    {id:'phones', icon:'smartphone',label:'Subscriber service',tag:'01 / ACCESS'},
    {id:'bts',icon:'cell_tower',label:'BTS-001',tag:'02 / RADIO ACCESS'},
    {id:'link',icon:'router',label:'Physical backhaul',tag:'03 / TRANSPORT'},
    {id:'local',icon:'settings_input_component',label:'Local equipment',tag:'04 / FAULT DIMENSION B'},
    {id:'ai',icon:'neurology',label:'Dual AI diagnosis',tag:'05 / INFERENCE'},
    {id:'upstream',icon:'public',label:'Upstream network',tag:'04 / FAULT DIMENSION A'},
    {id:'power',icon:'electric_bolt',label:'Power supply',tag:'06 / ENERGY'},
    {id:'control',icon:'shield',label:'ESP32 guardrails',tag:'07 / FINAL AUTHORITY'},
  ];
  const EDGES = [
    ['phones','bts','signal'],
    ['bts','link','signal'],
    ['link','upstream','signal'],
    ['bts','local','telemetry'],
    ['local','ai','telemetry'],
    ['upstream','ai','telemetry'],
    ['ai','control','control'],
    ['power','control','power']
  ];
  let selected = 'upstream';
  let lastRefresh = 0;
  let refreshScheduled = 0;
  let lastInspector = '';

  function data() {
    const s=state();
    const btsLive=s.meta?.btsPresenceAvailable === true && !!s.bts;
    const espLive=s.meta?.telemetryStatus === 'LIVE' && !!s.telemetry;
    const aiLive=s.meta?.aiFresh === true && !!s.ai;
    const b=btsLive?s.bts:{};
    const t=espLive?s.telemetry:{};
    const a=aiLive?s.ai:{};
    const cmd=a.runtime?.ai_command??{};
    const pico=a.runtime?.pico_decision??{};
    const online=new Set(Array.isArray(b.subscriber_numbers)?b.subscriber_numbers.map(norm):[]);
    const pairs=Array.isArray(b.active_call_pairs)?b.active_call_pairs.filter(c=>['RINGING','CONNECTED'].includes(String(c?.state??'').toUpperCase())):[];
    const calling=new Set(pairs.flatMap(c=>[norm(c.caller),norm(c.callee)]));
    const totalCalls=btsLive?Number(b.active_calls??pairs.length):null;
    const backhaulFault=btsLive && (b.link_up===false || b.upstream_reachable===false);
    const upstreamCause=!btsLive?'No live link reading':b.link_up===false?'PHYSICAL LINK FAILURE':b.upstream_reachable===false?'UPSTREAM UNREACHABLE':'PATH REACHABLE';
    const localRaw=String(t.local_site_status??'').toUpperCase();
    const localFault=espLive&&['FAULT','DEGRADED'].includes(localRaw);
    const localSignal=espLive?(localRaw==='FAULT'?'fault':localRaw==='DEGRADED'?'warn':localRaw==='NORMAL'?'ok':'unknown'):(btsLive&&b.radio_operational===false?'fault':'unknown');
    const upstreamSignal=!btsLive?'unknown':backhaulFault?'fault':'ok';
    const btsSignal=!btsLive?'unknown':b.radio_operational===false?'fault':b.radio_operational===true?'ok':'unknown';
    const pair=(k,v)=>[k, String(v??'—')];
    const items={
      phones: {
        status:!btsLive?'unknown':online.size?'ok':'idle',
        main:btsLive?`${online.size} ONLINE / 6`:'FEED UNAVAILABLE',
        secondary:btsLive?`${totalCalls} active call${totalCalls===1?'':'s'} · signalling`:'Registration status unknown',
        description:'BTS registration and call signalling. Phone audio is direct local-network WebRTC.',
        rows:[pair('BTS presence',btsLive?'LIVE':'UNAVAILABLE'),pair('Registered',btsLive?online.size:'—'),pair('Active calls',totalCalls),pair('Voice media','Direct WebRTC / local Wi-Fi')]
      },
      bts:{
        status:btsSignal,
        main:!btsLive?'SERVICE OFFLINE':b.radio_operational===false?'RADIO FAULT':'RADIO ONLINE',
        secondary:btsLive?`Service :8100 · ${val(b.traffic_load_pct)}% load`:'Service :8100 · no live data',
        description:'BTS-001 service, hardware freshness and radio status.',
        rows:[pair('BTS service',btsLive?'REACHABLE':'OFFLINE'),pair('Hardware feed',btsLive?bool(b.hardware_connected,'LIVE','STALE'):'—'),pair('Radio',btsLive?bool(b.radio_operational,'OPERATIONAL','FAULT'):'—'),pair('Traffic',btsLive?`${val(b.traffic_load_pct)}%`:'—'),pair('Calls',totalCalls)]
      },
      link:{
        status:!btsLive?'unknown':b.link_up===false?'fault':b.link_up===true?'ok':'unknown',
        main:!btsLive?'NO TELEMETRY':bool(b.link_up,'LINK UP','LINK DOWN'),
        secondary:!btsLive?'BTS feed unavailable':b.link_up===false?'Cable/link failure':'Physical transport connected',
        description:'Physical link state is distinct from upstream reachability.',
        rows:[pair('Physical link',btsLive?bool(b.link_up):'—'),pair('Remote network',btsLive?bool(b.upstream_reachable,'REACHABLE','UNREACHABLE'):'—'),pair('Rule cause',espLive?t.backhaul_rule_cause:'—'),pair('Latency',btsLive?`${val(b.latency_ms)} ms`:'—')]
      },
      local:{
        status:localSignal,
        main:!espLive?(btsLive&&b.radio_operational===false?'BTS RADIO FAULT':'MONITOR OFFLINE'):`LOCAL ${localRaw||'UNKNOWN'}`,
        secondary:espLive?`RF ${t.rf_health??'—'} · PA ${val(t.pa_temp_c)}°C`:'ESP32 telemetry unavailable',
        description:'Local RF, radio, cooling, rectifier, grid, battery and mechanical faults.',
        rows:[pair('Local site',espLive?t.local_site_status:'—'),pair('Radio',espLive?bool(t.radio_operational,'OK','FAULT'):btsLive?bool(b.radio_operational,'OK','FAULT'):'—'),pair('Cooling fan',espLive?bool(t.fan_operational,'OK','FAILED'):'—'),pair('Rectifier',espLive?bool(t.rectifier_normal,'OK','FAILED'):'—'),pair('RF health',espLive?t.rf_health:'—'),pair('PA temperature',espLive?`${val(t.pa_temp_c)} °C`:'—')]
      },
      ai:{
        status:!aiLive?'unknown':cmd.dual_ai_agreement==='DISAGREE'?'warn':a.trust?.decision==='ACCEPT'?'ok':'warn',
        main:!aiLive?'INFERENCE OFFLINE':`${a.fault_domain?.label??'UNKNOWN'} / ${cmd.dual_ai_agreement??'WAIT'}`,
        secondary:aiLive?`Trust ${a.trust?.decision??'WAIT'} · ${val(Number(a.fault_domain?.confidence)*100)}% raw`:'Laptop + Pico results unavailable',
        description:'Laptop temporal classifier, independent Pico model and domain trust gate.',
        rows:[pair('Laptop domain',aiLive?a.fault_domain?.label:'—'),pair('Pico domain',aiLive?cmd.pico_ai_fault_domain:'—'),pair('Agreement',aiLive?cmd.dual_ai_agreement:'—'),pair('Trust',aiLive?a.trust?.decision:'—'),pair('Upstream cause',aiLive?a.root_cause?.upstream?.label??'N/A':'—'),pair('Local cause',aiLive?a.root_cause?.local?.label??'N/A':'—')]
      },
      upstream:{
        status:upstreamSignal,
        main:!btsLive?'NO LIVE READING':upstreamCause,
        secondary:btsLive?`Link ${bool(b.link_up)} · Remote ${bool(b.upstream_reachable,'UP','DOWN')}`:'Reachability unavailable',
        description:'Independent upstream fault dimension: link failure versus remote outage.',
        rows:[pair('Physical link',btsLive?bool(b.link_up):'—'),pair('Remote reachable',btsLive?bool(b.upstream_reachable,'YES','NO'):'—'),pair('Backhaul available',btsLive?bool(b.backhaul_available,'YES','NO'):'—'),pair('Packet loss',btsLive?`${val(b.packet_loss_pct)}%`:'—'),pair('Latency',btsLive?`${val(b.latency_ms)} ms`:'—'),pair('ESP32 fault',espLive?t.fault_label:'—')]
      },
      power:{
        status:!espLive?'unknown':t.grid_available===false?'warn':Number(t.battery_soc_pct)<25?'warn':'ok',
        main:!espLive?'POWER DATA OFFLINE':String(t.active_power_source??'UNKNOWN').toUpperCase(),
        secondary:espLive?`Battery ${val(t.battery_soc_pct)}% · Gen ${t.generator_feedback_running===true?'ON':'OFF'}`:'Grid / battery / generator status unavailable',
        description:'Source selection, battery reserve and independently confirmed generator state.',
        rows:[pair('Grid',espLive?bool(t.grid_available,'AVAILABLE','FAILED'):'—'),pair('Battery',espLive?`${val(t.battery_soc_pct)}%`:'—'),pair('Source',espLive?t.active_power_source:'—'),pair('Generator feedback',espLive?bool(t.generator_feedback_running,'RUNNING','STOPPED'):'—'),pair('Verification',espLive?t.generator_verification_state:'—')]
      },
      control:{
        status:!espLive?'unknown':String(t.guardrail_status??'').includes('FAULT')?'fault':t.guardrail_status==='PASSED'?'ok':'warn',
        main:!espLive?'CONTROLLER OFFLINE':`${t.operating_mode??'—'} · ${t.guardrail_status??'—'}`,
        secondary:espLive?`Power ${t.active_power_source??'—'} · Final safety authority`:'ESP32 :4001 · no fresh telemetry',
        description:'The ESP32 determines final equipment protection and operating mode.',
        rows:[pair('Requested',espLive?t.requested_mode:'—'),pair('Final mode',espLive?t.operating_mode:'—'),pair('Guardrail',espLive?t.guardrail_status:'—'),pair('Source',espLive?t.active_power_source:'—'),pair('Recovery',espLive?t.recovery_state:'—'),pair('Pico decision',aiLive?pico.mode_decision:'—')]
      }
    };
    return {btsLive,espLive,aiLive,b,t,a,cmd,items,online,calling,pairs,totalCalls};
  }

  function nodeHtml(id,d){
    const meta=ATTR.find(n=>n.id===id);
    return `<button class="fo-device fo-device-${id} fo-state-${d.status} ${selected===id?'is-selected':''}" type="button" data-fo-node="${id}" data-fo-status="${d.status}" aria-pressed="${selected===id}">
      <span class="fo-device-top"><span class="fo-device-tag">${safe(meta.tag)}</span><i class="fo-indicator"></i></span>
      <span class="fo-device-middle"><span class="material-symbols-rounded fo-device-icon">${safe(meta.icon)}</span><span class="fo-device-title">${safe(meta.label)}</span></span>
      <strong class="fo-device-main" data-fo-main>${safe(d.main)}</strong>
      <span class="fo-device-sub" data-fo-sub>${safe(d.secondary)}</span>
    </button>`;
  }

  function subscribers(p){
    return `<div class="fo-subscriber-grid">${numbers.map(n=>{
      const status=!p.btsLive?'unknown':p.calling.has(n)?'call':p.online.has(n)?'ok':'idle';
      const label=!p.btsLive?'—':p.calling.has(n)?'CALL':p.online.has(n)?'ON':'OFF';
      return `<div class="fo-phone fo-state-${status}" data-fo-phone="${n}"><i class="fo-indicator"></i><span>${phoneText(n)}</span><b>${label}</b></div>`;
    }).join('')}</div>`;
  }

  function inspectorHtml(p){
    const m=ATTR.find(x=>x.id===selected)??ATTR[0];
    const d=p.items[selected]??p.items.phones;
    return `<div class="fo-inspector-ident"><span class="material-symbols-rounded">${safe(m.icon)}</span><div><small>${safe(m.tag)}</small><strong>${safe(m.label)}</strong></div><i class="fo-indicator fo-state-${d.status}"></i></div>
      <div class="fo-inspector-feature fo-state-${d.status}"><span>CONDITION</span><strong>${safe(d.main)}</strong><p>${safe(d.description)}</p></div>
      <div class="fo-inspector-section">LIVE EVIDENCE</div>
      <div class="fo-inspector-rows">${d.rows.map(([k,v])=>`<div><span>${safe(k)}</span><strong>${safe(v)}</strong></div>`).join('')}</div>
      <p class="fo-inspector-note">An unavailable telemetry source is shown as unknown, not as a confirmed failure.</p>`;
  }

  function initialMarkup(){
    const p=data();
    const devices=p.items;
    const status=(key,yes)=>`<span data-fo-source="${key}" class="fo-uptime fo-state-${p[key]?'ok':'unknown'}"><i class="fo-indicator"></i>${yes} ${p[key]?'LIVE':'OFFLINE'}</span>`;
    return `<div class="fo-page" id="fo-page">
      <div class="fo-bar"><div class="fo-bar-label"><span class="fo-bar-mark material-symbols-rounded">account_tree</span><strong>Fault paths</strong><span class="fo-bar-note">BTS-001 / LOCAL + UPSTREAM DIAGNOSIS</span></div>
      <div class="fo-toolbar-right">${status('btsLive','BTS')}${status('espLive','ESP32')}${status('aiLive','AI')}</div></div>
      <div class="fo-workspace">
      <div class="fo-canvas">
      <div class="fo-canvas-top"><span><i></i> LIVE FAULT TOPOLOGY</span><span>READ ONLY · SELECT AN EQUIPMENT NODE</span></div>
      <div class="fo-stage" id="fo-stage">
      <svg class="fo-lines" aria-hidden="true"></svg>
      <div class="fo-device-wrap fo-pos-phones"><div class="fo-phone-unit"><div class="fo-phone-heading" data-fo-node="phones" role="button" tabindex="0"><span>SUBSCRIBER SERVICE</span><b data-fo-subscriber-summary>—</b></div>${subscribers(p)}<div class="fo-phone-footer"><span>WebRTC / local Wi-Fi media</span><span data-fo-call-count>—</span></div></div></div>
      <div class="fo-device-wrap fo-pos-bts">${nodeHtml('bts',devices.bts)}</div>
      <div class="fo-device-wrap fo-pos-link">${nodeHtml('link',devices.link)}</div>
      <div class="fo-device-wrap fo-pos-local">${nodeHtml('local',devices.local)}</div>
      <div class="fo-device-wrap fo-pos-ai">${nodeHtml('ai',devices.ai)}</div>
      <div class="fo-device-wrap fo-pos-upstream">${nodeHtml('upstream',devices.upstream)}</div>
      <div class="fo-device-wrap fo-pos-power">${nodeHtml('power',devices.power)}</div>
      <div class="fo-device-wrap fo-pos-control">${nodeHtml('control',devices.control)}</div>
      <div class="fo-device-wrap fo-pos-readout"><div class="fo-summary-plate"><span>FAULT DOMAINS</span><strong data-fo-summary>Waiting for live telemetry</strong><small data-fo-summary-details>Two independent diagnosis paths</small></div></div>
      </div>
      <div class="fo-canvas-bottom"><span><i class="fo-legend-ok"></i>Healthy</span><span><i class="fo-legend-fault"></i>Fault</span><span><i class="fo-legend-unknown"></i>Unavailable</span><span class="fo-canvas-help">Transport telemetry is separate from direct WebRTC voice media.</span></div>
      </div>
      <aside class="fo-inspector"><div class="fo-inspector-head"><span>FAULT INSPECTOR</span><small>SELECTION</small></div><div data-fo-inspector>${inspectorHtml(p)}</div></aside>
      </div></div>`;
  }

  window.renderTelemetry=initialMarkup;

  function linePath(start,end,direction='horizontal'){
    const dx=end.x-start.x,dy=end.y-start.y;
    if(direction==='vertical'){
      const k=Math.max(18,Math.min(62,Math.abs(dy)*.40));
      return `M${start.x},${start.y} C${start.x},${start.y+k} ${end.x},${end.y-k} ${end.x},${end.y}`;
    }
    const k=Math.max(15,Math.min(64,Math.abs(dx)*.38));
    return `M${start.x},${start.y} C${start.x+(dx>=0?k:-k)},${start.y} ${end.x-(dx>=0?k:-k)},${end.y} ${end.x},${end.y}`;
  }
  function wireDraw(){
    const root=document.getElementById('fo-page');
    const stage=root?.querySelector('.fo-stage'),svg=stage?.querySelector('.fo-lines');
    if(!svg||!stage?.isConnected)return;
    const rect=stage.getBoundingClientRect();
    if(rect.width<100||rect.height<100)return;
    svg.setAttribute('viewBox',`0 0 ${rect.width} ${rect.height}`);
    let paths='';
    const p=data();
    for(const [src,dst,kind] of EDGES){
      const aa=stage.querySelector(src==='phones'?'.fo-phone-unit':`.fo-device-${src}`);
      const bb=stage.querySelector(`.fo-device-${dst}`);
      if(!aa||!bb)continue;
      const a=aa.getBoundingClientRect(),b=bb.getBoundingClientRect();
      const topToBottom=b.top-a.bottom>4 && Math.abs(b.left-a.left)<Math.max(a.width,b.width)*.50;
      const start=topToBottom?{x:a.left+a.width*.5-rect.left,y:a.bottom-rect.top}:{x:(a.right<b.left?a.right:a.left)-rect.left,y:(a.top+a.height*.5)-rect.top};
      const end=topToBottom?{x:b.left+b.width*.5-rect.left,y:b.top-rect.top}:{x:(a.right<b.left?b.left:b.right)-rect.left,y:(b.top+b.height*.5)-rect.top};
      let signal='unknown';
      if(p.items[src]&&p.items[dst])signal=[p.items[src].status,p.items[dst].status].includes('fault')?'fault':[p.items[src].status,p.items[dst].status].includes('unknown')?'unknown':'ok';
      if(src==='phones')signal=!p.btsLive?'unknown':p.items.bts.status;
      paths+=`<path class="fo-line fo-line-${kind} fo-line-${signal}" d="${linePath(start,end,topToBottom?'vertical':'horizontal')}" />`;
    }
    svg.innerHTML=paths;
  }

  function refreshMap(force=false){
    const root=document.getElementById('fo-page');
    if(!root)return;
    const p=data();
    const status=(key,suffix)=>{
      const el=root.querySelector(`.fo-uptime[data-fo-source="${key}"]`);
      if(!el)return;
      el.className=`fo-uptime fo-state-${p[key]?'ok':'unknown'}`;
      el.innerHTML=`<i class="fo-indicator"></i>${suffix} ${p[key]?'LIVE':'OFFLINE'}`;
    };
    status('btsLive','BTS');status('espLive','ESP32');status('aiLive','AI');
    const h=root.querySelector('[data-fo-subscriber-summary]');if(h)h.textContent=p.btsLive?`${p.online.size}/6 ONLINE`:'FEED OFFLINE';
    const c=root.querySelector('[data-fo-call-count]');if(c)c.textContent=p.btsLive?`${p.totalCalls} CALL${p.totalCalls===1?'':'S'}`:'NO DATA';
    for(const n of numbers){
      const el=root.querySelector(`[data-fo-phone="${n}"]`);if(!el)continue;
      const st=!p.btsLive?'unknown':p.calling.has(n)?'call':p.online.has(n)?'ok':'idle';
      el.className=`fo-phone fo-state-${st}`;
      const b=el.querySelector('b');if(b)b.textContent=st==='unknown'?'—':st==='call'?'CALL':st==='ok'?'ON':'OFF';
    }
    for(const m of ATTR){
      if(m.id==='phones')continue;
      const d=p.items[m.id],el=root.querySelector(`.fo-device-${m.id}`);
      if(!el)continue;
      el.className=`fo-device fo-device-${m.id} fo-state-${d.status}${selected===m.id?' is-selected':''}`;
      el.dataset.foStatus=d.status;
      el.setAttribute('aria-pressed',selected===m.id?'true':'false');
      const mv=el.querySelector('[data-fo-main]'),sv=el.querySelector('[data-fo-sub]');
      if(mv && mv.textContent!==d.main)mv.textContent=d.main;
      if(sv && sv.textContent!==d.secondary)sv.textContent=d.secondary;
    }
    const sum=root.querySelector('[data-fo-summary]');
    const explanation=root.querySelector('[data-fo-summary-details]');
    if(sum&&explanation){
      sum.textContent=!p.btsLive&&!p.espLive?'NO ACTIVE TELEMETRY':p.items.upstream.status==='fault'&&p.items.local.status==='fault'?'MIXED FAULT':p.items.upstream.status==='fault'?'UPSTREAM FAULT':p.items.local.status==='fault'?'LOCAL FAULT':p.espLive&&p.btsLive?'MONITORING':'PARTIAL FEED';
      explanation.textContent=!p.btsLive&&!p.espLive?'Awaiting BTS-001 and ESP32':p.aiLive?`AI ${p.a.fault_domain?.label??'UNKNOWN'} · ${p.cmd.dual_ai_agreement??'WAITING'}`:'AI classification unavailable';
    }
    const ins=root.querySelector('[data-fo-inspector]');
    const newInspector=inspectorHtml(p);
    if(ins && (force||lastInspector!==newInspector)){
      ins.innerHTML=newInspector;
      lastInspector=newInspector;
    }
    const phoneUnit=root.querySelector('.fo-phone-unit');
    if(phoneUnit) phoneUnit.classList.toggle('is-selected',selected==='phones');
    wireDraw();
  }
  function scheduleRefresh(){
    if(window.ABSDashboardGetActiveTab?.()!=='telemetry')return;
    const time=performance.now();
    if(time-lastRefresh<400){
      if(refreshScheduled)return;
      refreshScheduled=window.setTimeout(()=>{refreshScheduled=0;lastRefresh=performance.now();refreshMap();},440-(time-lastRefresh));
    }else{lastRefresh=time;window.requestAnimationFrame(()=>refreshMap());}
  }
  // Static shell is inserted by app.js only when entering the Fault Map.
  // Subsequent telemetry changes patch values without rebuilding the page.
  document.addEventListener('click',ev=>{
    const node=ev.target.closest?.('[data-fo-node]');
    if(!node)return;
    if(!document.getElementById('fo-page')?.contains(node))return;
    selected=node.dataset.foNode;
    refreshMap(true);
  });
  document.addEventListener('keydown',ev=>{
    if(!['Enter',' '].includes(ev.key))return;
    const el=ev.target.closest?.('.fo-phone-heading');
    if(el){ev.preventDefault();selected='phones';refreshMap(true);}
  });
  window.addEventListener('abs-dashboard-rendered', scheduleRefresh);
  window.addEventListener('resize',()=>window.requestAnimationFrame(wireDraw));
  if(window.ABSDashboardGetActiveTab?.()==='telemetry'&&typeof window.render==='function')window.render();
})();
