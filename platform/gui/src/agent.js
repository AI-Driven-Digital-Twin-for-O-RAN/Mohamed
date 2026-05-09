let _net, _cfg, _ui;

let conv       = [];         // conversation history
const cooldowns = {};        // cooldowns[cellId][xappName] = timestamp
let agentOpen  = true;

// ── Tools (Gemini function_declarations format) ───────────────
function buildTools() {
  const xappNames = Object.keys(_cfg.XAPP_RULES);
  const scenNames = Object.keys(_cfg.SCENARIOS);
  return [
    {
      name: 'get_network_status',
      description: 'Get real-time network status including all cells, xApps, alerts, and available scenarios',
      parameters: { type: 'object', properties: {} },
    },
    {
      name: 'trigger_load_balancing',
      description: 'Move UEs from an overloaded cell to an underloaded cell',
      parameters: {
        type: 'object',
        properties: {
          from_cell: { type: 'string', description: 'Cell ID to offload UEs from' },
          to_cell:   { type: 'string', description: 'Cell ID to receive UEs' },
          ue_count:  { type: 'number', description: 'Number of UEs to move (max ' + _cfg.XAPP_RULES.LOAD_BALANCER.max_ues_to_move + ')' },
        },
        required: ['from_cell', 'to_cell', 'ue_count'],
      },
    },
    {
      name: 'start_xapp',
      description: 'Start an xApp on the RIC',
      parameters: {
        type: 'object',
        properties: {
          xapp_name:   { type: 'string', description: 'One of: ' + xappNames.join(', ') },
          target_cell: { type: 'string', description: 'Optional cell ID to target' },
        },
        required: ['xapp_name'],
      },
    },
    {
      name: 'stop_xapp',
      description: 'Stop a running xApp',
      parameters: {
        type: 'object',
        properties: {
          xapp_name: { type: 'string', description: 'One of: ' + xappNames.join(', ') },
        },
        required: ['xapp_name'],
      },
    },
    {
      name: 'set_scenario',
      description: 'Activate a network scenario',
      parameters: {
        type: 'object',
        properties: {
          scenario: { type: 'string', description: 'One of: ' + scenNames.join(', ') },
        },
        required: ['scenario'],
      },
    },
    {
      name: 'restart_component',
      description: 'Restart a RIC component or cell',
      parameters: {
        type: 'object',
        properties: {
          component: { type: 'string', description: 'One of: RIC, E2_INTERFACE, XAPP_MANAGER, or any cell id' },
        },
        required: ['component'],
      },
    },
    {
      name: 'adjust_cell_params',
      description: 'Adjust RF parameters for a specific cell',
      parameters: {
        type: 'object',
        properties: {
          cell_id:   { type: 'string' },
          tx_power:  { type: 'number', description: 'TX power in dBm' },
          beam_tilt: { type: 'number', description: 'Beam tilt in degrees (-15 to 0)' },
        },
        required: ['cell_id'],
      },
    },
    {
      name: 'resolve_alert',
      description: 'Mark an active alert as resolved',
      parameters: {
        type: 'object',
        properties: { alert_id: { type: 'string' } },
        required: ['alert_id'],
      },
    },
  ];
}

// ── Tool execution ────────────────────────────────────────────
async function execTool(name, inp) {
  const B = _cfg.BACKEND_URL;

  if (name === 'get_network_status') {
    if (!_net.simMode) {
      try {
        const r = await fetch(B + '/refresh-data');
        if (r.ok) {
          const d = await r.json();
          return {
            cells:              _net.cells,
            ric:                _net.ric,
            scenario:           _net.scenario,
            alerts:             _net.alerts,
            running_xapps:      _net.xapps,
            available_xapps:    Object.keys(_cfg.XAPP_RULES),
            available_scenarios: Object.keys(_cfg.SCENARIOS),
            sim_status:         d.simulation_status,
          };
        }
      } catch (_) {}
    }
    return {
      cells: _net.cells.map(c => ({ ...c })),
      ric: _net.ric, scenario: _net.scenario,
      alerts: _net.alerts, running_xapps: _net.xapps,
      available_xapps: Object.keys(_cfg.XAPP_RULES),
      available_scenarios: Object.keys(_cfg.SCENARIOS),
      sim_status: _net.simMode ? 'simulation' : 'unknown',
    };
  }

  if (name === 'trigger_load_balancing') {
    const rule = _cfg.XAPP_RULES.LOAD_BALANCER;
    const f    = _net.cells.find(c => c.id === inp.from_cell);
    const t    = _net.cells.find(c => c.id === inp.to_cell);
    if (!f || !t) return { success: false, error: 'Cell not found' };
    // validate conditions
    if (f.load < _cfg.LOAD_BALANCE_TRIGGER) return { success: false, error: `${inp.from_cell} load ${(f.load*100).toFixed(0)}% is below trigger threshold ${(_cfg.LOAD_BALANCE_TRIGGER*100).toFixed(0)}%` };
    if (t.load > _cfg.LOAD_BALANCE_TARGET + rule.min_target_headroom) return { success: false, error: `${inp.to_cell} load ${(t.load*100).toFixed(0)}% has insufficient headroom` };
    // check cooldown
    const now = Date.now();
    cooldowns[f.id] = cooldowns[f.id] || {};
    if (cooldowns[f.id].LOAD_BALANCER && (now - cooldowns[f.id].LOAD_BALANCER) < rule.cooldown_seconds * 1000) {
      return { success: false, error: 'Cooldown active for this cell' };
    }
    // try real backend
    if (!_net.simMode) {
      try {
        await fetch(B + '/api/xapps/load-balance', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(inp),
        });
      } catch (_) {}
    }
    const mv = Math.min(inp.ue_count, f.ues, rule.max_ues_to_move);
    f.ues   -= mv; f.load = Math.max(.05, f.load - mv * .13);
    t.ues   += mv; t.load = Math.min(.97, t.load + mv * .09);
    if (f.load < .85) f.status = 'ONLINE';
    cooldowns[f.id].LOAD_BALANCER = now;
    _ui.addLog(`Handover: ${mv} UEs ${inp.from_cell} → ${inp.to_cell}`);
    return { success: true, moved: mv, from_load_now: f.load.toFixed(2), to_load_now: t.load.toFixed(2) };
  }

  if (name === 'start_xapp') {
    const rule = _cfg.XAPP_RULES[inp.xapp_name];
    if (!_net.simMode && rule) {
      // GRU_HANDOVER: real trigger server
      if (inp.xapp_name === 'GRU_HANDOVER' && rule.start_url) {
        try {
          await fetch(rule.start_url, { method: 'POST', body: 'start' });
        } catch (e) { _ui.addLog('GRU trigger: ' + e.message); }
      }
      // generic backend
      try {
        await fetch(B + '/api/xapps/start', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ xapp_name: inp.xapp_name, target_cell: inp.target_cell }),
        });
      } catch (_) {}
    }
    if (!_net.xapps.includes(inp.xapp_name)) _net.xapps.push(inp.xapp_name);
    // update cell xapps list
    if (inp.target_cell) {
      const c = _net.cells.find(c => c.id === inp.target_cell);
      if (c && !c.xapps.includes(inp.xapp_name)) c.xapps.push(inp.xapp_name);
    } else {
      _net.cells.forEach(c => { if (!c.xapps.includes(inp.xapp_name)) c.xapps.push(inp.xapp_name); });
    }
    _ui.addLog(`xApp started: ${inp.xapp_name}`);
    return { success: true };
  }

  if (name === 'stop_xapp') {
    const rule = _cfg.XAPP_RULES[inp.xapp_name];
    if (!_net.simMode && rule) {
      if (inp.xapp_name === 'GRU_HANDOVER' && rule.stop_url) {
        try { await fetch(rule.stop_url, { method: 'POST', body: 'stop' }); } catch (_) {}
      }
      try {
        await fetch(B + '/api/xapps/stop', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ xapp_name: inp.xapp_name }),
        });
      } catch (_) {}
    }
    _net.xapps = _net.xapps.filter(x => x !== inp.xapp_name);
    _net.cells.forEach(c => { c.xapps = (c.xapps || []).filter(x => x !== inp.xapp_name); });
    _ui.addLog(`xApp stopped: ${inp.xapp_name}`);
    return { success: true };
  }

  if (name === 'set_scenario') {
    if (!_net.simMode) {
      try {
        await fetch(B + '/api/scenario/activate', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ scenario: inp.scenario }),
        });
      } catch (_) {}
    }
    _net.scenario = inp.scenario;
    const sc = _cfg.SCENARIOS[inp.scenario];
    if (sc && sc.loads) {
      _net.cells.forEach((c, i) => {
        if (i < sc.loads.length) {
          c.load   = sc.loads[i];
          c.status = c.load > .90 ? 'DEGRADED' : 'ONLINE';
        }
      });
    }
    _ui.addLog(`Scenario activated: ${inp.scenario}`);
    return { success: true };
  }

  if (name === 'restart_component') {
    if (!_net.simMode) {
      try {
        await fetch(B + '/api/ric/restart', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ component: inp.component }),
        });
      } catch (_) {}
    }
    const cell = _net.cells.find(c => c.id === inp.component);
    if (cell) {
      cell.status = 'RESTARTING'; cell.load = .1;
      setTimeout(() => { cell.status = 'ONLINE'; cell.load = .3; }, 3500);
    } else if (inp.component === 'RIC') {
      _net.ric.status = 'RESTARTING';
      setTimeout(() => { _net.ric.status = 'ONLINE'; }, 3000);
    }
    _ui.addLog(`Restarting: ${inp.component}`);
    return { success: true };
  }

  if (name === 'adjust_cell_params') {
    if (!_net.simMode) {
      try {
        await fetch(B + '/api/cells/params', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(inp),
        });
      } catch (_) {}
    }
    _ui.addLog(`Params adjusted: ${inp.cell_id}`);
    return { success: true };
  }

  if (name === 'resolve_alert') {
    if (!_net.simMode) {
      try {
        await fetch(B + '/api/alerts/resolve', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ alert_id: inp.alert_id }),
        });
      } catch (_) {}
    }
    _net.alerts = _net.alerts.filter(a => a.id !== inp.alert_id);
    _ui.addLog(`Alert resolved: ${inp.alert_id}`);
    return { success: true };
  }

  return { error: 'Unknown tool: ' + name };
}

// ── System prompt builder ─────────────────────────────────────
function buildSystemPrompt() {
  const cellLines = _net.cells.map(c =>
    `  - ${c.id}: load=${Math.round(c.load * 100)}% status=${c.status} ues=${c.ues} lat=${(c.lat||1).toFixed(1)}ms thr=${(c.thr||2).toFixed(1)}G xapps=[${(c.xapps||[]).join(',')}]`
  ).join('\n');
  const rules = Object.entries(_cfg.XAPP_RULES).map(([k, v]) =>
    `  - ${k}: trigger="${v.trigger_condition}" cooldown=${v.cooldown_seconds}s`
  ).join('\n');
  return `You are an autonomous 5G O-RAN network operator AI agent with full authority to execute network operations immediately.

Current network state:
${cellLines}
  - RIC: ${_net.ric.status}
  - Active scenario: ${_net.scenario}
  - Running xApps: ${_net.xapps.join(', ') || 'none'}
  - Available xApps: ${Object.keys(_cfg.XAPP_RULES).join(', ')}
  - Available scenarios: ${Object.keys(_cfg.SCENARIOS).join(', ')}
  - Active alerts: ${_net.alerts.map(a => `[${a.id}] ${a.msg}`).join('; ') || 'none'}

xApp rules to follow:
${rules}

Load thresholds:
  - GREEN < ${_cfg.LOAD_GREEN} | YELLOW < ${_cfg.LOAD_YELLOW} | ORANGE < ${_cfg.LOAD_ORANGE} | CRITICAL >= ${_cfg.CRITICAL_LOAD}
  - Load balance trigger: ${_cfg.LOAD_BALANCE_TRIGGER} | target: ${_cfg.LOAD_BALANCE_TARGET}

Behavioral rules:
1. Always call get_network_status first before any action
2. Before executing xApp actions, validate trigger and target conditions
3. Respect cooldown periods per cell per xApp
4. Explain which rule was matched and why in your response
5. If conditions not met, explain what needs to change first
6. Respond in the same language the user writes in (Arabic or English)
7. Be concise and action-focused`;
}

// ── Agentic loop (Gemini) ─────────────────────────────────────
async function runAgent(userText, isAuto) {
  if (!_cfg.GEMINI_API_KEY) {
    _ui.addMsg('a', '⚠ No Gemini API key set. Enter it in the popup or add to GEMINI_API_KEY in src/config.js.');
    return;
  }
  const fnDecls = buildTools();
  const sys     = buildSystemPrompt();
  const userMsg = { role: 'user', parts: [{ text: userText }] };
  if (!isAuto) conv.push(userMsg);
  const msgs = isAuto ? [userMsg] : conv.slice(-20);
  _ui.showTyping();
  let allText = '', loop = 0;

  while (loop < _cfg.AGENT_LOOP_LIMIT) {
    loop++;
    let data;
    try {
      const res = await fetch(
        `https://generativelanguage.googleapis.com/v1beta/models/${_cfg.AGENT_MODEL}:generateContent?key=${_cfg.GEMINI_API_KEY}`,
        {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            system_instruction: { parts: [{ text: sys }] },
            contents:           msgs,
            tools:              [{ function_declarations: fnDecls }],
            generationConfig:   { maxOutputTokens: _cfg.AGENT_MAX_TOKENS },
          }),
        }
      );
      data = await res.json();
    } catch (e) {
      _ui.hideTyping();
      _ui.addMsg('a', 'Network error: ' + e.message);
      _ui.setError();
      _ui.addLog('API error: ' + e.message);
      return;
    }

    if (!data.candidates || !data.candidates[0]) {
      const errMsg = data.error?.message || JSON.stringify(data).slice(0, 120);
      _ui.addMsg('a', '⚠ Gemini error: ' + errMsg);
      _ui.setError();
      _ui.addLog('Gemini error: ' + errMsg.slice(0, 60));
      break;
    }

    const candidate = data.candidates[0];
    const parts     = candidate.content?.parts || [];

    // collect text
    const texts = parts.filter(p => p.text).map(p => p.text).join('\n');
    if (texts) allText += texts + '\n';

    // collect function calls
    const fnCalls = parts.filter(p => p.functionCall);
    if (fnCalls.length === 0) break;

    // add model turn to conversation
    msgs.push({ role: 'model', parts });

    // execute tools, build functionResponse parts
    const responseParts = await Promise.all(fnCalls.map(async p => {
      const { name, args } = p.functionCall;
      _ui.addLog(name + '(' + JSON.stringify(args).slice(0, 50) + ')');
      const result = await execTool(name, args || {});
      return { functionResponse: { name, response: { result } } };
    }));

    msgs.push({ role: 'user', parts: responseParts });

    if (candidate.finishReason === 'STOP') break;
  }

  _ui.hideTyping();
  if (allText.trim()) {
    _ui.addMsg('a', allText.trim());
    if (!isAuto) conv.push({ role: 'model', parts: [{ text: allText.trim() }] });
  }
}

// ── Auto-heal ─────────────────────────────────────────────────
function autoHeal(cellId, alertId, issueType) {
  _ui.addMsg('a', `AUTO-DETECTED: ${cellId} — ${issueType}\nInitiating autonomous recovery...`);
  _ui.setBusy(true);
  runAgent(
    `AUTONOMOUS ACTION REQUIRED: ${cellId} has ${issueType}. ` +
    `Current network state has been injected above. ` +
    `Check the relevant xApp rules, validate all conditions, and fix immediately. ` +
    `Resolve alert ${alertId} after fixing. Do not ask for confirmation. Act now.`,
    true
  ).then(() => _ui.setBusy(false));
}

// ── Autonomous monitoring ─────────────────────────────────────
function startMonitoring() {
  setInterval(() => {
    _net.cells.forEach(c => {
      const now = Date.now();
      // critical load
      if (c.load > _cfg.CRITICAL_LOAD && c.status === 'ONLINE') {
        const existing = _net.alerts.find(a => a.cell === c.id && a.type === 'HIGH_LOAD');
        if (!existing) {
          const aid = 'AID_' + now;
          _net.alerts.push({ id: aid, cell: c.id, type: 'HIGH_LOAD', msg: `${c.id} CRIT ${Math.round(c.load * 100)}%` });
          _ui.addLog(`AUTO-DETECT: ${c.id} CRITICAL ${Math.round(c.load * 100)}%`);
          autoHeal(c.id, aid, `critical load ${Math.round(c.load * 100)}%`);
        }
      }
      // degraded cell
      if (c.status === 'DEGRADED') {
        const existing = _net.alerts.find(a => a.cell === c.id && a.type === 'DEGRADED');
        if (!existing) {
          const aid = 'AID_DEG_' + now;
          _net.alerts.push({ id: aid, cell: c.id, type: 'DEGRADED', msg: `${c.id} DEGRADED` });
          _ui.addLog(`AUTO-DETECT: ${c.id} DEGRADED`);
          autoHeal(c.id, aid, 'DEGRADED status');
        }
      }
      // high latency
      if ((c.lat || 0) > _cfg.XAPP_RULES.QOS.latency_threshold_ms) {
        const existing = _net.alerts.find(a => a.cell === c.id && a.type === 'HIGH_LATENCY');
        if (!existing) {
          const aid = 'AID_LAT_' + now;
          _net.alerts.push({ id: aid, cell: c.id, type: 'HIGH_LATENCY', msg: `${c.id} latency ${(c.lat).toFixed(1)}ms` });
          _ui.addLog(`AUTO-DETECT: ${c.id} high latency ${(c.lat).toFixed(1)}ms`);
        }
      }
    });
  }, _cfg.MONITOR_INTERVAL_MS);
}

// ── Init ──────────────────────────────────────────────────────
export function initAgent(net, cfg, ui) {
  _net = net; _cfg = cfg; _ui = ui;

  // welcome message
  const xappStr = Object.keys(cfg.XAPP_RULES).join(', ');
  ui.addMsg('a',
    `O-RAN AI Agent online. Monitoring ${net.cells.length} gNB towers.\n\n` +
    `Available xApps: ${xappStr}\n` +
    `Active scenario: ${net.scenario}\n\n` +
    `Use quick commands above or type a command.`
  );

  window.__toggleAgent = () => {
    agentOpen = !agentOpen;
    const panel = document.getElementById('agent');
    const tog   = document.getElementById('a-tog');
    if (panel) panel.className = agentOpen ? '' : 'shut';
    if (tog)   tog.innerHTML   = agentOpen ? '&#9654;' : '&#9664;';
  };

  window.__qcmd = txt => {
    const inp = document.getElementById('inp');
    if (inp) inp.value = txt;
    window.__doSend();
  };

  window.__doSend = () => {
    const inp = document.getElementById('inp');
    const txt = inp ? inp.value.trim() : '';
    if (!txt) return;
    if (inp) inp.value = '';
    ui.addMsg('u', txt);
    ui.setBusy(true);
    runAgent(txt, false).then(() => ui.setBusy(false));
  };

  return { startMonitoring };
}
