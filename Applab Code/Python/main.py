from arduino.app_utils import App, Bridge
import time
from esp32_stream import camera
from hand_gesture import gesture_model
from stream_relay import relay

FRAME_INTERVAL = 0.5
WARMUP_SECONDS = 5.0
DEBOUNCE_COUNT = 5

last_label = None
consecutive_count = 0

def loop():
    global last_label, consecutive_count
    try:
        frame = camera.get_frame()
        if frame is None:
            time.sleep(FRAME_INTERVAL); return
        if camera.last_jpg_bytes is not None:
            relay.update_frame(camera.last_jpg_bytes)
        if not camera.is_warmed_up(WARMUP_SECONDS):
            time.sleep(FRAME_INTERVAL); return
        if not gesture_model.is_ready():
            time.sleep(FRAME_INTERVAL); return

        detections = gesture_model.detect(frame)
        current_label = detections[0]['label'] if detections else None

        if current_label is not None and current_label == last_label:
            consecutive_count += 1
        else:
            consecutive_count = 1 if current_label else 0
            last_label = current_label

        if current_label:
            print(f"[main] {current_label} ({consecutive_count}/{DEBOUNCE_COUNT})")
        else:
            print("[main] No gesture detected")

        if consecutive_count >= DEBOUNCE_COUNT:
            try:
                if Bridge.call("is_camera_mode_active"):
                    print(f"[main] Confirmed: {current_label} — triggering")
                    Bridge.call("receive_gesture", current_label)
                    while not Bridge.call("is_action_complete"):
                        time.sleep(0.2)
                    print("[main] Action complete")
                else:
                    print("[main] Gesture confirmed but camera mode is OFF — ignoring")
            except Exception as e:
                print(f"[main] Bridge error: {e}")
            consecutive_count = 0
            last_label = None  # must release + redetect before retrigger

    except Exception as e:
        print(f"[main] Unexpected error: {e}")
    time.sleep(FRAME_INTERVAL)

App.run(user_loop=loop)
