# tally-light — Design Notes

A device that lights an LED when either (a) an external USB webcam is actively capturing, or (b) Google Calendar shows busy, a "tally light" for being on camera or in a meeting.

## Architecture

```
[USB webcam] --power line--> [INA219 current sensor] --I2C--> [ESP32] --GPIO--> [LED]
                                                                   ^
                                                                   | HTTP GET /calendar?state=on|off
                                                                   |
                                                          [Docker container on antares]
                                                                   |
                                                          polls Google Calendar (iCal feed)
```

ESP32 combines both signals with OR logic: LED is on if camera is active or calendar shows busy.

## Key decisions

### Camera detection: current sensing, not phototransistor or software polling

- Software polling (`fuser /dev/video*`) was the original plan, but the webcam in question connects to a work laptop, where running detection scripts isn't an option (IT-managed machine).
- A phototransistor on the camera's status LED was considered (most webcams light an LED when actively streaming, hard to fake in firmware) but rejected because the build should leave the LED itself visible, not occluded by a sensor housing.
- Landed on current sensing via INA219: splice the USB cable's V+ line through an INA219 breakout, read current draw over I2C from the ESP32. Requires calibration per camera since idle vs. active current draw varies by webcam model (could be a 20mA gap or 150mA gap). Calibrate by logging `getCurrent_mA()` in both states and setting the threshold at the midpoint, not pinned close to either baseline.

### Calendar detection: iCal secret URL, not OAuth

- Work calendar lives on a managed Google Workspace account where self-serve OAuth app creation/consent is often locked down, so it couldn't reliably be built around the Calendar API with a standard OAuth flow against the work account.
- Resolution: shared the work calendar's busy/free status into a personal Google account (`joe.sanford@bighealth.com` now appears as a subscribed "Other calendar" with free/busy-only blocks, no event details, good for privacy too).
- Polling happens against the personal account using the standard Calendar API (`freebusy().query`), checking both `primary` and the work calendar ID. No work-machine involvement at all.
- (iCal secret-address polling was an earlier fallback considered before confirming OAuth would work fine against the personal account. Left here as a noted alternative if OAuth setup ever becomes a hassle.)

### Where the poller runs: antares (Docker), not the ESP32

- ESP32 could do HTTPS plus iCal parsing itself, but it's a real squeeze: TLS handshake overhead, no clean JSON-style parsing for raw `.ics` text, NTP needed for `now()`, all on a RAM-constrained chip that's also running a web server and I2C polling. Fragile, hard to debug over serial.
- antares is already an always-on box running the rest of the homelab stack, so the marginal cost of one more lightweight container is close to zero, and it's far easier to debug (`docker logs`) than chasing a flaky ESP32 TLS stack.
- Division of responsibility: ESP32 stays dumb (I2C sensor read, GPIO write, tiny HTTP server exposing `/calendar?state=on|off`). antares container owns all networking/auth complexity and pushes state changes to the ESP32 over LAN.

## Hardware

- ESP32-DevKitC (ESP32-32U variant, has external antenna connector, unused here since onboard antenna is fine on the home LAN)
- INA219 current/voltage sensor breakout (I2C: SDA to 21, SCL to 22 on DevKitC)
- LED on GPIO2 (matches onboard LED for early bring-up testing) through a ~220Ω resistor
- USB extension cable, spliced on the red (V+) wire only, routed through INA219 `VIN+`/`VIN-` in series with the camera's power line

## Deployment

- Firmware: Arduino, flashed via USB-serial (CP2102/CH340, CachyOS needs `cp210x`/`ch341` kernel modules, usually present by default)
- Poller: Python (`requests`, or `icalendar` if the iCal fallback is used), containerized, env vars for `ESP32_IP` and calendar credentials (kept out of the image via Unraid container config, not baked into the image layer history)
- Restart policy: `unless-stopped`

## Open items

- Confirm INA219 current draw gap (idle vs. active) once the webcam is wired in. Threshold is unset until calibration data exists.
- Decide local build vs. GHCR image for the antares container (local is fine to start, GHCR gives a cleaner `docker pull` update path later).
