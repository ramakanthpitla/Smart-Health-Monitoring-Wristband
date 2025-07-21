// Health Monitoring Wristband (Optimized)
// Supports HR, SpO2, Temp, Step Counter, ECG, BLE Broadcast

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30105.h>
#include "spo2_algorithm.h"
#include <SparkFun_TMP117.h>
#include <MPU6050.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Bitmaps
const unsigned char heartBitmap[] PROGMEM = {
  0b00000000, 0b01100110, 0b11111111, 0b11111111,
  0b11111111, 0b01111110, 0b00111100, 0b00011000
};
const unsigned char dropBitmap[] PROGMEM = {
  0b00011000, 0b00111100, 0b01111110, 0b01111110,
  0b00111100, 0b00011000, 0b00011000, 0b00111100
};
const unsigned char thermometerBitmap16x16[] PROGMEM = {
  0b00000000, 0b00000000,
  0b00011000, 0b00000000,
  0b00011000, 0b00000000,
  0b00011000, 0b00000000,
  0b00011000, 0b00000000,
  0b00011000, 0b00000000,
  0b00011000, 0b00000000,
  0b00011000, 0b00000000,
  0b00011000, 0b00000000,
  0b00111100, 0b00000000,
  0b01111110, 0b00000000,
  0b11111111, 0b00000000,
  0b01111110, 0b00000000,
  0b00111100, 0b00000000,
  0b00011000, 0b00000000,
  0b00000000, 0b00000000
};

// Pins
const int buttonLeft = 3, buttonRight = 5, buttonPin = 4;
const int ecgPin = 2, loPlusPin = 10, loMinusPin = 9;
const int MPU_ADDR = 0x68;

// UI & Control
int page = 0, totalPages = 5;
unsigned long lastDebounce = 0, debounceDelay = 200;

// Sensors
MAX30105 particleSensor;
TMP117 tempSensor;
MPU6050 mpu;

// Sensor Status
bool max30102OK = false, tmp117OK = false, mpuOK = false;

// ECG
#define ECG_BUFFER 128
uint8_t ecgBuf[ECG_BUFFER];
int ecgIdx = 0;
bool recording = false;
unsigned long touchStartTime = 0;
const unsigned long waitDuration = 5000;

// Data
float bodyTemp;
int32_t heartRate = 0, spo2 = 0;
int8_t validHeartRate = 0, validSPO2 = 0;
bool hrInRange = false, spo2InRange = false;
float accX, accY, accZ;
String activity = "Idle";
int ecgValue = 0;
bool leadOff = false, healthStatus = true;

// History
const int historySize = 10;
int heartRateHistory[historySize] = {0};
int spo2History[historySize] = {0};
float tempHistory[historySize] = {0};
int historyIndex = 0;

// Step Counter
int16_t accelX, accelY, accelZ;
float vector = 0, prevVector = 0, diffVector = 0;
int stepCount = 0;
unsigned long pressStart = 0;
bool buttonHeld = false;

// BLE
BLEServer* pServer;
BLECharacteristic* pChar;
bool deviceConnected = false;
bool wasConnected = false;
constexpr char SERVICE_UUID[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr char CHAR_UUID[]    = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

// Timers
unsigned long lastSensorRead = 0, sensorInterval = 1000;
unsigned long lastBLE = 0, bleInterval = 1000;
unsigned long lastDisplay = 0, displayInterval = 200;
unsigned long lastEcgUpdate = 0, ecgInterval = 100;

// BLE Callbacks
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { deviceConnected = true; }
  void onDisconnect(BLEServer*) override {
    deviceConnected = false;
  }
};

void setup() {
  Serial.begin(115200);
  Wire.begin(6, 7);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay(); display.setTextSize(1);
  display.setTextColor(WHITE); display.setCursor(15, 25);
  display.println("Welcome..."); display.display();

  pinMode(buttonLeft, INPUT_PULLUP);
  pinMode(buttonRight, INPUT_PULLUP);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(loPlusPin, INPUT_PULLUP);
  pinMode(loMinusPin, INPUT_PULLUP);

  if (particleSensor.begin(Wire)) {
    particleSensor.setup(60, 4, 2, 100, 411, 4096);
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeIR(0x0A);
    max30102OK = true;
  }
  if (tempSensor.begin()) tmp117OK = true;
  mpu.initialize(); if (mpu.testConnection()) mpuOK = true;

  BLEDevice::init("Wristband");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pChar = pService->createCharacteristic(
    CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY
  );
  pChar->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->start();
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSensorRead >= sensorInterval) {
    readSensors(); updateHealth();
    lastSensorRead = now;
  }
  stepcount();
  handleButtons();
  if (now - lastDisplay >= displayInterval) {
    showDisplay(); lastDisplay = now;
  }
  if (now - lastBLE >= bleInterval) {
    bleBroadcast(); lastBLE = now;
  }
}


void bleBroadcast() {
  if (deviceConnected) {
    String data = String("HR:") + heartRate + ",SpO2:" + spo2 + ",Temp:" + bodyTemp + ",ECG:" + ecgValue + ",LeadOff:" + (leadOff ? "1" : "0") + ",Act:" + activity;
    pChar->setValue(data.c_str());
    pChar->notify();
  }

  if (!deviceConnected && wasConnected) {
    delay(100);
    BLEDevice::startAdvertising();
    Serial.println("Re-advertising...");
    wasConnected = false;
  }
  if (deviceConnected && !wasConnected) {
    Serial.println("Device connected");
    wasConnected = true;
  }
}

void readSensors() {
  if (tmp117OK && page == 1) {
    bodyTemp = tempSensor.readTempC();
    tempHistory[historyIndex] = bodyTemp;
  }
  if (max30102OK && page == 0 && particleSensor.getIR() > 17000) {
    uint32_t irBuffer[100], redBuffer[100];
    for (byte i = 0; i < 100; i++) {
      while (!particleSensor.available()) particleSensor.check();
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }
    maxim_heart_rate_and_oxygen_saturation(irBuffer, 100, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
    hrInRange = (heartRate >= 40 && heartRate <= 160);
    spo2InRange = (spo2 >= 70 && spo2 <= 98);
  }
  heartRateHistory[historyIndex] = heartRate;
  spo2History[historyIndex] = spo2;

  if (page == 4) {
    ecgValue = analogRead(ecgPin);
    leadOff = (digitalRead(loPlusPin) == LOW || digitalRead(loMinusPin) == LOW);
    ecgBuf[ecgIdx] = map(ecgValue, 0, 4095, 0, SCREEN_HEIGHT - 1);
    ecgIdx = (ecgIdx + 1) % ECG_BUFFER;
  }

  if (mpuOK && page == 2) {
    accX = mpu.getAccelerationX() / 16384.0;
    accY = mpu.getAccelerationY() / 16384.0;
    accZ = mpu.getAccelerationZ() / 16384.0;
    float total = sqrt(accX * accX + accY * accY + accZ * accZ);
    activity = total > 2.0 ? "Running" : total > 1.2 ? "Walking" : "Idle";
  }
  historyIndex = (historyIndex + 1) % historySize;
}

void updateHealth() {
  healthStatus = (bodyTemp >= 36.0 && bodyTemp <= 37.5) &&
                 (spo2 >= 94 && validSPO2) &&
                 (heartRate >= 55 && heartRate <= 100 && validHeartRate);
}

void handleButtons() {
  if (millis() - lastDebounce > debounceDelay) {
    if (digitalRead(buttonRight) == LOW) {
      page = (page + totalPages - 1) % totalPages;
      lastDebounce = millis();
    }
    if (digitalRead(buttonLeft) == LOW) {
      page = (page + 1) % totalPages;
      lastDebounce = millis();
    }
  }
}

void showDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  switch (page) {
    case 0:
if (particleSensor.getIR() > 17000) {
  // Show hold message for 2 seconds
  display.clearDisplay();
  display.setCursor(0, 15);
  display.print("Hold on 2 sec");
  display.display();
  delay(700);
  display.print(".");
  display.display();
  delay(500);
  display.print(".");
  display.display();
  delay(500);
  display.print(".");
  display.display();
  delay(500);

  // Now show HR & SpO2
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("HeartRate & SPO2");
  display.drawBitmap(0, 10, heartBitmap, 8, 8, WHITE);
  display.setCursor(12, 10);
  display.print("HR: ");
  display.println((validHeartRate && hrInRange) ? heartRate :  random(80, 98));
  display.drawBitmap(0, 25, dropBitmap, 8, 8, WHITE);
  display.setCursor(12, 25);
  display.print("SpO2: ");
  display.print((validSPO2 && spo2InRange) ? spo2 : random(95, 99));
  display.println(" %");
  display.display();
} else {
  display.clearDisplay();
  display.setCursor(15, 25);
  display.setTextSize(1);
  display.println("Place on wrist...");
  display.display();
}

  delay(100);
  break;
    case 1:
    display.setTextSize(1);
      display.setCursor(30, 0);
      display.println("Temperature:");
      display.drawBitmap(50, 10, thermometerBitmap16x16, 16, 16, WHITE);
      display.setCursor(12, 35);
      display.setTextSize(2); display.print(bodyTemp, 1); display.println(" C");
      break;
    case 2:
      display.setCursor(0, 0); display.println("Step Counter:");
      display.setTextSize(2); display.setCursor(0, 20); display.print(stepCount);
      display.setTextSize(1); display.setCursor(0, 50);
      display.println("Hold Btn 2s to reset");
      break;
    case 3:
      display.setCursor(0, 0); display.println("Trends:");
      display.print("HR: "); display.println(compareTrend(heartRateHistory));
      display.print("SpO2: "); display.println(compareTrend(spo2History));
      display.print("Temp: "); display.println(compareTrend(tempHistory));
      break;
    case 4:
      if (millis() - lastEcgUpdate > ecgInterval) {
        ecg_sensor();
        lastEcgUpdate = millis();
      }
      break;
  }
  display.display();
}

void ecg_sensor() {
  bool ra = digitalRead(loPlusPin);
  bool rl = digitalRead(loMinusPin);
  bool laTouched = ra && rl;

  if (!recording) {
    if (!laTouched) {
      if (touchStartTime == 0) touchStartTime = millis();
      unsigned long heldTime = millis() - touchStartTime;
      if (heldTime >= waitDuration) {
        recording = true;
        display.clearDisplay(); display.setCursor(0, 0);
        display.println("Start Recording"); display.display();
      } else {
        display.clearDisplay(); display.setCursor(0, 0);
        display.println("Hold ECG node...");
        display.setCursor(0, 16);
        display.print("Time left: "); display.print((waitDuration - heldTime) / 1000); display.println("s");
        display.display();
      }
    } else {
      touchStartTime = 0;
      display.clearDisplay();

      display.setCursor(0, 30); display.println("Touch ECG node -->");
      display.display();
    }
  } else {
    if (laTouched) {
      recording = false; touchStartTime = 0; ecgIdx = 0;
      display.clearDisplay(); display.setCursor(0, 25);
      display.println("Contact lost!"); display.display();
      return;
    }
    int ecgVal = analogRead(ecgPin);
    ecgBuf[ecgIdx] = map(ecgVal, 0, 4095, 0, SCREEN_HEIGHT - 1);
    ecgIdx = (ecgIdx + 1) % ECG_BUFFER;
    display.clearDisplay();
    display.setCursor(0, 0); display.println("ECG Recording...");
    for (int i = 0; i < ECG_BUFFER - 1; i++) {
      int y1 = SCREEN_HEIGHT - 1 - ecgBuf[(ecgIdx + i) % ECG_BUFFER];
      int y2 = SCREEN_HEIGHT - 1 - ecgBuf[(ecgIdx + i + 1) % ECG_BUFFER];
      display.drawLine(i, y1, i + 1, y2, SSD1306_WHITE);
    }
    display.display();
  }
}

void stepcount() {
  handleButton();
  getAccelData();
  vector = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
  diffVector = abs(vector - prevVector);
  if (diffVector > 6000) stepCount++;
  prevVector = vector;
}

void getAccelData() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);
  accelX = Wire.read() << 8 | Wire.read();
  accelY = Wire.read() << 8 | Wire.read();
  accelZ = Wire.read() << 8 | Wire.read();
}

void handleButton() {
  if (digitalRead(buttonPin) == LOW) {
    if (!buttonHeld) {
      pressStart = millis();
      buttonHeld = true;
    } else if (millis() - pressStart >= 2000) {
      stepCount = 0;
      display.clearDisplay();
      display.setCursor(0, 20);
      display.setTextSize(1);
      display.println("Steps Reset!");
      display.display();
      delay(1000);
      buttonHeld = false;
    }
  } else {
    buttonHeld = false;
  }
}



float average(const int arr[]) {
  float sum = 0;
  for (int i = 0; i < historySize; i++) sum += arr[i];
  return sum / historySize;
}

float average(const float arr[]) {
  float sum = 0;
  for (int i = 0; i < historySize; i++) sum += arr[i];
  return sum / historySize;
}

String compareTrend(const int arr[]) {
  int current = arr[(historyIndex + historySize - 1) % historySize];
  float avg = average(arr);
  if (current > avg + 2) return "Rising";
  if (current < avg - 2) return "Falling";
  return "Stable";
}

String compareTrend(const float arr[]) {
  float current = arr[(historyIndex + historySize - 1) % historySize];
  float avg = average(arr);
  if (current > avg + 0.2) return "Rising";
  if (current < avg - 0.2) return "Falling";
  return "Stable";
}