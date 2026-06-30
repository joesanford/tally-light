"""One-time helper: run this locally, NOT in the container, to produce token.json.

Needs an OAuth client of type "Desktop app" downloaded as credentials.json from
console.cloud.google.com.

Usage:
    python authorize.py [path/to/credentials.json]
"""

import sys

from google_auth_oauthlib.flow import InstalledAppFlow

SCOPES = ["https://www.googleapis.com/auth/calendar.freebusy"]


def main():
    credentials_path = sys.argv[1] if len(sys.argv) > 1 else "credentials.json"
    flow = InstalledAppFlow.from_client_secrets_file(credentials_path, SCOPES)
    creds = flow.run_local_server(port=0)
    with open("token.json", "w") as f:
        f.write(creds.to_json())
    print("Wrote token.json, mount this into the poller container.")


if __name__ == "__main__":
    main()
