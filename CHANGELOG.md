# v0.2.3-dev

- Wi-Fi-only reset preserving meter assignments and full factory NVS reset.
- Typed confirmations and delayed reboot after HTTP response.
- Explicit admin credentials for custom panel routes; resets disabled without credentials.
- Host-only checks; hardware validation pending.

# v0.2.2-dev

- Add Alarm Details as a dedicated text sensor for all ten slots, exposing all 11 PRIOS alarm bits and general alarms.
- Add Stale and Leak Alarm binary sensors, Flow (estimated) L/h, and retain Radio/Printed ID text sensors.
- Correct /selectro slot dropdown resetting to Slot 1 on telemetry refresh.
- Add regression tests for flow, slot selection, and alarm text.

# v0.2.0-dev

- SAP printed number extracted from telegram; manual binding by printed number when observed.
- 10 persistent runtime slots and 40 discovered meters.
- Native API compact and optional full-flag profiles.
- PRIOS per-meter optional key, key-clear operation.
- Flag received Diehl frames as handled (success of PRIOS decryption independently tracked).
- Pinned external radio component version.
- Factory AP-first YAML; explicit warning about saved WiFi NVS.
- Host regression for captured frames / alarm status / printed ID.
