#include <Arduino_RouterBridge.h>
#include <Wire.h>

// ==== Ultrasonic ====
#define TRIG_WEST 2
#define ECHO_WEST 3
#define TRIG_NORTH 4
#define ECHO_NORTH 5
#define TRIG_EAST 6
#define ECHO_EAST 7
#define BLOCKED_THRESHOLD_CM 15.0
#define DANCE_CLEARANCE_CM 25.0
#define ECHO_TIMEOUT_US 15000UL
#define SENSOR_SETTLE_MS 20
const unsigned long TX_INTERVAL_MS = 150;
unsigned long lastTxTime = 0;
unsigned long lastAckTime = 0;
#define ESP32_ACK_TIMEOUT_MS 600  // generous margin over the 150ms nav cycle
float distWest = 999, distNorth = 999, distEast = 999;

// ==== MPU9250 — tip-over/crash detection only ====
const int MPU9250_ADDRESS = 0x68;
// PLACEHOLDER thresholds — untuned, adjust after real chassis test
#define TILT_ACCEL_THRESHOLD_G 0.5   // accelZ below this while others high = likely tipped`
#define JERK_THRESHOLD_G 2.0         // sudden spike on X/Y = likely impact
float lastAccelX = 0, lastAccelY = 0, lastAccelZ = 1.0;
bool emergencyStop = false;

// ==== LEDs (D8=White, D9=Blue, D10=Green, D11=Red) ====
#define LED_WHITE 8
#define LED_BLUE 9
#define LED_GREEN 10
#define LED_RED 11

// ==== Button (D12, pull-down = active-HIGH) + Buzzer (D13) ====
#define BUTTON_PIN 12
#define BUZZER_PIN 13
bool cameraModeActive = false;
bool lastButtonReading = LOW;
unsigned long lastDebounceTime = 0;
#define DEBOUNCE_MS 50

// ==== Gesture action state ====
bool actionInProgress = false;
bool actionComplete = true; // idle default — see design note in chat
String currentAction = "";
int blockedStreak = 0;
#define STUCK_STREAK_THRESHOLD 10 // ~1.5s of continuous "blocked" at 150ms cycle

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);

  pinMode(TRIG_WEST, OUTPUT);  pinMode(ECHO_WEST, INPUT);
  pinMode(TRIG_NORTH, OUTPUT); pinMode(ECHO_NORTH, INPUT);
  pinMode(TRIG_EAST, OUTPUT);  pinMode(ECHO_EAST, INPUT);
  digitalWrite(TRIG_WEST, LOW); digitalWrite(TRIG_NORTH, LOW); digitalWrite(TRIG_EAST, LOW);

  pinMode(LED_WHITE, OUTPUT); pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT); pinMode(LED_RED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT); // external pull-down, per your wiring
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin();
  setupMPU9250();

  Bridge.begin();
  Bridge.provide("receive_gesture", onGestureReceived);
  Bridge.provide("is_camera_mode_active", isCameraModeActive);
  Bridge.provide("is_action_complete", isActionComplete);

  Serial.println("Anvesha_UnoQ — full system online.");
}

void loop() {
  handleButton();
  checkTipOver();

  if (millis() - lastTxTime >= TX_INTERVAL_MS) {
    lastTxTime = millis();
    distWest  = readDistanceCM(TRIG_WEST, ECHO_WEST);
    delay(SENSOR_SETTLE_MS);
    distNorth = readDistanceCM(TRIG_NORTH, ECHO_NORTH);
    delay(SENSOR_SETTLE_MS);
    distEast  = readDistanceCM(TRIG_EAST, ECHO_EAST);

    const char* navCommand = evaluateNavigation();
    updateStuckTracking(navCommand);
    updateLEDs(navCommand);

    if (!actionInProgress) {
      char payload[40];
      snprintf(payload, sizeof(payload), "<NAV:%s>", navCommand);
      Serial1.println(payload);
    }

    Serial.print("W:"); Serial.print(distWest, 1);
    Serial.print(" N:"); Serial.print(distNorth, 1);
    Serial.print(" E:"); Serial.print(distEast, 1);
    Serial.print(" nav:"); Serial.print(navCommand);
    Serial.print(" cameraMode:"); Serial.print(cameraModeActive);
    Serial.print(" ESP32:"); Serial.println((millis() - lastAckTime < ESP32_ACK_TIMEOUT_MS) ? "OK" : "NOT RESPONDING");
  }

  // Listen for ESP32 completion signal
  if (Serial1.available() > 0) {
  String msg = Serial1.readStringUntil('\n');
  msg.trim();
  if (msg == "<ACK>") {
    lastAckTime = millis();
  } else if (msg == "<DONE>" && actionInProgress) {
    actionInProgress = false;
    actionComplete = true;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Action complete — resuming nav.");
  }
}
}

// ==== Bridge-exposed functions ====

void onGestureReceived(String label) {
  label.trim();
  if (!cameraModeActive || actionInProgress || emergencyStop) return;

  if (label == "THUMPS_UP") {
    if (distWest < DANCE_CLEARANCE_CM || distNorth < DANCE_CLEARANCE_CM || distEast < DANCE_CLEARANCE_CM) {
      Serial.println("Dance blocked — insufficient clearance.");
      return; // stays idle, Python's poll sees actionComplete=true immediately, no hang
    }
  }

  currentAction = label;
  actionInProgress = true;
  actionComplete = false;
  digitalWrite(BUZZER_PIN, HIGH); // beep on accepting an action

  char payload[40];
  snprintf(payload, sizeof(payload), "<GESTURE:%s>", label.c_str());
  Serial1.println(payload);
  Serial.print("Gesture action started -> "); Serial.println(payload);
}

bool isCameraModeActive() { return cameraModeActive; }
bool isActionComplete() { return actionComplete; }

// ==== Button ====
void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    static bool stableState = LOW;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == HIGH) { // press detected, active-HIGH
        cameraModeActive = !cameraModeActive;
        Serial.print("Camera mode toggled -> "); Serial.println(cameraModeActive);
      }
    }
  }
  lastButtonReading = reading;
}

// ==== Navigation ====
const char* evaluateNavigation() {
  bool frontBlocked = (distNorth <= BLOCKED_THRESHOLD_CM);
  bool leftBlocked  = (distWest  <= BLOCKED_THRESHOLD_CM);
  bool rightBlocked = (distEast  <= BLOCKED_THRESHOLD_CM);
  if (!frontBlocked) return "going forward";
  if (!leftBlocked && !rightBlocked) return (distWest >= distEast) ? "move left" : "move right";
  if (!leftBlocked) return "move left";
  if (!rightBlocked) return "move right";
  return "blocked";
}

void updateStuckTracking(const char* navCommand) {
  if (strcmp(navCommand, "blocked") == 0) blockedStreak++;
  else blockedStreak = 0;
  if (blockedStreak == STUCK_STREAK_THRESHOLD) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void updateLEDs(const char* navCommand) {
  digitalWrite(LED_WHITE, (actionInProgress && currentAction == "FULL_HAND"));
  digitalWrite(LED_BLUE,  (actionInProgress && currentAction == "THUMPS_UP"));
  digitalWrite(LED_RED,   (!actionInProgress && strcmp(navCommand, "blocked") == 0));
  digitalWrite(LED_GREEN, (!actionInProgress && strcmp(navCommand, "blocked") != 0));
}

float readDistanceCM(int trigPin, int echoPin) {
  if (digitalRead(echoPin) == HIGH) return 999.0;
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  unsigned long duration = pulseIn(echoPin, HIGH, ECHO_TIMEOUT_US);
  return (duration == 0) ? 999.0 : duration / 58.0;
}

// ==== MPU9250 ====
void setupMPU9250() {
  writeMPU9250(MPU9250_ADDRESS, 0x6B, 0x00);
  writeMPU9250(MPU9250_ADDRESS, 0x1B, 0x00);
  writeMPU9250(MPU9250_ADDRESS, 0x1C, 0x00);
}
void writeMPU9250(byte address, byte reg, byte data) {
  Wire.beginTransmission(address); Wire.write(reg); Wire.write(data); Wire.endTransmission();
}
void checkTipOver() {
  Wire.beginTransmission(MPU9250_ADDRESS);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU9250_ADDRESS, 6, true);
  if (Wire.available() < 6) return;
  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();
  float ax = rawX / 16384.0, ay = rawY / 16384.0, az = rawZ / 16384.0;

  bool tilted = (abs(az) < TILT_ACCEL_THRESHOLD_G);
  bool jerked = (abs(ax - lastAccelX) > JERK_THRESHOLD_G) || (abs(ay - lastAccelY) > JERK_THRESHOLD_G);
  emergencyStop = tilted || jerked;
  if (emergencyStop) {
    Serial1.println("<EMERGENCY:stop>");
    digitalWrite(LED_RED, HIGH);
  }
  lastAccelX = ax; lastAccelY = ay; lastAccelZ = az;
}
