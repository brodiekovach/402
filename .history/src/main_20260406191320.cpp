/*
 * Automatic Defogging Mirror - Duty Cycle Control
 * Arduino Uno R4 WiFi with Modulino Thermo
 *
 * SENSOR PLACEMENT:
 * - Modulino Thermo: Mount at top/bottom of mirror with air hole
 *   Measures bathroom humidity AND mirror temperature
 * - Heating pad: Attached to back of mirror, controlled by relay
 *
 * OPERATION:
 * OUTER LOOP (every 1 minute):
 *   - Check bathroom humidity
 *   - If humidity > threshold → Enter heating mode
 * 
 * INNER LOOP (heating mode):
 *   - Turn pads ON for 1 minute
 *   - Turn pads OFF for 2 minutes (cooldown)
 *   - Monitor temperature during cooldown
 *   - Check humidity: still high? → Repeat cycle
 *   - Humidity drops below threshold? → Exit to outer loop
 */

#include <Modulino.h>

ModulinoThermo thermo;

// Pin Definitions
#define RELAY_PIN 7         // Relay module control pin for heating pad

// ============ ADJUST THESE VALUES ============
// Humidity Thresholds
#define HUMIDITY_THRESHOLD 70.0  // Start heating when humidity exceeds this (%)
#define HUMIDITY_EXIT 55.0       // Exit heating mode when humidity drops below this (%)

// Temperature Limits
#define TARGET_TEMP 40.0         // Target mirror temperature in Celsius (104°F)
#define MAX_SAFE_TEMP 45.0       // Safety cutoff - stop if exceeded (113°F)
#define MIN_TEMP 10.0            // Don't heat if room is too cold (safety)

// Duty Cycle Timing (all in milliseconds)
#define HEAT_ON_TIME 60000       // Heating pads ON for 1 minute (60000 ms)
#define HEAT_OFF_TIME 120000     // Heating pads OFF for 2 minutes (120000 ms)

// Sensor Check Intervals
#define IDLE_CHECK_INTERVAL 30000   // Check sensors every 30 seconds when IDLE
#define ACTIVE_CHECK_INTERVAL 10000 // Check sensors every 10 seconds when heating/cooling
// =============================================

// System State
enum SystemMode {
  IDLE,           // Waiting, checking humidity every 30 seconds
  HEATING,        // Heating pads ON
  COOLDOWN        // Heating pads OFF, waiting before next cycle
};

SystemMode currentMode = IDLE;
bool heatingActive = false;
unsigned long lastSensorRead = 0;
unsigned long lastModeChange = 0;

// Function declarations
void checkAndEnterHeatingMode(float humidity, float temp);
void runHeatingCycle(float humidity, float temp);
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
  Serial.println("Control: Duty Cycle (1 min ON / 2 min OFF)");
  Serial.println();

  // Initialize pins
  Serial.println("Initializing pins...");
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relay OFF initially
  Serial.println("Pins initialized OK");

  // Initialize Modulino system
  Serial.println("Initializing Modulino system...");
  Modulino.begin();
  Serial.println("Modulino.begin() OK");
  
  Serial.println("Initializing Thermo sensor...");
  thermo.begin();
  Serial.println("Thermo sensor initialized OK");

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

  // Determine check interval based on mode
  unsigned long checkInterval = (currentMode == IDLE) ? IDLE_CHECK_INTERVAL : ACTIVE_CHECK_INTERVAL;

  // Read sensors at appropriate intervals
  if (currentTime - lastSensorRead >= checkInterval) {
    lastSensorRead = currentTime;

    // Read humidity and temperature from Modulino Thermo
    float humidity = thermo.getHumidity();
    float mirrorTemp = thermo.getTemperature();

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
    Serial.println();

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
    currentMode = IDLE;
    return;
  }
  
  // Check if humidity has dropped - exit heating mode
  if (humidity < HUMIDITY_EXIT) {
    Serial.print("Humidity dropped to "); Serial.print(humidity, 1);
    Serial.println("% - EXITING to IDLE mode");
    turnOffHeating();
    currentMode = IDLE;
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
    } else {
      // Still heating - show progress
      unsigned long remaining = HEAT_ON_TIME - timeInMode;
      Serial.print("Heating: "); Serial.print(remaining / 1000);
      Serial.println(" seconds remaining");
    }
  }
  else if (currentMode == COOLDOWN) {
    // Currently cooling - check if OFF time expired
    if (timeInMode >= HEAT_OFF_TIME) {
      Serial.println("Cooldown complete");
      Serial.print("Current temp: "); Serial.print(temp, 1); Serial.println("°C");
      
      // Check if we should heat again
      if (humidity >= HUMIDITY_THRESHOLD && temp < TARGET_TEMP) {
        Serial.println("Still humid & below target - restarting HEATING");
        currentMode = HEATING;
        lastModeChange = currentTime;
        turnOnHeating();
      } else {
        Serial.println("Conditions met - EXITING to IDLE");
        currentMode = IDLE;
      }
    } else {
      // Still cooling - show progress
      unsigned long remaining = HEAT_OFF_TIME - timeInMode;
      Serial.print("Cooldown: "); Serial.print(remaining / 1000);
      Serial.println(" seconds remaining");
    }
  }
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
