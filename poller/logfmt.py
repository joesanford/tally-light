"""logfmt-style logging: `<UTC timestamp> level=INFO component=poller msg="..." key=val`.

Timestamp leads bare (no ts= key) so it's the first thing the eye hits when
tailing `docker logs`, matching the format used by the firmware's serial log.
"""

import logging
import sys
import time

# Attributes present on every LogRecord regardless of what's passed via
# `extra=`; anything else on the record is a caller-supplied structured field.
_STANDARD_ATTRS = frozenset(logging.LogRecord("", 0, "", 0, "", (), None).__dict__)


def _quote(value):
    text = str(value)
    if " " in text or '"' in text:
        return '"{}"'.format(text.replace('"', '\\"'))
    return text


class LogfmtFormatter(logging.Formatter):
    converter = time.gmtime  # timestamps in UTC, not local time

    def __init__(self, component):
        super().__init__()
        self.component = component

    def format(self, record):
        parts = [self.formatTime(record, "%Y-%m-%dT%H:%M:%SZ")]
        parts.append("level=" + record.levelname)
        parts.append("component=" + self.component)
        parts.append("msg=" + _quote(record.getMessage()))
        for key, value in record.__dict__.items():
            if key not in _STANDARD_ATTRS:
                parts.append("{}={}".format(key, _quote(value)))
        if record.exc_info:
            parts.append("exc_info=" + _quote(self.formatException(record.exc_info)))
        return " ".join(parts)


def configure_logging(component, level=logging.INFO):
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(LogfmtFormatter(component))
    root = logging.getLogger()
    root.handlers = [handler]
    root.setLevel(level)
