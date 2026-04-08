/*
 * Automatic Defogging Mirror
 * Arduino Uno R4 WiFi
 *
 * This system automatically heats a mirror to prevent fogging during showers.
 * It uses light detection to activate, humidity sensing to trigger heating,
 * and temperature monitoring to control heating intensity.
 */

#include <DHT.h>

// Pin Definitions
#define DHT_PIN 2           // DHT11/DHT22 sensor data pin
#define LIGHT_SENSOR_PIN A0 // Photoresistor (LDR) analog pin
#define MIRROR_TEMP_PIN A1  // Thermistor or temp sensor for mirror
#define RELAY_PIN 7         // Relay module control pin for heating pad

// Sensor Configuration
#define DHT_TYPE DHT11      // Change to DHT22 if you have that model
DHT dht(DHT_PIN, DHT_TYPE);

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

void setup() {
  Serial.begin(9600);

  // Initialize pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relay OFF initially

  // Initialize DHT sensor
  dht.begin();

  Serial.println("=== Automatic Defogging Mirror ===");
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

    // Read humidity and ambient temperature from DHT sensor
    float humidity = dht.readHumidity();
    float ambientTemp = dht.readTemperature();

    // Check for sensor errors
    if (isnan(humidity) || isnan(ambientTemp)) {
      Serial.println("Error reading DHT sensor!");
      return;
    }

    // Read mirror temperature (using thermistor - requires calibration)
    float mirrorTemp = readMirrorTemperature();

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

float readMirrorTemperature() {
  // Read thermistor on mirror surface
  // This is a simplified version - actual implementation depends on your sensor

  int rawValue = analogRead(MIRROR_TEMP_PIN);

  // For thermistor (10K NTC), use Steinhart-Hart equation
  // This requires calibration - values below are approximate
  float resistance = 10000.0 * (1023.0 / rawValue - 1.0);
  float steinhart;
  steinhart = resistance / 10000.0;     // (R/Ro)
  steinhart = log(steinhart);           // ln(R/Ro)
  steinhart /= 3950.0;                  // 1/B * ln(R/Ro)
  steinhart += 1.0 / (25.0 + 273.15);   // + (1/To)
  steinhart = 1.0 / steinhart;          // Invert
  steinhart -= 273.15;                  // Convert to Celsius

  return steinhart;

  // Alternative: If using DS18B20 digital sensor, use:
  // #include <OneWire.h>
  // #include <DallasTemperature.h>
  // And follow DS18B20 library documentation
}
