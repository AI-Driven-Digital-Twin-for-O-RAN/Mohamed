import subprocess
import json
import threading
import os
import signal
from http.server import BaseHTTPRequestHandler, HTTPServer

XAPP_BIN = "/home/omar_farouk/open-ran-clean/yousef_fathy/flexric/build/examples/xApp/c/ctrl/xapp_rc_handover_ctrl"
LOG_FILE  = 'xapp_rc_handover_ctrl.log'
INTERVAL  = 8   # seconds between successive xApp runs

_stop_event   = threading.Event()
_loop_thread  = None
_current_proc = None
_lock         = threading.Lock()


def _xapp_loop():
    global _current_proc
    while not _stop_event.is_set():
        with open(LOG_FILE, 'a') as log:
            print("[xApp loop] Starting run...")
            proc = subprocess.Popen(
                f"stdbuf -oL -eL {XAPP_BIN}",
                shell=True, stdout=log, stderr=log, executable='/bin/bash'
            )
            with _lock:
                _current_proc = proc
            proc.wait()
            with _lock:
                _current_proc = None
        print("[xApp loop] Run complete. Waiting before next run...")
        _stop_event.wait(timeout=INTERVAL)
    print("[xApp loop] Stopped.")


def _start_loop():
    global _loop_thread
    if _loop_thread and _loop_thread.is_alive():
        return False   # already running
    _stop_event.clear()
    _loop_thread = threading.Thread(target=_xapp_loop, daemon=True)
    _loop_thread.start()
    return True


def _stop_loop():
    global _current_proc
    _stop_event.set()
    with _lock:
        p = _current_proc
    if p and p.poll() is None:
        try:
            p.terminate()
        except Exception:
            pass


def _is_running():
    if _loop_thread and _loop_thread.is_alive():
        return True
    # fallback: check if the binary is running outside of our loop
    r = subprocess.run("pgrep -f xapp_rc_handover_ctrl",
                       shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode == 0


def run_startup_commands():
    """Auto-start the stop server on port 38869."""
    try:
        subprocess.Popen(
            'python3 stop_xApp.py',
            shell=True,
            stdout=open('stop_xApp.log', 'w'),
            stderr=subprocess.STDOUT,
            executable='/bin/bash'
        )
    except Exception as e:
        print(f"[startup] Warning: {e}")


class BashRequestHandler(BaseHTTPRequestHandler):

    def do_POST(self):
        length   = int(self.headers.get('Content-Length', 0))
        body     = self.rfile.read(length).decode('utf-8').strip() if length else ''

        if body == 'start':
            started = _start_loop()
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            msg = b'xApp loop started.' if started else b'xApp loop already running.'
            self.wfile.write(msg)

        elif body == 'stop':
            _stop_loop()
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(b'xApp loop stopped.')

        else:
            self.send_response(400)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"Use 'start' or 'stop'.")

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps({'running': _is_running()}).encode())

    def log_message(self, fmt, *args):
        pass   # silence HTTP access log


def run(server_class=HTTPServer, handler_class=BashRequestHandler, port=38868):
    run_startup_commands()
    httpd = server_class(('', port), handler_class)
    print(f'[xApp trigger] listening on port {port}')
    httpd.serve_forever()


if __name__ == '__main__':
    run()
