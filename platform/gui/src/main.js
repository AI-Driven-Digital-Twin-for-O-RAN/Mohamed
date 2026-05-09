import './style.css';
import { CONFIG, ALL_CELLS } from './config.js';
import { initScene, setUEPositions, triggerHandover, flashUELabel } from './scene.js';

const BASE = CONFIG.BACKEND_URL;   // proxied → http://localhost:8000

// ── Network state ─────────────────────────────────────────────
const NET = {
  cells: CONFIG.CELLS.map(c => ({
    id: c.id, load: c.initLoad, ues: CONFIG.NUM_UES_PER_CELL,
    thr: 1.8 + Math.random() * 0.8, lat: 0.8 + Math.random() * 0.6,
    status: c.initLoad > 0.90 ? 'DEGRADED' : 'ONLINE',
  })),
  simMode: true,
};

// Track UE serving cells between polls to detect handovers
let prevServingCells = {};

// ── Simulation drift (demo mode when backend unreachable) ──────
let drifts = NET.cells.map(() => 0);
setInterval(() => {
  if (!NET.simMode) return;
  NET.cells.forEach((c, i) => {
    drifts[i] += (Math.random() - 0.5) * 0.06;
    drifts[i]  = Math.max(-0.25, Math.min(0.25, drifts[i]));
    c.load     = Math.max(0.05, Math.min(0.97, c.load + drifts[i] * 0.012));
    c.thr      = Math.max(0.5,  c.thr + (Math.random() - 0.5) * 0.05);
    c.lat      = Math.max(0.2,  c.lat + (Math.random() - 0.5) * 0.08);
    c.status   = c.load > 0.90 ? 'DEGRADED' : 'ONLINE';
  });
}, 2000);

// ── Backend polling (live InfluxDB data) ──────────────────────
async function pollBackend() {
  try {
    const r = await fetch(BASE + '/refresh-data', { signal: AbortSignal.timeout(3000) });
    if (!r.ok) throw new Error('not ok');
    const data = await r.json();
    const bc = data.cells || [];

    // Extract world bounds early (needed for cell + UE position transforms)
    const [maxX, maxY] = data.max_x_max_y || [2000, 2000];
    const SCENE_SCALE  = 180;
    const ns3ToScene   = (nx, ny) => ({
      x: ((nx ?? maxX/2) - maxX/2) / (maxX/2) * SCENE_SCALE,
      z: ((ny ?? maxY/2) - maxY/2) / (maxY/2) * SCENE_SCALE,
    });

    // Rebuild scene if topology changed (use sceneCellCount to track what's actually rendered)
    if (bc.length > 0 && bc.length !== sceneCellCount) {
      // Build cell list with REAL ns-3 positions from backend
      const activeCells = bc.map((d, i) => {
        const base = ALL_CELLS[i] || { id: `cell-${i+1}`, type: d.type || 'mmwave', color: '#00ccff', initLoad: 0.5 };
        const pos  = ns3ToScene(d.x_position, d.y_position);
        return { ...base, x: pos.x, z: pos.z };
      });
      const ueArr = distributeUEs(data.ues?.length || bc.length * 2, bc.length);
      NET.cells = activeCells.map((cell, i) => ({
        id: cell.id,
        load: (bc[i]?.dlPrbUsage_percentage ?? 50) / 100,
        ues: ueArr[i],
        thr: 1.8 + Math.random() * 0.8,
        lat: 0.8 + Math.random() * 0.6,
        status: 'ONLINE',
      }));
      CONFIG.CELLS = activeCells;
      drifts = NET.cells.map(() => 0);
      prevServingCells = {};
      scene = initScene(NET, CONFIG, { onCellPick: showCellTooltip, onCellDismiss: dismissTooltip });
      sceneCellCount = bc.length;
      buildCards();
    } else {
      // Count UEs per cell to derive proxy load when PRB data is zero (real E2 mode)
      const ueCountPerCell = new Array(bc.length).fill(0);
      (data.ues || []).forEach(u => {
        const cid = (u.MMWave_Cell ?? u.LTE_Cell ?? 0) - 1;
        if (cid >= 0 && cid < bc.length) ueCountPerCell[cid]++;
      });
      const maxUeCount = Math.max(...ueCountPerCell, 1);

      bc.forEach((d, i) => {
        if (i >= NET.cells.length) return;
        const prb = d.dlPrbUsage_percentage;
        NET.cells[i].load = (prb !== undefined && prb > 0)
          ? Math.max(0, Math.min(1, prb / 100))
          : 0.10 + (ueCountPerCell[i] / maxUeCount) * 0.70;
        NET.cells[i].ues = d.MeanActiveUEsDownlink !== undefined
          ? Math.round(d.MeanActiveUEsDownlink)
          : ueCountPerCell[i];
      });
    }

    NET.simMode = false;
    updateRIC(true);
    updateEnergy(data.totalcurrec, data.maxec);

    const simOn = data.simulation_status === 'on';
    _simRunning = simOn;
    setSimStatus(simOn ? 'RUNNING' : 'STOPPED');
    if (simOn) {
      const ues = data.ues || [];
      updateSINRStrip(ues.map(u => { const v = parseFloat(u.L3servingSINR_dB); return isNaN(v) ? null : v; }));
      ues.forEach((u, i) => {
        const cellId = u.MMWave_Cell ?? u.LTE_Cell ?? 0;
        const prev = prevServingCells[i];
        if (prev !== undefined && prev !== cellId && prev > 0 && cellId > 0) {
          const fromCell = CONFIG.CELLS[prev - 1];
          const toCell   = CONFIG.CELLS[cellId - 1];
          if (fromCell && toCell) triggerHandover(fromCell.x, fromCell.z, toCell.x, toCell.z);
          flashUELabel(i);
          logHandover(i, fromCell, toCell);
        }
        prevServingCells[i] = cellId;
      });
      setUEPositions(ues.length > 0 ? ues : null, maxX, maxY, CONFIG.CELLS);
    } else {
      prevServingCells = {};
      setUEPositions(null, 2000, 2000);
    }
  } catch (_) {
    NET.simMode = true;
    updateRIC(false);
    setUEPositions(null, 2000, 2000);
  }
}

// ── Simulation status indicator ───────────────────────────────
function setSimStatus(status) {
  const dot = document.getElementById('sim-dot');
  const txt = document.getElementById('sim-status-txt');
  if (!dot || !txt) return;
  const running = status === 'RUNNING';
  dot.className = 'ric-dot ' + (running ? 'online' : 'offline');
  txt.className  = 'ric-val '  + (running ? 'online' : 'offline');
  txt.textContent = status;
}

// ── RIC status ────────────────────────────────────────────────
function updateRIC(online) {
  ['flexric', 'odu', 'oru', 'e2'].forEach(id => {
    const dot = document.getElementById('ric-' + id);
    const val = document.getElementById('ric-' + id + '-v');
    if (dot) dot.className = 'ric-dot ' + (online ? 'online' : 'offline');
    if (val) { val.className = 'ric-val ' + (online ? 'online' : 'offline'); val.textContent = online ? 'ONLINE' : 'OFFLINE'; }
  });
}

// ── Color helper ──────────────────────────────────────────────
function loadColor(v) {
  return v < 0.45 ? '#00ff88' : v < 0.70 ? '#ffcc00' : v < 0.88 ? '#ff6600' : '#ff2244';
}

// ── Distribute N UEs across K cells evenly ────────────────────
function distributeUEs(totalUEs, cellCount) {
  const base  = Math.floor(totalUEs / cellCount);
  const extra = totalUEs % cellCount;
  return Array.from({ length: cellCount }, (_, i) => base + (i < extra ? 1 : 0));
}

// ── Cell cards ────────────────────────────────────────────────
function buildCards() {
  const container = document.getElementById('cell-cards');
  if (!container) return;
  container.innerHTML = '';
  NET.cells.forEach((_, i) => {
    const div = document.createElement('div');
    div.className = 'cc'; div.id = 'cc' + i;
    div.innerHTML = `
      <div class="cc-head">
        <span class="cc-id" id="cc-id${i}">—</span>
        <span class="cc-st" id="cc-st${i}">—</span>
      </div>
      <div class="cc-bar-row">
        <div class="cc-track"><div class="cc-fill" id="cc-bar${i}"></div></div>
        <span class="cc-pct" id="cc-load${i}">—</span>
      </div>
      <div class="cc-metrics">
        <span id="cc-thr${i}">—</span><span class="cc-mu">G</span>
        <span class="cc-sep">·</span>
        <span id="cc-lat${i}">—</span><span class="cc-mu">ms</span>
        <span class="cc-sep">·</span>
        <span id="cc-ues${i}">—</span><span class="cc-mu">UE</span>
      </div>`;
    container.appendChild(div);
  });
}

// ── System Control Panel ──────────────────────────────────────────
async function loadScenarios() {
  try {
    const r = await fetch('/ctrl/scenarios', { signal: AbortSignal.timeout(4000) });
    if (!r.ok) return;
    const list = await r.json();
    const sel = document.getElementById('scenario-sel');
    if (!sel) return;
    sel.innerHTML = list.map(s => `<option value="${s}">${s}</option>`).join('');
    const gru = list.indexOf('gru_scenario');
    if (gru >= 0) sel.selectedIndex = gru;
  } catch (_) {}
}

function _setCompStatus(comp, running) {
  const dot   = document.getElementById('dot-'   + comp);
  const state = document.getElementById('state-' + comp);
  const card  = document.getElementById('sys-'   + comp);
  if (dot)   dot.className   = 'sys-dot '   + (running ? 'running' : 'stopped');
  if (state) { state.textContent = running ? 'RUNNING' : 'OFFLINE'; state.className = 'sys-state ' + (running ? 'online' : 'offline'); }
  if (card)  { if (running) card.classList.add('active'); else card.classList.remove('active'); }
}

// ── Handover log ──────────────────────────────────────────────
const HO_MAX = 6;
function logHandover(ueIdx, fromCell, toCell) {
  const log = document.getElementById('ho-log');
  if (!log) return;
  const now = new Date();
  const ts  = now.toTimeString().slice(0,8);
  const uc  = toCell?.color || '#00ccff';
  const entry = document.createElement('div');
  entry.className = 'ho-entry';
  entry.innerHTML =
    `<span class="ho-time">${ts}</span>` +
    `<span class="ho-ue" style="color:${uc}">UE-${String(ueIdx+1).padStart(2,'0')}</span>` +
    `<span class="ho-arr">→</span>` +
    `<span class="ho-cell" style="color:${uc}">${toCell?.id || '?'}</span>`;
  log.insertBefore(entry, log.firstChild);
  while (log.children.length > HO_MAX) log.removeChild(log.lastChild);
}

async function pollCtrlStatus() {
  try {
    const r = await fetch('/ctrl/status', { signal: AbortSignal.timeout(3000) });
    if (!r.ok) return;
    const s = await r.json();
    _setCompStatus('docker',     s.docker);
    _setCompStatus('flexric',    s.flexric);
    _setCompStatus('simulation', s.simulation);
    _setCompStatus('pusher',     s.pusher);
    _setCompStatus('xapp',       s.xapp);
    // Update xApp label based on which model is actually running
    const xappNameEl = document.getElementById('xapp-name-label');
    const xappSubEl  = document.getElementById('xapp-sub-label');
    if (xappNameEl) {
      if (s.active_xapp_type === 'rl') {
        xappNameEl.textContent = 'RL xAPP (DDQN)';
        if (xappSubEl) xappSubEl.textContent = 'Deep Q-Network · port 5001';
      } else if (s.active_xapp_type === 'gru') {
        xappNameEl.textContent = 'GRU xAPP';
        if (xappSubEl) xappSubEl.textContent = 'GRU Predictor · port 5000';
      } else {
        xappNameEl.textContent = 'xAPP';
        if (xappSubEl) xappSubEl.textContent = 'Handover Controller';
      }
    }
  } catch (_) {}
}

async function ctrlStart(comp) {
  const btns = document.querySelectorAll(`#sys-${comp} .sbtn`);
  btns.forEach(b => b.disabled = true);

  const body = comp === 'simulation' ? {
    scenario:   document.getElementById('scenario-sel')?.value  || 'gru_scenario',
    n_ues:      parseInt(document.getElementById('param-ues')?.value)   || 20,
    n_mmwave:   parseInt(document.getElementById('param-cells')?.value) || 7,
    sim_time:   parseInt(document.getElementById('param-time')?.value)  || 60,
  } : {};

  try {
    await fetch(`/ctrl/${comp}/start`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body), signal: AbortSignal.timeout(12000),
    });
  } catch (_) {}
  btns.forEach(b => b.disabled = false);
  pollCtrlStatus();
}

async function ctrlStop(comp) {
  try {
    await fetch(`/ctrl/${comp}/stop`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: '{}', signal: AbortSignal.timeout(8000),
    });
  } catch (_) {}
  pollCtrlStatus();
}

async function launchAll() {
  const btn = document.getElementById('btn-launch-all');
  if (btn) { btn.disabled = true; btn.textContent = '⏳ LAUNCHING...'; }
  const body = {
    scenario:  document.getElementById('scenario-sel')?.value  || 'gru_scenario',
    n_ues:     parseInt(document.getElementById('param-ues')?.value)   || 20,
    n_mmwave:  parseInt(document.getElementById('param-cells')?.value) || 7,
    sim_time:  parseInt(document.getElementById('param-time')?.value)  || 60,
  };
  try {
    await fetch('/ctrl/launch-all', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body), signal: AbortSignal.timeout(10000),
    });
  } catch (_) {}
  if (btn) { btn.disabled = false; btn.textContent = '⚡ LAUNCH ALL'; }
}

async function stopAll() {
  const btn = document.getElementById('btn-stop-all');
  if (btn) { btn.disabled = true; btn.textContent = '⏳ STOPPING...'; }
  try {
    await fetch('/ctrl/stop-all', {
      method: 'POST', signal: AbortSignal.timeout(10000),
    });
  } catch (_) {}
  if (btn) { btn.disabled = false; btn.textContent = '■ STOP ALL'; }
  pollCtrlStatus();
}

window.ctrlStart = ctrlStart;
window.ctrlStop  = ctrlStop;

// ── Update UI ─────────────────────────────────────────────────
function updateUI() {
  const $ = id => document.getElementById(id);

  const totalUEs = NET.cells.reduce((s, c) => s + (c.ues || 0), 0);
  const avgThr   = NET.cells.reduce((s, c) => s + (c.thr || 2), 0) / NET.cells.length;
  const avgLat   = NET.cells.reduce((s, c) => s + (c.lat || 1), 0) / NET.cells.length;
  const avgLoad  = NET.cells.reduce((s, c) => s + c.load, 0) / NET.cells.length;
  const online   = NET.cells.filter(c => c.status === 'ONLINE').length;

  if ($('k-cells')) $('k-cells').textContent = online;
  if ($('k-ues'))   $('k-ues').textContent   = totalUEs;
  if ($('k-thr'))   $('k-thr').textContent   = avgThr.toFixed(1) + ' G';
  if ($('k-lat'))   $('k-lat').textContent   = avgLat.toFixed(1) + ' ms';
  if ($('k-load'))  $('k-load').textContent  = Math.round(avgLoad * 100) + '%';

  NET.cells.forEach((c, i) => {
    const col  = loadColor(c.load);
    const card = $('cc' + i); if (!card) return;
    card.style.borderLeftColor = col;
    const id  = $('cc-id'   + i); if (id)  id.textContent = c.id;
    const st  = $('cc-st'   + i); if (st)  { st.textContent = c.status; st.style.color = col; }
    const bar = $('cc-bar'  + i); if (bar) { bar.style.width = Math.round(c.load * 100) + '%'; bar.style.background = col; }
    const ld  = $('cc-load' + i); if (ld)  { ld.textContent = Math.round(c.load * 100) + '%'; ld.style.color = col; }
    const thr = $('cc-thr'  + i); if (thr) { thr.textContent = (c.thr || 2).toFixed(1); thr.style.color = '#00ff88'; }
    const lat = $('cc-lat'  + i); if (lat) { lat.textContent = (c.lat || 1).toFixed(1); lat.style.color = '#ffcc00'; }
    const ues = $('cc-ues'  + i); if (ues) { ues.textContent = c.ues || 0; ues.style.color = '#00bbff'; }
  });
}

// ── Boot ──────────────────────────────────────────────────────
// Use all 8 cells immediately so the scene always shows on load
CONFIG.CELLS = ALL_CELLS.slice();
const _bootUEs = distributeUEs(20, CONFIG.CELLS.length);
NET.cells = CONFIG.CELLS.map((c, i) => ({
  id: c.id, load: c.initLoad, ues: _bootUEs[i],
  thr: 1.8 + Math.random() * 0.8, lat: 0.8 + Math.random() * 0.6,
  status: c.initLoad > 0.90 ? 'DEGRADED' : 'ONLINE',
}));
drifts = NET.cells.map(() => 0);
buildCards();

// Init UI
setSimStatus('STOPPED');

// Wire master buttons
document.getElementById('btn-launch-all')?.addEventListener('click', launchAll);
document.getElementById('btn-stop-all')  ?.addEventListener('click', stopAll);

// Load scenario list and start polling controller status
loadScenarios();
pollCtrlStatus();
setInterval(pollCtrlStatus, 3000);

// Scene starts immediately — no waiting for backend
let scene = initScene(NET, CONFIG, { onCellPick: showCellTooltip, onCellDismiss: dismissTooltip });
let sceneCellCount = CONFIG.CELLS.length;

setInterval(() => { const el = document.getElementById('clock'); if (el) el.textContent = new Date().toTimeString().slice(0, 8); }, 1000);
setInterval(() => { updateUI(); scene.updateColors(); refreshTooltip(); }, 1000);

// Poll backend every 1.5s — tight sync with 2D GUI
pollBackend();
setInterval(pollBackend, 1500);

// ── SINR sparklines ───────────────────────────────────────────
const sinrHistory = {};
function updateSINRStrip(sinrs) {
  const strip = document.getElementById('sinr-strip');
  if (!strip) return;
  sinrs.forEach((sinr, i) => {
    if (!sinrHistory[i]) sinrHistory[i] = [];
    if (sinr !== null) {
      sinrHistory[i].push(sinr);
      if (sinrHistory[i].length > 30) sinrHistory[i].shift();
    }
    let cv = document.getElementById('sk' + i);
    if (!cv) {
      cv = document.createElement('canvas');
      cv.id = 'sk' + i; cv.className = 'sinr-spark';
      cv.width = 26; cv.height = 36;
      cv.title = `UE ${i + 1}`;
      strip.appendChild(cv);
    }
    const ctx = cv.getContext('2d');
    ctx.clearRect(0, 0, 26, 36);
    const hist = sinrHistory[i];
    const cur  = hist.length > 0 ? hist[hist.length - 1] : sinr;
    const col  = cur === null ? '#112233' : cur > 10 ? '#00ff88' : cur > 0 ? '#ffcc00' : '#ff3344';
    if (hist.length < 2) {
      if (cur !== null) {
        const pct = Math.max(0.05, Math.min(1, (cur + 10) / 35));
        ctx.fillStyle = col + '33';
        ctx.fillRect(0, 36 * (1 - pct), 26, 36 * pct);
      }
      return;
    }
    const minV = Math.min(...hist) - 2;
    const maxV = Math.max(...hist) + 2;
    const range = Math.max(8, maxV - minV);
    const xOf  = j => (j / (hist.length - 1)) * 24 + 1;
    const yOf  = v => 34 - ((v - minV) / range) * 30;
    // Gradient fill under line
    const grad = ctx.createLinearGradient(0, 0, 0, 36);
    grad.addColorStop(0, col + '28'); grad.addColorStop(1, col + '06');
    ctx.beginPath();
    hist.forEach((v, j) => { j === 0 ? ctx.moveTo(xOf(j), yOf(v)) : ctx.lineTo(xOf(j), yOf(v)); });
    ctx.lineTo(xOf(hist.length - 1), 36); ctx.lineTo(xOf(0), 36); ctx.closePath();
    ctx.fillStyle = grad; ctx.fill();
    // Trend line
    ctx.beginPath();
    hist.forEach((v, j) => { j === 0 ? ctx.moveTo(xOf(j), yOf(v)) : ctx.lineTo(xOf(j), yOf(v)); });
    ctx.strokeStyle = col; ctx.lineWidth = 1.2;
    ctx.shadowBlur = 5; ctx.shadowColor = col;
    ctx.stroke(); ctx.shadowBlur = 0;
    // Latest value dot
    const lv = hist[hist.length - 1];
    ctx.beginPath(); ctx.arc(25, yOf(lv), 2, 0, Math.PI * 2);
    ctx.fillStyle = col; ctx.shadowBlur = 6; ctx.shadowColor = col;
    ctx.fill(); ctx.shadowBlur = 0;
    cv.title = `UE ${i + 1}: ${lv.toFixed(1)} dB`;
  });
}

// ── Cell inspect tooltip ──────────────────────────────────────
let _selectedCell = null;

function showCellTooltip(cellIdx) {
  _selectedCell = cellIdx;
  const cell = CONFIG.CELLS[cellIdx];
  const tt   = document.getElementById('cell-tooltip');
  if (!tt || !cell) return;
  tt.style.setProperty('--ctt-col', cell.color || '#00ccff');
  tt.classList.remove('hidden');
  refreshTooltip();
}

function dismissTooltip() {
  _selectedCell = null;
  const tt = document.getElementById('cell-tooltip');
  if (tt) tt.classList.add('hidden');
}

function refreshTooltip() {
  if (_selectedCell === null) return;
  const c    = NET.cells[_selectedCell];
  const cell = CONFIG.CELLS[_selectedCell];
  if (!c || !cell) return;
  const col = loadColor(c.load);
  const $   = id => document.getElementById(id);
  if ($('ctt-id'))     { $('ctt-id').textContent = cell.id; }
  if ($('ctt-type'))   $('ctt-type').textContent  = cell.type === 'lte' ? 'LTE MACRO' : 'mmWave SC';
  if ($('ctt-status')) { $('ctt-status').textContent = c.status; $('ctt-status').style.color = col; }
  if ($('ctt-load'))   { $('ctt-load').textContent  = Math.round(c.load * 100) + '%'; $('ctt-load').style.color = col; }
  if ($('ctt-bar'))    { $('ctt-bar').style.width = Math.round(c.load * 100) + '%'; $('ctt-bar').style.background = col; }
  if ($('ctt-ues'))    { $('ctt-ues').textContent  = c.ues || 0; $('ctt-ues').style.color = '#00bbff'; }
  if ($('ctt-thr'))    { $('ctt-thr').textContent  = (c.thr || 2).toFixed(2) + ' Gbps'; $('ctt-thr').style.color = '#00ff88'; }
  if ($('ctt-lat'))    { $('ctt-lat').textContent  = (c.lat || 1).toFixed(1) + ' ms'; $('ctt-lat').style.color = '#ffcc00'; }
}

document.getElementById('ctt-close')?.addEventListener('click', dismissTooltip);

// ── Energy bar ────────────────────────────────────────────────
function updateEnergy(curr, max) {
  const fill = document.getElementById('energy-fill');
  const val  = document.getElementById('energy-val');
  if (!curr || !max) return;
  const pct = Math.min(1, curr / max);
  if (fill) { fill.style.width = (pct * 100) + '%'; fill.style.background = pct > 0.92 ? '#ff3344' : pct > 0.76 ? '#ff7700' : '#00ccff'; }
  if (val)  val.textContent = curr.toFixed(0) + ' / ' + max.toFixed(0) + ' kJ';
}

// ── xApp status polling ───────────────────────────────────────
let _simRunning = false;

