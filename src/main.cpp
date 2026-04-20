/*
 * Automatic Defogging Mirror - Temperature-Hold Control
 * Arduino Uno R4 WiFi with Modulino Thermo
 *
 * SENSOR PLACEMENT:
 * - Modulino Thermo: Mount at top/bottom of mirror with air hole
 *   Measures bathroom humidity AND mirror temperature
 * - Heating pad: Attached to back of mirror, controlled by relay
 *
 * OPERATION:
 * IDLE LOOP:
 *   - Check humidity periodically
 *   - If humidity is high enough, start a heating cycle
 *
 * ACTIVE LOOP:
 *   - HEATING: pads ON for HEAT_ON_TIME (4 minutes)
 *   - COOLDOWN: pads OFF, recheck once per minute
 *   - If temp is still >= TARGET_TEMP, stay OFF and keep checking
 *   - If temp drops below TARGET_TEMP, run another heat cycle
 *   - If humidity drops below HUMIDITY_EXIT, return to IDLE
 */

#include <Modulino.h>
#include <Adafruit_NeoPixel.h>
#include <DHT.h>

ModulinoThermo thermo;

// Pin Definitions
#define RELAY_PIN A5         // Relay module control pin for heating pad
#define LED_PIN A1            // WS2812B data input pin
#define BUTTON_PIN 4         // Button pin (wired to GND, uses INPUT_PULLUP)
#define DHT_PIN 2            // Placeholder pin for DHT11 data line
#define DHT_TYPE DHT11

// ============ ADJUST THESE VALUES ============
// Humidity Thresholds
#define HUMIDITY_THRESHOLD 70.0  // Start heating when humidity exceeds this (%)
#define HUMIDITY_EXIT 60.0       // Exit heating mode when humidity drops below this (%)

// Temperature Limits
#define TARGET_TEMP 40.0         // Target mirror temperature in Celsius (104°F)
#define MAX_SAFE_TEMP 50.0       // Safety cutoff - stop if exceeded (113°F)
#define TEMP_RESET_TEMP 39.0     // Re-enable heating after safety cutoff once cooled to this temp
#define MIN_TEMP 10.0            // Don't heat if room is too cold (safety)

// Heating/Cooldown Timing (all in milliseconds)
#define HEAT_ON_TIME 270000        // Heating pads ON for 4 minutes
#define HEAT_OFF_TIME 60000        // Heater OFF, recheck every 1 minute

// Sensor Check Intervals
#define IDLE_CHECK_INTERVAL 60000   // Check sensors every 1 minute when IDLE
#define ACTIVE_CHECK_INTERVAL 10000 // Check sensors every 10 seconds when active

// LED Settings
#define LED_COUNT 30                // Number of LEDs in the WS2812B strip
#define LED_BRIGHTNESS 80           // Global strip brightness (0-255)
#define TRANSITION_FADE_TIME 8000   // Red->Blue fade time when heating stops (ms)
#define RED_STROBE_PERIOD 2000      // Slow red strobe period while heating (ms)
#define RED_STROBE_MIN 40           // Minimum red intensity during strobe (0-255)
#define BUTTON_DEBOUNCE_TIME 50     // Button debounce time (ms)
// =============================================

// System State
enum SystemMode {
  IDLE,           // Waiting, checking humidity every 30 seconds
  HEATING,        // Heating pads ON
  COOLDOWN        // Heating pads OFF, waiting before next cycle
};

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
DHT dht(DHT_PIN, DHT_TYPE);

SystemMode currentMode = IDLE;
bool heatingActive = false;
bool temperatureLockout = false;
bool whiteLightOverride = false;
bool cooldownFadeActive = false;
unsigned long lastSensorRead = 0;
unsigned long lastModeChange = 0;
unsigned long cooldownFadeStart = 0;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;

uint8_t lastLedR = 255;
uint8_t lastLedG = 255;
uint8_t lastLedB = 255;
bool ledStateInitialized = false;

// Function declarations
void checkAndEnterHeatingMode(float humidity, float temp);
void runHeatingCycle(float humidity, float temp);
void updateButtonState(unsigned long currentTime);
void updateStripColor(unsigned long currentTime);
void setStripColorIfChanged(uint8_t red, uint8_t green, uint8_t blue);
void turnOnHeating();
void turnOffHeating();
float celsiusToFahrenheit(float celsius);

void setup() {
  Serial.begin(9600);
  
  // Wait up to 3 seconds for serial connection (prevents hanging)
  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 3000)) { 
    delay(10); 
  }

  Serial.println();
  Serial.println("=== Automatic Defogging Mirror ===");
  Serial.println("Hardware: Arduino Uno R4 WiFi + Modulino Thermo");
  Serial.println("Control: 4 min heat + temp recheck hold");
  Serial.println();

  // Initialize pins
  Serial.println("Initializing pins...");
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relay OFF initially
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Pins initialized OK");

  Serial.println("Initializing WS2812B strip...");
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();
  Serial.println("WS2812B initialized OK");

  // Initialize Modulino system
  Serial.println("Initializing Modulino system...");
  Modulino.begin();
  Serial.println("Modulino.begin() OK");
  
  Serial.println("Initializing Thermo sensor...");
  thermo.begin();
  Serial.println("Thermo sensor initialized OK");

  Serial.println("Initializing DHT11 temperature sensor...");
  dht.begin();
  Serial.println("DHT11 initialized OK");

  Serial.println();
  Serial.println("** Mount sensor at top/bottom of mirror with air hole **");
  Serial.println("System ready. Starting in IDLE mode...");
  Serial.print("Checking humidity every "); 
  Serial.print(IDLE_CHECK_INTERVAL / 1000); 
  Serial.println(" seconds");
  Serial.println();
  delay(2000); // Let sensors stabilize
}

void loop() {
  unsigned long currentTime = millis();
  updateButtonState(currentTime);
  updateStripColor(currentTime);

  // Determine check interval based on mode
  unsigned long checkInterval = (currentMode == IDLE) ? IDLE_CHECK_INTERVAL : ACTIVE_CHECK_INTERVAL;

  // Read sensors at appropriate intervals
  if (currentTime - lastSensorRead >= checkInterval) {
    lastSensorRead = currentTime;

    // Read humidity from Modulino Thermo and temperature from DHT11
    float humidity = thermo.getHumidity();
    float mirrorTemp = dht.readTemperature();

    // Check for sensor errors
    if (isnan(humidity) || isnan(mirrorTemp)) {
      Serial.println("ERROR: Cannot read sensor!");
      if (heatingActive) {
        turnOffHeating();
        currentMode = IDLE;
      }
      return;
    }

    // Display current readings
    Serial.println("--- Sensor Readings ---");
    Serial.print("Mode: ");
    switch(currentMode) {
      case IDLE: Serial.println("IDLE (monitoring every 30s)"); break;
      case HEATING: Serial.println("HEATING"); break;
      case COOLDOWN: Serial.println("COOLDOWN"); break;
    }
    Serial.print("Humidity: "); Serial.print(humidity, 1); Serial.println("%");
    Serial.print("Mirror Temp: "); Serial.print(mirrorTemp, 1); 
    Serial.print("°C ("); Serial.print(celsiusToFahrenheit(mirrorTemp), 1); Serial.println("°F)");
    Serial.print("Heating Pads: "); Serial.println(heatingActive ? "ON" : "OFF");
    Serial.print("Temp Lockout: "); Serial.println(temperatureLockout ? "ACTIVE" : "INACTIVE");
    Serial.println();

    // Safety lockout gate: after high-temp cutoff, do not allow reheating
    // until mirror temperature cools to TEMP_RESET_TEMP.
    if (temperatureLockout) {
      if (mirrorTemp <= TEMP_RESET_TEMP) {
        temperatureLockout = false;
        Serial.print("Temp lockout cleared at ");
        Serial.print(mirrorTemp, 1);
        Serial.println("°C - heating allowed again");
      } else {
        if (heatingActive) {
          turnOffHeating();
        }
        currentMode = IDLE;
        if (!cooldownFadeActive) {
          cooldownFadeActive = true;
          cooldownFadeStart = currentTime;
        }
        Serial.print("Temp lockout active: waiting to cool to ");
        Serial.print(TEMP_RESET_TEMP, 1);
        Serial.println("°C before reheating");
        return;
      }
    }

    // State machine logic
    switch(currentMode) {
      case IDLE:
        checkAndEnterHeatingMode(humidity, mirrorTemp);
        break;
      
      case HEATING:
      case COOLDOWN:
        runHeatingCycle(humidity, mirrorTemp);
        break;
    }
  }
}

// IDLE Mode: Check humidity periodically
void checkAndEnterHeatingMode(float humidity, float temp) {
  unsigned long currentTime = millis();
  
  Serial.print("IDLE CHECK: Humidity = "); Serial.print(humidity, 1); Serial.println("%");
  
  // Safety check
  if (temp < MIN_TEMP) {
    Serial.println("Room too cold - staying in IDLE");
    return;
  }
  
  // Check if humidity exceeds threshold
  if (humidity >= HUMIDITY_THRESHOLD) {
    Serial.println(">>> ENTERING HEATING MODE <<<");
    Serial.print("Humidity ("); Serial.print(humidity, 1);
    Serial.println("%) exceeds threshold");
    currentMode = HEATING;
    lastModeChange = currentTime;
    cooldownFadeActive = false;
    turnOnHeating();
  }
}

// HEATING/COOLDOWN Mode: Run duty cycle
void runHeatingCycle(float humidity, float temp) {
  unsigned long currentTime = millis();
  unsigned long timeInMode = currentTime - lastModeChange;
  
  // SAFETY: Check if temperature is too high
  if (temp >= MAX_SAFE_TEMP) {
    Serial.print("SAFETY CUTOFF: Temp too high (");
    Serial.print(temp, 1); Serial.println("°C)");
    turnOffHeating();
    temperatureLockout = true;
    currentMode = IDLE;
    cooldownFadeActive = true;
    cooldownFadeStart = currentTime;
    return;
  }
  
  // Check if humidity has dropped - exit heating mode
  if (humidity < HUMIDITY_EXIT) {
    Serial.print("Humidity dropped to "); Serial.print(humidity, 1);
    Serial.println("% - EXITING to IDLE mode");
    turnOffHeating();
    currentMode = IDLE;
    cooldownFadeActive = true;
    cooldownFadeStart = currentTime;
    return;
  }
  
  // Duty cycle state machine
  if (currentMode == HEATING) {
    // Currently heating - check if ON time expired
    if (timeInMode >= HEAT_ON_TIME) {
      Serial.println("Heat ON time complete - entering COOLDOWN");
      Serial.print("Mirror reached: "); Serial.print(temp, 1); Serial.println("°C");
      turnOffHeating();
      currentMode = COOLDOWN;
      lastModeChange = currentTime;
      cooldownFadeActive = true;
      cooldownFadeStart = currentTime;
    } else {
      // Still heating - show progress
      unsigned long remaining = HEAT_ON_TIME - timeInMode;
      Serial.print("Heating: "); Serial.print(remaining / 1000);
      Serial.println(" seconds remaining");
    }
  }
  else if (currentMode == COOLDOWN) {
    // Heater OFF hold: evaluate once per minute.
    if (timeInMode >= HEAT_OFF_TIME) {
      Serial.println("Cooldown recheck");
      Serial.print("Current temp: "); Serial.print(temp, 1); Serial.println("°C");
      
      // Continue active control while humidity remains elevated.
      if (humidity >= HUMIDITY_EXIT && temp < TARGET_TEMP) {
        Serial.println("Temp below target - restarting HEATING");
        currentMode = HEATING;
        lastModeChange = currentTime;
        cooldownFadeActive = false;
        turnOnHeating();
      } else if (humidity < HUMIDITY_EXIT) {
        Serial.println("Humidity low - EXITING to IDLE");
        currentMode = IDLE;
        cooldownFadeActive = false;
      } else {
        Serial.println("Temp still above target - staying OFF");
        lastModeChange = currentTime;
      }
    } else {
      // Still cooling - show progress
      unsigned long remaining = HEAT_OFF_TIME - timeInMode;
      Serial.print("Cooldown: "); Serial.print(remaining / 1000);
      Serial.println(" seconds remaining");
    }
  }
}

void updateButtonState(unsigned long currentTime) {
  bool buttonReading = digitalRead(BUTTON_PIN);

  if (buttonReading != lastButtonReading) {
    lastDebounceTime = currentTime;
    lastButtonReading = buttonReading;
  }

  if ((currentTime - lastDebounceTime) >= BUTTON_DEBOUNCE_TIME && buttonReading != stableButtonState) {
    stableButtonState = buttonReading;
    if (stableButtonState == LOW) {
      whiteLightOverride = !whiteLightOverride;
      Serial.print("White light override: ");
      Serial.println(whiteLightOverride ? "ON" : "OFF");
    }
  }
}

void updateStripColor(unsigned long currentTime) {
  // Button override: force soft white regardless of heating state.
  if (whiteLightOverride) {
    setStripColorIfChanged(255, 180, 120);
    return;
  }

  if (currentMode == HEATING) {
    unsigned long phase = currentTime % RED_STROBE_PERIOD;
    unsigned long halfPeriod = RED_STROBE_PERIOD / 2;
    float ramp = (phase < halfPeriod)
      ? (float)phase / (float)halfPeriod
      : (float)(RED_STROBE_PERIOD - phase) / (float)halfPeriod;
    uint8_t red = (uint8_t)(RED_STROBE_MIN + (255.0f - RED_STROBE_MIN) * ramp);
    setStripColorIfChanged(red, 0, 0);
    return;
  }

  // Fade from red to blue after heating stops.
  if (cooldownFadeActive) {
    float progress = (float)(currentTime - cooldownFadeStart) / (float)TRANSITION_FADE_TIME;
    if (progress >= 1.0f) {
      cooldownFadeActive = false;
      setStripColorIfChanged(0, 0, 255);
    } else {
      uint8_t red = (uint8_t)(255.0f * (1.0f - progress));
      uint8_t blue = (uint8_t)(255.0f * progress);
      setStripColorIfChanged(red, 0, blue);
    }
    return;
  }

  // Default non-heating color.
  setStripColorIfChanged(0, 0, 255);
}

void setStripColorIfChanged(uint8_t red, uint8_t green, uint8_t blue) {
  if (ledStateInitialized && red == lastLedR && green == lastLedG && blue == lastLedB) {
    return;
  }

  uint32_t color = strip.Color(red, green, blue);
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();

  lastLedR = red;
  lastLedG = green;
  lastLedB = blue;
  ledStateInitialized = true;
}

void turnOnHeating() {
  digitalWrite(RELAY_PIN, HIGH); // Activate relay
  heatingActive = true;
  Serial.println("*** HEATING PADS ON ***");
}

void turnOffHeating() {
  digitalWrite(RELAY_PIN, LOW); // Deactivate relay
  heatingActive = false;
  Serial.println("*** HEATING PADS OFF ***");
}

float celsiusToFahrenheit(float celsius) {
  return (celsius * 9.0 / 5.0) + 32.0;
}
