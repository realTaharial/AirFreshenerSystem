#include <WiFi.h>
#include "AdafruitIO_WiFi.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// ================= ADAFRUIT IO AYARLARI =================


// =========================================================

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Adafruit IO Feeds
AdafruitIO_Feed *mq2Feed = io.feed("mq2-value");
AdafruitIO_Feed *mq135Feed = io.feed("mq135-value");
AdafruitIO_Feed *statusFeed = io.feed("smoke-status");
AdafruitIO_Feed *autoModeFeed = io.feed("auto-mode");
AdafruitIO_Feed *sprayControlFeed = io.feed("spray-control");
AdafruitIO_Feed *mq2ThresholdFeed = io.feed("mq2-threshold");
AdafruitIO_Feed *mq135ThresholdFeed = io.feed("mq135-threshold");
AdafruitIO_Feed *wifiRssiFeed = io.feed("wifi-rssi");

// ================= OLED AYARLARI =================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================= PINLER =================

#define MQ2_PIN 34
#define MQ135_PIN 35
#define SERVO_PIN 13

Servo sprayServo;

// ================= THRESHOLD =================

// MQ2 <= 2300 SAFE, üstü SMOKE
// MQ135 <= 450 SAFE, üstü SMOKE
int mq2SmokeThreshold = 2300;
int mq135SmokeThreshold = 450;

// ================= SERVO =================

int REST_ANGLE = 0;
int PRESS_ANGLE = 180;

unsigned long lastSprayTime = 0;
unsigned long sprayCooldown = 10000; // 10 saniye

bool smokeDetected = false;
bool autoMode = true;
bool manualSprayRequest = false;

String statusText = "SAFE";

// ================= ZAMANLAYICILAR =================

unsigned long lastSensorReadTime = 0;
unsigned long sensorReadInterval = 500; // OLED/Serial okuma

unsigned long lastPublishTime = 0;
unsigned long publishInterval = 10000; // Adafruit IO free limit için 10 saniye

// ================= CALLBACK PROTOTYPES =================

void handleAutoMode(AdafruitIO_Data *data);
void handleSprayControl(AdafruitIO_Data *data);
void handleMq2Threshold(AdafruitIO_Data *data);
void handleMq135Threshold(AdafruitIO_Data *data);

// ================= SENSOR OKUMA =================

int readAverage(int pin) {
  long total = 0;

  for (int i = 0; i < 10; i++) {
    total += analogRead(pin);
    delay(5);
  }

  return total / 10;
}

// ================= SERVO HAREKETİ =================

void pressSpray() {
  Serial.println("Servo: spray movement");

  sprayServo.write(PRESS_ANGLE);
  delay(900);

  sprayServo.write(REST_ANGLE);
  delay(900);
}

// ================= OLED =================

void updateOled(int mq2Value, int mq135Value) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Smoke Detection");

  display.setCursor(0, 12);
  display.print("MQ2: ");
  display.print(mq2Value);
  display.print("/");
  display.println(mq2SmokeThreshold);

  display.setCursor(0, 24);
  display.print("MQ135: ");
  display.print(mq135Value);
  display.print("/");
  display.println(mq135SmokeThreshold);

  display.setCursor(0, 36);
  display.print("Mode: ");
  display.println(autoMode ? "AUTO" : "MANUAL");

  display.setCursor(0, 48);
  display.print("Status: ");
  display.println(statusText);

  display.display();
}


void setup() {
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12); // ESP32 ADC: 0-4095

  // OLED I2C pinleri
  Wire.begin(21, 22); // SDA = GPIO21, SCL = GPIO22

  bool oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (!oledOk) {
    Serial.println("OLED 0x3C bulunamadi, 0x3D deneniyor...");
    oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }

  if (!oledOk) {
    Serial.println("OLED bulunamadi! Baglantilari kontrol et.");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Smart Smoke System");
  display.println("Connecting WiFi...");
  display.println("Adafruit IO...");
  display.display();

  // Servo ayari
  sprayServo.setPeriodHertz(50);
  sprayServo.attach(SERVO_PIN, 500, 2400);
  sprayServo.write(REST_ANGLE);

  // Dashboard'dan gelecek verileri dinle
  autoModeFeed->onMessage(handleAutoMode);
  sprayControlFeed->onMessage(handleSprayControl);
  mq2ThresholdFeed->onMessage(handleMq2Threshold);
  mq135ThresholdFeed->onMessage(handleMq135Threshold);

  Serial.print("Adafruit IO'ya baglaniliyor");

  io.connect();

  while (io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("Adafruit IO baglandi!");
  Serial.println(io.statusText());

  // Dashboard'daki son degerleri al
  autoModeFeed->get();
  sprayControlFeed->get();
  mq2ThresholdFeed->get();
  mq135ThresholdFeed->get();

  // Baslangic degerlerini cloud'a yaz
  autoModeFeed->save(1);
  sprayControlFeed->save(0);
  mq2ThresholdFeed->save(mq2SmokeThreshold);
  mq135ThresholdFeed->save(mq135SmokeThreshold);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connected!");
  display.println("Adafruit IO OK");
  display.display();

  delay(1500);
}

// ================= LOOP =================

void loop() {
  io.run();

  unsigned long now = millis();

  if (now - lastSensorReadTime >= sensorReadInterval) {
    lastSensorReadTime = now;

    int mq2Value = readAverage(MQ2_PIN);
    int mq135Value = readAverage(MQ135_PIN);

    if (mq2Value > mq2SmokeThreshold || mq135Value > mq135SmokeThreshold) {
      smokeDetected = true;
      statusText = "SMOKE!";
    } else {
      smokeDetected = false;
      statusText = "SAFE";
    }

    // AUTO mode aciksa ve duman varsa servo otomatik calisir
    if (autoMode && smokeDetected && now - lastSprayTime > sprayCooldown) {
      pressSpray();
      lastSprayTime = millis();
    }

    // Dashboard'dan manuel spray
    if (manualSprayRequest) {
      pressSpray();
      manualSprayRequest = false;
      sprayControlFeed->save(0);
      lastSprayTime = millis();
    }

    Serial.print("MQ2: ");
    Serial.print(mq2Value);
    Serial.print(" / ");
    Serial.print(mq2SmokeThreshold);

    Serial.print(" | MQ135: ");
    Serial.print(mq135Value);
    Serial.print(" / ");
    Serial.print(mq135SmokeThreshold);

    Serial.print(" | Status: ");
    Serial.print(statusText);

    Serial.print(" | Mode: ");
    Serial.print(autoMode ? "AUTO" : "MANUAL");

    Serial.print(" | WiFi RSSI: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);

    updateOled(mq2Value, mq135Value);

    // Adafruit IO'ya verileri 10 saniyede bir gonder
    if (now - lastPublishTime >= publishInterval) {
      lastPublishTime = now;

      mq2Feed->save(mq2Value);
      mq135Feed->save(mq135Value);
      statusFeed->save(statusText);
      wifiRssiFeed->save(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);

      Serial.println("Adafruit IO verileri gonderildi.");
    }
  }
}

// ================= ADAFRUIT CALLBACKS =================

void handleAutoMode(AdafruitIO_Data *data) {
  int value = data->toInt();

  autoMode = value == 1;

  Serial.print("Dashboard auto-mode: ");
  Serial.println(autoMode ? "AUTO" : "MANUAL");
}

void handleSprayControl(AdafruitIO_Data *data) {
  int value = data->toInt();

  if (value == 1) {
    manualSprayRequest = true;
    Serial.println("Dashboard manuel spray istegi geldi.");
  }
}

void handleMq2Threshold(AdafruitIO_Data *data) {
  int value = data->toInt();

  if (value > 0) {
    mq2SmokeThreshold = value;
  }

  Serial.print("MQ2 threshold changed: ");
  Serial.println(mq2SmokeThreshold);
}

void handleMq135Threshold(AdafruitIO_Data *data) {
  int value = data->toInt();

  if (value > 0) {
    mq135SmokeThreshold = value;
  }

  Serial.print("MQ135 threshold changed: ");
  Serial.println(mq135SmokeThreshold);
}