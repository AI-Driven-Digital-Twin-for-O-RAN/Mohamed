# تشغيل الستاك الكامل: RIC + xApp (Handover + AI من Docker)

## ترتيب التشغيل

### 1) تشغيل الـ AI (Docker) — مرة واحدة أو إذا أوقفته

```bash
docker run --rm -d -p 5000:5000 --name xapp-ai mohamed710/xapp:v1
```

تحقق: `curl -s http://localhost:5000/health` يجب أن يرجع `"status":"ok"`.

---

### 2) تشغيل الـ RIC — ترمينال 1 (اتركه مفتوحاً)

```bash
cd ~/Documents/o-ran/yousef_fathy/flexric/build/examples/ric
./nearRT-RIC
```

إذا ظهر `errno = 98`: نفّذ `pkill -9 nearRT-RIC` ثم شغّل الـ RIC مرة أخرى.

---

### 3) تشغيل الـ xApp — ترمينال 2

```bash
cd ~/Documents/o-ran/yousef_fathy/flexric/build/examples/xApp/c/handover_lstm
./xapp_handover_lstm
```

أو من مجلد المشروع:

```bash
cd ~/Documents/o-ran/yousef_fathy/HANDOVER_xApp_Test
./run_xapp_with_docker_ai.sh
```

الـ xApp يتصل تلقائياً بـ:
- **الـ RIC** (على المنفذ 36422 للـ iApp)
- **الـ AI في Docker** (على `http://localhost:5000`)

---

## ربط الـ RAN (ns-3 + E2 Simulator)

لإرسال قياسات KPM حقيقية من السيناريو إلى الـ RIC ثم الـ xApp:

1. شغّل الـ RIC والـ xApp كما فوق.
2. من مشروع ns-O-RAN شغّل السيناريو (مثلاً Energy_Saving_with_load_balancing).
3. شغّل الـ E2 simulator بحيث يتصل بـ `127.0.0.1:36421` (E2AP).

بهذا الـ xApp يستقبل القياسات ويستدعي الـ LSTM في Docker لقرار الـ handover.
