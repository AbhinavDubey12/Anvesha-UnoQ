#include <Arduino.h>

#define RXD2 25
#define TXD2 26

#define LEFT_IN1 27
#define LEFT_IN2 14
#define RIGHT_IN1 13
#define RIGHT_IN2 15
#define FL_EN 32
#define RL_EN 33
#define FR_EN 4
#define RR_EN 23

#define FL_ENCA 34
#define FL_ENCB 35
#define RL_ENCA 19
#define RL_ENCB 21
#define FR_ENCA 2
#define FR_ENCB 22
#define RR_ENCA 5
#define RR_ENCB 18

volatile long posFL = 0, posRL = 0, posFR = 0, posRR = 0;
const int DRIVE_PWM = 110;

// PPR=117, wheel circumference 20.4cm -> ticks for 20cm = (20/20.4)*117 ≈ 115
#define REVERSE_TARGET_TICKS 115
#define DANCE_BEAT_MS 1000
#define DANCE_GAP_MS 100

unsigned long lastPacketTime = 0;
const unsigned long TIMEOUT_LIMIT_MS = 1000;
bool busy = false;

void IRAM_ATTR readEncoderFL();
void IRAM_ATTR readEncoderRL();
void IRAM_ATTR readEncoderFR();
void IRAM_ATTR readEncoderRR();

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(LEFT_IN1, OUTPUT); pinMode(LEFT_IN2, OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT); pinMode(RIGHT_IN2, OUTPUT);
  pinMode(FL_EN, OUTPUT); pinMode(RL_EN, OUTPUT);
  pinMode(FR_EN, OUTPUT); pinMode(RR_EN, OUTPUT);

  pinMode(FL_ENCA, INPUT); pinMode(FL_ENCB, INPUT);
  pinMode(FR_ENCA, INPUT); pinMode(FR_ENCB, INPUT);
  pinMode(RL_ENCA, INPUT_PULLUP); pinMode(RL_ENCB, INPUT_PULLUP);
  pinMode(RR_ENCA, INPUT_PULLUP); pinMode(RR_ENCB, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(FL_ENCA), readEncoderFL, RISING);
  attachInterrupt(digitalPinToInterrupt(RL_ENCA), readEncoderRL, RISING);
  attachInterrupt(digitalPinToInterrupt(FR_ENCA), readEncoderFR, RISING);
  attachInterrupt(digitalPinToInterrupt(RR_ENCA), readEncoderRR, RISING);

  stopAll();
  lastPacketTime = millis();
  Serial.println("ESP32 Motor Node — full system ready.");
}

void loop() {
  readUARTStream();
  if (!busy && (millis() - lastPacketTime > TIMEOUT_LIMIT_MS)) {
    stopAll();
  }
}

void readUARTStream() {
  static boolean recvInProgress = false;
  static char rxBuffer[40];
  static byte idx = 0;

  while (Serial2.available() > 0) {
    char rc = Serial2.read();
    if (recvInProgress) {
      if (rc != '>') {
        if (idx < sizeof(rxBuffer) - 1) rxBuffer[idx++] = rc;
      } else {
        rxBuffer[idx] = '\0';
        recvInProgress = false;
        idx = 0;
        lastPacketTime = millis();
        routePacket(rxBuffer);
      }
    } else if (rc == '<') {
      recvInProgress = true;
      idx = 0;
    }
  }
}

void routePacket(char* packet) {
  if (strncmp(packet, "EMERGENCY:", 10) == 0) {
    stopAll();
    busy = false;
    Serial.println("EMERGENCY STOP received.");
    return;
  }
  if (strncmp(packet, "NAV:", 4) == 0) {
    if (busy) return; // ignore nav while executing a gesture action
    executeNav(packet + 4);
    return;
  }
  if (strncmp(packet, "GESTURE:", 8) == 0) {
    if (busy) return;
    executeGesture(packet + 8);
    return;
  }
}

void executeNav(char* cmd) {
  if (strcmp(cmd, "going forward") == 0) driveMotors(1, 1, DRIVE_PWM);
  else if (strcmp(cmd, "move left") == 0) driveMotors(-1, 1, DRIVE_PWM);
  else if (strcmp(cmd, "move right") == 0) driveMotors(1, -1, DRIVE_PWM);
  else if (strcmp(cmd, "blocked") == 0) stopAll();
}

void executeGesture(char* label) {
  busy = true;
  if (strcmp(label, "FULL_HAND") == 0) {
    doReverse20cm();
  } else if (strcmp(label, "THUMPS_UP") == 0) {
    doDance();
  }
  stopAll();
  busy = false;
  Serial2.println("<DONE>");
}

void doReverse20cm() {
  posFL = posRL = posFR = posRR = 0;
  driveMotors(-1, -1, DRIVE_PWM);
  while (true) {
    long avgTicks = (abs(posFL) + abs(posRL) + abs(posFR) + abs(posRR)) / 4;
    if (avgTicks >= REVERSE_TARGET_TICKS) break;
    lastPacketTime = millis(); // feed watchdog while legitimately busy
    delay(10);
  }
}

void doDance() {
  // F-R-L-B-F-R-L, 1s per beat, 100ms gap
  int leftDirs[]  = { 1, -1,  1, -1,  1, -1,  1};
  int rightDirs[] = { 1,  1, -1, -1,  1,  1, -1};
  for (int i = 0; i < 7; i++) {
    driveMotors(leftDirs[i], rightDirs[i], DRIVE_PWM);
    unsigned long start = millis();
    while (millis() - start < DANCE_BEAT_MS) {
      lastPacketTime = millis();
      delay(10);
    }
    stopAll();
    delay(DANCE_GAP_MS);
  }
}

void driveMotors(int leftDir, int rightDir, int pwmSpeed) {
  setDirection(LEFT_IN1, LEFT_IN2, leftDir);
  setDirection(RIGHT_IN1, RIGHT_IN2, rightDir);
  analogWrite(FL_EN, pwmSpeed); analogWrite(RL_EN, pwmSpeed);
  analogWrite(FR_EN, pwmSpeed); analogWrite(RR_EN, pwmSpeed);
}

void stopAll() {
  setDirection(LEFT_IN1, LEFT_IN2, 0);
  setDirection(RIGHT_IN1, RIGHT_IN2, 0);
  analogWrite(FL_EN, 0); analogWrite(RL_EN, 0);
  analogWrite(FR_EN, 0); analogWrite(RR_EN, 0);
}

void setDirection(int in1, int in2, int dir) {
  if (dir == 1) { digitalWrite(in1, HIGH); digitalWrite(in2, LOW); }
  else if (dir == -1) { digitalWrite(in1, LOW); digitalWrite(in2, HIGH); }
  else { digitalWrite(in1, LOW); digitalWrite(in2, LOW); }
}

void IRAM_ATTR readEncoderFL() { posFL += (digitalRead(FL_ENCB) == HIGH) ? 1 : -1; }
void IRAM_ATTR readEncoderRL() { posRL += (digitalRead(RL_ENCB) == HIGH) ? 1 : -1; }
void IRAM_ATTR readEncoderFR() { posFR += (digitalRead(FR_ENCB) == HIGH) ? 1 : -1; }
void IRAM_ATTR readEncoderRR() { posRR += (digitalRead(RR_ENCB) == HIGH) ? 1 : -1; }
