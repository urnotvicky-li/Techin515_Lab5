#include <Techin515_Lab4_inferencing.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

#define RED_PIN    5
#define GREEN_PIN  4
#define BLUE_PIN   10
#define BUTTON_PIN 3

#define SAMPLE_RATE_MS 10
#define FEATURE_SIZE EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE
#define CONFIDENCE_THRESHOLD 80.0

const char* ssid = "Trailside Temp";
const char* password = "trailside1";
const char* serverUrl = "http://10.3.3.13:5000/predict";

Adafruit_MPU6050 mpu;
bool capturing = false;
unsigned long last_sample_time = 0;
int sample_count = 0;
float features[FEATURE_SIZE];

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  analogWriteFrequency(RED_PIN, 5000);
  analogWriteResolution(RED_PIN, 8);
  analogWriteFrequency(GREEN_PIN, 5000);
  analogWriteResolution(GREEN_PIN, 8);
  analogWriteFrequency(BLUE_PIN, 5000);
  analogWriteResolution(BLUE_PIN, 8);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.println("Initializing MPU6050...");
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 initialized. Press button to start gesture recognition.");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW && !capturing) {
    Serial.println("Button pressed! Starting gesture capture...");
    sample_count = 0;
    capturing = true;
    last_sample_time = millis();
    delay(200);
  }

  if (capturing) {
    capture_accelerometer_data();
  }
}

void capture_accelerometer_data() {
  if (millis() - last_sample_time >= SAMPLE_RATE_MS) {
    last_sample_time = millis();

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    if (sample_count < FEATURE_SIZE / 3) {
      int idx = sample_count * 3;
      features[idx]     = a.acceleration.x;
      features[idx + 1] = a.acceleration.y;
      features[idx + 2] = a.acceleration.z;
      sample_count++;
    }

    if (sample_count >= FEATURE_SIZE / 3) {
      capturing = false;
      Serial.println("Capture complete");
      run_inference();
    }
  }
}

void run_inference() {
  ei_impulse_result_t result = { 0 };
  signal_t features_signal;
  features_signal.total_length = FEATURE_SIZE;
  features_signal.get_data = &raw_feature_get_data;

  EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);
  if (res != EI_IMPULSE_OK) {
    Serial.print("Classifier failed: ");
    Serial.println(res);
    return;
  }

  float max_val = 0;
  int max_idx = -1;
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.printf("  %s: %.2f%%\n", ei_classifier_inferencing_categories[i], result.classification[i].value * 100);
    if (result.classification[i].value > max_val) {
      max_val = result.classification[i].value;
      max_idx = i;
    }
  }

  if (max_idx < 0) return;

  String gesture = ei_classifier_inferencing_categories[max_idx];
  gesture.trim();
  gesture.toUpperCase();
  Serial.printf("Prediction: %s (%.2f%%)\n", gesture.c_str(), max_val * 100);

  if (max_val * 100 < CONFIDENCE_THRESHOLD) {
    Serial.println("Low confidence - sending raw data to server...");
    sendRawDataToServer();
  } else {
    actuateLED(gesture);
  }
}

void actuateLED(String gesture) {
  if (gesture == "Z") fadeLED(RED_PIN);
  else if (gesture == "O") fadeLED(BLUE_PIN);
  else if (gesture == "V") fadeLED(GREEN_PIN);
  else Serial.println("  → no matching gesture");
}

void fadeLED(int pin) {
  for (int i = 0; i <= 50; i++) {
    float bell = sin(PI * i / 50);
    analogWrite(pin, (int)(bell * 255));
    delay(20);
  }
  delay(200);
  for (int i = 50; i >= 0; i--) {
    float bell = sin(PI * i / 50);
    analogWrite(pin, (int)(bell * 255));
    delay(20);
  }
  analogWrite(pin, 0);
}

void sendRawDataToServer() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    String jsonPayload = "{\"features\":[";
    for (int i = 0; i < FEATURE_SIZE; i++) {
      jsonPayload += String(features[i], 6);
      if (i != FEATURE_SIZE - 1) jsonPayload += ",";
    }
    jsonPayload += "]}";

    int httpResponseCode = http.POST(jsonPayload);
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Server response: " + response);

      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, response);
      if (!error) {
        const char* gesture = doc["gesture"];
        float confidence = doc["confidence"];
        Serial.printf("Server Inference Result: %s (%.2f%%)\n", gesture, confidence);
        actuateLED(String(gesture));
      } else {
        Serial.println("Failed to parse server response");
      }
    } else {
      Serial.println("Error sending POST request");
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}