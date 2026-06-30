"""Polls Google Calendar free/busy status and pushes on/off state to the
ESP32's /calendar endpoint. Runs against a personal Google account that the
work calendar's busy/free blocks are shared into (see docs/design-notes.md)
for why this avoids OAuth against a locked-down work Workspace account.
"""

import datetime
import os
import time

import requests
from google.auth.transport.requests import Request as AuthRequest
from google.oauth2.credentials import Credentials

FREEBUSY_URL = "https://www.googleapis.com/calendar/v3/freeBusy"
SCOPES = ["https://www.googleapis.com/auth/calendar.freebusy"]


def load_credentials(token_path):
    creds = Credentials.from_authorized_user_file(token_path, SCOPES)
    if creds.expired and creds.refresh_token:
        creds.refresh(AuthRequest())
        with open(token_path, "w") as f:
            f.write(creds.to_json())
    return creds


def is_busy(creds, calendar_ids):
    now = datetime.datetime.now(datetime.timezone.utc)
    body = {
        "timeMin": now.isoformat(),
        "timeMax": (now + datetime.timedelta(minutes=1)).isoformat(),
        "items": [{"id": cal_id} for cal_id in calendar_ids],
    }
    resp = requests.post(
        FREEBUSY_URL,
        json=body,
        headers={"Authorization": f"Bearer {creds.token}"},
        timeout=10,
    )
    resp.raise_for_status()
    calendars = resp.json().get("calendars", {})
    return any(cal.get("busy") for cal in calendars.values())


def push_state(esp32_ip, busy):
    state = "on" if busy else "off"
    resp = requests.get(
        f"http://{esp32_ip}/calendar",
        params={"state": state},
        timeout=5,
    )
    resp.raise_for_status()


def main():
    esp32_ip = os.environ["ESP32_IP"]
    calendar_ids = os.environ.get("CALENDAR_IDS", "primary").split(",")
    token_path = os.environ.get("TOKEN_PATH", "/secrets/token.json")
    poll_interval = int(os.environ.get("POLL_INTERVAL_SECONDS", "60"))

    last_state = None
    while True:
        try:
            creds = load_credentials(token_path)
            busy = is_busy(creds, calendar_ids)
            if busy != last_state:
                push_state(esp32_ip, busy)
                last_state = busy
                print(f"calendar state -> {'on' if busy else 'off'}", flush=True)
        except Exception as exc:
            print(f"poll failed: {exc}", flush=True)
        time.sleep(poll_interval)


if __name__ == "__main__":
    main()
