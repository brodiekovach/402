/*
 * Automatic Defogging Mirror
 * Arduino Uno R4 WiFi with Modulino Thermo
 *
 * This system automatically heats a mirror to prevent fogging during showers.
 * It uses light detection to activate, humidity sensing to trigger heating,
 * and temperature monitoring to control heating intensity.
 */

#include <Modulino.h>

ModulinoThermo thermo;

// Pin Definitions
#define LIGHT_SENSOR_PIN A0 // Photoresistor (LDR) analog pin
#define RELAY_PIN 7         // Relay module control pin for heating pad

// System Thresholds (adjust these based on testing)
#define LIGHT_THRESHOLD 300      // Light level to activate system (0-1023, lower = darker)
#define HUMIDITY_THRESHOLD 65.0  // Humidity % to trigger heating (typical shower: 70-90%)
#define TEMP_OFFSET 2.0          // Keep mirror this many degrees C warmer than ambient
#define MIN_HEATING_TEMP 15.0    // Don't heat if ambient is below this (safety)
#define MAX_HEATING_TEMP 45.0    // Maximum mirror temperature (safety limit)

// Timing Constants
#define SENSOR_READ_INTERVAL 2000  // Read sensors every 2 seconds
#define HEATING_CYCLE 5000         // Check heating decision every 5 seconds

// System State
bool systemActive = false;
bool heatingActive = false;
unsigned long lastSensorRead = 0;
unsigned long lastHeatingCycle = 0;

// Function declarations
void makeHeatingDecision(float humidity, float ambientTemp, float mirrorTemp);
void turnOnHeating();
void turnOffHeating();
ModulinoButtons 
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
  Serial.println("Using Arduino Modulino Thermo sensor");
  Serial.println("System initialized. Waiting for light...");
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

    // For now, use ambient temperature as mirror temp estimate
    // You can add a separate thermistor on A1 later for actual mirror temp
    float mirrorTemp = ambientTemp; // Will be close to ambient initially

    // Check for sensor errors
    if (isnan(humidity) || isnan(ambientTemp) || isnan(mirrorTemp)) {
      Serial.println("Error reading Modulino Thermo sensor!");
      return;
    }

    // Display current readings
    Serial.println("--- Sensor Readings ---");
    Serial.print("Light Level: "); Serial.println(lightLevel);
    Serial.print("Humidity: "); Serial.print(humidity); Serial.println("%");
    Serial.print("Ambient Temp: "); Serial.print(ambientTemp); Serial.println("°C");
    Serial.print("Mirror Temp: "); Serial.print(mirrorTemp); Serial.println("°C");

    // Make heating decision every HEATING_CYCLE
    if (currentTime - lastHeatingCycle >= HEATING_CYCLE) {
      lastHeatingCycle = currentTime;
      makeHeatingDecision(humidity, ambientTemp, mirrorTemp);
    }
  }
}

void makeHeatingDecision(float humidity, float ambientTemp, float mirrorTemp) {
  // Target temperature: ambient + offset
  float targetTemp = ambientTemp + TEMP_OFFSET;

  // Safety check: don't heat if ambient is too cold or target is too high
  if (ambientTemp < MIN_HEATING_TEMP || targetTemp > MAX_HEATING_TEMP) {
    if (heatingActive) {
      turnOffHeating();
      Serial.println("Safety limit reached - Heating OFF");
    }
    return;
  }

  // Decision logic: Turn on heating if high humidity AND mirror is cooler than target
  bool shouldHeat = (humidity >= HUMIDITY_THRESHOLD) && (mirrorTemp < targetTemp);

  if (shouldHeat && !heatingActive) {
    turnOnHeating();
    Serial.print("High humidity detected - Heating ON (Target: ");
    Serial.print(targetTemp); Serial.println("°C)");
  }
  else if (!shouldHeat && heatingActive) {
    turnOffHeating();
    Serial.println("Conditions normalized - Heating OFF");
  }
  else if (heatingActive) {
    Serial.print("Heating active (Current: "); Serial.print(mirrorTemp);
    Serial.print("°C, Target: "); Serial.print(targetTemp); Serial.println("°C)");
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
