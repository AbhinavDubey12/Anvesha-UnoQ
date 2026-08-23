import cv2
import time
import urllib.request
import numpy as np

STREAM_URL = ""

class ESP32Stream:
    def __init__(self, url=STREAM_URL):
        self.url = url
        self.stream = None
        self.bytes_buffer = bytes()
        self.last_jpg_bytes = None
        self.connected_at = None
        try:
            self._connect()
        except Exception as e:
            print(f"[esp32_stream] Initial connection failed: {e}")
            print("[esp32_stream] Will retry on next get_frame() call")

    def _connect(self):
        print(f"[esp32_stream] Connecting to {self.url} ...")
        self.stream = urllib.request.urlopen(self.url, timeout=5)
        self.bytes_buffer = bytes()
        self.connected_at = time.time()
        print("[esp32_stream] Connected — stream found, fetching frames now")

    def is_warmed_up(self, warmup_seconds=5.0):
        if self.connected_at is None:
            return False
        return (time.time() - self.connected_at) >= warmup_seconds

    def get_frame(self, timeout=3.0):
        if self.stream is None:
            try:
                self._connect()
            except Exception as e:
                print(f"[esp32_stream] Reconnect attempt failed: {e}")
                return None

        start = time.time()
        while (time.time() - start) < timeout:
            try:
                self.bytes_buffer += self.stream.read(2048)
            except Exception as e:
                print(f"[esp32_stream] read error: {e}, reconnecting...")
                self._reconnect()
                return None

            s = self.bytes_buffer.find(b'\xff\xd8')
            e = self.bytes_buffer.find(b'\xff\xd9')
            if s != -1 and e != -1 and s < e:
                jpg_bytes = self.bytes_buffer[s:e+2]
                self.bytes_buffer = self.bytes_buffer[e+2:]
                self.last_jpg_bytes = jpg_bytes
                return cv2.imdecode(np.frombuffer(jpg_bytes, dtype=np.uint8), cv2.IMREAD_COLOR)

        print("[esp32_stream] Timed out waiting for a frame, forcing reconnect...")
        self._reconnect()
        return None

    def _reconnect(self):
        try:
            self.stream.close()
        except Exception:
            pass
        self.stream = None
        try:
            self._connect()
        except Exception as e:
            print(f"[esp32_stream] Reconnect failed: {e}")

camera = ESP32Stream()
