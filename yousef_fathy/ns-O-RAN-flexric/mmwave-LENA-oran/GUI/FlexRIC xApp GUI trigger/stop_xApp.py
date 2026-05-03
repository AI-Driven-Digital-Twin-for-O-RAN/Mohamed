import os
import signal
import subprocess
from http.server import BaseHTTPRequestHandler, HTTPServer

class BashRequestHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        import urllib.request
        # Tell the trigger server to stop its loop
        try:
            req = urllib.request.Request(
                'http://localhost:38868/',
                data=b'stop',
                headers={'Content-Type': 'text/plain'},
                method='POST'
            )
            urllib.request.urlopen(req, timeout=3)
        except Exception:
            pass

        # Also kill any lingering binary processes
        subprocess.run("pkill -TERM -f xapp_rc_handover_ctrl", shell=True)

        self.send_response(200)
        self.send_header('Content-type', 'text/plain')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(b'xApp loop stopped.')

    def do_GET(self):
        self.send_response(405)
        self.send_header('Content-type', 'text/plain')
        self.end_headers()
        self.wfile.write(b"Use POST to stop processes by partial name.")

def run(server_class=HTTPServer, handler_class=BashRequestHandler, port=38869):
    server_address = ('', port)
    httpd = server_class(server_address, handler_class)
    print(f'Starting httpd on port {port}...')
    httpd.serve_forever()

if __name__ == '__main__':
    run()
