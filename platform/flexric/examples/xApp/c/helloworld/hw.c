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
 *
 * NOTE (ns-O-RAN specific):
 *   The ns-O-RAN KPM indication encodes ALL measurement values as REAL (double),
 *   even those declared as long/integer in the helper (e.g. RRU.PrbUsedDl).
 *   This is because getMesInfoItem() always calls:
 *       getMesDataItem(static_cast<double>(item.pmVal.choice.valueInt))
 *   So we handle EVERYTHING in the REAL_MEAS_VALUE branch.
 */

#include "../../../../src/xApp/e42_xapp_api.h"
#include "../../../../src/util/alg_ds/alg/defer.h"
#include "../../../../src/util/time_now_us.h"
#include "../../../../src/util/alg_ds/ds/lock_guard/lock_guard.h"
#include "../../../../src/util/e.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

/* =========================================================
 * KPMs we want to monitor — edit freely
 * =========================================================
 * These are sent to the E2 node in the subscription.
 * ns-O-RAN ignores the list and sends whatever it has,
 * but declaring them here documents intent clearly.
 */
static const char* WANTED_KPMS[] = {
  /* --- DU per-UE --- */
  "DRB.UEThpDl.UEID",               /* DL Throughput per UE [kbps]   */
  "RRU.PrbUsedDl.UEID",             /* Used PRBs per UE (DL)         */
  /* --- DU per-Cell --- */
  "RRU.PrbUsedDl",                  /* Total used PRBs per Cell (DL) */
  "DRB.MeanActiveUeDl",             /* Mean active UEs per Cell      */
  /* --- CU-CP L3/RRC --- */
  "HO.SrcCellQual.RS-SINR.UEID",   /* Serving Cell SINR             */
  "HO.SrcCellQual.RS-RSRP.UEID",   /* Serving Cell RSRP             */
  "HO.TrgtCellQual.RS-SINR.UEID",  /* Target/Neighbour SINR         */
  "HO.TrgtCellQual.RS-RSRP.UEID",  /* Target/Neighbour RSRP         */
  /* --- CU-UP --- */
  "DRB.PdcpSduVolumeDl_Filter.UEID",
};
static const size_t N_WANTED_KPMS = sizeof(WANTED_KPMS) / sizeof(WANTED_KPMS[0]);

/* =========================================================
 * Globals
 * ========================================================= */
static uint64_t const period_ms = 1000;
static pthread_mutex_t mtx;

/* =========================================================
 * UE-ID logging
 * ========================================================= */
static void log_gnb_ue_id(ue_id_e2sm_t uid)
{
  if (uid.gnb.gnb_cu_ue_f1ap_lst != NULL)
    for (size_t i = 0; i < uid.gnb.gnb_cu_ue_f1ap_lst_len; i++)
      printf("  UE [gNB-CU] gnb_cu_ue_f1ap = %u\n", uid.gnb.gnb_cu_ue_f1ap_lst[i]);
  else
    printf("  UE [gNB]    amf_ue_ngap_id  = %lu\n", uid.gnb.amf_ue_ngap_id);
  if (uid.gnb.ran_ue_id)
    printf("              ran_ue_id        = %lx\n", *uid.gnb.ran_ue_id);
}
static void log_du_ue_id(ue_id_e2sm_t uid)
{
  printf("  UE [gNB-DU] gnb_cu_ue_f1ap   = %u\n", uid.gnb_du.gnb_cu_ue_f1ap);
}
static void log_cuup_ue_id(ue_id_e2sm_t uid)
{
  printf("  UE [CU-UP]  gnb_cu_cp_ue_e1ap= %u\n", uid.gnb_cu_up.gnb_cu_cp_ue_e1ap);
}
typedef void (*log_ue_id_fn)(ue_id_e2sm_t);
static log_ue_id_fn log_ue_id_tbl[END_UE_ID_E2SM] = {
  log_gnb_ue_id, log_du_ue_id, log_cuup_ue_id, NULL, NULL, NULL, NULL,
};

/* =========================================================
 * Helper: buf prefix match (buf is NOT null-terminated)
 * ========================================================= */
static int starts_with(byte_array_t name, const char* prefix)
{
  size_t plen = strlen(prefix);
  return (name.len >= plen) && (memcmp(name.buf, prefix, plen) == 0);
}

/* =========================================================
 * Pretty-print one measurement.
 *
 * IMPORTANT: in ns-O-RAN ALL values arrive as REAL_MEAS_VALUE
 * because getMesInfoItem() casts long → double before encoding.
 * We therefore handle both INTEGER and REAL branches the same way
 * for the metrics we care about.
 * ========================================================= */
static void print_one_measurement(byte_array_t name, meas_record_lst_t rec)
{
  /* Grab the numeric value regardless of declared type */
  double val = (rec.value == REAL_MEAS_VALUE)    ? rec.real_val :
               (rec.value == INTEGER_MEAS_VALUE)  ? (double)rec.int_val : 0.0;

  /* ── DU per-UE ──────────────────────────────────────── */
  if (cmp_str_ba("DRB.UEThpDl.UEID", name) == 0) {
    printf("  [DL Thp/UE        ] DRB.UEThpDl.UEID              = %.2f [kbps]\n", val);

  } else if (cmp_str_ba("RRU.PrbUsedDl.UEID", name) == 0) {
    printf("  [Used PRBs/UE  DL ] RRU.PrbUsedDl.UEID             = %.0f [PRBs]\n", val);

  /* ── DU per-Cell ─────────────────────────────────────── */
  } else if (cmp_str_ba("RRU.PrbUsedDl", name) == 0) {
    printf("  [Total PRBs/Cell  ] RRU.PrbUsedDl                  = %.0f [PRBs]\n", val);

  } else if (cmp_str_ba("RRU.PrbTotDl", name) == 0) {
    printf("  [Avail PRBs/Cell  ] RRU.PrbTotDl                   = %.0f [PRBs]\n", val);

  } else if (cmp_str_ba("DRB.MeanActiveUeDl", name) == 0) {
    printf("  [Active UEs/Cell  ] DRB.MeanActiveUeDl              = %.0f\n", val);

  /* ── CU-UP ───────────────────────────────────────────── */
  } else if (cmp_str_ba("DRB.PdcpSduVolumeDl_Filter.UEID", name) == 0) {
    printf("  [PDCP DL Vol/UE   ] DRB.PdcpSduVolumeDl_Filter.UEID= %.0f [bytes]\n", val);

  /* ── CU-CP: DRB counts ───────────────────────────────── */
  } else if (cmp_str_ba("DRB.EstabSucc.5QI.UEID", name) == 0) {
    printf("  [DRBs Established ] DRB.EstabSucc.5QI.UEID          = %.0f\n", val);

  } else if (cmp_str_ba("DRB.RelActNbr.5QI.UEID", name) == 0) {
    printf("  [DRBs Released    ] DRB.RelActNbr.5QI.UEID          = %.0f\n", val);

  /* ── Serving SINR ────────────────────────────────────────
     Dynamic name: "L3servingSINR3gpp_cell_<X>_UEID_<IMSI>"  */
  } else if (starts_with(name, "L3servingSINR3gpp_cell_")) {
    printf("  [Serving SINR     ] %.*s = %.2f\n",
           (int)name.len, name.buf, val);

  /* ── Serving RSRP ────────────────────────────────────────
     Dynamic name: "L3servingRSRP3gpp_cell_<X>_UEID_<IMSI>_RSRP_<val>" */
  } else if (starts_with(name, "L3servingRSRP3gpp_cell_")) {
    printf("  [Serving RSRP     ] %.*s = %.2f\n",
           (int)name.len, name.buf, val);

  /* ── Target / Neighbour SINR ─────────────────────────────
     Dynamic name: "L3neighSINRListOf_UEID_<IMSI>_of_Cell_<X>" */
  } else if (starts_with(name, "L3neighSINRListOf_")) {
    printf("  [Target SINR      ] %.*s = %.2f\n",
           (int)name.len, name.buf, val);

  /* ── Target / Neighbour RSRP ─────────────────────────────
     Dynamic name: "L3neighRSRPListOf_UEID_<IMSI>_of_Cell_<X>" */
  } else if (starts_with(name, "L3neighRSRPListOf_")) {
    printf("  [Target RSRP      ] %.*s = %.2f\n",
           (int)name.len, name.buf, val);

  /* ── DU extra (reducedPmValues=false) ───────────────────── */
  } else if (cmp_str_ba("DRB.BufferSize.Qos.UEID", name) == 0) {
    printf("  [RLC Buffer/UE    ] DRB.BufferSize.Qos.UEID         = %.0f [bytes]\n", val);

  } else if (starts_with(name, "TB.TotNbrDlInitial.Qpsk")) {
    printf("  [QPSK TB          ] %.*s = %.0f\n", (int)name.len, name.buf, val);

  } else if (starts_with(name, "TB.TotNbrDlInitial.16Qam")) {
    printf("  [16QAM TB         ] %.*s = %.0f\n", (int)name.len, name.buf, val);

  } else if (starts_with(name, "TB.TotNbrDlInitial.64Qam")) {
    printf("  [64QAM TB         ] %.*s = %.0f\n", (int)name.len, name.buf, val);

  } else if (starts_with(name, "L1M.RS-SINR.Bin")) {
    printf("  [SINR Bin         ] %.*s = %.0f\n", (int)name.len, name.buf, val);

  /* ── Fake placeholder — silently ignore ─────────────────── */
  } else if (starts_with(name, "DRB.RlcSduDelayDl_Fake")) {
    /* skip */

  /* ── Unknown — print raw so we can identify later ────────── */
  } else {
    if (rec.value == REAL_MEAS_VALUE)
      printf("  [?REAL] %.*s = %.4f\n", (int)name.len, name.buf, rec.real_val);
    else
      printf("  [?INT ] %.*s = %d\n",   (int)name.len, name.buf, rec.int_val);
  }
}

/* =========================================================
 * Iterate Format-1 measurements for one UE
 * ========================================================= */
static void log_kpm_measurements(kpm_ind_msg_format_1_t const* f1)
{
  if (f1->meas_info_lst_len == 0) return;

  for (size_t j = 0; j < f1->meas_data_lst_len; j++) {
    meas_data_lst_t const d = f1->meas_data_lst[j];
    for (size_t z = 0; z < d.meas_record_len; z++) {
      meas_type_t       mt = f1->meas_info_lst[z].meas_type;
      meas_record_lst_t r  = d.meas_record_lst[z];

      if (mt.type == NAME_MEAS_TYPE)
        print_one_measurement(mt.name, r);
      else
        printf("  [ID meas type — not handled]\n");

      if (d.incomplete_flag && *d.incomplete_flag == TRUE_ENUM_VALUE)
        printf("  [WARNING] record not reliable\n");
    }
  }
}

/* =========================================================
 * KPM indication callback
 * ========================================================= */
static void sm_cb_kpm(sm_ag_if_rd_t const* rd)
{
  assert(rd != NULL);
  assert(rd->type == INDICATION_MSG_AGENT_IF_ANS_V0);
  assert(rd->ind.type == KPM_STATS_V3_0);

  kpm_ind_data_t const*            ind  = &rd->ind.kpm.ind;
  kpm_ric_ind_hdr_format_1_t const* hdr = &ind->hdr.kpm_ric_ind_hdr_format_1;
  kpm_ind_msg_format_3_t const*    frm3 = &ind->msg.frm_3;

  int64_t const now = time_now_us();
  static int counter = 1;

  lock_guard(&mtx);

  printf("\n========== KPM Report #%d | Latency = %ld [μs] ==========\n",
         counter, now - hdr->collectStartTime);

  for (size_t i = 0; i < frm3->ue_meas_report_lst_len; i++) {
    printf("\n--- UE #%zu ---\n", i + 1);
    ue_id_e2sm_t uid = frm3->meas_report_per_ue[i].ue_meas_report_lst;
    log_ue_id_tbl[uid.type](uid);
    log_kpm_measurements(&frm3->meas_report_per_ue[i].ind_msg_format_1);
  }

  printf("==========================================================\n");
  counter++;
}

/* =========================================================
 * Build KPM subscription
 * ========================================================= */
static label_info_lst_t fill_kpm_label(void)
{
  label_info_lst_t lbl = {0};
  lbl.noLabel  = ecalloc(1, sizeof(enum_value_e));
  *lbl.noLabel = TRUE_ENUM_VALUE;
  return lbl;
}

static kpm_act_def_format_1_t build_act_def_frm_1(void)
{
  kpm_act_def_format_1_t ad = {0};
  ad.meas_info_lst_len = N_WANTED_KPMS;
  ad.meas_info_lst     = calloc(N_WANTED_KPMS, sizeof(meas_info_format_1_lst_t));
  assert(ad.meas_info_lst);

  for (size_t i = 0; i < N_WANTED_KPMS; i++) {
    meas_info_format_1_lst_t* m = &ad.meas_info_lst[i];
    m->meas_type.type           = NAME_MEAS_TYPE;
    m->meas_type.name           = cp_str_to_ba(WANTED_KPMS[i]);
    m->label_info_lst_len       = 1;
    m->label_info_lst           = ecalloc(1, sizeof(label_info_lst_t));
    m->label_info_lst[0]        = fill_kpm_label();
  }

  ad.gran_period_ms = period_ms;
  ad.cell_global_id = NULL;
#if defined KPM_V2_03 || defined KPM_V3_00
  ad.meas_bin_range_info_lst_len = 0;
  ad.meas_bin_info_lst           = NULL;
#endif
  return ad;
}

static test_info_lst_t filter_predicate(test_cond_type_e tp, test_cond_e cond, int val)
{
  test_info_lst_t dst = {0};
  dst.test_cond_type  = tp;
  dst.S_NSSAI         = TRUE_TEST_COND_TYPE;

  dst.test_cond  = calloc(1, sizeof(test_cond_e)); assert(dst.test_cond);
  *dst.test_cond = cond;

  dst.test_cond_value       = calloc(1, sizeof(test_cond_value_t)); assert(dst.test_cond_value);
  dst.test_cond_value->type = OCTET_STRING_TEST_COND_VALUE;

  dst.test_cond_value->octet_string_value = calloc(1, sizeof(byte_array_t));
  assert(dst.test_cond_value->octet_string_value);
  dst.test_cond_value->octet_string_value->len    = 1;
  dst.test_cond_value->octet_string_value->buf    = calloc(1, sizeof(uint8_t));
  assert(dst.test_cond_value->octet_string_value->buf);
  dst.test_cond_value->octet_string_value->buf[0] = (uint8_t)val;
  return dst;
}

static kpm_act_def_t build_report_style_4(void)
{
  kpm_act_def_t act = {.type = FORMAT_4_ACTION_DEFINITION};
  act.frm_4.matching_cond_lst_len = 1;
  act.frm_4.matching_cond_lst =
      calloc(1, sizeof(matching_condition_format_4_lst_t));
  assert(act.frm_4.matching_cond_lst);
  act.frm_4.matching_cond_lst[0].test_info_lst =
      filter_predicate(S_NSSAI_TEST_COND_TYPE, EQUAL_TEST_COND, 1);
  act.frm_4.action_def_format_1 = build_act_def_frm_1();
  return act;
}

static kpm_sub_data_t gen_kpm_subs(void)
{
  kpm_sub_data_t s = {0};
  s.ev_trg_def.type = FORMAT_1_RIC_EVENT_TRIGGER;
  s.ev_trg_def.kpm_ric_event_trigger_format_1.report_period_ms = period_ms;
  s.sz_ad = 1;
  s.ad    = calloc(1, sizeof(kpm_act_def_t)); assert(s.ad);
  *s.ad   = build_report_style_4();
  return s;
}

static bool eq_sm(sm_ran_function_t const* e, int const id) { return e->id == id; }

static size_t find_sm_idx(sm_ran_function_t* rf, size_t sz,
                           bool (*f)(sm_ran_function_t const*, int const), int id)
{
  for (size_t i = 0; i < sz; i++) if (f(&rf[i], id)) return i;
  assert(0 && "SM not found");
}

/* =========================================================
 * main
 * ========================================================= */
int main(int argc, char* argv[])
{
  fr_args_t args = init_fr_args(argc, argv);
  init_xapp_api(&args);
  sleep(1);

  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  defer({ free_e2_node_arr_xapp(&nodes); });
  assert(nodes.len > 0);

  printf("Connected E2 nodes = %zu\n", nodes.len);
  printf("Requesting %zu KPMs:\n", N_WANTED_KPMS);
  for (size_t k = 0; k < N_WANTED_KPMS; k++)
    printf("  [%zu] %s\n", k + 1, WANTED_KPMS[k]);
  printf("\n");

  assert(pthread_mutex_init(&mtx, NULL) == 0);

  sm_ans_xapp_t* hndl = calloc(nodes.len, sizeof(sm_ans_xapp_t));
  assert(hndl);

  int const KPM_rf = 2;

  for (size_t i = 0; i < nodes.len; ++i) {
    e2_node_connected_xapp_t* n = &nodes.n[i];
    size_t idx = find_sm_idx(n->rf, n->len_rf, eq_sm, KPM_rf);
    assert(n->rf[idx].defn.type == KPM_RAN_FUNC_DEF_E);

    kpm_sub_data_t sub = gen_kpm_subs();
    hndl[i] = report_sm_xapp_api(&n->id, KPM_rf, &sub, sm_cb_kpm);
    assert(hndl[i].success == true);
    free_kpm_sub_data(&sub);
  }

  xapp_wait_end_api();

  for (size_t i = 0; i < nodes.len; ++i)
    if (hndl[i].success) rm_report_sm_xapp_api(hndl[i].u.handle);
  free(hndl);

  while (try_stop_xapp_api() == false) usleep(1000);

  printf("Test xApp run SUCCESSFULLY\n");
  return 0;
}
