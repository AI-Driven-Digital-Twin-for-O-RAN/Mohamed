## Realism of KPI Flow and Use of Fallbacks

هذا الملف يجاوب تحديدًا على سؤالين:

1. **هل الـ flow كامل من الـ simulator → KPM → RIC → xApp حقيقي فعلاً مثل ما يعمل الباحثون؟**  
2. **هل الأرقام التي تظهر في الـ logs (خاصة PRB/SINR/RSRP) تعتمد على Fallback "تحايلي" أم على قياسات حقيقية، ومتى يُستخدم الـ Fallback بالضبط؟**

الإجابة مبنية بالكامل على كود المشروع نفسه (ns-3 + flexric + xApp)، مع استشهادات مباشرة.

---

### 1. هل الـ KPI flow حقيقي بالكامل أم فيه تحايل؟

#### 1.1 من أين تُحسب القيم داخل الـ simulator؟

- **PRB (استغلال الـ PRBs)**  
  - يُشتق من إحصاءات MAC و/أو DU في mmWave/LTE eNB داخل ns-3.  
  - في `MmWaveIndicationMessageHelper::AddDuUePmItem` و `AddDuCellPmItem` يتم أخذ قيم مثل  
    `macPrb` و `prbUtilizationDl` (المحسوبة من جدولة MAC الحقيقية) وتحويلها إلى:
    - `RRU.PrbUsedDl.UEID` (per UE)  
    - `RRU.PrbUsedDl` (per cell)
  - هذا يعني أن الـ PRB المستخدمة في KPM هي **قياسات حقيقية من سلوك الشبكة في الـ simulator**،  
    وليس أرقام مصطنعة داخل الـ xApp.

- **SINR**  
  - يُحسب من طبقة PHY و L3 RRC في ns-3، داخل كائنات `L3RrcMeasurements`.  
  - helper `MmWaveIndicationMessageHelper::AddCuCpUePmItem` يضع هذه القياسات في حقول مثل  
    `HO.SrcCellQual.RS-SINR.UEID` و `HO.TrgtCellQual.RS-SINR.UEID`.  
  - الـ xApp يقرأ هذه الأسماء (`L3servingSINR3gpp_cell_...`, `L3neighSINRListOf_UEID_...`) ويخزن  
    القيم كما هي في هياكله (`SINRServingValues.sinr`, `SINRNeighboringValues.sinr`).

- **RSRP**  
  - يأتي من L3 measurements (`L3servingRSRP3gpp`, `L3neighRSRPListOf`) ويتم ترميزها وفقًا  
    للمواصفات (قيمة + offset).  
  - في الـ xApp تُفكّر هذه القيم (طرح 157 dB مثلاً) وتخزن كـ `rsrp` حقيقي بالـ dBm.

**الخلاصة هنا**: الجزء المسؤول عن توليد الـ KPIs موجود في طبقات ns-3 (MAC/PHY/RRC)،  
والـ xApp مجرد "مستهلك" لهذه القيم، لا يقوم بتوليدها من الصفر.

#### 1.2 كيف تنتقل القيم عبر KPM/E2 إلى الـ xApp؟

- helper `MmWaveIndicationMessageHelper` يبني رسائل `E2SM_KPM_IndicationMessage`  
  ويملأها بـ:
  - أسماء قياسات قياسية (`RRU.PrbUsedDl`, `L1M.RS-SINR.*`, `DRB.*`, إلخ).  
  - قيم رقمية مشتقة من الـ MAC/PHY/RRC.
- ملف `scenario.log` الذي حصلتَ عليه من تشغيل:
  - `./ns3 run "Energy_Saving_with_load_balancing_scenario ..."`  
  يحتوي على XML KPM Indication Messages، وفيه تكرار لأسماء مثل:
  - `<measName>RRU.PrbUsedDl.UEID</measName>`  
  - `<measName>...L3servingSINR3gpp_cell_...</measName>`  
  - إلخ.
- الـ xApp `xapp_es_with_cell_util` مشترك على RAN function ID 2 (KPM) ويستقبل نفس الرسائل  
  عبر الـ near-RT RIC، ويفككها في الدوال:
  - `log_kpm_measurements(...)`  
  - `sm_cb_kpm_core(...)`  
  - routines الخاصة بـ parsing للأسماء والقيم.

**النتيجة**: الـ flow من الـ simulator إلى الـ xApp هو نفس المسار الذي يعمل به باحثو O-RAN/ns-3:

> ns-3 (MAC/PHY/RRC) → KPM helper → E2SM KPM message → near-RT RIC → xApp callback → منطق القرار.

لا يوجد "قيمة من الهواء" تُنتج داخل الـ xApp، بل كل شيء يبدأ من الـ simulator.

---

### 2. على ماذا تعتمد الأرقام في الـ logs؟ وهل فيها Fallback؟

#### 2.1 ما الذي تراه في الـ logs بالضبط؟

مثال من log الـ xApp الذي ذكرته:

> `Cell 2  SINR +7.00 dB  Load 5%  Ωγ=0.81  ΩPRB=0.19  f_γ=-0.433  f_PRB=+0.667  fWF=-0.226  pilot=196.00  ΔHOM=-2.26  ◆ BEST (NCL+SINRpilot)`

هذه الأرقام تأتي من متغيرات في الكود:

- `serving_sinr`, `neighbor_sinr` ← قيم SINR الحقيقية من KPM.  
- `serving_prb_per_ue`, `neighbor_prb_per_ue` ← مشتقة من `RRU.PrbUsedDl` أو fallback واضح.  
- `f_sinr`, `f_prb` ← تطبيق Eq.(5) و Eq.(6) على هذه القيم.  
- `Omega_sinr` (= Ωγ), `Omega_prb` (= ΩPRB) ← حساب أوزان AWF من Eq.(2–3).  
- `combined_score` (= fWF) ← Eq.(1).  
- `sinr_pilot`, `RSRPpilot_S`, `RSRPpilot_T`, `ΔHOM` ← من Eq.(8–9–14) على قيم SINR/RSRP الحقيقية.

أي أن **كل رقم تراه في هذا السطر مُشتق رياضيًا من قياسات وصلت عبر KPM**،  
مع استخدام Fallback **فقط** عندما لا تصل بعض القياسات (وسنوضح متى بالضبط).

#### 2.2 متى يعتمد النظام على Fallback بدل القياس الحقيقي؟

الكود في `xapp_es_with_cell_util.c` موثّق بوضوح حول مصادر PRB:

```1469:1526:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
        // ── مصدر PRB (أولوية):
        //    1) RRU.PrbUsedDl من g_cell_load (أدق — جاي من DU report)
        //    2) numOfConnectedUEs كـ fallback
        double serving_prb_per_ue, neighbor_prb_per_ue;

        // Serving PRB — استخدم RRU.PrbUsedDl لو متوفر
        if (data.frmCurntCell < MAX_REGISTERED_CELLS &&
            g_cell_load[data.frmCurntCell].valid &&
            g_cell_load[data.frmCurntCell].prb_avg > 0.0) {
            ...
            serving_prb_per_ue = used_prbs_srv / (double)serving_nue;

        } else if (serving_ue != NULL && serving_ue->prb_used > 0) {
            serving_prb_per_ue  = (double)serving_ue->prb_used;

        } else {
            serving_prb_per_ue  = (double)TOTAL_PRB / (double)serving_nue;
        }

        // Neighbor PRB — استخدم RRU.PrbUsedDl لو متوفر
        if (i < MAX_REGISTERED_CELLS &&
            g_cell_load[i].valid &&
            g_cell_load[i].prb_avg > 0.0) {
            ...
            neighbor_prb_per_ue  = used_prbs_tgt / (double)neighbor_nue;

        } else {
            ...
            // fallback: TOTAL_PRB / NUE لو مفيش RRU ولا L3servingPRB
            neighbor_prb_per_ue = (double)TOTAL_PRB / (double)neighbor_nue;
        }
```

**بالتفصيل:**

- **الحالة الطبيعية (بدون Fallback)**  
  - عندما يتوفر قياس `RRU.PrbUsedDl` للخلية من الـ DU عبر KPM (وهذا ظاهر بوضوح في `scenario.log`)،  
    يتم حساب:
    - `prb_avg` في `g_cell_load[cell].prb_avg` من `RRU.PrbUsedDl`.  
    - `used_prbs = prb_avg * TOTAL_PRB`.  
    - `PRBs per UE = used_prbs / number_of_UEs`.  
  - هذه القيمة تدخل في `f_prb` مباشرة.  
  - في سيناريوك، الـ logs تظهر وجود `RRU.PrbUsedDl.UEID` و `RRU.PrbUsedDl`، وبالتالي **الغالبية العظمى من القرارات تستخدم قياسات حقيقية للـ PRB**.

- **الحالة مع Fallback حقيقي/محدود المعلومات**  
  - لو لم تصل قيمة `RRU.PrbUsedDl` لتلك الخلية في تلك الدورة:
    1. يحاول استخدام `serving_ue->prb_used` من قياسات أخرى (`L3servingPRB3gpp`) إن توفرت.  
    2. إن لم تتوفر أيضًا، يستخدم Fallback هندسي مبني على:  
       - `TOTAL_PRB / NUE` (عدد PRBs الكلي في الخلية مقسومًا على عدد الـ UEs).  
  - هذا Fallback **لا يخترع Traffic أو SINR**؛ هو تقريب لحمل الخلية باستخدام معلومات topology (عدد UEs، حجم PRB الكلي)،  
    ويُستخدم فقط عند غياب القياس المباشر.

**مهم جدًا**: الكود نفسه يعلّق بوضوح أنه Fallback:

- `"fallback: TOTAL_PRB / NUE لو مفيش RRU ولا L3servingPRB"`  
- أي أن الأمر **مش تحايل مخفي**؛ بل تصميم معلن لتفادي فقدان القياسات في بعض الدورات.

#### 2.3 ما وضع SINR و RSRP بالنسبة للـ Fallback؟

- **SINR**  
  - لا يوجد في الكود توليد "وهمي" لقيم SINR.  
  - كل أماكن استخدام SINR (f(γ), SINRpilot, thresholds, إلخ) تعتمد على:
    - `SINRServingValues.sinr` ← من `L3servingSINR3gpp_cell_...`.  
    - `SINRNeighboringValues.sinr` ← من `L3neighSINRListOf_UEID_...`.  
  - إذا لم تصل قياسات SINR لخلية/UE معينة، الـ UE ببساطة لا يُستخدم في قرار HO في تلك الدورة أو يتم تجاهل تلك الخلية.

- **RSRP**  
  - في جزء `RSRPpilot`، الكود يقول:

    ```1558:1569:/home/youseif-fathy/swig/flexric/examples/xApp/c/orange/xapp_es_with_cell_util.c
        // Paper Eq.(14): RSRPpilot_T = RSRP_T × (TOTAL_PRB / NUE_T)
        // Use real RSRP from KPM when available
        double rsrp_neigh_real = (data.neighCells[i].rsrp < -10.0)
                                 ? data.neighCells[i].rsrp : 0.0;
    ```

  - إذا لم يكن هناك RSRP حقيقي، يستخدم الكود قيمة 0 (أي أنه فعليًا **يعاملها كغياب معلومات** ويعتمد أكثر على SINR)،  
    وليس أنه يخلق RSRP وهمي موجّه لنتيجة معينة.

---

### 3. هل ما يحدث هنا مشابه لما يفعله الباحثون، أم "تحايل"؟

بناءً على كل ما سبق:

- **نعم، الـ flow المستخدم هو نفس الفلسفة التي يتبعها الباحثون في أبحاث O-RAN/ns-3:**
  - KPIs تولَّد من طبقات MAC/PHY/RRC في simulator.  
  - تُرسل عبر KPM/E2 بنفس تعريفات الأسماء القياسية.  
  - xApp يقرأ القياسات ويطبّق عليها معادلات (من ورقة علمية) لاتخاذ قرارات HO/LB.

- **نعم، القيم التي تراها في الـ logs (خاصة الـ block الذي يحتوي على Ωγ و ΩPRB و f_γ و f_PRB و fWF و SINRpilot و RSRPpilot) تعتمد على قياسات حقيقية عندما تكون متاحة.**

- **وجود Fallback لا يعني تحايل، بل هو ممارسة شائعة في الأنظمة الحقيقية والبحثية:**
  - الأنظمة الواقعية تواجه أحيانًا فقدان قياسات أو تأخير تقارير؛  
    الحل الشائع هو استخدام نموذج تقريبي (مثل `TOTAL_PRB / NUE`) حتى تصل قياسات أدق.  
  - هنا الـ Fallback:
    - موثّق في الكود.  
    - يُفعَّل فقط عند غياب القياس المباشر.  
    - لا يختلق SINR/RSRP/Traffic من العدم، بل يعتمد على topology وعدد UEs.

**إجابة مباشرة على سؤالك:**

> - الـ flow **حقيقي بالكامل** من الـ simulator إلى الـ xApp.  
> - الأرقام في الـ logs تعتمد أساسًا على قياسات حقيقية (PRB/SINR/RSRP) من الشبكة الـ simulated.  
> - Fallback موجود **فقط** للتعامل مع حالات غياب بعض القياسات، وهو واضح ومعلن في الكود،  
>   وليس تحايلاً للهروب من مشكلة في حساب القيم.  
> - ما قمتَ به يتماشى مع طريقة عمل الباحثين والخبراء عند التعامل مع ns-3 و O-RAN،  
>   مع زيادة في الشفافية (بسبب التعليقات الواضحة حول الـ fallback).

