# Selectro IZAR Gateway v0.2.3 DEV — ESPHome

**Developer build; not verified on physical board or in a complete ESPHome build.**

ESP32-C3 + CC1101. Native API, 10 persistent meter assignments, up to 40 recently detected meters, /selectro onboarding panel, meter matching by current m³ reading or radio/printed ID, optional per-meter 8-byte PRIOS LFSR key, decoding of 11 alarm flags, last billing value, battery, RSSI and stale status.

This version integrates the dynamic selection ideas from the separately provided Claude receiver prototype with the Selectro multi-slot setup and independent PRIOS parser. It uses **wmbus_radio** from SzczepanLeon for actual RF reception, which was verified on the user's board in the earlier test. `izarv2` XMQ is **not ported**; the code implements a subset of common PRIOS telegrams with automatic header/field detection and must not be advertised as fully compatible with izarv2.

## ESPHome Builder installation

This is an experimental developer build, not a tested commercial release.

1. Copy `examples/selectro_izar_gateway.yaml` to `/config/esphome/selectro_izar_gateway.yaml`.
2. Merge `examples/secrets.yaml.example` fields into `/config/esphome/secrets.yaml` and replace all placeholders with unique per-device credentials. Generate a random 32-byte Base64 Native API encryption key.
3. `external_components` downloads this component from this repository automatically at build time; no local `components/` folder is necessary. The dependency on SzczepanLeon's radio code is pinned separately.
4. Run ESPHome **Validate**, then build and flash with USB only after reviewing the configuration. Keep a recovery copy of any prior firmware.

For developers editing C++ locally, substitute a local `external_components` entry pointing to `./components` in the YAML.

### Boot behaviour

No home WiFi SSID is compiled in. Captive portal AP: `Selectro IZAR Setup` -> `http://192.168.4.1`. AP fallback timeout is set to 15s for test builds. After provisioning, AP is fallback only. Previously flashed boards may still have credentials saved in NVS and auto-connect; for factory testing use full erase when suitable and save any configuration first.

At device LAN IP, open `/selectro`, authenticate using the configured web credentials, select a radio meter after hearing a telegram. Never assume a meter uniquely matches m³. Use a 10-slot selector. Removing or changing an assigned meter changes the mapping of its fixed Native API entity IDs: history in Home Assistant may need care.

### Component on GitHub

The public development branch is `Mikescotland/selectro-izar-esphome@main`:

```yaml
external_components:
  - source: github://SzczepanLeon/esphome-components@01464a25c704b7c4e7e272affbd82be23568cc74
    components: [wmbus_common, wmbus_radio]
  - source: github://Mikescotland/selectro-izar-esphome@main
    components: [selectro_izar]
```

**`main` is experimental and may change**. Pin a known-good commit SHA or release tag after actual hardware validation. A precompiled `.bin` already contains the component and does not need a GitHub download at flashing time.

### Native API entities

10 fixed slots each have Total, Previous Month, RSSI, Battery life, Last telegram age, Alarm mask, Leakage, Previous leakage, Status, Radio ID, Printed ID. `all_alarms.yaml` is an **alternate heavyweight profile** with 11 independent flags for every slot; it may exceed practical ESP32-C3 RAM budgets and requires testing. Start with the compact example. Invalid/stale telemetry returns unknown rather than fake zero; status remains diagnostic.

### Scope and constraints

- T1 PRIOS 868.95MHz, no verification of R4 868.30MHz proprietary long-range.
- The 8-byte hexadecimal PRIOS custom LFSR key is **not** an AES-128 OMS key; latter is unsupported.
- Only SAP/DME/HYD manufacturers are recognized in this version; not all variants/CI.
- This project uses GPL-3.0-or-later-derived PRIOS logic; distribution entails GPL obligations. See LICENSE and LICENSE_NOTES.md.
- Web panel uses basic authentication over HTTP and stores keys in ESPHome NVS (not secure storage). **Not hardened for public-network or product release**. Credentials unique per device. No public exposure of TCP 80 or ESPHome API.
- Custom key is *never* returned in API JSON, but physical access and device backups may expose NVS.
- The fixed wifi reset via BOOT alone is **not implemented** here; existing ESPHome factory reset clears more than WiFi.

### Validation

`make test` runs standalone PRIOS regression tests on captured/simulator telegrams. No full ESPHome build verified yet; ESPHome 2026.10.2 validation and hardware test are next gates. Test both active and historical leak flags and an unknown-key telegram.

## v0.2.2 DEV — alarm diagnostics

Each of the 10 slots exposes: Leak Alarm (binary flag decoded from PRIOS), Stale (binary, 30-minute limit), Radio ID, Printed ID, a distinct Alarm Details text entity listing all decoded flags (including sensor/mechanical fraud, reverse flow, historical flags), and Flow (estimated) in L/h. The /selectro panel retains manual slot selection during discovery refresh. If the assigned meter has no fresh valid frame, Alarm Details returns WAITING_FOR_TELEGRAM, STALE or UNDECODABLE instead of a misleading OK. **Flow (estimated) is calculated from changes in total_m3 and is not an instantaneous flow field transmitted in the supplied SAP/00/07 telegram.**

This is a DEV build. Validate with ESPHome 2026.10.2 and on the actual board before deploying to customers.


### Maintenance (v0.2.3 DEV)

Two independent actions are available in the Selectro panel. **Reset Wi-Fi only** removes the saved ESPHome station credential key (88491487, AP-first configuration in ESPHome 2026.10.2), **preserving all ten meter assignments and custom PRIOS keys**. **Factory reset** erases all ESPHome NVS preferences including Wi-Fi and meter slots, but not firmware or build-time passwords. A typed confirmation is required for each POST action. A delayed main-loop reboot is scheduled after the HTTP response.

The custom web handler does **not** inherit authentication from the built-in ESPHome web server. Configure \`selectro_izar.admin_username\` and \`selectro_izar.admin_password\` in YAML (the example uses the same secrets as the regular web server). When omitted, reset endpoints are disabled. Do not expose the DEV device to an untrusted network.

**Not yet tested** by a full ESPHome firmware compile or a real-device reset. Test on disposable configurations first. A physical-button reset has not been added.
