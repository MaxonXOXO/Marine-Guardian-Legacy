#include <ThingerESP32.h>
#include <Wire.h>
#include <DHT.h>
#include <TinyGPS++.h>
#include <math.h>
#include <LiquidCrystal_I2C.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_MPU6050.h> // Library for MPU6050

// Define Wi-Fi credentials
#define WIFI_SSID "Thunderbird 242"
#define WIFI_PASSWORD "9947666371"

// Thinger.io credentials
#define USERNAME "Gauth_xo"
#define DEVICE_ID "Nodemcuv3"
#define DEVICE_CREDENTIAL "Y2&b8mnBm#+7VmE1"

// Thinger.io setup
ThingerESP32 thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

// Sensor pins and setup
#define TRIG_PIN 18
#define ECHO_PIN 19
#define WATER_LEVEL_PIN 34
#define FLAME_SENSOR_PIN 23
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define MQ2_PIN 36

DHT dht(DHT_PIN, DHT_TYPE);
TinyGPSPlus gps;
LiquidCrystal_I2C lcd(0x27, 16, 2);
DFRobotDFPlayerMini dfplayer;
Adafruit_MPU6050 mpu;

bool waterDetected = false;
bool flameDetected = false;
bool flameLedActive = false; // Variable to track if the flame LED is active
unsigned long flameDetectedTime = 0; // Time when flame was detected
unsigned long flameLedStartTime = 0; // Time when flame LED was activated

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN); // Start Serial1 for GPS

  thing.add_wifi(WIFI_SSID, WIFI_PASSWORD);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);
  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Welcome to");
  lcd.setCursor(0, 1);
  lcd.print("Marine Guardian");
  delay(3000);
  lcd.clear();

  if (!mpu.begin()) {
    Serial.println("Could not find a valid MPU6050 sensor, check wiring!");
    while (1);
  }

  // Initialize DFPlayer
  Serial2.begin(9600, SERIAL_8N1, 12, 14); // Use GPIO12 as RX and GPIO14 as TX for DFPlayer Mini
  if (!dfplayer.begin(Serial2)) {
    Serial.println("Unable to begin DFPlayer Mini.");
    while (1);
  }
  dfplayer.setTimeOut(500);
  dfplayer.volume(25); 
  dfplayer.play(3); // Play welcome message (003.mp3)

  // Define Thinger.io resources
  thing["distance"] >> [](pson &out) { out = getDistance(); };
  thing["x_tilt"] >> [](pson &out) { out = calculateTilt('x'); };
  thing["y_tilt"] >> [](pson &out) { out = calculateTilt('y'); };
  thing["z_tilt"] >> [](pson &out) { out = calculateTilt('z'); };
  thing["flame_led"] >> [](pson &out) { out = flameLedActive ? 1 : 0; }; // Update flame_led as 1 or 0
  thing["temperature"] >> [](pson &out) { out = dht.readTemperature(); };
  thing["humidity"] >> [](pson &out) { out = dht.readHumidity(); };
  thing["gas_level"] >> [](pson &out) { out = (analogRead(MQ2_PIN) / 4095.0) * 100.0; };
  thing["location"] >> [](pson &out) {
    if (gps.location.isValid()) {
      out["lat"] = gps.location.lat();
      out["lon"] = gps.location.lng();
    } else {
      Serial.println("GPS location not valid.");
    }
  };
  thing["water_moisture"] >> [](pson &out) {
    out = (analogRead(WATER_LEVEL_PIN) / 4095.0) * 100.0;
  };
}

void loop() {
  thing.handle();

  float waterMoisturePercentage = (analogRead(WATER_LEVEL_PIN) / 4095.0) * 100.0;

  if (waterMoisturePercentage > 30.0) {
    if (!waterDetected) {
      waterDetected = true;
      displayAlert("Attention!", "Water detected!");
      dfplayer.play(2); // Water alarm
    }
  } else if (waterDetected) {
    waterDetected = false;
  }

  if (digitalRead(FLAME_SENSOR_PIN) == LOW && !flameDetected) {
    flameDetected = true;
    flameDetectedTime = millis();
    flameLedActive = true; // Turn on the flame LED
    flameLedStartTime = millis(); // Record the start time for the LED
    displayAlert("Attention!", "Fire detected!");
    dfplayer.play(1); // Fire alarm
  } else if (flameDetected && millis() - flameDetectedTime >= 5000) {
    flameDetected = false;
  }

  // Check if the flame LED should turn off after 20 seconds
  if (flameLedActive && millis() - flameLedStartTime >= 20000) {
    flameLedActive = false; // Turn off the flame LED after 20 seconds
  }

  while (Serial1.available() > 0) {
    gps.encode(Serial1.read());
  }

  if (!waterDetected && !flameDetected) {
    displayNormal("No issues found");
  }
}

float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = (duration * 0.034) / 2;
  return distance;
}

float calculateTilt(char axis) {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float x = a.acceleration.x;
  float y = a.acceleration.y;
  float z = a.acceleration.z;

  switch (axis) {
    case 'x': return atan(x / sqrt(y * y + z * z)) * (180 / PI);
    case 'y': return atan(y / sqrt(x * x + z * z)) * (180 / PI);
    case 'z': return atan(z / sqrt(x * x + y * y)) * (180 / PI);
    default: return 0;
  }
}

void displayAlert(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  for (int i = 0; i < 5; i++) {
    lcd.noBacklight();
    delay(500);
    lcd.backlight();
    delay(500);
  }
}

void displayNormal(const char* message) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(message);
  lcd.backlight();
}
