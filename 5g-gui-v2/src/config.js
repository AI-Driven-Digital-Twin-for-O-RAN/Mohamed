// ── Cell definitions: 1 LTE macro + 7 mmWave small cells ──────
export const ALL_CELLS = [
  // LTE macro — index 0, tall tower, wide coverage
  { id: 'LTE-001',  x:   0, z:   0, initLoad: 0.62, color: '#00aaff', type: 'lte' },
  // mmWave small cells — indices 1-7, spread at radius ~80-95 from center
  { id: 'mmW-001',  x: -78, z: -38, initLoad: 0.45, color: '#00ff88', type: 'mmwave' },
  { id: 'mmW-002',  x:  78, z: -38, initLoad: 0.78, color: '#ffcc00', type: 'mmwave' },
  { id: 'mmW-003',  x: -92, z:  14, initLoad: 0.55, color: '#ff6600', type: 'mmwave' },
  { id: 'mmW-004',  x:  92, z:  14, initLoad: 0.38, color: '#aa44ff', type: 'mmwave' },
  { id: 'mmW-005',  x: -22, z: -90, initLoad: 0.91, color: '#00ccff', type: 'mmwave' },
  { id: 'mmW-006',  x:  22, z: -90, initLoad: 0.42, color: '#ff2244', type: 'mmwave' },
  { id: 'mmW-007',  x:   0, z:  88, initLoad: 0.33, color: '#44ff88', type: 'mmwave' },
];

export const CONFIG = {
  // Active topology — updated by applyScenario()
  CELLS:            ALL_CELLS.slice(0, 3),
  NUM_CELLS:        3,
  NUM_UES_PER_CELL: 3,

  // ── RF ──
  TX_POWER_DBM:      78,
  FREQUENCY_GHZ:     3.5,
  BEAM_TILT_DEG:    -6,
  BANDWIDTH_MHZ:     100,
  COVERAGE_RADIUS_M: 650,

  // ── Thresholds ──
  LOAD_GREEN:           0.45,
  LOAD_YELLOW:          0.70,
  LOAD_ORANGE:          0.88,
  LOAD_BALANCE_TRIGGER: 0.85,
  LOAD_BALANCE_TARGET:  0.60,
  CRITICAL_LOAD:        0.92,

  // ── Scenarios ─────────────────────────────────────────────────
  SCENARIOS: {
    DEFAULT: {
      label: 'Default',
      cellCount: 3, totalUEs: 9,
      loadRange: [0.35, 0.75],
      description: '1 LTE + 2 mmWave — 9 UEs',
    },
    GRU_SCENARIO: {
      label: 'GRU xApp',
      cellCount: 8, totalUEs: 20,
      loadRange: [0.55, 0.75], spikeFraction: 0.33, spikeLoad: 0.91,
      description: 'GRU handover optimization — 1 LTE + 7 mmWave, 20 UEs',
    },
    LB_SCENARIO: {
      label: 'Load Balance',
      cellCount: 8, totalUEs: 20,
      loadRange: [0.15, 0.35], spikeFraction: 0.40, spikeLoad: 0.87,
      description: 'AWF load balancing — 1 LTE + 7 mmWave, 20 UEs',
    },
    DENSE_URBAN: {
      label: 'Dense Urban',
      cellCount: 6, totalUEs: 18,
      loadRange: [0.60, 0.82],
      description: 'High-density — 1 LTE + 5 mmWave, 18 UEs',
    },
    RURAL: {
      label: 'Rural',
      cellCount: 3, totalUEs: 6,
      loadRange: [0.08, 0.25],
      description: 'Low-density rural — 3 cells, 6 UEs',
    },
    STRESS_TEST: {
      label: 'Stress Test',
      cellCount: 8, totalUEs: 20,
      loadRange: [0.91, 0.97],
      description: 'Maximum load — all 8 cells, 20 UEs',
    },
    IDLE: {
      label: 'Idle',
      cellCount: 3, totalUEs: 3,
      loadRange: [0.04, 0.14],
      description: 'Minimal traffic',
    },
    PEAK_HOURS: {
      label: 'Peak Hours',
      cellCount: 6, totalUEs: 18,
      loadRange: [0.72, 0.91],
      description: 'Rush hour — 1 LTE + 5 mmWave, 18 UEs',
    },
  },

  // ── xApp rules ──
  XAPP_RULES: {
    GRU_HANDOVER: {
      display_name: 'GRU xAPP',
      trigger_condition: 'cell.load > 0.70',
      max_concurrent: 2, cooldown_seconds: 30,
      rssi_threshold_dbm: -100, hysteresis_db: 3, time_to_trigger_ms: 320,
      start_url: 'http://localhost:38868/',
      stop_url:  'http://localhost:38869/',
      inference_server: 'http://localhost:5000',
    },
    LOAD_BALANCER: {
      display_name: 'LB xAPP',
      trigger_condition: 'cell.load > LOAD_BALANCE_TRIGGER',
      target_condition: 'cell.load < LOAD_BALANCE_TARGET',
      max_ues_to_move: 2, cooldown_seconds: 30,
    },
  },

  BACKEND_URL: '/api',
  GUI_PORT:    3001,

  GEMINI_API_KEY: 'AIzaSyAVTfoSYD-9wKGn1f_IFUxdAjZQxG--qkc',
  AGENT_MODEL:    'gemini-2.5-flash',

  CAMERA_INIT_R: 70,
};

export function getFullCellList(cfg) { return cfg.CELLS; }
