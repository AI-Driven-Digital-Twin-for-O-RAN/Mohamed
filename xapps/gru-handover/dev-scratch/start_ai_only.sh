#!/bin/bash
# تشغيل حاوية الـ AI فقط (إن لم تكن شغالة). لا يبدأ الـ RIC ولا الـ xApp.
set -e
if docker ps --format '{{.Names}}' | grep -qx xapp-ai; then
  echo "[*] AI container 'xapp-ai' already running on port 5000."
  exit 0
fi
echo "[*] Starting AI container (mohamed710/xapp:v1) on port 5000..."
docker run --rm -d -p 5000:5000 --name xapp-ai mohamed710/xapp:v1
echo "[*] Done. Check: curl -s http://localhost:5000/health"
