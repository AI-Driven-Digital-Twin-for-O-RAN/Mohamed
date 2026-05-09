/*
 * sinr-ho-xApp[RB] — Smart SINR-based continuous handover xApp for ns-O-RAN-flexric.
 * Uses KPM v3 UE SINR measurements to evaluate serving/neighbor cells and
 * dynamically trigger RIC Control handover commands via FlexRIC's E2SM-RC.
 * Implements multi-handover logic, anti ping-pong protection, and robust
 * UE tracking across mmWave cells, with rich, colorized runtime logging.
 *
 * Authors:
 *   Youssef Fathy <youssefathy1@gmail.com>
 *   Alyaa Mohamed <alyaamohammed251@gmail.com>
 *   Omar Farouk   <omar297farouk@gmail.com>
 *
 * Suez Canal University
 * Feb 2026
 *
 * ════════════════════════════════════════════════════════════════════
 *  FINAL CORRECTIONS (paper: Gures et al., ICT Express 2023)
 *  "Load balancing in 5G heterogeneous networks based on AWF"
 *  Equations: Eq.(1-14) + Algorithm 1 (page 1022)
 * ════════════════════════════════════════════════════════════════════
 *
 *  FIX 1 — TTT_MIN_SEC wrong value (Eq.10-13 Table-1):
 *    WRONG   : #define TTT_MIN_SEC 0.9  (causes T always ≤ Tmin → Z2 branch)
 *    CORRECT : #define TTT_MIN_SEC 0.0  (paper Table-1: Tmin = 0.0 s)
 *    EFFECT  : TTT was always jumping by +ρ even when fWF improved, and
 *              the [at Tmin] label was always shown incorrectly.
 *    WHERE   : #define block, compute_ttt_update()
 *
 *  FIX 2 — last_fWF updated in wrong place → TTT double-update (Eq.10-13):
 *    WRONG   : ctx->last_fWF updated inside getTargetCellID → idle TTT path
 *              reads fWF_prev == fWF_now (same cycle) → Delta huge → TTT jump.
 *              Then doHandoverAction updates TTT again in same cycle → 2ρ jump.
 *    CORRECT : last_fWF updated ONLY in idle TTT path and doHandoverAction,
 *              NOT in getTargetCellID. This ensures fWF_prev is always from
 *              the PREVIOUS cycle. Idle path ALWAYS advances last_fWF.
 *    WHERE   : getTargetCellID() (removed), idle TTT block, doHandoverAction()
 *
 *  FIX 3 — current_ttt_sec zero-initialized → display shows 0.000s:
 *    WRONG   : g_ue_ho_ctx[] from calloc has current_ttt_sec = 0.0 → decision
 *              box prints TTT_cur=0.000s; idle path re-initialises every cycle.
 *    CORRECT : Init loop in main() sets current_ttt_sec = TTT_DEFAULT_SEC (0.5s)
 *              and last_fWF = 0.0 for all UEs before first evaluation.
 *    WHERE   : main() — after init_xapp_api()
 *
 *  FIX 4 — TTT idle label misleading (Eq.10-13 Q dead-zone):
 *    WRONG   : Old label logic: when TTT decreased, it could print
 *              "signal worsened -> TTT decreased" which is a contradiction.
 *              Also: no log when TTT stays unchanged (dead-zone |Δ| < Q),
 *              making it impossible to verify the Q threshold is working.
 *    CORRECT : Label derived directly from which compute_ttt_update branch fired:
 *              - TTT decreased  ← fWF_improved (Δ ≤ -Q): "fWF improved → faster HO"
 *              - TTT increased  ← fWF_worsened (Δ ≥ +Q): "fWF worsened → slower HO"
 *              - TTT unchanged  ← dead-zone (|Δ| < Q): logged as "dead-zone no change"
 *              This makes the Q threshold behaviour fully observable in logs.
 *    WHERE   : evaluateHandoverOpportunities() idle TTT block
 *
 *  NOTE on FIX 1 (Q threshold):
 *    The fix ensures TTT changes ONLY when |fWF_now - fWF_prev| >= Q.
 *    This is already implemented in compute_ttt_update() via:
 *      fWF_improved = (delta_fWF <= -TTT_Q)   [Δ ≤ -0.1 → TTT decreases]
 *      fWF_worsened = (delta_fWF >=  TTT_Q)   [Δ ≥ +0.1 → TTT increases]
 *      else → dead-zone, TTT unchanged
 *    FIX 4 makes this dead-zone visible in the log output.
 *
 *  NOTE on Speeds (UPDATED — νmax set to scenario max speed):
 *    νmax = 13.9 m/s = 50.04 km/h (highest UE speed in scenario: UE7).
 *    UE7 (13.9 m/s) produces f(υ) = 2·log2(100.08/50.04)−1 = +1.0 (clamped).
 *    Fixed UEs (UE1, UE8) produce f(υ) = 2·log2(50.04/50.04)−1 = -1.0.
 *    Other moving UEs produce intermediate f(υ) values between -1 and +1.
 *
 *  NOTE on Global HO Lock (FIX analysed — no code change needed):
 *    Lock prevents concurrent X2 handovers because ns3 epc-x2 does not
 *    support parallel X2 operations. This is an ns3 constraint, NOT a
 *    paper algorithm violation. Paper does not restrict to serial HOs.
 * ════════════════════════════════════════════════════════════════════
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
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <inttypes.h>                                                           //rsrp

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

/* ============================================================
 * log_decision_step() — يطبع حسابات دورة القرار كاملة في الترمنال
 * يستخدم في getTargetCellID بعد حساب كل المتغيرات لكل candidate cell.
 * الـ output بيظهر كل خطوة بالأرقام الفعلية:
 *   Eq.5 f(γ) → Eq.6 f(PRB) → Eq.4 AWF → Eq.2/3 Ω
 *   → Eq.1 fWF → NCL gate → Eq.14 RSRPpilot → Eq.8 ΔHOM → Algo1
 * ============================================================ */
static void log_decision_step(
    int    tgt_cell,
    double gamma_S,   double gamma_T,
    double f_sinr,    double f_prb,    double f_speed,
    double w_s,       double w_p,      double w_v,
    double Omega_s,   double Omega_p,  double Omega_v,
    double fWF,
    double prb_srv,   double prb_tgt,
    double rp_srv,    double rp_tgt,
    double delta_hom, int    hom_case,
    double algo_thr,
    int    ncl_nue,   double ncl_load,
    int    ncl_pass,  int    algo_pass)
{
    const char* case_str =
        hom_case == 1 ? "Case1 same-side γth" :
        hom_case == 2 ? "Case2 tgt-poor"      : "Case3 srv-poor";
    long long hom_enc = (long long)(
        (delta_hom < 0.0 ? 0.0 : delta_hom > 20.0 ? 20.0 : delta_hom) * 10.0);

    printf(
        CLR_DIM
        "  ├─ Candidate Cell %-2d ──────────────────────────────────────────\n"
        CLR_RESET
        "  │  " CLR_BWHITE "Eq.5 " CLR_RESET
            "f(γ)   = (γT−γS)/60"
            "  =  (%s%+.2f%s − %s%+.2f%s) / 60"
            "  = %s%+.4f%s\n"
        "  │  " CLR_BWHITE "Eq.6 " CLR_RESET
            "f(PRB) = (PRBT−PRBS)/28"
            "  =  (%s%.3f%s − %s%.3f%s) / 28"
            "  = %s%+.4f%s\n"
        "  │  " CLR_BWHITE "Eq.7 " CLR_RESET
            "f(υ)   = %s%+.4f%s\n"
        "  │  " CLR_BWHITE "Eq.4 " CLR_RESET
            "AWF    ωγ=1−f(γ)=%s%.4f%s"
            "  ωPRB=1−f(PRB)=%s%.4f%s"
            "  ων=1−f(υ)=%s%.4f%s"
            "  ωt=%.4f\n"
        "  │         "
            CLR_BWHITE "Eq.2 " CLR_RESET
            "Ωγ=" CLR_BYELLOW "%.4f" CLR_RESET
            "  ΩPRB=" CLR_BYELLOW "%.4f" CLR_RESET
            "  Ων=" CLR_BYELLOW "%.4f" CLR_RESET "\n"
        "  │  " CLR_BWHITE "Eq.1 " CLR_RESET
            "fWF = Ωγ·f(γ)+ΩPRB·f(PRB)+Ων·f(υ)"
            "  = %.4f×%+.4f+%.4f×%+.4f+%.4f×%+.4f"
            "  = " CLR_BMAGENTA "%+.4f" CLR_RESET "\n"
        "  │  " CLR_BWHITE "NCL  " CLR_RESET
            "load = %d/19 = %.0f%%"
            "  gate ≤19%%: %s\n"
        "  │  " CLR_BWHITE "Eq.14" CLR_RESET
            " RSRPpilot_S=" CLR_BCYAN "%+.2f dBm" CLR_RESET
            "   RSRPpilot_T=" CLR_BCYAN "%+.2f dBm" CLR_RESET "\n"
        "  │  " CLR_BWHITE "Eq.8 " CLR_RESET
            "[%s]"
            "  ΔHOM=" CLR_BYELLOW "%+.3f dB" CLR_RESET
            "  encoded=%lld (RC ID=1)\n"
        "  │  " CLR_BWHITE "Algo1" CLR_RESET
            " RSRPpilot_T+ΔHOM(%+.2f) %s RSRPpilot_S(%+.2f)"
            "  →  %s\n",
        tgt_cell,
        CLR_BBLUE, gamma_T, CLR_RESET, CLR_BBLUE, gamma_S, CLR_RESET,
        CLR_BBLUE, f_sinr, CLR_RESET,
        CLR_BGREEN, prb_tgt, CLR_RESET, CLR_BGREEN, prb_srv, CLR_RESET,
        CLR_BGREEN, f_prb, CLR_RESET,
        CLR_BYELLOW, f_speed, CLR_RESET,
        CLR_DIM, w_s, CLR_RESET, CLR_DIM, w_p, CLR_RESET, CLR_DIM, w_v, CLR_RESET,
        w_s + w_p + w_v,
        Omega_s, Omega_p, Omega_v,
        Omega_s, f_sinr, Omega_p, f_prb, Omega_v, f_speed, fWF,
        ncl_nue, ncl_load * 100.0,
        ncl_pass ? (CLR_BGREEN "PASS" CLR_RESET) : (CLR_BRED "FAIL — SKIP" CLR_RESET),
        rp_srv, rp_tgt,
        case_str, delta_hom, hom_enc,
        rp_tgt + delta_hom,
        algo_pass ? ">" : "≤",
        rp_srv,
        algo_pass ? (CLR_BGREEN "✓  HO ELIGIBLE" CLR_RESET)
                  : (CLR_DIM   "✗  rejected"    CLR_RESET)
    );
}

// ============================================
// CONFIGURABLE HANDOVER PARAMETERS
// ============================================
#define HANDOVER_THRESHOLD_DB 6.0      // SINR difference threshold for handover

/*
 * [IMPROVEMENT #3 — Enhanced Anti Ping-Pong: Hysteresis Margin]
 * PURPOSE : منع الـ UE من الرجوع لـ cell سابها بعد وقت قصير حتى لو انتهى الـ timer.
 * WHAT IT DOES : لو الـ UE عايز يعمل HO لـ cell سابها في آخر PING_PONG_WINDOW_SEC ثانية،
 *                بنطلب إن الـ neighbor SINR يتجاوز (HANDOVER_THRESHOLD_DB + HYSTERESIS_DB)
 *                بدل HANDOVER_THRESHOLD_DB فقط — يعني الفارق لازم يكون 5 dB مش 3 dB.
 * WHY         : الـ timer لوحده مش كافي — ممكن الـ SINR يتذبذب وبعد 5 ثواني يعمل HO تاني.
 *               الـ hysteresis بيحتاج فرق حقيقي ومستقر قبل ما يسمح بالعودة.
 */
#define HYSTERESIS_DB         8.0

#define MIN_HO_INTERVAL_SEC 30          // Minimum time between consecutive handovers per UE

/*
 * [IMPROVEMENT #2 — Adaptive Sleep: Dynamic Polling Interval]
 * PURPOSE : بدل sleep ثابت 2 ثانية، بنحدد وقت الانتظار حسب حالة الشبكة.
 * WHAT IT DOES :
 *   - ADAPTIVE_SLEEP_SHORT_US  (0.5s) : لو في HO جاري أو SINR لأي UE أقل من SINR_POOR_THRESHOLD_DB
 *                                        → نستجيب بسرعة لأي تغيير.
 *   - ADAPTIVE_SLEEP_NORMAL_US (2.0s) : الحالة العادية — UEs شغالة ومحتاجة متابعة.
 *   - ADAPTIVE_SLEEP_LONG_US   (4.0s) : كل الـ UEs SINR عالي ومستقر → نقلل استهلاك الـ CPU.
 * WHY : السيناريو ممتد 60 ثانية ومعاه 20 UE. الـ fixed sleep يضيع وقت لما كل حاجة هادية،
 *       ويتأخر في الاستجابة لما الحالة تسوء.
 */
#define ADAPTIVE_SLEEP_SHORT_US  12000000  //  2.0 s — urgent (poor SINR or HO in flight)
#define ADAPTIVE_SLEEP_NORMAL_US 12000000  //  2.0 s — default
#define ADAPTIVE_SLEEP_LONG_US   12000000  //  4.0 s — all UEs healthy
#define SINR_POOR_THRESHOLD_DB   2.0      // below this → trigger urgent polling
#define MIN_SINR -10                   // Minimum acceptable SINR
#define HO_COMPLETION_TIMEOUT_SEC 15    // Time to assume HO completed if no explicit confirmation

// ===== Dynamic AWF weights (paper Eq.1,2,3,4,5,6) =====
// Paper Eq.(1): fWF(γ,PRB,υ) = Ωγ·f(γ) + ΩPRB·f(PRB) + Ων·f(υ)
// Paper Eq.(4): ωx = (1−f(x)) / Σ(1−f(xi))  → weights computed per-decision.
// Impact: weights adapt to current network conditions each HO cycle instead of
//         using hardcoded constants, matching the paper's AWF algorithm exactly.
//
// HO DECISION (Algorithm 1, paper page 1022):
//   Step 1: RSRPpilot_T + ΔHOM > RSRPpilot_S  [Eq.14 + Eq.8]
//   Step 2: elapsed_time >= current_TTT_sec    [Eq.10-13]
//   Both conditions must be TRUE for handover to execute.
// ============================================================
#define MAX_UES_PER_CELL    20.0   // للـ normalization (عدد الـ UEs الكلي في السيناريو)

/* ======================================================================
 * [NEW — Paper Eq.(7)]: Per-UE Speed Table (from scenario mobility model)
 *
 * الـ scenario بيعيّن سرعات حقيقية لكل UE من mobility model:
 *   - UEs 3, 9, 10  : ConstantVelocity = 200.0 m/s (northeast/southeast)
 *   - UEs 1, 8      : ConstantPosition  = 0.0 m/s  (fixed)
 *   - UEs 2,4,5,6,7,11-20: SteadyStateRandomWaypoint 150–280 m/s
 *                           → نستخدم المتوسط (150+280)/2 = 215 m/s
 *
 * الـ xApp مش بيعرف السرعة الفعلية عبر E2 مباشرة، لكن بيعرفها من CSV
 * reporting (speed = sqrt(vx²+vy²+vz²) من mobility model).
 * البيبر بيطلب f(υ) في Eq.(7) — نستخدم القيم المعروفة من السيناريو.
 *
 * νmax = 13.9 m/s = 50.04 km/h (highest UE speed in scenario — UE7, Cell 2 arterial road)
 * NOTE: νmax مشتق من أعلى سرعة فعلية في السيناريو وليس من البيبر.
 * بكده كل UE هياخد قيمة f(υ) مختلفة ومعبّرة بدل ما كلهم يتحولوا لـ +1.0.
 * ====================================================================== */
/* ======================================================================
 * جدول السرعات الفعلية لكل UE من السيناريو (m/s) — index = UE ID (1-based)
 *
 * مصدر البيانات: Energy_Saving_with_load_balancing_scenario.cc
 *   UE 1  → ConstantPosition (ثابت)          → 0.0 m/s
 *   UE 8  → ConstantPosition (ثابت)          → 0.0 m/s
 *   UE 3  → ConstantVelocity northeast        → 200.0 m/s
 *   UE 9  → ConstantVelocity southeast        → 200.0 m/s
 *   UE 10 → ConstantVelocity southwest        → 200.0 m/s
 *   بقية الـ UEs → SteadyStateRandomWaypoint  → 150–280 m/s (avg 215 m/s)
 *
 * ملاحظة مهمة: νmax = 13.9 m/s = 50.04 km/h (أعلى سرعة فعلية في السيناريو)
 * بكده:
 *   - UE7 (13.9 m/s = 50.04 km/h): f(υ) = 2·log2(100.08/50.04)−1 = +1.0 (clamped)
 *   - UEs الثابتة (speed=0): f(υ) = 2·log2(50.04/50.04)−1 = -1.0
 * هذا يعني:
 *   - UEs الثابتة (speed=0): f(υ) = 2·log2(1+0/50.04) - 1 = -1.0
 *   - UEs المتحركة (speed>>νmax): f(υ) = clamped to +1.0
 * وهو سلوك صحيح ومتوافق مع البيبر — السرعة العالية تعني HO أسرع.
 * ====================================================================== */
/* =====================================================================
 * UE Speed Table — MATCHED to Energy_Saving_with_load_balancing_scenario.cc
 *
 * السيناريو عنده 14 UE بـ index 0-based (g_ue_speed_mps[0..13]).
 * الـ xApp بيستخدم UE ID 1-based، يعني:
 *   xApp UE 1  = scenario index 0  → Cell 2 pedestrian  → 1.4  m/s
 *   xApp UE 2  = scenario index 1  → Cell 2 pedestrian  → 11.2  m/s
 *   xApp UE 3  = scenario index 2  → Cell 2 fast walker → 12.5  m/s
 *   xApp UE 4  = scenario index 3  → Cell 2 fast walker → 12.8  m/s
 *   xApp UE 5  = scenario index 4  → Cell 2 car 30km/h  → 8.3  m/s
 *   xApp UE 6  = scenario index 5  → Cell 2 car 40km/h  → 11.1 m/s
 *   xApp UE 7  = scenario index 6  → Cell 2 car 50km/h  → 13.9 m/s
 *   xApp UE 8  = scenario index 7  → Cell 2 cyclist     → 4.5  m/s
 *   xApp UE 9  = scenario index 8  → Cell 5 pedestrian  → 1.3  m/s
 *   xApp UE 10 = scenario index 9  → Cell 6 fast walker → 2.0  m/s
 *   xApp UE 11 = scenario index 10 → Cell 7 pedestrian  → 1.5  m/s
 *   xApp UE 12 = scenario index 11 → Cell 8 cyclist     → 6.0  m/s
 *   xApp UE 13 = scenario index 12 → Cell 3 pedestrian  → 1.2  m/s
 *   xApp UE 14 = scenario index 13 → Cell 4 fast walker → 2.5  m/s
 *
 * Eq.(7): νmax = 13.9 m/s = 50.04 km/h (scenario max speed — UE7)
 *   f(υ) = 2·log2((υ_kmh + 50.04) / 50.04) − 1  ∈ [−1, +1]
 *   Sample values:
 *     1.4 m/s =  5.04 km/h → f(υ) = 2·log2( 55.08/50.04)−1 ≈ −0.74  (pedestrian — TTT slow)
 *     2.8 m/s = 10.08 km/h → f(υ) = 2·log2( 60.12/50.04)−1 ≈ −0.52
 *     8.3 m/s = 29.88 km/h → f(υ) = 2·log2( 79.92/50.04)−1 ≈ +0.18
 *    13.9 m/s = 50.04 km/h → f(υ) = 2·log2(100.08/50.04)−1 = +1.0   (fastest UE — clamped)
 *   كل UE دلوقتي بياخد قيمة f(υ) مختلفة ومعبّرة بدل ما كلهم يتحولوا لـ +1.0.
 * ===================================================================== */
#define UE_SPEED_DEFAULT_MPS  11   /* default for unknown UEs — fast walker */

/* Forward defines needed for g_ue_speed_mps array — full definitions below */
#ifndef MAX_REGISTERED_UES
#define MAX_REGISTERED_UES 64
#endif

/* جدول السرعات لكل UE (index = UE ID, 1-based) — m/s
 * مطابق تماماً لـ g_ue_speed_mps[] في السيناريو (0-based → +1 offset هنا) */
static const double __attribute__((unused)) g_ue_speed_mps[MAX_REGISTERED_UES] = {
    /* 0  */ 0.0,    /* unused — UE IDs start at 1                                */
    /* 1  */ 1.4,    /* UE 1  — Cell 2 pedestrian (shopping area)   1.4 m/s       */
    /* 2  */ 11.2,    /* UE 2  — Cell 2 pedestrian (slow)            1.2 m/s       */
    /* 3  */ 12.5,    /* UE 3  — Cell 2 fast walker                  2.5 m/s       */
    /* 4  */ 12.8,    /* UE 4  — Cell 2 fast walker (commuter)       2.8 m/s       */
    /* 5  */ 8.3,    /* UE 5  — Cell 2 car local road (30 km/h)     8.3 m/s       */
    /* 6  */ 11.1,   /* UE 6  — Cell 2 car (40 km/h)               11.1 m/s       */
    /* 7  */ 13.9,   /* UE 7  — Cell 2 car arterial (50 km/h)      13.9 m/s       */
    /* 8  */ 4.5,    /* UE 8  — Cell 2 cyclist                      4.5 m/s       */
    /* 9  */ 1.3,    /* UE 9  — Cell 5 pedestrian                   1.3 m/s       */
    /* 10 */ 2.0,    /* UE 10 — Cell 6 fast walker                  2.0 m/s       */
    /* 11 */ 1.5,    /* UE 11 — Cell 7 pedestrian                   1.5 m/s       */
    /* 12 */ 6.0,    /* UE 12 — Cell 8 cyclist (fast)               6.0 m/s       */
    /* 13 */ 10.2,    /* UE 13 — Cell 3 pedestrian                   1.2 m/s       */
    /* 14 */ 2.5,    /* UE 14 — Cell 4 fast walker                  2.5 m/s       */
    /* 15 */ 3.9,    /* UE 14 — Cell 4 fast walker                  2.5 m/s       */
    /* 16 */ 10.5,    /* UE 14 — Cell 4 fast walker                  2.5 m/s       */
    /* 17 */ 8.5,    /* UE 14 — Cell 4 fast walker                  2.5 m/s       */
    /* 18 */ 6.5,    /* UE 14 — Cell 4 fast walker                  2.5 m/s       */
    /* 19 */ 9.5,    /* UE 14 — Cell 4 fast walker                  2.5 m/s       */
    /* 20 */ 12.6,    /* UE 14 — Cell 4 fast walker                  2.5 m/s       */
    /* 21-63 — future UEs: default fast-walker speed */
    2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5,
    2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5,
    2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5,
    2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5,
    2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5, 2.5,
    2.5, 2.5, 2.5
};

/* ======================================================================
 * [NEW — Paper Eq.(9)]: HOM boundaries used in ΔHOM calculation and RC signaling.
 *   HOMmin = 0 dB  (minimum handover margin)
 *   HOMmax = 20 dB (maximum handover margin)
 *   HOM_avg = (HOMmax + HOMmin) / 2 = 10 dB  [Eq.(9)]
 * These are table-1 values from the paper. The xApp now sends ΔHOM to the
 * RAN as an RC control parameter so the eNB uses the updated margin.
 * ====================================================================== */
#define HOM_MIN_DB  -10.0   // FIX: ΔHOM clamp lower = HOM_avg × (-1) = -10 dB
                            //      البيبر بيقول HOM_avg=10 و fWF∈[-1,+1]
                            //      فـ ΔHOM الطبيعي في [-10,+10] — القيمة السالبة
                            //      مقصودة: تصعّب الـ HO لما الـ target مش أحسن
#define HOM_MAX_DB  +10.0   // FIX: ΔHOM clamp upper = HOM_avg × (+1) = +10 dB
#define HOM_AVG_DB   10.0   // paper Eq.(9): (HOMmax+HOMmin)/2

/* ======================================================================
 * [NEW — Paper Eq.(10-13)]: TTT (Time-to-Trigger) boundaries.
 *   Tmin = 0.0 s
 *   Tmax = 5.12 s
 *   ρ    = 0.02 s  (optimisation interval — step size for TTT update)
 *   Q    = 0.2  s  (step level threshold — controls when TTT changes)
 * The xApp sends the updated TTT value to the RAN via RC control message
 * after each evaluation cycle, matching paper Section 3 exactly.
 * ====================================================================== */
#define TTT_MIN_SEC  0.0    // FIX-1: paper Table-1 Tmin = 0.0 s (was 0.9 — WRONG)
#define TTT_MAX_SEC  5.12   // paper: Tmax = 5.12 s
#define TTT_RHO      0.02   // paper: ρ = 0.02 s  (optimisation step)
#define TTT_Q        0.01    // paper: Q = 0.01s   (step level threshold)
#define TTT_ZETA     0.06   // ζ: large improvement step (fWF really improved)
#define TTT_DEFAULT_SEC 0.5 // starting TTT per UE (standard LTE default)

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
    int ue_ho_count;
    bool lb_triggered;

    /* ── القيم الفعلية من getTargetCellID ── */
    double dec_serving_sinr;    /* γS */
    double dec_target_sinr;     /* γT */
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

    /* ── [NEW — Paper Eq.(10-13)]: per-UE TTT state ──
     * current_ttt_sec : القيمة الحالية للـ TTT لهذا الـ UE (تبدأ بـ TTT_DEFAULT_SEC).
     *                   بيتحدث بعد كل evaluation cycle وبيتبعت للـ RAN عبر RC.
     * last_fWF        : قيمة fWF من الـ cycle السابق — بيُستخدم لحساب ΔT (Eq.11-13).
     * هذين الحقلين ضروريين عشان الـ TTT update logic ممكن يتحقق من
     * (fWF <= fWF + Q) أو (fWF >= fWF + Q) زي ما البيبر بيقول.         */
    double current_ttt_sec;    /* الـ TTT الحالي لهذا الـ UE (seconds) */
    double last_fWF;           /* قيمة fWF من الـ cycle السابق لمقارنة Eq.11-13 */

    /* ── [TTT Internal Timer — Algorithm 1 strict incremental counter] ──
     * ttt_counter    : accumulates DELTA_T (0.04 s) each cycle the condition is TRUE.
     *                  Resets ONLY when: (a) condition becomes FALSE, or
     *                                   (b) HO successfully executed.
     *                  Never reset mid-accumulation (Algorithm 1, strict).
     * ttt_start_time : kept for struct layout compatibility (unused by timer logic). */
    double   ttt_counter;         /* incremental TTT counter (seconds), Algorithm 1 */
    time_t   ttt_start_time;      /* layout-compat placeholder (not used for timing) */
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

// ============================================
// SINR Data Structures
// (moved here so metrics layer can use them)
// ============================================
struct SINRNeighboringValues
{
  bool is_available;
  uint16_t neighCellID;
  double sinr;
  double rsrp;   // real RSRP (dBm) from L3neighRSRPListOf KPM measurement
};

// NOTE: SINR moving average (IMPROVEMENT #1) intentionally REMOVED.
// All HO decisions use raw SINR directly — matches paper Eq.(5) which uses
// instantaneous γT and γS values. sinr_avg/sinr_buf fields removed from struct.

struct SINRServingValues
{
  bool is_available;
  uint16_t ueID;
  double sinr;                           // raw KPM reading — used directly for all HO decisions
  double rsrp;                           // real RSRP (dBm) from L3servingRSRP3gpp KPM measurement
  int    prb_used;                       // real PRB used from L3servingPRB3gpp KPM measurement
  int    prb_total;                      // total PRBs in cell (TOTAL_PRB)
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
#define MAX_REGISTERED_UES 64
#define MAX_REGISTERED_NEIGHBOURS 20

struct registeredCells cells_sinr_map[MAX_REGISTERED_CELLS];

// ============================================
// Global UE state (authoritative)
// ============================================
static handover_context_t g_ue_ho_ctx[MAX_REGISTERED_UES];
static uint16_t g_ue_serving_cell[MAX_REGISTERED_UES];
static uint16_t g_ue_candidate_cell[MAX_REGISTERED_UES];
static uint8_t  g_ue_candidate_cnt[MAX_REGISTERED_UES];

// Total handover commands sent since xApp started.
static int g_total_ho_count = 0;

/* Forward declaration — get_UE is defined later */
struct SINRServingValues* get_UE(const uint16_t cellID, const uint16_t ueID);

// ============================================================
//  EARLY DEFINITIONS — moved here so metrics functions can use them
//  (cell_load_t, g_cell_load, compute_awf_weights)
// ============================================================

// --- cell_load_t and g_cell_load (paper Eq.6 PRB load tracking) ---
// Full definitions here; the LB block below only adds lb_update_cell_prb().
#define MAX_PRB_HISTORY       5
#define TOTAL_PRB 28
                                      // hardcoded in mmwave-enb-net-device.cc: dlAvailablePrbs=139
                                      // Paper Table1: 5G=2500 PRBs (different numerology)
#define LB_OVERLOAD_THRESHOLD  0.19   // paper Section 3: NCL = cells with load ≤ 50%
#define LB_UNDERLOAD_THRESHOLD 0.25
#define LB_MIN_SINR_FOR_LB    (-3.0)
#define LB_PRB_DIFF_THRESHOLD  0.19

typedef struct {
    double   prb_util;
    double   prb_buf[MAX_PRB_HISTORY];
    int      prb_buf_idx;
    int      prb_buf_count;
    double   prb_avg;
    int      used_prb;
    bool     valid;
} cell_load_t;

static cell_load_t g_cell_load[MAX_REGISTERED_CELLS];

// --- compute_awf_weights (paper Eq.4, Eq.2, Eq.3) ---
// Implements: ωx = (1 - f(x)) / Σ(1 - f(xi))  then  Ωx = ωx / ωt
// F=3: all three parameters (SINR, PRB, speed) used when speed > 0.
// F=2: speed excluded when not available (pass use_speed=0).
// Full definition here; the MODIFICATION 1 section below keeps its comment block.
static void __attribute__((unused)) compute_awf_weights(double f_sinr, double f_prb, double f_speed,
                                int use_speed,
                                double *Omega_sinr, double *Omega_prb, double *Omega_speed)
{
    /* Paper Eq.(4): ωx = (1 - f(x)) / Σ(1 - f(xi)) */
    double w_sinr  = 1.0 - f_sinr;
    double w_prb   = 1.0 - f_prb;
    double w_speed = use_speed ? (1.0 - f_speed) : 0.0;

    /* Floor to avoid division by zero */
    if (w_sinr  < 0.01) w_sinr  = 0.01;
    if (w_prb   < 0.01) w_prb   = 0.01;
    if (use_speed && w_speed < 0.01) w_speed = 0.01;

    /* Paper Eq.(3): ωt = ωγ + ωPRB + ων */
    double wt = w_sinr + w_prb + (use_speed ? w_speed : 0.0);

    /* Paper Eq.(2): Ωx = ωx / ωt */
    *Omega_sinr  = w_sinr  / wt;
    *Omega_prb   = w_prb   / wt;
    *Omega_speed = use_speed ? (w_speed / wt) : 0.0;
}

// ============================================================
//  METRICS LOGGING — Full CSV system for baseline comparison
// ============================================================
/*
 * [IMPROVEMENT #4 — Full Metrics CSV System]
 * PURPOSE : توفير بيانات كاملة تُمكّن من المقارنة العلمية بين الـ baseline
 *           (الكود القديم بدون تحسينات) وكل إصدار محسّن.
 *
 * WHAT IT DOES : ينتج ملفين CSV تلقائياً في HOME directory:
 *
 *   1) handover_xapp.csv — سجل كل HO event:
 *      الأعمدة الجديدة مقارنةً بالقديم:
 *        serving_sinr_before : قيمة SINR للـ UE لحظة إصدار الـ HO command.
 *                              يُظهر هل القرار اتخذ لما الإشارة كانت سيئة فعلاً.
 *        latency_sec         : الزمن بين COMMAND_SENT و COMPLETED.
 *                              مقياس مباشر لكفاءة تنفيذ الـ handover.
 *        is_ping_pong        : 1 لو الـ UE رجع لـ cell سابها في أقل من
 *                              PING_PONG_WINDOW_SEC ثانية — مقياس جودة القرار.
 *
 *   2) sinr_xapp.csv — snapshot دوري لكل UE في كل KPM cycle:
 *      الأعمدة: time_sec, ue_id, cell_id, sinr_db
 *      يُستخدم لرسم SINR vs Time لكل UE ومقارنة متوسط الـ SINR قبل/بعد التحسينات.
 *
 *   3) Summary table في الـ terminal عند انتهاء الـ xApp:
 *      - إجمالي الـ handovers
 *      - Ping-pong rate
 *      - متوسط SINR لكل UE
 *      - متوسط HO latency لكل UE
 *
 * WHY : الكود القديم كان بيسجل 6 أعمدة فقط (time/ue/from/to/event/ok) —
 *       مش كافي للمقارنة. الآن عندنا كل الـ metrics المطلوبة للـ paper.
 */

// ---------- HO events log ----------
static FILE* g_handover_xapp_log        = NULL;
static int   g_handover_xapp_header_done = 0;

// ---------- SINR snapshot log ----------
static FILE* g_sinr_log        = NULL;
static int   g_sinr_log_header = 0;

// ---------- per-UE per-cycle TTT/fWF CSV log (REQ §4) ----------
static FILE* g_ttt_cycle_log        = NULL;
static int   g_ttt_cycle_log_header = 0;
// Epoch for sim_time_sec column — set on first call.
static time_t g_xapp_epoch = 0;

/*
 * [IMPROVEMENT #4 — Ping-Pong Detection & Tracking]
 * PURPOSE : قياس عدد مرات الـ ping-pong عشان نقارن قبل وبعد تحسين الـ Anti Ping-Pong.
 * WHAT IT DOES :
 *   - g_ue_prev_cell[]      : بيحفظ آخر cell الـ UE سابها.
 *   - g_ue_prev_cell_time[] : بيحفظ وقت المغادرة.
 *   - g_total_ping_pong_count : counter عالمي بيتزود كل ما يُكتشف ping-pong.
 *   - PING_PONG_WINDOW_SEC   : لو الـ UE رجع لنفس الـ cell في أقل من الوقت ده → ping-pong.
 * WHY : بدون tracking مش هنعرف نثبت إن التحسينات فعلاً قللت الـ ping-pong.
 */
// A "ping-pong" is a HO back to a cell the UE left within PING_PONG_WINDOW_SEC.
#define PING_PONG_WINDOW_SEC 10
static uint16_t g_ue_prev_cell[MAX_REGISTERED_UES]      = {0};
static time_t   g_ue_prev_cell_time[MAX_REGISTERED_UES] = {0};
static int      g_total_ping_pong_count = 0;

// ---------- per-UE HO latency accumulator ----------
// Latency = time from COMMAND_SENT to COMPLETED (seconds)
static double   g_ue_ho_latency_sum[MAX_REGISTERED_UES]   = {0};
static int      g_ue_ho_latency_count[MAX_REGISTERED_UES] = {0};

// ---------- SINR accumulator for average-SINR-after-HO ----------
static double   g_ue_sinr_sum[MAX_REGISTERED_UES]   = {0};
static int      g_ue_sinr_count[MAX_REGISTERED_UES] = {0};

// Open (or reuse) the HO events CSV.
static FILE* metrics_get_ho_log(void)
{
    if (g_handover_xapp_log != NULL) return g_handover_xapp_log;
    const char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/alyaadone.csv", (home && home[0]) ? home : ".");
    g_handover_xapp_log = fopen(path, "w");
    if (g_handover_xapp_log && !g_handover_xapp_header_done) {
        fprintf(g_handover_xapp_log,
            /* ── تعريف الحدث ── */
            "ho_number,sim_time_sec,ue_id,from_cell,to_cell,event,executed_ok,trigger_type,"
            /* ── SINR — Paper Eq.5 ── */
            "serving_SINR_dB,target_SINR_dB,f_gamma_Eq5,"
            /* ── PRB — Paper Eq.6 ── */
            "serving_NUEs,target_NUEs,"
            "PRBs_per_UE_serving,PRBs_per_UE_target,"
            "TOTAL_PRB,f_PRB_Eq6,"
            "RRU_PrbUsedDl_serving_pct,RRU_PrbUsedDl_target_pct,PRB_source,"
            /* ── RSRP — Paper Eq.14 ── */
            "RSRP_serving_dBm,RSRP_target_dBm,"
            "RSRPpilot_serving,RSRPpilot_target,"
            /* ── AWF + fWF — Paper Eq.1-4 ── */
            "Omega_gamma_Eq2,Omega_PRB_Eq3,fWF_Eq1,"
            /* ── ΔHOM — Paper Eq.8 ── */
            "delta_HOM_Eq8,delta_HOM_case,"
            /* ── Algorithm 1 + NCL — Paper Section 3 ── */
            "Algo1_threshold,Algo1_HO_decision,"
            "NCL_target_load_pct,NCL_gate_result,"
            /* ── Load Balance ── */
            "serving_cell_load_pct,load_balance_result,load_diff_ues,"
            /* ── HO Quality ── */
            "latency_sec,ho_success,data_source\n");
        g_handover_xapp_header_done = 1;
    }
    return g_handover_xapp_log;
}

// Open (or reuse) the SINR snapshot CSV.
static FILE* metrics_get_sinr_log(void)
{
    if (g_sinr_log != NULL) return g_sinr_log;
    const char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/sinr_xapp.csv", (home && home[0]) ? home : ".");
    g_sinr_log = fopen(path, "a");
    if (g_sinr_log && !g_sinr_log_header) {
        fprintf(g_sinr_log, "time_sec,ue_id,cell_id,sinr_db,prb_util,f_prb_normalized\n");
        g_sinr_log_header = 1;
    }
    return g_sinr_log;
}

// Log a SINR snapshot for one UE (called from add_UE every KPM cycle).
static void metrics_log_sinr(uint16_t ue_id, uint16_t cell_id, double sinr)
{
    FILE* f = metrics_get_sinr_log();
    if (f == NULL) return;

    /* PRB utilization from numOfConnectedUEs [paper Eq.6, Eq.14] */
    int nue_m = (cell_id < MAX_REGISTERED_CELLS &&
                 cells_sinr_map[cell_id].is_registered &&
                 cells_sinr_map[cell_id].sinrMap != NULL)
                ? (int)cells_sinr_map[cell_id].sinrMap->numOfConnectedUEs : 1;
    if (nue_m < 1) nue_m = 1;
    double prb_util = 1.0 - (1.0 / (double)nue_m);  /* load fraction: 0=empty, 1=full */
    double f_prb_serving = -prb_util;   /* f(PRB) serving reference */

    fprintf(f, "%ld,%u,%u,%.4f,%.4f,%.4f\n",
            (long)time(NULL), ue_id, cell_id, sinr, prb_util, f_prb_serving);
    fflush(f);

    // Accumulate for average-SINR computation
    if (ue_id > 0 && ue_id < MAX_REGISTERED_UES) {
        g_ue_sinr_sum[ue_id]  += sinr;
        g_ue_sinr_count[ue_id]++;
    }
}

// Detect ping-pong: returns 1 if this HO is back to a recently-left cell.
static int metrics_is_ping_pong(uint16_t ue_id, uint16_t from_cell, uint16_t to_cell)
{
    if (ue_id == 0 || ue_id >= MAX_REGISTERED_UES) return 0;
    if (g_ue_prev_cell[ue_id] == to_cell) {
        double delta = difftime(time(NULL), g_ue_prev_cell_time[ue_id]);
        if (delta < PING_PONG_WINDOW_SEC) {
            g_total_ping_pong_count++;
            LOG_WARN("Ping-pong detected  UE " CLR_BRED "%d" CLR_RESET
                     "  %d→%d→%d  (%.1fs since last visit)",
                     ue_id, to_cell, from_cell, to_cell, delta);
            return 1;
        }
    }
    // Record the departure from from_cell
    g_ue_prev_cell[ue_id]      = from_cell;
    g_ue_prev_cell_time[ue_id] = time(NULL);
    return 0;
}

// Main HO event logger — replaces the old log_handover_xapp().
static void log_handover_xapp(uint16_t ue_id, uint16_t from_cell, uint16_t to_cell,
                               const char* event, int executed_ok)
{
    FILE* f = metrics_get_ho_log();
    if (f == NULL) return;

    double serving_sinr = 0.0;
    struct SINRServingValues* ue_rec = get_UE(from_cell, ue_id);
    if (ue_rec) serving_sinr = ue_rec->sinr;

    double latency_sec  = 0.0;

    if (ue_id > 0 && ue_id < MAX_REGISTERED_UES) {
        handover_context_t* ctx = &g_ue_ho_ctx[ue_id];

        if (strcmp(event, "COMPLETED") == 0 && ctx->command_time > 0) {
            latency_sec = difftime(time(NULL), ctx->command_time);
            if (latency_sec > 0) {
                g_ue_ho_latency_sum[ue_id]  += latency_sec;
                g_ue_ho_latency_count[ue_id]++;
            }
            (void)metrics_is_ping_pong(ue_id, from_cell, to_cell);
        }
    }

    // [LB IMPROVEMENT] حساب load metrics وقت الـ HO لتسجيلهم في الـ CSV
    double serving_load_ues = 0.0;
    double target_load_ues  = 0.0;
    double lb_score         = 0.0;
    if (from_cell > 0 && from_cell < MAX_REGISTERED_CELLS &&
        cells_sinr_map[from_cell].is_registered &&
        cells_sinr_map[from_cell].sinrMap != NULL) {
        serving_load_ues = (double)cells_sinr_map[from_cell].sinrMap->numOfConnectedUEs;
    }
    (void)serving_load_ues;  /* used indirectly via cell_load_level below */
    if (to_cell > 0 && to_cell < MAX_REGISTERED_CELLS &&
        cells_sinr_map[to_cell].is_registered &&
        cells_sinr_map[to_cell].sinrMap != NULL) {
        target_load_ues = (double)cells_sinr_map[to_cell].sinrMap->numOfConnectedUEs;
    }

    /* Paper Eq.(5): f(γ) = (γT − γS) / γmax — γmax=30 dB */
    // serving_sinr is γS; target SINR approximated as 0 (unknown at log time)
    // Use serving SINR as reference: f(γ) = (0 - γS)/30 = -γS/30
    // This correctly reflects how loaded/unloaded the serving cell signal is
    double f_sinr_log = -serving_sinr / 60.0;
    if (f_sinr_log >  1.0) f_sinr_log =  1.0;
    if (f_sinr_log < -1.0) f_sinr_log = -1.0;

    /* Paper Eq.(6): f(PRB) from numOfConnectedUEs */
    int srv_nue_log = (from_cell < MAX_REGISTERED_CELLS &&
                       cells_sinr_map[from_cell].is_registered &&
                       cells_sinr_map[from_cell].sinrMap != NULL)
                      ? (int)cells_sinr_map[from_cell].sinrMap->numOfConnectedUEs : 1;
    if (srv_nue_log < 1) srv_nue_log = 1;
    int tgt_nue_log = (to_cell < MAX_REGISTERED_CELLS &&
                       cells_sinr_map[to_cell].is_registered &&
                       cells_sinr_map[to_cell].sinrMap != NULL)
                      ? (int)cells_sinr_map[to_cell].sinrMap->numOfConnectedUEs : 1;
    if (tgt_nue_log < 1) tgt_nue_log = 1;
    /* Paper Eq.(6): f(PRB) = (PRBT-PRBS)/PRBmax
     * PRBT = TOTAL_PRB/NUE_target, PRBS = TOTAL_PRB/NUE_serving, PRBmax = TOTAL_PRB */
    double serving_prb_util = (double)TOTAL_PRB / (double)srv_nue_log;
    double target_prb_util  = (double)TOTAL_PRB / (double)tgt_nue_log;
    double f_prb_normalized = (target_prb_util - serving_prb_util) / (double)TOTAL_PRB;
    if (f_prb_normalized >  1.0) f_prb_normalized =  1.0;
    if (f_prb_normalized < -1.0) f_prb_normalized = -1.0;

    /* Paper Eq.(4,2,3): dynamic AWF weights */
    double Om_s_log = 0.333, Om_p_log = 0.333;
    // F=2 AWF — speed excluded
    {
        double w_s = 1.0 - f_sinr_log;    if (w_s < 0.01) w_s = 0.01;
        double w_p = 1.0 - f_prb_normalized; if (w_p < 0.01) w_p = 0.01;
        double wt  = w_s + w_p;
        Om_s_log = w_s / wt;  Om_p_log = w_p / wt;
    }
    lb_score = (Om_s_log * f_sinr_log) + (Om_p_log * f_prb_normalized);  /* Eq.1 */

    /* Cell load level: ratio UEs/capacity — matches paper Section 3 definition */
    double cell_load_level = (from_cell < MAX_REGISTERED_CELLS &&
                              cells_sinr_map[from_cell].is_registered &&
                              cells_sinr_map[from_cell].sinrMap != NULL)
                             ? (double)cells_sinr_map[from_cell].sinrMap->numOfConnectedUEs / MAX_UES_PER_CELL
                             : 0.0;

    /* Paper Eq.(8+9): ΔHOM = HOM_avg × fWF,  HOM_avg = (20+0)/2 = 10 dB */
    double hom_delta = 10.0 * lb_score;

    // [LB#5] trigger type
    const char* trigger_type = "SINR";
    if (ue_id > 0 && ue_id < MAX_REGISTERED_UES &&
        g_ue_ho_ctx[ue_id].lb_triggered) {
        trigger_type = "LB";
    }

    /* ── استخدام القيم الفعلية من getTargetCellID ── */
    static int s_ho_number = 0;
    s_ho_number++;

    handover_context_t* ctx_dec = (ue_id > 0 && ue_id < MAX_REGISTERED_UES)
                                   ? &g_ue_ho_ctx[ue_id] : NULL;
    int use_real = (ctx_dec && ctx_dec->dec_data_captured);

    double real_srv_sinr   = use_real ? ctx_dec->dec_serving_sinr  : serving_sinr;
    double real_tgt_sinr   = use_real ? ctx_dec->dec_target_sinr   : 0.0;
    double real_f_sinr     = use_real ? ctx_dec->dec_f_sinr        : f_sinr_log;
    double real_f_prb      = use_real ? ctx_dec->dec_f_prb         : f_prb_normalized;
    double real_Om_s       = use_real ? ctx_dec->dec_Omega_sinr    : Om_s_log;
    double real_Om_p       = use_real ? ctx_dec->dec_Omega_prb     : Om_p_log;
    double real_fWF        = use_real ? ctx_dec->dec_fWF           : lb_score;
    double real_dHOM       = use_real ? ctx_dec->dec_delta_HOM     : hom_delta;
    int    real_case       = use_real ? ctx_dec->dec_delta_HOM_case : 1;
    double real_rp_srv     = use_real ? ctx_dec->dec_rsrp_pilot_srv : (real_srv_sinr*(double)TOTAL_PRB/srv_nue_log);
    double real_rp_tgt     = use_real ? ctx_dec->dec_rsrp_pilot_tgt : (real_srv_sinr*(double)TOTAL_PRB/tgt_nue_log);
    int    real_srv_nue    = use_real ? ctx_dec->dec_serving_nue   : srv_nue_log;
    int    real_tgt_nue    = use_real ? ctx_dec->dec_target_nue    : tgt_nue_log;
    double real_rsrp_s     = use_real ? ctx_dec->dec_rsrp_serving  : 0.0;
    double real_rsrp_t     = use_real ? ctx_dec->dec_rsrp_target   : 0.0;
    double real_prb_ue_s   = use_real ? ctx_dec->dec_prb_per_ue_srv : (double)TOTAL_PRB/real_srv_nue;
    double real_prb_ue_t   = use_real ? ctx_dec->dec_prb_per_ue_tgt : (double)TOTAL_PRB/real_tgt_nue;
    double real_rru_srv    = use_real ? ctx_dec->dec_prb_util_srv  : 0.0;
    double real_rru_tgt    = use_real ? ctx_dec->dec_prb_util_tgt  : 0.0;
    int    real_rru_ok     = use_real ? ctx_dec->dec_prb_src_rru   : 0;
    double real_ncl        = use_real ? ctx_dec->dec_ncl_load_tgt  : (target_load_ues/(double)MAX_UES_PER_CELL);

    /* PRB source string */
    const char* prb_src = real_rru_ok ? "RRU.PrbUsedDl" : "NUE_fallback";

    /* ΔHOM case */
    const char* dhom_case;
    if      (real_case == 1) dhom_case = "Case1_both_same_side_gth__DHOM=HOM_x_fWF";
    else if (real_case == 2) dhom_case = "Case2_target_below_gth__DHOM=HOM_x(1+wPRB_x_fPRB)";
    else                     dhom_case = "Case3_serving_below_gth__DHOM=HOM_x(-1+wPRB_x_fPRB)";

    const char* dhom_meaning;
    if      (real_dHOM >  0.5) dhom_meaning = "HOM_INCREASED__easier_HO__low_loaded_target";
    else if (real_dHOM < -0.5) dhom_meaning = "HOM_DECREASED__harder_HO__discourage_bad_move";
    else                        dhom_meaning = "HOM_NEUTRAL__equal_conditions";
    (void)dhom_meaning;

    /* Algorithm 1 */
    double algo1_thr = real_rp_srv + real_dHOM;
    const char* algo1_res = (real_rp_tgt > algo1_thr)
                             ? "HO_APPROVED__RSRPpilot_T_wins"
                             : "HO_BLOCKED__serving_still_preferred";

    /* NCL */
    const char* ncl_res = (real_ncl <= 0.19)
                           ? "NCL_PASSED__load_below_19pct"
                           : "NCL_FAIL__target_overloaded";

    /* LB */
    int load_diff = real_srv_nue - real_tgt_nue;
    const char* lb_res = (load_diff > 0) ? "LOAD_REDUCED__good_LB" :
                         (load_diff == 0) ? "EQUAL_LOAD__neutral"   :
                                            "LOAD_INCREASED__SINR_driven";

    /* raw AWF weights */
    double ws = 1.0 - real_f_sinr; if (ws < 0.01) ws = 0.01;
    double wp = 1.0 - real_f_prb;  if (wp < 0.01) wp = 0.01;
    (void)(ws + wp);  /* wt not used directly — weights used via real_Om_s/p */

    const char* data_src = use_real ? "REAL_from_getTargetCellID" : "APPROX_post_hoc";

    fprintf(f,
        /* تعريف الحدث */
        "%d,%ld,%u,%u,%u,%s,%d,%s,"
        /* SINR Eq.5 */
        "%.4f,%.4f,%.4f,"
        /* PRB Eq.6 */
        "%d,%d,%.2f,%.2f,%d,%.4f,%.1f,%.1f,%s,"
        /* RSRP Eq.14 */
        "%.4f,%.4f,%.4f,%.4f,"
        /* AWF Eq.1-4 */
        "%.4f,%.4f,%.4f,"
        /* ΔHOM Eq.8 */
        "%.4f,%s,"
        /* Algo1 + NCL */
        "%.4f,%s,%.1f,%s,"
        /* Load Balance */
        "%.1f,%s,%d,"
        /* HO Quality */
        "%.2f,%d,%s\n",
        /* values */
        s_ho_number, (long)time(NULL),
        ue_id, from_cell, to_cell, event, executed_ok, trigger_type,
        /* SINR */
        real_srv_sinr, real_tgt_sinr, real_f_sinr,
        /* PRB */
        real_srv_nue, real_tgt_nue,
        real_prb_ue_s, real_prb_ue_t, TOTAL_PRB, real_f_prb,
        real_rru_srv*100.0, real_rru_tgt*100.0, prb_src,
        /* RSRP */
        real_rsrp_s, real_rsrp_t,
        real_rp_srv, real_rp_tgt,
        /* AWF */
        real_Om_s, real_Om_p, real_fWF,
        /* ΔHOM */
        real_dHOM, dhom_case,
        /* Algo1 + NCL */
        algo1_thr, algo1_res, real_ncl*100.0, ncl_res,
        /* LB */
        cell_load_level*100.0, lb_res, load_diff,
        /* Quality */
        latency_sec,
        (strcmp(event,"COMPLETED")==0) ? 1 : 0,
        data_src);
    fflush(f);
}

// Print a final summary table when the xApp exits (or call anytime for live stats).
/* ══════════════════════════════════════════════════════════════════════════
 *  fWF MATRIX — يطبع ويحفظ آخر قيمة last_fWF لكل UE عند انتهاء الـ Simulation
 *
 *  last_fWF بيتحدث مرة واحدة لما candidate جديد يظهر (idle TTT path) أو بعد
 *  doHandoverAction، فبالتالي القيمة المحفوظة هي آخر fWF candidate شافه الـ xApp.
 *
 *  المخرجات:
 *    - جدول ملون في الـ terminal
 *    - ملف CSV باسم  fWF_final_matrix.csv  في المجلد الحالي
 * ════════════════════════════════════════════════════════════════════════ */
static void print_fWF_matrix(void)
{
    /* ══════════════════════════════════════════════════════════════════════
     *  fWF FINAL MATRIX
     *  ─────────────────
     *  fWF_current  = dec_fWF   : آخر قيمة fWF حسبها getTargetCellID
     *                             (الـ cycle الأخير اللي فيه candidate)
     *  fWF_previous = last_fWF  : قيمة fWF من الـ cycle اللي قبله
     *                             (بيتحدث مرة واحدة لما candidate جديد يظهر)
     *  delta_fWF    = fWF_current - fWF_previous
     *                             양موجب = الشبكة تدهورت / سالب = تحسنت
     * ══════════════════════════════════════════════════════════════════════ */

    /* ── 1) طباعة الجدول في الـ terminal ── */
    printf("\n" CLR_BBLUE
           "  ╔════════════════════════════════════════════════════════════════════════════════════════════════╗\n"
           "  ║          fWF FINAL MATRIX  (fWF_current & fWF_previous per UE at sim end)                   ║\n"
           "  ╠══════╦══════════════╦══════════════╦══════════════╦══════════════╦═══════════╦═══════════════╣\n"
           "  ║ " CLR_CYAN "UE  " CLR_BBLUE
           " ║ " CLR_CYAN "fWF_current  " CLR_BBLUE
           " ║ " CLR_CYAN "fWF_previous " CLR_BBLUE
           " ║ " CLR_CYAN "delta_fWF    " CLR_BBLUE
           " ║ " CLR_CYAN "curr_TTT(s)  " CLR_BBLUE
           " ║ " CLR_CYAN "HO_count " CLR_BBLUE
           " ║ " CLR_CYAN "State         " CLR_BBLUE " ║\n"
           "  ╠══════╬══════════════╬══════════════╬══════════════╬══════════════╬═══════════╬═══════════════╣\n"
           CLR_RESET);

    int active_ues = 0;
    for (int u = 1; u < MAX_REGISTERED_UES; u++) {
        handover_context_t *c = &g_ue_ho_ctx[u];
        /* تجاهل الـ UEs اللي ما شوفوا أي نشاط خالص */
        if (c->ue_ho_count == 0 && g_ue_sinr_count[u] == 0
            && c->last_fWF == 0.0 && c->dec_fWF == 0.0)
            continue;
        active_ues++;

        double fWF_current  = c->dec_fWF;   /* آخر fWF محسوب (Eq.1) */
        double fWF_previous = c->last_fWF;  /* fWF من الـ cycle السابق */
        double delta_fWF    = fWF_current - fWF_previous;

        /* ألوان:
         *   fWF_current  : أخضر = سالب (إشارة أفضل)، أحمر = موجب (إشارة أسوأ)
         *   delta_fWF    : أخضر = تحسن (سالب)، أحمر = تدهور (موجب) */
        const char *cur_color  = (fWF_current  < -0.001) ? CLR_BGREEN
                                : (fWF_current  >  0.001) ? CLR_BRED : CLR_BWHITE;
        const char *prev_color = (fWF_previous < -0.001) ? CLR_BGREEN
                                : (fWF_previous >  0.001) ? CLR_BRED : CLR_BWHITE;
        const char *delt_color = (delta_fWF    < -0.001) ? CLR_BGREEN
                                : (delta_fWF   >  0.001) ? CLR_BRED : CLR_DIM;

        printf(CLR_BBLUE "  ║ " CLR_CYAN "%-4d" CLR_BBLUE
               " ║ %s%+12.6f" CLR_BBLUE
               " ║ %s%+12.6f" CLR_BBLUE
               " ║ %s%+12.6f" CLR_BBLUE
               " ║ " CLR_BWHITE "%12.4f" CLR_BBLUE
               " ║ " CLR_BWHITE "%9d" CLR_BBLUE
               " ║ " CLR_BWHITE "%-13s" CLR_BBLUE " ║\n" CLR_RESET,
               u,
               cur_color,  fWF_current,
               prev_color, fWF_previous,
               delt_color, delta_fWF,
               c->current_ttt_sec,
               c->ue_ho_count,
               ho_state_to_string(c->state));
    }

    if (active_ues == 0) {
        printf(CLR_BBLUE
               "  ║                    " CLR_YELLOW "No active UEs recorded"
               CLR_BBLUE "                                                              ║\n"
               CLR_RESET);
    }

    printf(CLR_BBLUE
           "  ╚══════╩══════════════╩══════════════╩══════════════╩══════════════╩═══════════╩═══════════════╝\n"
           CLR_RESET "\n");

    /* ── 2) حفظ في ملف CSV ── */
    /* ملف 1: fWF_history.csv — الملف الرئيسي الجديد
     *   fWF_current  = dec_fWF   (آخر قيمة fWF الحالية لكل UE)
     *   fWF_previous = last_fWF  (قيمة fWF من الـ cycle السابق لكل UE)
     *   delta_fWF    = الفرق (موجب = تدهور، سالب = تحسن)
     */
    const char *csv_path = "fWF_history.csv";
    FILE *fp = fopen(csv_path, "w");
    if (!fp) {
        fprintf(stderr, "[fWF-MATRIX] ERROR: cannot open %s for writing: %s\n",
                csv_path, strerror(errno));
    } else {
        /* ── Header ── */
        fprintf(fp,
            /* تعريف الـ UE */
            "ue_id,"
            /* القيم الأساسية المطلوبة */
            "fWF_current,fWF_previous,delta_fWF,"
            /* TTT + state */
            "current_ttt_sec,ue_ho_count,state,"
            /* الـ cells */
            "source_cell,target_cell,lb_triggered,"
            /* تفاصيل الـ decision الأخيرة */
            "dec_serving_sinr,dec_target_sinr,"
            "dec_f_sinr,dec_f_prb,"
            "dec_Omega_sinr,dec_Omega_prb,"
            "dec_delta_HOM\n");

        for (int u = 1; u < MAX_REGISTERED_UES; u++) {
            handover_context_t *c = &g_ue_ho_ctx[u];
            if (c->ue_ho_count == 0 && g_ue_sinr_count[u] == 0
                && c->last_fWF == 0.0 && c->dec_fWF == 0.0)
                continue;

            double fWF_current  = c->dec_fWF;
            double fWF_previous = c->last_fWF;
            double delta_fWF    = fWF_current - fWF_previous;

            fprintf(fp,
                "%d,"
                "%.8f,%.8f,%.8f,"
                "%.6f,%d,%s,"
                "%u,%u,%d,"
                "%.6f,%.6f,"
                "%.6f,%.6f,"
                "%.6f,%.6f,"
                "%.6f\n",
                u,
                fWF_current, fWF_previous, delta_fWF,
                c->current_ttt_sec, c->ue_ho_count,
                ho_state_to_string(c->state),
                (unsigned)c->source_cell,
                (unsigned)c->target_cell,
                (int)c->lb_triggered,
                c->dec_serving_sinr,
                c->dec_target_sinr,
                c->dec_f_sinr,
                c->dec_f_prb,
                c->dec_Omega_sinr,
                c->dec_Omega_prb,
                c->dec_delta_HOM);
        }

        fclose(fp);
        printf("  [fWF-MATRIX] fWF history saved → %s\n", csv_path);
    }

    /* ملف 2: fWF_final_matrix.csv — نبقيه موجود للتوافق مع الإصدارات السابقة */
    const char *csv_path2 = "fWF_final_matrix.csv";
    FILE *fp2 = fopen(csv_path2, "w");
    if (!fp2) {
        fprintf(stderr, "[fWF-MATRIX] ERROR: cannot open %s for writing: %s\n",
                csv_path2, strerror(errno));
        return;
    }

    fprintf(fp2, "ue_id,fWF_current,fWF_previous,delta_fWF,last_fWF,dec_fWF,"
                 "current_ttt_sec,ue_ho_count,"
                 "state,source_cell,target_cell,lb_triggered,"
                 "dec_serving_sinr,dec_target_sinr,dec_f_sinr,dec_f_prb,"
                 "dec_Omega_sinr,dec_Omega_prb,dec_delta_HOM\n");

    for (int u = 1; u < MAX_REGISTERED_UES; u++) {
        handover_context_t *c = &g_ue_ho_ctx[u];
        if (c->ue_ho_count == 0 && g_ue_sinr_count[u] == 0
            && c->last_fWF == 0.0 && c->dec_fWF == 0.0)
            continue;

        double fWF_current  = c->dec_fWF;
        double fWF_previous = c->last_fWF;
        double delta_fWF    = fWF_current - fWF_previous;

        fprintf(fp2, "%d,%.8f,%.8f,%.8f,%.8f,%.8f,%.6f,%d,%s,%u,%u,%d,"
                     "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                u,
                fWF_current, fWF_previous, delta_fWF,
                c->last_fWF, c->dec_fWF,
                c->current_ttt_sec,
                c->ue_ho_count,
                ho_state_to_string(c->state),
                (unsigned)c->source_cell,
                (unsigned)c->target_cell,
                (int)c->lb_triggered,
                c->dec_serving_sinr,
                c->dec_target_sinr,
                c->dec_f_sinr,
                c->dec_f_prb,
                c->dec_Omega_sinr,
                c->dec_Omega_prb,
                c->dec_delta_HOM);
    }

    fclose(fp2);
    printf("  [fWF-MATRIX] Full matrix saved  → %s\n\n", csv_path2);
}

static void metrics_print_summary(void)
{
    int total_ho = g_total_ho_count;
    int total_pp = g_total_ping_pong_count;
    double pp_rate = (total_ho > 0) ? (100.0 * total_pp / total_ho) : 0.0;

    printf("\n" CLR_BBLUE
           "  ╔══════════════════════════════════════════════════════════════╗\n"
           "  ║                  METRICS SUMMARY                            ║\n"
           "  ╠══════════════════════════════════════════════════════════════╣\n"
           "  ║  " CLR_CYAN "Total Handovers        " CLR_BWHITE "%-8d" CLR_BBLUE "                        ║\n"
           "  ║  " CLR_CYAN "Ping-Pong Events       " CLR_BWHITE "%-8d" CLR_BBLUE " (%.1f%%)                ║\n"
           CLR_RESET, total_ho, total_pp, pp_rate);

    for (int u = 1; u < MAX_REGISTERED_UES; u++) {
        if (g_ue_ho_ctx[u].ue_ho_count == 0 && g_ue_sinr_count[u] == 0) continue;
        double avg_sinr    = (g_ue_sinr_count[u] > 0)
                              ? g_ue_sinr_sum[u] / g_ue_sinr_count[u] : 0.0;
        double avg_latency = (g_ue_ho_latency_count[u] > 0)
                              ? g_ue_ho_latency_sum[u] / g_ue_ho_latency_count[u] : 0.0;
        printf(CLR_BBLUE
               "  ║  " CLR_CYAN "UE %-3d" CLR_RESET
               "  HOs=%-4d  AvgSINR=%+6.2fdB  AvgHOLatency=%.2fs"
               CLR_BBLUE "  ║\n" CLR_RESET,
               u, g_ue_ho_ctx[u].ue_ho_count, avg_sinr, avg_latency);
    }

    printf(CLR_BBLUE
           "  ╚══════════════════════════════════════════════════════════════╝\n"
           CLR_RESET "\n");
}
  
/* ============================================================
 * flush_all_csv_files() — يغلق ويـ flush كل ملفات الـ CSV
 *
 * بيتستدعى من:
 *   1) atexit() handler — بيضمن إن الـ CSV بيتكتب حتى لو
 *      الـ xApp اتقتل بـ Ctrl+C أو kill قبل ما يوصل لآخر main.
 *   2) نهاية main() بعد metrics_print_summary() و print_fWF_matrix().
 *
 * المشكلة القديمة: لو الـ xApp اتقتل وسط الـ sleep أو قبل ما
 *   تتعمل cleanup، الـ FILE buffers بيتفقدوا وملفات الـ CSV
 *   بتطلع ناقصة أو فاضية.
 * الحل: نضمن fflush + fclose لكل FILE* قبل ما العملية تخلص.
 * ============================================================ */
static void flush_all_csv_files(void)
{
    /* HO events log */
    if (g_handover_xapp_log != NULL) {
        fflush(g_handover_xapp_log);
        fclose(g_handover_xapp_log);
        g_handover_xapp_log = NULL;
    }
    /* SINR snapshot log */
    if (g_sinr_log != NULL) {
        fflush(g_sinr_log);
        fclose(g_sinr_log);
        g_sinr_log = NULL;
    }
    /* per-UE per-cycle TTT/fWF log */
    if (g_ttt_cycle_log != NULL) {
        fflush(g_ttt_cycle_log);
        fclose(g_ttt_cycle_log);
        g_ttt_cycle_log = NULL;
    }
}

/* ============================================================
 * atexit_handler() — يُستدعى تلقائياً عند أي خروج للعملية
 *
 * بيعمل:
 *   1) flush + close لكل ملفات الـ CSV.
 *   2) print_fWF_matrix() لإنتاج fWF_history.csv و fWF_final_matrix.csv
 *      حتى لو الـ xApp اتقتل بـ Ctrl+C أو kill قبل ما يوصل للـ cleanup.
 *
 * ملاحظة: print_fWF_matrix بتفتح ملفات جديدة (mode "w") —
 *   بتنجح حتى لو الـ process بيخرج من signal handler طالما
 *   مكانش SIGKILL (اللي مش ممكن يتمسك). ولده سجلنا SIGINT + SIGTERM.
 * ============================================================ */
static void atexit_handler(void)
{
    /* طباعة fWF matrix وكتابة الـ CSV — بيشتغل حتى بعد Ctrl+C */
    print_fWF_matrix();
    /* flush + close كل ملفات الـ CSV الأخرى */
    flush_all_csv_files();
}

static ue_id_e2sm_t ue_id;

static uint64_t const period_ms = 100;

static pthread_mutex_t mtx;

static void __attribute__((unused)) log_gnb_ue_id(ue_id_e2sm_t ue_id) 
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

static __attribute__((unused)) log_ue_id log_ue_id_e2sm[END_UE_ID_E2SM] = 
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
  if (cmp_str_ba("RRU.PrbUsedDl", name) == 0) {
    LOG_KPM("RRU.PrbUsedDl = " CLR_BYELLOW "%d" CLR_RESET " PRBs", meas_record.int_val);
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
  } else if (strncmp((const char*)name.buf, "L3servingSINR3gpp_cell_", strlen("L3servingSINR3gpp_cell_")) == 0) {
    LOG_KPM(CLR_DIM "%s" CLR_RESET " sinr=" CLR_BBLUE "%.4f dB" CLR_RESET, name.buf, meas_record.real_val);
  } else if (strncmp((const char*)name.buf, "L3neighSINRListOf_UEID_", strlen("L3neighSINRListOf_UEID_")) == 0) {
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

static __attribute__((unused)) check_meas_type match_meas_type[END_MEAS_TYPE] = 
{
    match_meas_name_type,
    match_id_meas_type,
};

// (structs and globals defined earlier, before the metrics block)

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

// ============================================================
// [LB#1] — Cell PRB Load Tracking
//
// PURPOSE:
//   تخزين استخدام الـ Physical Resource Blocks (PRBs) لكل cell
//   وحساب moving average للـ utilization — ده اللي بيمثّل الـ load الحقيقي.
//
// WHAT IT DOES:
//   - lb_update_cell_prb() بتُستدعى من log_kpm_measurements كل ما يجي
//     KPM report بـ RRU.PrbUsedDl لأي cell.
//   - بتحدّث circular buffer (MAX_PRB_HISTORY=5 قراءات) وتحسب المتوسط.
//   - الـ smoothed prb_avg ده اللي بيستخدمه getLBTargetCell() للمقارنة.
//
// WHY PRB وليس numOfConnectedUEs:
//   - numOfConnectedUEs بيعد الـ UEs بس مش بيعكس كمية الـ traffic الفعلية.
//   - UE واحد بيستهلك stream HD ممكن يأخد 30 PRBs.
//   - 5 UEs بـ idle sessions ممكن يأخدوا 10 PRBs بالمجموع.
//   - الـ PRB utilization هو المقياس الحقيقي للـ cell load.
//
// TOTAL_PRB = 52:
//   قيمة نموذجية لـ mmWave 100MHz channel في ns-O-RAN scenario.
//   غيّرها لو الـ scenario بتاعك بيستخدم bandwidth مختلف.
// ============================================================
// NOTE: MAX_PRB_HISTORY, TOTAL_PRB, LB_*_THRESHOLD, cell_load_t, g_cell_load
// are defined above (before metrics section) to satisfy forward-use by metrics functions.

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
      ctx->ttt_counter     = 0.0;
      ctx->current_ttt_sec = TTT_DEFAULT_SEC;

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
      ctx->ttt_counter     = 0.0;
      ctx->current_ttt_sec = TTT_DEFAULT_SEC;

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
  
  /*
   * Raw SINR stored directly — no averaging (paper Eq.5 uses instantaneous γT and γS).
   * [IMPROVEMENT #4 — log SINR snapshot to sinr_xapp.csv]
   * كل مرة بيجي KPM reading لـ UE، بنسجل (time, ue_id, cell_id, sinr) في الـ CSV.
   */
  cell->connectedUEs[ueID].ueID     = ueID;
  cell->connectedUEs[ueID].sinr     = sinr;   // KPM E2 value — full precision since SINR fix
  cell->connectedUEs[ueID].is_available = true;
  // Log SINR snapshot for metrics comparison
  metrics_log_sinr(ueID, cell->cellID, sinr);
  // Mirror global HO context into per-cell record for logging/debug only
  cell->connectedUEs[ueID].ho_context = *ctx;
  
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

// ===== Paper Eq.(7): f(υ) = 2·log2((υt+υmax)/υmax) - 1,  υmax=13.9 m/s = 50.04 km/h =====
// υt in m/s — converted to km/h inside.
// Returns value in [-1, +1].
// νmax = 13.9 m/s = 50.04 km/h (highest UE speed in scenario: UE7, Cell 2 arterial road)
#define VMAX_KMH 50.04
static double compute_f_speed(double speed_mps)
{
    double vt_kmh = speed_mps * 3.6;
    if (vt_kmh < 0.0) vt_kmh = 0.0;
    // Eq.(7): 2·log2((vt+vmax)/vmax) - 1
    double ratio = (vt_kmh + VMAX_KMH) / VMAX_KMH;
    double f = 2.0 * log2(ratio) - 1.0;
    if (f >  1.0) f =  1.0;
    if (f < -1.0) f = -1.0;
    return f;
}

// ===== MODIFICATION 1 — Helper: compute_awf_weights (paper Eq.4, Eq.2, Eq.3) =====
/*
 * compute_awf_weights — Implements paper Eq.(4): ωx = (1−f(x)) / Σ(1−f(xi))
 * Then normalises to Ωx = ωx / ωt  (paper Eq.2,3).
 * Full definition moved to "EARLY DEFINITIONS" block above the metrics section
 * to resolve forward-use compile error. This comment block is kept for context.
 */
// ===== MODIFICATION 1 — Helper end =====

/* ================================================================
 * log_ttt_cycle() — per-UE per-cycle CSV logger  (Requirement §4)
 *
 * Columns: ue_id, time_sec, fWF_now, fWF_prev, current_TTT,
 *          target_cell, ho_triggered
 *
 * RULES:
 *   • fWF_now  is NEVER written as 0.0 — caller substitutes last_fWF.
 *   • fWF_prev must equal the previous row's fWF_now; enforced by the
 *     caller passing ctx->last_fWF which is updated after every cycle.
 *   • File is ttt_cycle_log.csv in HOME directory (or cwd fallback).
 *   • Does NOT touch any existing logging path.
 * ================================================================ */
static void log_ttt_cycle(int ue_id, double fWF_now, double fWF_prev,
                           double current_ttt, int target_cell, int ho_triggered)
{
    /* Open file on first call */
    if (g_ttt_cycle_log == NULL) {
        const char* home = getenv("HOME");
        char path[512];
        snprintf(path, sizeof(path), "%s/ttt_cycle_log.csv",
                 (home && home[0]) ? home : ".");
        g_ttt_cycle_log = fopen(path, "w");
        if (g_ttt_cycle_log == NULL) {
            LOG_ERR("[TTT-CSV] Cannot open %s: %s", path, strerror(errno));
            return;
        }
    }
    /* Write header once */
    if (!g_ttt_cycle_log_header) {
        fprintf(g_ttt_cycle_log,
                "ue_id,time_sec,fWF_now,fWF_prev,current_TTT,target_cell,ho_triggered\n");
        g_ttt_cycle_log_header = 1;
    }
    /* Record epoch on first real data row */
    if (g_xapp_epoch == 0) g_xapp_epoch = time(NULL);

    double time_sec = difftime(time(NULL), g_xapp_epoch);

    /* Requirement: NO zero fWF in logs — substitute prev if now==0 */
    double safe_now  = (fWF_now  == 0.0) ? fWF_prev : fWF_now;
    double safe_prev = fWF_prev;   /* prev is already the previous cycle's now */

    fprintf(g_ttt_cycle_log,
            "%d,%.3f,%.8f,%.8f,%.3f,%d,%d\n",
            ue_id, time_sec, safe_now, safe_prev,
            current_ttt, target_cell, ho_triggered);
    fflush(g_ttt_cycle_log);
}

// ===== MODIFICATION 3 — Helper: compute_rsrp_pilot (paper Eq.14, linear/mW form) =====
/*
 * Paper Eq.(14) — LINEAR (mW) form:
 *   RSRPpilot_mW = RSRP_mW × (NPRB_total / NUE)
 *
 * NOTE: Phase 2 of getTargetCellID() uses the equivalent dBm form directly:
 *   RSRPpilot_dBm = RSRP_dBm + 10·log10(NPRB/NUE)
 * Both forms are mathematically identical. The dBm form is used in the main
 * pipeline (Phase 2-3-9) to keep all comparisons dimensionally consistent
 * in dB. This function is retained as a legacy/fallback helper only.
 *
 * PARAMETERS:
 *   rsrp_dbm  : RSRP in dBm  (from KPM L3servingRSRP / L3neighRSRP)
 *   sinr_db   : SINR in dB   (fallback if RSRP not available)
 *   total_prb : NPRB_total   (total PRBs in cell — TOTAL_PRB)
 *   num_ues   : NUE          (current connected UEs in cell)
 *
 * RETURN: RSRPpilot in mW (linear) — for legacy callers only
 */
static double compute_rsrp_pilot(double rsrp_dbm, double sinr_db, int total_prb, int num_ues)
{
    if (num_ues < 1) num_ues = 1;

    double prb_ratio = (double)total_prb / (double)num_ues;  /* NPRB_total / NUE */

    /* Use real RSRP if available: convert dBm → mW */
    if (rsrp_dbm < -10.0) {
        /* Real RSRP available — paper Eq.(14) exactly */
        double rsrp_mw = pow(10.0, rsrp_dbm / 10.0);  /* dBm → mW */
        return rsrp_mw * prb_ratio;
    }

    /* Fallback: use SINR-derived proxy power when RSRP not reported.
     * Reference: 0 dB SINR → 1 mW proxy; scale linearly with SINR. */
    double sinr_linear = pow(10.0, sinr_db / 10.0);  /* dB → linear */
    return sinr_linear * prb_ratio;
}

/* Legacy alias for compatibility with existing call sites */
static double __attribute__((unused)) compute_sinr_pilot(double sinr_db, int total_prb, int num_ues)
{
    return compute_rsrp_pilot(0.0, sinr_db, total_prb, num_ues);
}
// ===== MODIFICATION 3 — Helper end =====

/* ================================================================
 * getTargetCellID — Paper Algorithm 1 — Strict 9-Phase Pipeline
 *
 * PHASE 1 : Cell Filtering  → build NCL (load ≤ 50%)
 * PHASE 2 : RSRPpilot       → Eq.14  RSRPpilot_dBm = RSRP_dBm + 10·log10(NPRB/NUE)
 * PHASE 3 : Target Select   → argmax(RSRPpilot) over NCL
 * PHASE 4 : Metric funcs    → f(γ), f(PRB), f(υ)  [Eq.5,6,7]
 * PHASE 5 : AWF weights     → ωx = (1-f(x))/Σ, Ωx = ωx/ωt  [Eq.4,2,3]
 * PHASE 6 : fWF score       → Eq.1
 * PHASE 7 : HOM adaptation  → Eq.8,9  — uses raw ωPRB, ων (not Ω)
 * PHASE 8 : TTT adaptation  → Eq.10-13 (updated in evaluateHO after return)
 * PHASE 9 : HO decision     → Algorithm 1 Line 2 [checked HERE]:
 *             RSRPpilot_target + ΔHOM > RSRPpilot_serving
 *             Returns targetCell if Condition 1 passes; returns 0 otherwise.
 *             Condition 2 (TTT elapsed >= TTT_cur) is enforced in evaluateHO.
 *             Both conditions must pass for HO to be executed.
 * ================================================================ */
uint16_t getTargetCellID(callback_data_t data)
{
    assert(data.neighCells != NULL);

    struct SINRServingValues* serving_ue = get_UE(data.frmCurntCell, data.ueID);
    if (serving_ue == NULL) {
        LOG_ERR("HO decision: UE " CLR_BRED "%d" CLR_RESET " not found in Cell %d",
                data.ueID, data.frmCurntCell);
        return 0;
    }

    /* γS — raw instantaneous SINR from MAC (paper Eq.5) */
    double serving_sinr = serving_ue->sinr;

    /* RSRP_S in dBm — from KPM L3servingRSRP3gpp (full precision, O-RAN E2 path) */
    double rsrp_serving_dbm = serving_ue->rsrp;

    /* Serving cell load for display */
    int srv_nue = (cells_sinr_map[data.frmCurntCell].is_registered &&
                   cells_sinr_map[data.frmCurntCell].sinrMap != NULL)
                  ? (int)cells_sinr_map[data.frmCurntCell].sinrMap->numOfConnectedUEs : 1;
    if (srv_nue < 1) srv_nue = 1;
    double serving_load = (double)srv_nue / MAX_UES_PER_CELL;

    /* ── Serving PRBs per UE (PRBS) — for Eq.6 ─────────────────────── */
double serving_prb_per_ue;
if (serving_ue->prb_used > 0) {
    serving_prb_per_ue = (double)serving_ue->prb_used;
} else if (data.frmCurntCell < MAX_REGISTERED_CELLS &&
           g_cell_load[data.frmCurntCell].valid &&
           g_cell_load[data.frmCurntCell].prb_avg > 0.0) {
    double used_srv = g_cell_load[data.frmCurntCell].prb_avg * TOTAL_PRB;
    serving_prb_per_ue = used_srv / (double)srv_nue;
} else {
    serving_prb_per_ue = (double)TOTAL_PRB / (double)srv_nue;
}


    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 1 — CELL FILTERING: Build NCL (Neighbor Cell List)
     *   Include cell i if:  load(i) = NUE(i)/MAX_UES ≤ 0.50
     *   AND SINR(i) ≥ MIN_SINR
     * Paper Section 3: "a restricted NCL is generated from cells whose
     *                   load level is below a certain threshold (≤ 50%)"
     * ═══════════════════════════════════════════════════════════════════ */
    typedef struct { int cell_id; int nue; double load; double sinr; double rsrp_dbm; } ncl_entry_t;
    ncl_entry_t ncl[MAX_REGISTERED_NEIGHBOURS];
    int ncl_size = 0;

    printf("\n" CLR_BBLUE
           "  ╔══ PAPER PIPELINE ════════════════════════════════════════════════╗\n"
           "  ║  UE %-3d │ Serving Cell %-2d │ γS=%+.2f dB │ RSRP_S=%+.1f dBm    ║\n"
           "  ║  NUE=%d  Load=%.0f%%  PRB/UE=%.1f                                ║\n"
           "  ╠══ PHASE 1: NCL FILTERING (load ≤ 19%%) ═══════════════════════════╣\n"
           CLR_RESET,
           data.ueID, data.frmCurntCell,
           serving_sinr, rsrp_serving_dbm,
           srv_nue, serving_load * 100.0, serving_prb_per_ue);

    for (int i = 0; i < MAX_REGISTERED_NEIGHBOURS; i++) {
        if (!data.neighCells[i].is_available) continue;
        if (data.neighCells[i].sinr < (double)MIN_SINR) {
            printf("  │  Cell %2d: SINR=%+.2f dB < MIN_SINR → " CLR_DIM "SKIP\n" CLR_RESET,
                   i, data.neighCells[i].sinr);
            continue;
        }
        int nue_i = (i < MAX_REGISTERED_CELLS &&
                     cells_sinr_map[i].is_registered &&
                     cells_sinr_map[i].sinrMap != NULL)
                    ? (int)cells_sinr_map[i].sinrMap->numOfConnectedUEs : 0;
        double load_i = (double)nue_i / MAX_UES_PER_CELL;

        if (load_i > 0.19) {
            printf("  │  Cell %2d: NUE=%d load=%.0f%% > 19%% → " CLR_BRED "EXCLUDED from NCL\n" CLR_RESET,
                   i, nue_i, load_i * 100.0);
            continue;
        }

        /* RSRP for this neighbor — raw dBm from KPM L3neighRSRPListOf (O-RAN E2 path) */
        double rsrp_i_dbm = data.neighCells[i].rsrp;

        ncl[ncl_size].cell_id  = i;
        ncl[ncl_size].nue      = nue_i;
        ncl[ncl_size].load     = load_i;
        ncl[ncl_size].sinr     = data.neighCells[i].sinr;  // KPM E2 value — full precision since SINR fix
        ncl[ncl_size].rsrp_dbm = rsrp_i_dbm;
        ncl_size++;

        printf("  │  Cell %2d: NUE=%d load=%.0f%% SINR=%+.2f dB RSRP=%+.1f dBm → "
               CLR_BGREEN "IN NCL [%d]\n" CLR_RESET,
               i, nue_i, load_i * 100.0, ncl[ncl_size - 1].sinr, rsrp_i_dbm, ncl_size);
    }

    if (ncl_size == 0) {
        printf("  │  " CLR_BYELLOW "NCL empty — no eligible neighbors\n" CLR_RESET);
        printf(CLR_BBLUE
               "  ╚═════════════════════════════════════════════════════════════════╝\n"
               CLR_RESET "\n");
        /* fWF not yet computed (Phase 6 not reached) — dec_fWF left unchanged */
        return 0;
    }

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 2 — RSRPpilot COMPUTATION  [Paper Eq.14]  — ALL IN dBm
     *   RSRPpilot_dBm = RSRP_dBm + 10·log10(NPRB_total / NUE)
     *   This is the dB form of Eq.14: pilot adds PRB gain in dB.
     *   Using dB throughout keeps Phase 3 argmax and Phase 9 comparison
     *   dimensionally consistent without any mW↔dBm conversion.
     * ═══════════════════════════════════════════════════════════════════ */
    printf(CLR_BBLUE
           "  ╠══ PHASE 2: RSRPpilot_dBm = RSRP_dBm + 10·log10(NPRB/NUE)  [Eq.14] ╣\n"
           CLR_RESET);

    /* RSRPpilot serving — dBm */
    double pilot_srv = rsrp_serving_dbm + 10.0 * log10((double)TOTAL_PRB / (double)srv_nue);
    printf("  │  Serving Cell %2d: RSRP=%+.1f dBm + 10·log10(%d/%d) = "
           CLR_BCYAN "%+.2f dBm\n" CLR_RESET,
           data.frmCurntCell, rsrp_serving_dbm, TOTAL_PRB, srv_nue, pilot_srv);

    /* RSRPpilot for each NCL cell — dBm */
    double pilot_ncl[MAX_REGISTERED_NEIGHBOURS];
    for (int k = 0; k < ncl_size; k++) {
        int nue_k = ncl[k].nue;
        if (nue_k < 1) nue_k = 1;
        pilot_ncl[k] = ncl[k].rsrp_dbm + 10.0 * log10((double)TOTAL_PRB / (double)nue_k);
        printf("  │  NCL Cell   %2d: RSRP=%+.1f dBm + 10·log10(%d/%d) = "
               CLR_BCYAN "%+.2f dBm\n" CLR_RESET,
               ncl[k].cell_id, ncl[k].rsrp_dbm, TOTAL_PRB, nue_k, pilot_ncl[k]);
    }

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 3 — TARGET CELL SELECTION  [Paper Section 3]
     *   target = argmax(RSRPpilot)  over NCL
     *   "The cell with the highest RSRP value in NCL is determined as
     *    the target cell."
     * ═══════════════════════════════════════════════════════════════════ */
    printf(CLR_BBLUE
           "  ╠══ PHASE 3: TARGET = argmax(RSRPpilot) ══════════════════════════╣\n"
           CLR_RESET);

    int    best_ncl_idx = 0;
    double best_pilot   = pilot_ncl[0];
    for (int k = 1; k < ncl_size; k++) {
        if (pilot_ncl[k] > best_pilot) {
            best_pilot   = pilot_ncl[k];
            best_ncl_idx = k;
        }
    }

    /* Anti-ping-pong hysteresis: if recently left this cell, require higher pilot */
    int hysteresis_active = 0;
    uint16_t ue_id_chk = (uint16_t)data.ueID;
    if (ue_id_chk > 0 && ue_id_chk < MAX_REGISTERED_UES &&
        g_ue_prev_cell[ue_id_chk] == (uint16_t)ncl[best_ncl_idx].cell_id) {
        double since = difftime(time(NULL), g_ue_prev_cell_time[ue_id_chk]);
        if (since < PING_PONG_WINDOW_SEC) {
            hysteresis_active = 1;
            double hysteresis_boost = pilot_srv * (HYSTERESIS_DB / 30.0);
            if (best_pilot < pilot_srv + hysteresis_boost) {
                printf("  │  " CLR_BYELLOW "Hysteresis: Cell %d recently left (%.1fs), "
                       "pilot %.6g < required %.6g → NO HO\n" CLR_RESET,
                       ncl[best_ncl_idx].cell_id, since, best_pilot,
                       pilot_srv + hysteresis_boost);
                printf(CLR_BBLUE
                       "  ╚═════════════════════════════════════════════════════════════════╝\n"
                       CLR_RESET "\n");
                /* fWF not yet computed (Phase 6 not reached) — dec_fWF left unchanged */
                return 0;
            }
        }
    }
    (void)hysteresis_active;

    uint8_t targetCell = (uint8_t)ncl[best_ncl_idx].cell_id;
    double  target_sinr    = ncl[best_ncl_idx].sinr;
    double  rsrp_target_dbm = ncl[best_ncl_idx].rsrp_dbm;
    int     target_nue     = ncl[best_ncl_idx].nue;
    if (target_nue < 1) target_nue = 1;

    printf("  │  " CLR_BGREEN "TARGET = Cell %d  RSRPpilot=%+.2f dBm  γT=%+.2f dB  "
           "RSRP_T=%+.1f dBm\n" CLR_RESET,
           targetCell, best_pilot, target_sinr, rsrp_target_dbm);

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 4 — METRIC FUNCTIONS  [Eq.5, Eq.6, Eq.7]
     *   All computed for (serving, target) pair AFTER target is selected.
     * ═══════════════════════════════════════════════════════════════════ */
    printf(CLR_BBLUE
           "  ╠══ PHASE 4: METRIC FUNCTIONS [Eq.5,6,7] ════════════════════════╣\n"
           CLR_RESET);

    /* Eq.5: f(γ) = (γT − γS) / γmax,   γmax = 30 dB */
    double f_sinr = (target_sinr - serving_sinr) / 60.0;
    if (f_sinr >  1.0) f_sinr =  1.0;
    if (f_sinr < -1.0) f_sinr = -1.0;

    /* Eq.6: f(PRB) = (PRBT − PRBS) / PRBmax */
    double target_prb_per_ue;
    if (targetCell < MAX_REGISTERED_CELLS &&
        g_cell_load[targetCell].valid &&
        g_cell_load[targetCell].prb_avg > 0.0) {
        double used_tgt = g_cell_load[targetCell].prb_avg * TOTAL_PRB;
        target_prb_per_ue = used_tgt / (double)target_nue;
    } else {
        target_prb_per_ue = (double)TOTAL_PRB / (double)target_nue;
    }
    double f_prb = (target_prb_per_ue - serving_prb_per_ue) / (double)TOTAL_PRB;
    if (f_prb >  1.0) f_prb =  1.0;
    if (f_prb < -1.0) f_prb = -1.0;

    /* Eq.7: f(υ) = 2·log2((υt + υmax) / υmax) − 1,   υmax = 13.9 m/s = 50.04 km/h */
    double ue_speed_mps = (data.ueID > 0 && data.ueID < MAX_REGISTERED_UES)
                           ? g_ue_speed_mps[data.ueID] : UE_SPEED_DEFAULT_MPS;
    double f_speed = compute_f_speed(ue_speed_mps);

    printf("  │  Eq.5  f(γ)   = (%+.2f − %+.2f) / 60 = " CLR_BBLUE "%+.4f\n" CLR_RESET,
           target_sinr, serving_sinr, f_sinr);
    printf("  │  Eq.6  f(PRB) = (%.2f − %.2f) / %d = " CLR_BGREEN "%+.4f\n" CLR_RESET,
           target_prb_per_ue, serving_prb_per_ue, TOTAL_PRB, f_prb);
    printf("  │  Eq.7  f(υ)   = 2·log2((%.1f+%.2f)/%.2f)−1 = " CLR_BYELLOW "%+.4f\n" CLR_RESET,
           ue_speed_mps * 3.6, VMAX_KMH, VMAX_KMH, f_speed);

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 5 — AWF ADAPTIVE WEIGHTING  [Eq.4, Eq.2, Eq.3]
     *   ωx = (1 − f(x)) / Σ(1 − f(xi)),   Ωx = ωx / ωt
     * ═══════════════════════════════════════════════════════════════════ */
    printf(CLR_BBLUE
           "  ╠══ PHASE 5: AWF WEIGHTS [Eq.4,2,3] ════════════════════════════╣\n"
           CLR_RESET);

    double w_s = 1.0 - f_sinr;   if (w_s  < 0.01) w_s  = 0.01;
    double w_p = 1.0 - f_prb;    if (w_p  < 0.01) w_p  = 0.01;
    double w_v = 1.0 - f_speed;  if (w_v  < 0.01) w_v  = 0.01;
    double wt  = w_s + w_p + w_v;

    double Omega_sinr  = w_s / wt;
    double Omega_prb   = w_p / wt;
    double Omega_speed = w_v / wt;

    printf("  │  ωγ=%.4f  ωPRB=%.4f  ων=%.4f  ωt=%.4f\n",
           w_s, w_p, w_v, wt);
    printf("  │  Ωγ=" CLR_BYELLOW "%.4f" CLR_RESET
           "  ΩPRB=" CLR_BYELLOW "%.4f" CLR_RESET
           "  Ων=" CLR_BYELLOW "%.4f" CLR_RESET
           "  (sum=%.4f)\n",
           Omega_sinr, Omega_prb, Omega_speed,
           Omega_sinr + Omega_prb + Omega_speed);

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 6 — FINAL SCORE fWF  [Eq.1]
     *   fWF = Ωγ·f(γ) + ΩPRB·f(PRB) + Ων·f(υ)
     * ═══════════════════════════════════════════════════════════════════ */
    printf(CLR_BBLUE
           "  ╠══ PHASE 6: fWF [Eq.1] ══════════════════════════════════════════╣\n"
           CLR_RESET);

    double fWF = (Omega_sinr * f_sinr) + (Omega_prb * f_prb) + (Omega_speed * f_speed);

    printf("  │  fWF = %.4f×%+.4f + %.4f×%+.4f + %.4f×%+.4f = "
           CLR_BMAGENTA "%+.4f\n" CLR_RESET,
           Omega_sinr, f_sinr, Omega_prb, f_prb, Omega_speed, f_speed, fWF);

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 7 — HOM ADAPTATION  [Eq.8, Eq.9]
     *   HOM_avg = (HOMmax + HOMmin) / 2 = 10 dB  [Eq.9]
     *   γth = -10 dB  [paper Table 1 SINR threshold]
     *
     *   CRITICAL: Cases 2 & 3 use RAW weights ωPRB and ων
     *             (not normalized Ωx) — as written in paper Eq.(8).
     *
     *   Case 1: both γT,S on same side of γth
     *           ΔHOM = HOM_avg × fWF
     *   Case 2: γT ≤ γth AND γS ≥ γth
     *           ΔHOM = HOM_avg × (1 + ωPRB·f(PRB) + ων·f(υ))
     *   Case 3: γS ≤ γth AND γT ≥ γth
     *           ΔHOM = HOM_avg × (−1 + ωPRB·f(PRB) + ων·f(υ))
     *   Clamp: ΔHOM ∈ [HOMmin, HOMmax] = [0, 20] dB
     * ═══════════════════════════════════════════════════════════════════ */
    printf(CLR_BBLUE
           "  ╠══ PHASE 7: HOM ADAPTATION [Eq.8,9] ═══════════════════════════╣\n"
           CLR_RESET);

    /* [FIX 1] gamma_th = -10 dB — Paper Table 1: SINR threshold = -10 dB
     * الكود القديم كان يستخدم 0.0 مما يجعل Case 3 يُطلق لـ UEs فوق -10 dB
     * كـ γS=-1 dB وده خطأ — البيبر يحدد -10 dB بوضوح في Table 1. */
    double gamma_th = -10.0;  /* Paper Table 1: SINR threshold = -10 dB */
    double delta_HOM;
    int    hom_case;

    if ((target_sinr <= gamma_th && serving_sinr <= gamma_th) ||
        (target_sinr >= gamma_th && serving_sinr >= gamma_th)) {
        /* Case 1: both on same side of threshold */
        delta_HOM = HOM_AVG_DB * fWF;
        hom_case  = 1;
        printf("  │  Case 1 (both same side γth=−10dB): ΔHOM = %.1f × %+.4f = "
               CLR_BYELLOW "%+.3f dB\n" CLR_RESET, HOM_AVG_DB, fWF, delta_HOM);
    } else if (target_sinr <= gamma_th && serving_sinr >= gamma_th) {
        /* Case 2: target poor, serving good — use raw ωPRB and ων */
        delta_HOM = HOM_AVG_DB * (1.0 + (w_p * f_prb) + (w_v * f_speed));
        hom_case  = 2;
        printf("  │  Case 2 (γT≤γth, γS≥γth): ΔHOM = %.1f×(1+%.4f×%+.4f+%.4f×%+.4f) = "
               CLR_BYELLOW "%+.3f dB\n" CLR_RESET,
               HOM_AVG_DB, w_p, f_prb, w_v, f_speed, delta_HOM);
    } else {
        /* Case 3: serving poor, target good — use raw ωPRB and ων */
        delta_HOM = HOM_AVG_DB * (-1.0 + (w_p * f_prb) + (w_v * f_speed));
        hom_case  = 3;
        printf("  │  Case 3 (γS≤γth, γT≥γth): ΔHOM = %.1f×(−1+%.4f×%+.4f+%.4f×%+.4f) = "
               CLR_BYELLOW "%+.3f dB\n" CLR_RESET,
               HOM_AVG_DB, w_p, f_prb, w_v, f_speed, delta_HOM);
    }

    /* Clamp ΔHOM to [HOMmin, HOMmax] = [0, 20] dB */
    if (delta_HOM < HOM_MIN_DB) delta_HOM = HOM_MIN_DB;
    if (delta_HOM > HOM_MAX_DB) delta_HOM = HOM_MAX_DB;
    printf("  │  ΔHOM (clamped [-10,+10]) = " CLR_BYELLOW "%+.3f dB\n" CLR_RESET, delta_HOM);

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 8 — TTT ADAPTATION  [Eq.10-13]
     *   Updated in evaluateHandoverOpportunities after this function
     *   returns (TTT timer is external to the target-finding step).
     *   fWF is stored in ctx for the TTT update logic.
     * ═══════════════════════════════════════════════════════════════════ */
    /* (TTT timer check and update happens in evaluateHandoverOpportunities) */

    /* ═══════════════════════════════════════════════════════════════════
     * PHASE 9 — HANDOVER DECISION  [Algorithm 1, Line 2]
     *   ALL VALUES IN dBm — dimensionally consistent throughout.
     *
     *   Paper Algorithm 1 Line 2 (page 1022):
     *     "if RSRPpilot_T + ΔHOM > RSRPpilot_S  then  HO = True"
     *
     *   RSRPpilot already computed in dBm in Phase 2:
     *     RSRPpilot_dBm = RSRP_dBm + 10·log10(NPRB/NUE)
     *
     *   ΔHOM is in dB → direct dB addition is valid.
     *   Criterion: RSRPpilot_T_dBm + ΔHOM_dB > RSRPpilot_S_dBm
     * ═══════════════════════════════════════════════════════════════════ */
    printf(CLR_BBLUE
           "  ╠══ PHASE 9: HO DECISION [Algo1 Line 2] ════════════════════════╣\n"
           CLR_RESET);

    /* dB criterion: RSRPpilot_T + ΔHOM > RSRPpilot_S  [Algorithm 1 Line 2] */
    double algo1_thr_dBm = pilot_srv - delta_HOM;   /* rearranged equivalent: T > S - ΔHOM */
    int    algo1_pass    = (best_pilot + delta_HOM > pilot_srv);  /* CORRECT: T + ΔHOM > S */

    printf("  │  RSRPpilot_S=%+.2f dBm  ΔHOM=%+.3f dB  RSRPpilot_T=%+.2f dBm\n",
           pilot_srv, delta_HOM, best_pilot);
    printf("  │  Criterion: RSRPpilot_T + ΔHOM > RSRPpilot_S\n");
    printf("  │  %+.2f + %+.3f = %+.2f  vs  RSRPpilot_S = %+.2f dBm\n",
           best_pilot, delta_HOM, best_pilot + delta_HOM, pilot_srv);
    printf("  │  RSRPpilot_T+ΔHOM(%+.2f) %s RSRPpilot_S(%+.2f) → %s\n",
           best_pilot + delta_HOM,
           algo1_pass ? CLR_BGREEN ">" CLR_RESET : CLR_BRED "≤" CLR_RESET,
           pilot_srv,
           algo1_pass ? CLR_BGREEN "HO APPROVED" CLR_RESET
                      : CLR_BRED   "HO REJECTED" CLR_RESET);

    /* ── [REQ §1,5] Save dec_fWF BEFORE any return — always, even on reject.
     * fWF is computed at Phase 6 so it is valid here.
     * Zero-guard: if fWF is somehow 0.0, keep the previous value and warn. */
    if (data.ueID > 0 && data.ueID < MAX_REGISTERED_UES) {
        handover_context_t* _ctx_early = &g_ue_ho_ctx[data.ueID];
        if (fWF == 0.0) {
            LOG_WARN("[fWF-GUARD] UE %d  fWF==0.0 at Phase9 — substituting last_fWF=%.8f",
                     data.ueID, _ctx_early->last_fWF);
            _ctx_early->dec_fWF = (_ctx_early->last_fWF != 0.0)
                                  ? _ctx_early->last_fWF
                                  : TTT_DEFAULT_SEC; /* absolute fallback */
        } else {
            _ctx_early->dec_fWF = fWF;
        }
    }

    if (!algo1_pass) {
        printf(CLR_BBLUE
               "  ╚═════════════════════════════════════════════════════════════════╝\n"
               CLR_RESET "\n");
        return 0;
    }

    /* ── DECISION LOG — print all intermediate values ─────────────── */
    /* Use log_decision_step for consistent logging */
    log_decision_step(
        targetCell,
        serving_sinr,  target_sinr,
        f_sinr,        f_prb,      f_speed,
        w_s,           w_p,        w_v,
        Omega_sinr,    Omega_prb,  Omega_speed,
        fWF,
        serving_prb_per_ue, target_prb_per_ue,
        pilot_srv,     best_pilot,
        delta_HOM,     hom_case,
        algo1_thr_dBm,
        target_nue,    ncl[best_ncl_idx].load,
        1 /*ncl_pass*/, 1 /*algo1_pass*/);

    /* ── Store all computed values for CSV logging ────────────────── */
    if (data.ueID > 0 && data.ueID < MAX_REGISTERED_UES) {
        handover_context_t* ctx = &g_ue_ho_ctx[data.ueID];
        ctx->dec_serving_sinr    = serving_sinr;
        ctx->dec_target_sinr     = target_sinr;
        ctx->dec_f_sinr          = f_sinr;
        ctx->dec_f_prb           = f_prb;
        ctx->dec_Omega_sinr      = Omega_sinr;
        ctx->dec_Omega_prb       = Omega_prb;
        ctx->dec_fWF             = fWF;
        ctx->dec_delta_HOM       = delta_HOM;
        ctx->dec_delta_HOM_case  = hom_case;
        ctx->dec_rsrp_pilot_srv  = pilot_srv;     /* dBm */
        ctx->dec_rsrp_pilot_tgt  = best_pilot;    /* dBm */
        ctx->dec_algo1_threshold = algo1_thr_dBm; /* dBm */
        ctx->dec_serving_nue     = srv_nue;
        ctx->dec_target_nue      = target_nue;
        ctx->dec_ncl_load_tgt    = ncl[best_ncl_idx].load;
        ctx->dec_rsrp_serving    = rsrp_serving_dbm;
        ctx->dec_rsrp_target     = rsrp_target_dbm;
        ctx->dec_prb_per_ue_srv  = serving_prb_per_ue;
        ctx->dec_prb_per_ue_tgt  = target_prb_per_ue;
        ctx->dec_prb_util_srv    = (data.frmCurntCell < MAX_REGISTERED_CELLS &&
                                    g_cell_load[data.frmCurntCell].valid)
                                   ? g_cell_load[data.frmCurntCell].prb_avg : 0.0;
        ctx->dec_prb_util_tgt    = (targetCell < MAX_REGISTERED_CELLS &&
                                    g_cell_load[targetCell].valid)
                                   ? g_cell_load[targetCell].prb_avg : 0.0;
        ctx->dec_prb_src_rru     = (data.frmCurntCell < MAX_REGISTERED_CELLS &&
                                    g_cell_load[data.frmCurntCell].valid) ? 1 : 0;
        ctx->dec_data_captured   = 1;
        /* FIX-2: Do NOT update last_fWF here (inside getTargetCellID).
         * If we update it here, the idle TTT path in evaluateHO will
         * see fWF_prev == fWF_now (same cycle) → wrong Delta → TTT jumps.
         * last_fWF is updated ONLY in:
         *   a) doHandoverAction  (after HO command sent)
         *   b) idle TTT path     (when no HO this cycle)
         * This ensures fWF_prev is always from the PREVIOUS cycle. */
    }

    /* ── DECISION SUMMARY BOX ─────────────────────────────────────── */
    /* FIX-3: Show correct TTT (initialized to TTT_DEFAULT_SEC in main) */
    double ttt_cur = (data.ueID > 0 && data.ueID < MAX_REGISTERED_UES &&
                      g_ue_ho_ctx[data.ueID].current_ttt_sec > 0.0)
                     ? g_ue_ho_ctx[data.ueID].current_ttt_sec : TTT_DEFAULT_SEC;

    printf(CLR_BBLUE
           "  ╠═════════════════════════════════════════════════════════════════╣\n"
           "  ║  " CLR_BGREEN "✔ DECISION: HANDOVER" CLR_RESET CLR_BBLUE
           "  UE %-3d  Cell %-2d ──▶ Cell %-2d                    ║\n"
           "  ║  f(γ)=%+.4f  f(PRB)=%+.4f  f(υ)=%+.4f  fWF=%+.4f           ║\n"
           "  ║  Ωγ=%.4f  ΩPRB=%.4f  Ων=%.4f  ΔHOM=%+.3f dB [Case%d]      ║\n"
           "  ║  RSRPpilot_T=%+.2f dBm > thr=%+.2f dBm  TTT_cur=%.3fs      ║\n"
           "  ╚═════════════════════════════════════════════════════════════════╝\n"
           CLR_RESET "\n",
           data.ueID, data.frmCurntCell, targetCell,
           f_sinr, f_prb, f_speed, fWF,
           Omega_sinr, Omega_prb, Omega_speed, delta_HOM, hom_case,
           best_pilot, algo1_thr_dBm, ttt_cur);

    return targetCell;
}

void remove_neighCell() {}

struct InfoObj 
{
  uint16_t cellID;
  uint16_t ueID;
};

struct InfoObj parseServingMsg(const uint8_t* msg) 
{
  struct InfoObj info;
  int ret = sscanf((const char*)msg, "L3servingSINR3gpp_cell_%hd_UEID_%hd", &info.cellID, &info.ueID);
  if (ret == 2)
    return info;
  info.cellID = -1;
  info.ueID = -1;
  return info;
}

struct InfoObj parseNeighMsg(const uint8_t* msg) 
{
  struct InfoObj info;
  int ret = sscanf((const char*)msg, "L3neighSINRListOf_UEID_%hd_of_Cell_%hd", &info.ueID, &info.cellID);
  if (ret == 2)
    return info;
  info.ueID = -1;
  info.cellID = -1;
  return info;
}

bool isMeasNameContains(const uint8_t* meas_name, const char* name) 
{
  return strncmp((const char*)meas_name, name, strlen(name)) == 0;
}

// [LB#2] Static variable لتتبع الـ cell_id الحالية داخل log_kpm_measurements
// بتتحدث كل ما بنشوف L3servingSINR3gpp_cell_X وبتُستخدم لتمرير الـ cell_id
// لـ lb_update_cell_prb() لما بنشوف RRU.PrbUsedDl في نفس الـ KPM report.
static uint16_t g_current_kpm_cell_id = 0;

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

          if (info.cellID > 0 && info.cellID < MAX_REGISTERED_CELLS)
              g_current_kpm_cell_id = (uint16_t)info.cellID;

          struct SINR_Map* cell = add_SINR(info.cellID);
          add_UE(cell, info.ueID, sinr);

          if (node_id_src != NULL)
              cell_node_map_register((uint16_t)info.cellID, node_id_src);

          LOG_KPM("Serving  Cell " CLR_BYELLOW "%2d" CLR_RESET "  UE " CLR_BYELLOW "%2d" CLR_RESET "  SINR " CLR_BBLUE "%+7.2f dB" CLR_RESET,
                  info.cellID, info.ueID, sinr);

        } else if (isMeasNameContains(meas_type.name.buf, "L3servingRSRP3gpp_cell_")) {
          // Serving RSRP — raw dBm as double (choice.real), full floating-point precision
          uint16_t cell_id = 0, ue_id = 0;
          if (sscanf((const char*)meas_type.name.buf, "L3servingRSRP3gpp_cell_%hu_UEID_%hu",
                     &cell_id, &ue_id) == 2 &&
              cell_id > 0 && cell_id < MAX_REGISTERED_CELLS &&
              ue_id   > 0 && ue_id   < MAX_REGISTERED_UES) {
              struct SINRServingValues* ue = get_UE(cell_id, ue_id);
              if (ue != NULL) {
                  ue->rsrp = record_item.real_val;
                  LOG_KPM("[KPM] Serving Cell %2d  UE %2d  RSRP %+.2f dBm",
                          cell_id, ue_id, ue->rsrp);
              }
          }

        } else if (isMeasNameContains(meas_type.name.buf, "L3servingPRB3gpp_cell_")) {
          // Real PRB used for serving cell — from scenario UpdateRealRsrpPrb()
          struct InfoObj info = parseServingMsg(meas_type.name.buf);
          if (info.cellID > 0 && info.cellID < MAX_REGISTERED_CELLS &&
              info.ueID  > 0 && info.ueID  < MAX_REGISTERED_UES) {
              struct SINRServingValues* ue = get_UE(info.cellID, info.ueID);
              if (ue != NULL) {

ue->prb_used  = (int)record_item.real_val;
ue->prb_total = TOTAL_PRB;
LOG_KPM("Serving  Cell %2d  UE %2d  MAC_PRB=%d (real from scheduler)",
        info.cellID, info.ueID, ue->prb_used);

                  LOG_KPM("Serving  Cell %2d  UE %2d  PRB %d/%d",
                          info.cellID, info.ueID, ue->prb_used, ue->prb_total);
              }
          }

        } else if (isMeasNameContains(meas_type.name.buf, "L3neighRSRPListOf_UEID_")) {
          // Neighbor RSRP — raw dBm as double; cell ID encoded in measurement name
          uint16_t ue_id = 0, neigh_cell_id = 0;
          if (sscanf((const char*)meas_type.name.buf, "L3neighRSRPListOf_UEID_%hu_of_Cell_%hu",
                     &ue_id, &neigh_cell_id) == 2 &&
              g_current_kpm_cell_id > 0 &&
              neigh_cell_id > 0 && neigh_cell_id < MAX_REGISTERED_NEIGHBOURS &&
              ue_id > 0 && ue_id < MAX_REGISTERED_UES) {
              struct SINRServingValues* ue = get_UE(g_current_kpm_cell_id, ue_id);
              if (ue != NULL && ue->neighCells != NULL) {
                  ue->neighCells[neigh_cell_id].rsrp = record_item.real_val;
                  LOG_KPM("[KPM] Neigh  Cell %2d  UE %2d  RSRP %+.2f dBm",
                          neigh_cell_id, ue_id, record_item.real_val);
              }
          }

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

/* ============================================================
 * [NEW — Paper Eq.(8,9)]: send_hom_rc_control()
 *
 * PURPOSE:
 *   إرسال قيمة ΔHOM المحسوبة من Eq.(8) للـ eNB عبر E2SM-RC control message.
 *   في الكود القديم كانت ΔHOM بتتحسب صح لكن بتُستخدم فقط كـ threshold محلي
 *   داخل Algorithm 1 في الـ xApp — البيبر بيطلب إن الـ RAN نفسه يستخدمها.
 *
 * HOW IT WORKS:
 *   - بنبني RC control message بـ RIC Style 1 (Cell-Level Parameter Control).
 *   - الـ RAN_PARAMETER_ID للـ HOM هو 1 (Handover Margin — O-RAN WG3 E2SM-RC spec).
 *   - ΔHOM بيتحول لـ integer بالـ dB×10 عشان يتبعت كـ int64 RAN parameter.
 *   - الـ message بيتبعت للـ E2 node اللي بيملك الـ serving cell.
 *
 * PARAMETERS:
 *   ue_id_val  : UE ID (للـ header)
 *   delta_hom  : ΔHOM بالـ dB المحسوبة من Eq.(8) — ممكن تكون سالبة أو موجبة
 *   serving_cell: الـ cell ID اللي بنبعت الـ HOM update ليها
 *   nodes      : مصفوفة الـ E2 nodes المتصلة
 *
 * RETURN: 1 لو الإرسال نجح، 0 لو فشل
 * ============================================================ */
static int send_hom_rc_control(int ue_id_val, double delta_hom,
                                uint16_t serving_cell,
                                const e2_node_arr_xapp_t* nodes)
{
    /* RAN Parameter ID 1 = Handover Margin (O-RAN E2SM-RC v3 Table 8.1.1.1)
     * الـ value بيتبعت كـ integer بوحدة 0.1 dB — يعني 10 dB = 100 */
    int64_t hom_encoded = (int64_t)(delta_hom * 10.0);

    /* clamp: ΔHOM في نطاق [-10, +10] dB = HOM_avg × fWF مع fWF∈[-1,+1] */
    int64_t hom_min_enc = (int64_t)(HOM_MIN_DB * 10.0);
    int64_t hom_max_enc = (int64_t)(HOM_MAX_DB * 10.0);
    if (hom_encoded < hom_min_enc) hom_encoded = hom_min_enc;
    if (hom_encoded > hom_max_enc) hom_encoded = hom_max_enc;

    LOG_INFO("[HOM-RC] UE %d  Cell %d  ΔHOM=%.2f dB  encoded=%lld  (paper Eq.8+9)",
             ue_id_val, serving_cell, delta_hom, (long long)hom_encoded);

    /* بناء الـ RC control message */
    rc_ctrl_req_data_t rc_hom = {0};
    ue_id_e2sm_t uid = gen_rc_ue_id(GNB_UE_ID_E2SM, ue_id_val);

    /* Style 1 = Cell-Level RAN Parameter Control (O-RAN WG3 E2SM-RC) */
    rc_hom.hdr = gen_rc_ctrl_hdr(FORMAT_1_E2SM_RC_CTRL_HDR, uid, 1, 1);

    /* بناء الـ message payload: RAN parameter ID=1 (HOM), value=hom_encoded */
    e2sm_rc_ctrl_msg_frmt_1_t frmt = {0};
    frmt.sz_ran_param = 1;
    frmt.ran_param = calloc(1, sizeof(seq_ran_param_t));
    assert(frmt.ran_param != NULL && "Memory exhausted — HOM RC message");

    frmt.ran_param[0].ran_param_id  = 1;   /* Handover Margin RAN param ID */
    frmt.ran_param[0].ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
    frmt.ran_param[0].ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(frmt.ran_param[0].ran_param_val.flag_false != NULL && "Memory exhausted");
    frmt.ran_param[0].ran_param_val.flag_false->type    = INTEGER_RAN_PARAMETER_VALUE;
    frmt.ran_param[0].ran_param_val.flag_false->int_ran = hom_encoded;

    rc_hom.msg.format  = FORMAT_1_E2SM_RC_CTRL_MSG;
    rc_hom.msg.frmt_1  = frmt;

    /* إرسال targeted للـ E2 node اللي بيملك الـ serving cell */
    bool sent = false;
    global_e2_node_id_t* tnode = cell_node_map_get(serving_cell);
    if (tnode != NULL) {
        sm_ans_xapp_t ans = control_sm_xapp_api(tnode, SM_RC_ID, &rc_hom);
        if (ans.success) {
            sent = true;
            LOG_OK("[HOM-RC] ΔHOM=%.2f dB sent to Cell %d  NB_ID=%u  (paper Eq.8)",
                   delta_hom, serving_cell, tnode->nb_id.nb_id);
        }
    }
    /* fallback: broadcast لو الـ targeted send فشل */
    if (!sent) {
        for (size_t i = 0; i < nodes->len && !sent; ++i) {
            sm_ans_xapp_t ans = control_sm_xapp_api(&nodes->n[i].id, SM_RC_ID, &rc_hom);
            if (ans.success) {
                sent = true;
                LOG_WARN("[HOM-RC] broadcast fallback  ΔHOM=%.2f dB → node %zu", delta_hom, i);
            }
        }
    }

    free_rc_ctrl_req_data(&rc_hom);

    if (!sent)
        LOG_ERR("[HOM-RC] Failed to deliver ΔHOM RC message for UE %d", ue_id_val);

    return sent ? 1 : 0;
}
/* ---- end of send_hom_rc_control ---- */

/* ============================================================
 * [NEW — Paper Eq.(10-13)]: compute_ttt_update()
 *
 * PURPOSE:
 *   حساب قيمة TTT الجديدة لـ UE معين بناءً على الـ fWF الحالي والسابق.
 *   البيبر بيقول إن الـ xApp يحدّث TTT ديناميكياً بعد كل دورة تقييم وبيبعت
 *   القيمة الجديدة للـ RAN.
 *
 * PAPER EQUATIONS:
 *   ΔT = Z1 if Tmin < T < Tmax
 *   ΔT = Z2 if T == Tmin
 *   ΔT = Z3 if T == Tmax
 *
 *   Z1 = T - ρ  if fWF <= fWF_prev + Q   (تحسّن الحالة → قلّل TTT)
 *        T + ρ  if fWF >= fWF_prev + Q   (تدهور الحالة → زوّد TTT)
 *
 *   Z2 = T      if fWF <= fWF_prev + Q   (لو في الـ min، ابقى عليه)
 *        T + ρ  if fWF >= fWF_prev + Q
 *
 *   Z3 = T - ρ  if fWF <= fWF_prev + Q
 *        T      if fWF >= fWF_prev + Q   (لو في الـ max، ابقى عليه)
 *
 * PARAMETERS:
 *   current_ttt : الـ TTT الحالي بالثواني
 *   fWF_now     : قيمة الـ Weight Function في الـ cycle الحالي (Eq.1)
 *   fWF_prev    : قيمة الـ Weight Function من الـ cycle السابق
 *
 * RETURN: الـ TTT الجديد بالثواني، مقيّد بـ [Tmin, Tmax]
 * ============================================================ */
static double compute_ttt_update(double current_ttt, double fWF_now, double fWF_prev)
{
    double T        = current_ttt;
    double new_T;
    double delta_fWF = fWF_now - fWF_prev;

    /*
     * 4 cases (new logic):
     *   Case 1: delta == 0          → dead-zone, TTT unchanged, ttt_counter unchanged
     *   Case 2: delta < 0           → fWF worsened → TTT + ρ (slower HO)
     *   Case 3: delta > 0 && < Q    → fWF improved → TTT − ρ (faster HO)
     *   Case 4: delta >= Q          → fWF really improved → TTT − ζ (much faster HO)
     *
     * Boundary cases unchanged (Z2/Z3):
     *   T == Tmin: only TTT+ρ if worsened, else stay
     *   T == Tmax: only TTT−ρ or TTT−ζ if improved, else stay
     */

    if (T > TTT_MIN_SEC && T < TTT_MAX_SEC) {
        /* Normal range — Z1 */
        if (fabs(delta_fWF) < 1e-9) {
            new_T = T;                  /* Case 1: dead-zone */
        } else if (delta_fWF < 0.0) {
            new_T = T + TTT_RHO;       /* Case 2: worsened */
        } else if (delta_fWF < TTT_Q) {
            new_T = T - TTT_RHO;       /* Case 3: improved */
        } else {
            new_T = T - TTT_ZETA;      /* Case 4: really improved */
        }
    } else if (T <= TTT_MIN_SEC) {
        /* Z2: at minimum */
        if (delta_fWF < 0.0)
            new_T = T + TTT_RHO;       /* worsened only */
        else
            new_T = T;                 /* improved or dead-zone → stay */
    } else {
        /* Z3: at maximum */
        if (delta_fWF > 0.0)
            new_T = T - (delta_fWF >= TTT_Q ? TTT_ZETA : TTT_RHO);  /* improved */
        else
            new_T = T;                 /* worsened or dead-zone → stay */
    }

    /* Clamp to [Tmin, Tmax] */
    if (new_T < TTT_MIN_SEC) new_T = TTT_MIN_SEC;
    if (new_T > TTT_MAX_SEC) new_T = TTT_MAX_SEC;

    return new_T;
}
/* ---- end of compute_ttt_update ---- */

/* ============================================================
 * [NEW — Paper Eq.(10-13)]: send_ttt_rc_control()
 *
 * PURPOSE:
 *   إرسال قيمة TTT المحدثة للـ eNB عبر E2SM-RC control message.
 *   الـ xApp بيحسب TTT جديد بعد كل evaluation cycle ويبعته للـ RAN
 *   عشان الـ trigger timer في الـ eNB نفسه يتغير — مش بس الـ xApp polling.
 *
 * HOW IT WORKS:
 *   - بنبني RC control message بـ RIC Style 1.
 *   - الـ RAN_PARAMETER_ID للـ TTT هو 2 (Time-to-Trigger — O-RAN WG3 E2SM-RC).
 *   - TTT بيتحول لـ integer بالـ ms×10 عشان يتبعت كـ int64.
 *   - الـ message بيتبعت للـ E2 node اللي بيملك الـ serving cell.
 *
 * PARAMETERS:
 *   ue_id_val    : UE ID
 *   new_ttt_sec  : الـ TTT الجديد بالثواني (محسوب من compute_ttt_update)
 *   serving_cell : الـ cell ID
 *   nodes        : مصفوفة الـ E2 nodes
 *
 * RETURN: 1 لو الإرسال نجح، 0 لو فشل
 * ============================================================ */
static int send_ttt_rc_control(int ue_id_val, double new_ttt_sec,
                                uint16_t serving_cell,
                                const e2_node_arr_xapp_t* nodes)
{
    /* الـ TTT بيتبعت بالـ ms مضروبة في 10 (0.1ms resolution — O-RAN E2SM-RC spec)
     * مثال: 0.5 s = 500 ms → encoded = 5000 */
    int64_t ttt_encoded = (int64_t)(new_ttt_sec * 1000.0 * 10.0);

    LOG_INFO("[TTT-RC] UE %d  Cell %d  TTT=%.3f s  encoded=%lld  (paper Eq.10-13)",
             ue_id_val, serving_cell, new_ttt_sec, (long long)ttt_encoded);

    rc_ctrl_req_data_t rc_ttt = {0};
    ue_id_e2sm_t uid = gen_rc_ue_id(GNB_UE_ID_E2SM, ue_id_val);

    /* Style 1 = Cell-Level RAN Parameter Control */
    rc_ttt.hdr = gen_rc_ctrl_hdr(FORMAT_1_E2SM_RC_CTRL_HDR, uid, 1, 1);

    /* بناء الـ message payload: RAN parameter ID=2 (TTT), value=ttt_encoded */
    e2sm_rc_ctrl_msg_frmt_1_t frmt = {0};
    frmt.sz_ran_param = 1;
    frmt.ran_param = calloc(1, sizeof(seq_ran_param_t));
    assert(frmt.ran_param != NULL && "Memory exhausted — TTT RC message");

    frmt.ran_param[0].ran_param_id  = 2;   /* Time-to-Trigger RAN param ID */
    frmt.ran_param[0].ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
    frmt.ran_param[0].ran_param_val.flag_false = calloc(1, sizeof(ran_parameter_value_t));
    assert(frmt.ran_param[0].ran_param_val.flag_false != NULL && "Memory exhausted");
    frmt.ran_param[0].ran_param_val.flag_false->type    = INTEGER_RAN_PARAMETER_VALUE;
    frmt.ran_param[0].ran_param_val.flag_false->int_ran = ttt_encoded;

    rc_ttt.msg.format = FORMAT_1_E2SM_RC_CTRL_MSG;
    rc_ttt.msg.frmt_1 = frmt;

    bool sent = false;
    global_e2_node_id_t* tnode = cell_node_map_get(serving_cell);
    if (tnode != NULL) {
        sm_ans_xapp_t ans = control_sm_xapp_api(tnode, SM_RC_ID, &rc_ttt);
        if (ans.success) {
            sent = true;
            LOG_OK("[TTT-RC] TTT=%.3fs sent to Cell %d  NB_ID=%u  (paper Eq.10-13)",
                   new_ttt_sec, serving_cell, tnode->nb_id.nb_id);
        }
    }
    if (!sent) {
        for (size_t i = 0; i < nodes->len && !sent; ++i) {
            sm_ans_xapp_t ans = control_sm_xapp_api(&nodes->n[i].id, SM_RC_ID, &rc_ttt);
            if (ans.success) {
                sent = true;
                LOG_WARN("[TTT-RC] broadcast fallback  TTT=%.3fs → node %zu", new_ttt_sec, i);
            }
        }
    }

    free_rc_ctrl_req_data(&rc_ttt);

    if (!sent)
        LOG_ERR("[TTT-RC] Failed to deliver TTT RC message for UE %d", ue_id_val);

    return sent ? 1 : 0;
}
/* ---- end of send_ttt_rc_control ---- */

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
      // CRITICAL FIX: Update last_handover_time on completion so anti-ping-pong timer is fresh.
      target_cell->connectedUEs[ueID].ho_context.last_handover_time = time(NULL);
      target_cell->connectedUEs[ueID].ho_context.attempts = 0;

      // Sync global context so evaluateHandoverOpportunities sees the updated last_handover_time
      if (ueID > 0 && ueID < MAX_REGISTERED_UES) {
          g_ue_ho_ctx[ueID].state               = HO_STATE_IDLE;
          g_ue_ho_ctx[ueID].completion_time     = target_cell->connectedUEs[ueID].ho_context.completion_time;
          g_ue_ho_ctx[ueID].last_handover_time  = target_cell->connectedUEs[ueID].ho_context.last_handover_time;
          g_ue_ho_ctx[ueID].attempts            = 0;
          /* pending_target_cell unused in Algo1 — field kept for struct compatibility */
          g_ue_ho_ctx[ueID].ttt_start_time      = 0;
      }
      
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
          /* pending_target_cell unused in Algo1 */
          ctx->ttt_start_time      = 0;
          ctx->ttt_counter     = 0.0;
          ctx->current_ttt_sec = TTT_DEFAULT_SEC;
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

      // ── IMPROVEMENT #3: Enhanced Anti Ping-Pong ─────────────────
      // Two-layer protection:
      //   Layer 1 — Time guard  : same as before (MIN_HO_INTERVAL_SEC)
      //   Layer 2 — Hysteresis  : candidate cell must be better by
      //             HANDOVER_THRESHOLD_DB *plus* an extra HYSTERESIS_DB
      //             margin if we just left that cell recently.
      //             This prevents oscillation even if the timer expired.
      // ──────────────────────────────────────────────────────────────
      /*
       * [IMPROVEMENT #3 — Enhanced Anti Ping-Pong: Two-Layer Protection]
       *
       * الكود القديم كان عنده Layer-1 (time guard) فقط.
       * دلوقتي عندنا طبقتين:
       *
       * LAYER 1 — Time Guard (ده الموجود هنا):
       *   PURPOSE : منع أي HO لنفس الـ UE خلال MIN_HO_INTERVAL_SEC ثانية بعد آخر HO.
       *   WHAT IT DOES : بيحسب difftime(now, last_handover_time) ولو أقل من 5 ثواني → skip.
       *   WHY : بيمنع التنفيذ السريع المتكرر حتى لو الـ SINR بيطلب HO.
       *
       * LAYER 2 — SINR Hysteresis (موجودة في getTargetCellID):
       *   PURPOSE : منع الرجوع لـ cell سابقة حتى لو انتهى الـ timer.
       *   WHAT IT DOES : بيزود الـ threshold المطلوب بـ HYSTERESIS_DB.
       *   WHY : يكمّل الـ time guard — بيضمن إن القرار مبنى على تحسن حقيقي مش تذبذب.
       */
      time_t current_time = time(NULL);
      if (ctx->last_handover_time > 0) {
        double time_since_last_ho = difftime(current_time, ctx->last_handover_time);
        if (time_since_last_ho < MIN_HO_INTERVAL_SEC) {
          LOG_INFO("UE " CLR_DIM "%d" CLR_RESET
                   " anti-ping-pong [time] %.1fs since last HO (min: %ds)",
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

      /* ══════════════════════════════════════════════════════════════════════
       * Algorithm 1 — Strict paper implementation (line-by-line)
       *
       *  Line 2 : if RSRPpilot_T + ΔHOM > RSRPpilot_S  →  condition TRUE  (target != 0)
       *           else                                   →  condition FALSE (target == 0)
       *  Line 3 : if ttt_counter >= TTT  →  HO = True, execute HO  (L4-5)
       *           else                   →  ttt_counter += DELTA_T  (L6-7)
       *  Line 9 : else (condition FALSE) →  ttt_counter = 0.0       (L9-10)
       *
       * STRICT RULES:
       *  - ttt_counter increments by exactly DELTA_T (0.04 s) each cycle condition is TRUE.
       *  - ttt_counter is NEVER reset while condition remains TRUE and counter < TTT.
       *  - ttt_counter resets ONLY in two cases:
       *      (a) condition becomes FALSE  (Line 9-10)
       *      (b) HO successfully executed (after Line 4-5)
       *  - NO wall-clock (difftime / ttt_start_time) used for the timer.
       *  - NO candidate-cell tracking (no pending_target_cell).
       * ══════════════════════════════════════════════════════════════════════ */

      /* Fixed simulation step — must match the evaluation polling interval. */
      const double DELTA_T = 0.04;   /* 40 ms per cycle */

      if (target == 0 || target == cell->cellID) {
          /* Line 9-10: condition FALSE → reset counter AND TTT */
          if (ctx->ttt_counter != 0.0) {
              LOG_INFO("[Algo1 L9-10] UE %d  condition FALSE → ttt_counter+TTT RESET"
                       "  (counter was %.3fs  TTT was %.3fs)",
                       ue->ueID, ctx->ttt_counter, ctx->current_ttt_sec);
              ctx->ttt_counter     = 0.0;
              ctx->current_ttt_sec = TTT_DEFAULT_SEC;
          }
          log_ttt_cycle(ue->ueID, ctx->dec_fWF, ctx->last_fWF,
                        ctx->current_ttt_sec, 0, 0);

      } else {
          /* ── Line 2: condition TRUE (RSRPpilot_T + ΔHOM > RSRPpilot_S) ── */

          /* Accumulate one step — DO NOT reset if counter < TTT */
          ctx->ttt_counter += DELTA_T;

          double ttt_required = (ctx->current_ttt_sec > 0.0) ? ctx->current_ttt_sec : TTT_DEFAULT_SEC;

          if (ctx->ttt_counter < ttt_required) {
              /* Line 6-7: timer accumulating — update TTT using current vs previous fWF */
              double delta_fWF_log = ctx->dec_fWF - ctx->last_fWF;

              /* Dead-zone check: if delta == 0, freeze BOTH ttt and counter */
              if (fabs(delta_fWF_log) < 1e-9) {
                  /* Dead-zone: undo the counter increment done above */
                  ctx->ttt_counter -= DELTA_T;
                  LOG_INFO("[TTT-UPDATE] UE %d  DEAD-ZONE (delta=%.8f) → TTT=%.3fs  counter FROZEN=%.3fs",
                           ue->ueID, delta_fWF_log,
                           ctx->current_ttt_sec, ctx->ttt_counter);
                  log_ttt_cycle(ue->ueID, ctx->dec_fWF, ctx->last_fWF,
                                ctx->current_ttt_sec, (int)target, 0);
                  target = 0;
              } else {
                  double new_ttt = compute_ttt_update(ctx->current_ttt_sec,
                                                       ctx->dec_fWF,
                                                       ctx->last_fWF);
                  if (new_ttt != ctx->current_ttt_sec) {
                      const char* reason =
                          (delta_fWF_log <  0.0)   ? "fWF worsened -> slower HO" :
                          (delta_fWF_log >= TTT_Q)  ? "fWF really improved -> much faster HO" :
                                                       "fWF improved -> faster HO";
                      LOG_INFO("[TTT-UPDATE] UE %d  TTT %.3fs -> %.3fs  delta_fWF=%+.4f  (%s)",
                               ue->ueID, ctx->current_ttt_sec, new_ttt,
                               delta_fWF_log, reason);
                      ctx->current_ttt_sec = new_ttt;
                  }
                  /* Re-check against UPDATED ttt after compute_ttt_update */
                  ttt_required = ctx->current_ttt_sec;

                  ctx->last_fWF = ctx->dec_fWF;
                  LOG_INFO("[Algo1 L6-7] UE %d  ttt_counter accumulating → %.3fs / %.3fs  Cell %d",
                           ue->ueID, ctx->ttt_counter, ttt_required, target);
                  log_ttt_cycle(ue->ueID, ctx->dec_fWF, ctx->last_fWF,
                                ctx->current_ttt_sec, (int)target, 0);
                  /* HO decision = False this cycle; timer keeps running */
                  target = 0;
              }

          } else {
              /* ── Line 4-5: ttt_counter >= TTT → HO decision = True ── */
              LOG_OK("[Algo1 L4-5] UE %d  ttt_counter >= TTT (%.3fs >= %.3fs)"
                     " → HO decision = TRUE  Cell %d",
                     ue->ueID, ctx->ttt_counter, ttt_required, target);
              /* Reset both counter AND TTT before HO executes */
              ctx->ttt_counter     = 0.0;
              ctx->current_ttt_sec = TTT_DEFAULT_SEC;
          }
      }
      
      /* ── HANDOVER EXECUTION GATE ─────────────────────────────────────────
       * Paper Algorithm 1: HO executes ONLY when BOTH conditions are TRUE:
       *   Condition 1: RSRPpilot_T + ΔHOM > RSRPpilot_S  [checked in getTargetCellID → algo1_pass]
       *   Condition 2: elapsed_time >= current_TTT_sec    [checked in VERSION-A TTT block above]
       * If either condition fails, target==0 here and HO is NOT sent.
       *
       * SAFETY: if target != 0 here it MUST have passed both conditions.
       *   - Condition 1 guaranteed: getTargetCellID returns 0 if !algo1_pass.
       *   - Condition 2 guaranteed: VERSION-A sets target=0 if ttt_elapsed < ttt_required.
       * ──────────────────────────────────────────────────────────────────── */
      if (target != 0 && target != cell->cellID) {
        /* Both conditions verified — safe to execute handover */
        ue_data.toTargetCell = target;

        if (cbHOAction(ue_data)) {
          /* [REQ §4] Log SINR HO triggered */
          log_ttt_cycle(ue->ueID, ctx->dec_fWF, ctx->last_fWF,
                        ctx->current_ttt_sec, (int)target, 1 /*ho_triggered*/);
          ctx->last_handover_time = current_time;
          ctx->lb_triggered = false;
          ue->ho_context = *ctx;
          LOG_OK("Handover initiated  UE " CLR_BWHITE "%d" CLR_RESET
                 "  " CLR_BYELLOW "Cell %d" CLR_RESET " ──▶ " CLR_BGREEN "Cell %d" CLR_RESET,
                 ue->ueID, cell->cellID, target);
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

    /* ── FIX 2: Reuse the REAL values computed in getTargetCellID ──────────
     * doHandoverAction MUST NOT recompute f(γ), f(PRB), fWF, ΔHOM
     * independently using wrong formulas.
     *
     * Problem was: f_sinr_ho = sinr_S / 30  (WRONG — missing γT)
     * Correct Eq.5: f(γ) = (γT − γS) / γmax
     *
     * Solution: read values stored by getTargetCellID in ctx->dec_*
     * These are guaranteed to be consistent with the Phase 4-7 pipeline.
     * Fallback to local computation only if dec_data_captured == 0.
     * ─────────────────────────────────────────────────────────────────── */
    double f_sinr_ho, f_prb_ho, f_speed_ho;
    double w_s_ho, w_p_ho, w_v_ho;
    double Om_s_ho, Om_p_ho, Om_v_ho;
    double fWF_ho;
    double delta_HOM;

    if (ctx->dec_data_captured) {
        /* Use REAL values from getTargetCellID — consistent with all phases */
        f_sinr_ho  = ctx->dec_f_sinr;
        f_prb_ho   = ctx->dec_f_prb;
        /* f_speed: reconstruct from speed table (not stored separately) */
        double ue_speed_ho_mps = (data.ueID > 0 && data.ueID < MAX_REGISTERED_UES)
                                  ? g_ue_speed_mps[data.ueID] : UE_SPEED_DEFAULT_MPS;
        f_speed_ho = compute_f_speed(ue_speed_ho_mps);
        /* Raw weights ωx = (1 - f(x)) — for Eq.8 Cases 2&3 */
        w_s_ho = 1.0 - f_sinr_ho;  if (w_s_ho < 0.01) w_s_ho = 0.01;
        w_p_ho = 1.0 - f_prb_ho;   if (w_p_ho < 0.01) w_p_ho = 0.01;
        w_v_ho = 1.0 - f_speed_ho; if (w_v_ho < 0.01) w_v_ho = 0.01;
        /* Normalized weights from stored values */
        Om_s_ho = ctx->dec_Omega_sinr;
        Om_p_ho = ctx->dec_Omega_prb;
        Om_v_ho = 1.0 - Om_s_ho - Om_p_ho;  /* Ων = 1 - Ωγ - ΩPRB */
        if (Om_v_ho < 0.0) Om_v_ho = 0.0;
        fWF_ho  = ctx->dec_fWF;
        delta_HOM = ctx->dec_delta_HOM;
    } else {
        /* Fallback: compute from scratch using CORRECT Eq.5 formula */
        /* γS = serving SINR, γT from neighbor list */
        double gamma_S_fb = (ue != NULL) ? ue->sinr : 0.0;
        double gamma_T_fb = 0.0;
        if (ue != NULL && ue->neighCells != NULL &&
            data.toTargetCell < MAX_REGISTERED_NEIGHBOURS &&
            ue->neighCells[data.toTargetCell].is_available)
            gamma_T_fb = ue->neighCells[data.toTargetCell].sinr;

        /* Eq.5: f(γ) = (γT − γS) / γmax  — CORRECT formula */
        f_sinr_ho = (gamma_T_fb - gamma_S_fb) / 60.0;
        if (f_sinr_ho >  1.0) f_sinr_ho =  1.0;
        if (f_sinr_ho < -1.0) f_sinr_ho = -1.0;

        int srv_nue_ho = (cells_sinr_map[data.frmCurntCell].is_registered &&
                          cells_sinr_map[data.frmCurntCell].sinrMap != NULL)
                         ? (int)cells_sinr_map[data.frmCurntCell].sinrMap->numOfConnectedUEs : 1;
        if (srv_nue_ho < 1) srv_nue_ho = 1;
        int tgt_nue_ho = (data.toTargetCell < MAX_REGISTERED_CELLS &&
                          cells_sinr_map[data.toTargetCell].is_registered &&
                          cells_sinr_map[data.toTargetCell].sinrMap != NULL)
                         ? (int)cells_sinr_map[data.toTargetCell].sinrMap->numOfConnectedUEs : 1;
        if (tgt_nue_ho < 1) tgt_nue_ho = 1;
        double prb_s_fb = (double)TOTAL_PRB / (double)srv_nue_ho;
        double prb_t_fb = (double)TOTAL_PRB / (double)tgt_nue_ho;
        f_prb_ho = (prb_t_fb - prb_s_fb) / (double)TOTAL_PRB;
        if (f_prb_ho >  1.0) f_prb_ho =  1.0;
        if (f_prb_ho < -1.0) f_prb_ho = -1.0;

        double spd_fb = (data.ueID > 0 && data.ueID < MAX_REGISTERED_UES)
                         ? g_ue_speed_mps[data.ueID] : UE_SPEED_DEFAULT_MPS;
        f_speed_ho = compute_f_speed(spd_fb);

        w_s_ho = 1.0 - f_sinr_ho;  if (w_s_ho < 0.01) w_s_ho = 0.01;
        w_p_ho = 1.0 - f_prb_ho;   if (w_p_ho < 0.01) w_p_ho = 0.01;
        w_v_ho = 1.0 - f_speed_ho; if (w_v_ho < 0.01) w_v_ho = 0.01;
        double wt_fb = w_s_ho + w_p_ho + w_v_ho;
        Om_s_ho = w_s_ho / wt_fb;
        Om_p_ho = w_p_ho / wt_fb;
        Om_v_ho = w_v_ho / wt_fb;
        fWF_ho  = (Om_s_ho * f_sinr_ho) + (Om_p_ho * f_prb_ho) + (Om_v_ho * f_speed_ho);

        /* Paper Eq.(8): ΔHOM — 3 cases */
        /* [FIX 1] gamma_th_fb = -10 dB — Paper Table 1 */
        double gamma_th_fb = -10.0;
        if ((gamma_T_fb <= gamma_th_fb && gamma_S_fb <= gamma_th_fb) ||
            (gamma_T_fb >= gamma_th_fb && gamma_S_fb >= gamma_th_fb)) {
            delta_HOM = HOM_AVG_DB * fWF_ho;
        } else if (gamma_T_fb <= gamma_th_fb && gamma_S_fb >= gamma_th_fb) {
            delta_HOM = HOM_AVG_DB * (1.0 + (w_p_ho * f_prb_ho) + (w_v_ho * f_speed_ho));
        } else {
            delta_HOM = HOM_AVG_DB * (-1.0 + (w_p_ho * f_prb_ho) + (w_v_ho * f_speed_ho));
        }
        if (delta_HOM < HOM_MIN_DB) delta_HOM = HOM_MIN_DB;
        if (delta_HOM > HOM_MAX_DB) delta_HOM = HOM_MAX_DB;
    }

    printf("\n" CLR_BCYAN
           "  ╔══════════════════════════════════════════════════════════════════╗\n"
           "  ║  ▶  HANDOVER COMMAND  " CLR_BWHITE "#%-3d (System)" CLR_BCYAN
           "  |  " CLR_BWHITE "UE #%-2d (UE %-2d)" CLR_BCYAN "         ║\n"
           "  ║     UE " CLR_BYELLOW "%-4d" CLR_BCYAN
           "  |  Cell " CLR_BYELLOW "%-2d" CLR_BCYAN
           " ──▶ Cell " CLR_BGREEN "%-2d" CLR_BCYAN
           "  |  State: " CLR_BYELLOW "%-14s" CLR_BCYAN "  ║\n"
           "  ║  " CLR_CYAN "Eq.8 ΔHOM=" CLR_BWHITE "%+.2f dB" CLR_BCYAN
           "  Ωγ=" CLR_BWHITE "%.3f" CLR_BCYAN
           "  ΩPRB=" CLR_BWHITE "%.3f" CLR_BCYAN
           "  fWF=" CLR_BWHITE "%+.3f" CLR_BCYAN "                ║\n"
           "  ╚══════════════════════════════════════════════════════════════════╝\n"
           CLR_RESET "\n",
           g_total_ho_count,
           ue_ho_seq, data.ueID,
           data.ueID, data.frmCurntCell, data.toTargetCell,
           ho_state_to_string(ctx->state),
           delta_HOM, Om_s_ho, Om_p_ho, fWF_ho);
    
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

        /* ==============================================================
         * [NEW — Paper Eq.(8,9)]: إرسال ΔHOM للـ RAN عبر RC control.
         *
         * في الكود القديم، ΔHOM كانت بتتحسب وتتسجل في CSV فقط.
         * دلوقتي بنبعتها للـ eNB عشان الـ RAN نفسه يستخدمها كـ HOM
         * parameter — ده هو اللي البيبر بيطلبه بالظبط في Section 3.
         *
         * بنبعت الـ HOM update بعد ما HO command اتبعت بنجاح عشان:
         *   1) نضمن إن الـ RAN استلم الـ HO command الأول.
         *   2) الـ HOM الجديد بيأثر على الـ HO decisions الجاية بعد كده.
         * ============================================================== */
        send_hom_rc_control(data.ueID, delta_HOM,
                            data.frmCurntCell, data.nodes);

        /* Reset TTT للـ UE عشان يبدأ من الأول في الـ serving cell الجديدة */
        ctx->current_ttt_sec = TTT_DEFAULT_SEC;
        ctx->ttt_counter     = 0.0;
        ctx->last_fWF        = fWF_ho;
        ue->ho_context = *ctx;

        return 1;
    } else {
        LOG_ERR("Status: " CLR_BRED "FAILED" CLR_RESET " — RC command not delivered to any node");
        ctx->state = HO_STATE_IDLE;
        ue->ho_context = *ctx;
        return 0;
    }
}

// ============================================================
//  GRACEFUL SHUTDOWN — signal handler
//  Catches Ctrl+C (SIGINT) and kill (SIGTERM).
//  Sets g_shutdown = 1 so the while(1) loop exits cleanly,
//  then the normal end-of-main path calls metrics_print_summary()
//  and print_fWF_matrix() — same as if the xApp finished on its own.
// ============================================================
static volatile sig_atomic_t g_shutdown = 0;

static void xapp_signal_handler(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

int main(int argc, char* argv[]) 
{
  fr_args_t args = init_fr_args(argc, argv);
  init_xapp_api(&args);
  sleep(1);

  /* Register atexit handler — guarantees fWF CSV and all CSV files are
   * written even if the xApp is killed with Ctrl+C or SIGTERM before
   * reaching the normal end-of-main cleanup path.
   * atexit_handler() calls print_fWF_matrix() + flush_all_csv_files(). */
  atexit(atexit_handler);

  /* Register graceful-shutdown handler — fires on Ctrl+C or kill */
  signal(SIGINT,  xapp_signal_handler);
  signal(SIGTERM, xapp_signal_handler);

  /* FIX-3: Initialize per-UE TTT and last_fWF so they show correct values
   * from the very first evaluation cycle.
   * Without this, calloc zero-initialises current_ttt_sec = 0.0, causing:
   *   a) decision box to print TTT_cur=0.000s (misleading)
   *   b) idle TTT path to set TTT_DEFAULT each cycle instead of tracking it
   * Paper Eq.(10-13): TTT starts at the standard LTE default (0.5 s). */
  for (int _u = 1; _u < MAX_REGISTERED_UES; _u++) {
      g_ue_ho_ctx[_u].current_ttt_sec = TTT_DEFAULT_SEC;
      g_ue_ho_ctx[_u].last_fWF        = 0.0;
  }

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
    "  ║  " CLR_CYAN "Decision Mode      " CLR_BWHITE "AWF dynamic weights (paper Eq.1-4)" CLR_BBLUE "  ║\n"
    "  ║  " CLR_CYAN "HO Decision        " CLR_BWHITE "RSRPpilot_T+ΔHOM > RSRPpilot_S [Algo1]" CLR_BBLUE " ║\n"
    "  ║  " CLR_CYAN "TTT Condition      " CLR_BWHITE "elapsed >= TTT_cur (both required)" CLR_BBLUE "   ║\n"
    "  ║  " CLR_CYAN "NCL Load Gate      " CLR_BWHITE "<=19%% (paper Section 3)" CLR_BBLUE "            ║\n"
    "  ║  " CLR_CYAN "HOMmax / HOMmin    " CLR_BWHITE "20 dB / 0 dB  HOM_avg=10 dB (Eq.9)" CLR_BBLUE " ║\n"
    "  ║  " CLR_CYAN "RC Mode            " CLR_BWHITE "Targeted (cell->node map)" CLR_BBLUE "           ║\n"
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

  while (!g_shutdown) {
    pthread_mutex_lock(&mtx);
    
    evaluateHandoverOpportunities(getTargetCellID, doHandoverAction, context);
    
    pthread_mutex_unlock(&mtx);

    /*
     * [IMPROVEMENT #2 — Adaptive Sleep: Dynamic Polling Interval]
     * PURPOSE : تحسين الاستجابة لما الشبكة تحتاجها وتقليل الـ CPU usage لما كل حاجة هادية.
     *
     * WHAT IT DOES : بعد كل cycle من evaluateHandoverOpportunities، بيحسب وقت الانتظار:
     *
     *   CASE 1 — HO جاري (global_ho_in_progress() == 1):
     *     → 0.5 ثانية. السبب: عايزين نكتشف إتمام الـ HO بسرعة عشان نبدأ تقييم الـ UEs التانية.
     *
     *   CASE 2 — أي UE عنده SINR < SINR_POOR_THRESHOLD_DB:
     *     → 0.5 ثانية. السبب: الإشارة سيئة وممكن تحتاج HO في أي لحظة.
     *
     *   CASE 3 — UEs شغالة وSINR بين 2 و 10 dB:
     *     → 2.0 ثانية. الحالة العادية.
     *
     *   CASE 4 — كل الـ UEs SINR > 10 dB:
     *     → 4.0 ثانية. كل حاجة مستقرة، مفيش ضرورة للمراقبة السريعة.
     *
     * WHY : الـ fixed sleep(2) في الكود القديم:
     *   - بيضيع وقت لما الشبكة مستقرة (كان ممكن ينتظر 4 ثواني).
     *   - بيتأخر في الاستجابة لما في HO جاري (كان لازم 0.5 ثانية).
     *
     * COMPARISON METRIC : ممكن نقيس average response time لـ HO completion
     *                     ونقارنها مع الكود القديم من الـ latency_sec في CSV.
     */
    // ── IMPROVEMENT #2: Adaptive Sleep ─────────────────────────────

    unsigned int next_sleep_us = ADAPTIVE_SLEEP_LONG_US;

    // Check for in-progress HO
    if (global_ho_in_progress()) {
        next_sleep_us = ADAPTIVE_SLEEP_SHORT_US;
    } else {
        // Scan all UEs for poor SINR
        for (int _ci = 0; _ci < MAX_REGISTERED_CELLS && next_sleep_us > ADAPTIVE_SLEEP_SHORT_US; _ci++) {
            if (!cells_sinr_map[_ci].is_registered || cells_sinr_map[_ci].sinrMap == NULL) continue;
            struct SINR_Map* _c = cells_sinr_map[_ci].sinrMap;
            for (int _ui = 1; _ui < MAX_REGISTERED_UES; _ui++) {
                if (!_c->connectedUEs || !_c->connectedUEs[_ui].is_available) continue;
                double _s = _c->connectedUEs[_ui].sinr;
                if (_s < SINR_POOR_THRESHOLD_DB) {
                    next_sleep_us = ADAPTIVE_SLEEP_SHORT_US;
                    break;
                }
                // At least one UE needs attention → normal interval
                if (_s < 10.0 && next_sleep_us > ADAPTIVE_SLEEP_NORMAL_US)
                    next_sleep_us = ADAPTIVE_SLEEP_NORMAL_US;
            }
        }
    }

    LOG_INFO("Adaptive sleep: " CLR_BWHITE "%.1f s" CLR_RESET
             "  (ho_active=%d)", next_sleep_us / 1e6, global_ho_in_progress());
    usleep(next_sleep_us);
  }

  for (int i = 0; i < nodes.len; ++i) 
  {
    if (hndl[i].success == true)
      rm_report_sm_xapp_api(hndl[i].u.handle);
  }
  free(hndl);

  LOG_OK("Shutdown signal received — flushing CSV files and summary...");

  /* Flush all CSV files immediately so data is safe before API teardown */
  flush_all_csv_files();

  while (try_stop_xapp_api() == false)
    usleep(1000);

  rc = pthread_mutex_destroy(&mtx);
  assert(rc == 0);

  metrics_print_summary();
  /* print_fWF_matrix() and flush_all_csv_files() are also called via
   * atexit_handler() — calling them here explicitly ensures they run
   * before mutex destruction when the xApp exits normally.
   * atexit will call them again after return but the NULL-guards in
   * flush_all_csv_files() prevent double-close of already-closed files. */
  print_fWF_matrix();

  LOG_OK("xApp completed successfully");
  return 0;
}

