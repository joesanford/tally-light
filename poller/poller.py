"""Polls Google Calendar for currently active events and pushes on/off state
to the ESP32's /calendar endpoint. Runs against a personal Google account that
the work calendar's busy/free blocks are shared into (see
docs/design-notes.md) for why this avoids OAuth against a locked-down work
Workspace account.

Uses the Events API rather than freeBusy: freeBusy can't distinguish an
all-day event from a real meeting, both show up as an opaque busy block
spanning the whole day, so it would light up the tally light all day for
things like a holiday or an out-of-office marker. Events have a start.date
(all-day) or start.dateTime (timed) field, which lets us filter all-day
events out explicitly.
"""

import datetime
import os
import time
import urllib.parse

import requests
from google.auth.transport.requests import Request as AuthRequest
from google.oauth2.credentials import Credentials

EVENTS_URL_TEMPLATE = "https://www.googleapis.com/calendar/v3/calendars/{calendar_id}/events"
SCOPES = ["https://www.googleapis.com/auth/calendar.events.readonly"]


def load_credentials(token_path):
    creds = Credentials.from_authorized_user_file(token_path, SCOPES)
    if creds.expired and creds.refresh_token:
        creds.refresh(AuthRequest())
        with open(token_path, "w") as f:
            f.write(creds.to_json())
    return creds


def has_active_event(creds, calendar_id, now):
    url = EVENTS_URL_TEMPLATE.format(calendar_id=urllib.parse.quote(calendar_id, safe=""))
    resp = requests.get(
        url,
        headers={"Authorization": f"Bearer {creds.token}"},
        params={
            "timeMin": (now - datetime.timedelta(seconds=30)).isoformat(),
            "timeMax": (now + datetime.timedelta(seconds=30)).isoformat(),
            "singleEvents": "true",
            "showDeleted": "false",
        },
        timeout=10,
    )
    resp.raise_for_status()
    for event in resp.json().get("items", []):
        if event.get("status") == "cancelled":
            continue
        if "dateTime" not in event.get("start", {}):
            continue  # all-day event, ignore
        if event.get("transparency") == "transparent":
            continue  # explicitly marked "free"
        return True
    return False


def is_busy(creds, calendar_ids):
    now = datetime.datetime.now(datetime.timezone.utc)
    return any(has_active_event(creds, cal_id, now) for cal_id in calendar_ids)


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
