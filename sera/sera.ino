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
// 1. Toprak ve Su Sensörleri (SADECE ADC1 PİNLERİ - Wi-Fi ile sorunsuz çalışır)
#define SOIL_1 32 // Sol Bölge 1
#define SOIL_2 33 // Sol Bölge 2
#define SOIL_3 34 // Merkez Bölge
#define SOIL_4 35 // Sağ Bölge 1
#define SOIL_5 39 // Sağ Bölge 2
#define WATER_TANK_PIN 36 // YENİ: Ana Su Deposu Seviye Sensörü

// 2. DHT11 Sensörleri (Dijital pinler, ADC2 kullanılabilir sorun yaratmaz)
#define DHT_1_PIN 4  // Sol Bölge 1
#define DHT_2_PIN 13 // Sol Bölge 2
#define DHT_3_PIN 14 // Merkez Bölge
#define DHT_4_PIN 25 // Sağ Bölge 1
#define DHT_5_PIN 26 // Sağ Bölge 2
#define DHTTYPE DHT11 

DHT dht1(DHT_1_PIN, DHTTYPE);
DHT dht2(DHT_2_PIN, DHTTYPE);
DHT dht3(DHT_3_PIN, DHTTYPE);
DHT dht4(DHT_4_PIN, DHTTYPE);
DHT dht5(DHT_5_PIN, DHTTYPE);

// 3. L298N Sürücü 1 (Su Pompaları)
// Motor A (Sol Su Pompası)
#define PUMP_L_IN1 27
#define PUMP_L_IN2 16
// Motor B (Sağ Su Pompası)
#define PUMP_R_IN3 17
#define PUMP_R_IN4 18

// 4. L298N Sürücü 2 (Fanlar / Pervaneler)
// Motor A (Sol Fan)
#define FAN_L_IN1 19
#define FAN_L_IN2 21
// Motor B (Sağ Fan)
#define FAN_R_IN3 22
#define FAN_R_IN4 23

// --- FİREBASE NESNELERİ ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String documentPath = "artifacts/" + String(APP_ID) + "/public/data/greenhouse/status";

unsigned long lastUpdate = 0;
const long updateInterval = 4000; // Her 4 saniyede bir okuma ve yazma yap

// --- WİFİ KORUMASI VE OTOMATİK YENİDEN BAĞLANMA ---
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("WiFi Baglandi! IP Adresi: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi Baglantisi Koptu! Yeniden baglanmaya calisiliyor...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  
  // DHT Sensörleri Başlat
  dht1.begin(); dht2.begin(); dht3.begin(); dht4.begin(); dht5.begin();

  // Motor Pinleri Çıkış Ayarı
  pinMode(PUMP_L_IN1, OUTPUT); pinMode(PUMP_L_IN2, OUTPUT);
  pinMode(PUMP_R_IN3, OUTPUT); pinMode(PUMP_R_IN4, OUTPUT);
  pinMode(FAN_L_IN1, OUTPUT); pinMode(FAN_L_IN2, OUTPUT);
  pinMode(FAN_R_IN3, OUTPUT); pinMode(FAN_R_IN4, OUTPUT);

  // Başlangıçta tüm motorları kapat
  stopAllMotors();

  // WiFi Bağlantısı
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true); 
  WiFi.onEvent(WiFiEvent);     
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("WiFi Baglaniliyor...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Baglandi!");

  // Firebase Bağlantısı
  config.api_key = API_KEY;
  config.signer.test_mode = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// Analog Toprak Nem Değerini Yüzdeye Çevirir (0-4095 arasını 100-0 arasına mapler)
float mapSoilMoisture(int analogValue) {
  // Islaklık değerine göre kalibre edebilirsiniz. Genelde kuru=4095, su içi=1500 civarıdır.
  int percentage = map(analogValue, 4095, 1500, 0, 100); 
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;
  return percentage;
}

void stopAllMotors() {
  digitalWrite(PUMP_L_IN1, LOW); digitalWrite(PUMP_L_IN2, LOW);
  digitalWrite(PUMP_R_IN3, LOW); digitalWrite(PUMP_R_IN4, LOW);
  digitalWrite(FAN_L_IN1, LOW);  digitalWrite(FAN_L_IN2, LOW);
  digitalWrite(FAN_R_IN3, LOW);  digitalWrite(FAN_R_IN4, LOW);
}

void driveMotor(int in1, int in2, bool state) {
  if (state) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }
}

void loop() {
  if (!Firebase.ready()) return;

  if (millis() - lastUpdate > updateInterval) {
    lastUpdate = millis();

    // 1. SENSÖRLERİ OKU
    float t1 = dht1.readTemperature(); float h1 = dht1.readHumidity();
    float t2 = dht2.readTemperature(); float h2 = dht2.readHumidity();
    float t3 = dht3.readTemperature(); float h3 = dht3.readHumidity(); // Merkez
    float t4 = dht4.readTemperature(); float h4 = dht4.readHumidity();
    float t5 = dht5.readTemperature(); float h5 = dht5.readHumidity();

    float s1 = mapSoilMoisture(analogRead(SOIL_1));
    float s2 = mapSoilMoisture(analogRead(SOIL_2));
    float s3 = mapSoilMoisture(analogRead(SOIL_3)); // Merkez
    float s4 = mapSoilMoisture(analogRead(SOIL_4));
    float s5 = mapSoilMoisture(analogRead(SOIL_5));
    
    // YENİ: Ana Depo Su Seviyesi (6. Toprak Nem Sensörü Okuması)
    float water_level = mapSoilMoisture(analogRead(WATER_TANK_PIN));

    float left_temp = (isnan(t1) || isnan(t2)) ? 24.0 : (t1 + t2) / 2.0;
    float left_hum  = (isnan(h1) || isnan(h2)) ? 50.0 : (h1 + h2) / 2.0;
    float left_soil = (s1 + s2 + s3/2) / 2.5;

    float right_temp = (isnan(t4) || isnan(t5)) ? 24.0 : (t4 + t5) / 2.0;
    float right_hum  = (isnan(h4) || isnan(h5)) ? 50.0 : (h4 + h5) / 2.0;
    float right_soil = (s4 + s5 + s3/2) / 2.5;

    Serial.println("--- SENSÖR VERILERI ---");
    Serial.printf("Sol  -> Isi: %.1f, Nem: %.1f, Toprak: %.1f%%\n", left_temp, left_hum, left_soil);
    Serial.printf("Sag  -> Isi: %.1f, Nem: %.1f, Toprak: %.1f%%\n", right_temp, right_hum, right_soil);
    Serial.printf("Ana Depo Su Seviyesi: %.1f%%\n", water_level);

    // 2. FİREBASE'E SENSÖR VERİLERİNİ GÖNDER
    FirebaseJson content;
    content.set("fields/left_temp/doubleValue", left_temp);
    content.set("fields/left_hum/doubleValue", left_hum);
    content.set("fields/left_soil/doubleValue", left_soil);
    content.set("fields/right_temp/doubleValue", right_temp);
    content.set("fields/right_hum/doubleValue", right_hum);
    content.set("fields/right_soil/doubleValue", right_soil);
    content.set("fields/waterLevel/doubleValue", water_level); // Su seviyesini ekledik
    
    String updateMask = "left_temp,left_hum,left_soil,right_temp,right_hum,right_soil,waterLevel";
    
    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), updateMask.c_str())) {
      Serial.println("Veriler Firebase'e basariyla gonderildi!");
    } else {
      Serial.println("Hata (Gonderim): " + fbdo.errorReason());
    }

    fbdo.clear(); 

    // 3. FİREBASE'DEN MOTOR DURUMLARINI OKU VE ÇALIŞTIR
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), "")) {
      FirebaseJson json(fbdo.payload());
      FirebaseJsonData jsonData;
      
      bool leftPump = false, rightPump = false, leftFan = false, rightFan = false;

      if (json.get(jsonData, "fields/left_pump/booleanValue")) leftPump = jsonData.boolValue;
      if (json.get(jsonData, "fields/right_pump/booleanValue")) rightPump = jsonData.boolValue;
      if (json.get(jsonData, "fields/left_fan/booleanValue")) leftFan = jsonData.boolValue;
      if (json.get(jsonData, "fields/right_fan/booleanValue")) rightFan = jsonData.boolValue;

      driveMotor(PUMP_L_IN1, PUMP_L_IN2, leftPump);
      driveMotor(PUMP_R_IN3, PUMP_R_IN4, rightPump);
      driveMotor(FAN_L_IN1, FAN_L_IN2, leftFan);
      driveMotor(FAN_R_IN3, FAN_R_IN4, rightFan);

      Serial.println("--- MOTOR DURUMLARI ---");
      Serial.printf("Sol Pompa: %d | Sag Pompa: %d\n", leftPump, rightPump);
      Serial.printf("Sol Fan: %d   | Sag Fan: %d\n", leftFan, rightFan);
      
      fbdo.clear(); 
    } else {
      Serial.println("Hata (Okuma): " + fbdo.errorReason());
      fbdo.clear(); 
    }
    Serial.println("------------------------\n");
  }
}