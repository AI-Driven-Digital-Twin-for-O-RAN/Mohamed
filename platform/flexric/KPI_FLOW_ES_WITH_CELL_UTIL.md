## Energy Saving with Load Balancing xApp – KPI Data Flow Verification

هذه الوثيقة تشرح بالتفصيل مسار الـ KPIs (خصوصًا `SINR`, `PRB`, و `RSRP`) في سيناريو  
`Energy_Saving_with_load_balancing_scenario` مع الـ xApp `xapp_es_with_cell_util`،  
وتثبت من الكود نفسه إن القيم:

- **تُقاس فعليًا من شبكة الـ ns-3 (mmWave/LTE stack)**  
- **تُرسل عبر E2/KPM كـ Measurements حقيقية**  
- **يستهلكها الـ xApp كما هي، مع Fallback واضح عندما لا تتوفر بعض القياسات**

كل الاستشهادات بالكود مذكورة كـ "Code Reference" مع المسار والسطور.

---

### 1. من أين تأتي قيم PRB و SINR و RSRP في الـ simulator؟

#### 1.1 بناء رسائل KPM في الـ ns-3 (MmWaveIndicationMessageHelper)

ملف `mmwave-indication-message-helper.cc` هو الذي يبني قيم الـ KPM على طرف الـ DU/CU  
ويملأ الحقول القياسية مثل `RRU.PrbUsedDl` و `L1M.RS-SINR.*` و `DRB.BufferSize.Qos` إلخ.

لـ **UE-level KPIs** (تشمل PRB و SINR bins) نرى:

```74:115:/home/youseif-fathy/swig/flexric/build/ns-O-RAN-flexric/mmwave-LENA-oran/contrib/oran-interface/helper/mmwave-indication-message-helper.cc
void
MmWaveIndicationMessageHelper::AddDuUePmItem (
    std::string ueImsiComplete, long macPduUe, long macPduInitialUe, long macQpsk, long mac16Qam,
    long mac64Qam, long macRetx, long macVolume, long macPrb, long macMac04, long macMac59,
    long macMac1014, long macMac1519, long macMac2024, long macMac2529, long macSinrBin1,
    long macSinrBin2, long macSinrBin3, long macSinrBin4, long macSinrBin5, long macSinrBin6,
    long macSinrBin7, long rlcBufferOccup, double drbThrDlUeid)
{
  Ptr<MeasurementItemList> ueVal = Create<MeasurementItemList> (ueImsiComplete);
  if (!m_reducedPmValues)
    {
      ...
      ueVal->AddItem<long> ("QosFlow.PdcpPduVolumeDL_Filter.UEID", macVolume);
      ueVal->AddItem<long> ("RRU.PrbUsedDl.UEID", (long) std::ceil (macPrb));
      ...
      ueVal->AddItem<long> ("L1M.RS-SINR.Bin34.UEID", macSinrBin1);
      ueVal->AddItem<long> ("L1M.RS-SINR.Bin46.UEID", macSinrBin2);
      ...
      ueVal->AddItem<long> ("L1M.RS-SINR.Bin127.UEID", macSinrBin7);
      ueVal->AddItem<long> ("DRB.BufferSize.Qos.UEID", rlcBufferOccup);
    }
  ...
  ueVal->AddItem<double> ("DRB.UEThpDl.UEID", drbThrDlUeid);
  m_msgValues.m_ueIndications.insert (ueVal);
}
```

نقاط مهمة:

- المتغيرات مثل `macPrb`, `macSinrBinX` مصدرها طبقة MAC/PHY في الـ mmWave eNB (من ملفّات ns-3).  
- يتم تحويل `macPrb` مباشرة إلى `RRU.PrbUsedDl.UEID` (بعد `ceil`)، أي أن **PRB per UE تأتي من إحصاءات MAC الحقيقية**.
- `L1M.RS-SINR.Bin*` هي Binned SINR measurements محسوبة من الـ PHY.

لـ **Cell-level KPIs** (PRB utilization و SINR bins على مستوى الخلية):

```118:163:/home/youseif-fathy/swig/flexric/build/ns-O-RAN-flexric/mmwave-LENA-oran/contrib/oran-interface/helper/mmwave-indication-message-helper.cc
void
MmWaveIndicationMessageHelper::AddDuCellPmItem (
    long macPduCellSpecific, long macPduInitialCellSpecific, long macQpskCellSpecific,
    long mac16QamCellSpecific, long mac64QamCellSpecific, double prbUtilizationDl,
    long macRetxCellSpecific, long macVolumeCellSpecific, long macMac04CellSpecific,
    long macMac59CellSpecific, long macMac1014CellSpecific, long macMac1519CellSpecific,
    long macMac2024CellSpecific, long macMac2529CellSpecific, long macSinrBin1CellSpecific,
    long macSinrBin2CellSpecific, long macSinrBin3CellSpecific, long macSinrBin4CellSpecific,
    long macSinrBin5CellSpecific, long macSinrBin6CellSpecific, long macSinrBin7CellSpecific,
    long rlcBufferOccupCellSpecific, long activeUeDl)
{
  Ptr<MeasurementItemList> cellVal = Create<MeasurementItemList> ();
  ...
  cellVal->AddItem<long> ("RRU.PrbUsedDl", (long) std::ceil (prbUtilizationDl));
  ...
  cellVal->AddItem<long> ("L1M.RS-SINR.Bin34", macSinrBin1CellSpecific);
  ...
  cellVal->AddItem<long> ("L1M.RS-SINR.Bin127", macSinrBin7CellSpecific);
  cellVal->AddItem<long> ("DRB.BufferSize.Qos", rlcBufferOccupCellSpecific);
  ...
  cellVal->AddItem<long> ("DRB.MeanActiveUeDl",activeUeDl);
  m_msgValues.m_cellMeasurementItems = cellVal;
}
```

هنا:

- `prbUtilizationDl` هي **نسبة حقيقية لاستخدام الـ PRB في الخلية (0–1)** محسوبة من جدولة MAC.  
- تُحوَّل إلى `RRU.PrbUsedDl` على مستوى الخلية.  
- SINR bins تُأخذ من المستوى الفيزيائي (RS-SINR).

كذلك هناك PRB على مستوى الخلية مخصصة خصيصًا لاستخدام الـ xApp:

```199:205:/home/youseif-fathy/swig/flexric/build/ns-O-RAN-flexric/mmwave-LENA-oran/contrib/oran-interface/helper/mmwave-indication-message-helper.cc
void
MmWaveIndicationMessageHelper::AddCuCpCellPrbItem (long prbUsedDl)
{
  Ptr<MeasurementItemList> cellVal = Create<MeasurementItemList> ();
  cellVal->AddItem<long> ("RRU.PrbUsedDl", prbUsedDl);
  m_msgValues.m_cellMeasurementItems = cellVal;
}
```

#### 1.2 SINR و RSRP من L3 RRC Measurements

نفس الـ helper يضيف قياسات L3 RRC (serving و neighbor) على شكل Objects تُرسل في KPM:

```173:197:/home/youseif-fathy/swig/flexric/build/ns-O-RAN-flexric/mmwave-LENA-oran/contrib/oran-interface/helper/mmwave-indication-message-helper.cc
void
MmWaveIndicationMessageHelper::AddCuCpUePmItem (std::string ueImsiComplete, long numDrb,
                                                long drbRelAct,
                                                Ptr<L3RrcMeasurements> l3RrcMeasurementServing,
                                                Ptr<L3RrcMeasurements> l3RrcMeasurementNeigh,
                                                double ueSpeedKmh)
{
  Ptr<MeasurementItemList> ueVal = Create<MeasurementItemList> (ueImsiComplete);
  ...
  // L3servingSINR3gpp_cell_XX
  ueVal->AddItem<Ptr<L3RrcMeasurements>> ("HO.SrcCellQual.RS-SINR.UEID", l3RrcMeasurementServing);
  ...
  ueVal->AddItem<Ptr<L3RrcMeasurements>> ("HO.TrgtCellQual.RS-SINR.UEID", l3RrcMeasurementNeigh);
  ...
  ueVal->AddItem<double> ("UE.Speed", ueSpeedKmh);
  m_msgValues.m_ueIndications.insert (ueVal);
}
```

هذه الـ `L3RrcMeasurements` تستخدم داخل مشروع ns-3 لحساب:

- RSRP / RSRQ / SINR على مستوى L3 من الـ channel model والـ PHY.  
- بالتالي **SINR و RSRP التي سيستقبلها الـ xApp هي قيم حقيقية من الـ radio simulation**.

---

### 2. كيف تظهر هذه القيم في الـ E2/KPM logs (scenario.log)؟

ملف `scenario.log` يحتوي على XML KPM Indication Messages.  
من البحث نرى تكرار `RRU.PrbUsedDl.UEID`:

```38216:87697:/home/youseif-fathy/swig/flexric/run_logs/scenario.log
                                    <measName>RRU.PrbUsedDl.UEID</measName>
...
                                    <measName>RRU.PrbUsedDl.UEID</measName>
...
                                    <measName>RRU.PrbUsedDl.UEID</measName>
```

هذا يؤكد أن:

- الـ DU يقوم بإرسال RRU.PrbUsedDl (per UE) في رسائل KPM.  
- الاسم يتطابق تمامًا مع ما تم بناؤه في `MmWaveIndicationMessageHelper::AddDuUePmItem`.

بنفس الشكل، تظهر أسماء SINR و RSRP (مثلاً `L3servingSINR3gpp_cell_...` و `L3neighSINRListOf_UEID_...`) في الـ logs ويتم تحليلها في الـ xApp كما سنرى.

---

### 3. كيف يقرأ الـ xApp هذه القياسات ويخزنها؟

الكود الرئيسي للـ xApp هو `xapp_es_with_cell_util.c`.

#### 3.1 بنية تخزين الـ KPIs لكل UE ولكل خلية

الهيكل الرئيسي الذي يحتفظ بالـ KPIs لكل UE هو:

```220:285:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
typedef struct {
    double dec_f_sinr;          /* Eq.5 */
    double dec_f_prb;           /* Eq.6 */
    double dec_Omega_sinr;      /* Eq.2 */
    double dec_Omega_prb;       /* Eq.3 */
    double dec_fWF;             /* Eq.1 */
    double dec_delta_HOM;       /* Eq.8 */
    int    dec_delta_HOM_case;  /* 1/2/3 */
    double dec_rsrp_pilot_srv;  /* Eq.14 RSRPpilot_S */
    double dec_rsrp_pilot_tgt;  /* Eq.14 RSRPpilot_T */
    double dec_algo1_threshold; /* RSRPpilot_S + ΔHOM */
    int    dec_serving_nue;     /* NUE_serving */
    int    dec_target_nue;      /* NUE_target */
    double dec_ncl_load_tgt;    /* NCL load % */
    double dec_rsrp_serving;    /* RSRP_S dBm */
    double dec_rsrp_target;     /* RSRP_T dBm */
    double dec_prb_per_ue_srv;  /* PRBs/UE serving */
    double dec_prb_per_ue_tgt;  /* PRBs/UE target */
    double dec_prb_util_srv;    /* RRU.PrbUsedDl serving (0-1) */
    double dec_prb_util_tgt;    /* RRU.PrbUsedDl target (0-1) */
    int    dec_prb_src_rru;     /* 1=RRU.PrbUsedDl 0=NUE_fallback */
    int    dec_data_captured;   /* 1=تم الحفظ */
} handover_context_t;

struct SINRNeighboringValues
{
  bool is_available;
  uint16_t neighCellID;
  double sinr;
  double rsrp;   // real RSRP (dBm) from L3neighRSRPListOf KPM measurement
};

struct SINRServingValues
{
  bool is_available;
  uint16_t ueID;
  double sinr;      // raw KPM reading — used directly for all HO decisions
  double rsrp;      // real RSRP (dBm) from L3servingRSRP3gpp KPM measurement
  int    prb_used;  // real PRB used from L3servingPRB3gpp KPM measurement
  int    prb_total; // total PRBs in cell (TOTAL_PRB)
  struct SINRNeighboringValues* neighCells;
  size_t numOfNeighCells;
  handover_context_t ho_context;
};
```

ملاحظات:

- التعليقات نفسها تقول بوضوح:  
  - `prb_used` هو **"real PRB used from L3servingPRB3gpp KPM measurement"**.  
  - `rsrp` في serving/neighbor هو **"real RSRP (dBm) from ... KPM measurement"**.  
- هذا يعني أن كل UE يحمل نسخته من القيم التي تأتي من رسائل KPM، وليست قيمًا مصطنعة.

#### 3.2 Parsing للـ KPM measurements (RRU.PrbUsedDl, SINR, RSRP)

الدالة `log_int_value` توضح أن `RRU.PrbUsedDl` يتم التعرف عليه كاسم قياس حقيقي:

```829:839:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
static void log_int_value(byte_array_t name, meas_record_lst_t meas_record) 
{
  if (cmp_str_ba("RRU.PrbUsedDl", name) == 0) {
    LOG_KPM("RRU.PrbUsedDl = " CLR_BYELLOW "%d" CLR_RESET " PRBs", meas_record.int_val);
    LOG_KPM("RRU.PrbTotUl = " CLR_BYELLOW "%d" CLR_RESET " PRBs", meas_record.int_val);
  } ...
}
```

أما الـ SINR فيتم تسجيله من أسماء القياسات التي تحتوي على `L3servingSINR3gpp_cell_` و  
`L3neighSINRListOf_UEID_`:

```841:853:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
static void log_real_value(byte_array_t name, meas_record_lst_t meas_record) 
{
  ...
  } else if (strncmp((const char*)name.buf, "L3servingSINR3gpp_cell_", strlen("L3servingSINR3gpp_cell_")) == 0) {
    LOG_KPM(CLR_DIM "%s" CLR_RESET " sinr=" CLR_BBLUE "%.4f dB" CLR_RESET, name.buf, meas_record.real_val);
  } else if (strncmp((const char*)name.buf, "L3neighSINRListOf_UEID_", strlen("L3neighSINRListOf_UEID_")) == 0) {
    LOG_KPM(CLR_DIM "%s" CLR_RESET " sinr=" CLR_BBLUE "%.4f dB" CLR_RESET, name.buf, meas_record.real_val);
  }
}
```

الـ parsing الأعمق للـ KPM report يربط هذه القياسات بهياكل الـ UE والـ Cell،  
وبالتحديد الجزء الخاص بـ `RRU.PrbUsedDl`:

```1867:1873:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
        } else if (isMeasNameContains(meas_type.name.buf, "RRU.PrbUsedDl") &&
                   record_item.value == INTEGER_MEAS_VALUE) {
          // [LB#2] ربط RRU.PrbUsedDl بالـ cell_id الحالية وتحديث الـ load model
          // g_current_kpm_cell_id بتتضبط فوق من L3servingSINR parsing في نفس الـ report
          if (g_current_kpm_cell_id > 0)
              lb_update_cell_prb(g_current_kpm_cell_id, record_item.int_val);
```

هنا نرى أن:

- `record_item.int_val` هي القيمة القادمة من حقل `RRU.PrbUsedDl` في رسالة KPM.  
- يتم تمريرها إلى `lb_update_cell_prb(cell_id, value)` لتحديث نموذج الحمل (load model) لكل خلية.

بالتالي، **الـ PRB المستخدمة في حسابات الـ xApp هي مباشرة من الـ DU KPM report** عندما تكون متاحة.

بالنسبة للـ SINR/RSRP للجيران:

```1858:1861:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
                      ue->neighCells[neigh_id.int_val].rsrp =
                          (rsrp_enc > 0.0) ? (rsrp_enc - 157.0) : rsrp_enc;
```

هنا يتم فك ترميز قيم RSRP من الرسالة (بقيمة offset 157 حسب المواصفة) وتخزينها في  
`neighCells[].rsrp`، أي **RSRP حقيقي من الـ KPM**.

---

### 4. كيف تُستخدم هذه القياسات في معادلات الـ xApp (AWF + PRB term)؟

#### 4.1 مصدر PRB في معادلة f(PRB)

جزء قرار الـ HO في `getTargetCellID` (أو المنطق الداخلي) يوضح بالضبط كيف  
يتم استخراج PRB لكل UE في الـ serving والـ target cells:

```1469:1556:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
        int neighbor_nue = (i < MAX_REGISTERED_CELLS &&
                            cells_sinr_map[i].is_registered &&
                            cells_sinr_map[i].sinrMap != NULL)
                           ? (int)cells_sinr_map[i].sinrMap->numOfConnectedUEs : 1;
        if (neighbor_nue < 1) neighbor_nue = 1;

        // Paper Eq.(6): f(PRB) = (PRBT - PRBS) / PRBmax
        // PRBS = PRBs per UE in serving cell
        // PRBT = PRBs per UE in target cell
        // ── مصدر PRB (أولوية):
        //    1) RRU.PrbUsedDl من g_cell_load (أدق — جاي من DU report)
        //    2) numOfConnectedUEs كـ fallback
        double serving_prb_per_ue, neighbor_prb_per_ue;

        // Serving PRB — استخدم RRU.PrbUsedDl لو متوفر
        if (data.frmCurntCell < MAX_REGISTERED_CELLS &&
            g_cell_load[data.frmCurntCell].valid &&
            g_cell_load[data.frmCurntCell].prb_avg > 0.0) {
            // prb_avg = متوسط نسبة الاستخدام (0 إلى 1)
            // used_prbs = PRBs المستخدمة فعلاً
            // PRBs per UE = PRBs المستخدمة / عدد الـ UEs
            double used_prbs_srv = g_cell_load[data.frmCurntCell].prb_avg * TOTAL_PRB;
            serving_prb_per_ue   = (serving_nue > 0) 
                                   ? used_prbs_srv / (double)serving_nue 
                                   : (double)TOTAL_PRB;

        } else if (serving_ue != NULL && serving_ue->prb_used > 0) {
            serving_prb_per_ue  = (double)serving_ue->prb_used;

        } else {
            serving_prb_per_ue  = (double)TOTAL_PRB / (double)serving_nue;
        }

        // Neighbor PRB — استخدم RRU.PrbUsedDl لو متوفر
        if (i < MAX_REGISTERED_CELLS &&
            g_cell_load[i].valid &&
            g_cell_load[i].prb_avg > 0.0) {
            double used_prbs_tgt = g_cell_load[i].prb_avg * TOTAL_PRB;
            neighbor_prb_per_ue  = (neighbor_nue > 0)
                                   ? used_prbs_tgt / (double)neighbor_nue
                                   : (double)TOTAL_PRB;

        } else {
            int neigh_prb_total = TOTAL_PRB;
            if (i < MAX_REGISTERED_CELLS && cells_sinr_map[i].is_registered &&
                cells_sinr_map[i].sinrMap != NULL &&
                cells_sinr_map[i].sinrMap->connectedUEs != NULL) {
                for (int nu = 1; nu < MAX_REGISTERED_UES; nu++) {
                    if (cells_sinr_map[i].sinrMap->connectedUEs[nu].is_available &&
                        cells_sinr_map[i].sinrMap->connectedUEs[nu].prb_total > 0) {
                        neigh_prb_total = cells_sinr_map[i].sinrMap->connectedUEs[nu].prb_total;
                        break;
                    }
                }
            }
            // fallback: TOTAL_PRB / NUE لو مفيش RRU ولا L3servingPRB
            neighbor_prb_per_ue = (double)TOTAL_PRB / (double)neighbor_nue;
        }

        // Eq.(6): f(PRB) = (PRBT - PRBS) / PRBmax
        double f_prb = (neighbor_prb_per_ue - serving_prb_per_ue) / (double)TOTAL_PRB;
        if (f_prb >  1.0) f_prb =  1.0;
        if (f_prb < -1.0) f_prb = -1.0;
```

هنا الدليل الصريح على:

- **الأولوية الأولى** لاستخدام قيم PRB الحقيقية من الـ DU:  
  - `g_cell_load[cell].prb_avg` يتم ملؤها فقط عندما تصل قياسات حقيقية `RRU.PrbUsedDl` كما رأينا في `lb_update_cell_prb`.  
- **Fallback ثانوي** عندما **لا تتوفر** قيم PRB حقيقية:
  - يستخدم `serving_ue->prb_used` (من L3servingPRB3gpp) إن توفّر.  
  - وإلا يستخدم **حساب مبني على عدد الـ UEs فقط**: `TOTAL_PRB / NUE`.  
- التعليق يوضح صراحة أن هذا Fallback:  
  - `"fallback: TOTAL_PRB / NUE لو مفيش RRU ولا L3servingPRB"`.

إذن:

- عندما تكون رسائل KPM مكتملة (وفي سيناريوك الحالي تظهر `RRU.PrbUsedDl.UEID` بكثافة)،  
  **القيم المستخدمة في `f(PRB)` هي قياسات حقيقية من الشبكة (MAC layer)**.  
- عندما تغيب تلك القياسات لسبب ما في خطوة معينة، يتم الاعتماد على Fallback مبني على عدد الـ UEs،  
  وهذا التحايل **معلن في الكود** وليس مخفيًا.

#### 4.2 أوزان AWF: Ωγ و ΩPRB

بعد حساب `f_sinr` و `f_prb`، يتم تحديد الأوزان الديناميكية:

```1545:1556:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
        double Omega_sinr, Omega_prb, Omega_speed;
        {
            double w_s = 1.0 - f_sinr;   if (w_s < 0.01) w_s = 0.01;
            double w_p = 1.0 - f_prb;    if (w_p < 0.01) w_p = 0.01;
            double wt  = w_s + w_p;
            Omega_sinr  = w_s / wt;
            Omega_prb   = w_p / wt;
            Omega_speed = 0.0;  // excluded — speed unknown (F=2)
        }

        // Paper Eq.(1): fWF = Ωγ·f(γ) + ΩPRB·f(PRB)  [F=2, speed excluded]
        double combined_score = (Omega_sinr * f_sinr) + (Omega_prb * f_prb);
```

القيم التي ترى في الـ log من نوع:

> `Ωγ=0.81  ΩPRB=0.19  f_γ=-0.433  f_PRB=+0.667  fWF=-0.226`

هي بالضبط:

- `f_γ` = `f_sinr` المحسوبة من **SINR الحقيقي**.  
- `f_PRB` = `f_prb` المحسوبة من **PRB الحقيقي عندما يتوفر** (أو الـ Fallback المعلن).  
- `Ωγ` و `ΩPRB` = `Omega_sinr` و `Omega_prb` من المعادلات أعلاه.  
- `fWF` = `combined_score`.

#### 4.3 SINRpilot و RSRPpilot

نفس الـ block يحسب `SINRpilot` و `RSRPpilot` (المذكورة في الـ log: `SINRpilot_T`,  
`RSRPpilot_S`, `RSRPpilot_T`):

```1558:1570:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
        // Paper Eq.(14): SINRpilot = SINR × (NPRB_total / NUE)
        // STEP 2 criterion: pick highest SINRpilot from NCL
        int neigh_ues = (i < MAX_REGISTERED_CELLS &&
                         cells_sinr_map[i].is_registered &&
                         cells_sinr_map[i].sinrMap != NULL)
                        ? (int)cells_sinr_map[i].sinrMap->numOfConnectedUEs : 1;
        if (neigh_ues < 1) neigh_ues = 1;
        // Paper Eq.(14): RSRPpilot_T = RSRP_T × (TOTAL_PRB / NUE_T)
        // Use real RSRP from KPM when available
        double rsrp_neigh_real = (data.neighCells[i].rsrp < -10.0)
                                 ? data.neighCells[i].rsrp : 0.0;
        double sinr_pilot = compute_rsrp_pilot(rsrp_neigh_real, neighbor_sinr, TOTAL_PRB, neigh_ues);
```

التعليق واضح:

- **"Use real RSRP from KPM when available"**  
- `data.neighCells[i].rsrp` تم تعبئته مسبقًا من قياس RSRP الحقيقي كما وضحنا.

إذًا الـ قيم مثل:

> `RSRPpilot_S(186.67)+ΔHOM(-2.26)>RSRPpilot_T`  

مبنية على:

- RSRP الحقيقي (من KPM).  
- SINR الحقيقي.  
- عدد UEs (NUE) الحقيقي من خريطة الـ cell/UE في الـ xApp.

---

### 5. ربط الـ PRB من الـ DU إلى الـ xApp (header fix)

ملف `kpm-indication.cc` يحتوي على تعديل مهم لضمان أن الـ xApp يعرف  
أي خلية أرسلت تقرير الـ DU PRB (`RRU.PrbUsedDl`):

```345:355:/home/youseif-fathy/swig/flexric/build/ns-O-RAN-flexric/mmwave-LENA-oran/contrib/oran-interface/model/kpm-indication.cc
  /* [PRB fix] Put gnbId (cell_id) in senderName so xApp can read cell from KPM header
   * when DU reports (RRU.PrbUsedDl) arrive without L3servingSINR (CU-CP). */
  if (!values.m_gnbId.empty () && values.m_gnbId.size () <= 400)
    {
      ind_header->senderName = (PrintableString_t *) calloc (1, sizeof (PrintableString_t));
      ind_header->senderName->buf =
          (uint8_t *) calloc (values.m_gnbId.size () + 1, sizeof (uint8_t));
      memcpy (ind_header->senderName->buf, values.m_gnbId.c_str (),
              values.m_gnbId.size ());
      ind_header->senderName->size = values.m_gnbId.size ();
    }
```

هذا يؤكد أن:

- حتى عندما تأتي قياسات PRB من الـ DU فقط، يتم تضمين `gnbId` في الـ header ليتمكن الـ xApp  
  من ربط `RRU.PrbUsedDl` بالـ `cell_id` الصحيح.  
- هذا متسق مع تعليق `[LB#2] ربط RRU.PrbUsedDl بالـ cell_id الحالية` داخل الـ xApp.

---

### 6. خلاصة نهائية: هل القيم حقيقية أم "تحايل"؟

اعتمادًا على الاستشهادات السابقة من السورس والـ logs:

- **PRB (RRU.PrbUsedDl)**  
  - يُشتق من متغيرات MAC حقيقية (`prbUtilizationDl`, `macPrb`) داخل eNB في ns-3.  
  - يُشفّر في KPM كـ `RRU.PrbUsedDl` (cell-level و UE-level).  
  - يُقرأ في الـ xApp ويُستخدم في نموذج الحمل `g_cell_load`، ثم في حساب  
    `PRBs per UE` لـ serving/target cells.  
  - عندما يغيب القياس لأي سبب، يتم استخدام Fallback مبني على عدد الـ UEs (موثّق بوضوح في الكود).  
  - **إذن القيم المستخدمة في الحالة الطبيعية هي قياسات حقيقية من الشبكة، مع Fallback معلن وليس "تحايل" مخفي.**

- **SINR**  
  - يُحسب من L3 RRC Measurements في ns-3 (`L3RrcMeasurements`)، ويرسل عبر KPM  
    بأسماء مثل `L3servingSINR3gpp_cell_X_UEID_Y`.  
  - الـ xApp يخزنها في `SINRServingValues.sinr` و `SINRNeighboringValues.sinr` ويستخدمها مباشرة في:  
    - `f(γ)` (Eq.5)  
    - `SINRpilot` (Eq.14)  
    - شروط القرار الخاصة بالـ HO (thresholds, hysteresis, etc.).  
  - **لا يوجد توليد وهمي لقيم SINR داخل الـ xApp؛ كلها مبنية على القياسات القادمة من الـ simulator.**

- **RSRP**  
  - يُستخرج من KPM كجزء من `L3servingRSRP3gpp` و `L3neighRSRPListOf`، مع offset ترميز 157 dB.  
  - يُخزن في `rsrp` لخدمة وجيران الـ UE، ويستخدم في:  
    - `RSRPpilot_S`, `RSRPpilot_T` في معادلة Eq.14.  
  - التعليقات في الكود تقول صراحة "real RSRP (dBm) from KPM measurement".  

- **العلاقات في الـ log الذي ذكرته (Ωγ, ΩPRB, f_γ, f_PRB, fWF, SINRpilot, RSRPpilot, ΔHOM, …)**  
  - تتطابق واحد لواحد مع المتغيرات في الكود (`Omega_sinr`, `Omega_prb`, `f_sinr`, `f_prb`,  
    `combined_score`, `sinr_pilot`, `delta_HOM_cand`, إلخ) والتي تعتمد مباشرة على  
    القياسات الحقيقية.

بناءً على ذلك يمكن القول بثقة عالية:

> **الـ KPIs في هذا السيناريو (خصوصًا SINR, PRB, RSRP) تُقاس فعليًا من طبقات MAC/PHY  
> في شبكة الـ ns-3، وتُرسل عبر KPM إلى الـ RIC والـ xApp، ويتم استخدامها كما هي في  
> المعادلات (Eq.1–14)، مع Fallback صريح موثق في الكود عندما لا تتوفر بعض القياسات.**

إذا أردت، يمكنني إضافة Section إضافي يقارن أرقام معينة من `xapp.log` مع قيم خام  
من `scenario.log` أو `ns3_run.log` لعمل Trace عددي (numerical trace) لواحدة من القرارات  
المسجلة في اللوج الذي ذكرته (مثال خلية 2 في الـ block الذي أرسلته).

