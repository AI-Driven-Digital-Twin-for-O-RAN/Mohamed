/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 *-------------------------------------------------------------------------------
 * Authors:
 *  FIXED VERSION - Complete Multiple Handover Support with KPM Confirmation
 */

// RL (DDQN) Handover xApp - FIXED VERSION
// RL (DDQN) gated SINR-driven mobility management with proper state tracking
//
// KEY FIXES:
// 1. Handover confirmation via KPM measurements (UE cell detection)
// 2. Proper data structure cleanup when UE moves between cells
// 3. No premature success marking - wait for actual execution
//
// RESULT: Unlimited handovers per UE ✅

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_struct.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/sm/rc_sm/rc_sm_id.h"
#include "../../../../src/sm/rc_sm/ie/rc_data_ie.h"
#include "../../../../src/util/e.h"
#include <unistd.h>  // For sleep() and usleep()
#include "rl_predictor_client.h"  // RL (DDQN) handover prediction service

// ============================================
// CONFIGURATION PARAMETERS
// ============================================
#define MIN_SINR -10
#define MAX_REGISTERED_CELLS 16
#define MAX_REGISTERED_UES   32    // UE IMSIs from ns-3 are 1-based; 20 UEs → ID up to 20
#define MAX_REGISTERED_NEIGHBOURS 32
#define MAX_NUM_OF_RIC_INDICATIONS 2  // Require 2 samples before A3 fires

// A3 Event Configuration (3GPP Standard)
#define A3_OFFSET 1.0           // dB - Offset for A3 event
#define A3_HYSTERESIS 1.0       // dB - Hysteresis to prevent ping-pong
#define A3_TIME_TO_TRIGGER 0    // Not implemented in this version

// Handover Timing Parameters
#define HANDOVER_COOLDOWN_SIM_SEC 5.0   // Cooldown between handovers (simulated seconds)
#define HANDOVER_COMPLETION_TIMEOUT 10   // Max time to wait for handover completion
#define MEASUREMENT_UPDATE_INTERVAL 1    // Seconds between decision cycles

// ============================================
// ENUMERATIONS
// ============================================
typedef enum {
    CONNECTED_MODE_MOBILITY = 3
} rc_ctrl_service_style_id_e;

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
// HANDOVER STATE MACHINE
// ============================================
typedef enum {
    HO_STATE_IDLE = 0,           // No handover in progress
    HO_STATE_TRIGGERED,          // A3 event triggered, ready to execute
    HO_STATE_COMMAND_SENT,       // Control message sent to RAN
    HO_STATE_WAITING_COMPLETION, // Waiting for handover to complete
    HO_STATE_COMPLETED,          // Successfully completed
    HO_STATE_FAILED,             // Failed (will retry after cooldown)
    HO_STATE_COOLDOWN            // Cooldown period after handover
} handover_state_e;

const char* ho_state_to_string(handover_state_e state) {
    switch(state) {
        case HO_STATE_IDLE: return "IDLE";
        case HO_STATE_TRIGGERED: return "TRIGGERED";
        case HO_STATE_COMMAND_SENT: return "COMMAND_SENT";
        case HO_STATE_WAITING_COMPLETION: return "WAITING_COMPLETION";
        case HO_STATE_COMPLETED: return "COMPLETED";
        case HO_STATE_FAILED: return "FAILED";
        case HO_STATE_COOLDOWN: return "COOLDOWN";
        default: return "UNKNOWN";
    }
}

typedef struct {
    handover_state_e state;
    uint16_t source_cell;
    uint16_t target_cell;
    uint16_t prev_cell;           // Cell before last HO — anti-ping-pong
    time_t trigger_time;
    time_t command_time;
    time_t completion_time;
    time_t last_handover_time;    // For cooldown tracking (wall-clock, used for COMMAND_SENT timeout)
    double last_handover_sim_sec; // Sim-time of last HO — anti-ping-pong guard
    int total_handovers;          // Total successful handovers
} handover_context_t;

#define ANTI_PP_SIM_SEC 5.0       // Block returning to previous cell for 5 simulated seconds

// ============================================
// SINR MEASUREMENT STRUCTURES
// ============================================
struct SINRNeighboringValues {
    bool is_available;
    uint16_t neighCellID;
    double sinr;                  // Average SINR
    int measurement_count;        // Number of measurements averaged
    bool meets_a3_criteria;       // Does this neighbor meet A3 conditions?
};

struct SINRServingValues {
    bool is_available;
    uint16_t ueID;
    double sinr;                  // Current serving cell SINR
    double rsrp;          // Actual RSRP dBm from KPM Level measurement
    double cqi;           // CQI value
    double dl_bitrate;    // DL throughput kbps
    double ul_bitrate;    // UL throughput kbps
    double speed;         // UE speed estimate m/s
    struct SINRNeighboringValues* neighCells;
    size_t numOfNeighCells;
    handover_context_t ho_context;
    time_t last_measurement_update;
};

struct SINR_Map {
    uint16_t cellID;
    struct SINRServingValues* connectedUEs;
    size_t numOfConnectedUEs;
    bool is_active;
};

struct registeredCells {
    bool is_registered;
    struct SINR_Map* sinrMap;
};

// ============================================
// GLOBAL STATE
// ============================================
static struct registeredCells cells_sinr_map[MAX_REGISTERED_CELLS] = {{false, NULL}};
static ue_id_e2sm_t ue_id;
static uint64_t const period_ms = 100;
static pthread_mutex_t mtx;
static bool g_rl_available = false;  // RL prediction service availability

static FILE* g_decision_log = NULL;
static uint64_t g_cycle_count = 0;
static uint64_t g_total_a3_triggers = 0;
static uint64_t g_total_gru_blocked = 0;
static uint64_t g_total_executed = 0;
// Actual simulation time read from ~/lstm_features.csv (written by ns-3).
// Updated at the start of every decision cycle.
static double g_sim_time_sec = 0.0;

// Read the last line of ~/lstm_features.csv and extract the first CSV column
// (sim_time).  Falls back to g_cycle_count*0.25 when the file isn't ready yet.
static void update_sim_time_from_csv(void) {
    static char path[256];
    if (!path[0]) {
        const char* home = getenv("HOME");
        snprintf(path, sizeof(path), "%s/lstm_features.csv", home ? home : "");
    }
    FILE* f = fopen(path, "r");
    if (!f) return;
    // Seek to the last ~256 bytes to avoid reading megabytes
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz > 256) fseek(f, sz - 256, SEEK_SET);
    else           rewind(f);
    char buf[300] = {0};
    int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n <= 0) return;
    buf[n] = '\0';
    // Find last complete line (skip the trailing newline if any)
    char* end = buf + n - 1;
    while (end > buf && (*end == '\n' || *end == '\r')) *end-- = '\0';
    char* nl = strrchr(buf, '\n');
    char* last = nl ? nl + 1 : buf;
    // Skip header line that starts with a letter
    if (*last < '0' || *last > '9') return;
    double t = atof(last);
    if (t > 0.0) g_sim_time_sec = t;
}

// Mapping from cell_id → node index in the nodes array.
// Populated by per-node KPM callbacks; avoids broadcasting CONTROL to all nodes.
static int8_t  g_cell_to_node_idx[MAX_REGISTERED_CELLS];  // -1 = unknown
static uint8_t g_current_cb_node = 255;                    // set in per-node wrappers

// ============================================
// FORWARD DECLARATIONS
// ============================================
struct SINRServingValues* get_UE(const uint16_t cellID, const uint16_t ueID);
void confirm_handover_completion(uint16_t ueID, uint16_t old_cell, uint16_t new_cell);

// ============================================
// UTILITY FUNCTIONS
// ============================================

static void log_gnb_ue_id(ue_id_e2sm_t ue_id) {
    if (ue_id.gnb.gnb_cu_ue_f1ap_lst != NULL) {
        for (size_t i = 0; i < ue_id.gnb.gnb_cu_ue_f1ap_lst_len; i++) {
            printf("UE ID type = gNB-CU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb.gnb_cu_ue_f1ap_lst[i]);
        }
    } else {
        printf("UE ID type = gNB, amf_ue_ngap_id = %lu\n", ue_id.gnb.amf_ue_ngap_id);
    }
    if (ue_id.gnb.ran_ue_id != NULL) {
        printf("ran_ue_id = %lx\n", *ue_id.gnb.ran_ue_id);
    }
}

static void log_du_ue_id(ue_id_e2sm_t ue_id) {
    printf("UE ID type = gNB-DU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb_du.gnb_cu_ue_f1ap);
    if (ue_id.gnb_du.ran_ue_id != NULL) {
        printf("ran_ue_id = %lx\n", *ue_id.gnb_du.ran_ue_id);
    }
}

static void log_cuup_ue_id(ue_id_e2sm_t ue_id) {
    printf("UE ID type = gNB-CU-UP, gnb_cu_cp_ue_e1ap = %u\n", ue_id.gnb_cu_up.gnb_cu_cp_ue_e1ap);
    if (ue_id.gnb_cu_up.ran_ue_id != NULL) {
        printf("ran_ue_id = %lx\n", *ue_id.gnb_cu_up.ran_ue_id);
    }
}

static void log_int_value(byte_array_t name, meas_record_lst_t meas_record) {
    if (cmp_str_ba("RRU.PrbTotDl", name) == 0) {
        printf("RRU.PrbTotDl = %d [PRBs]\n", meas_record.int_val);
    } else if (cmp_str_ba("DRB.PdcpSduVolumeDL", name) == 0) {
        printf("DRB.PdcpSduVolumeDL = %d [kb]\n", meas_record.int_val);
    } else if (cmp_str_ba("DRB.PdcpSduVolumeUL", name) == 0) {
        printf("DRB.PdcpSduVolumeUL = %d [kb]\n", meas_record.int_val);
    }
}

static void log_real_value(byte_array_t name, meas_record_lst_t meas_record) {
    if (cmp_str_ba("DRB.RlcSduDelayDl", name) == 0) {
        printf("DRB.RlcSduDelayDl = %.2f [μs]\n", meas_record.real_val);
    } else if (cmp_str_ba("DRB.UEThpDl", name) == 0) {
        printf("DRB.UEThpDl = %.2f [kbps]\n", meas_record.real_val);
    } else if (cmp_str_ba("DRB.UEThpUl", name) == 0) {
        printf("DRB.UEThpUl = %.2f [kbps]\n", meas_record.real_val);
    } else if (strncmp((const char*)name.buf, "L3servingSINR3gpp_cell_", strlen("L3servingSINR3gpp_cell_")) == 0) {
        printf("%s, sinr= %.4f [db]\n", name.buf, meas_record.real_val);
    } else if (strncmp((const char*)name.buf, "L3neighSINRListOf_UEID_", strlen("L3neighSINRListOf_UEID_")) == 0) {
        printf("%s, sinr= %.4f [db]\n", name.buf, meas_record.real_val);
    }
}

typedef void (*log_meas_value)(byte_array_t name, meas_record_lst_t meas_record);

static log_meas_value get_meas_value[END_MEAS_VALUE] = {
    log_int_value,
    log_real_value,
    NULL,
};

static void match_meas_name_type(meas_type_t meas_type, meas_record_lst_t meas_record) {
    get_meas_value[meas_record.value](meas_type.name, meas_record);
}

static void match_id_meas_type(meas_type_t meas_type, meas_record_lst_t meas_record) {
    (void)meas_type;
    (void)meas_record;
    assert(false && "ID Measurement Type not yet supported");
}

typedef void (*check_meas_type)(meas_type_t meas_type, meas_record_lst_t meas_record);

static check_meas_type match_meas_type[END_MEAS_TYPE] = {
    match_meas_name_type,
    match_id_meas_type,
};

// ============================================
// DATA STRUCTURE MANAGEMENT
// ============================================

struct SINR_Map* add_SINR(const uint16_t cellID) {
    assert(cellID != 0 && cellID < MAX_REGISTERED_CELLS);
    
    if (cells_sinr_map[cellID].sinrMap == NULL && !cells_sinr_map[cellID].is_registered) {
        cells_sinr_map[cellID].sinrMap = (struct SINR_Map*)calloc(1, sizeof(struct SINR_Map));
        assert(cells_sinr_map[cellID].sinrMap != NULL && "Memory allocation failed");
        
        cells_sinr_map[cellID].is_registered = true;
        cells_sinr_map[cellID].sinrMap->cellID = cellID;
        cells_sinr_map[cellID].sinrMap->connectedUEs = NULL;
        cells_sinr_map[cellID].sinrMap->numOfConnectedUEs = 0;
        cells_sinr_map[cellID].sinrMap->is_active = true;
        
        printf("[DataMgmt] Registered new cell: %d\n", cellID);
    }
    return cells_sinr_map[cellID].sinrMap;
}

void add_UE(struct SINR_Map* cell, const uint16_t ueID, const double sinr) {
    assert(cell != NULL);
    assert(ueID < MAX_REGISTERED_UES);
    
    if (cell->connectedUEs == NULL) {
        cell->connectedUEs = (struct SINRServingValues*)calloc(MAX_REGISTERED_UES, 
                                                              sizeof(struct SINRServingValues));
        assert(cell->connectedUEs != NULL && "Memory allocation failed");
    }
    
    struct SINRServingValues* ue = &cell->connectedUEs[ueID];
    
    // ✅ FIX #3: Check if UE was in a different cell (handover detection)
    bool ue_moved = false;
    uint16_t old_cell_id = 0;
    
    if (!ue->is_available) {
        // Check all other cells for this UE
        for (int i = 0; i < MAX_REGISTERED_CELLS; i++) {
            if (i == cell->cellID) continue;  // Skip current cell
            
            struct SINRServingValues* other_ue = get_UE(i, ueID);
            if (other_ue != NULL) {
                // UE found in different cell - handover detected!
                ue_moved = true;
                old_cell_id = i;
                printf("[DataMgmt] 🔄 Detected UE %d movement: Cell %d → Cell %d\n",
                       ueID, i, cell->cellID);
                break;
            }
        }
    }
    
    // Initialize new UE
    if (!ue->is_available) {
        ue->ueID = ueID;
        ue->neighCells = NULL;
        ue->numOfNeighCells = 0;
        ue->rsrp = 0.0;
        ue->cqi = 0.0;
        ue->dl_bitrate = 0.0;
        ue->ul_bitrate = 0.0;
        ue->speed = 0.0;
        ue->ho_context.state = HO_STATE_IDLE;
        ue->ho_context.source_cell = cell->cellID;
        ue->ho_context.target_cell = 0;
        ue->ho_context.total_handovers = 0;
        ue->ho_context.last_handover_time = 0;
        ue->is_available = true;
        ue->last_measurement_update = time(NULL);
        cell->numOfConnectedUEs++;
        
        if (ue_moved) {
            // ✅ Handover detected and confirmed!
            confirm_handover_completion(ueID, old_cell_id, cell->cellID);
        } else {
            printf("[DataMgmt] ➕ New UE %d added to cell %d\n", ueID, cell->cellID);
        }
    }
    
    // Update SINR
    ue->sinr = sinr;
    ue->last_measurement_update = time(NULL);
}

struct SINRServingValues* get_UE(const uint16_t cellID, const uint16_t ueID) {
    if (cellID >= MAX_REGISTERED_CELLS || ueID >= MAX_REGISTERED_UES) {
        return NULL;
    }
    
    if (cells_sinr_map[cellID].is_registered) {
        struct SINRServingValues* ue = &cells_sinr_map[cellID].sinrMap->connectedUEs[ueID];
        if (ue->is_available && ue->ueID == ueID) {
            return ue;
        }
    }
    return NULL;
}

void add_neighCell(struct SINRServingValues* UE, const uint16_t neighCellID, const double sinr) {
    assert(UE != NULL);
    assert(neighCellID < MAX_REGISTERED_NEIGHBOURS);
    
    // Initialize neighbor cells array if needed
    if (UE->neighCells == NULL) {
        UE->neighCells = (struct SINRNeighboringValues*)calloc(MAX_REGISTERED_NEIGHBOURS,
                                                               sizeof(struct SINRNeighboringValues));
        assert(UE->neighCells != NULL && "Memory allocation failed");
    }
    
    struct SINRNeighboringValues* neighCell = &UE->neighCells[neighCellID];
    
    // Initialize new neighbor
    if (!neighCell->is_available) {
        neighCell->neighCellID = neighCellID;
        neighCell->sinr = sinr;
        neighCell->measurement_count = 1;
        neighCell->is_available = true;
        neighCell->meets_a3_criteria = false;
        UE->numOfNeighCells++;
        
        printf("[Measurement] New neighbor cell %d for UE %d: SINR=%.2f dB\n", 
               neighCellID, UE->ueID, sinr);
    }
    // Update existing neighbor with running average
    else {
        // Always update running average; cap count at MAX so A3 stays eligible
        double alpha = (neighCell->measurement_count < MAX_NUM_OF_RIC_INDICATIONS) ?
                       (1.0 / (neighCell->measurement_count + 1)) :
                       (1.0 / MAX_NUM_OF_RIC_INDICATIONS);
        neighCell->sinr = (1.0 - alpha) * neighCell->sinr + alpha * sinr;
        if (neighCell->measurement_count < MAX_NUM_OF_RIC_INDICATIONS)
            neighCell->measurement_count++;

        printf("[Measurement] Updated neighbor %d for UE %d: SINR=%.2f dB (sample %d/%d)\n",
               neighCellID, UE->ueID, neighCell->sinr,
               neighCell->measurement_count, MAX_NUM_OF_RIC_INDICATIONS);
    }
}

// ============================================
// MESSAGE PARSING
// ============================================

struct InfoObj {
    uint16_t cellID;
    uint16_t ueID;
};

struct InfoObj parseServingMsg(const char* msg) {
    struct InfoObj info;
    int ret = sscanf(msg, "L3servingSINR3gpp_cell_%hd_UEID_%hd", &info.cellID, &info.ueID);
    
    if (ret == 2) {
        return info;
    }
    
    info.cellID = 0;
    info.ueID = 0;
    return info;
}

struct InfoObj parseNeighMsg(const char* msg) {
    struct InfoObj info;
    int ret = sscanf(msg, "L3neighSINRListOf_UEID_%hd_of_Cell_%hd", &info.ueID, &info.cellID);

    if (ret == 2) {
        return info;
    }

    info.ueID = 0;
    info.cellID = 0;
    return info;
}

struct InfoObj parseRSRPMsg(const char* msg) {
    struct InfoObj info;
    int ret = sscanf(msg, "L3servingRSRP3gpp_cell_%hd_UEID_%hd", &info.cellID, &info.ueID);
    if (ret == 2) return info;
    info.cellID = 0; info.ueID = 0;
    return info;
}

struct InfoObj parseDRBMsg(const char* msg, const char* prefix) {
    struct InfoObj info;
    char fmt[128];
    snprintf(fmt, sizeof(fmt), "%s%%hd_UEID_%%hd", prefix);
    int ret = sscanf(msg, fmt, &info.cellID, &info.ueID);
    if (ret == 2) return info;
    info.cellID = 0; info.ueID = 0;
    return info;
}

bool isMeasNameContains(const char* meas_name, const char* name) {
    return strncmp(meas_name, name, strlen(name)) == 0;
}

// ============================================
// KPM INDICATION PROCESSING
// ============================================

static void log_kpm_measurements(kpm_ind_msg_format_1_t const* msg_frm_1) {
    // Enhanced validation
    if (msg_frm_1 == NULL ||
        msg_frm_1->meas_info_lst_len == 0 || 
        msg_frm_1->meas_info_lst_len > 1000 ||
        msg_frm_1->meas_data_lst_len == 0 ||
        msg_frm_1->meas_data_lst_len > 1000000) {
        
        fprintf(stderr, "[WARN] Invalid/Corrupted KPM - info=%zu, data=%zu (SKIPPING)\n", 
                msg_frm_1->meas_info_lst_len,
                msg_frm_1->meas_data_lst_len);
        return;
    }
    
    if (msg_frm_1->meas_info_lst_len != msg_frm_1->meas_data_lst_len) {
        printf("[ERROR] Mismatch: info_len=%zu, data_len=%zu\n", 
               msg_frm_1->meas_info_lst_len,
               msg_frm_1->meas_data_lst_len);
        return;
    }
    
    // Process measurements
    for (size_t i = 0; i < msg_frm_1->meas_info_lst_len; i++) {
        meas_type_t const meas_type = msg_frm_1->meas_info_lst[i].meas_type;
        meas_data_lst_t const data_item = msg_frm_1->meas_data_lst[i];
        
        for (size_t j = 0; j < data_item.meas_record_len;) {
            meas_record_lst_t const record_item = data_item.meas_record_lst[j];
            
            if (meas_type.type == NAME_MEAS_TYPE) {
                // Serving Cell SINR
                if (isMeasNameContains((const char*)meas_type.name.buf, "L3servingSINR3gpp_cell_")) {
                    struct InfoObj info = parseServingMsg((const char*)meas_type.name.buf);
                    
                    if (info.cellID != 0 && info.cellID < MAX_REGISTERED_CELLS) {
                        double sinr = record_item.real_val;
                        struct SINR_Map* cell = add_SINR(info.cellID);
                        add_UE(cell, info.ueID, sinr);
                        printf("[KPM] Serving Cell %d - UE %d: SINR=%.2f dB\n",
                               info.cellID, info.ueID, sinr);
                        // Store cell-to-node mapping for targeted CONTROL
                        if (g_current_cb_node != 255) {
                            g_cell_to_node_idx[info.cellID] = (int8_t)g_current_cb_node;
                        }
                    }
                }
                // Neighbor Cell SINR
                else if (isMeasNameContains((const char*)meas_type.name.buf, "L3neighSINRListOf_UEID_")) {
                    struct InfoObj info = parseNeighMsg((const char*)meas_type.name.buf);

                    if (info.cellID != 0 && info.ueID < MAX_REGISTERED_UES &&
                        (j + 1) < data_item.meas_record_len) {

                        meas_record_lst_t const sinr_record = record_item;
                        meas_record_lst_t const neighbor_id = data_item.meas_record_lst[j + 1];

                        struct SINRServingValues* UE = get_UE(info.cellID, info.ueID);
                        if (UE != NULL) {
                            add_neighCell(UE, neighbor_id.int_val, sinr_record.real_val);
                        }

                        j += 2;
                        continue;
                    }
                }
                // Serving Cell RSRP
                else if (isMeasNameContains((const char*)meas_type.name.buf, "L3servingRSRP3gpp_cell_")) {
                    struct InfoObj info = parseRSRPMsg((const char*)meas_type.name.buf);
                    if (info.cellID != 0 && info.cellID < MAX_REGISTERED_CELLS) {
                        struct SINRServingValues* ue = get_UE(info.cellID, info.ueID);
                        if (ue != NULL) {
                            ue->rsrp = record_item.real_val;
                            printf("[KPM] Serving Cell %d - UE %d: RSRP=%.2f dBm\n",
                                   info.cellID, info.ueID, ue->rsrp);
                        }
                    }
                }
                // DL throughput
                else if (isMeasNameContains((const char*)meas_type.name.buf, "DRB.UEThpDl_cell_")) {
                    struct InfoObj info = parseDRBMsg((const char*)meas_type.name.buf, "DRB.UEThpDl_cell_");
                    if (info.cellID != 0 && info.cellID < MAX_REGISTERED_CELLS) {
                        struct SINRServingValues* ue = get_UE(info.cellID, info.ueID);
                        if (ue != NULL) {
                            ue->dl_bitrate = record_item.real_val;
                        }
                    }
                }
                // UL throughput
                else if (isMeasNameContains((const char*)meas_type.name.buf, "DRB.UEThpUl_cell_")) {
                    struct InfoObj info = parseDRBMsg((const char*)meas_type.name.buf, "DRB.UEThpUl_cell_");
                    if (info.cellID != 0 && info.cellID < MAX_REGISTERED_CELLS) {
                        struct SINRServingValues* ue = get_UE(info.cellID, info.ueID);
                        if (ue != NULL) {
                            ue->ul_bitrate = record_item.real_val;
                        }
                    }
                }
            }
            j++;
        }
    }
}

static void sm_cb_kpm(sm_ag_if_rd_t const* rd) {
    assert(rd != NULL);
    assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
    assert(rd->ind.type == KPM_STATS_V3_0);

    kpm_ind_data_t const* ind = &rd->ind.kpm.ind;
    kpm_ind_msg_format_3_t const* msg_frm_3 = &ind->msg.frm_3;

    {
        lock_guard(&mtx);

        if (msg_frm_3 == NULL ||
            msg_frm_3->ue_meas_report_lst_len == 0 ||
            msg_frm_3->ue_meas_report_lst_len > 1000 ||
            msg_frm_3->meas_report_per_ue == NULL) {
            fprintf(stderr, "[WARN] Invalid KPM Format 3 data (SKIPPING)\n");
            return;
        }

        for (size_t i = 0; i < msg_frm_3->ue_meas_report_lst_len; i++) {
            ue_id_e2sm_t const ue_id_e2sm = msg_frm_3->meas_report_per_ue[i].ue_meas_report_lst;

            // Save UE ID for RC Control
            free_ue_id_e2sm(&ue_id);
            ue_id = cp_ue_id_e2sm(&ue_id_e2sm);

            // Process measurements (g_current_cb_node is set by the per-node wrapper)
            log_kpm_measurements(&msg_frm_3->meas_report_per_ue[i].ind_msg_format_1);
        }
    }
}

// Per-node KPM callback wrappers — each sets g_current_cb_node before calling sm_cb_kpm.
// Supports up to 16 E2 nodes (7 mmWave + 1 LTE + margin).
#define MAKE_KPM_NODE_CB(N) \
    static void sm_cb_kpm_node##N(sm_ag_if_rd_t const* rd) { \
        g_current_cb_node = (uint8_t)(N); sm_cb_kpm(rd); }
MAKE_KPM_NODE_CB(0)  MAKE_KPM_NODE_CB(1)  MAKE_KPM_NODE_CB(2)  MAKE_KPM_NODE_CB(3)
MAKE_KPM_NODE_CB(4)  MAKE_KPM_NODE_CB(5)  MAKE_KPM_NODE_CB(6)  MAKE_KPM_NODE_CB(7)
MAKE_KPM_NODE_CB(8)  MAKE_KPM_NODE_CB(9)  MAKE_KPM_NODE_CB(10) MAKE_KPM_NODE_CB(11)
MAKE_KPM_NODE_CB(12) MAKE_KPM_NODE_CB(13) MAKE_KPM_NODE_CB(14) MAKE_KPM_NODE_CB(15)

static sm_cb const g_kpm_node_cbs[] = {
    sm_cb_kpm_node0,  sm_cb_kpm_node1,  sm_cb_kpm_node2,  sm_cb_kpm_node3,
    sm_cb_kpm_node4,  sm_cb_kpm_node5,  sm_cb_kpm_node6,  sm_cb_kpm_node7,
    sm_cb_kpm_node8,  sm_cb_kpm_node9,  sm_cb_kpm_node10, sm_cb_kpm_node11,
    sm_cb_kpm_node12, sm_cb_kpm_node13, sm_cb_kpm_node14, sm_cb_kpm_node15,
};
#define MAX_KPM_NODE_CBS 16

// ============================================
// A3 EVENT EVALUATION
// ============================================

typedef struct {
    bool a3_triggered;
    uint16_t best_target_cell;
    double best_target_sinr;
    double serving_sinr;
    double sinr_gain;
} a3_evaluation_result_t;

a3_evaluation_result_t evaluate_a3_event(struct SINRServingValues* ue, uint16_t serving_cell_id) {
    a3_evaluation_result_t result = {
        .a3_triggered = false,
        .best_target_cell = 0,
        .best_target_sinr = -999.0,
        .serving_sinr = ue->sinr,
        .sinr_gain = 0.0
    };
    
    if (ue->neighCells == NULL || ue->numOfNeighCells == 0) {
        return result;
    }
    
    // Calculate A3 threshold: Serving_SINR + Offset + Hysteresis
    double a3_threshold = ue->sinr + A3_OFFSET + A3_HYSTERESIS;
    
    printf("\n[A3 Eval] UE %d @ Cell %d: Serving_SINR=%.2f dB, Threshold=%.2f dB\n",
           ue->ueID, serving_cell_id, ue->sinr, a3_threshold);
    
    // Evaluate all neighbors
    for (int i = 0; i < MAX_REGISTERED_NEIGHBOURS; i++) {
        if (!ue->neighCells[i].is_available) {
            continue;
        }
        
        struct SINRNeighboringValues* neighbor = &ue->neighCells[i];
        
        // Need enough measurements
        if (neighbor->measurement_count < MAX_NUM_OF_RIC_INDICATIONS) {
            printf("  [A3] Cell %d: SINR=%.2f dB - Insufficient samples (%d/%d)\n",
                   i, neighbor->sinr, neighbor->measurement_count, MAX_NUM_OF_RIC_INDICATIONS);
            neighbor->meets_a3_criteria = false;
            continue;
        }
        
        // Check if target cell is active
        if (!cells_sinr_map[i].is_registered || !cells_sinr_map[i].sinrMap->is_active) {
            printf("  [A3] Cell %d: Inactive or unregistered\n", i);
            neighbor->meets_a3_criteria = false;
            continue;
        }
        
        // A3 Event: Neighbor > Threshold
        if (neighbor->sinr > a3_threshold) {
            neighbor->meets_a3_criteria = true;
            double gain = neighbor->sinr - ue->sinr;
            
            printf("  [A3] Cell %d: SINR=%.2f dB ✓ A3 TRIGGERED (Gain=%.2f dB)\n",
                   i, neighbor->sinr, gain);
            
            // Track best neighbor
            if (neighbor->sinr > result.best_target_sinr) {
                result.best_target_sinr = neighbor->sinr;
                result.best_target_cell = i;
                result.sinr_gain = gain;
                result.a3_triggered = true;
            }
        } else {
            neighbor->meets_a3_criteria = false;
            printf("  [A3] Cell %d: SINR=%.2f dB - Below threshold\n",
                   i, neighbor->sinr);
        }
    }
    
    if (result.a3_triggered) {
        printf("  [A3] ✓ BEST TARGET: Cell %d (SINR=%.2f dB, Gain=%.2f dB)\n",
               result.best_target_cell, result.best_target_sinr, result.sinr_gain);
    } else {
        printf("  [A3] ✗ No suitable target found\n");
    }
    
    return result;
}

// ============================================
// HANDOVER STATE MANAGEMENT
// ============================================

bool can_attempt_handover(struct SINRServingValues* ue) {
    time_t now = time(NULL);
    double sim_now = g_sim_time_sec; // current sim-time in seconds

    switch (ue->ho_context.state) {
        case HO_STATE_IDLE:
            // Always ready for new handover
            return true;

        case HO_STATE_TRIGGERED:
            // Can retry if stuck in triggered state
            return true;

        case HO_STATE_COMMAND_SENT:
        case HO_STATE_WAITING_COMPLETION:
            // Check for timeout (wall-clock is fine here — it's a real-time safety net)
            if (difftime(now, ue->ho_context.command_time) > HANDOVER_COMPLETION_TIMEOUT) {
                printf("[StateMgmt] UE %d: Handover timeout, resetting to IDLE\n", ue->ueID);
                ue->ho_context.state = HO_STATE_IDLE;
                return true;
            }
            return false;

        case HO_STATE_COMPLETED:
        case HO_STATE_COOLDOWN:
            // Check cooldown period using sim-time
            if ((sim_now - ue->ho_context.last_handover_sim_sec) >= HANDOVER_COOLDOWN_SIM_SEC) {
                printf("[StateMgmt] UE %d: Cooldown expired, ready for new handover\n", ue->ueID);
                ue->ho_context.state = HO_STATE_IDLE;
                return true;
            }
            printf("[StateMgmt] UE %d: In cooldown (%.2f sim-s remaining)\n",
                   ue->ueID, HANDOVER_COOLDOWN_SIM_SEC - (sim_now - ue->ho_context.last_handover_sim_sec));
            return false;
            
        case HO_STATE_FAILED:
            // Can retry immediately after failure
            printf("[StateMgmt] UE %d: Retrying after failure\n", ue->ueID);
            ue->ho_context.state = HO_STATE_IDLE;
            return true;
            
        default:
            return false;
    }
}

void update_handover_state_on_success(struct SINRServingValues* ue) {
    time_t now = time(NULL);
    double sim_now = g_sim_time_sec;
    ue->ho_context.state = HO_STATE_COMPLETED;
    ue->ho_context.completion_time = now;
    // Record previous cell and sim-time for anti-ping-pong guard
    ue->ho_context.prev_cell             = ue->ho_context.source_cell;
    ue->ho_context.last_handover_time    = now;
    ue->ho_context.last_handover_sim_sec = sim_now;
    ue->ho_context.total_handovers++;

    printf("[StateMgmt] UE %d: Handover #%d completed (prev_cell=%d, sim_t=%.2fs)\n",
           ue->ueID, ue->ho_context.total_handovers, ue->ho_context.prev_cell, sim_now);

    // Enter cooldown
    ue->ho_context.state = HO_STATE_COOLDOWN;
}

void update_handover_state_on_failure(struct SINRServingValues* ue) {
    ue->ho_context.state = HO_STATE_FAILED;
    printf("[StateMgmt] UE %d: Handover failed\n", ue->ueID);
}

// ============================================
// HANDOVER CONFIRMATION FROM KPM (FIX #2)
// ============================================

void confirm_handover_completion(uint16_t ueID, uint16_t old_cell, uint16_t new_cell) {
    printf("\n[HandoverConfirm] ═══════════════════════════════════════\n");
    printf("[HandoverConfirm] 🔄 UE %d moved: Cell %d → Cell %d\n", 
           ueID, old_cell, new_cell);
    
    // Get old UE context
    if (!cells_sinr_map[old_cell].is_registered) {
        printf("[HandoverConfirm] ⚠️ Old cell %d not registered\n", old_cell);
        return;
    }
    
    struct SINRServingValues* old_ue = get_UE(old_cell, ueID);
    if (old_ue == NULL) {
        printf("[HandoverConfirm] ⚠️ UE %d not found in old cell %d\n", ueID, old_cell);
        return;
    }
    
    // Get new UE context
    if (!cells_sinr_map[new_cell].is_registered) {
        printf("[HandoverConfirm] ⚠️ New cell %d not registered\n", new_cell);
        return;
    }
    
    struct SINRServingValues* new_ue = get_UE(new_cell, ueID);
    if (new_ue == NULL) {
        printf("[HandoverConfirm] ⚠️ UE %d not found in new cell %d\n", ueID, new_cell);
        return;
    }
    
    // Copy handover context from old to new
    handover_context_t old_context = old_ue->ho_context;
    new_ue->ho_context = old_context;
    
    printf("[HandoverConfirm] Old state: %s\n", ho_state_to_string(old_context.state));
    printf("[HandoverConfirm] Expected target: %d, Actual: %d\n", 
           old_context.target_cell, new_cell);
    
    // Check if this was a pending handover.
    // Accept both COMMAND_SENT and WAITING_COMPLETION as valid pending states —
    // execute_handover sets COMMAND_SENT and there is no transition to WAITING_COMPLETION.
    bool was_pending = (old_context.state == HO_STATE_WAITING_COMPLETION ||
                        old_context.state == HO_STATE_COMMAND_SENT);
    if (was_pending && old_context.target_cell == new_cell) {

        // ✅ Handover completed successfully — sets prev_cell + last_handover_time
        update_handover_state_on_success(new_ue);

    } else if (was_pending) {
        printf("[HandoverConfirm] ⚠️ UE moved to unexpected cell (expected %d, got %d)\n",
               old_context.target_cell, new_cell);
        new_ue->ho_context.state = HO_STATE_IDLE;
    } else {
        printf("[HandoverConfirm] ℹ️ UE moved but no pending handover (state was %s)\n",
               ho_state_to_string(old_context.state));
        new_ue->ho_context.state = HO_STATE_IDLE;
    }
    
    // Clean up old cell entry
    old_ue->is_available = false;
    old_ue->neighCells = NULL;  // Don't free - might be referenced
    old_ue->numOfNeighCells = 0;
    cells_sinr_map[old_cell].sinrMap->numOfConnectedUEs--;
    
    printf("[HandoverConfirm] ✅ Cleaned up old cell %d (remaining UEs: %zu)\n",
           old_cell, cells_sinr_map[old_cell].sinrMap->numOfConnectedUEs);
    printf("[HandoverConfirm] ═══════════════════════════════════════\n\n");
}

// ============================================
// RC CONTROL MESSAGE GENERATION
// ============================================

static test_info_lst_t filter_predicate(test_cond_type_e type, test_cond_e cond, int value) {
    test_info_lst_t dst = {0};
    
    dst.test_cond_type = type;
    dst.IsStat = TRUE_TEST_COND_TYPE;
    
    dst.test_cond = calloc(1, sizeof(test_cond_e));
    assert(dst.test_cond != NULL);
    *dst.test_cond = cond;
    
    dst.test_cond_value = calloc(1, sizeof(test_cond_value_t));
    assert(dst.test_cond_value != NULL);
    dst.test_cond_value->type = INTEGER_TEST_COND_VALUE;
    
    int64_t* int_value = calloc(1, sizeof(int64_t));
    assert(int_value != NULL);
    *int_value = value;
    dst.test_cond_value->int_value = int_value;
    
    return dst;
}

static label_info_lst_t fill_kpm_label(void) {
    label_info_lst_t label_item = {0};
    label_item.noLabel = ecalloc(1, sizeof(enum_value_e));
    *label_item.noLabel = TRUE_ENUM_VALUE;
    return label_item;
}

static kpm_act_def_format_1_t fill_act_def_frm_1(ric_report_style_item_t const* report_item) {
    assert(report_item != NULL);
    
    kpm_act_def_format_1_t ad_frm_1 = {0};
    size_t const sz = report_item->meas_info_for_action_lst_len;
    
    ad_frm_1.meas_info_lst_len = sz;
    ad_frm_1.meas_info_lst = calloc(sz, sizeof(meas_info_format_1_lst_t));
    assert(ad_frm_1.meas_info_lst != NULL);
    
    for (size_t i = 0; i < sz; i++) {
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

static kpm_act_def_t fill_report_style_4(ric_report_style_item_t const* report_item) {
    assert(report_item != NULL);
    assert(report_item->act_def_format_type == FORMAT_4_ACTION_DEFINITION);
    
    kpm_act_def_t act_def = {.type = FORMAT_4_ACTION_DEFINITION};
    
    act_def.frm_4.matching_cond_lst_len = 1;
    act_def.frm_4.matching_cond_lst = calloc(1, sizeof(matching_condition_format_4_lst_t));
    assert(act_def.frm_4.matching_cond_lst != NULL);
    
    test_cond_type_e const type = IsStat_TEST_COND_TYPE;
    test_cond_e const condition = LESSTHAN_TEST_COND;
    int const value = 40;
    act_def.frm_4.matching_cond_lst[0].test_info_lst = filter_predicate(type, condition, value);
    
    act_def.frm_4.action_def_format_1 = fill_act_def_frm_1(report_item);
    
    return act_def;
}

typedef kpm_act_def_t (*fill_kpm_act_def)(ric_report_style_item_t const* report_item);

static fill_kpm_act_def get_kpm_act_def[END_RIC_SERVICE_REPORT] = {
    NULL, NULL, NULL, fill_report_style_4, NULL,
};

static kpm_sub_data_t gen_kpm_subs(kpm_ran_function_def_t const* ran_func) {
    assert(ran_func != NULL);
    assert(ran_func->ric_event_trigger_style_list != NULL);
    
    kpm_sub_data_t kpm_sub = {0};
    
    assert(ran_func->ric_event_trigger_style_list[0].format_type == FORMAT_1_RIC_EVENT_TRIGGER);
    kpm_sub.ev_trg_def.type = FORMAT_1_RIC_EVENT_TRIGGER;
    kpm_sub.ev_trg_def.kpm_ric_event_trigger_format_1.report_period_ms = period_ms;
    
    kpm_sub.sz_ad = 1;
    kpm_sub.ad = calloc(1, sizeof(kpm_act_def_t));
    assert(kpm_sub.ad != NULL);
    
    ric_report_style_item_t* const report_item = &ran_func->ric_report_style_list[0];
    ric_service_report_e const report_style_type = report_item->report_style_type;
    *kpm_sub.ad = get_kpm_act_def[report_style_type](report_item);
    
    return kpm_sub;
}

static size_t find_sm_idx(sm_ran_function_t* rf, size_t sz, bool (*f)(sm_ran_function_t const*, int const), int const id) {
    for (size_t i = 0; i < sz; i++) {
        if (f(&rf[i], id)) return i;
    }
    assert(0 != 0 && "SM ID not found");
}

static bool eq_sm(sm_ran_function_t const* elem, int const id) {
    return elem->id == id;
}

static e2sm_rc_ctrl_hdr_frmt_1_t gen_rc_ctrl_hdr_frmt_1(ue_id_e2sm_t ue_id, uint32_t ric_style_type, uint16_t ctrl_act_id) {
    e2sm_rc_ctrl_hdr_frmt_1_t dst = {0};
    dst.ue_id = cp_ue_id_e2sm(&ue_id);
    dst.ric_style_type = ric_style_type;
    dst.ctrl_act_id = ctrl_act_id;
    return dst;
}

static e2sm_rc_ctrl_hdr_t gen_rc_ctrl_hdr(e2sm_rc_ctrl_hdr_e hdr_frmt, ue_id_e2sm_t ue_id, uint32_t ric_style_type, uint16_t ctrl_act_id) {
    e2sm_rc_ctrl_hdr_t dst = {0};
    
    if (hdr_frmt == FORMAT_1_E2SM_RC_CTRL_HDR) {
        dst.format = FORMAT_1_E2SM_RC_CTRL_HDR;
        dst.frmt_1 = gen_rc_ctrl_hdr_frmt_1(ue_id, ric_style_type, ctrl_act_id);
    } else {
        assert(0 && "Unsupported control header format");
    }
    
    return dst;
}

static void set_EUTRA_CGI(seq_ran_param_t* EUTRA_CGI, const char targetcell) {
    assert(EUTRA_CGI != NULL);
    assert(targetcell >= '0' && targetcell <= '9');
    
    if (EUTRA_CGI->ran_param_val.flag_false == NULL) {
        EUTRA_CGI->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
        assert(EUTRA_CGI->ran_param_val.flag_false != NULL);
    }
    
    EUTRA_CGI->ran_param_val.flag_false->type = BIT_STRING_RAN_PARAMETER_VALUE;
    
    byte_array_t target_ba = {0};
    target_ba.len = 1;
    target_ba.buf = malloc(sizeof(uint8_t));
    assert(target_ba.buf != NULL);
    target_ba.buf[0] = targetcell;
    
    EUTRA_CGI->ran_param_val.flag_false->octet_str_ran.len = target_ba.len;
    EUTRA_CGI->ran_param_val.flag_false->octet_str_ran.buf = target_ba.buf;
}

static void gen_Target_Primary_Cell_ID(seq_ran_param_t* Target_Primary_Cell_ID, char targetcell) {
    Target_Primary_Cell_ID->ran_param_id = TARGET_PRIMARY_CELL_ID_8_4_4_1;
    Target_Primary_Cell_ID->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
    Target_Primary_Cell_ID->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
    assert(Target_Primary_Cell_ID->ran_param_val.strct != NULL);
    Target_Primary_Cell_ID->ran_param_val.strct->sz_ran_param_struct = 1;
    Target_Primary_Cell_ID->ran_param_val.strct->ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
    assert(Target_Primary_Cell_ID->ran_param_val.strct->ran_param_struct != NULL);
    
    seq_ran_param_t* CHOICE_Target_Cell = &Target_Primary_Cell_ID->ran_param_val.strct->ran_param_struct[0];
    CHOICE_Target_Cell->ran_param_id = CHOICE_TARGET_CELL_8_4_4_1;
    CHOICE_Target_Cell->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
    CHOICE_Target_Cell->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
    assert(CHOICE_Target_Cell->ran_param_val.strct != NULL);
    CHOICE_Target_Cell->ran_param_val.strct->sz_ran_param_struct = 2;
    CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
    assert(CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct != NULL);
    
    seq_ran_param_t* NR_Cell = &CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct[0];
    NR_Cell->ran_param_id = NR_CELL_8_4_4_1;
    NR_Cell->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
    NR_Cell->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
    assert(NR_Cell->ran_param_val.strct != NULL);
    NR_Cell->ran_param_val.strct->sz_ran_param_struct = 1;
    NR_Cell->ran_param_val.strct->ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
    
    seq_ran_param_t* NR_CGI = &NR_Cell->ran_param_val.strct->ran_param_struct[0];
    NR_CGI->ran_param_id = NR_CGI_8_4_4_1;
    NR_CGI->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
    NR_CGI->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(NR_CGI->ran_param_val.flag_false != NULL);
    NR_CGI->ran_param_val.flag_false->type = BIT_STRING_RAN_PARAMETER_VALUE;
    
    char nr_cgi_str[1] = {targetcell};
    byte_array_t nr_cgi = cp_str_to_ba(nr_cgi_str);
    NR_CGI->ran_param_val.flag_false->octet_str_ran.len = nr_cgi.len;
    NR_CGI->ran_param_val.flag_false->octet_str_ran.buf = nr_cgi.buf;
    
    seq_ran_param_t* EUTRA_Cell = &CHOICE_Target_Cell->ran_param_val.strct->ran_param_struct[1];
    EUTRA_Cell->ran_param_id = EUTRA_CELL_8_4_4_1;
    EUTRA_Cell->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
    EUTRA_Cell->ran_param_val.strct = calloc(1, sizeof(ran_param_struct_t));
    assert(EUTRA_Cell->ran_param_val.strct != NULL);
    EUTRA_Cell->ran_param_val.strct->sz_ran_param_struct = 1;
    EUTRA_Cell->ran_param_val.strct->ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
    
    seq_ran_param_t* EUTRA_CGI = &EUTRA_Cell->ran_param_val.strct->ran_param_struct[0];
    EUTRA_CGI->ran_param_id = EUTRA_CGI_8_4_4_1;
    EUTRA_CGI->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
    EUTRA_CGI->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(EUTRA_CGI->ran_param_val.flag_false != NULL);
    EUTRA_CGI->ran_param_val.flag_false->type = BIT_STRING_RAN_PARAMETER_VALUE;
    
    set_EUTRA_CGI(EUTRA_CGI, targetcell);
}

static void gen_List_of_PDU_sessions_for_handover(seq_ran_param_t* List_PDU_sessions_ho) {
    List_PDU_sessions_ho->ran_param_id = LIST_OF_PDU_SESSIONS_FOR_HANDOVER_8_4_4_1;
    List_PDU_sessions_ho->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
    List_PDU_sessions_ho->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
    assert(List_PDU_sessions_ho->ran_param_val.lst != NULL);
    List_PDU_sessions_ho->ran_param_val.lst->sz_lst_ran_param = 1;
    List_PDU_sessions_ho->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
    assert(List_PDU_sessions_ho->ran_param_val.lst->lst_ran_param != NULL);
    
    lst_ran_param_t* PDU_session_item = &List_PDU_sessions_ho->ran_param_val.lst->lst_ran_param[0];
    PDU_session_item->ran_param_struct.sz_ran_param_struct = 2;
    PDU_session_item->ran_param_struct.ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
    assert(PDU_session_item->ran_param_struct.ran_param_struct != NULL);
    
    seq_ran_param_t* PDU_Session_ID = &PDU_session_item->ran_param_struct.ran_param_struct[0];
    PDU_Session_ID->ran_param_id = PDU_SESSION_ID_8_4_4_1;
    PDU_Session_ID->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
    PDU_Session_ID->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(PDU_Session_ID->ran_param_val.flag_false != NULL);
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
    assert(List_of_QoS_flows->ran_param_val.lst != NULL);
    List_of_QoS_flows->ran_param_val.lst->sz_lst_ran_param = 1;
    List_of_QoS_flows->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
    assert(List_of_QoS_flows->ran_param_val.lst->lst_ran_param != NULL);
    
    lst_ran_param_t* QoS_flow_Item = &List_of_QoS_flows->ran_param_val.lst->lst_ran_param[0];
    QoS_flow_Item->ran_param_struct.sz_ran_param_struct = 1;
    QoS_flow_Item->ran_param_struct.ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
    assert(QoS_flow_Item->ran_param_struct.ran_param_struct != NULL);
    
    seq_ran_param_t* QoS_Flow_Id = &QoS_flow_Item->ran_param_struct.ran_param_struct[0];
    QoS_Flow_Id->ran_param_id = QOS_FLOW_IDENTIFIER_8_4_4_1;
    QoS_Flow_Id->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
    QoS_Flow_Id->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(QoS_Flow_Id->ran_param_val.flag_false != NULL);
    QoS_Flow_Id->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
    
    char qosid_str[3];
    snprintf(qosid_str, sizeof(qosid_str), "%d", QOS_FLOW_ID_1);
    byte_array_t qosid = cp_str_to_ba(qosid_str);
    QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.len = qosid.len;
    QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.buf = qosid.buf;
}

static void gen_List_of_DRBs_for_handover(seq_ran_param_t* List_DRBs_ho) {
    List_DRBs_ho->ran_param_id = LIST_OF_DRBS_FOR_HANDOVER_8_4_4_1;
    List_DRBs_ho->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
    List_DRBs_ho->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
    assert(List_DRBs_ho->ran_param_val.lst != NULL);
    List_DRBs_ho->ran_param_val.lst->sz_lst_ran_param = 1;
    List_DRBs_ho->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
    assert(List_DRBs_ho->ran_param_val.lst->lst_ran_param != NULL);
    
    lst_ran_param_t* DRB_item_ho = (lst_ran_param_t*)&List_DRBs_ho->ran_param_val.strct->ran_param_struct[0];
    DRB_item_ho->ran_param_struct.sz_ran_param_struct = 2;
    DRB_item_ho->ran_param_struct.ran_param_struct = calloc(2, sizeof(seq_ran_param_t));
    assert(DRB_item_ho->ran_param_struct.ran_param_struct != NULL);
    
    seq_ran_param_t* DRB_ID = &DRB_item_ho->ran_param_struct.ran_param_struct[0];
    DRB_ID->ran_param_id = DRB_ID_8_4_4_1;
    DRB_ID->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
    DRB_ID->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(DRB_ID->ran_param_val.flag_false != NULL);
    DRB_ID->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
    
    char DRB_ID_str[] = "3";
    byte_array_t drpID = cp_str_to_ba(DRB_ID_str);
    DRB_ID->ran_param_val.flag_false->octet_str_ran.len = drpID.len;
    DRB_ID->ran_param_val.flag_false->octet_str_ran.buf = drpID.buf;
    
    seq_ran_param_t* List_of_QoS_flows = &DRB_item_ho->ran_param_struct.ran_param_struct[1];
    List_of_QoS_flows->ran_param_id = LIST_OF_QOS_FLOWS_IN_THE_DRB_8_4_4_1;
    List_of_QoS_flows->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
    List_of_QoS_flows->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
    assert(List_of_QoS_flows->ran_param_val.lst != NULL);
    List_of_QoS_flows->ran_param_val.lst->sz_lst_ran_param = 1;
    List_of_QoS_flows->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
    assert(List_of_QoS_flows->ran_param_val.lst->lst_ran_param != NULL);
    
    lst_ran_param_t* QoS_flow_Item = &List_of_QoS_flows->ran_param_val.lst->lst_ran_param[0];
    QoS_flow_Item->ran_param_struct.sz_ran_param_struct = 1;
    QoS_flow_Item->ran_param_struct.ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
    assert(QoS_flow_Item->ran_param_struct.ran_param_struct != NULL);
    
    seq_ran_param_t* QoS_Flow_Id = &QoS_flow_Item->ran_param_struct.ran_param_struct[0];
    QoS_Flow_Id->ran_param_id = QOS_FLOW_IDENTIFIER_8_4_4_1;
    QoS_Flow_Id->ran_param_val.type = ELEMENT_KEY_FLAG_TRUE_RAN_PARAMETER_VAL_TYPE;
    QoS_Flow_Id->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(QoS_Flow_Id->ran_param_val.flag_false != NULL);
    QoS_Flow_Id->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
    
    char QFI_str[] = "10";
    byte_array_t QFI = cp_str_to_ba(QFI_str);
    QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.len = QFI.len;
    QoS_Flow_Id->ran_param_val.flag_false->octet_str_ran.buf = QFI.buf;
}

static void gen_List_of_Secondary_cells_to_be_setup(seq_ran_param_t* List_num_2ndCells) {
    List_num_2ndCells->ran_param_id = LIST_OF_SECONDARY_CELLS_TO_BE_SETUP_8_4_4_1;
    List_num_2ndCells->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
    List_num_2ndCells->ran_param_val.lst = calloc(1, sizeof(ran_param_list_t));
    assert(List_num_2ndCells->ran_param_val.lst != NULL);
    List_num_2ndCells->ran_param_val.lst->sz_lst_ran_param = 1;
    List_num_2ndCells->ran_param_val.lst->lst_ran_param = calloc(1, sizeof(lst_ran_param_t));
    assert(List_num_2ndCells->ran_param_val.lst->lst_ran_param != NULL);
    
    lst_ran_param_t* secCell_item = (lst_ran_param_t*)&List_num_2ndCells->ran_param_val.strct->ran_param_struct[0];
    secCell_item->ran_param_struct.sz_ran_param_struct = 1;
    secCell_item->ran_param_struct.ran_param_struct = calloc(1, sizeof(seq_ran_param_t));
    assert(secCell_item->ran_param_struct.ran_param_struct != NULL);
    
    seq_ran_param_t* secCell_Id = &secCell_item->ran_param_struct.ran_param_struct[0];
    secCell_Id->ran_param_id = SECONDARY_CELL_ID_8_4_4_1;
    secCell_Id->ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
    secCell_Id->ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(secCell_Id->ran_param_val.flag_false != NULL);
    secCell_Id->ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
    
    char cellID_str[] = "0";
    byte_array_t QFI = cp_str_to_ba(cellID_str);
    secCell_Id->ran_param_val.flag_false->octet_str_ran.len = QFI.len;
    secCell_Id->ran_param_val.flag_false->octet_str_ran.buf = QFI.buf;
}

static e2sm_rc_ctrl_msg_frmt_1_t gen_rc_ctrl_msg_frmt_1_Handover_Control(char targetcell) {
    e2sm_rc_ctrl_msg_frmt_1_t dst = {0};
    
    dst.sz_ran_param = 4;
    dst.ran_param = calloc(4, sizeof(seq_ran_param_t));
    assert(dst.ran_param != NULL);
    
    gen_Target_Primary_Cell_ID(&dst.ran_param[0], targetcell);
    gen_List_of_PDU_sessions_for_handover(&dst.ran_param[1]);
    gen_List_of_DRBs_for_handover(&dst.ran_param[2]);
    gen_List_of_Secondary_cells_to_be_setup(&dst.ran_param[3]);
    
    return dst;
}

static e2sm_rc_ctrl_msg_t gen_handover_rc_ctrl_msg(e2sm_rc_ctrl_msg_e msg_frmt, uint8_t targetcell) {
    e2sm_rc_ctrl_msg_t dst = {0};
    
    if (msg_frmt == FORMAT_1_E2SM_RC_CTRL_MSG) {
        dst.format = msg_frmt;
        dst.frmt_1 = gen_rc_ctrl_msg_frmt_1_Handover_Control(targetcell);
    } else {
        assert(0 && "Unsupported control message format");
    }
    
    return dst;
}

static ue_id_e2sm_t gen_rc_ue_id(ue_id_e2sm_e type, int ueid) {
    ue_id_e2sm_t ue_id = {0};
    
    if (type == GNB_UE_ID_E2SM) {
        ue_id.type = GNB_UE_ID_E2SM;
        ue_id.gnb.ran_ue_id = (uint64_t*)malloc(sizeof(uint64_t));
        assert(ue_id.gnb.ran_ue_id != NULL);
        *(ue_id.gnb.ran_ue_id) = ueid;
    } else {
        assert(0 && "Unsupported UE ID type");
    }
    
    return ue_id;
}

// ============================================
// HANDOVER EXECUTION
// ============================================

bool execute_handover(e2_node_arr_xapp_t const* nodes, uint16_t ueID, 
                     uint16_t source_cell, uint16_t target_cell) {
    
    char target_cell_char = '0' + target_cell;
    
    if (!(target_cell_char > '0' && target_cell_char <= '9')) {
        printf("[HandoverExec] ERROR: Invalid target cell %d\n", target_cell);
        return false;
    }
    
    // Get UE context
    struct SINRServingValues* ue = get_UE(source_cell, ueID);
    if (ue == NULL) {
        printf("[HandoverExec] ERROR: Cannot find UE %d in cell %d\n", ueID, source_cell);
        return false;
    }

    // ── Anti-Ping-Pong hard guard ─────────────────────────────────────────
    // Block returning to the previous cell within ANTI_PP_SIM_SEC simulated seconds.
    double sim_now_pp = g_sim_time_sec;
    if (ue->ho_context.prev_cell != 0 &&
        target_cell == ue->ho_context.prev_cell &&
        ue->ho_context.last_handover_sim_sec > 0.0 &&
        (sim_now_pp - ue->ho_context.last_handover_sim_sec) < ANTI_PP_SIM_SEC) {
        printf("[AntiPP] ✗ UE %d: BLOCKED return to prev Cell %d (%.2f sim-s ago, guard=%.1fs)\n",
               ueID, target_cell,
               sim_now_pp - ue->ho_context.last_handover_sim_sec,
               ANTI_PP_SIM_SEC);
        return false;
    }

    // Prepare RC Control Request
    rc_ctrl_req_data_t rc_ctrl = {0};
    ue_id_e2sm_t ue_id_rc = gen_rc_ue_id(GNB_UE_ID_E2SM, ueID);
    
    rc_ctrl.hdr = gen_rc_ctrl_hdr(FORMAT_1_E2SM_RC_CTRL_HDR, ue_id_rc,
                                   CONNECTED_MODE_MOBILITY, HANDOVER_CONTROL_7_6_4_1);
    rc_ctrl.msg = gen_handover_rc_ctrl_msg(FORMAT_1_E2SM_RC_CTRL_MSG, target_cell_char);
    
    // Update state BEFORE sending
    ue->ho_context.state = HO_STATE_COMMAND_SENT;
    ue->ho_context.source_cell = source_cell;
    ue->ho_context.target_cell = target_cell;
    ue->ho_context.command_time = time(NULL);
    
    printf("\n[HandoverExec] ═══════════════════════════════════════════════\n");
    printf("[HandoverExec] HANDOVER COMMAND\n");
    printf("[HandoverExec] UE %d: Cell %d → Cell %d\n", ueID, source_cell, target_cell);
    printf("[HandoverExec] State: %s\n", ho_state_to_string(ue->ho_context.state));
    printf("[HandoverExec] Total Handovers: %d\n", ue->ho_context.total_handovers);
    printf("[HandoverExec] ═══════════════════════════════════════════════\n");
    
    // Send control message — targeted to the source cell's E2 node only
    bool handover_sent = false;
    int8_t node_idx = g_cell_to_node_idx[source_cell];
    if (node_idx >= 0 && node_idx < (int8_t)nodes->len) {
        printf("[HandoverExec] Sending CONTROL to node %d (cell %d→%d)\n",
               (int)node_idx, source_cell, target_cell);
        sm_ans_xapp_t ans = control_sm_xapp_api(&nodes->n[node_idx].id, SM_RC_ID, &rc_ctrl);
        if (ans.success) {
            handover_sent = true;
            ue->ho_context.state = HO_STATE_WAITING_COMPLETION;
            printf("[HandoverExec] ✓ Command sent to node %d\n", (int)node_idx);
        } else {
            printf("[HandoverExec] ✗ Failed to send to node %d\n", (int)node_idx);
        }
    } else {
        // Fallback: mapping not yet learned — try nodes one at a time, stop at first success
        printf("[HandoverExec] WARN: no mapping for cell %d, trying nodes sequentially\n", source_cell);
        for (size_t i = 0; i < nodes->len && !handover_sent; ++i) {
            sm_ans_xapp_t ans = control_sm_xapp_api(&nodes->n[i].id, SM_RC_ID, &rc_ctrl);
            if (ans.success) {
                handover_sent = true;
                ue->ho_context.state = HO_STATE_WAITING_COMPLETION;
                // Learn the mapping now so future handovers don't need fallback
                if (source_cell < MAX_REGISTERED_CELLS)
                    g_cell_to_node_idx[source_cell] = (int8_t)i;
                printf("[HandoverExec] ✓ Command sent to node %zu (fallback, mapping learned)\n", i);
            }
        }
    }
    
    free_rc_ctrl_req_data(&rc_ctrl);
    
    if (handover_sent) {
        // ── Anti-PP: record source cell and start sim-time timer NOW, on source UE.
        // Also update the target cell entry immediately if it already exists (dual-conn
        // scenarios register UEs in both cells before the handover, bypassing the
        // confirm_handover_completion path).
        time_t now_ho = time(NULL);
        double sim_ho = g_sim_time_sec;
        ue->ho_context.prev_cell             = source_cell;
        ue->ho_context.last_handover_time    = now_ho;
        ue->ho_context.last_handover_sim_sec = sim_ho;

        struct SINRServingValues* tgt_ue = get_UE(target_cell, ueID);
        if (tgt_ue != NULL) {
            tgt_ue->ho_context.prev_cell             = source_cell;
            tgt_ue->ho_context.last_handover_time    = now_ho;
            tgt_ue->ho_context.last_handover_sim_sec = sim_ho;
            tgt_ue->ho_context.state                 = HO_STATE_COOLDOWN;
            printf("[HandoverExec] AntiPP: pre-set target UE %d in Cell %d "
                   "(prev_cell=%d, sim_t=%.2fs)\n", ueID, target_cell, source_cell, sim_ho);
        }

        printf("[HandoverExec] ⏳ Status: SENT - waiting for NS-3 confirmation via KPM\n");
        printf("[HandoverExec] State: %s (will confirm when UE appears in target cell)\n\n",
               ho_state_to_string(ue->ho_context.state));
        return true;
    } else {
        printf("[HandoverExec] ❌ Status: FAILED - command not sent\n\n");
        update_handover_state_on_failure(ue);
        return false;
    }
}

// ============================================
// MAIN DECISION LOOP
// ============================================

void process_handover_decisions(e2_node_arr_xapp_t const* nodes) {
    g_cycle_count++;
    update_sim_time_from_csv();   // Refresh actual sim-time from ns-3 CSV

    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║         SINR-BASED HANDOVER DECISION CYCLE                ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("[SimTime] sim_time=%.3f s  (cycle #%lu)\n", g_sim_time_sec, g_cycle_count);

    int handovers_triggered = 0;
    int ues_evaluated = 0;
    
    // Iterate through all registered cells
    for (int cell_idx = 0; cell_idx < MAX_REGISTERED_CELLS; cell_idx++) {
        if (!cells_sinr_map[cell_idx].is_registered) {
            continue;
        }
        
        struct SINR_Map* cell = cells_sinr_map[cell_idx].sinrMap;
        if (cell == NULL || !cell->is_active) {
            continue;
        }
        
        printf("\n[Decision] Processing Cell %d (%zu UEs connected)\n", 
               cell->cellID, cell->numOfConnectedUEs);
        
        // Evaluate each UE in this cell
        for (int ue_idx = 0; ue_idx < MAX_REGISTERED_UES; ue_idx++) {
            struct SINRServingValues* ue = &cell->connectedUEs[ue_idx];
            
            if (!ue->is_available) {
                continue;
            }
            
            ues_evaluated++;
            
            printf("\n[Decision] --- UE %d @ Cell %d ---\n", ue->ueID, cell->cellID);
            printf("[Decision] State: %s\n", ho_state_to_string(ue->ho_context.state));
            printf("[Decision] Serving SINR: %.2f dB\n", ue->sinr);
            
            // Check if UE can attempt handover
            if (!can_attempt_handover(ue)) {
                printf("[Decision] Cannot attempt handover now\n");
                continue;
            }
            
            // Evaluate A3 event
            a3_evaluation_result_t a3_result = evaluate_a3_event(ue, cell->cellID);
            
            if (a3_result.a3_triggered) {
                printf("\n[Decision] ✓ A3 EVENT TRIGGERED for UE %d\n", ue->ueID);
                printf("[Decision] Best Target: Cell %d (SINR: %.2f dB, Gain: %.2f dB)\n",
                       a3_result.best_target_cell, a3_result.best_target_sinr, a3_result.sinr_gain);

                // ============================================
                // RL MODEL CONSULTATION (Ping-Pong Avoidance)
                // ============================================
                bool execute_ho = true;
                uint16_t final_target = a3_result.best_target_cell;
                rl_prediction_result_t rl_result = {0};

                if (g_rl_available) {
                    rl_ue_measurements_t meas = {
                        .ue_id            = ue->ueID,
                        .serving_cell_id  = cell->cellID,
                        .serving_sinr     = ue->sinr,
                        .serving_rsrp     = (ue->rsrp != 0.0) ? ue->rsrp : (ue->sinr - 10.0),
                        .serving_qual     = ue->sinr - 5.0,
                        .neighbor_cell_id = a3_result.best_target_cell,
                        .neighbor_sinr    = a3_result.best_target_sinr,
                        .neighbor_rsrp    = a3_result.best_target_sinr - 10.0,
                        .cqi              = (ue->cqi > 0) ? ue->cqi : 10.0,
                        .speed            = (ue->speed > 0) ? ue->speed : 1.4,
                        .dl_bitrate       = ue->dl_bitrate,
                        .ul_bitrate       = ue->ul_bitrate,
                        .bandwidth        = 20
                    };

                    printf("[RL] Querying prediction service for UE %d...\n", ue->ueID);

                    pthread_mutex_unlock(&mtx);
                    int pred_rc = rl_predict_handover(&meas, &rl_result);
                    pthread_mutex_lock(&mtx);
                    if (pred_rc == 0 && rl_result.prediction_success) {

                        printf("[RL] Decision: %s | Confidence: %.2f | ToS: %.2fs\n",
                               rl_result.should_handover ? "EXECUTE" : "AVOID",
                               rl_result.confidence,
                               rl_result.time_to_handover);

                        if (!rl_result.should_handover) {
                            /* Paper Algorithm 1+2: service already applied:
                             *   Alg-1 (Detection): is_pp = (ToS<1.2s AND signal_osc) OR class==2
                             *   Alg-2 (Avoidance): is_unnec = safe AND maway AND ToS<1.35s
                             * If service says AVOID, honour it unconditionally.            */
                            execute_ho = false;
                            printf("[RL] BLOCKED: Handover to Cell %d suppressed "
                                   "(ToS=%.2fs, confidence=%.2f)\n",
                                   a3_result.best_target_cell,
                                   rl_result.time_to_handover,
                                   rl_result.confidence);
                        } else if (rl_result.should_handover &&
                                   rl_result.target_cell != 0 &&
                                   rl_result.target_cell < MAX_REGISTERED_CELLS &&
                                   rl_result.target_cell != a3_result.best_target_cell &&
                                   cells_sinr_map[rl_result.target_cell].is_registered) {
                            // RL recommends a different (better) target cell
                            printf("[RL] Redirecting: SINR pick=Cell %d -> RL pick=Cell %d\n",
                                   a3_result.best_target_cell, rl_result.target_cell);
                            final_target = rl_result.target_cell;
                        }
                    } else {
                        printf("[RL] Prediction failed -- falling back to SINR decision\n");
                    }
                }

                // CSV decision logging
                g_total_a3_triggers++;
                if (g_decision_log) {
                    fprintf(g_decision_log, "%lu,%ld,%d,%d,%.2f,%.2f,%d,%.2f,%s,%.3f,%.3f,0,0,%s\n",
                        g_cycle_count, (long)time(NULL),
                        ue->ueID, cell->cellID, ue->sinr, ue->rsrp,
                        a3_result.best_target_cell, a3_result.best_target_sinr,
                        rl_result.prediction_success ? (rl_result.should_handover ? "EXECUTE" : "AVOID") : "NO_PRED",
                        rl_result.confidence, rl_result.time_to_handover,
                        execute_ho ? "EXECUTED" : "BLOCKED");
                    fflush(g_decision_log);
                }
                if (!execute_ho) g_total_gru_blocked++;
                else g_total_executed++;

                if (execute_ho) {
                    bool success = execute_handover(nodes, ue->ueID, cell->cellID, final_target);
                    if (success) {
                        handovers_triggered++;
                    }
                }
            } else {
                printf("[Decision] ✗ No A3 event for UE %d\n", ue->ueID);
            }
        }
    }
    
    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  CYCLE SUMMARY: %d UEs evaluated, %d handovers triggered   ║\n", 
           ues_evaluated, handovers_triggered);
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
}

// ============================================
// MAIN FUNCTION
// ============================================

int main(int argc, char* argv[]) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                              ║\n");
    printf("║       RL-ENHANCED HANDOVER xAPP - INTEGRATED VERSION       ║\n");
    printf("║       SINR A3 + RL Ping-Pong Avoidance (Fares Model)       ║\n");
    printf("║                                                              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    // Initialize cell-to-node mapping to "unknown" (-1)
    memset(g_cell_to_node_idx, -1, sizeof(g_cell_to_node_idx));

    // Initialize RL prediction service
    printf("[Init] Connecting to RL prediction service (Fares model)...\n");
    if (rl_predictor_init() == 0 && rl_service_available()) {
        g_rl_available = true;
        printf("[Init] ✅ RL prediction service connected — ping-pong avoidance ACTIVE\n");
    } else {
        printf("[Init] ⚠️  RL service unavailable — running in SINR-only mode\n");
        printf("[Init]     Start service: python3 omar_salama/rl_xapp.py\n");
    }

    // Initialize xApp API
    fr_args_t args = init_fr_args(argc, argv);
    init_xapp_api(&args);
    sleep(1);
    
    // Get E2 nodes
    e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
    defer({ free_e2_node_arr_xapp(&nodes); });
    
    if (nodes.len == 0) {
        printf("[ERROR] No E2 nodes connected\n");
        return 1;
    }
    
    printf("[Init] Connected E2 nodes: %d\n", nodes.len);
    
    // Initialize mutex
    pthread_mutexattr_t attr = {0};
    int rc = pthread_mutex_init(&mtx, &attr);
    assert(rc == 0);
    
    // Start KPM subscription
    sm_ans_xapp_t* hndl = calloc(nodes.len, sizeof(sm_ans_xapp_t));
    assert(hndl != NULL);
    
    int const KPM_ran_function = 2;
    
    for (size_t i = 0; i < nodes.len; ++i) {
        e2_node_connected_xapp_t* n = &nodes.n[i];
        size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_ran_function);
        
        if (n->rf[idx].defn.kpm.ric_report_style_list != NULL) {
            kpm_sub_data_t kpm_sub = gen_kpm_subs(&n->rf[idx].defn.kpm);
            sm_cb node_cb = (i < MAX_KPM_NODE_CBS) ? g_kpm_node_cbs[i] : sm_cb_kpm;
            hndl[i] = report_sm_xapp_api(&n->id, KPM_ran_function, &kpm_sub, node_cb);
            
            if (hndl[i].success) {
                printf("[Init] ✓ KPM subscription successful for node %zu\n", i);
            } else {
                printf("[Init] ✗ KPM subscription failed for node %zu\n", i);
            }
            
            free_kpm_sub_data(&kpm_sub);
        }
    }
    
    // Open decision log
    {
        char log_path[256];
        const char* home = getenv("HOME");
        snprintf(log_path, sizeof(log_path), "%s/gru_decisions.csv", home ? home : ".");
        g_decision_log = fopen(log_path, "w");
        if (g_decision_log) {
            fprintf(g_decision_log,
                "cycle,timestamp,ue_id,serving_cell,serving_sinr,serving_rsrp,"
                "target_cell,target_sinr,gru_decision,gru_confidence,predicted_tos,"
                "is_ping_pong,is_unnecessary,final_action\n");
            fflush(g_decision_log);
            printf("[Init] Decision log: %s\n", log_path);
        }
    }

    // Wait for initial measurements
    printf("\n[Init] Waiting for initial KPM measurements (%d seconds)...\n",
           MEASUREMENT_UPDATE_INTERVAL);
    sleep(MEASUREMENT_UPDATE_INTERVAL);
    
    printf("\n[Init] Starting handover decision loop...\n");
    printf("[Init] Configuration:\n");
    printf("  - A3 Offset: %.1f dB\n", A3_OFFSET);
    printf("  - A3 Hysteresis: %.1f dB\n", A3_HYSTERESIS);
    printf("  - Handover Cooldown: %.1f sim-seconds\n", HANDOVER_COOLDOWN_SIM_SEC);
    printf("  - Measurement Samples: %d\n", MAX_NUM_OF_RIC_INDICATIONS);
    printf("  - Decision Interval: %d seconds\n", MEASUREMENT_UPDATE_INTERVAL);
    printf("  - RL Model: %s\n", g_rl_available ? "ACTIVE (ping-pong avoidance ON)" : "OFFLINE (SINR-only fallback)");
    printf("  - RL Confidence Threshold: 0.60\n\n");
    
    // Main decision loop
    while (1) {
        pthread_mutex_lock(&mtx);
        
        process_handover_decisions(&nodes);
        
        pthread_mutex_unlock(&mtx);
        
        // Wait before next cycle
        sleep(MEASUREMENT_UPDATE_INTERVAL);
    }
    
    // Cleanup (unreachable in infinite loop, but good practice)
    for (int i = 0; i < nodes.len; ++i) {
        if (hndl[i].success == true) {
            rm_report_sm_xapp_api(hndl[i].u.handle);
        }
    }
    free(hndl);
    
    while (try_stop_xapp_api() == false) {
        usleep(1000);
    }

    // Cleanup RL prediction service
    if (g_rl_available) {
        rl_predictor_cleanup();
    }

    rc = pthread_mutex_destroy(&mtx);
    assert(rc == 0);

    printf("\n[Exit] xApp completed successfully\n");

    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║              RL SESSION SUMMARY                    ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Total A3 triggers    : %5lu                        ║\n", g_total_a3_triggers);
    printf("║  Handovers EXECUTED   : %5lu                        ║\n", g_total_executed);
    printf("║  Handovers BLOCKED    : %5lu (RL avoidance)        ║\n", g_total_gru_blocked);
    if (g_total_a3_triggers > 0) {
        printf("║  Block rate           : %4.1f%%                       ║\n",
               100.0 * g_total_gru_blocked / g_total_a3_triggers);
    }
    printf("╚══════════════════════════════════════════════════════╝\n");
    if (g_decision_log) { fclose(g_decision_log); g_decision_log = NULL; }

    return 0;
}
