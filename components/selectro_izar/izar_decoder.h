#pragma once
// Selectro PRIOS decoder. Standalone, dependency-free for host-side regression tests.
// Reference: wmbusmeters decodeDiehlLfsr() + izar driver / izarv2.xmq.
// GPL-3.0-or-later derived algorithm; see LICENSE_NOTES.md.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome { namespace selectro_izar {

struct IzarReading {
  bool valid{false};
  uint32_t id{0};
  uint16_t manufacturer{0};
  uint8_t ci{0};
  uint8_t version{0};
  uint8_t device_type{0};
  uint32_t total_l{0};
  uint32_t target_l{0};
  bool has_target{false};
  int year{0};
  int month{0};
  int day{0};
  float battery_y{0};
  uint32_t transmit_s{0};
  uint16_t alarms{0}; // Bits 0..10 per docs; see flags below.
  bool general_alarm{false};
  std::string printed_number;  // SAP/Diehl printed meter identifier if recoverable
  std::string decoder;  // legacy-prios or extended-prios (not full izarv2 XMQ)
  std::string error;
};

enum IzarAlarm : uint16_t {
  LEAKAGE=1<<0, PREV_LEAKAGE=1<<1, METER_BLOCKED=1<<2,
  BACK_FLOW=1<<3, UNDERFLOW=1<<4, OVERFLOW=1<<5,
  SUBMARINE=1<<6, SENSOR_FRAUD=1<<7, PREV_SENSOR_FRAUD=1<<8,
  MECHANICAL_FRAUD=1<<9, PREV_MECHANICAL_FRAUD=1<<10
};

// key_hex: nullptr/empty = try both known PRIOS factory keys; otherwise
// exactly 16 hex chars (8 bytes). AES-128 is NOT supported by this decoder.
IzarReading decode_izar_prios(const std::vector<uint8_t>& frame,
                              const char *key_hex = nullptr);
std::string alarms_text(const IzarReading &r);
bool valid_prios_key(const std::string &s);

} } // namespace
