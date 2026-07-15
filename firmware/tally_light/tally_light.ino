#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <time.h>

#include "config.h"

Adafruit_INA219 ina219;
WebServer server(80);

enum ManualOverride { AUTO, FORCE_ON, FORCE_OFF };

bool cameraActive = false;
bool calendarBusy = false;
ManualOverride manualOverride = AUTO;

const unsigned long CAMERA_POLL_INTERVAL_MS = 1000;
unsigned long lastCameraPoll = 0;

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#endif

enum LogLevel { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

const char* levelName(LogLevel level) {
  switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
  }
  return "INFO";
}

// logfmt-style line: `<UTC timestamp> level=INFO component=firmware msg="..." key=val`,
// matching the poller's log format so `docker logs` and the serial monitor read the same way.
// Falls back to the epoch if NTP hasn't synced yet (see setup()).
void logLine(LogLevel level, const char* msg, const char* fieldsFmt = "", ...) {
  if (level < LOG_LEVEL) return;

  char fields[128];
  va_list args;
  va_start(args, fieldsFmt);
  vsnprintf(fields, sizeof(fields), fieldsFmt, args);
  va_end(args);

  char ts[25] = "1970-01-01T00:00:00Z";
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10)) {
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  }

  Serial.print(ts);
  Serial.print(" level=");
  Serial.print(levelName(level));
  Serial.print(" component=firmware msg=\"");
  Serial.print(msg);
  Serial.print('"');
  if (fields[0] != '\0') {
    Serial.print(' ');
    Serial.print(fields);
  }
  Serial.println();
}

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
  logLine(LOG_DEBUG, "camera poll",
          "current_mA=%.2f camera_active=%d calendar_busy=%d manual_override=%d led=%d",
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
    logLine(LOG_WARN, "bad /calendar request", "state=%s", state.c_str());
    server.send(400, "text/plain", "state must be 'on' or 'off'");
    return;
  }

  logLine(LOG_INFO, "calendar state changed", "state=%s", state.c_str());
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
    logLine(LOG_WARN, "bad /manual request", "state=%s", state.c_str());
    server.send(400, "text/plain", "state must be 'on', 'off', or 'auto'");
    return;
  }

  logLine(LOG_INFO, "manual override changed", "state=%s", state.c_str());
  updateLed();
  server.send(200, "text/plain", "ok");
}

void handleNotFound() {
  logLine(LOG_WARN, "unknown request", "uri=%s method=%d", server.uri().c_str(), server.method());
  server.send(404, "text/plain", "not found");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ina219.begin()) {
    logLine(LOG_WARN, "INA219 not found");
  }

  logLine(LOG_DEBUG, "scanning for networks");
  int networkCount = WiFi.scanNetworks();
  for (int i = 0; i < networkCount; i++) {
    logLine(LOG_DEBUG, "network found", "ssid=%s rssi=%d channel=%d",
            WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
  }

  logLine(LOG_INFO, "wifi connecting", "ssid=%s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long lastWifiLog = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - lastWifiLog >= 5000) {
      logLine(LOG_WARN, "wifi still connecting", "status=%d", WiFi.status());
      lastWifiLog = millis();
    }
  }
  logLine(LOG_INFO, "wifi connected", "ip=%s", WiFi.localIP().toString().c_str());

  // Wall-clock timestamps for the log lines below (and every logLine() call
  // after this point) depend on NTP; getLocalTime() falls back to the epoch
  // until this succeeds.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10000)) {
    logLine(LOG_INFO, "ntp synced");
  } else {
    logLine(LOG_ERROR, "ntp sync failed, timestamps will read 1970-01-01 until it recovers");
  }

  server.on("/calendar", handleCalendar);
  server.on("/manual", handleManual);
  server.onNotFound(handleNotFound);
  server.begin();
  logLine(LOG_INFO, "http server started", "port=80");
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
