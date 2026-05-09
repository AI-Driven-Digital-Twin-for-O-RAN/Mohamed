/*
 * sinr-ho-xApp[RB] — Smart SINR-based continuous handover xApp for ns-O-RAN-flexric.
 * Uses KPM v3 UE SINR measurements to evaluate serving/neighbor cells and
 * dynamically trigger RIC Control handover commands via FlexRIC’s E2SM-RC.
 * Implements multi-handover logic, anti ping‑pong protection, and robust
 * UE tracking across mmWave cells, with rich, colorized runtime logging.
 *
 * Authors:
 *   Youssef Fathy <youssefathy1@gmail.com>
 *   Alyaa Mohamed <alyaamohammed251@gmail.com>
 *   Omar Farouk   <omar297farouk@gmail.com>
 *
 * Suez Canal University
 * Feb 2026
 */

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_struct.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/sm/rc_sm/rc_sm_id.h"
#include "../../../../src/sm/rc_sm/ie/rc_data_ie.h"
#include "../../../../src/util/e.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// ============================================================
//  ANSI Color & Style Definitions
// ============================================================
#define CLR_RESET     "\033[0m"
#define CLR_BOLD      "\033[1m"
#define CLR_DIM       "\033[2m"

#define CLR_BLACK     "\033[30m"
#define CLR_RED       "\033[31m"
#define CLR_GREEN     "\033[32m"
#define CLR_YELLOW    "\033[33m"
#define CLR_BLUE      "\033[34m"
#define CLR_MAGENTA   "\033[35m"
#define CLR_CYAN      "\033[36m"
#define CLR_WHITE     "\033[37m"

#define CLR_BRED      "\033[1;31m"
#define CLR_BGREEN    "\033[1;32m"
#define CLR_BYELLOW   "\033[1;33m"
#define CLR_BBLUE     "\033[1;34m"
#define CLR_BMAGENTA  "\033[1;35m"
#define CLR_BCYAN     "\033[1;36m"
#define CLR_BWHITE    "\033[1;37m"

#define CLR_BG_BLUE   "\033[44m"
#define CLR_BG_GREEN  "\033[42m"
#define CLR_BG_RED    "\033[41m"

// ============================================================
//  Logging Helpers
// ============================================================
static inline void _xapp_timestamp(char* buf, size_t sz) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buf, sz, "%H:%M:%S", t);
}

#define LOG_INFO(fmt, ...)  do { char _ts[16]; _xapp_timestamp(_ts,sizeof(_ts)); \
    printf(CLR_CYAN "[%s]" CLR_RESET CLR_BWHITE " INFO  " CLR_RESET " " fmt "\n", _ts, ##__VA_ARGS__); } while(0)

#define LOG_OK(fmt, ...)    do { char _ts[16]; _xapp_timestamp(_ts,sizeof(_ts)); \
    printf(CLR_CYAN "[%s]" CLR_RESET CLR_BGREEN " OK    " CLR_RESET " " fmt "\n", _ts, ##__VA_ARGS__); } while(0)

#define LOG_WARN(fmt, ...)  do { char _ts[16]; _xapp_timestamp(_ts,sizeof(_ts)); \
    printf(CLR_CYAN "[%s]" CLR_RESET CLR_BYELLOW " WARN  " CLR_RESET " " fmt "\n", _ts, ##__VA_ARGS__); } while(0)

#define LOG_ERR(fmt, ...)   do { char _ts[16]; _xapp_timestamp(_ts,sizeof(_ts)); \
    fprintf(stderr, CLR_CYAN "[%s]" CLR_RESET CLR_BRED " ERROR " CLR_RESET " " fmt "\n", _ts, ##__VA_ARGS__); } while(0)

#define LOG_HO(fmt, ...)    do { char _ts[16]; _xapp_timestamp(_ts,sizeof(_ts)); \
    printf(CLR_CYAN "[%s]" CLR_RESET CLR_BMAGENTA " HO    " CLR_RESET " " fmt "\n", _ts, ##__VA_ARGS__); } while(0)

#define LOG_KPM(fmt, ...)   do { char _ts[16]; _xapp_timestamp(_ts,sizeof(_ts)); \
    printf(CLR_CYAN "[%s]" CLR_RESET CLR_BBLUE   " KPM   " CLR_RESET " " fmt "\n", _ts, ##__VA_ARGS__); } while(0)

#define LOG_TRACK(fmt, ...) do { char _ts[16]; _xapp_timestamp(_ts,sizeof(_ts)); \
    printf(CLR_CYAN "[%s]" CLR_RESET CLR_DIM      " TRACK " CLR_RESET " " fmt "\n", _ts, ##__VA_ARGS__); } while(0)

#define LOG_SECTION(label) do { \
    printf("\n" CLR_BBLUE "┌─────────────────────────────────────────┐\n" \
           "│  %-40s│\n" \
           "└─────────────────────────────────────────┘" CLR_RESET "\n", label); } while(0)

#define LOG_DIVIDER() printf(CLR_DIM "  ─────────────────────────────────────────" CLR_RESET "\n")

// ============================================
// CONFIGURABLE HANDOVER PARAMETERS
// ============================================
#define HANDOVER_THRESHOLD_DB 3.0      // SINR difference threshold for handover
#define MIN_HO_INTERVAL_SEC 5          // Minimum time between consecutive handovers per UE
#define MIN_SINR -10                   // Minimum acceptable SINR
#define HO_COMPLETION_TIMEOUT_SEC 3    // Time to assume HO completed if no explicit confirmation

typedef struct {
    const e2_node_arr_xapp_t* nodes;
    struct SINRNeighboringValues* neighCells;
    int ueID;
    uint8_t frmCurntCell;
    uint8_t toTargetCell;
} callback_data_t;

typedef uint16_t (*Callback)(callback_data_t);

typedef enum {
    CONNECTED_MODE_MOBILITY = 3,
    ENERGY_STATE = 300
} rc_ctrl_service_style_id_e;
  
typedef enum {
    CELL_OFF = '0',
    CELL_ON = '1',  
    CELL_SLEEP = '2',
} cell_state_e;
  
typedef enum {
    QOS_FLOW_ID_1 = 1,
    QOS_FLOW_ID_10 = 10
} qos_flow_id_e;

typedef enum {
    DRB_ID_3 = 3
} drb_id_e;
  
typedef enum {
    PDU_SESSION_ID_5 = 5
} pdu_session_id_e;

// ============================================
// Handover State Management
// ============================================
typedef enum {
    HO_STATE_IDLE = 0,
    HO_STATE_DECISION_MADE,
    HO_STATE_COMMAND_SENT,
    HO_STATE_IN_PROGRESS,
    HO_STATE_COMPLETED,
    HO_STATE_FAILED
} handover_state_e;

typedef struct {
    handover_state_e state;
    uint16_t source_cell;
    uint16_t target_cell;
    time_t decision_time;
    time_t command_time;
    time_t completion_time;
    time_t last_handover_time;
    int attempts;
    int ue_ho_count;     // total handovers completed for this UE (lifetime)
} handover_context_t;

#define MAX_HANDOVER_TIMEOUT_SEC 5
#define MAX_HANDOVER_ATTEMPTS 3

const char* ho_state_to_string(handover_state_e state) {
    switch(state) {
        case HO_STATE_IDLE: return "IDLE";
        case HO_STATE_DECISION_MADE: return "DECISION_MADE";
        case HO_STATE_COMMAND_SENT: return "COMMAND_SENT";
        case HO_STATE_IN_PROGRESS: return "IN_PROGRESS";
        case HO_STATE_COMPLETED: return "COMPLETED";
        case HO_STATE_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

// handover_xapp.csv — stub replaced below after struct definitions
static FILE*  g_handover_xapp_log        = NULL;
static int    g_handover_xapp_header_done = 0;
// log_handover_xapp() is defined after struct SINRServingValues (see below)
  
static ue_id_e2sm_t ue_id;

static uint64_t const period_ms = 100;

static pthread_mutex_t mtx;

static void log_gnb_ue_id(ue_id_e2sm_t ue_id) 
{
  if (ue_id.gnb.gnb_cu_ue_f1ap_lst != NULL) 
  {
    for (size_t i = 0; i < ue_id.gnb.gnb_cu_ue_f1ap_lst_len; i++) 
    {
      LOG_TRACK("UE-ID gNB-CU  gnb_cu_ue_f1ap=%-6u", ue_id.gnb.gnb_cu_ue_f1ap_lst[i]);
    }
  } else 
  {
    LOG_TRACK("UE-ID gNB     amf_ue_ngap_id=%lu", ue_id.gnb.amf_ue_ngap_id);
  }
  if (ue_id.gnb.ran_ue_id != NULL) 
  {
    LOG_TRACK("              ran_ue_id=0x%lx", *ue_id.gnb.ran_ue_id);
  }
}

static void log_du_ue_id(ue_id_e2sm_t ue_id) 
{
  LOG_TRACK("UE-ID gNB-DU  gnb_cu_ue_f1ap=%u", ue_id.gnb_du.gnb_cu_ue_f1ap);
  if (ue_id.gnb_du.ran_ue_id != NULL) 
  {
    LOG_TRACK("              ran_ue_id=0x%lx", *ue_id.gnb_du.ran_ue_id);
  }
}

static void log_cuup_ue_id(ue_id_e2sm_t ue_id) 
{
  LOG_TRACK("UE-ID gNB-CU-UP  gnb_cu_cp_ue_e1ap=%u", ue_id.gnb_cu_up.gnb_cu_cp_ue_e1ap);
  if (ue_id.gnb_cu_up.ran_ue_id != NULL) 
  {
    LOG_TRACK("                 ran_ue_id=0x%lx", *ue_id.gnb_cu_up.ran_ue_id);
  }
}

typedef void (*log_ue_id)(ue_id_e2sm_t ue_id);

static log_ue_id log_ue_id_e2sm[END_UE_ID_E2SM] = 
{
    log_du_ue_id,
    log_cuup_ue_id,
    NULL,
    NULL,
    NULL,
    NULL,
};

static void log_int_value(byte_array_t name, meas_record_lst_t meas_record) 
{
  if (cmp_str_ba("RRU.PrbTotDl", name) == 0) {
    LOG_KPM("RRU.PrbTotDl = " CLR_BYELLOW "%d" CLR_RESET " PRBs", meas_record.int_val);
    LOG_KPM("RRU.PrbTotUl = " CLR_BYELLOW "%d" CLR_RESET " PRBs", meas_record.int_val);
  } else if (cmp_str_ba("DRB.PdcpSduVolumeDL", name) == 0) {
    LOG_KPM("DRB.PdcpSduVolumeDL = " CLR_BYELLOW "%d" CLR_RESET " kb", meas_record.int_val);
  } else if (cmp_str_ba("DRB.PdcpSduVolumeUL", name) == 0) {
    LOG_KPM("DRB.PdcpSduVolumeUL = " CLR_BYELLOW "%d" CLR_RESET " kb", meas_record.int_val);
  }
}

static void log_real_value(byte_array_t name, meas_record_lst_t meas_record) 
{
  if (cmp_str_ba("DRB.RlcSduDelayDl", name) == 0) {
    LOG_KPM("RlcSduDelayDl = " CLR_BYELLOW "%.2f" CLR_RESET " μs", meas_record.real_val);
  } else if (cmp_str_ba("DRB.UEThpDl", name) == 0) {
    LOG_KPM("UEThpDl       = " CLR_BYELLOW "%.2f" CLR_RESET " kbps", meas_record.real_val);
  } else if (cmp_str_ba("DRB.UEThpUl", name) == 0) {
    LOG_KPM("UEThpUl       = " CLR_BYELLOW "%.2f" CLR_RESET " kbps", meas_record.real_val);
  } else if (strncmp(name.buf, "L3servingSINR3gpp_cell_", strlen("L3servingSINR3gpp_cell_")) == 0) {
    LOG_KPM(CLR_DIM "%s" CLR_RESET " sinr=" CLR_BBLUE "%.4f dB" CLR_RESET, name.buf, meas_record.real_val);
  } else if (strncmp(name.buf, "L3neighSINRListOf_UEID_", strlen("L3neighSINRListOf_UEID_")) == 0) {
    LOG_KPM(CLR_DIM "%s" CLR_RESET " sinr=" CLR_BBLUE "%.4f dB" CLR_RESET, name.buf, meas_record.real_val);
  }
}

typedef void (*log_meas_value)(byte_array_t name, meas_record_lst_t meas_record);

static log_meas_value get_meas_value[END_MEAS_VALUE] = {
    log_int_value,
    log_real_value,
    NULL,
};

static void match_meas_name_type(meas_type_t meas_type, meas_record_lst_t meas_record) 
{
  get_meas_value[meas_record.value](meas_type.name, meas_record);
}

static void match_id_meas_type(meas_type_t meas_type, meas_record_lst_t meas_record) 
{
  (void)meas_type;
  (void)meas_record;
  assert(false && "ID Measurement Type not yet supported");
}

typedef void (*check_meas_type)(meas_type_t meas_type, meas_record_lst_t meas_record);

static check_meas_type match_meas_type[END_MEAS_TYPE] = 
{
    match_meas_name_type,
    match_id_meas_type,
};

// ============================================
// SINR Data Structures
// ============================================
struct SINRNeighboringValues 
{
  bool is_available;
  uint16_t neighCellID;
  double sinr;
};

struct SINRServingValues 
{
  bool is_available;
  uint16_t ueID;
  double sinr;
  struct SINRNeighboringValues* neighCells;
  size_t numOfNeighCells;
  handover_context_t ho_context;
};

struct SINR_Map 
{
  uint16_t cellID;
  struct SINRServingValues* connectedUEs;
  size_t numOfConnectedUEs;
  bool is_running;
};

struct registeredCells {
  bool is_registered;
  struct SINR_Map* sinrMap;
};

#define MAX_REGISTERED_CELLS 10
// NOTE: In this scenario UE IDs are 1..20 (not 0-based).
// This xApp indexes arrays by UE ID directly, so we must provision an extra margin
// and guard against out-of-range UE IDs to avoid heap corruption (double free, etc.).
#define MAX_REGISTERED_UES 64
#define MAX_REGISTERED_NEIGHBOURS 20

struct registeredCells cells_sinr_map[MAX_REGISTERED_CELLS] = {{false, NULL}};

// ============================================================
//  BASELINE METRICS LOGGING
//  Same 9-column CSV schema as the improved version so results
//  can be compared column-by-column without any reformatting.
//
//  Placed HERE (after all struct definitions) so log_handover_xapp
//  can safely read sinr from SINRServingValues without forward-decl errors.
//
//  handover_xapp.csv:
//    time_sec, ue_id, from_cell, to_cell, event, executed_ok,
//    serving_sinr_before, latency_sec, is_ping_pong
//  sinr_xapp.csv:
//    time_sec, ue_id, cell_id, sinr_db   (raw — no averaging in baseline)
// ============================================================

// ---------- ping-pong tracking ----------
#define PING_PONG_WINDOW_SEC 10
static uint16_t g_ue_prev_cell[MAX_REGISTERED_UES]      = {0};
static time_t   g_ue_prev_cell_time[MAX_REGISTERED_UES] = {0};
static int      g_total_ping_pong_count                  = 0;

// ---------- latency tracking (COMMAND_SENT timestamp per UE) ----------
static time_t g_ue_cmd_time[MAX_REGISTERED_UES] = {0};

// ---------- SINR snapshot log ----------
static FILE* g_sinr_log        = NULL;
static int   g_sinr_log_header = 0;

static FILE* baseline_get_ho_log(void)
{
    if (g_handover_xapp_log) return g_handover_xapp_log;
    const char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/handover_xapp.csv", (home && home[0]) ? home : ".");
    g_handover_xapp_log = fopen(path, "a");
    if (g_handover_xapp_log && !g_handover_xapp_header_done) {
        fprintf(g_handover_xapp_log,
                "time_sec,ue_id,from_cell,to_cell,event,executed_ok,"
                "serving_sinr_before,latency_sec,is_ping_pong\n");
        g_handover_xapp_header_done = 1;
    }
    return g_handover_xapp_log;
}

static FILE* baseline_get_sinr_log(void)
{
    if (g_sinr_log) return g_sinr_log;
    const char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/sinr_xapp.csv", (home && home[0]) ? home : ".");
    g_sinr_log = fopen(path, "a");
    if (g_sinr_log && !g_sinr_log_header) {
        fprintf(g_sinr_log, "time_sec,ue_id,cell_id,sinr_db\n");
        g_sinr_log_header = 1;
    }
    return g_sinr_log;
}

// Log raw SINR snapshot — called from add_UE on every KPM cycle.
static void baseline_log_sinr(uint16_t ue_id, uint16_t cell_id, double sinr)
{
    FILE* f = baseline_get_sinr_log();
    if (!f) return;
    fprintf(f, "%ld,%u,%u,%.4f\n", (long)time(NULL), ue_id, cell_id, sinr);
    fflush(f);
}

// Detect ping-pong: returns 1 if this HO is back to a recently-left cell.
static int baseline_is_ping_pong(uint16_t ue_id, uint16_t from_cell, uint16_t to_cell)
{
    if (ue_id == 0 || ue_id >= MAX_REGISTERED_UES) return 0;
    int pp = 0;
    if (g_ue_prev_cell[ue_id] == to_cell) {
        double delta = difftime(time(NULL), g_ue_prev_cell_time[ue_id]);
        if (delta < PING_PONG_WINDOW_SEC) { pp = 1; g_total_ping_pong_count++; }
    }
    g_ue_prev_cell[ue_id]      = from_cell;
    g_ue_prev_cell_time[ue_id] = time(NULL);
    return pp;
}

// Main HO event logger — 9-column output, identical schema to improved version.
static void log_handover_xapp(uint16_t ue_id, uint16_t from_cell, uint16_t to_cell,
                               const char* event, int executed_ok)
{
    FILE* f = baseline_get_ho_log();
    if (!f) return;

    // Read raw SINR directly from the cell map (structs are fully defined here).
    double serving_sinr = 0.0;
    if (from_cell < MAX_REGISTERED_CELLS &&
        cells_sinr_map[from_cell].is_registered &&
        cells_sinr_map[from_cell].sinrMap != NULL &&
        cells_sinr_map[from_cell].sinrMap->connectedUEs != NULL &&
        ue_id > 0 && ue_id < MAX_REGISTERED_UES &&
        cells_sinr_map[from_cell].sinrMap->connectedUEs[ue_id].is_available)
        serving_sinr = cells_sinr_map[from_cell].sinrMap->connectedUEs[ue_id].sinr;

    double latency_sec  = 0.0;
    int    is_ping_pong = 0;

    if (strcmp(event, "COMMAND_SENT") == 0 && ue_id > 0 && ue_id < MAX_REGISTERED_UES)
        g_ue_cmd_time[ue_id] = time(NULL);

    if (strcmp(event, "COMPLETED") == 0 && ue_id > 0 && ue_id < MAX_REGISTERED_UES) {
        if (g_ue_cmd_time[ue_id] > 0)
            latency_sec = difftime(time(NULL), g_ue_cmd_time[ue_id]);
        is_ping_pong = baseline_is_ping_pong(ue_id, from_cell, to_cell);
    }

    fprintf(f, "%ld,%u,%u,%u,%s,%d,%.4f,%.2f,%d\n",
            (long)time(NULL), ue_id, from_cell, to_cell,
            event, executed_ok, serving_sinr, latency_sec, is_ping_pong);
    fflush(f);
}

// ============================================
// Global UE state (authoritative)
// ============================================
// The RIC controls handover. To support reliable multi-handover and avoid stale KPM races
// where the same UE appears in multiple cells, we keep a single authoritative context per UE.
static handover_context_t g_ue_ho_ctx[MAX_REGISTERED_UES] = {0};
static uint16_t g_ue_serving_cell[MAX_REGISTERED_UES] = {0};
static uint16_t g_ue_candidate_cell[MAX_REGISTERED_UES] = {0};
static uint8_t  g_ue_candidate_cnt[MAX_REGISTERED_UES] = {0};

// Total handover commands sent since xApp started (system-wide counter).
static int g_total_ho_count = 0;

// ============================================
// Cell-ID → Node-ID Mapping Table
// ============================================
// Populated automatically from incoming KPM reports.
// Each KPM report arrives tagged with the node that sent it (via the per-node
// wrapper callback). The first serving-cell measurement seen from a node is
// used to anchor cell_id → node_id permanently.
//
// cell_id is used as the direct index (cells 2-8 in ns3 scenario).
// Index 0 and 1 are unused (LTE anchor is never a handover target here).

typedef struct {
    bool         mapped;           // true once this cell has been seen in a KPM report
    global_e2_node_id_t node_id;   // copy of the node's global E2 node ID
} cell_node_map_entry_t;

static cell_node_map_entry_t g_cell_node_map[MAX_REGISTERED_CELLS] = {{false, {0}}};

// Register a cell→node mapping (idempotent after first call for that cell).
static void cell_node_map_register(uint16_t cell_id, const global_e2_node_id_t* nid)
{
    if (cell_id == 0 || cell_id >= MAX_REGISTERED_CELLS) return;
    if (g_cell_node_map[cell_id].mapped) return;   // already known
    g_cell_node_map[cell_id].node_id = cp_global_e2_node_id(nid);
    g_cell_node_map[cell_id].mapped  = true;
    LOG_OK("Cell→Node mapping learned: Cell " CLR_BYELLOW "%d" CLR_RESET
           " → NB_ID " CLR_BWHITE "%u" CLR_RESET, cell_id, nid->nb_id.nb_id);
}

// Return pointer to the node_id for cell_id, or NULL if not yet mapped.
static global_e2_node_id_t* cell_node_map_get(uint16_t cell_id)
{
    if (cell_id == 0 || cell_id >= MAX_REGISTERED_CELLS) return NULL;
    if (!g_cell_node_map[cell_id].mapped) return NULL;
    return &g_cell_node_map[cell_id].node_id;
}

// Print the full cell→node mapping table. Call after first KPM cycle to verify.
static void cell_node_map_print(void)
{
    int mapped = 0;
    for (int i = 0; i < MAX_REGISTERED_CELLS; i++)
        if (g_cell_node_map[i].mapped) mapped++;

    if (mapped == 0) {
        LOG_WARN("Cell→Node map: empty (no KPM reports received yet)");
        return;
    }

    printf("\n" CLR_BBLUE
           "  ╔══════════════════════════════════════════════╗\n"
           "  ║      Cell → E2 Node Mapping Table            ║\n"
           "  ╠══════════╦═══════════════════════════════════╣\n"
           "  ║  Cell ID ║  NB_ID (E2 Node)                  ║\n"
           "  ╠══════════╬═══════════════════════════════════╣\n"
           CLR_RESET);

    for (int i = 0; i < MAX_REGISTERED_CELLS; i++) {
        if (!g_cell_node_map[i].mapped) continue;
        printf(CLR_BBLUE "  ║  " CLR_BYELLOW "%-8d" CLR_BBLUE
               "║  " CLR_BWHITE "%-33u" CLR_BBLUE "║\n" CLR_RESET,
               i, g_cell_node_map[i].node_id.nb_id.nb_id);
    }

    printf(CLR_BBLUE
           "  ╚══════════╩═══════════════════════════════════╝\n"
           CLR_RESET "\n");
}

// ============================================
// Per-Node KPM Callback Context
// ============================================
// report_sm_xapp_api accepts a single (sm_cb) function pointer with no user-data
// parameter. To know which node sent a given KPM indication, we register one
// thin wrapper per node. Each wrapper holds a copy of the node's global_e2_node_id_t
// and calls the shared processing logic after updating the cell→node map.

#define MAX_E2_NODES 16

typedef struct {
    global_e2_node_id_t node_id;   // owning copy (freed on xApp exit)
    bool                in_use;
} kpm_cb_ctx_t;

static kpm_cb_ctx_t g_kpm_cb_ctx[MAX_E2_NODES] = {{{0}, false}};

// ============================================
// Global Handover Lock
// ============================================
// Prevents concurrent handovers across all UEs. ns3's epc-x2 layer cannot
// safely handle simultaneous X2 handovers and will assert-fail. Only one
// handover may be IN_PROGRESS system-wide at any time.
static volatile int g_global_ho_active = 0;  // 1 = a handover is in flight

// Forward declaration (used by helpers below)
void remove_UE_from_cell(struct SINR_Map* cell, const uint16_t ueID);

static void reset_ue_candidate(const uint16_t ueID)
{
  if (ueID == 0 || ueID >= MAX_REGISTERED_UES) return;
  g_ue_candidate_cell[ueID] = 0;
  g_ue_candidate_cnt[ueID] = 0;
}

static void evict_ue_from_other_cells(const uint16_t ueID, const uint16_t keep_cellID)
{
  if (ueID == 0 || ueID >= MAX_REGISTERED_UES) return;
  for (int cid = 0; cid < MAX_REGISTERED_CELLS; ++cid) {
    if (!cells_sinr_map[cid].is_registered || cells_sinr_map[cid].sinrMap == NULL)
      continue;
    struct SINR_Map* c = cells_sinr_map[cid].sinrMap;
    if (c->cellID == keep_cellID)
      continue;
    if (c->connectedUEs != NULL && c->connectedUEs[ueID].is_available)
      remove_UE_from_cell(c, ueID);
  }
}

struct SINR_Map* add_SINR(const uint16_t cellID) 
{
  assert(cellID != 0);
  if (cells_sinr_map[cellID].sinrMap == NULL && !cells_sinr_map[cellID].is_registered) 
  {
    cells_sinr_map[cellID].sinrMap = (struct SINR_Map*)calloc(1, sizeof(struct SINR_Map));
    cells_sinr_map[cellID].is_registered = true;
    cells_sinr_map[cellID].sinrMap->cellID = cellID;
    cells_sinr_map[cellID].sinrMap->connectedUEs = NULL;
    cells_sinr_map[cellID].sinrMap->numOfConnectedUEs = 0;
  }
  return cells_sinr_map[cellID].sinrMap;
}

// ============================================
// FIXED: add_UE - Proper UE Counter Management
// ============================================
void add_UE(struct SINR_Map* cell, const uint16_t ueID, const double sinr) 
{
  assert(cell != NULL);

  if (ueID == 0 || ueID >= MAX_REGISTERED_UES) {
    LOG_WARN("add_UE: out-of-range ueID=%u (max=%d) — skipping", ueID, MAX_REGISTERED_UES);
    return;
  }

  // Enforce a single serving cell per UE (stale KPM protection + multi-handover correctness)
  time_t now = time(NULL);
  handover_context_t* ctx = &g_ue_ho_ctx[ueID];
  uint16_t cur_cell = g_ue_serving_cell[ueID];

  if (cur_cell == 0) {
    g_ue_serving_cell[ueID] = cell->cellID;
    reset_ue_candidate(ueID);
  } else if (cur_cell != cell->cellID) {
    // Fast-path: treat serving measurement in target cell as completion
    if (ctx->state == HO_STATE_IN_PROGRESS && ctx->target_cell == cell->cellID) {
      printf("\n" CLR_BGREEN "  ╔══════════════════════════════════════════╗\n"
             "  ║  ✔  HANDOVER COMPLETED                   ║\n"
             "  ║     UE %-4d  Cell %-2d  ──▶  Cell %-2d         ║\n"
             "  ╚══════════════════════════════════════════╝" CLR_RESET "\n",
             ueID, ctx->source_cell, ctx->target_cell);
      log_handover_xapp(ueID, ctx->source_cell, ctx->target_cell, "COMPLETED", 1);

      ctx->ue_ho_count++;
      ctx->state = HO_STATE_IDLE;
      ctx->completion_time = now;
      ctx->last_handover_time = now;
      ctx->attempts = 0;

      g_ue_serving_cell[ueID] = cell->cellID;
      reset_ue_candidate(ueID);

      // Do NOT evict here: UE must stay in old cell until we receive neighbor list
      // for this (target) cell in the same or next KPM. Evict when we add_neighCell
      // for this UE in the serving cell (see log_kpm_measurements).
    } else if (ctx->target_cell == cell->cellID) {
      // Late completion: we sent HO to this cell (e.g. 4) but timed out; RAN did the HO.
      // Accept so multi-handover works (3→4 then 4→5).
      LOG_OK("HO late-confirm  UE " CLR_BYELLOW "%d" CLR_RESET "  now anchored in Cell " CLR_BYELLOW "%d" CLR_RESET,
             ueID, cell->cellID);
      log_handover_xapp(ueID, ctx->source_cell, ctx->target_cell, "COMPLETED", 1);
      ctx->ue_ho_count++;
      ctx->state = HO_STATE_IDLE;
      ctx->completion_time = now;
      ctx->last_handover_time = now;
      ctx->attempts = 0;
      g_ue_serving_cell[ueID] = cell->cellID;
      reset_ue_candidate(ueID);
    } else {
      // Block source-cell overwrite: after we completed HO to target_cell, ignore
      // serving reports from the old cell for a short period (stale from source eNB).
      if (ctx->last_handover_time != 0 && (now - ctx->last_handover_time) < 15 &&
          ctx->target_cell != 0 && cell->cellID != ctx->target_cell) {
        LOG_TRACK("Stale source report ignored  UE " CLR_DIM "%d" CLR_RESET
                  "  report-cell=%d  serving=%d  target=%d",
                  ueID, cell->cellID, cur_cell, ctx->target_cell);
        return;
      }
      // If we did NOT command a handover to this cell, allow one report to switch (multi-HO).
      if (g_ue_candidate_cell[ueID] == cell->cellID) {
        if (g_ue_candidate_cnt[ueID] < 255) g_ue_candidate_cnt[ueID]++;
      } else {
        g_ue_candidate_cell[ueID] = cell->cellID;
        g_ue_candidate_cnt[ueID] = 1;
      }
      if (g_ue_candidate_cnt[ueID] < 1) {
        LOG_TRACK("Stale measurement ignored  UE " CLR_DIM "%d" CLR_RESET
                  "  in cell=%d  serving=%d", ueID, cell->cellID, cur_cell);
        return;
      }
      LOG_TRACK("UE " CLR_BYELLOW "%d" CLR_RESET " serving cell switched  "
                CLR_DIM "%d" CLR_RESET " ──▶ " CLR_BWHITE "%d" CLR_RESET "  (confirmed)",
                ueID, cur_cell, cell->cellID);
      g_ue_serving_cell[ueID] = cell->cellID;
      reset_ue_candidate(ueID);
      evict_ue_from_other_cells(ueID, cell->cellID);
    }
  } else {
    reset_ue_candidate(ueID);
  }
  
  // First time initialization of UE array
  if (cell->connectedUEs == NULL) {
    cell->connectedUEs = (struct SINRServingValues*)calloc(MAX_REGISTERED_UES, sizeof(struct SINRServingValues));
    assert(cell->connectedUEs != NULL && "Memory exhausted");
    
    // Initialize all entries as unavailable
    for (int i = 0; i < MAX_REGISTERED_UES; i++) {
      cell->connectedUEs[i].is_available = false;
    }
  }
  
  // CRITICAL FIX: Check if UE already exists before incrementing counter
  bool is_new_ue = !cell->connectedUEs[ueID].is_available;
  
  // Update or create UE entry
  cell->connectedUEs[ueID].ueID = ueID;
  cell->connectedUEs[ueID].sinr = sinr;
  cell->connectedUEs[ueID].is_available = true;
  // Mirror global HO context into per-cell record for logging/debug only
  cell->connectedUEs[ueID].ho_context = *ctx;
  // Log SINR snapshot to sinr_xapp.csv (same format as improved version)
  baseline_log_sinr(ueID, cell->cellID, sinr);
  
  // Initialize handover context for new UEs only
  if (is_new_ue) {
    cell->connectedUEs[ueID].neighCells = NULL;
    cell->connectedUEs[ueID].numOfNeighCells = 0;
    // NOTE: Handover state is kept in the global `g_ue_ho_ctx[ueID]`.
    // `ho_context` inside the per-cell UE record is just a mirror for logging/debug.

    // CRITICAL FIX: Only increment counter for NEW UEs
    cell->numOfConnectedUEs++;

    LOG_TRACK("NEW UE " CLR_BWHITE "%2d" CLR_RESET " attached to Cell " CLR_BWHITE "%d" CLR_RESET
              "  total=%zu  SINR=" CLR_BBLUE "%+.2f dB" CLR_RESET,
              ueID, cell->cellID, cell->numOfConnectedUEs, sinr);
  } else {
    LOG_TRACK("UE " CLR_DIM "%2d" CLR_RESET " SINR update  Cell " CLR_DIM "%d" CLR_RESET
              "  " CLR_BBLUE "%+.2f dB" CLR_RESET,
              ueID, cell->cellID, sinr);
  }
}

struct SINRServingValues* get_UE(const uint16_t cellID, const uint16_t ueID) 
{
  if (ueID == 0 || ueID >= MAX_REGISTERED_UES) {
    return NULL;
  }
  if (cells_sinr_map[cellID].is_registered) {
    if (cells_sinr_map[cellID].sinrMap->connectedUEs != NULL &&
        cells_sinr_map[cellID].sinrMap->connectedUEs[ueID].is_available &&
        cells_sinr_map[cellID].sinrMap->connectedUEs[ueID].ueID == ueID) {
      return &(cells_sinr_map[cellID].sinrMap->connectedUEs[ueID]);
    }
  }
  return NULL;
}

// ============================================
// ADDED: Remove UE from source cell after handover
// ============================================
void remove_UE_from_cell(struct SINR_Map* cell, const uint16_t ueID) 
{
  assert(cell != NULL);

  if (ueID == 0 || ueID >= MAX_REGISTERED_UES) {
    return;
  }
  
  if (cell->connectedUEs == NULL || !cell->connectedUEs[ueID].is_available) {
    return;  // UE not in this cell
  }
  
  // Mark as unavailable
  cell->connectedUEs[ueID].is_available = false;
  
  // Free neighbor cells memory
  if (cell->connectedUEs[ueID].neighCells != NULL) {
    free(cell->connectedUEs[ueID].neighCells);
    cell->connectedUEs[ueID].neighCells = NULL;
  }
  
  cell->connectedUEs[ueID].numOfNeighCells = 0;
  
  // Decrement counter
  if (cell->numOfConnectedUEs > 0) {
    cell->numOfConnectedUEs--;
  }
  
  LOG_TRACK("UE " CLR_DIM "%2d" CLR_RESET " detached from Cell " CLR_DIM "%d" CLR_RESET
            "  remaining=%zu", ueID, cell->cellID, cell->numOfConnectedUEs);
}

void add_neighCell(struct SINRServingValues* UE, const uint16_t neighCellID, const double sinr) 
{
  assert(UE != NULL);

  if (UE->neighCells == NULL) {
    UE->neighCells = (struct SINRNeighboringValues*)calloc(MAX_REGISTERED_NEIGHBOURS,
                                                           sizeof(struct SINRNeighboringValues));
    assert(UE->neighCells != NULL);
  }

  struct SINRNeighboringValues* neighCell = &UE->neighCells[neighCellID];

  if (!neighCell->is_available) {
    neighCell->neighCellID = neighCellID;
    neighCell->sinr = sinr;
    neighCell->is_available = true;
    UE->numOfNeighCells++;
  } else {
    neighCell->sinr = sinr;
  }
}

uint16_t getTargetCellID(callback_data_t data)
{
    assert(data.neighCells != NULL);
    
    struct SINRServingValues* serving_ue = get_UE(data.frmCurntCell, data.ueID);
    if (serving_ue == NULL) {
        LOG_ERR("HO decision: UE " CLR_BRED "%d" CLR_RESET " not found in Cell %d",
                data.ueID, data.frmCurntCell);
        return 0;
    }

    double serving_sinr = serving_ue->sinr;
    double max_neighbor_sinr = serving_sinr;
    uint8_t targetCell = 0;

    printf("\n" CLR_BMAGENTA "  ┌── SINR-Based Handover Decision ──────────────────────────────┐\n" CLR_RESET);
    printf(CLR_BMAGENTA   "  │" CLR_RESET "  UE " CLR_BYELLOW "%2d" CLR_RESET
           "   Serving Cell " CLR_BYELLOW "%d" CLR_RESET
           "   SINR " CLR_BBLUE "%+7.2f dB" CLR_RESET
           "   Threshold " CLR_BWHITE "%.1f dB" CLR_RESET "\n",
           data.ueID, data.frmCurntCell, serving_sinr, HANDOVER_THRESHOLD_DB);
    printf(CLR_BMAGENTA   "  ├────────────────────────────────────────────────────────────────┤\n" CLR_RESET);

    for (int i = 0; i < MAX_REGISTERED_NEIGHBOURS; i++)
    {
        if (!data.neighCells[i].is_available)
            continue;

        double neighbor_sinr = data.neighCells[i].sinr;

        if (neighbor_sinr > (serving_sinr + HANDOVER_THRESHOLD_DB)) {
            double gain = neighbor_sinr - serving_sinr;
            printf(CLR_BMAGENTA "  │" CLR_RESET "  Cell " CLR_BYELLOW "%2d" CLR_RESET
                   "  SINR " CLR_BBLUE "%+7.2f dB" CLR_RESET
                   "  Gain " CLR_BGREEN "%+.2f dB" CLR_RESET
                   "  " CLR_BGREEN "◆ CANDIDATE" CLR_RESET "\n",
                   i, neighbor_sinr, gain);
            if (neighbor_sinr > max_neighbor_sinr) {
                max_neighbor_sinr = neighbor_sinr;
                targetCell = i;
            }
        } else {
            printf(CLR_BMAGENTA "  │" CLR_RESET "  Cell " CLR_DIM "%2d" CLR_RESET
                   "  SINR " CLR_DIM "%+7.2f dB" CLR_RESET
                   "  " CLR_DIM "  below threshold" CLR_RESET "\n",
                   i, neighbor_sinr);
        }
    }

    printf(CLR_BMAGENTA "  ├────────────────────────────────────────────────────────────────┤\n" CLR_RESET);
    if (targetCell != 0 && targetCell != data.frmCurntCell) {
        double final_gain = max_neighbor_sinr - serving_sinr;
        printf(CLR_BMAGENTA "  │" CLR_RESET "  " CLR_BGREEN "✔ DECISION" CLR_RESET
               "  Handover to Cell " CLR_BYELLOW "%d" CLR_RESET
               "  SINR " CLR_BBLUE "%+.2f dB" CLR_RESET
               "  Gain " CLR_BGREEN "%+.2f dB" CLR_RESET "\n",
               targetCell, max_neighbor_sinr, final_gain);
    } else {
        printf(CLR_BMAGENTA "  │" CLR_RESET "  " CLR_DIM "✘ No handover — no better neighbour found" CLR_RESET "\n");
    }
    printf(CLR_BMAGENTA "  └────────────────────────────────────────────────────────────────┘\n" CLR_RESET "\n");

    return targetCell;
}

void remove_neighCell() {}

struct InfoObj 
{
  uint16_t cellID;
  uint16_t ueID;
};

struct InfoObj parseServingMsg(const char* msg) 
{
  struct InfoObj info;
  int ret = sscanf(msg, "L3servingSINR3gpp_cell_%hd_UEID_%hd", &info.cellID, &info.ueID);
  if (ret == 2)
    return info;
  info.cellID = -1;
  info.ueID = -1;
  return info;
}

struct InfoObj parseNeighMsg(const char* msg) 
{
  struct InfoObj info;
  int ret = sscanf(msg, "L3neighSINRListOf_UEID_%hd_of_Cell_%hd", &info.ueID, &info.cellID);
  if (ret == 2)
    return info;
  info.ueID = -1;
  info.cellID = -1;
  return info;
}

bool isMeasNameContains(const char* meas_name, const char* name) 
{
  return strncmp(meas_name, name, strlen(name)) == 0;
}

// node_id_src may be NULL if the source node is not yet known (legacy path).
static void log_kpm_measurements(kpm_ind_msg_format_1_t const* msg_frm_1,
                                  const global_e2_node_id_t* node_id_src)
{
  if (msg_frm_1 == NULL ||
      msg_frm_1->meas_info_lst_len == 0 || 
      msg_frm_1->meas_info_lst_len > 1000 ||
      msg_frm_1->meas_data_lst_len == 0 ||
      msg_frm_1->meas_data_lst_len > 1000000) {
      
      LOG_WARN("Invalid/corrupted KPM  info=%zu  data=%zu — skipping",
               msg_frm_1->meas_info_lst_len, msg_frm_1->meas_data_lst_len);
      return;
  }

  if (msg_frm_1->meas_info_lst_len != msg_frm_1->meas_data_lst_len) 
  {
    LOG_ERR("KPM length mismatch: info=%zu  data=%zu",
            msg_frm_1->meas_info_lst_len, msg_frm_1->meas_data_lst_len);
    return;
  }

  for (size_t i = 0; i < msg_frm_1->meas_info_lst_len; i++) 
  {
    meas_type_t const meas_type = msg_frm_1->meas_info_lst[i].meas_type;
    meas_data_lst_t const data_item = msg_frm_1->meas_data_lst[i];

    for (size_t j = 0; j < data_item.meas_record_len;) {
      meas_record_lst_t const record_item = data_item.meas_record_lst[j];

      if (meas_type.type == NAME_MEAS_TYPE) 
      {
        if (isMeasNameContains(meas_type.name.buf, "L3servingSINR3gpp_cell_")) 
        {
          struct InfoObj info = parseServingMsg(meas_type.name.buf);
          double sinr = record_item.real_val;

          struct SINR_Map* cell = add_SINR(info.cellID);
          add_UE(cell, info.ueID, sinr);

          // Anchor cell_id → node_id from the first serving measurement of this node.
          if (node_id_src != NULL)
              cell_node_map_register((uint16_t)info.cellID, node_id_src);

          LOG_KPM("Serving  Cell " CLR_BYELLOW "%2d" CLR_RESET "  UE " CLR_BYELLOW "%2d" CLR_RESET "  SINR " CLR_BBLUE "%+7.2f dB" CLR_RESET,
                  info.cellID, info.ueID, sinr);

        } else if (isMeasNameContains(meas_type.name.buf, "L3neighSINRListOf_UEID_")) 
        {
          struct InfoObj info = parseNeighMsg(meas_type.name.buf);

          meas_record_lst_t const sinr = record_item;
          meas_record_lst_t const NeighbourID = data_item.meas_record_lst[j + 1];

          /* Neighbor list must be for a valid (positive) cell so we can look up the UE in that cell */
          if (info.cellID <= 0 || info.cellID >= MAX_REGISTERED_CELLS) {
            j += 2;
            continue;
          }
          struct SINRServingValues* UE = get_UE(info.cellID, info.ueID);
          if (UE == NULL) {
            // This can happen when we intentionally ignore stale serving reports from non-serving cells.
            j += 2;
            continue;
          }

          // Neighbor IDs are used as direct indices in neighCells
          if (NeighbourID.int_val >= MAX_REGISTERED_NEIGHBOURS) {
            j += 2;
            continue;
          }

          add_neighCell(UE, NeighbourID.int_val, sinr.real_val);
          /* After adding a neighbor for this (cell, UE), if this cell is the
           * UE's authoritative serving cell, evict the UE from all other cells.
           * This allows multi-handover: completion sets serving cell but we only
           * remove from old cell once we have neighbor list in the new cell. */
          if (info.ueID > 0 && info.ueID < MAX_REGISTERED_UES &&
              g_ue_serving_cell[info.ueID] == info.cellID) {
            evict_ue_from_other_cells(info.ueID, info.cellID);
          }
          j += 2;
          continue;
        }
      }
      j++;
    }
  }
}

// ============================================
// Per-Node KPM Callback Wrappers
// ============================================
// report_sm_xapp_api only accepts a plain sm_cb function pointer (no user-data).
// We pre-allocate one static wrapper per possible E2 node slot. Each wrapper
// reads its own kpm_cb_ctx_t to learn which node sent this indication, passes
// the node_id to the shared processing core so the cell→node map is populated.

static void sm_cb_kpm_core(sm_ag_if_rd_t const* rd, const global_e2_node_id_t* nid)
{
  assert(rd != NULL);
  assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
  assert(rd->ind.type == KPM_STATS_V3_0);

  kpm_ind_data_t const* ind   = &rd->ind.kpm.ind;
  (void)ind;  // hdr accessed inside log_kpm_measurements as needed
  kpm_ind_msg_format_3_t const* msg_frm_3 = &rd->ind.kpm.ind.msg.frm_3;

  lock_guard(&mtx);

  if (msg_frm_3 == NULL ||
      msg_frm_3->ue_meas_report_lst_len == 0 ||
      msg_frm_3->ue_meas_report_lst_len > 1000 ||
      msg_frm_3->meas_report_per_ue == NULL) {
    LOG_WARN("Invalid KPM Format-3 data — skipping callback");
    return;
  }

  for (size_t i = 0; i < msg_frm_3->ue_meas_report_lst_len; i++) {
    ue_id_e2sm_t const ue_id_e2sm = msg_frm_3->meas_report_per_ue[i].ue_meas_report_lst;
    free_ue_id_e2sm(&ue_id);
    ue_id = cp_ue_id_e2sm(&ue_id_e2sm);
    log_kpm_measurements(&msg_frm_3->meas_report_per_ue[i].ind_msg_format_1, nid);
  }
}

// Macro: generates a static wrapper function for slot N.
#define DEFINE_KPM_WRAPPER(N) \
  static void sm_cb_kpm_node_##N(sm_ag_if_rd_t const* rd) { \
    sm_cb_kpm_core(rd, g_kpm_cb_ctx[N].in_use ? &g_kpm_cb_ctx[N].node_id : NULL); \
  }

DEFINE_KPM_WRAPPER(0)
DEFINE_KPM_WRAPPER(1)
DEFINE_KPM_WRAPPER(2)
DEFINE_KPM_WRAPPER(3)
DEFINE_KPM_WRAPPER(4)
DEFINE_KPM_WRAPPER(5)
DEFINE_KPM_WRAPPER(6)
DEFINE_KPM_WRAPPER(7)
DEFINE_KPM_WRAPPER(8)
DEFINE_KPM_WRAPPER(9)
DEFINE_KPM_WRAPPER(10)
DEFINE_KPM_WRAPPER(11)
DEFINE_KPM_WRAPPER(12)
DEFINE_KPM_WRAPPER(13)
DEFINE_KPM_WRAPPER(14)
DEFINE_KPM_WRAPPER(15)

static sm_cb g_kpm_wrappers[MAX_E2_NODES] = {
  sm_cb_kpm_node_0,  sm_cb_kpm_node_1,  sm_cb_kpm_node_2,  sm_cb_kpm_node_3,
  sm_cb_kpm_node_4,  sm_cb_kpm_node_5,  sm_cb_kpm_node_6,  sm_cb_kpm_node_7,
  sm_cb_kpm_node_8,  sm_cb_kpm_node_9,  sm_cb_kpm_node_10, sm_cb_kpm_node_11,
  sm_cb_kpm_node_12, sm_cb_kpm_node_13, sm_cb_kpm_node_14, sm_cb_kpm_node_15,
};

static test_info_lst_t filter_predicate(test_cond_type_e type, test_cond_e cond, int value) 
{
  test_info_lst_t dst = {0};

  dst.test_cond_type = type;
  dst.IsStat = TRUE_TEST_COND_TYPE;

  dst.test_cond = calloc(1, sizeof(test_cond_e));
  assert(dst.test_cond != NULL && "Memory allocation failed for test_cond");
  *dst.test_cond = cond;

  dst.test_cond_value = calloc(1, sizeof(test_cond_value_t));
  assert(dst.test_cond_value != NULL && "Memory allocation failed for test_cond_value");
  dst.test_cond_value->type = INTEGER_TEST_COND_VALUE;

  int64_t* int_value = calloc(1, sizeof(int64_t));
  assert(int_value != NULL && "Memory allocation failed for int_value");
  *int_value = value;
  dst.test_cond_value->int_value = int_value;
  return dst;
}

static label_info_lst_t fill_kpm_label(void) 
{
  label_info_lst_t label_item = {0};

  label_item.noLabel = ecalloc(1, sizeof(enum_value_e));
  *label_item.noLabel = TRUE_ENUM_VALUE;

  return label_item;
}

static kpm_act_def_format_1_t fill_act_def_frm_1(ric_report_style_item_t const* report_item) 
{
  assert(report_item != NULL);

  kpm_act_def_format_1_t ad_frm_1 = {0};

  size_t const sz = report_item->meas_info_for_action_lst_len;

  ad_frm_1.meas_info_lst_len = sz;
  ad_frm_1.meas_info_lst = calloc(sz, sizeof(meas_info_format_1_lst_t));
  assert(ad_frm_1.meas_info_lst != NULL && "Memory exhausted");

  for (size_t i = 0; i < sz; i++) 
  {
    meas_info_format_1_lst_t* meas_item = &ad_frm_1.meas_info_lst[i];
    meas_item->meas_type.type = NAME_MEAS_TYPE;
    meas_item->meas_type.name = copy_byte_array(report_item->meas_info_for_action_lst[i].name);

    meas_item->label_info_lst_len = 1;
    meas_item->label_info_lst = ecalloc(1, sizeof(label_info_lst_t));
    meas_item->label_info_lst[0] = fill_kpm_label();
  }

  ad_frm_1.gran_period_ms = period_ms;
  ad_frm_1.cell_global_id = NULL;

#if defined KPM_V2_03 || defined KPM_V3_00
  ad_frm_1.meas_bin_range_info_lst_len = 0;
  ad_frm_1.meas_bin_info_lst = NULL;
#endif

  return ad_frm_1;
}

static kpm_act_def_t fill_report_style_4(ric_report_style_item_t const* report_item) 
{
  assert(report_item != NULL);
  assert(report_item->act_def_format_type == FORMAT_4_ACTION_DEFINITION);

  kpm_act_def_t act_def = {.type = FORMAT_4_ACTION_DEFINITION};

  act_def.frm_4.matching_cond_lst_len = 1;
  act_def.frm_4.matching_cond_lst =
      calloc(act_def.frm_4.matching_cond_lst_len, sizeof(matching_condition_format_4_lst_t));
  assert(act_def.frm_4.matching_cond_lst != NULL && "Memory exhausted");
  
  test_cond_type_e const type = IsStat_TEST_COND_TYPE;
  test_cond_e const condition = LESSTHAN_TEST_COND;
  int const value = 40;
  act_def.frm_4.matching_cond_lst[0].test_info_lst = filter_predicate(type, condition, value);

  act_def.frm_4.action_def_format_1 = fill_act_def_frm_1(report_item);

  return act_def;
}

typedef kpm_act_def_t (*fill_kpm_act_def)(ric_report_style_item_t const* report_item);

static fill_kpm_act_def get_kpm_act_def[END_RIC_SERVICE_REPORT] = 
{
    NULL,
    NULL,
    NULL,
    fill_report_style_4,
    NULL,
};

static kpm_sub_data_t gen_kpm_subs(kpm_ran_function_def_t const* ran_func) 
{
  assert(ran_func != NULL);
  assert(ran_func->ric_event_trigger_style_list != NULL);

  kpm_sub_data_t kpm_sub = {0};

  assert(ran_func->ric_event_trigger_style_list[0].format_type == FORMAT_1_RIC_EVENT_TRIGGER);
  kpm_sub.ev_trg_def.type = FORMAT_1_RIC_EVENT_TRIGGER;
  kpm_sub.ev_trg_def.kpm_ric_event_trigger_format_1.report_period_ms = period_ms;

  kpm_sub.sz_ad = 1;
  kpm_sub.ad = calloc(kpm_sub.sz_ad, sizeof(kpm_act_def_t));
  assert(kpm_sub.ad != NULL && "Memory exhausted");

  ric_report_style_item_t* const report_item = &ran_func->ric_report_style_list[0];
  ric_service_report_e const report_style_type = report_item->report_style_type;
  *kpm_sub.ad = get_kpm_act_def[report_style_type](report_item);

  return kpm_sub;
}

static size_t find_sm_idx(sm_ran_function_t* rf, size_t sz, bool (*f)(sm_ran_function_t const*, int const),
                          int const id) 
{
  for (size_t i = 0; i < sz; i++) 
  {
    if (f(&rf[i], id))
      return i;
  }

  assert(0 != 0 && "SM ID could not be found in the RAN Function List");
}

// ============================================
// RC CONTROL MESSAGE BUILDERS (PRESERVED)
// ============================================
static e2sm_rc_ctrl_hdr_frmt_1_t gen_rc_ctrl_hdr_frmt_1(ue_id_e2sm_t ue_id, uint32_t ric_style_type,
                                                         uint16_t ctrl_act_id) {
  e2sm_rc_ctrl_hdr_frmt_1_t dst = {0};

  dst.ue_id = cp_ue_id_e2sm(&ue_id);
  dst.ric_style_type = ric_style_type;
  dst.ctrl_act_id = ctrl_act_id;

  return dst;
}

static e2sm_rc_ctrl_hdr_t gen_rc_ctrl_hdr(e2sm_rc_ctrl_hdr_e hdr_frmt, ue_id_e2sm_t ue_id,
                                           uint32_t ric_style_type, uint16_t ctrl_act_id) 
{
  e2sm_rc_ctrl_hdr_t dst = {0};

  if (hdr_frmt == FORMAT_1_E2SM_RC_CTRL_HDR) {
    dst.format = FORMAT_1_E2SM_RC_CTRL_HDR;
    dst.frmt_1 = gen_rc_ctrl_hdr_frmt_1(ue_id, ric_style_type, ctrl_act_id);
  } else 
  {
    assert(0 != 0 && "not implemented the fill func for this ctrl hdr frmt");
  }

  return dst;
}

static void set_EUTRA_CGI(seq_ran_param_t* EUTRA_CGI, const char targetcell) 
{
  assert(EUTRA_CGI != NULL);
  assert(targetcell >= '0' && targetcell <= '9');

  if (EUTRA_CGI->ran_param_val.flag_false == NULL) 
  {
    EUTRA_CGI->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(EUTRA_CGI->ran_param_val.flag_false != NULL && "Memory exhausted");
  }

  EUTRA_CGI->ran_param_val.flag_false->type = BIT_STRING_RAN_PARAMETER_VALUE;

  byte_array_t target_ba = {0};
  target_ba.len = 1;
  target_ba.buf = malloc(sizeof(uint8_t));
  assert(target_ba.buf != NULL && "Memory exhausted");
  target_ba.buf[0] = targetcell;

  EUTRA_CGI->ran_param_val.flag_false->octet_str_ran.len = target_ba.len;
  EUTRA_CGI->ran_param_val.flag_false->octet_str_ran.buf = target_ba.buf;
}

static void gen_Target_Primary_Cell_ID(seq_ran_param_t* Target_Primary_Cell_ID, char targetcell) 
{
  Target_Primary_Cell_ID->ran_param_id = TARGET_PRIMARY_CELL_ID_8_4_4_1;
  Target_Primary_Cell_ID->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  Target_Primary_Cell_ID->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
  assert(Target_Primary_Cell_ID->ran_param_val.strct != NULL && "Memory exhausted");
  Target_Primary_Cell_ID->ran_param_val.strct->sz_ran_param_struct = 1;
  Target_Primary_Cell_ID->ran_param_val.strct->ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
  assert(Target_Primary_Cell_ID->ran_param_val.strct->ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* CHOICE_Target_Cell = &Target_Primary_Cell_ID->ran_param_val.strct->ran_param_struct[0];
  CHOICE_Target_Cell->ran_param_id = CHOICE_TARGET_CELL_8_4_4_1;
  CHOICE_Target_Cell->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  CHOICE_Target_Cell->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
  assert(CHOICE_Target_Cell->ran_param_val.strct != NULL && "Memory exhausted");
  CHOICE_Target_Cell->ran_param_val.strct->sz_ran_param_struct = 2;
  CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
  assert(CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* NR_Cell = &CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct[0];
  NR_Cell->ran_param_id = NR_CELL_8_4_4_1;
  NR_Cell->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  NR_Cell->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
  assert(NR_Cell->ran_param_val.strct != NULL && "Memory exhausted");
  NR_Cell->ran_param_val.strct->sz_ran_param_struct = 1;
  NR_Cell->ran_param_val.strct->ran_param_struct = calloc(1, sizeof(seq_ran_param_t));

  seq_ran_param_t* NR_CGI = &NR_Cell->ran_param_val.strct->ran_param_struct[0];
  NR_CGI->ran_param_id = NR_CGI_8_4_4_1;
  NR_CGI->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  NR_CGI->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(NR_CGI->ran_param_val.flag_false != NULL && "Memory exhausted");
  NR_CGI->ran_param_val.flag_false->type = BIT_STRING_RAN_PARAMETER_VALUE;
  
  char nr_cgi_str[1] = {targetcell};
  byte_array_t nr_cgi = cp_str_to_ba(nr_cgi_str);
  NR_CGI->ran_param_val.flag_false->octet_str_ran.len = nr_cgi.len;
  NR_CGI->ran_param_val.flag_false->octet_str_ran.buf = nr_cgi.buf;

  seq_ran_param_t* EUTRA_Cell = &CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct[1];
  EUTRA_Cell->ran_param_id = EUTRA_CELL_8_4_4_1;
  EUTRA_Cell->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  EUTRA_Cell->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
  assert(EUTRA_Cell->ran_param_val.strct != NULL && "Memory exhausted");
  EUTRA_Cell->ran_param_val.strct->sz_ran_param_struct = 1;
  EUTRA_Cell->ran_param_val.strct->ran_param_struct = calloc(1, sizeof(seq_ran_param_t));

  seq_ran_param_t* EUTRA_CGI = &EUTRA_Cell->ran_param_val.strct->ran_param_struct[0];
  EUTRA_CGI->ran_param_id = EUTRA_CGI_8_4_4_1;
  EUTRA_CGI->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  EUTRA_CGI->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(EUTRA_CGI->ran_param_val.flag_false != NULL && "Memory exhausted");
  EUTRA_CGI->ran_param_val.flag_false->type = BIT_STRING_RAN_PARAMETER_VALUE;

  set_EUTRA_CGI(EUTRA_CGI, targetcell);

  return;
}

static void gen_List_of_PDU_sessions_for_handover(seq_ran_param_t* List_PDU_sessions_ho) 
{
  int num_PDU_session = 1;

  List_PDU_sessions_ho->ran_param_id = LIST_OF_PDU_SESSIONS_FOR_HANDOVER_8_4_4_1;
  List_PDU_sessions_ho->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  List_PDU_sessions_ho->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
  assert(List_PDU_sessions_ho->ran_param_val.lst != NULL && "Memory exhausted");
  List_PDU_sessions_ho->ran_param_val.lst->sz_lst_ran_param = num_PDU_session;
  List_PDU_sessions_ho->ran_param_val.lst->lst_ran_param = calloc(num_PDU_session, sizeof(lst_ran_param_t));
  assert(List_PDU_sessions_ho->ran_param_val.lst->lst_ran_param != NULL && "Memory exhausted");

  lst_ran_param_t* PDU_session_item = &List_PDU_sessions_ho->ran_param_val.lst->lst_ran_param[0];
  PDU_session_item->ran_param_struct.sz_ran_param_struct = 2;
  PDU_session_item->ran_param_struct.ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
  assert(PDU_session_item->ran_param_struct.ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* PDU_Session_ID = &PDU_session_item->ran_param_struct.ran_param_struct[0];
  PDU_Session_ID->ran_param_id = PDU_SESSION_ID_8_4_4_1;
  PDU_Session_ID->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
  PDU_Session_ID->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(PDU_Session_ID->ran_param_val.flag_false != NULL && "Memory exhausted");
  PDU_Session_ID->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;

  char pduid_str[2];
  snprintf(pduid_str, sizeof(pduid_str), "%d", PDU_SESSION_ID_5);
  byte_array_t pduid = cp_str_to_ba(pduid_str);
  PDU_Session_ID->ran_param_val.flag_false->octet_str_ran.len = pduid.len;
  PDU_Session_ID->ran_param_val.flag_false->octet_str_ran.buf = pduid.buf;

  seq_ran_param_t* List_of_QoS_flows = &PDU_session_item->ran_param_struct.ran_param_struct[1];
  List_of_QoS_flows->ran_param_id = LIST_OF_QOS_FLOWS_IN_THE_PDU_SESSION_8_4_4_1;
  List_of_QoS_flows->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  List_of_QoS_flows->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
  assert(List_of_QoS_flows->ran_param_val.lst != NULL && "Memory exhausted");
  List_of_QoS_flows->ran_param_val.lst->sz_lst_ran_param = 1;
  List_of_QoS_flows->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
  assert(List_of_QoS_flows->ran_param_val.lst->lst_ran_param != NULL && "Memory exhausted");

  lst_ran_param_t* QoS_flow_Item = &List_of_QoS_flows->ran_param_val.lst->lst_ran_param[0];
  QoS_flow_Item->ran_param_struct.sz_ran_param_struct = 1;
  QoS_flow_Item->ran_param_struct.ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
  assert(QoS_flow_Item->ran_param_struct.ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* QoS_Flow_Id = &QoS_flow_Item->ran_param_struct.ran_param_struct[0];
  QoS_Flow_Id->ran_param_id = QOS_FLOW_IDENTIFIER_8_4_4_1;
  QoS_Flow_Id->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
  QoS_Flow_Id->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(QoS_Flow_Id->ran_param_val.flag_false != NULL && "Memory exhausted");
  QoS_Flow_Id->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;

  char qosid_str[3];
  snprintf(qosid_str, sizeof(qosid_str), "%d", QOS_FLOW_ID_1);
  byte_array_t qosid = cp_str_to_ba(qosid_str);
  QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.len = qosid.len;
  QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.buf = qosid.buf;

  return;
}

static void gen_List_of_DRBs_for_handover(seq_ran_param_t* List_DRBs_ho) 
{
  int num_DRBs = 1;
  
  List_DRBs_ho->ran_param_id = LIST_OF_DRBS_FOR_HANDOVER_8_4_4_1;
  List_DRBs_ho->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  List_DRBs_ho->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
  assert(List_DRBs_ho->ran_param_val.lst != NULL && "Memory exhausted");
  List_DRBs_ho->ran_param_val.lst->sz_lst_ran_param = num_DRBs;
  List_DRBs_ho->ran_param_val.lst->lst_ran_param = calloc(num_DRBs, sizeof(lst_ran_param_t));
  assert(List_DRBs_ho->ran_param_val.lst->lst_ran_param != NULL && "Memory exhausted");

  lst_ran_param_t* DRB_item_ho = (lst_ran_param_t*)&List_DRBs_ho->ran_param_val.strct->ran_param_struct[0];

  DRB_item_ho->ran_param_struct.sz_ran_param_struct = 2;
  DRB_item_ho->ran_param_struct.ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
  assert(DRB_item_ho->ran_param_struct.ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* DRB_ID = &DRB_item_ho->ran_param_struct.ran_param_struct[0];
  DRB_ID->ran_param_id = DRB_ID_8_4_4_1;
  DRB_ID->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
  DRB_ID->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(DRB_ID->ran_param_val.flag_false != NULL && "Memory exhausted");
  DRB_ID->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  char DRB_ID_str[] = "3";
  byte_array_t drpID = cp_str_to_ba(DRB_ID_str);
  DRB_ID->ran_param_val.flag_false->octet_str_ran.len = drpID.len;
  DRB_ID->ran_param_val.flag_false->octet_str_ran.buf = drpID.buf;

  seq_ran_param_t* List_of_QoS_flows = &DRB_item_ho->ran_param_struct.ran_param_struct[1];
  List_of_QoS_flows->ran_param_id = LIST_OF_QOS_FLOWS_IN_THE_DRB_8_4_4_1;
  List_of_QoS_flows->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  List_of_QoS_flows->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
  assert(List_of_QoS_flows->ran_param_val.lst != NULL && "Memory exhausted");
  List_of_QoS_flows->ran_param_val.lst->sz_lst_ran_param = 1;
  List_of_QoS_flows->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
  assert(List_of_QoS_flows->ran_param_val.lst->lst_ran_param != NULL && "Memory exhausted");

  lst_ran_param_t* QoS_flow_Item = &List_of_QoS_flows->ran_param_val.lst->lst_ran_param[0];
  QoS_flow_Item->ran_param_struct.sz_ran_param_struct = 1;
  QoS_flow_Item->ran_param_struct.ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
  assert(QoS_flow_Item->ran_param_struct.ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* QoS_Flow_Id = &QoS_flow_Item->ran_param_struct.ran_param_struct[0];
  QoS_Flow_Id->ran_param_id = QOS_FLOW_IDENTIFIER_8_4_4_1;
  QoS_Flow_Id->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
  QoS_Flow_Id->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(QoS_Flow_Id->ran_param_val.flag_false != NULL && "Memory exhausted");
  QoS_Flow_Id->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  char QFI_str[] = "10";
  byte_array_t QFI = cp_str_to_ba(QFI_str);
  QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.len = QFI.len;
  QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.buf = QFI.buf;

  return;
}

static void gen_List_of_Secondary_cells_to_be_setup(seq_ran_param_t* List_num_2ndCells) 
{
  int num_2ndCells = 1;
  
  List_num_2ndCells->ran_param_id = LIST_OF_SECONDARY_CELLS_TO_BE_SETUP_8_4_4_1;
  List_num_2ndCells->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  List_num_2ndCells->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
  assert(List_num_2ndCells->ran_param_val.lst != NULL && "Memory exhausted");
  List_num_2ndCells->ran_param_val.lst->sz_lst_ran_param = num_2ndCells;
  List_num_2ndCells->ran_param_val.lst->lst_ran_param = calloc(num_2ndCells, sizeof(lst_ran_param_t));
  assert(List_num_2ndCells->ran_param_val.lst->lst_ran_param != NULL && "Memory exhausted");

  lst_ran_param_t* secCell_item = (lst_ran_param_t*)&List_num_2ndCells->ran_param_val.strct->ran_param_struct[0];

  secCell_item->ran_param_struct.sz_ran_param_struct = 1;
  secCell_item->ran_param_struct.ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
  assert(secCell_item->ran_param_struct.ran_param_struct != NULL && "Memory exhausted");

  seq_ran_param_t* secCell_Id = &secCell_item->ran_param_struct.ran_param_struct[0];
  secCell_Id->ran_param_id = SECONDARY_CELL_ID_8_4_4_1;
  secCell_Id->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  secCell_Id->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
  assert(secCell_Id->ran_param_val.flag_false != NULL && "Memory exhausted");
  secCell_Id->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  char cellID_str[] = "0";
  byte_array_t QFI = cp_str_to_ba(cellID_str);
  secCell_Id->ran_param_val.flag_false->octet_str_ran.len = QFI.len;
  secCell_Id->ran_param_val.flag_false->octet_str_ran.buf = QFI.buf;

  return;
}

static e2sm_rc_ctrl_msg_frmt_1_t gen_rc_ctrl_msg_frmt_1_Handover_Control(char targetcell) 
{
  e2sm_rc_ctrl_msg_frmt_1_t dst = {0};

  dst.sz_ran_param = 4;
  dst.ran_param = calloc(4, sizeof(seq_ran_param_t));
  assert(dst.ran_param != NULL && "Memory exhausted");

  gen_Target_Primary_Cell_ID(&dst.ran_param[0], targetcell);
  gen_List_of_PDU_sessions_for_handover(&dst.ran_param[1]);
  gen_List_of_DRBs_for_handover(&dst.ran_param[2]);
  gen_List_of_Secondary_cells_to_be_setup(&dst.ran_param[3]);

  return dst;
}

static e2sm_rc_ctrl_msg_t gen_handover_rc_ctrl_msg(e2sm_rc_ctrl_msg_e msg_frmt, uint8_t targetcell) 
{
  e2sm_rc_ctrl_msg_t dst = {0};

  if (msg_frmt == FORMAT_1_E2SM_RC_CTRL_MSG) 
  {
    dst.format = msg_frmt;
    dst.frmt_1 = gen_rc_ctrl_msg_frmt_1_Handover_Control(targetcell);
  } else 
  {
    assert(0 != 0 && "not implemented the fill func for this ctrl msg frmt");
  }

  return dst;
}

static ue_id_e2sm_t gen_rc_ue_id(ue_id_e2sm_e type, int ueid) 
{
  ue_id_e2sm_t ue_id = {0};
  if (type == GNB_UE_ID_E2SM) 
  {
    ue_id.type = GNB_UE_ID_E2SM;
    ue_id.gnb.ran_ue_id = (uint64_t*)malloc(sizeof(uint64_t));
    *(ue_id.gnb.ran_ue_id) = ueid;
  } else 
  {
    assert(0 != 0 && "not supported UE ID type");
  }
  return ue_id;
}

static bool eq_sm(sm_ran_function_t const* elem, int const id) 
{
  if (elem->id == id)
    return true;

  return false;
}

// ============================================
// FIXED: Handover State Machine with Completion Detection
// ============================================
void checkHandoverCompletion(struct SINR_Map* target_cell, uint16_t ueID, uint16_t source_cell_id) 
{
  if (ueID == 0 || ueID >= MAX_REGISTERED_UES) {
    return;
  }
  // Check if UE appears in target cell's serving measurements
  if (target_cell != NULL && 
      target_cell->connectedUEs != NULL &&
      target_cell->connectedUEs[ueID].is_available &&
      target_cell->connectedUEs[ueID].ueID == ueID) {
    
    // Get source cell
    struct SINR_Map* source_cell = cells_sinr_map[source_cell_id].sinrMap;
    
    // Check if UE was in IN_PROGRESS state in source cell
    if (source_cell != NULL && 
        source_cell->connectedUEs != NULL &&
        source_cell->connectedUEs[ueID].ho_context.state == HO_STATE_IN_PROGRESS) {

      // CRITICAL: Only declare completion if this matches the expected HO (source/target)
      handover_context_t* src_ctx = &source_cell->connectedUEs[ueID].ho_context;
      if (src_ctx->source_cell != source_cell_id || src_ctx->target_cell != target_cell->cellID) {
        // UE may appear in multiple cells during transition/stale reports; ignore mismatches
        return;
      }
      
      printf("\n" CLR_BGREEN
             "  ╔══════════════════════════════════════════╗\n"
             "  ║  ✔  HANDOVER COMPLETED                   ║\n"
             "  ║     UE %-4d  Cell %-2d  ──▶  Cell %-2d         ║\n"
             "  ╚══════════════════════════════════════════╝" CLR_RESET "\n",
             ueID, source_cell_id, target_cell->cellID);
      log_handover_xapp(ueID, source_cell_id, target_cell->cellID, "COMPLETED", 1);

      // Increment global UE handover counter
      if (ueID > 0 && ueID < MAX_REGISTERED_UES)
          g_ue_ho_ctx[ueID].ue_ho_count++;

      // Copy handover context to target cell (including last_handover_time)
      target_cell->connectedUEs[ueID].ho_context = source_cell->connectedUEs[ueID].ho_context;
      
      // Mark as completed and reset to IDLE for future handovers
      target_cell->connectedUEs[ueID].ho_context.state = HO_STATE_IDLE;
      target_cell->connectedUEs[ueID].ho_context.completion_time = time(NULL);
      // CRITICAL FIX: Do NOT reset last_handover_time here!
      // It should remain the time when HO was initiated (already copied from source)
      target_cell->connectedUEs[ueID].ho_context.attempts = 0;
      
      // Remove from source cell
      remove_UE_from_cell(source_cell, ueID);
    }
  }
}

// Returns 1 if any UE currently has an active (non-timed-out) handover in flight.
static int global_ho_in_progress(void)
{
  time_t now = time(NULL);
  for (int u = 1; u < MAX_REGISTERED_UES; ++u) {
    handover_context_t* ctx = &g_ue_ho_ctx[u];
    if (ctx->state == HO_STATE_IN_PROGRESS) {
      double elapsed = difftime(now, ctx->command_time);
      if (elapsed <= HO_COMPLETION_TIMEOUT_SEC)
        return 1;
    }
  }
  return 0;
}

void evaluateHandoverOpportunities(Callback targetCellFinding, Callback cbHOAction, callback_data_t data) 
{
  LOG_SECTION("Handover Evaluation Cycle");

  // Global lock: do not start any new handover while one is already in flight.
  // ns3's epc-x2 cannot handle concurrent X2 handovers safely.
  if (global_ho_in_progress()) {
    LOG_HO(CLR_BYELLOW "Global HO lock active" CLR_RESET " — skipping cycle (waiting for in-flight HO to complete)");
    return;
  }

  for (int i = 0; i < MAX_REGISTERED_CELLS; i++) 
  {
    if (cells_sinr_map[i].sinrMap == NULL || !cells_sinr_map[i].is_registered) 
      continue;

    struct SINR_Map* cell = cells_sinr_map[i].sinrMap;

    printf(CLR_BBLUE "  ● Cell %-2d" CLR_RESET "  Connected UEs: " CLR_BYELLOW "%zu" CLR_RESET "\n",
           cell->cellID, cell->numOfConnectedUEs);

    for (int j = 0; j < MAX_REGISTERED_UES; j++) 
    {
      if (!cell->connectedUEs[j].is_available || 
          cell->connectedUEs[j].neighCells == NULL)
        continue;

      struct SINRServingValues* ue = &cell->connectedUEs[j];
      if (ue->ueID == 0 || ue->ueID >= MAX_REGISTERED_UES)
        continue;
      /* Multi-handover: only evaluate a UE in its authoritative serving cell.
       * After handover the UE may still appear in the old cell until we evict
       * (when neighbor list is received); avoid evaluating the same UE twice. */
      if (g_ue_serving_cell[ue->ueID] != 0 && g_ue_serving_cell[ue->ueID] != cell->cellID)
        continue;

      handover_context_t* ctx = &g_ue_ho_ctx[ue->ueID];
      
      // Skip if handover in progress
      if (ctx->state == HO_STATE_IN_PROGRESS) {
        time_t now = time(NULL);
        double elapsed = difftime(now, ctx->command_time);
        
        if (elapsed > HO_COMPLETION_TIMEOUT_SEC) {
          LOG_HO("UE " CLR_BYELLOW "%d" CLR_RESET " IN_PROGRESS timeout "
                 CLR_DIM "(%.1fs)" CLR_RESET " — assuming completed, serving cell "
                 CLR_DIM "%d" CLR_RESET " ──▶ " CLR_BWHITE "%d" CLR_RESET,
                 ue->ueID, elapsed, g_ue_serving_cell[ue->ueID], ctx->target_cell);
          g_ue_serving_cell[ue->ueID] = ctx->target_cell;
          reset_ue_candidate(ue->ueID);
          log_handover_xapp(ue->ueID, ctx->source_cell, ctx->target_cell, "COMPLETED", 1);
          ctx->ue_ho_count++;
          ctx->state = HO_STATE_IDLE;
          ctx->completion_time = now;
          ctx->last_handover_time = now;
          ctx->attempts = 0;
        } else {
          LOG_HO("UE " CLR_DIM "%d" CLR_RESET "  state=" CLR_BYELLOW "%s" CLR_RESET
                 "  waiting " CLR_DIM "(%.1fs / %ds)" CLR_RESET,
                 ue->ueID, ho_state_to_string(ctx->state), elapsed, HO_COMPLETION_TIMEOUT_SEC);
          continue;
        }
      }
      
      // Handle FAILED state: allow limited retries after cooldown
      if (ctx->state == HO_STATE_FAILED) {
        time_t now = time(NULL);
        double since_last = (ctx->last_handover_time > 0)
                              ? difftime(now, ctx->last_handover_time)
                              : 9999.0;
        if (ctx->attempts >= MAX_HANDOVER_ATTEMPTS) {
          LOG_WARN("UE " CLR_DIM "%d" CLR_RESET " FAILED — max attempts (%d) reached, parking in IDLE",
                   ue->ueID, ctx->attempts);
          // Park the UE in IDLE but keep last_handover_time to avoid immediate flip-flop
          ctx->state = HO_STATE_IDLE;
          ctx->attempts = 0;
          continue;
        }
        if (since_last < MIN_HO_INTERVAL_SEC) {
          LOG_WARN("UE " CLR_DIM "%d" CLR_RESET " FAILED — cooldown %.1fs / %ds before retry",
                   ue->ueID, since_last, MIN_HO_INTERVAL_SEC);
          continue;
        }
        // Ready to retry
        ctx->state = HO_STATE_IDLE;
      }

      if (ctx->state != HO_STATE_IDLE) {
        LOG_HO("UE " CLR_DIM "%d" CLR_RESET "  state=" CLR_BYELLOW "%s" CLR_RESET " — skipping",
               ue->ueID, ho_state_to_string(ctx->state));
        continue;
      }

      // Anti ping-pong protection
      time_t current_time = time(NULL);
      if (ctx->last_handover_time > 0) {
        double time_since_last_ho = difftime(current_time, ctx->last_handover_time);
        if (time_since_last_ho < MIN_HO_INTERVAL_SEC) {
          LOG_INFO("UE " CLR_DIM "%d" CLR_RESET " anti-ping-pong  %.1fs since last HO (min: %ds)",
                   ue->ueID, time_since_last_ho, MIN_HO_INTERVAL_SEC);
          continue;
        }
      }

      callback_data_t ue_data = {
        .nodes = data.nodes,
        .neighCells = ue->neighCells,
        .ueID = ue->ueID,
        .frmCurntCell = cell->cellID
      };

      uint8_t target = targetCellFinding(ue_data);
      
      if (target != 0 && target != cell->cellID) {
        ue_data.toTargetCell = target;

        if (cbHOAction(ue_data)) {
          ctx->last_handover_time = current_time;
          ue->ho_context = *ctx;
          LOG_OK("Handover initiated  UE " CLR_BWHITE "%d" CLR_RESET
                 "  " CLR_BYELLOW "Cell %d" CLR_RESET " ──▶ " CLR_BGREEN "Cell %d" CLR_RESET,
                 ue->ueID, cell->cellID, target);
          // Global HO lock: one HO in flight at a time. Stop evaluating all
          // other UEs this cycle to prevent concurrent X2 operations in ns3.
          return;
        } else {
          LOG_ERR("Handover initiation failed  UE " CLR_BRED "%d" CLR_RESET, ue->ueID);
        }
      }
    }
  }
}

uint16_t doHandoverAction(callback_data_t data)
{
    char trgtCell = '0' + data.toTargetCell;
    
    if (!(trgtCell > '0' && trgtCell <= '9')) {
        LOG_ERR("Invalid target cell character '%c' for UE %d", trgtCell, data.ueID);
        return 0;
    }

    struct SINRServingValues* ue = get_UE(data.frmCurntCell, data.ueID);
    if (ue == NULL) {
        LOG_ERR("UE " CLR_BRED "%d" CLR_RESET " not found in Cell %d — cannot send HO command",
                data.ueID, data.frmCurntCell);
        return 0;
    }

    if (data.ueID == 0 || data.ueID >= MAX_REGISTERED_UES)
        return 0;
    handover_context_t* ctx = &g_ue_ho_ctx[data.ueID];
    
    // Should not happen due to evaluation logic, but double-check
    if (ctx->state == HO_STATE_IN_PROGRESS) {
        time_t now = time(NULL);
        double elapsed = difftime(now, ctx->command_time);
        
        if (elapsed < MAX_HANDOVER_TIMEOUT_SEC) {
            return 0;
        }
    }
    
    if (ctx->attempts >= MAX_HANDOVER_ATTEMPTS) {
        ctx->state = HO_STATE_IDLE;
        ctx->attempts = 0;
        return 0;
    }
    
    rc_ctrl_req_data_t rc_ctrl = {0};
    ue_id_e2sm_t ue_id_1 = gen_rc_ue_id(GNB_UE_ID_E2SM, data.ueID);
    
    rc_ctrl.hdr = gen_rc_ctrl_hdr(FORMAT_1_E2SM_RC_CTRL_HDR, ue_id_1,
                                   CONNECTED_MODE_MOBILITY, HANDOVER_CONTROL_7_6_4_1);
    rc_ctrl.msg = gen_handover_rc_ctrl_msg(FORMAT_1_E2SM_RC_CTRL_MSG, trgtCell);
    
    ctx->state = HO_STATE_COMMAND_SENT;
    ctx->source_cell = data.frmCurntCell;
    ctx->target_cell = data.toTargetCell;
    ctx->command_time = time(NULL);
    ctx->attempts++;
    // Ensure we have an initial serving cell recorded
    if (g_ue_serving_cell[data.ueID] == 0)
      g_ue_serving_cell[data.ueID] = data.frmCurntCell;

    // Increment system-wide counter BEFORE printing (so #1 is the first ever HO)
    g_total_ho_count++;
    // UE-level counter tracks how many HOs this specific UE has had
    int ue_ho_seq = ctx->ue_ho_count + 1;   // +1 because completion increments it

    printf("\n" CLR_BCYAN
           "  ╔══════════════════════════════════════════════════════════════════╗\n"
           "  ║  ▶  HANDOVER COMMAND  " CLR_BWHITE "#%-3d (System)" CLR_BCYAN
           "  |  " CLR_BWHITE "UE #%-2d (UE %-2d)" CLR_BCYAN "         ║\n"
           "  ║     UE " CLR_BYELLOW "%-4d" CLR_BCYAN
           "  |  Cell " CLR_BYELLOW "%-2d" CLR_BCYAN
           " ──▶ Cell " CLR_BGREEN "%-2d" CLR_BCYAN
           "  |  State: " CLR_BYELLOW "%-14s" CLR_BCYAN "  ║\n"
           "  ╚══════════════════════════════════════════════════════════════════╝\n"
           CLR_RESET "\n",
           g_total_ho_count,
           ue_ho_seq, data.ueID,
           data.ueID, data.frmCurntCell, data.toTargetCell,
           ho_state_to_string(ctx->state));
    
    /* Targeted RC send: look up which E2 node owns the source cell via the
     * cell→node map (populated incrementally from incoming KPM indications).
     * Falls back to broadcast only on cold-start or if targeted send fails,
     * so we never drop a handover due to a map-not-ready race at startup. */
    bool handover_sent = false;

    global_e2_node_id_t* target_node = cell_node_map_get(data.frmCurntCell);

    if (target_node != NULL) {
        LOG_INFO("Targeted RC  Cell " CLR_BYELLOW "%d" CLR_RESET
                 " → NB_ID " CLR_BWHITE "%u" CLR_RESET,
                 data.frmCurntCell, target_node->nb_id.nb_id);
        sm_ans_xapp_t ans = control_sm_xapp_api(target_node, SM_RC_ID, &rc_ctrl);
        if (ans.success) {
            handover_sent = true;
            ctx->state = HO_STATE_IN_PROGRESS;
            LOG_OK("RC delivered  NB_ID=" CLR_BWHITE "%u" CLR_RESET " (targeted)",
                   target_node->nb_id.nb_id);
        } else {
            LOG_WARN("Targeted send failed for Cell %d — falling back to broadcast",
                     data.frmCurntCell);
        }
    }

    if (!handover_sent) {
        LOG_WARN("Broadcast RC fallback for Cell " CLR_DIM "%d" CLR_RESET
                 " (map not ready or targeted send failed)", data.frmCurntCell);
        for (size_t i = 0; i < (*data.nodes).len; ++i) {
            sm_ans_xapp_t ans = control_sm_xapp_api(&(*data.nodes).n[i].id, SM_RC_ID, &rc_ctrl);
            if (ans.success) {
                handover_sent = true;
                ctx->state = HO_STATE_IN_PROGRESS;
                LOG_OK("RC broadcast delivered to node " CLR_BWHITE "%zu" CLR_RESET, i);
            }
        }
    }

    free_rc_ctrl_req_data(&rc_ctrl);

    if (handover_sent) {
        log_handover_xapp(data.ueID, data.frmCurntCell, data.toTargetCell, "COMMAND_SENT", 0);
        LOG_HO("Status: " CLR_BGREEN "SENT" CLR_RESET " — waiting for RAN confirmation ...");
        ue->ho_context = *ctx;
        return 1;
    } else {
        LOG_ERR("Status: " CLR_BRED "FAILED" CLR_RESET " — RC command not delivered to any node");
        ctx->state = HO_STATE_IDLE;
        ue->ho_context = *ctx;
        return 0;
    }
}

int main(int argc, char* argv[]) 
{
  fr_args_t args = init_fr_args(argc, argv);
  init_xapp_api(&args);
  sleep(1);

  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  defer({ free_e2_node_arr_xapp(&nodes); });
  assert(nodes.len > 0);
  LOG_INFO("Connected E2 nodes: " CLR_BGREEN "%d" CLR_RESET, nodes.len);

  pthread_mutexattr_t attr = {0};
  int rc = pthread_mutex_init(&mtx, &attr);
  assert(rc == 0);

  sm_ans_xapp_t* hndl = calloc(nodes.len, sizeof(sm_ans_xapp_t));
  assert(hndl != NULL);

  int const KPM_ran_function = 2;
  assert(nodes.len <= MAX_E2_NODES && "More E2 nodes than pre-allocated wrapper slots");
  for (size_t i = 0; i < nodes.len; ++i) 
  {
    e2_node_connected_xapp_t* n = &nodes.n[i];

    // Populate the per-node context so the wrapper knows which node it belongs to.
    g_kpm_cb_ctx[i].node_id = cp_global_e2_node_id(&n->id);
    g_kpm_cb_ctx[i].in_use  = true;

    size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_ran_function);

    if (n->rf[idx].defn.kpm.ric_report_style_list != NULL) 
    {
      kpm_sub_data_t kpm_sub = gen_kpm_subs(&n->rf[idx].defn.kpm);
      // Use dedicated per-node wrapper so cell→node mapping is populated automatically.
      hndl[i] = report_sm_xapp_api(&n->id, KPM_ran_function, &kpm_sub, g_kpm_wrappers[i]);
      assert(hndl[i].success == true);
      free_kpm_sub_data(&kpm_sub);
      LOG_INFO("Subscribed KPM on node %zu  NB_ID=" CLR_BWHITE "%u" CLR_RESET, i, n->id.nb_id.nb_id);
    }
  }

  printf("\n" CLR_BBLUE
    "  ╔══════════════════════════════════════════════════════════╗\n"
    "  ║                                                          ║\n"
    "  ║    " CLR_BWHITE "Smart SINR-Based Continuous Handover xApp" CLR_BBLUE "       ║\n"
    "  ║                                                          ║\n"
    "  ╠══════════════════════════════════════════════════════════╣\n"
    "  ║  " CLR_CYAN "HO Threshold      " CLR_BWHITE "%.1f dB" CLR_BBLUE "                              ║\n"
    "  ║  " CLR_CYAN "Anti Ping-Pong     " CLR_BWHITE "%d s" CLR_BBLUE "                               ║\n"
    "  ║  " CLR_CYAN "HO Timeout         " CLR_BWHITE "%d s" CLR_BBLUE "                               ║\n"
    "  ║  " CLR_CYAN "Decision Mode      " CLR_BWHITE "Immediate (no averaging)" CLR_BBLUE "            ║\n"
    "  ║  " CLR_CYAN "RC Mode            " CLR_BWHITE "Targeted (cell→node map)" CLR_BBLUE "            ║\n"
    "  ║                                                          ║\n"
    "  ╚══════════════════════════════════════════════════════════╝\n"
    CLR_RESET "\n",
    HANDOVER_THRESHOLD_DB, MIN_HO_INTERVAL_SEC, HO_COMPLETION_TIMEOUT_SEC);

  LOG_INFO("Waiting for initial KPM measurements ...");
  sleep(3);

  // Print the cell→node mapping table built from the first KPM reports.
  // This confirms that targeted RC sends are active and working correctly.
  pthread_mutex_lock(&mtx);
  cell_node_map_print();
  pthread_mutex_unlock(&mtx);

  callback_data_t context = {.nodes = &nodes};

  while (1) {
    pthread_mutex_lock(&mtx);
    
    evaluateHandoverOpportunities(getTargetCellID, doHandoverAction, context);
    
    pthread_mutex_unlock(&mtx);

    sleep(2);
  }

  for (int i = 0; i < nodes.len; ++i) 
  {
    if (hndl[i].success == true)
      rm_report_sm_xapp_api(hndl[i].u.handle);
  }
  free(hndl);

  while (try_stop_xapp_api() == false)
    usleep(1000);

  rc = pthread_mutex_destroy(&mtx);
  assert(rc == 0);

  LOG_OK("xApp completed successfully");
  return 0;
}
