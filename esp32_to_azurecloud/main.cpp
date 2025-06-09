#define CONFIDENCE_THRESHOLD 80.0
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <a515_magic_wand_inferencing.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

// WiFi credentials 
const char* ssid = "UW MPSK";
const char* password = "X9\\r@4L!vZ";
// Server details 
const char* predictUrl = "http://10.18.215.67:8000/predict";
const char* logUrl = "http://10.18.215.67:8000/log";
// Student identifier
const char* studentId = "changlic";

// MPU6050 sensor
Adafruit_MPU6050 mpu;

#define SAMPLE_RATE_MS 10
#define CAPTURE_DURATION_MS 1000
#define FEATURE_SIZE EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE

// Button and LED pins
#define BUTTON_PIN D2
#define RED_PIN D8
#define GREEN_PIN D9
#define BLUE_PIN D6

// Feature array and state variables
float features[FEATURE_SIZE];
bool capturing = false;
unsigned long last_sample_time = 0;
unsigned long capture_start_time = 0;
int sample_count = 0;
bool button_was_pressed = false;

void sendGestureToServer(const char* gesture, float confidence);  // <- Add this

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}

void setLEDColor(bool r, bool g, bool b) {
    digitalWrite(RED_PIN, r ? HIGH : LOW);
    digitalWrite(GREEN_PIN, g ? HIGH : LOW);
    digitalWrite(BLUE_PIN, b ? HIGH : LOW);
}

void setupWiFi() {
    Serial.println("🔄 Connecting to WiFi...");
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }

    Serial.println("\n✅ Connected to WiFi!");
    Serial.print("📡 IP Address: ");
    Serial.println(WiFi.localIP());
}

void sendGestureToServer(const char* gesture, float confidence) {
    String jsonPayload = "{";
    jsonPayload += "\"student_id\":\"" + String(studentId) + "\",";
    jsonPayload += "\"gesture\":\"" + String(gesture) + "\",";
    jsonPayload += "\"confidence\":" + String(confidence);
    jsonPayload += "}";

    Serial.println("\n--- Sending Prediction to Server ---");
    Serial.println("URL: " + String(logUrl));
    Serial.println("Payload: " + jsonPayload);

    HTTPClient http;
    http.begin(logUrl);
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonPayload);
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Server response: " + response);
    } else {
        Serial.printf("Error sending POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
    Serial.println("--- End of Request ---\n");
}


void sendRawDataToServer() {
    HTTPClient http;
    http.begin(predictUrl);
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");

    // Build the correct JSON payload
    JsonDocument doc;
    JsonArray data = doc["data"].to<JsonArray>();  // ✅ Fix: Flask expects "data", not "features"
    for (int i = 0; i < FEATURE_SIZE; i++) {
        data.add(features[i]);
    }

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    Serial.println("\n--- Sending Raw Data to Server ---");
    Serial.println("URL: " + String(predictUrl));
    Serial.println("Payload: " + jsonPayload);

    int httpResponseCode = http.POST(jsonPayload);
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Server response: " + response);

        JsonDocument resDoc;
        DeserializationError error = deserializeJson(resDoc, response);
        if (!error && resDoc.containsKey("gesture") && resDoc.containsKey("confidence")) {
            const char* gesture = resDoc["gesture"];
            float confidence = resDoc["confidence"];

            Serial.println("Server Inference Result:");
            Serial.print("Gesture: ");
            Serial.println(gesture);
            Serial.print("Confidence: ");
            Serial.print(confidence);
            Serial.println("%");

            // Actuate LED based on gesture
            if (strcmp(gesture, "Z") == 0) {
                setLEDColor(true, false, false);
            } else if (strcmp(gesture, "V") == 0) {
                setLEDColor(false, true, false);
            } else if (strcmp(gesture, "O") == 0) {
                setLEDColor(false, false, true);
            } else {
                setLEDColor(false, false, false);
            }

            sendGestureToServer(gesture, confidence);  // Optionally forward to logging endpoint
        } else {
            Serial.print("❌ Failed to parse or missing keys in response: ");
            Serial.println(error.c_str());
        }
    } else {
        Serial.printf("Error sending POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    Serial.println("--- End of Request ---\n");
}


void print_inference_result(ei_impulse_result_t result) {
    float max_value = 0;
    int max_index = -1;

    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value > max_value) {
            max_value = result.classification[i].value;
            max_index = i;
        }
    }

    if (max_index != -1) {
        const char* label = ei_classifier_inferencing_categories[max_index];
        float confidence = max_value * 100;

        Serial.print("Prediction: ");
        Serial.print(label);
        Serial.print(" (");
        Serial.print(confidence);
        Serial.println("%)");

        // 🔁 Cloud-edge offloading logic
        if (confidence < CONFIDENCE_THRESHOLD) {
            Serial.println("Low confidence - sending raw data to server...");
            sendRawDataToServer();
        } else {
            if (strcmp(label, "Z") == 0) {
                setLEDColor(true, false, false);
            } else if (strcmp(label, "V") == 0) {
                setLEDColor(false, true, false);
            } else if (strcmp(label, "O") == 0) {
                setLEDColor(false, false, true);
            } else {
                setLEDColor(false, false, false);
            }
            sendGestureToServer(label, confidence);
        }
    }
}


void run_inference() {
  if (sample_count * 3 < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
      Serial.println("ERROR: Not enough data for inference");
      return;
  }

  ei_impulse_result_t result = { 0 };
  signal_t features_signal;
  features_signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  features_signal.get_data = &raw_feature_get_data;

  EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);
  if (res != EI_IMPULSE_OK) {
      Serial.print("ERR: Failed to run classifier (");
      Serial.print(res);
      Serial.println(")");
      return;
  }
  print_inference_result(result);
}


void capture_accelerometer_data() {
    
  if (millis() - last_sample_time >= SAMPLE_RATE_MS) {
      last_sample_time = millis();
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);

      if (sample_count < FEATURE_SIZE / 3) {
          int idx = sample_count * 3;
          features[idx] = a.acceleration.x;
          features[idx + 1] = a.acceleration.y;
          features[idx + 2] = a.acceleration.z;
          sample_count++;
      }

      if (millis() - capture_start_time >= CAPTURE_DURATION_MS) {
          capturing = false;
          Serial.println("Capture complete");
          Serial.print("First feature value: ");
          Serial.println(features[0]);
          run_inference();
          delay(800);
          setLEDColor(false, false, false);
      }
  }
}

void setup() {
    Serial.begin(115200);
    setupWiFi();

    Serial.println("Initializing MPU6050...");
    if (!mpu.begin()) {
        Serial.println("Failed to find MPU6050 chip");
        while (1) delay(10);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 initialized successfully");
    Serial.println("Press button to start gesture capture");

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BLUE_PIN, OUTPUT);
    setLEDColor(false, false, false);
}



void loop() {
    bool button_pressed = digitalRead(BUTTON_PIN) == LOW;

    if (button_pressed && !button_was_pressed && !capturing) {
        Serial.println("Button pressed! Starting gesture capture...");
        sample_count = 0;
        memset(features, 0, sizeof(features)); // ✅ Reset only ONCE at the beginning
        capturing = true;
        capture_start_time = millis();
        last_sample_time = millis();
        setLEDColor(true, true, true);
    }
    button_was_pressed = button_pressed;

    if (capturing) {
        capture_accelerometer_data();
    }
}
