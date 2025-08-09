// Clean, self-contained ESP32 sketch:
// - Offline software clock (millis())
// - D14 increments alarm hour, D27 increments alarm minute (internal pull-ups)
// - Guaranteed single "ready" event scheduled between now and alarm
// - On ready: prints "ready", "Alarm in X hour(s), Y minute(s)", "Random value: N"
// - On alarm second: prints "ALARM", then prints "read" once, and shows "Wake UP!" on OLED

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pins
const int BTN_HOUR = 14; // increment alarm hour (pin -> button -> GND)
const int BTN_MIN  = 27; // increment alarm minute (pin -> button -> GND)

// Debounce & timing
const unsigned long DEBOUNCE_MS = 50UL;
const unsigned long TICK_MS = 1000UL;

// Random range (change here)
int minRandomVal = 0;
int maxRandomVal = 10;

// Scheduling constraints (seconds)
const int MIN_OFFSET_SEC = 2;         // at least X seconds after now
const int MIN_BEFORE_ALARM_SEC = 2;   // at least X seconds before alarm

// Clock state (set initial/manual time here)
int currentHour = 12;
int currentMinute = 0;
int currentSecond = 0;

// Alarm state (initial)
int alarmHour = 12;
int alarmMinute = 1;

// Flags
bool alarmTriggered = false;
bool readyScheduled = false;
bool readyTriggered = false;
unsigned long readyEventAtMs = 0; // millis timestamp when "ready" will fire; 0 = none

// Timing anchor
unsigned long lastTickMs = 0;

// Debounce state
int lastReadingHour = HIGH, stableHour = HIGH;
unsigned long lastChangeHourMs = 0;
int lastReadingMin = HIGH, stableMin = HIGH;
unsigned long lastChangeMinMs = 0;

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(10);

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed");
    while (true) delay(10);
  }
  display.clearDisplay();
  display.display();

  pinMode(BTN_HOUR, INPUT_PULLUP);
  pinMode(BTN_MIN, INPUT_PULLUP);

  randomSeed((unsigned long)analogRead(0));
  lastTickMs = millis();

  // schedule initial ready event if possible
  scheduleReadyEvent();

  Serial.println("Started. Open Serial Monitor at 115200.");
  printStatus();
}

// ---------- Loop ----------
void loop() {
  unsigned long now = millis();

  // stable 1-second ticks (catch-up safe)
  while (now - lastTickMs >= TICK_MS) {
    lastTickMs += TICK_MS;
    tickSecond();

    // if we moved away from exact alarm moment, allow alarm to trigger next day
    if (!(currentHour == alarmHour && currentMinute == alarmMinute && currentSecond == 0)) {
      alarmTriggered = false;
    }
  }

  // handle buttons (debounced) - register on press only
  handleButton(BTN_HOUR, lastReadingHour, lastChangeHourMs, stableHour, onHourPressed);
  handleButton(BTN_MIN,  lastReadingMin,  lastChangeMinMs,  stableMin,  onMinPressed);

  // fire ready event if scheduled
  if (readyScheduled && !readyTriggered && readyEventAtMs != 0 && now >= readyEventAtMs) {
    triggerReadyEvent();
  }

  // check alarm: exact second match
  if (!alarmTriggered && currentHour == alarmHour && currentMinute == alarmMinute && currentSecond == 0) {
    alarmTriggered = true;
    readyScheduled = false;
    readyTriggered = false;
    readyEventAtMs = 0;
    Serial.println("ALARM");
    Serial.println("read"); // <-- PRINT "read" ONCE WHEN ALARM TRIGGERS
  }

  // update OLED frequently
  updateOLED();

  delay(5); // short yield
}

// ---------- Helpers ----------

void tickSecond() {
  currentSecond++;
  if (currentSecond >= 60) {
    currentSecond = 0;
    currentMinute++;
    if (currentMinute >= 60) {
      currentMinute = 0;
      currentHour = (currentHour + 1) % 24;
    }
  }
}

long secondsUntilAlarm() {
  long cur = (long)currentHour * 3600L + (long)currentMinute * 60L + (long)currentSecond;
  long alm = (long)alarmHour * 3600L + (long)alarmMinute * 60L; // alarm at 0 sec
  long diff = alm - cur;
  if (diff <= 0) diff += 24L * 3600L;
  return diff;
}

void scheduleReadyEvent() {
  long secsLeft = secondsUntilAlarm();

  // too little time => don't schedule
  if (secsLeft <= (MIN_OFFSET_SEC + MIN_BEFORE_ALARM_SEC)) {
    readyScheduled = false;
    readyTriggered = false;
    readyEventAtMs = 0;
    Serial.println("Not enough time to schedule 'ready' event.");
    return;
  }

  int minOffset = MIN_OFFSET_SEC;
  int maxOffset = (int)(secsLeft - MIN_BEFORE_ALARM_SEC);
  int chosenOffset;
  if (maxOffset <= minOffset) {
    chosenOffset = (int)(secsLeft / 2);
  } else {
    chosenOffset = random(minOffset, maxOffset + 1);
  }
  readyEventAtMs = millis() + (unsigned long)chosenOffset * 1000UL;
  readyScheduled = true;
  readyTriggered = false;

  char buf[80];
  snprintf(buf, sizeof(buf), "Ready event scheduled in %d s (secsLeft=%ld)", chosenOffset, secsLeft);
  Serial.println(buf);
}

void triggerReadyEvent() {
  long secsLeft = secondsUntilAlarm();
  if (secsLeft <= 0) {
    Serial.println("ready (skipped: no time left)");
    readyTriggered = true;
    readyScheduled = false;
    readyEventAtMs = 0;
    return;
  }
  int hLeft = (int)(secsLeft / 3600);
  int mLeft = (int)((secsLeft % 3600) / 60);

  Serial.println("ready");
  {
    char buf[64];
    snprintf(buf, sizeof(buf), "Alarm in %d hour(s), %d minute(s)", hLeft, mLeft);
    Serial.println(buf);
  }
  int val = random(minRandomVal, maxRandomVal + 1);
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "Random value: %d", val);
    Serial.println(buf);
  }

  readyTriggered = true;
  readyScheduled = false;
  readyEventAtMs = 0;
}

void updateOLED() {
  char buf[40];
  display.clearDisplay();

  // current time big
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", currentHour, currentMinute, currentSecond);
  display.print(buf);

  // alarm small
  display.setTextSize(1);
  display.setCursor(0, 44);
  snprintf(buf, sizeof(buf), "Alarm %02d:%02d", alarmHour, alarmMinute);
  display.print(buf);

  // overlay Wake UP! if alarm triggered
  if (alarmTriggered) {
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print("Wake UP!");
    // removed Serial.println("read"); from here so "read" only prints once in loop()
  }

  display.display();
}

// generic debounced button handler (calls onPress once when stable LOW is detected)
void handleButton(int pin, int &lastReading, unsigned long &lastChangeMs, int &stableState, void (*onPress)()) {
  int reading = digitalRead(pin);
  unsigned long now = millis();

  if (reading != lastReading) {
    lastChangeMs = now;
  }

  if ((now - lastChangeMs) > DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        onPress();
      }
    }
  }
  lastReading = reading;
}

// callbacks for button presses
void onHourPressed() {
  alarmHour = (alarmHour + 1) % 24;
  char buf[40];
  snprintf(buf, sizeof(buf), "Alarm hour -> %02d", alarmHour);
  Serial.println(buf);
  scheduleReadyEvent();
}

void onMinPressed() {
  alarmMinute = (alarmMinute + 1) % 60;
  char buf[40];
  snprintf(buf, sizeof(buf), "Alarm minute -> %02d", alarmMinute);
  Serial.println(buf);
  scheduleReadyEvent();
}

void printStatus() {
  char buf[80];
  snprintf(buf, sizeof(buf), "Time %02d:%02d:%02d | Alarm %02d:%02d | Random range %d..%d",
           currentHour, currentMinute, currentSecond, alarmHour, alarmMinute, minRandomVal, maxRandomVal);
  Serial.println(buf);
}
