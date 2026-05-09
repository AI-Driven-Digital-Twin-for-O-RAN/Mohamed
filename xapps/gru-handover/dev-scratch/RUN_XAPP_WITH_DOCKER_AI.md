# تشغيل الـ xApp باستخدام الـ AI داخل Docker

## 1) تثبيت حزم الـ LSTM client (مرة واحدة)

الـ xApp يتصل بخدمة الـ AI عبر HTTP (libcurl + json-c). ثبّت حزم التطوير:

```bash
sudo apt-get update
sudo apt-get install -y libcurl4-openssl-dev libjson-c-dev
```

## 2) بناء الـ xApp (Handover + LSTM) داخل FlexRIC

```bash
cd /home/mhmd/Documents/o-ran/yousef_fathy/flexric/build
cmake .. -DE2AP_VERSION=E2AP_V1 -DKPM_VERSION=KPM_V3_00
make -j4 xapp_handover_lstm
```

الـ binary يكون في:
`flexric/build/examples/xApp/c/handover_lstm/xapp_handover_lstm`

## 3) تشغيل الـ AI في Docker

```bash
docker run --rm -d -p 5000:5000 --name xapp-ai mohamed710/xapp:v1
```

(أو استخدم الصورة المحلية بعد البناء من مجلد `ai/Fares`.)

## 4) تشغيل الـ RIC ثم الـ xApp

**إذا ظهر خطأ `errno = 98` أو `Address already in use`:** المنفذ 36421 مستخدم (مثلاً من تشغيل سابق للـ RIC). إما:
- أوقف العملية القديمة: `pkill -9 nearRT-RIC`
- أو غيّر المنفذ في ملف الإعدادات: في `/usr/local/etc/flexric/flexric.conf` (أو الملف الذي تستخدمه بـ `-c`) أضف أو عدّل: `NEAR_RIC_PORT = 36422`

**ترمينال 1 – الـ RIC:**
```bash
cd /home/mhmd/Documents/o-ran/yousef_fathy/flexric/build/examples/ric
./nearRT-RIC
```

**ترمينال 2 – الـ xApp (بعد تشغيل الـ RIC):**
```bash
cd /home/mhmd/Documents/o-ran/yousef_fathy/flexric/build/examples/xApp/c/handover_lstm
export LSTM_SERVICE_URL=http://localhost:5000   # اختياري؛ الافتراضي localhost:5000
./xapp_handover_lstm
```

الـ xApp يستخدم الـ AI في Docker تلقائياً عبر `LSTM_SERVICE_URL` (الافتراضي `http://localhost:5000`).

## ملخص

| الخطوة | الأمر |
|--------|--------|
| تثبيت حزم التطوير | `sudo apt install libcurl4-openssl-dev libjson-c-dev` |
| بناء الـ xApp | `cd flexric/build && cmake .. && make xapp_handover_lstm` |
| تشغيل الـ AI | `docker run --rm -d -p 5000:5000 --name xapp-ai mohamed710/xapp:v1` |
| تشغيل الـ RIC | `./flexric/build/examples/ric/nearRT-RIC` |
| تشغيل الـ xApp | `./flexric/build/examples/xApp/c/handover_lstm/xapp_handover_lstm` |
