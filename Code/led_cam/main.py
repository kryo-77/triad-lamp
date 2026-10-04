import cv2
import time
import queue
import threading
import requests
import mediapipe as mp
import os, urllib.request
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

#CAM_URL  = "http://192.168.1.10"   # ESP32-CAM IP (from its serial monitor)
CAM_URL = 1
LAMP_URL = "http://192.168.1.3"    # lamp IP; use the IP, not lamp.local (mDNS lookups on Windows can be slow)
FLIP_MODE = None   # None = no flip, 1 = mirror, 0 = upside down, -1 = rotate 180
# ---------- lamp control (background thread so video never stalls) ----------
cmd_q = queue.Queue(maxsize=1)      # only the latest command matters
session = requests.Session()

def lamp_get(path):
    try:
        session.get(LAMP_URL + path, timeout=1.5)
    except requests.RequestException as e:
        print("lamp error:", e)

def apply_fingers(n):
    if n == 0:
        lamp_get("/alloff")
    elif n >= 4:
        lamp_get("/allon")
    else:
        levels = {"left": 255 if n >= 1 else 0,
                  "right": 255 if n >= 2 else 0,
                  "stem": 255 if n >= 3 else 0}
        for led, v in levels.items():
            lamp_get(f"/led?id={led}&value={v}")

def lamp_worker():
    while True:
        apply_fingers(cmd_q.get())

threading.Thread(target=lamp_worker, daemon=True).start()

def send_latest(n):
    try:
        cmd_q.get_nowait()          # drop stale command
    except queue.Empty:
        pass
    cmd_q.put(n)

# ---------- camera grabber (always keeps the newest frame) ----------
class FrameGrabber(threading.Thread):
    def __init__(self, url):
        super().__init__(daemon=True)
        self.cap = cv2.VideoCapture(url)
        self.frame = None

    def run(self):
        while True:
            ok, f = self.cap.read()
            if ok:
                self.frame = f
            else:
                time.sleep(0.05)

grabber = FrameGrabber(CAM_URL)
grabber.start()

MODEL = "hand_landmarker.task"
if not os.path.exists(MODEL):
    urllib.request.urlretrieve(
        "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
        MODEL)

landmarker = vision.HandLandmarker.create_from_options(
    vision.HandLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=MODEL),
        running_mode=vision.RunningMode.VIDEO,
        num_hands=1,
        min_hand_detection_confidence=0.6,
        min_tracking_confidence=0.6))

FINGERS = [(8, 6), (12, 10), (16, 14), (20, 18)]

def count_fingers(lm):
    return sum(lm[tip].y < lm[pip].y for tip, pip in FINGERS)

last_sent, stable_val, stable_count = -1, -1, 0

while True:
    frame = grabber.frame
    if frame is None:
        time.sleep(0.01)
        continue

    frame = frame.copy() if FLIP_MODE is None else cv2.flip(frame.copy(), FLIP_MODE)
    h, w = frame.shape[:2]
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    img = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
    result = landmarker.detect_for_video(img, int(time.time() * 1000))

    fingers = 0
    if result.hand_landmarks:
        lm = result.hand_landmarks[0]
        fingers = count_fingers(lm)
        for p in lm:
            cv2.circle(frame, (int(p.x * w), int(p.y * h)), 3, (0, 255, 0), -1)

    # debounce: same value for 5 frames in a row before acting
    if fingers == stable_val:
        stable_count += 1
    else:
        stable_val, stable_count = fingers, 1

    if stable_count >= 5 and stable_val != last_sent:
        send_latest(stable_val)
        last_sent = stable_val
        print("fingers =", stable_val)

    cv2.putText(frame, f"Fingers: {fingers}", (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
    cv2.imshow("Gesture control", frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()