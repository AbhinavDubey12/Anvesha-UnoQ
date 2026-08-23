import http.server
import threading
import time

RELAY_PORT = 8081
BOUNDARY = "frameboundary"

class FrameBuffer:
    def __init__(self):
        self._lock = threading.Lock()
        self._jpg_bytes = None

    def update(self, jpg_bytes):
        with self._lock:
            self._jpg_bytes = jpg_bytes

    def get(self):
        with self._lock:
            return self._jpg_bytes

frame_buffer = FrameBuffer()

class RelayHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", f"multipart/x-mixed-replace;boundary={BOUNDARY}")
        self.end_headers()
        try:
            while True:
                jpg = frame_buffer.get()
                if jpg is not None:
                    self.wfile.write(f"--{BOUNDARY}\r\n".encode())
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(f"Content-Length: {len(jpg)}\r\n\r\n".encode())
                    self.wfile.write(jpg)
                    self.wfile.write(b"\r\n")
                time.sleep(0.3)
        except (BrokenPipeError, ConnectionResetError):
            pass  # viewer just closed the tab — not an error

    def log_message(self, format, *args):
        pass  # silence default per-request console spam

class RelayServer:
    def __init__(self, port=RELAY_PORT):
        self.port = port
        self.server = None

    def start(self):
        # ThreadingHTTPServer, specifically — this is what actually enables
        # multiple simultaneous viewers, which was the whole point
        self.server = http.server.ThreadingHTTPServer(("0.0.0.0", self.port), RelayHandler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        print(f"[stream_relay] Relay running on port {self.port}")

    def update_frame(self, jpg_bytes):
        frame_buffer.update(jpg_bytes)

relay = RelayServer()
