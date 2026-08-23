from edge_impulse_linux.image import ImageImpulseRunner
import cv2
import os

MODEL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gesture_model.eim")

class HandGesture:
    def __init__(self, model_path=MODEL_PATH):
        self.runner = None
        try:
            print(f"[hand_gesture] Loading model: {model_path}")
            self.runner = ImageImpulseRunner(model_path)
            model_info = self.runner.init()
            print(f"[hand_gesture] Loaded: {model_info['project']['name']}")
        except Exception as e:
            print(f"[hand_gesture] Failed to load model: {e}")
            self.runner = None

    def is_ready(self):
        return self.runner is not None

    def detect(self, frame):
        if self.runner is None:
            return []
        try:
            img = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            features, cropped = self.runner.get_features_from_image_auto_studio_settings(img)
            res = self.runner.classify(features)
            detections = []
            if "bounding_boxes" in res["result"].keys():
                for bb in res["result"]["bounding_boxes"]:
                    detections.append({
                        "label": bb["label"], "confidence": bb["value"],
                        "x": bb["x"], "y": bb["y"], "width": bb["width"], "height": bb["height"],
                    })
            return detections
        except Exception as e:
            print(f"[hand_gesture] Inference error: {e}")
            return []

    def stop(self):
        if self.runner:
            self.runner.stop()

gesture_model = HandGesture()
