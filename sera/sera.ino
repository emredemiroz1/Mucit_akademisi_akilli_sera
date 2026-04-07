#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>

// Firebase eklentisi için gerekli ayarlar
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// --- WİFİ VE FİREBASE AYARLARI ---
#define WIFI_SSID "FiberHGW_ZT2K3R_2.4GHz"
#define WIFI_PASSWORD "cjsgsrDtyc"
#define API_KEY "AIzaSyAR1cyO7pFPM5RLvO308uAcSyf5HaqAizM"
#define FIREBASE_PROJECT_ID "akillisera-b71d4"
#define APP_ID "master-iot-final-v3"

// --- PİN TANIMLAMALARI ---
// 1. Toprak ve Su Sensörleri (ADC1 pinleri - WiFi ile uyumlu)
#define SOIL_1 32          // GPIO32
#define SOIL_2 33          // GPIO33
#define SOIL_3 34          // GPIO34
#define SOIL_4 35          // GPIO35
#define SOIL_5 39          // GPIO39 (VN)
#define WATER_TANK_PIN 36  // GPIO36 (VP)

// 2. DHT11 Sensörleri
#define DHT_1_PIN 4   // GPIO4
#define DHT_2_PIN 13  // GPIO13
#define DHT_3_PIN 14  // GPIO14
#define DHT_4_PIN 25  // GPIO25
#define DHT_5_PIN 26  // GPIO26
#define DHTTYPE DHT11

DHT dht1(DHT_1_PIN, DHTTYPE);
DHT dht2(DHT_2_PIN, DHTTYPE);
DHT dht3(DHT_3_PIN, DHTTYPE);
DHT dht4(DHT_4_PIN, DHTTYPE);
DHT dht5(DHT_5_PIN, DHTTYPE);

// 3. L298N Sürücü 1 (Su Pompaları)
// ENA ve ENB jumper takılı varsayılmıştır
#define PUMP_L_IN1 27  // GPIO27
#define PUMP_L_IN2 16  // GPIO16 (RX2)
#define PUMP_R_IN3 17  // GPIO17 (TX2)
#define PUMP_R_IN4 18  // GPIO18

// 4. L298N Sürücü 2 (Fanlar)
// ENA ve ENB jumper takılı varsayılmıştır
#define FAN_L_IN1 19  // GPIO19
#define FAN_L_IN2 21  // GPIO21
#define FAN_R_IN3 22  // GPIO22
#define FAN_R_IN4 23  // GPIO23

// --- FİREBASE NESNELERİ ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String documentPath = "artifacts/" + String(APP_ID) + "/public/data/greenhouse/status";

unsigned long lastUpdate = 0;
const unsigned long updateInterval = 4000;

// --- WİFİ OLAY YÖNETİMİ ---
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("WiFi Baglandi! IP Adresi: ");
      Serial.println(WiFi.localIP());
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi Baglantisi koptu! Yeniden baglaniliyor...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      break;

    default:
      break;
  }
}

// Analog toprak nemini yüzdeye çevir
float mapSoilMoisture(int analogValue) {
  int percentage = map(analogValue, 4095, 1500, 0, 100);
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;
  return percentage;
}

// Tüm pompa ve fanları kapat
void stopAllMotors() {
  digitalWrite(PUMP_L_IN1, LOW);
  digitalWrite(PUMP_L_IN2, LOW);

  digitalWrite(PUMP_R_IN3, LOW);
  digitalWrite(PUMP_R_IN4, LOW);

  digitalWrite(FAN_L_IN1, LOW);
  digitalWrite(FAN_L_IN2, LOW);

  digitalWrite(FAN_R_IN3, LOW);
  digitalWrite(FAN_R_IN4, LOW);
}

// Pompa kontrolü: tek yön aç/kapat
void drivePump(int in1, int in2, bool state) {
  if (state) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }
}

// Fan kontrolü: tek yön aç/kapat
void driveFan(int in1, int in2, bool state) {
  if (state) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // DHT sensörlerini başlat
  dht1.begin();
  dht2.begin();
  dht3.begin();
  dht4.begin();
  dht5.begin();

  // Motor/fan pinleri çıkış olarak ayarlanıyor
  pinMode(PUMP_L_IN1, OUTPUT);
  pinMode(PUMP_L_IN2, OUTPUT);
  pinMode(PUMP_R_IN3, OUTPUT);
  pinMode(PUMP_R_IN4, OUTPUT);

  pinMode(FAN_L_IN1, OUTPUT);
  pinMode(FAN_L_IN2, OUTPUT);
  pinMode(FAN_R_IN3, OUTPUT);
  pinMode(FAN_R_IN4, OUTPUT);

  stopAllMotors();

  // WiFi bağlantısı
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(WiFiEvent);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("WiFi baglaniliyor");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi baglandi!");

  // Firebase bağlantısı
  config.api_key = API_KEY;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Sistem hazir.");
}

void loop() {
  if (!Firebase.ready()) {
    delay(100);
    return;
  }

  if (millis() - lastUpdate > updateInterval) {
    lastUpdate = millis();

    float t1 = dht1.readTemperature();
    float h1 = dht1.readHumidity();

    float t2 = dht2.readTemperature();
    float h2 = dht2.readHumidity();

    float t3 = dht3.readTemperature();
    float h3 = dht3.readHumidity();

    float t4 = dht4.readTemperature();
    float h4 = dht4.readHumidity();

    float t5 = dht5.readTemperature();
    float h5 = dht5.readHumidity();

    float s1 = mapSoilMoisture(analogRead(SOIL_1));
    float s2 = mapSoilMoisture(analogRead(SOIL_2));
    float s3 = mapSoilMoisture(analogRead(SOIL_3));
    float s4 = mapSoilMoisture(analogRead(SOIL_4));
    float s5 = mapSoilMoisture(analogRead(SOIL_5));

    float water_level = mapSoilMoisture(analogRead(WATER_TANK_PIN));

    float left_temp = (isnan(t1) || isnan(t2)) ? 24.0 : (t1 + t2) / 2.0;
    float left_hum  = (isnan(h1) || isnan(h2)) ? 50.0 : (h1 + h2) / 2.0;
    float left_soil = (s1 + s2 + (s3 / 2.0)) / 2.5;

    float right_temp = (isnan(t4) || isnan(t5)) ? 24.0 : (t4 + t5) / 2.0;
    float right_hum  = (isnan(h4) || isnan(h5)) ? 50.0 : (h4 + h5) / 2.0;
    float right_soil = (s4 + s5 + (s3 / 2.0)) / 2.5;

    Serial.println("----- SENSOR VERILERI -----");
    Serial.printf("Sol  -> Isi: %.1f C | Nem: %.1f %% | Toprak: %.1f %%\n", left_temp, left_hum, left_soil);
    Serial.printf("Sag  -> Isi: %.1f C | Nem: %.1f %% | Toprak: %.1f %%\n", right_temp, right_hum, right_soil);
    Serial.printf("Ana Depo Su Seviyesi: %.1f %%\n", water_level);

    FirebaseJson content;
    // Bölgesel Ortalamalar
    content.set("fields/left_temp/doubleValue", left_temp);
    content.set("fields/left_hum/doubleValue", left_hum);
    content.set("fields/left_soil/doubleValue", left_soil);
    content.set("fields/right_temp/doubleValue", right_temp);
    content.set("fields/right_hum/doubleValue", right_hum);
    content.set("fields/right_soil/doubleValue", right_soil);
    content.set("fields/waterLevel/doubleValue", water_level);

    // BİREYSEL 5 SENSÖR (TERMAL HARİTA İÇİN)
    content.set("fields/s1_t/doubleValue", isnan(t1) ? 24.0 : t1);
    content.set("fields/s1_h/doubleValue", isnan(h1) ? 50.0 : h1);
    content.set("fields/s1_s/doubleValue", s1);

    content.set("fields/s2_t/doubleValue", isnan(t2) ? 24.0 : t2);
    content.set("fields/s2_h/doubleValue", isnan(h2) ? 50.0 : h2);
    content.set("fields/s2_s/doubleValue", s2);

    content.set("fields/s3_t/doubleValue", isnan(t3) ? 24.0 : t3);
    content.set("fields/s3_h/doubleValue", isnan(h3) ? 50.0 : h3);
    content.set("fields/s3_s/doubleValue", s3);

    content.set("fields/s4_t/doubleValue", isnan(t4) ? 24.0 : t4);
    content.set("fields/s4_h/doubleValue", isnan(h4) ? 50.0 : h4);
    content.set("fields/s4_s/doubleValue", s4);

    content.set("fields/s5_t/doubleValue", isnan(t5) ? 24.0 : t5);
    content.set("fields/s5_h/doubleValue", isnan(h5) ? 50.0 : h5);
    content.set("fields/s5_s/doubleValue", s5);

    String updateMask = "left_temp,left_hum,left_soil,right_temp,right_hum,right_soil,waterLevel,s1_t,s1_h,s1_s,s2_t,s2_h,s2_s,s3_t,s3_h,s3_s,s4_t,s4_h,s4_s,s5_t,s5_h,s5_s";

    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), updateMask.c_str())) {
      Serial.println("Veriler Firebase'e gonderildi.");
    } else {
      Serial.println("Firebase gonderim hatasi: " + fbdo.errorReason());
    }

    fbdo.clear();

    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "")) {
      FirebaseJson json(fbdo.payload());
      FirebaseJsonData jsonData;

      bool leftPump = false;
      bool rightPump = false;
      bool leftFan = false;
      bool rightFan = false;

      if (json.get(jsonData, "fields/left_pump/booleanValue"))  leftPump = jsonData.boolValue;
      if (json.get(jsonData, "fields/right_pump/booleanValue")) rightPump = jsonData.boolValue;
      if (json.get(jsonData, "fields/left_fan/booleanValue"))   leftFan = jsonData.boolValue;
      if (json.get(jsonData, "fields/right_fan/booleanValue"))  rightFan = jsonData.boolValue;

      drivePump(PUMP_L_IN1, PUMP_L_IN2, leftPump);
      drivePump(PUMP_R_IN3, PUMP_R_IN4, rightPump);
      driveFan(FAN_L_IN1, FAN_L_IN2, leftFan);
      driveFan(FAN_R_IN3, FAN_R_IN4, rightFan);

      Serial.println("----- MOTOR DURUMLARI -----");
      Serial.printf("Sol Pompa: %d | Sag Pompa: %d\n", leftPump, rightPump);
      Serial.printf("Sol Fan:   %d | Sag Fan:   %d\n", leftFan, rightFan);
    } else {
      Serial.println("Firebase okuma hatasi: " + fbdo.errorReason());
      stopAllMotors();
    }

    fbdo.clear();
    Serial.println("---------------------------\n");
  }
}