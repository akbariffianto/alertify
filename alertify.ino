#define BLYNK_TEMPLATE_ID "TMPL6Q8sVad0B"
#define BLYNK_TEMPLATE_NAME "GasFlame Detector"
#define BLYNK_AUTH_TOKEN "3dyoZpGf7hQA5M322_q1Z6G74XXgYQHx"

#include <Wire.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <MQUnifiedsensor.h>
#include <FirebaseESP32.h>

// Firebase Configuration
#define FIREBASE_HOST "https://fire-detection-iot2024-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "Mh7K1tjnTbeFWmLEs51woHEBEfabVxvfQMEMysfZ"

// WiFi credentials
char ssid[] = "Redmi 12C";
char pass[] = "akbar004";

// Firebase objects
FirebaseData fbdo;
FirebaseConfig config;
FirebaseAuth auth;

// MQ2 Configuration
#define Board ("ESP-32")
#define Pin (34)  // Use GPIO36 (ADC1_CH0) for analog input
#define Type ("MQ-2")
#define Voltage_Resolution (3.3)
#define ADC_Bit_Resolution (12)
#define RatioMQ2CleanAir (9.83)

// Create MQ2 object
MQUnifiedsensor MQ2(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);

// Pin Configuration
#define buzzer 2
#define flameSensorPin 4
#define ledPinBiru 18
#define ledPinMerah 19
#define servoPin 5

Servo myServo;
BlynkTimer timer;

// Blynk LED Widget
WidgetLED LED(V1);

// Sensor readings
float co = 0;
float lpg = 0;
int flameValue = 1;
bool buzzerEnabled = true; // Variable to track buzzer status

// Function prototypes
void setupMQ2();
void calibrateMQ2();
void readSensors();
void handleAlarm();
void sendToFirebase();
void monitorSensors();

// Blynk function to control buzzer
BLYNK_WRITE(V4) {
  int buttonState = param.asInt(); // Get the value from the button (0 or 1)
  if (buttonState == 0) {
    buzzerEnabled = false; // Disable the buzzer
    digitalWrite(buzzer, LOW); // Turn off the buzzer
  } else {
    buzzerEnabled = true; // Enable the buzzer
  }
}

void setup() {
  Serial.begin(115200);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 80);

  pinMode(buzzer, OUTPUT);
  pinMode(flameSensorPin, INPUT);
  pinMode(ledPinBiru, OUTPUT);
  pinMode(ledPinMerah, OUTPUT);

  myServo.attach(servoPin);
  myServo.write(0);

  setupMQ2();
  calibrateMQ2();

  // Firebase setup
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);

  if (!Firebase.ready()) {
    Serial.println("Koneksi Firebase gagal!");
    while (true) {
      delay(1000);
    }
  }

  timer.setInterval(500L, monitorSensors);
}

void setupMQ2() {
  MQ2.setRegressionMethod(1);
  MQ2.setA(1052.47); // LPG-specific "a" constant
  MQ2.setB(-2.273);  // LPG-specific "b" constant
  MQ2.init();
}

void calibrateMQ2() {
  Serial.print("Calibrating MQ2 sensor...");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ2.update();
    calcR0 += MQ2.calibrate(RatioMQ2CleanAir);
    Serial.print(".");
  }
  MQ2.setR0(calcR0 / 10);
  Serial.println("done!");
  if (isinf(calcR0) || calcR0 == 0) {
    Serial.println("MQ2 sensor calibration failed. Check connections!");
    while (true);
  }
}

void readSensors() {
  MQ2.update();
  float voltage = (analogRead(Pin) / 4095.0) * Voltage_Resolution;
  float Rs = (Voltage_Resolution - voltage) / voltage;
  float ratio = Rs / MQ2.getR0();
  co = MQ2.getA() * pow(ratio, MQ2.getB());
  lpg = MQ2.readSensor();
  flameValue = digitalRead(flameSensorPin);
}

void sendNotification(String message) {
  Blynk.logEvent("ada_kebocoran_gas", message);
}

void handleAlarm() {
  if (!buzzerEnabled) {
    digitalWrite(buzzer, LOW);
    return;
  }

  digitalWrite(buzzer, LOW);
  digitalWrite(ledPinBiru, LOW);
  digitalWrite(ledPinMerah, LOW);
  myServo.write(0);

  if (flameValue == 0 && co >= 6000) {
    digitalWrite(buzzer, HIGH);
    myServo.write(90);
    for (int i = 0; i < 5; i++) {
      digitalWrite(ledPinMerah, HIGH);
      delay(250);
      digitalWrite(ledPinMerah, LOW);
      delay(250);
    }
    sendNotification("Peringatan! Kebakaran terdeteksi.");
  } else if (flameValue == 0) {
    digitalWrite(buzzer, HIGH);
    sendNotification("Peringatan! Api terdeteksi.");
  } else if (co >= 4000 && flameValue == 1) {
    digitalWrite(buzzer, HIGH);
    digitalWrite(ledPinMerah, HIGH);
    myServo.write(90);
    sendNotification("Peringatan! Asap tebal terdeteksi.");
  } else if (lpg >= 10 && lpg <= 30 && flameValue == 1) {
    digitalWrite(buzzer, HIGH);
    digitalWrite(ledPinBiru, HIGH);
    myServo.write(90);
    sendNotification("Peringatan! Kebocoran gas terdeteksi.");
  }
}

void sendToFirebase() {
  if (Firebase.setFloat(fbdo, "/Nilai CO", co)) {
    Serial.println("Data Nilai CO dikirim ke Firebase.");
  } else {
    Serial.print("Gagal kirim data Nilai CO: ");
    Serial.println(fbdo.errorReason());
  }

  if (Firebase.setFloat(fbdo, "/Nilai LPG", lpg)) {
    Serial.println("Data Nilai LPG dikirim ke Firebase.");
  } else {
    Serial.print("Gagal kirim data Nilai LPG: ");
    Serial.println(fbdo.errorReason());
  }

  String kondisi;
  if (co >= 3000 && flameValue == 0) {
    kondisi = "Ada Kebakaran";
  } else if (co >= 3000 && flameValue == 1) {
    kondisi = "Ada Asap";
  } else if (lpg >= 10) {
    kondisi = "Ada Kebocoran Gas LPG";
  } else {
    kondisi = "Aman";
  }

  if (Firebase.setString(fbdo, "/Kondisi", kondisi)) {
    Serial.println("Kondisi: " + kondisi);
  } else {
    Serial.print("Gagal kirim data Kondisi: ");
    Serial.println(fbdo.errorReason());
  }
}

void monitorSensors() {
  readSensors();
  handleAlarm();
  sendToFirebase();

  Blynk.virtualWrite(V2, co);
  Blynk.virtualWrite(V1, flameValue);
  Blynk.virtualWrite(V0, lpg);
}

void loop() {
  Blynk.run();
  timer.run();
}
