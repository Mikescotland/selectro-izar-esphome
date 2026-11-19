# Repository publication and release checklist

Project: https://github.com/Mikescotland/selectro-izar-esphome

Development branch: `main` (not a production release).

- [x] Strip device credentials; only `examples/secrets.yaml.example` is public.
- [x] Include GPL-3.0 license and provenance notes.
- [x] Host PRIOS regression checks pass.
- [ ] Verify a **full ESPHome 2026.10.2 compile** with both selected upstream components.
- [ ] Test initial Wi-Fi provisioning, AP fallback, and web auth on real ESP32-C3 hardware.
- [ ] Test 10 concurrent meter assignments, retention after reboot, Native API and OTA.
- [ ] Check device RAM/flash budget, PRIOS variants, and negative cases.
- [ ] Audit GPL obligations and firmware hardening for commercial distribution.
- [ ] Tag a tested immutable release (e.g. `v0.2.0-dev`) **only after passing the gates**.

The YAML uses `@main` until a validated tag exists. Clients receiving prebuilt firmware do not download ESPHome C++ dependencies.
