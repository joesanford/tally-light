# tally-light
Camera-on/busy indicator. See [docs/design-notes.md](docs/design-notes.md) for the full design.

## Layout

- `firmware/tally_light/`: ESP32 Arduino sketch that reads the INA219 current sensor over I2C, runs a tiny HTTP server (`GET /calendar?state=on|off`), and drives the LED on camera-active OR calendar-busy.
- `poller/`: Python container that polls Google Calendar free/busy on a personal account and pushes state to the ESP32 over LAN.

## Quick start

### Firmware

1. Install the Arduino IDE (or PlatformIO) with ESP32 board support, plus the **Adafruit INA219** library.
2. Copy `firmware/tally_light/config.h.example` to `config.h` and fill in Wi-Fi credentials.
3. Wire INA219 SDA/SCL to GPIO21/22, LED to GPIO2 through ~220Ω, INA219 in series with the webcam's USB V+ line.
4. Flash `tally_light.ino`. `CURRENT_THRESHOLD_MA` in `config.h` is unset until you calibrate it (log `getCurrent_mA()` idle vs. active and use the midpoint).

### Poller

1. `cd poller && cp .env.example .env`, fill in `ESP32_IP` and `CALENDAR_IDS`.
2. Create an OAuth client (Desktop app type) in Google Cloud Console for your personal account, download it as `credentials.json`.
3. Run `python authorize.py` locally to produce `token.json`, then move it to `poller/secrets/token.json`.
4. `docker compose up -d --build`.
