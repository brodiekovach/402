/*
 * Automatic Defogging Mirror
 * Arduino Nano R4 WiFi with Modulino Thermo
 *
 * SENSOR PLACEMENT:
 * - Modulino Thermo: Mount in bathroom air near mirror (NOT on mirror)
 *   Measures ambient humidity and temperature
 * - Photoresistor (LDR): Mount to detect bathroom light
 * - Heating pad: Attached to back of mirror, controlled by relay
 *
 * OPERATION:
 * 1. Light sensor detects bathroom light ON → System activates
 * 2. High humidity detected (shower running) → Heating starts
 * 3. Heating maintains for set duration to prevent fogging
 * 4. Light OFF or low humidity → System deactivates
 */

#include <Modulino.h>

ModulinoThermo thermo;

// Pin Definitions
#define LIGHT_SENSOR_PIN A0 // Photoresistor (LDR) analog pin
#define RELAY_PIN 7         // Relay module control pin for heating pad

// System Thresholds (ADJUST THESE based on your testing!)
#define LIGHT_THRESHOLD 300      // Light level to activate system (0-1023 scale)
                                 // LOWER value = darker. Adjust based on your bathroom lighting
#define HUMIDITY_THRESHOLD 60.0  // Humidity % to trigger heating (typical shower: 70-90%)
#define HUMIDITY_DROP 50.0       // Turn off when humidity drops below this
#define MIN_HEATING_TEMP 10.0    // Don't heat if too cold (safety)
#define MAX_HEATING_TEMP 50.0    // Maximum allowed temp (safety limit)
#define HEATING_DURATION 300000  // Keep heating for 5 minutes once triggered (milliseconds)

// Timing Constants
#define SENSOR_READ_INTERVAL 2000  // Read sensors every 2 seconds
#define HEATING_CHECK_INTERVAL 3000 // Check heating decision every 3 seconds

// System State
bool systemActive = false;
bool heatingActive = false;
unsigned long lastSensorRead = 0;
unsigned long lastHeatingCheck = 0;
unsigned long heatingStartTime = 0;

// Function declarations
void makeHeatingDecision(float humidity, float ambientTemp);
void turnOnHeating();
void turnOffHeating();

void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(10); } // Wait for serial connection

  // Initialize pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relay OFF initially

  // Initialize Modulino system
  Modulino.begin();
  thermo.begin();

  Serial.println("=== Automatic Defogging Mirror ===");
  Serial.println("Hardware: Arduino Nano R4 WiFi + Modulino Thermo");
  Serial.println("Mount Modulino Thermo in ambient air (NOT on mirror)");
  Serial.println("System initialized. Waiting for light activation...");
  Serial.println();
  delay(2000); // Let sensors stabilize
}

void loop() {
  unsigned long currentTime = millis();

  // Read light sensor to determine if system should be active
  int lightLevel = analogRead(LIGHT_SENSOR_PIN);
  systemActive = (lightLevel > LIGHT_THRESHOLD);

  // If no light detected, turn off everything and wait
  if (!systemActive) {
    if (heatingActive) {
      turnOffHeating();
      Serial.println("Light off - System deactivated");
    }
    delay(1000);
    return;
  }

  // System is active - read sensors at intervals
  if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = currentTime;

    // Read humidity and ambient temperature from Modulino Thermo
    float humidity = thermo.getHumidity();
    float ambientTemp = thermo.getTemperature();

    // Check for sensor errors
    if (isnan(humidity) || isnan(ambientTemp)) {
      Serial.println("ERROR: Cannot read Modulino Thermo sensor!");
      if (heatingActive) turnOffHeating(); // Safety: turn off if sensor fails
      return;
    }

    // Display current readings
    Serial.println("--- Sensor Readings ---");
    Serial.print("Light Level: "); Serial.print(lightLevel);
    Serial.println(lightLevel > LIGHT_THRESHOLD ? " (ACTIVE)" : " (inactive)");
    Serial.print("Humidity: "); Serial.print(humidity, 1); Serial.println("%");
    Serial.print("Ambient Temp: "); Serial.print(ambientTemp, 1); Serial.println("°C");
    
    // Make heating decision every HEATING_CHECK_INTERVAL
    if (currentTime - lastHeatingCheck >= HEATING_CHECK_INTERVAL) {
      lastHeatingCheck = currentTime;
      makeHeatingDecision(humidity, ambientTemp);
    }
  }
}

void makeHeatingDecision(float humidity, float ambientTemp) {
  unsigned long currentTime = millis();
  
  // Safety check: don't heat if ambient temperature is out of safe range
  if (ambientTemp < MIN_HEATING_TEMP || ambientTemp > MAX_HEATING_TEMP) {
    if (heatingActive) {
      turnOffHeating();
      Serial.print("SAFETY: Temperature out of range (");
      Serial.print(ambientTemp, 1); Serial.println("°C) - Heating OFF");
    }
    return;
  }

  // Check if heating duration has expired
  if (heatingActive && (currentTime - heatingStartTime >= HEATING_DURATION)) {
    turnOffHeating();
    Serial.println("Heating duration expired - Heating OFF");
    return;
  }

  // Main logic: Start heating if high humidity detected and not already heating
  if (!heatingActive && humidity >= HUMIDITY_THRESHOLD) {
    turnOnHeating();
    heatingStartTime = currentTime;
    Serial.print("HIGH HUMIDITY DETECTED ("); Serial.print(humidity, 1);
    Serial.println("%) - Heating ON");
    Serial.print("Will maintain heating for ");
    Serial.print(HEATING_DURATION / 60000); Serial.println(" minutes");
  }
  // Stop heating if humidity drops significantly (shower ended)
  else if (heatingActive && humidity < HUMIDITY_DROP) {
    turnOffHeating();
    Serial.print("Humidity normalized ("); Serial.print(humidity, 1);
    Serial.println("%) - Heating OFF");
  }
  // Status update while heating
  else if (heatingActive) {
    unsigned long timeRemaining = HEATING_DURATION - (currentTime - heatingStartTime);
    Serial.print("Heating active - Time remaining: ");
    Serial.print(timeRemaining / 60000); Serial.print("m ");
    Serial.print((timeRemaining % 60000) / 1000); Serial.println("s");
  }
}

void turnOnHeating() {
  digitalWrite(RELAY_PIN, HIGH); // Activate relay
  heatingActive = true;
}

void turnOffHeating() {
  digitalWrite(RELAY_PIN, LOW); // Deactivate relay
  heatingActive = false;
}
