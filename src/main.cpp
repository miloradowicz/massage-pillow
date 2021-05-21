#include <Arduino.h>
#include <limits.h>
#include <EEPROM.h>
#include <avr/sleep.h>

constexpr int BUTTON = PIN_PA1;
constexpr int LEFTER = PIN_PA6;
constexpr int RIGHTER = PIN_PA2;
constexpr int HEATER = PIN_PA3;

constexpr int DIRECTION = 0x01;
constexpr int HEATING = 0x02;

constexpr int S_ALL_OFF = 0x07;

constexpr unsigned long debounceDelay = 50L;
constexpr unsigned long debounceCycles = F_CPU / 1000L * debounceDelay;
constexpr unsigned long longPressLength = 3000L;
constexpr unsigned long sessionLength = 900000L;

int state;
unsigned long poweredOn;
unsigned int eepromIndex;
unsigned int eepromPriority;

int restoreState() {
  eepromIndex = 0;
  eepromPriority = EEPROM.read(0);

  for (unsigned int t, i = 1; i < EEPROM_SIZE >> 1; i++) {
    t = EEPROM.read(i << 1);
    if (eepromPriority != (EEPROM_SIZE >> 1) - 1 && t != eepromPriority + 1 || eepromPriority == (EEPROM_SIZE >> 1) - 1 && t != 0) {
      break;
    }
    eepromIndex = i;
    eepromPriority = t;
  }

  return 0x03 & EEPROM.read((eepromIndex << 1) + 1);
}

void saveState(int state) {
  if (EEPROM.read((eepromIndex << 1) + 1) != state) {
    eepromIndex = eepromIndex + 1 < EEPROM_SIZE >> 1 ? eepromIndex + 1 : 0;
    eepromPriority = eepromPriority + 1 < (EEPROM_SIZE >> 1) - 1 ? eepromPriority + 1 : 0;
    EEPROM.write(eepromIndex << 1, eepromPriority);
    EEPROM.write((eepromIndex << 1) + 1, 0x03 & state);
  }
}

int readKey() {
  static unsigned long lastDebounceTime = 0;
  static int lastButtonState = digitalRead(BUTTON);
  static int keyState = !lastButtonState ? HIGH : LOW;
  
  int reading;

  reading = digitalRead(BUTTON);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
    lastButtonState = reading;
  }

  if (millis() - lastDebounceTime > debounceDelay) {
    keyState = !reading ? HIGH : LOW;
  }

  return keyState;
}

int readKeyBlocking() {
  unsigned long lastDebounceTime;
  int lastButtonState, reading;

  lastDebounceTime = millis();
  lastButtonState = digitalRead(BUTTON);
  while (millis() - lastDebounceTime < debounceDelay) {
    reading = digitalRead(BUTTON);
    if (reading != lastButtonState) {
      lastDebounceTime = millis();
      lastButtonState = reading;
    }
  }

  return !reading ? HIGH : LOW;
}

void setState(int state) {
  // 07h -- all off
  // 03h -- outwards, heating
  // 02h -- inwards, heating
  // 01h -- outwards, no heating
  // 00h -- inwards, no heating

  if (state == S_ALL_OFF) {
    digitalWrite(HEATER, LOW);
    digitalWrite(LEFTER, LOW);
    digitalWrite(RIGHTER, LOW);
    return;
  }

  if (state & HEATING) {
    digitalWrite(HEATER, HIGH);
  }
  else {
    digitalWrite(HEATER, LOW);
  }

  if (state & DIRECTION) {
    digitalWrite(LEFTER, LOW);
    digitalWrite(RIGHTER, HIGH);
  }
  else {
    digitalWrite(LEFTER, HIGH);
    digitalWrite(RIGHTER, LOW);
  }
}

void wakeUp() {
  detachInterrupt(digitalPinToInterrupt(BUTTON));
  sleep_disable();
}

void goSleep() {
  setState(S_ALL_OFF);
  saveState(state);
  delay(200);
  sleep_enable();
  attachInterrupt(digitalPinToInterrupt(BUTTON), wakeUp, CHANGE);
  sleep_cpu();
  if (readKeyBlocking() != HIGH) {
    sleep_enable();
    attachInterrupt(digitalPinToInterrupt(BUTTON), wakeUp, CHANGE);
    sleep_cpu();
  }
  setState(state);
  poweredOn = millis();
}

void setup() {
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(LEFTER, OUTPUT);
  pinMode(RIGHTER, OUTPUT);
  pinMode(HEATER, OUTPUT);
  state = restoreState();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  goSleep();
}

void loop() {
  static int keyState = readKey();
  static int lastKeyState = keyState;
  static bool unprocessed = false;
  static unsigned long keyDownTime = 0;

  int cmd = -1;

  keyState = readKey();
  if (lastKeyState == LOW && keyState == HIGH) {
    unprocessed = true;
    keyDownTime = millis();
  }

  if (unprocessed && keyState == LOW) {
    unprocessed = false;
    cmd = 1;
  }

  if (unprocessed && millis() - keyDownTime > longPressLength) {
    unprocessed = false;
    cmd = 2;
  }

  if (millis() - poweredOn > sessionLength) {
    unprocessed = false;
    cmd = 2;
  }
  lastKeyState = keyState;

  switch (cmd) {
  case 1:
    ++state %= 4;
    setState(state);
    poweredOn = millis();
    break;

  case 2:
    goSleep();
    break;
  
  default:
    break;
  }
}