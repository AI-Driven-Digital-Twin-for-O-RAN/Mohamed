let _net, _cfg, _callbacks;

// per-cell waveform phase
let wavePhase = [];
// throughput history (32 bars)
const thrHistory = Array(32).fill(0.4);

// ── Color helpers ─────────────────────────────────────────────
function loadColor(load) {
  return load < _cfg.LOAD_GREEN  ? '#00ff88'
       : load < _cfg.LOAD_YELLOW ? '#ffcc00'
       : load < _cfg.LOAD_ORANGE ? '#ff6600'
       : '#ff2244';
}
function loadLabel(load) {
  return load < _cfg.LOAD_GREEN  ? 'LOW'
       : load < _cfg.LOAD_YELLOW ? 'MED'
       : load < _cfg.LOAD_ORANGE ? 'HIGH'
       : 'CRIT';
}

// ── Waveform ──────────────────────────────────────────────────
function drawWave(canvas, ti, color) {
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W   = canvas.offsetWidth || 200, H = canvas.height || 22;
  canvas.width = W;
  ctx.clearRect(0, 0, W, H);
  ctx.beginPath();
  for (let x = 0; x <= W; x++) {
    const ld = _net.cells[ti].load;
    const y  = H / 2
      - Math.sin((x / W) * Math.PI * 6 + wavePhase[ti]) * H * (.2 + ld * .3)
      - Math.sin((x / W) * Math.PI * 10 + wavePhase[ti] * 1.3) * H * .08;
    x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  }
  ctx.strokeStyle = color; ctx.lineWidth = 1.5; ctx.stroke();
  wavePhase[ti] += .06;
}

// ── Throughput chart ──────────────────────────────────────────
function drawThrChart() {
  const canvas = document.getElementById('thr-chart');
  if (!canvas) return;
  const W = canvas.offsetWidth || 240, H = 36;
  canvas.width = W;
  const ctx  = canvas.getContext('2d');
  ctx.clearRect(0, 0, W, H);
  const totalBars = 32, bw = W / totalBars - 1;
  // push latest avg throughput
  const avgThr = _net.cells.reduce((s, c) => s + (c.thr || 2), 0) / _net.cells.length;
  thrHistory.push(avgThr / 5); // normalise to 0-1 range (max ~5 Gbps)
  if (thrHistory.length > totalBars) thrHistory.shift();
  thrHistory.forEach((v, i) => {
    const height = Math.max(2, v * H);
    const col    = i === thrHistory.length - 1
      ? loadColor(_net.cells.reduce((a, c) => c.load > a ? c.load : a, 0))
      : 'rgba(0,200,255,0.4)';
    ctx.fillStyle = col;
    ctx.fillRect(i * (bw + 1), H - height, bw, height);
  });
}

// ── Cell cards ────────────────────────────────────────────────
function buildCellCards() {
  const cc = document.getElementById('cell-cards');
  cc.innerHTML = '';
  _net.cells.forEach((_, ti) => {
    const card = document.createElement('div');
    card.className = 'cell-card'; card.id = 'cc' + ti;
    card.innerHTML =
      `<div class="cc-top"><div class="cc-id" id="ccid${ti}"></div><div class="cc-status" id="ccst${ti}"></div></div>
       <div class="cc-load-lbl">CELL LOAD</div>
       <div class="cc-bar-track"><div class="cc-bar-fill" id="ccbar${ti}"></div></div>
       <div class="cc-stats">
         <div class="cc-stat"><div class="cc-stat-v" id="ccthr${ti}"></div><div class="cc-stat-l">Gbps</div></div>
         <div class="cc-stat"><div class="cc-stat-v" id="cclat${ti}"></div><div class="cc-stat-l">Latency</div></div>
         <div class="cc-stat"><div class="cc-stat-v" id="ccues${ti}"></div><div class="cc-stat-l">UEs</div></div>
       </div>
       <canvas class="cc-wave" id="ccwave${ti}" height="22"></canvas>`;
    cc.appendChild(card);
  });
}

// ── xApp buttons ──────────────────────────────────────────────
function renderXAppButtons(xapps) {
  const container = document.getElementById('xapp-btns');
  container.innerHTML = '';
  xapps.forEach(xapp => {
    const name   = xapp.name || xapp;
    const disp   = xapp.display_name || _cfg.XAPP_RULES[name]?.display_name || name;
    const status = xapp.status || (_net.xapps.includes(name) ? 'running' : 'stopped');
    const btn    = document.createElement('button');
    btn.className = 'ctrl-btn' + (status === 'running' ? ' on' : status === 'error' ? ' kill' : '');
    btn.id        = 'xbtn-' + name;
    btn.innerHTML = `<div class="btn-led"${status !== 'running' ? ' style="background:rgba(0,160,255,.2)"' : ''}></div>${disp}`;
    btn.onclick   = () => _callbacks.onXApp(name, status === 'running' ? 'stop' : 'start');
    container.appendChild(btn);
  });
}

// ── Scenario buttons ──────────────────────────────────────────
function renderScenarioButtons(scenarios) {
  const container = document.getElementById('scenario-btns');
  container.innerHTML = '';
  scenarios.forEach(sc => {
    const name   = sc.name || sc;
    const disp   = sc.display_name || name.replace(/_/g, ' ');
    const active = sc.active !== undefined ? sc.active : (_net.scenario === name);
    const btn    = document.createElement('button');
    btn.className = 'ctrl-btn' + (active ? ' on' : '');
    btn.id        = 'scbtn-' + name;
    btn.innerHTML = `<div class="btn-led"${!active ? ' style="background:rgba(0,160,255,.2)"' : ''}></div>${disp}${active ? ' ●' : ''}`;
    btn.onclick   = () => _callbacks.onScenario(name);
    container.appendChild(btn);
  });
  // stop scenario button
  const stop = document.createElement('button');
  stop.className = 'ctrl-btn kill';
  stop.innerHTML = '<div class="btn-led"></div>STOP SCENARIO';
  stop.onclick   = () => _callbacks.onScenario('__stop__');
  container.appendChild(stop);
}

// ── Agent metrics bar ─────────────────────────────────────────
function buildAgentMetrics() {
  const am = document.getElementById('a-metrics');
  am.innerHTML = '';
  [['CELLS', '#00ccff'], ['UEs', '#00ff88'], ['ALERTS', '#ffcc00']].forEach(m => {
    const d = document.createElement('div');
    d.className = 'am';
    d.innerHTML = `<div class="am-v" style="color:${m[1]}" id="am-${m[0]}">—</div><div class="am-l">${m[0]}</div>`;
    am.appendChild(d);
  });
}

// ── Main update ───────────────────────────────────────────────
function updateUI() {
  const t = Date.now() / 1000;

  // top KPIs
  const totalUEs  = _net.cells.reduce((s, c) => s + (c.ues || 0), 0);
  const avgThr    = _net.cells.reduce((s, c) => s + (c.thr || 2), 0) / _net.cells.length;
  const avgLat    = _net.cells.reduce((s, c) => s + (c.lat || 1), 0) / _net.cells.length;
  const onlineCnt = _net.cells.filter(c => c.status === 'ONLINE').length;
  const el = id => document.getElementById(id);
  if (el('kgnb')) el('kgnb').textContent = onlineCnt;
  if (el('kue'))  el('kue').textContent  = totalUEs;
  if (el('kthr')) el('kthr').textContent = avgThr.toFixed(1) + 'G';
  if (el('klat')) el('klat').textContent = avgLat.toFixed(1) + 'ms';
  if (el('kcov')) el('kcov').textContent = Math.round(90 + Math.sin(t * .2) * 5) + '%';

  // cell cards
  _net.cells.forEach((c, ti) => {
    const col  = loadColor(c.load);
    const card = document.getElementById('cc' + ti);
    if (!card) return;
    card.style.borderColor = col + '44';
    card.style.color       = col;
    const ccid = document.getElementById('ccid' + ti); if (ccid) ccid.textContent = c.id;
    const ccst = document.getElementById('ccst' + ti);
    if (ccst) { ccst.textContent = c.status; ccst.style.borderColor = col + '88'; }
    const bar = document.getElementById('ccbar' + ti);
    if (bar) { bar.style.width = Math.round(c.load * 100) + '%'; bar.style.background = col; }
    const ccthr = document.getElementById('ccthr' + ti);
    if (ccthr) { ccthr.textContent = (c.thr || 2).toFixed(1); ccthr.style.color = col; }
    const cclat = document.getElementById('cclat' + ti);
    if (cclat) { cclat.textContent = (c.lat || 1).toFixed(1) + 'ms'; cclat.style.color = '#ffcc00'; }
    const ccues = document.getElementById('ccues' + ti);
    if (ccues) { ccues.textContent = c.ues || 0; ccues.style.color = '#00aaff'; }
    const wc = document.getElementById('ccwave' + ti); if (wc) drawWave(wc, ti, col);
  });

  // agent metrics
  const amCells  = document.getElementById('am-CELLS');
  const amUEs    = document.getElementById('am-UEs');
  const amAlerts = document.getElementById('am-ALERTS');
  if (amCells)  amCells.textContent  = _net.cells.filter(c => c.status === 'ONLINE').length;
  if (amUEs)    amUEs.textContent    = totalUEs;
  if (amAlerts) amAlerts.textContent = _net.alerts.length;

  // alert badges
  const aa = document.getElementById('alert-area');
  if (aa) {
    aa.innerHTML = '';
    _net.alerts.slice(0, 3).forEach(a => {
      const el = document.createElement('div');
      el.className   = 'alert-badge';
      el.style.borderColor = '#ff4444'; el.style.color = '#ff4444'; el.style.background = 'rgba(255,40,40,.1)';
      el.textContent = 'ALERT: ' + (a.msg || a.type || '').slice(0, 18);
      aa.appendChild(el);
    });
  }

  // sim mode badge
  const sb = document.getElementById('sim-badge');
  if (sb) sb.style.display = _net.simMode ? 'block' : 'none';

  // throughput chart
  drawThrChart();
}

// ── Slider setup ──────────────────────────────────────────────
function setupSliders() {
  const sliders = [
    { sl: 'sl-txpow', sv: 'sv-txpow', fmt: v => Math.round(20 + v * .8) + ' dBm',  param: 'tx_power' },
    { sl: 'sl-freq',  sv: 'sv-freq',  fmt: v => (1 + v * .049).toFixed(1) + ' GHz', param: 'frequency' },
    { sl: 'sl-tilt',  sv: 'sv-tilt',  fmt: v => (-15 + Math.round(v * .15)) + ' deg', param: 'beam_tilt' },
    { sl: 'sl-bw',    sv: 'sv-bw',    fmt: v => Math.round(20 + v * .8) + ' MHz',   param: 'bandwidth' },
  ];
  sliders.forEach(s => {
    const sl = document.getElementById(s.sl);
    const sv = document.getElementById(s.sv);
    if (!sl || !sv) return;
    sl.addEventListener('input', () => {
      sv.textContent = s.fmt(+sl.value);
      _callbacks.onRF(s.param, +sl.value);
    });
  });
}

// ── RIC buttons ───────────────────────────────────────────────
function setupRICButtons() {
  const stop    = document.getElementById('btn-ric-stop');
  const restart = document.getElementById('btn-ric-restart');
  const diag    = document.getElementById('btn-ric-diag');
  if (stop)    stop.onclick    = () => { stop.classList.toggle('on'); stop.classList.toggle('kill'); _callbacks.onRIC('stop'); };
  if (restart) restart.onclick = () => { _callbacks.onRIC('restart'); addLog('RIC restarting...'); };
  if (diag)    diag.onclick    = () => { _callbacks.onRIC('diagnostics'); addLog('Diagnostics started'); };
}

// ── Chat helpers ──────────────────────────────────────────────
let tyEl = null;

function addMsg(role, text) {
  const m = document.getElementById('msgs');
  if (!m) return;
  const w  = document.createElement('div'); w.className = 'mw' + (role === 'u' ? ' u' : '');
  const b  = document.createElement('div'); b.className  = 'mb ' + role; b.textContent = text;
  const ts = document.createElement('div'); ts.className = 'mts'; ts.textContent = new Date().toLocaleTimeString();
  w.appendChild(b); w.appendChild(ts); m.appendChild(w);
  m.scrollTop = m.scrollHeight;
}

function showTyping() {
  const m = document.getElementById('msgs');
  if (!m) return;
  tyEl = document.createElement('div'); tyEl.className = 'typing';
  tyEl.innerHTML = '<span></span><span></span><span></span>';
  m.appendChild(tyEl); m.scrollTop = m.scrollHeight;
}

function hideTyping() {
  if (tyEl && tyEl.parentNode) tyEl.parentNode.removeChild(tyEl);
  tyEl = null;
}

function setBusy(v) {
  const dot  = document.getElementById('a-dot');
  const st   = document.getElementById('a-status');
  const btn  = document.getElementById('sbtn');
  const inp  = document.getElementById('inp');
  if (v) {
    if (dot) { dot.style.background = '#ffcc00'; dot.style.boxShadow = '0 0 8px #ffcc00'; }
    if (st)  { st.style.color = '#ffcc00'; st.textContent = 'EXECUTING...'; }
    if (btn) { btn.textContent = '...'; btn.disabled = true; }
    if (inp) inp.disabled = true;
  } else {
    if (dot) { dot.style.background = '#00ff88'; dot.style.boxShadow = '0 0 8px #00ff88'; }
    if (st)  { st.style.color = '#00ff88'; st.textContent = 'MONITORING'; }
    if (btn) { btn.textContent = 'SEND'; btn.disabled = false; }
    if (inp) inp.disabled = false;
  }
}

function setError() {
  const dot = document.getElementById('a-dot');
  const st  = document.getElementById('a-status');
  if (dot) { dot.style.background = '#ff2244'; dot.style.boxShadow = '0 0 8px #ff2244'; }
  if (st)  { st.style.color = '#ff2244'; st.textContent = 'ERROR'; }
}

// ── Action log ────────────────────────────────────────────────
function addLog(txt) {
  const d = document.getElementById('logi');
  if (!d) return;
  const col = txt.includes('Handover') || txt.includes('handover') ? '#ffcc00'
            : txt.includes('xApp') || txt.includes('started') || txt.includes('success') ? '#00ff88'
            : txt.includes('CRIT') || txt.includes('error') || txt.includes('ERROR') ? '#ff4444'
            : 'rgba(120,170,255,.4)';
  const el = document.createElement('div'); el.className = 'll'; el.style.color = col;
  el.textContent = '[' + new Date().toTimeString().slice(0, 8) + '] ' + txt;
  d.insertBefore(el, d.firstChild);
  while (d.children.length > 20) d.removeChild(d.lastChild);
}

// ── Init ──────────────────────────────────────────────────────
export function initUI(net, cfg, callbacks) {
  _net       = net;
  _cfg       = cfg;
  _callbacks = callbacks;
  wavePhase  = cfg.CELLS.map((_, i) => i * 0.7);

  buildCellCards();
  buildAgentMetrics();
  setupSliders();
  setupRICButtons();

  // initial dynamic buttons — use config fallback
  const xappList = Object.entries(cfg.XAPP_RULES).map(([name, rule]) => ({
    name, display_name: rule.display_name, status: 'stopped',
  }));
  renderXAppButtons(xappList);

  const scenarioList = Object.entries(cfg.SCENARIOS).map(([name]) => ({
    name, display_name: name.replace(/_/g, ' '), active: name === net.scenario,
  }));
  renderScenarioButtons(scenarioList);

  return { update: updateUI, addLog, addMsg, setBusy, setError, showTyping, hideTyping, renderXAppButtons, renderScenarioButtons };
}
