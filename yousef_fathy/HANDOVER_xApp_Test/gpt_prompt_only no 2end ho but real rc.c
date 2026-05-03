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
 *  Mina Yonan <m.yonan@aucegypt.edu>
 *  Mostafa Ashraf <mostafa.ashraf.ext@orange.com>
 *  Abdelrhman Soliman <abdelrhman.soliman.ext@orange.com>
 *  Aya Kamal <aya.kamal.ext@orange.com>
 *
 * Modified: Smart SINR-Based Continuous Handover xApp
 * - Fixed handover state machine (proper IDLE transition after completion)
 * - Fixed UE counter duplication issue
 * - Added handover completion detection mechanism
 */

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_struct.h"
#include "../../../../src/sm/rc_sm/ie/ir/ran_param_list.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/sm/rc_sm/rc_sm_id.h"
#include "../../../../src/sm/rc_sm/ie/rc_data_ie.h"
#include "../../../../src/util/e.h"

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
  
static ue_id_e2sm_t ue_id;

static uint64_t const period_ms = 100;

static pthread_mutex_t mtx;

static void log_gnb_ue_id(ue_id_e2sm_t ue_id) 
{
  if (ue_id.gnb.gnb_cu_ue_f1ap_lst != NULL) 
  {
    for (size_t i = 0; i < ue_id.gnb.gnb_cu_ue_f1ap_lst_len; i++) 
    {
      printf("UE ID type = gNB-CU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb.gnb_cu_ue_f1ap_lst[i]);
    }
  } else 
  {
    printf("UE ID type = gNB, amf_ue_ngap_id = %lu\n", ue_id.gnb.amf_ue_ngap_id);
  }
  if (ue_id.gnb.ran_ue_id != NULL) 
  {
    printf("ran_ue_id = %lx\n", *ue_id.gnb.ran_ue_id);
  }
}

static void log_du_ue_id(ue_id_e2sm_t ue_id) 
{
  printf("UE ID type = gNB-DU, gnb_cu_ue_f1ap = %u\n", ue_id.gnb_du.gnb_cu_ue_f1ap);
  if (ue_id.gnb_du.ran_ue_id != NULL) 
  {
    printf("ran_ue_id = %lx\n", *ue_id.gnb_du.ran_ue_id);
  }
}

static void log_cuup_ue_id(ue_id_e2sm_t ue_id) 
{
  printf("UE ID type = gNB-CU-UP, gnb_cu_cp_ue_e1ap = %u\n", ue_id.gnb_cu_up.gnb_cu_cp_ue_e1ap);
  if (ue_id.gnb_cu_up.ran_ue_id != NULL) 
  {
    printf("ran_ue_id = %lx\n", *ue_id.gnb_cu_up.ran_ue_id);
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
    printf("RRU.PrbTotDl = %d [PRBs]\n", meas_record.int_val);
    printf("RRU.PrbTotUl = %d [PRBs]\n", meas_record.int_val);
  } else if (cmp_str_ba("DRB.PdcpSduVolumeDL", name) == 0) {
    printf("DRB.PdcpSduVolumeDL = %d [kb]\n", meas_record.int_val);
  } else if (cmp_str_ba("DRB.PdcpSduVolumeUL", name) == 0) {
    printf("DRB.PdcpSduVolumeUL = %d [kb]\n", meas_record.int_val);
  }
}

static void log_real_value(byte_array_t name, meas_record_lst_t meas_record) 
{
  if (cmp_str_ba("DRB.RlcSduDelayDl", name) == 0) {
    printf("DRB.RlcSduDelayDl = %.2f [μs]\n", meas_record.real_val);
  } else if (cmp_str_ba("DRB.UEThpDl", name) == 0) {
    printf("DRB.UEThpDl = %.2f [kbps]\n", meas_record.real_val);
  } else if (cmp_str_ba("DRB.UEThpUl", name) == 0) {
    printf("DRB.UEThpUl = %.2f [kbps]\n", meas_record.real_val);
  } else if (strncmp(name.buf, "L3servingSINR3gpp_cell_", strlen("L3servingSINR3gpp_cell_")) == 0) {
    printf("%s, sinr= %.4f [db]\n", name.buf, meas_record.real_val);
  } else if (strncmp(name.buf, "L3neighSINRListOf_UEID_", strlen("L3neighSINRListOf_UEID_")) == 0) {
    printf("%s, sinr= %.4f [db]\n", name.buf, meas_record.real_val);
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
#define MAX_REGISTERED_UES 20
#define MAX_REGISTERED_NEIGHBOURS 20

struct registeredCells cells_sinr_map[MAX_REGISTERED_CELLS] = {{false, NULL}};

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
  
  // Initialize handover context for new UEs only
  if (is_new_ue) {
    cell->connectedUEs[ueID].neighCells = NULL;
    cell->connectedUEs[ueID].numOfNeighCells = 0;
    cell->connectedUEs[ueID].ho_context.state = HO_STATE_IDLE;
    cell->connectedUEs[ueID].ho_context.attempts = 0;
    cell->connectedUEs[ueID].ho_context.last_handover_time = 0;
    cell->connectedUEs[ueID].ho_context.source_cell = 0;
    cell->connectedUEs[ueID].ho_context.target_cell = 0;
    
    // CRITICAL FIX: Only increment counter for NEW UEs
    cell->numOfConnectedUEs++;
    
    printf("[UE Tracking] NEW UE %d added to cell %d (Total: %zu)\n", 
           ueID, cell->cellID, cell->numOfConnectedUEs);
  } else {
    // Just update SINR for existing UE
    printf("[UE Tracking] UE %d SINR updated in cell %d: %.2f dB\n", 
           ueID, cell->cellID, sinr);
  }
}

struct SINRServingValues* get_UE(const uint16_t cellID, const uint16_t ueID) 
{
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
  
  printf("[UE Tracking] UE %d removed from cell %d (Remaining: %zu)\n", 
         ueID, cell->cellID, cell->numOfConnectedUEs);
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

uint8_t getTargetCellID(callback_data_t data)
{
    assert(data.neighCells != NULL);
    
    struct SINRServingValues* serving_ue = get_UE(data.frmCurntCell, data.ueID);
    if (serving_ue == NULL) {
        printf("[HO Decision] ERROR: Cannot find serving UE %d in cell %d\n",
               data.ueID, data.frmCurntCell);
        return 0;
    }
    
    double serving_sinr = serving_ue->sinr;
    double max_neighbor_sinr = serving_sinr;
    uint8_t targetCell = 0;
    
    printf("\n=== Professional SINR-Based Handover Decision ===\n");
    printf("UE %d | Serving Cell %d | Serving SINR: %.2f dB\n", 
           data.ueID, data.frmCurntCell, serving_sinr);
    printf("Handover Threshold: %.2f dB\n", HANDOVER_THRESHOLD_DB);
    
    for (int i = 0; i < MAX_REGISTERED_NEIGHBOURS; i++)
    {
        if (!data.neighCells[i].is_available)
            continue;
        
        double neighbor_sinr = data.neighCells[i].sinr;
        
        if (neighbor_sinr > (serving_sinr + HANDOVER_THRESHOLD_DB)) {
            double gain = neighbor_sinr - serving_sinr;
            printf("  → Cell %d: SINR %.2f dB | Gain: %.2f dB | CANDIDATE\n",
                   i, neighbor_sinr, gain);
            
            if (neighbor_sinr > max_neighbor_sinr) {
                max_neighbor_sinr = neighbor_sinr;
                targetCell = i;
            }
        } else {
            printf("  → Cell %d: SINR %.2f dB | Below threshold\n", i, neighbor_sinr);
        }
    }
    
    if (targetCell != 0 && targetCell != data.frmCurntCell) {
        double final_gain = max_neighbor_sinr - serving_sinr;
        printf("  ✓ DECISION: Handover to Cell %d (SINR: %.2f dB, Gain: %.2f dB)\n",
               targetCell, max_neighbor_sinr, final_gain);
    } else {
        printf("  ✗ DECISION: No handover (no suitable neighbor found)\n");
    }
    
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

static void log_kpm_measurements(kpm_ind_msg_format_1_t const* msg_frm_1) 
{
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

  if (msg_frm_1->meas_info_lst_len != msg_frm_1->meas_data_lst_len) 
  {
    printf("Error: meas_info_lst_len= (%ld) not equal meas_data_lst_len= (%ld)\n", 
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

          printf("Serving Cell %d - UE %d: %.2f dB\n", info.cellID, info.ueID, sinr);

        } else if (isMeasNameContains(meas_type.name.buf, "L3neighSINRListOf_UEID_")) 
        {
          struct InfoObj info = parseNeighMsg(meas_type.name.buf);

          meas_record_lst_t const sinr = record_item;
          meas_record_lst_t const NeighbourID = data_item.meas_record_lst[j + 1];

          struct SINRServingValues* UE = get_UE(info.cellID, info.ueID);
          assert(UE != NULL);

          add_neighCell(UE, NeighbourID.int_val, sinr.real_val);
          j += 2;
          continue;
        }
      }
      j++;
    }
  }
}

static void sm_cb_kpm(sm_ag_if_rd_t const* rd) 
{
  assert(rd != NULL);
  assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
  assert(rd->ind.type == KPM_STATS_V3_0);
  
  kpm_ind_data_t const* ind = &rd->ind.kpm.ind;
  kpm_ric_ind_hdr_format_1_t const* hdr_frm_1 = &ind->hdr.kpm_ric_ind_hdr_format_1;
  kpm_ind_msg_format_3_t const* msg_frm_3 = &ind->msg.frm_3;
  
  static int counter = 1;
  
  {
    lock_guard(&mtx);
    
    if (msg_frm_3 == NULL || 
        msg_frm_3->ue_meas_report_lst_len == 0 || 
        msg_frm_3->ue_meas_report_lst_len > 1000 ||
        msg_frm_3->meas_report_per_ue == NULL) {
      fprintf(stderr, "[WARN] Invalid KPM Format 3 data (SKIPPING callback)\n");
      return;
    }
    
    for (size_t i = 0; i < msg_frm_3->ue_meas_report_lst_len; i++) 
    {
      ue_id_e2sm_t const ue_id_e2sm = msg_frm_3->meas_report_per_ue[i].ue_meas_report_lst;
      ue_id_e2sm_e const type = ue_id_e2sm.type;
      
      free_ue_id_e2sm(&ue_id);
      ue_id = cp_ue_id_e2sm(&ue_id_e2sm);
      
      log_kpm_measurements(&msg_frm_3->meas_report_per_ue[i].ind_msg_format_1);
    }
    counter++;
  }
}

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
      
      printf("\n[HO Completion] ✓ UE %d handover COMPLETED: Cell %d → Cell %d\n",
             ueID, source_cell_id, target_cell->cellID);
      
      // Copy handover context to target cell
      target_cell->connectedUEs[ueID].ho_context = source_cell->connectedUEs[ueID].ho_context;
      
      // Mark as completed and reset to IDLE for future handovers
      target_cell->connectedUEs[ueID].ho_context.state = HO_STATE_IDLE;
      target_cell->connectedUEs[ueID].ho_context.completion_time = time(NULL);
      target_cell->connectedUEs[ueID].ho_context.last_handover_time = time(NULL);
      target_cell->connectedUEs[ueID].ho_context.attempts = 0;
      
      // Remove from source cell
      remove_UE_from_cell(source_cell, ueID);
    }
  }
}

void evaluateHandoverOpportunities(Callback targetCellFinding, Callback cbHOAction, callback_data_t data) 
{
  printf("\n=== Continuous SINR-Based Handover Evaluation ===\n");

  // FIRST PASS: Detect completed handovers by checking all cells
  for (int target_idx = 0; target_idx < MAX_REGISTERED_CELLS; target_idx++) 
  {
    if (!cells_sinr_map[target_idx].is_registered || 
        cells_sinr_map[target_idx].sinrMap == NULL)
      continue;
    
    struct SINR_Map* target_cell = cells_sinr_map[target_idx].sinrMap;
    
    if (target_cell->connectedUEs == NULL)
      continue;
    
    // Check each UE in this potential target cell
    for (int ue_idx = 0; ue_idx < MAX_REGISTERED_UES; ue_idx++) 
    {
      if (!target_cell->connectedUEs[ue_idx].is_available)
        continue;
      
      uint16_t ueID = target_cell->connectedUEs[ue_idx].ueID;
      
      // Check all possible source cells for this UE
      for (int source_idx = 0; source_idx < MAX_REGISTERED_CELLS; source_idx++) 
      {
        if (source_idx == target_idx || 
            !cells_sinr_map[source_idx].is_registered)
          continue;
        
        checkHandoverCompletion(target_cell, ueID, source_idx);
      }
    }
  }

  // SECOND PASS: Evaluate new handover opportunities
  for (int i = 0; i < MAX_REGISTERED_CELLS; i++) 
  {
    if (cells_sinr_map[i].sinrMap == NULL || !cells_sinr_map[i].is_registered) 
      continue;
      
    struct SINR_Map* cell = cells_sinr_map[i].sinrMap;
    
    printf("\n--- Evaluating Cell %d (Connected UEs: %zu) ---\n", 
           cell->cellID, cell->numOfConnectedUEs);

    for (int j = 0; j < MAX_REGISTERED_UES; j++) 
    {
      if (!cell->connectedUEs[j].is_available || 
          cell->connectedUEs[j].neighCells == NULL)
        continue;

      struct SINRServingValues* ue = &cell->connectedUEs[j];
      
      // Skip if handover in progress
      if (ue->ho_context.state == HO_STATE_IN_PROGRESS) {
        time_t now = time(NULL);
        double elapsed = difftime(now, ue->ho_context.command_time);
        
        // Auto-reset if stuck too long
        if (elapsed > HO_COMPLETION_TIMEOUT_SEC) {
          printf("  UE %d: IN_PROGRESS timeout (%.1fs) - resetting to IDLE\n",
                 ue->ueID, elapsed);
          ue->ho_context.state = HO_STATE_IDLE;
          ue->ho_context.attempts = 0;
        } else {
          printf("  UE %d: State %s - waiting (%.1fs)\n", 
                 ue->ueID, ho_state_to_string(ue->ho_context.state), elapsed);
          continue;
        }
      }
      
      if (ue->ho_context.state != HO_STATE_IDLE) {
        printf("  UE %d: State %s - skipping\n", 
               ue->ueID, ho_state_to_string(ue->ho_context.state));
        continue;
      }

      // Anti ping-pong protection
      time_t current_time = time(NULL);
      if (ue->ho_context.last_handover_time > 0) {
        double time_since_last_ho = difftime(current_time, ue->ho_context.last_handover_time);
        if (time_since_last_ho < MIN_HO_INTERVAL_SEC) {
          printf("  UE %d: Anti ping-pong - %.1fs since last HO (min: %ds)\n",
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
          ue->ho_context.last_handover_time = current_time;
          printf("  ✓ Handover initiated for UE %d → Cell %d\n", 
                 ue->ueID, target);
        } else {
          printf("  ✗ Handover failed for UE %d\n", ue->ueID);
        }
      }
    }
  }
}

uint16_t doHandoverAction(callback_data_t data)
{
    char trgtCell = '0' + data.toTargetCell;
    
    if (!(trgtCell > '0' && trgtCell <= '9')) {
        printf("[xApp] Invalid target cell %c\n", trgtCell);
        return 0;
    }
    
    struct SINRServingValues* ue = get_UE(data.frmCurntCell, data.ueID);
    if (ue == NULL) {
        printf("[xApp] Cannot find UE %d in cell %d\n",
               data.ueID, data.frmCurntCell);
        return 0;
    }
    
    // Should not happen due to evaluation logic, but double-check
    if (ue->ho_context.state == HO_STATE_IN_PROGRESS) {
        time_t now = time(NULL);
        double elapsed = difftime(now, ue->ho_context.command_time);
        
        if (elapsed < MAX_HANDOVER_TIMEOUT_SEC) {
            return 0;
        }
    }
    
    if (ue->ho_context.attempts >= MAX_HANDOVER_ATTEMPTS) {
        ue->ho_context.state = HO_STATE_IDLE;
        ue->ho_context.attempts = 0;
        return 0;
    }
    
    rc_ctrl_req_data_t rc_ctrl = {0};
    ue_id_e2sm_t ue_id_1 = gen_rc_ue_id(GNB_UE_ID_E2SM, data.ueID);
    
    rc_ctrl.hdr = gen_rc_ctrl_hdr(FORMAT_1_E2SM_RC_CTRL_HDR, ue_id_1,
                                   CONNECTED_MODE_MOBILITY, HANDOVER_CONTROL_7_6_4_1);
    rc_ctrl.msg = gen_handover_rc_ctrl_msg(FORMAT_1_E2SM_RC_CTRL_MSG, trgtCell);
    
    ue->ho_context.state = HO_STATE_COMMAND_SENT;
    ue->ho_context.source_cell = data.frmCurntCell;
    ue->ho_context.target_cell = data.toTargetCell;
    ue->ho_context.command_time = time(NULL);
    ue->ho_context.attempts++;
    
    printf("\n[xApp] ═══════════════════════════════════════\n");
    printf("[xApp] HANDOVER COMMAND #%d\n", ue->ho_context.attempts);
    printf("[xApp] UE %d: Cell %d → Cell %d\n",
           data.ueID, data.frmCurntCell, data.toTargetCell);
    printf("[xApp] State: %s\n", ho_state_to_string(ue->ho_context.state));
    printf("[xApp] ═══════════════════════════════════════\n");
    
    bool handover_sent = false;
    for (size_t i = 0; i < (*data.nodes).len; ++i) {
        sm_ans_xapp_t ans = control_sm_xapp_api(&(*data.nodes).n[i].id, SM_RC_ID, &rc_ctrl);
        
        if (ans.success) {
            handover_sent = true;
            ue->ho_context.state = HO_STATE_IN_PROGRESS;
            printf("[xApp] ✓ Command sent to node %zu\n", i);
        } else {
            printf("[xApp] ✗ Failed to send to node %zu\n", i);
        }
    }
    
    free_rc_ctrl_req_data(&rc_ctrl);
    
    if (handover_sent) {
        printf("[xApp] Status: SENT - waiting for completion...\n\n");
        return 1;
    } else {
        printf("[xApp] Status: FAILED - command not sent\n\n");
        ue->ho_context.state = HO_STATE_IDLE;
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
  printf("Connected E2 nodes = %d\n", nodes.len);

  pthread_mutexattr_t attr = {0};
  int rc = pthread_mutex_init(&mtx, &attr);
  assert(rc == 0);

  sm_ans_xapp_t* hndl = calloc(nodes.len, sizeof(sm_ans_xapp_t));
  assert(hndl != NULL);

  int const KPM_ran_function = 2;
  for (size_t i = 0; i < nodes.len; ++i) 
  {
    e2_node_connected_xapp_t* n = &nodes.n[i];
    size_t const idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_ran_function);

    if (n->rf[idx].defn.kpm.ric_report_style_list != NULL) 
    {
      kpm_sub_data_t kpm_sub = gen_kpm_subs(&n->rf[idx].defn.kpm);
      hndl[i] = report_sm_xapp_api(&n->id, KPM_ran_function, &kpm_sub, sm_cb_kpm);
      assert(hndl[i].success == true);
      free_kpm_sub_data(&kpm_sub);
    }
  }

  printf("\n╔══════════════════════════════════════════════════════════╗\n");
  printf("║   Smart SINR-Based Continuous Handover xApp Started     ║\n");
  printf("╠══════════════════════════════════════════════════════════╣\n");
  printf("║  Handover Threshold: %.1f dB                             ║\n", HANDOVER_THRESHOLD_DB);
  printf("║  Anti Ping-Pong Interval: %d seconds                    ║\n", MIN_HO_INTERVAL_SEC);
  printf("║  HO Completion Timeout: %d seconds                      ║\n", HO_COMPLETION_TIMEOUT_SEC);
  printf("║  Decision Mode: Immediate (no averaging)                ║\n");
  printf("╚══════════════════════════════════════════════════════════╝\n\n");

  printf("Waiting for initial KPM measurements...\n");
  sleep(3);

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

  printf("xApp completed successfully\n");
  return 0;
}
