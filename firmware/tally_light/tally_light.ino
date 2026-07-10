#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

#include "config.h"

Adafruit_INA219 ina219;
WebServer server(80);

enum ManualOverride { AUTO, FORCE_ON, FORCE_OFF };

bool cameraActive = false;
bool calendarBusy = false;
ManualOverride manualOverride = AUTO;

const unsigned long CAMERA_POLL_INTERVAL_MS = 1000;
unsigned long lastCameraPoll = 0;

bool ledShouldBeOn() {
  // Priority: manual override, then camera, then calendar.
  if (manualOverride == FORCE_ON) return true;
  if (manualOverride == FORCE_OFF) return false;
  if (cameraActive) return true;
  return calendarBusy;
}

void updateLed() {
  digitalWrite(LED_PIN, ledShouldBeOn() ? HIGH : LOW);
}

void pollCamera() {
  float current_mA = ina219.getCurrent_mA();
  cameraActive = current_mA >= CURRENT_THRESHOLD_MA;
  Serial.printf("current_mA=%.2f cameraActive=%d calendarBusy=%d manualOverride=%d led=%d\n",
                current_mA, cameraActive, calendarBusy, manualOverride, ledShouldBeOn());
}

void handleCalendar() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "missing state param");
    return;
  }

  String state = server.arg("state");
  if (state == "on") {
    calendarBusy = true;
  } else if (state == "off") {
    calendarBusy = false;
  } else {
    server.send(400, "text/plain", "state must be 'on' or 'off'");
    return;
  }

  Serial.printf("received /calendar?state=%s -> calendarBusy=%d\n", state.c_str(), calendarBusy);
  updateLed();
  server.send(200, "text/plain", "ok");
}

void handleManual() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "missing state param");
    return;
  }

  String state = server.arg("state");
  if (state == "on") {
    manualOverride = FORCE_ON;
  } else if (state == "off") {
    manualOverride = FORCE_OFF;
  } else if (state == "auto") {
    manualOverride = AUTO;
  } else {
    server.send(400, "text/plain", "state must be 'on', 'off', or 'auto'");
    return;
  }

  Serial.printf("received /manual?state=%s -> manualOverride=%d\n", state.c_str(), manualOverride);
  updateLed();
  server.send(200, "text/plain", "ok");
}

void handleNotFound() {
  server.send(404, "text/plain", "not found");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
  }

  Serial.println("Scanning for networks...");
  int networkCount = WiFi.scanNetworks();
  for (int i = 0; i < networkCount; i++) {
    Serial.printf("  %s (RSSI %d, channel %d)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.printf(" [status=%d]", WiFi.status());
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());

  server.on("/calendar", handleCalendar);
  server.on("/manual", handleManual);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastCameraPoll >= CAMERA_POLL_INTERVAL_MS) {
    lastCameraPoll = now;
    pollCamera();
    updateLed();
  }
}
