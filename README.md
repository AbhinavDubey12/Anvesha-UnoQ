# Anvesha — Autonomous Vision & Gesture-Controlled Rover

Autonomous rover built on the Arduino UNO Q (dual Linux MPU + STM32 MCU), combining reactive ultrasonic navigation, live on-device hand-gesture recognition via camera, and IMU-based safety monitoring — with a motor-control layer executed by a dedicated ESP32.

## Overview

By default, the rover navigates autonomously using three ultrasonic sensors (front/left/right), avoiding obstacles reactively. A physical button toggles an additional camera mode: a live ESP32-CAM feed is processed on-device by a custom-trained Edge Impulse object detection model, recognizing hand gestures that trigger specific rover actions. An MPU9250 IMU runs continuously as an independent safety layer, watching for tip-over or sudden impact regardless of what else the rover is doing. A live relay lets the camera feed be viewed from any browser on the network at the same time.

## Features

- Default autonomous reactive navigation (3x ultrasonic, North/East/West)
- Button-toggled camera mode with on-device gesture recognition (Edge Impulse FOMO, INT8, running on the UNO Q's Linux side)
- Two recognized gestures: **FULL_HAND** (controlled reverse) and **THUMPS_UP** (choreographed movement sequence)
- Live, multi-client-viewable camera feed via a custom relay
- MPU9250-based tip-over/impact safety monitoring, independent of navigation or gesture state
- LED status indicators + buzzer feedback
- UART link between the UNO Q (decision-making) and a dedicated ESP32 (motor execution)
- Per-wheel quadrature encoders for distance tracking

## Hardware Components

| Component | Role |
|---|---|
| Arduino UNO Q | Main brain — Linux (camera/AI) + STM32 (sensors/decisions) |
| ESP32 Dev Kit V1 | Motor + encoder controller |
| ESP32-CAM (AI-Thinker) | Live video source |
| 4x DC gear motors w/ quadrature encoders | Drive |
| L298N motor driver(s) | Motor power/direction |
| 3x HC-SR04 / HC-SR04P ultrasonic sensors | Reactive navigation |
| MPU9250 | IMU — safety monitoring |
| 4x LED, 1x buzzer, 1x push button | Status/mode indication |
| Chassis, wheels, battery, wiring | — |



## System Architecture



```
ESP32-CAM (video)
      ↓
UNO Q — Linux side (Python): fetch frame → Edge Impulse inference → debounce
      ↓ (Bridge RPC)
UNO Q — STM32 side (sketch): ultrasonic + IMU + LEDs/buzzer/button → decision
      ↓ (UART)
ESP32: motor execution + quadrature encoder tracking
```

## Software Stack

- **Arduino App Lab** — dual-processor (Linux + STM32) app framework
- **Python** (Linux/MPU side): OpenCV, Edge Impulse Linux SDK, a custom lightweight MJPEG relay
- **C++/Arduino** (STM32 sketch side): `Arduino_RouterBridge` for Python↔sketch RPC, ultrasonic + IMU sensing
- **C++/Arduino** (ESP32 side): UART command parsing, motor control, interrupt-driven encoder tracking

## Wiring Overview

**UNO Q**
- Ultrasonic: D2–D7 (West/North/East, trig+echo pairs)
- MPU9250: D20 (SDA) / D21 (SCL) — I2C2
- UART to ESP32: D0 (RX) / D1 (TX)
- LEDs: D8–D11 · Button: D12 · Buzzer: D13

**ESP32**
- UART: GPIO25 (RX2) / GPIO26 (TX2)
- Motor direction (shared per side): Left IN1/IN2 → GPIO27/14, Right IN1/IN2 → GPIO13/15
- Independent PWM speed: FL=32, RL=33, FR=4, RR=23
- Encoders (A/B): FL=34/35, RL=19/21, FR=2/22, RR=5/18

Full reasoning behind each pin choice is in the code comments.

## Key Engineering Challenges & Solutions

- **ESP32-CAM stream reliability:** `cv2.VideoCapture`'s FFmpeg/GStreamer backend proved unreliable against the board's minimal MJPEG server under real-world network jitter. Solved with a manual `urllib` + JPEG-marker-parsing fetcher, with reconnect-on-failure logic.
- **Single-client limitation of the ESP32-CAM's server:** its onboard server can only serve one viewer at a time. Solved by having the UNO Q relay already-fetched frames to any number of viewers via a lightweight custom HTTP MJPEG relay, rather than exposing the fragile camera directly.
- **Gesture model iteration:** started from a broader gesture set; iterative testing surfaced background-labeling issues, data-volume limits, and a genuine visual-similarity ceiling between finger-count gestures. Converged on two visually distinct, reliably-separable gestures for real-world performance.
- **App Lab dependency management:** `requirements.txt` must live inside the app's `python/` subfolder (not the app root), and requires a full app close/reopen — not just a re-run — for App Lab to pick it up.
- **Platform-specific I2C differences:** the UNO Q's STM32/Zephyr core uses a fixed-pin, no-argument `Wire.begin()`, unlike the flexible-pin-argument convention common in ESP32-based reference code.
- **Motor pin budget:** adopted a shared-direction / independent-PWM wiring scheme (paired left/right direction lines, 4 independent speed lines) to fit available GPIO while preserving per-wheel encoder telemetry.

## Known Issues

- **[N] of 4 drive motors are not reliably receiving power as of submission.** Root cause narrowed via isolated diagnostic testing to a likely power-brownout-under-load condition (motor and logic power sharing a supply), with a possible additional wiring fault isolated to specific motor channels. A fix (separating ESP32 logic power from motor power) was identified but not fully re-verified before the submission deadline.
- **Full closed-loop PID speed control was intentionally deferred**, given time constraints — a simpler proportional gyro-based heading correction and raw encoder-based distance tracking were implemented instead.
- **Speech recognition was scoped but intentionally dropped** to prioritize a fully working core pipeline within the available build time.

## Demo Video

(https://drive.google.com/file/d/1g66gReOm86e0tmXK2YHVcHV1n0MQbO11/view?usp=drivesdk)

## Repository Structure

```
/app          — Arduino App Lab project (Python + STM32 sketch)
/esp32        — ESP32 motor-controller sketch
/esp32-cam    — ESP32-CAM streaming sketch
/docs         — system diagram, BOM, additional documentation
```

## License

[MIT]

## Author(s)

Abhinav Dubey
